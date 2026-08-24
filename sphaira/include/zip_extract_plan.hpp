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

inline auto JoinDir(std::string_view parent, std::string_view name) -> std::string {
    const auto n = SafeFolderName(name);
    if (parent.empty() || parent == "/") {
        return std::string{"/"} + n;
    }
    if (parent.back() == '/') {
        return std::string{parent} + n;
    }
    return std::string{parent} + "/" + n;
}

// /downloads/<archive-stem> (or parent/<archive-stem>).
inline auto NewFolderDest(std::string_view parent, std::string_view zip_filename) -> std::string {
    return JoinDir(parent, FileStem(zip_filename));
}

struct ZipTreeNode {
    std::string label;
    std::string prefix;
    int depth{};
    bool is_dir{};
};

// Unique folders and files, zip order, folders created before their children.
inline auto BuildZipTree(std::span<const std::string_view> names) -> std::vector<ZipTreeNode> {
    std::vector<ZipTreeNode> tree;

    auto has = [&](std::string_view p) -> bool {
        for (const auto& n : tree) {
            if (n.prefix == p) {
                return true;
            }
        }
        return false;
    };

    for (const auto raw : names) {
        auto n = NormalizeZipEntry(raw);
        if (n.empty()) {
            continue;
        }
        const bool as_dir = n.back() == '/';
        if (as_dir) {
            n.pop_back();
        }
        if (n.empty()) {
            continue;
        }

        std::string accum;
        std::size_t start = 0;
        while (start <= n.size()) {
            const auto slash = n.find('/', start);
            const bool last = slash == std::string::npos;
            const auto part = last ? n.substr(start) : n.substr(start, slash - start);
            if (part.empty() || part == "." || part == "..") {
                if (last) {
                    break;
                }
                start = slash + 1;
                continue;
            }

            const bool dir_node = !last || as_dir;
            if (!accum.empty() && accum.back() != '/') {
                accum += '/';
            }
            accum += part;
            if (dir_node) {
                accum += '/';
            }

            if (!has(accum)) {
                int depth = 0;
                for (std::size_t i = 0; i + 1 < accum.size(); ++i) {
                    if (accum[i] == '/') {
                        depth++;
                    }
                }
                tree.push_back(ZipTreeNode{
                    std::string{part} + (dir_node ? "/" : ""),
                    accum,
                    depth,
                    dir_node,
                });
            }

            if (last) {
                break;
            }
            start = slash + 1;
        }
    }
    return tree;
}

inline auto SelectedFilePrefixes(std::span<const ZipTreeNode> nodes, std::span<const char> checked) -> std::vector<std::string> {
    std::vector<std::string> out;
    const auto n = std::min(nodes.size(), checked.size());
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (!nodes[i].is_dir && checked[i]) {
            out.push_back(nodes[i].prefix);
        }
    }
    return out;
}

inline auto EntryMatchesSelection(std::string_view normalized, std::span<const std::string> files) -> bool {
    const auto n = NormalizeZipEntry(normalized);
    if (n.empty() || n.back() == '/') {
        return false;
    }
    for (const auto& f : files) {
        if (path::EqualsIC(n, f)) {
            return true;
        }
    }
    return false;
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
