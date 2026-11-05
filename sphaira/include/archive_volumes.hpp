#pragma once

#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::archive {

// the file names (no folder) of every volume of the archive `name` belongs to, first volume first.
// `siblings` are the file names in the same folder. An archive that is not split gives {name}.
//   name.part1.rar, name.part2.rar ...    rar5
//   name.rar, name.r00, name.r01 ...      rar4
//   name.7z.001, name.7z.002 ...          7z
// Opening any volume lists the whole set, so the user may pick part 3 as well as part 1.
inline auto Volumes(std::string_view name, const std::vector<std::string>& siblings) -> std::vector<std::string> {
    const auto lower = [](std::string_view s) {
        std::string out{s};
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
        return out;
    };
    const auto number = [](std::string_view digits) -> long {
        if (digits.empty() || digits.size() > 6) {
            return -1;
        }
        long n = 0;
        for (const char c : digits) {
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                return -1;
            }
            n = n * 10 + (c - '0');
        }
        return n;
    };
    // all siblings whose name is `stem` + digits + `tail`, sorted by that number.
    const auto numbered = [&](const std::string& stem, const std::string& tail) {
        std::vector<std::pair<long, std::string>> found;
        for (const auto& s : siblings) {
            const auto l = lower(s);
            if (l.size() < stem.size() + tail.size() || l.compare(0, stem.size(), stem) || l.compare(l.size() - tail.size(), tail.size(), tail)) {
                continue;
            }
            const auto n = number(std::string_view{l}.substr(stem.size(), l.size() - stem.size() - tail.size()));
            if (n >= 0) {
                found.emplace_back(n, s);
            }
        }
        std::sort(found.begin(), found.end());
        std::vector<std::string> out;
        for (auto& f : found) {
            out.emplace_back(std::move(f.second));
        }
        return out;
    };

    const auto l = lower(name);

    // name.partN.rar
    if (l.ends_with(".rar")) {
        const auto base = l.substr(0, l.size() - 4);
        if (const auto dot = base.rfind(".part"); dot != base.npos && number(std::string_view{base}.substr(dot + 5)) >= 0) {
            if (auto v = numbered(base.substr(0, dot + 5), ".rar"); v.size() > 1) {
                return v;
            }
            return {std::string{name}};
        }

        // name.rar + name.r00 ...
        std::vector<std::string> v{std::string{name}};
        for (auto& s : numbered(base + ".r", "")) {
            v.emplace_back(std::move(s));
        }
        return v;
    }

    // name.rNN: the volumes of name.rar
    if (l.size() > 4 && l[l.size() - 4] == '.' && l[l.size() - 3] == 'r' && number(std::string_view{l}.substr(l.size() - 2)) >= 0) {
        const auto base = l.substr(0, l.size() - 4);
        for (const auto& s : siblings) {
            if (lower(s) == base + ".rar") {
                return Volumes(s, siblings);
            }
        }
        return {std::string{name}};
    }

    // name.7z.NNN
    if (const auto dot = l.rfind('.'); dot != l.npos && dot > 3 && l.compare(dot - 3, 3, ".7z") == 0 && number(std::string_view{l}.substr(dot + 1)) >= 0) {
        if (auto v = numbered(l.substr(0, dot + 1), ""); !v.empty()) {
            return v;
        }
    }

    return {std::string{name}};
}

// every volume of the archive at the stdio `path` (full paths, first volume first).
inline auto VolumePaths(const std::string& path) -> std::vector<std::string> {
    const auto slash = path.find_last_of('/');
    const auto dir = slash == std::string::npos ? std::string{"."} : path.substr(0, slash + 1);
    const auto leaf = slash == std::string::npos ? path : path.substr(slash + 1);

    std::vector<std::string> siblings;
    if (auto d = opendir(dir.c_str())) {
        while (const auto e = readdir(d)) {
            siblings.emplace_back(e->d_name);
        }
        closedir(d);
    }

    std::vector<std::string> out;
    for (const auto& name : Volumes(leaf, siblings)) {
        out.emplace_back(slash == std::string::npos ? name : dir + name);
    }
    return out;
}

// libarchive refuses rar4 archives packed with the "solid" option with this message.
inline bool IsSolidRar(const char* libarchive_error) {
    return libarchive_error && std::string_view{libarchive_error}.find("solid archive support unavailable") != std::string_view::npos;
}

} // namespace sphaira::archive
