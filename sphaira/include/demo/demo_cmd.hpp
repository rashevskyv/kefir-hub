#pragma once

// One line of sdmc:/config/kefir/demo/input.txt: tools/docs/eden.ps1 (DOCS_DEMO) or tools/dev/hub-input.ps1
// over MTP with the "Scripted input" setting on.
//   A | B | ... | Up | Down        press a button (names as in docs/site/shots.json)
//   wait <seconds>                  pause the queue
//   lang <code>                     switch the UI language and rebuild the menus from the main screen
//   ready                           write sdmc:/config/kefir/demo/ready (the PC side waits for it, then shoots)
//   dump                            write the open screens, bottom to top, to sdmc:/config/kefir/demo/state.txt
//   shot                            write the current screen as JPEG to sdmc:/config/kefir/demo/shot.jpg
//   text <string>                   the next system keyboard (rename, new folder, ...) returns <string> without opening
// libnx-free so tests/test_demo_cmd.cpp can run it on the host.

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

namespace sphaira::demo {

// HidNpadButton bits (libnx hid.h); demo_input.cpp static_asserts they match.
namespace pad {
constexpr std::uint64_t A = 1ull << 0, B = 1ull << 1, X = 1ull << 2, Y = 1ull << 3;
constexpr std::uint64_t L3 = 1ull << 4, R3 = 1ull << 5, L = 1ull << 6, R = 1ull << 7;
constexpr std::uint64_t ZL = 1ull << 8, ZR = 1ull << 9, Plus = 1ull << 10, Minus = 1ull << 11;
constexpr std::uint64_t Left = 1ull << 12, Up = 1ull << 13, Right = 1ull << 14, Down = 1ull << 15;
} // namespace pad

struct Cmd {
    enum Kind { Invalid, Button, Wait, Lang, Ready, Dump, Shot, Text } kind{Invalid};
    std::uint64_t button{};
    double seconds{};
    std::string arg{};
};

inline Cmd ParseCmd(std::string_view line) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.remove_suffix(1);
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);

    const auto sp = line.find(' ');
    const auto word = line.substr(0, sp);
    auto rest = sp == std::string_view::npos ? std::string_view{} : line.substr(sp + 1);
    while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);

    if (word == "wait") {
        const std::string s{rest};
        char* end{};
        const double sec = std::strtod(s.c_str(), &end);
        if (s.empty() || *end || sec < 0) return {};
        return {Cmd::Wait, 0, sec, {}};
    }
    if (word == "lang") {
        if (rest.empty()) return {};
        return {Cmd::Lang, 0, 0, std::string{rest}};
    }
    if (word == "ready") {
        return rest.empty() ? Cmd{Cmd::Ready} : Cmd{};
    }
    if (word == "dump") {
        return rest.empty() ? Cmd{Cmd::Dump} : Cmd{};
    }
    if (word == "shot") {
        return rest.empty() ? Cmd{Cmd::Shot} : Cmd{};
    }
    if (word == "text") {
        if (rest.empty()) return {};
        return {Cmd::Text, 0, 0, std::string{rest}};
    }
    if (!rest.empty()) {
        return {};
    }

    static constexpr struct { std::string_view name; std::uint64_t bit; } buttons[] = {
        {"A", pad::A}, {"B", pad::B}, {"X", pad::X}, {"Y", pad::Y}, {"L", pad::L}, {"R", pad::R},
        {"ZL", pad::ZL}, {"ZR", pad::ZR}, {"Plus", pad::Plus}, {"Minus", pad::Minus}, {"L3", pad::L3},
        {"R3", pad::R3}, {"Up", pad::Up}, {"Down", pad::Down}, {"Left", pad::Left}, {"Right", pad::Right},
    };
    for (const auto& b : buttons) {
        if (word == b.name) return {Cmd::Button, b.bit, 0, {}};
    }
    return {};
}

} // namespace sphaira::demo
