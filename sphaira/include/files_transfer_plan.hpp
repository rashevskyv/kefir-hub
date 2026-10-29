#pragma once

// Console Transfer → Send files / Receive files: what the sending console lists in its package folder
// and where the receiving console puts each item. No libnx, so tests/test_files_transfer_plan.cpp runs on the host.
//
// manifest.txt, one item per line, tab separated:  d|f <TAB> payload <TAB> parent <TAB> name <TAB> size
// `payload` is the file in the package folder (a folder travels as a .zip, a file as it is), `parent` is the
// folder the item was in on the sending console, `name` is its name there.

#include "path_util.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::files_transfer {

inline constexpr const char* MANIFEST_NAME = "manifest.txt";

struct Item {
    bool dir{};
    std::string payload{};
    std::string parent{}; // "/switch"
    std::string name{};   // "app"
    int64_t size{};       // payload bytes
};

// "/switch/app/" -> {"/switch", "app"}; "/app" -> {"/", "app"}.
inline auto SplitPath(std::string_view path) -> std::pair<std::string, std::string> {
    while (path.size() > 1 && path.back() == '/') {
        path.remove_suffix(1);
    }
    const auto slash = path.find_last_of('/');
    if (slash == std::string_view::npos) {
        return {"/", std::string{path}};
    }
    return {slash == 0 ? "/" : std::string{path.substr(0, slash)}, std::string{path.substr(slash + 1)}};
}

// one path component of an item name: no separator, no control character, not "." or "..".
inline auto IsSafeName(std::string_view name) -> bool {
    return path::IsSafeArchiveEntry(name) && name.find('/') == std::string_view::npos;
}

// an absolute folder with no "." / ".." component.
inline auto IsSafeFolder(std::string_view folder) -> bool {
    if (folder == "/") {
        return true;
    }
    return !folder.empty() && folder.front() == '/' && path::IsSafeArchiveEntry(folder.substr(1));
}

inline auto PayloadName(size_t index, bool dir) -> std::string {
    return std::to_string(index) + (dir ? ".zip" : ".bin");
}

inline auto FormatManifest(const std::vector<Item>& items) -> std::string {
    std::string out;
    for (const auto& i : items) {
        out += i.dir ? "d\t" : "f\t";
        out += i.payload + "\t" + i.parent + "\t" + i.name + "\t" + std::to_string(i.size) + "\n";
    }
    return out;
}

// bad lines (wrong field count, unsafe name or folder, payload that is not "<digits>.zip|.bin") are dropped:
// the manifest comes from another console.
inline auto ParseManifest(std::string_view text) -> std::vector<Item> {
    std::vector<Item> out;
    while (!text.empty()) {
        const auto eol = text.find('\n');
        auto line = text.substr(0, eol);
        text = eol == std::string_view::npos ? std::string_view{} : text.substr(eol + 1);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }

        std::string_view f[5];
        size_t n = 0;
        while (n < 5) {
            const auto tab = line.find('\t');
            f[n++] = line.substr(0, tab);
            if (tab == std::string_view::npos) {
                break;
            }
            line = line.substr(tab + 1);
            if (n == 5 && !line.empty()) { // a sixth field
                n = 6;
            }
        }
        if (n != 5 || (f[0] != "d" && f[0] != "f")) {
            continue;
        }

        Item item;
        item.dir = f[0] == "d";
        item.payload = f[1];
        item.parent = f[2];
        item.name = f[3];
        const auto ext = item.dir ? std::string_view{".zip"} : std::string_view{".bin"};
        const auto stem = item.payload.size() > ext.size() ? std::string_view{item.payload}.substr(0, item.payload.size() - ext.size()) : std::string_view{};
        const bool payload_ok = !stem.empty() && item.payload.ends_with(ext)
            && stem.find_first_not_of("0123456789") == std::string_view::npos;
        if (!payload_ok || !IsSafeName(item.name) || !IsSafeFolder(item.parent)) {
            continue;
        }
        for (const char c : f[4]) {
            if (c < '0' || c > '9') {
                item.size = -1;
                break;
            }
            item.size = item.size * 10 + (c - '0');
        }
        if (item.size < 0) {
            continue;
        }
        out.push_back(std::move(item));
    }
    return out;
}

// the name the item gets on the receiving console: a folder that stays packed is "<name>.zip".
inline auto SaveName(const Item& item, bool unpack) -> std::string {
    return item.dir && !unpack ? item.name + ".zip" : item.name;
}

// the folder the item goes to: the one it came from, or `chosen` when the user picked one.
inline auto TargetFolder(const Item& item, std::string_view chosen) -> std::string {
    return chosen.empty() ? item.parent : std::string{chosen};
}

inline auto TargetPath(const Item& item, std::string_view chosen, bool unpack) -> std::string {
    auto folder = TargetFolder(item, chosen);
    if (folder.empty() || folder.back() != '/') {
        folder += '/';
    }
    return folder + SaveName(item, unpack);
}

} // namespace sphaira::files_transfer
