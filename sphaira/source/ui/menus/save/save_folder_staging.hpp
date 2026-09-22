#pragma once

#include "fs.hpp"
#include "ui/menus/save/save_paths.hpp"
#include <string_view>
#include <vector>
#include <cstring>
#include <cstdio>
#include <strings.h>

namespace sphaira::ui::menu::save {

struct FolderSourceEntry {
    fs::FsPath full_path;
    s64 size{0};
};

static inline Result CheckedJoinPath(fs::FsPath& out, std::string_view base, std::string_view rel) {
    size_t base_len = base.size();
    while (base_len > 0 && base[base_len - 1] == '/') {
        base_len--;
    }
    size_t rel_len = rel.size();
    while (rel_len > 0 && rel[0] == '/') {
        rel.remove_prefix(1);
        rel_len--;
    }
    const size_t total_len = base_len + 1 + rel_len;
    if (total_len >= sizeof(fs::FsPath)) {
        return FsError_TooLongPath;
    }
    char buf[sizeof(fs::FsPath)];
    const int n = std::snprintf(buf, sizeof(buf), "%.*s/%.*s",
        static_cast<int>(base_len), base.data(),
        static_cast<int>(rel_len), rel.data());
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(buf)) {
        return FsError_TooLongPath;
    }
    out = buf;
    return 0;
}

static inline bool IsStagingParentOrRelated(std::string_view canonical_path) {
    if (canonical_path == "/" || canonical_path.empty()) {
        return true;
    }

    std::vector<std::string_view> comps;
    size_t start = 0;
    while (start < canonical_path.size()) {
        if (canonical_path[start] == '/') {
            start++;
            continue;
        }
        size_t end = canonical_path.find('/', start);
        if (end == std::string_view::npos) {
            end = canonical_path.size();
        }
        comps.push_back(canonical_path.substr(start, end - start));
        start = end;
    }

    if (comps.empty()) {
        return true;
    }

    for (const auto& c : comps) {
        if (c == "." || c == "..") {
            return true;
        }
    }

    const auto equals_ic = [](std::string_view a, const char* b) {
        const size_t b_len = std::strlen(b);
        return a.size() == b_len && !strncasecmp(a.data(), b, b_len);
    };

    if (comps.size() == 1) {
        if (equals_ic(comps[0], "dumps")) {
            return true;
        }
        return false;
    }

    if (equals_ic(comps[0], "dumps") && equals_ic(comps[1], "save-import")) {
        return true;
    }

    return false;
}

} // namespace sphaira::ui::menu::save
