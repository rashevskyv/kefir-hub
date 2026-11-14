#pragma once

// Auto backup of changed saves on Hub start: what was copied last time.
// One line per save: "<save_data_id hex> <commit_id hex> <backup path>". libnx-free, host-tested.

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::ui::menu::save::autobackup {

inline constexpr const char* STATE_PATH = "/config/kefir/autobackup.txt";

struct State {
    std::uint64_t commit{};
    std::string path; // the newest automatic copy; the older one is deleted when a new one is made
};
using States = std::map<std::uint64_t, State>;

inline auto Parse(std::string_view text) -> States {
    States out;
    while (!text.empty()) {
        const auto nl = text.find('\n');
        auto line = std::string{text.substr(0, nl)};
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        const auto a = line.find(' ');
        const auto b = a == std::string::npos ? a : line.find(' ', a + 1);
        if (b == std::string::npos || b + 1 >= line.size()) {
            continue;
        }
        char* end{};
        const auto id = std::strtoull(line.c_str(), &end, 16);
        if (end != line.c_str() + a || !id) {
            continue;
        }
        const auto commit = std::strtoull(line.c_str() + a + 1, &end, 16);
        if (end != line.c_str() + b) {
            continue;
        }
        out[id] = State{commit, line.substr(b + 1)};
    }
    return out;
}

inline auto Format(const States& states) -> std::string {
    std::string out;
    char buf[64];
    for (const auto& [id, st] : states) {
        if (st.path.empty()) {
            continue;
        }
        std::snprintf(buf, sizeof(buf), "%016llx %016llx ", static_cast<unsigned long long>(id), static_cast<unsigned long long>(st.commit));
        out += buf + st.path + "\n";
    }
    return out;
}

// games the auto backup leaves out: one line per game and save type, "<application_id hex> <type> <name>".
// type is an FsSaveDataType value, or ALL_TYPES for every save of the game.
inline constexpr const char* EXCLUDE_PATH = "/config/kefir/autobackup_exclude.txt";
inline constexpr int ALL_TYPES = -1;

struct Exclusion {
    std::uint64_t app_id{};
    int type{ALL_TYPES};
    std::string name;
};

inline auto ParseExclusions(std::string_view text) -> std::vector<Exclusion> {
    std::vector<Exclusion> out;
    while (!text.empty()) {
        const auto nl = text.find('\n');
        auto line = std::string{text.substr(0, nl)};
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        char* end{};
        const auto id = std::strtoull(line.c_str(), &end, 16);
        if (!id || *end != ' ') {
            continue;
        }
        const char* p = end + 1;
        const auto type = std::strtol(p, &end, 10);
        if (end == p || (*end != ' ' && *end != '\0')) {
            continue;
        }
        out.push_back(Exclusion{id, static_cast<int>(type), *end ? std::string{end + 1} : std::string{}});
    }
    return out;
}

inline auto FormatExclusions(const std::vector<Exclusion>& list) -> std::string {
    std::string out;
    char buf[48];
    for (const auto& x : list) {
        std::snprintf(buf, sizeof(buf), "%016llx %d ", static_cast<unsigned long long>(x.app_id), x.type);
        out += buf + x.name + "\n";
    }
    return out;
}

inline auto IsExcluded(const std::vector<Exclusion>& list, std::uint64_t app_id, int type) -> bool {
    for (const auto& x : list) {
        if (x.app_id == app_id && (x.type == ALL_TYPES || x.type == type)) {
            return true;
        }
    }
    return false;
}

// "Size limit": saves bigger than this are left out. Index 0 is no limit.
inline constexpr std::array<std::uint64_t, 5> SIZE_LIMITS{0, 16ULL << 20, 64ULL << 20, 256ULL << 20, 1ULL << 30};
inline constexpr std::array<const char*, 5> SIZE_LIMIT_LABELS{"Off", "16 MB", "64 MB", "256 MB", "1 GB"};

inline auto TooBig(std::uint64_t size, long limit_index) -> bool {
    if (limit_index <= 0 || limit_index >= static_cast<long>(SIZE_LIMITS.size())) {
        return false;
    }
    return size > SIZE_LIMITS[static_cast<std::size_t>(limit_index)];
}

// a save needs a new copy when it changed since the last one, or the last copy is gone.
inline auto NeedsCopy(const States& states, std::uint64_t id, std::uint64_t commit, bool copy_exists) -> bool {
    const auto it = states.find(id);
    return it == states.end() || it->second.commit != commit || it->second.path.empty() || !copy_exists;
}

} // namespace sphaira::ui::menu::save::autobackup
