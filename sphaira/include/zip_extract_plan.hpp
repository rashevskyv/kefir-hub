#pragma once

#include "path_util.hpp"

#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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

inline auto IsNroFile(std::string_view normalized) -> bool {
    return !normalized.empty() && normalized.back() != '/' && path::EndsWithIC(normalized, ".nro");
}

// One .nro anywhere in the archive, or nullopt if none / more than one.
inline auto FindSingleNro(std::span<const std::string_view> names) -> std::optional<std::string> {
    std::string found;
    int count = 0;
    for (const auto raw : names) {
        auto n = NormalizeZipEntry(raw);
        if (!IsNroFile(n)) {
            continue;
        }
        count++;
        found = std::move(n);
        if (count > 1) {
            return std::nullopt;
        }
    }
    if (count == 1) {
        return found;
    }
    return std::nullopt;
}

// /switch/<stem>/<file.nro>
inline auto NroInstallDest(std::string_view nro_zip_path) -> std::string {
    const auto n = NormalizeZipEntry(nro_zip_path);
    auto base = std::string{path::ExtractBasename(n)};
    if (!path::IsSafeFilename(base)) {
        base = "downloaded.nro";
    }
    return std::string{kSwitchDir} + "/" + SafeFolderName(FileStem(base)) + "/" + base;
}

// Direct .nro URL: /switch/<stem>/<filename>
inline auto SuggestNakedNroPath(std::string_view filename) -> std::string {
    const auto base = path::ExtractBasename(filename);
    const auto name = path::IsSafeFilename(base) ? std::string{base} : std::string{"downloaded.nro"};
    return NroInstallDest(name);
}

// Top-level names for the extract-all row, e.g. "atmosphere/, switch/, hbmenu.nro".
inline auto FormatZipRoots(std::span<const std::string_view> names, std::size_t max_names = 4) -> std::string {
    std::vector<std::string> roots;
    auto add = [&](std::string item) {
        for (const auto& r : roots) {
            if (path::EqualsIC(r, item)) {
                return;
            }
        }
        roots.emplace_back(std::move(item));
    };

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
            add(std::string{n.substr(0, slash)} + "/");
        } else if (is_dir) {
            add(n + "/");
        } else {
            add(n);
        }
    }

    if (roots.empty()) {
        return {};
    }

    std::string out;
    const auto shown = std::min(roots.size(), max_names);
    for (std::size_t i = 0; i < shown; i++) {
        if (i) {
            out += ", ";
        }
        out += roots[i];
    }
    if (roots.size() > max_names) {
        out += ", …";
    }
    return out;
}

} // namespace sphaira::zip_extract
