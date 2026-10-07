#pragma once

// shared between the system_info*.cpp collectors; nothing here is for the UI.

#include "system_info.hpp"
#include "i18n.hpp"

#include <switch.h>
#include <cstdio>
#include <string>

namespace sphaira::system_info {

struct GroupBuilder {
    Group group;

    explicit GroupBuilder(const char* title) : group{i18n::get(title), {}} {}

    void Line(const char* label, std::string value) {
        group.rows.push_back({i18n::get(label), std::move(value)});
    }
    // a row with an already translated label (built from pieces).
    void LineRaw(std::string label, std::string value) {
        group.rows.push_back({std::move(label), std::move(value)});
    }
};

inline auto Fmt(const char* fmt, auto... args) -> std::string {
    char buf[160];
    std::snprintf(buf, sizeof(buf), fmt, args...);
    return buf;
}

inline auto YesNo(bool v) -> std::string {
    return v ? "Yes"_i18n : "No"_i18n;
}

// one group each; a group that cannot be read at all is left empty and dropped.
auto CollectConsole() -> Group;
auto CollectAtmosphere() -> Group;
auto CollectStorage() -> Group;
auto CollectPower() -> Group;
auto CollectBattery() -> Group;
auto CollectHardware() -> Group;
auto CollectPlayActivity() -> Group;

} // namespace sphaira::system_info
