#pragma once

// Auto backup of changed saves on Hub start: what was copied last time.
// One line per save: "<save_data_id hex> <commit_id hex> <backup path>". libnx-free, host-tested.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <string_view>

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

// a save needs a new copy when it changed since the last one, or the last copy is gone.
inline auto NeedsCopy(const States& states, std::uint64_t id, std::uint64_t commit, bool copy_exists) -> bool {
    const auto it = states.find(id);
    return it == states.end() || it->second.commit != commit || it->second.path.empty() || !copy_exists;
}

} // namespace sphaira::ui::menu::save::autobackup
