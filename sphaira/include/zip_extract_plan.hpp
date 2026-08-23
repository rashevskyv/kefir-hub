#pragma once

#include "path_util.hpp"

#include <string>
#include <string_view>
#include <span>
#include <vector>

namespace sphaira::zip_extract {

inline constexpr std::string_view kDownloadsDir = "/downloads";
inline constexpr std::string_view kSwitchDir = "/switch";

inline auto FileStem(std::string_view path_or_name) -> std::string {
    const auto base = path::ExtractBasename(path_or_name);
    const auto ext = path::Extension(base);
    if (ext.empty() || ext.size() >= base.size()) {
        return std::string{base};
    }
    return std::string{base.substr(0, base.size() - ext.size() - 1)};
}

inline auto SafeFolderName(std::string_view raw) -> std::string {
    if (!path::IsSafeFilename(raw) || raw.empty()) {
        return "download";
    }
    return std::string{raw};
}

inline auto NormalizeZipEntry(std::string_view raw) -> std::string {
    std::string n;
    n.reserve(raw.size());
    for (const auto c : raw) {
        n += (c == '\\') ? '/' : c;
    }

    std::size_t i = 0;
    while (i < n.size() && n[i] == '/') {
        i++;
    }
    if (i + 1 < n.size() && n[i] == '.' && n[i + 1] == '/') {
        i += 2;
    }
    return n.substr(i);
}

// Default extract directory for a downloaded zip.
// - more than one folder at the archive root → /downloads/<archive_stem>
// - exactly one .nro sitting in a folder (one root folder) → /switch
//   (if that folder is already "switch", extract to /)
// - exactly one naked .nro at the archive root → /switch/<nro_stem>
// - anything else → /downloads
inline auto SuggestExtractPath(std::span<const std::string_view> names, std::string_view archive_stem) -> std::string {
    std::vector<std::string> root_folders;
    auto add_root_folder = [&](std::string_view comp) {
        if (comp.empty() || comp == "." || comp == "..") {
            return;
        }
        for (const auto& f : root_folders) {
            if (path::EqualsIC(f, comp)) {
                return;
            }
        }
        root_folders.emplace_back(comp);
    };

    int nro_root = 0;
    int nro_nested = 0;
    std::string root_nro;

    for (const auto raw : names) {
        auto n = NormalizeZipEntry(raw);
        if (n.empty()) {
            continue;
        }

        const bool is_dir = n.back() == '/';
        if (is_dir) {
            n.pop_back();
            if (n.empty()) {
                continue;
            }
        }

        const auto slash = n.find('/');
        if (slash != std::string::npos) {
            add_root_folder(n.substr(0, slash));
            if (!is_dir && path::EndsWithIC(n, ".nro")) {
                nro_nested++;
            }
        } else if (is_dir) {
            add_root_folder(n);
        } else if (path::EndsWithIC(n, ".nro")) {
            nro_root++;
            root_nro = n;
        }
    }

    if (root_folders.size() > 1) {
        return std::string{kDownloadsDir} + "/" + SafeFolderName(archive_stem);
    }

    if (nro_root + nro_nested == 1 && nro_nested == 1 && root_folders.size() == 1) {
        if (path::EqualsIC(root_folders[0], "switch")) {
            return "/";
        }
        return std::string{kSwitchDir};
    }

    if (nro_root + nro_nested == 1 && nro_root == 1 && root_folders.empty()) {
        return std::string{kSwitchDir} + "/" + SafeFolderName(FileStem(root_nro));
    }

    return std::string{kDownloadsDir};
}

// Direct .nro URL: /switch/<stem>/<filename>
inline auto SuggestNakedNroPath(std::string_view filename) -> std::string {
    const auto base = path::ExtractBasename(filename);
    const auto name = path::IsSafeFilename(base) ? std::string{base} : std::string{"downloaded.nro"};
    return std::string{kSwitchDir} + "/" + SafeFolderName(FileStem(name)) + "/" + name;
}

} // namespace sphaira::zip_extract
