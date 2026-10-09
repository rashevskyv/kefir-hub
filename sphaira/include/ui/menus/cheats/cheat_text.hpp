#pragma once

// Cheat file text as Atmosphere's dmnt reads it (CheatProcessManager::ParseCheats).
// One rule broken anywhere makes dmnt drop the whole file, so every cheat file Hub writes
// goes through SanitizeCheatText. libnx-free: host test tests/test_cheat_text.cpp.

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::ui::menu::hats::detail {

// dmnt has 0x80 entries; entry 0 is the one master cheat {..}, so 127 normal cheats [..].
inline constexpr std::size_t DMNT_MAX_CHEATS = 127;
// 32-bit words per cheat.
inline constexpr std::size_t DMNT_MAX_OPCODES = 0x100;

inline auto IsCheatHeaderLine(const std::string& line) -> bool {
    return line.size() >= 3 &&
        ((line.front() == '[' && line.back() == ']') ||
         (line.front() == '{' && line.back() == '}'));
}

inline auto GetCheatHeaderName(const std::string& line) -> std::string {
    if (!IsCheatHeaderLine(line)) {
        return {};
    }
    return line.substr(1, line.length() - 2);
}

inline auto IsParenthesizedNoteLine(const std::string& line) -> bool {
    return line.size() >= 2 && line.front() == '(' && line.back() == ')';
}

inline auto StripInlineCheatComment(std::string line) -> std::string {
    const auto comment_pos = line.find("//");
    if (comment_pos != std::string::npos) {
        line.erase(comment_pos);
    }
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) {
        line.pop_back();
    }
    return line;
}

inline auto IsHexCodeLine(const std::string& line) -> bool {
    std::istringstream stream(line);
    std::string part;
    std::size_t count = 0;
    while (stream >> part) {
        if (part.size() != 8) {
            return false;
        }
        for (const auto c : part) {
            if (!std::isxdigit(static_cast<unsigned char>(c))) {
                return false;
            }
        }
        count++;
    }
    return count >= 1 && count <= 4;
}

inline auto NormalizeHexCodeLine(const std::string& line) -> std::string {
    std::istringstream stream(line);
    std::string out;
    std::string part;
    while (stream >> part) {
        std::transform(part.begin(), part.end(), part.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        if (!out.empty()) {
            out += ' ';
        }
        out += part;
    }
    return out;
}

struct SanitizedCheats {
    std::string text;
    std::size_t cheats{};  // [..] blocks written
    std::size_t dropped{}; // blocks with code that dmnt would reject: over the limits or a second master
};

// Keeps headers that have code and the code lines; drops notes, comments, code before any header,
// a UTF-8 BOM, brackets inside names, a second master cheat, cheats over DMNT_MAX_OPCODES words
// and cheats past DMNT_MAX_CHEATS.
inline auto SanitizeCheatText(std::string_view in) -> SanitizedCheats {
    if (in.starts_with("\xEF\xBB\xBF")) {
        in.remove_prefix(3);
    }

    struct Block {
        bool master;
        std::string name;
        std::vector<std::string> lines;
        std::size_t words;
    };
    std::vector<Block> blocks;

    std::istringstream stream{std::string{in}};
    std::string line;
    while (std::getline(stream, line)) {
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) {
            line.pop_back();
        }
        line.erase(0, line.find_first_not_of(" \t"));
        if (line.empty() || line.starts_with("//") || IsParenthesizedNoteLine(line)) {
            continue;
        }

        if (IsCheatHeaderLine(line)) {
            std::string name;
            for (const auto c : GetCheatHeaderName(line)) {
                if (c != '[' && c != ']' && c != '{' && c != '}') {
                    name += c;
                }
            }
            blocks.push_back({line.front() == '{', name.empty() ? "Cheat" : name, {}, 0});
            continue;
        }

        line = StripInlineCheatComment(line);
        if (blocks.empty() || !IsHexCodeLine(line)) {
            continue;
        }
        auto& block = blocks.back();
        block.lines.push_back(NormalizeHexCodeLine(line));
        block.words += (block.lines.back().size() + 1) / 9;
    }

    SanitizedCheats out;
    bool have_master = false;
    for (const auto& block : blocks) {
        if (!block.words) {
            continue;
        }
        const bool fits = block.words <= DMNT_MAX_OPCODES &&
            (block.master ? !have_master : out.cheats < DMNT_MAX_CHEATS);
        if (!fits) {
            out.dropped++;
            continue;
        }
        if (block.master) {
            have_master = true;
            out.text += "{" + block.name + "}\n";
        } else {
            out.cheats++;
            out.text += "[" + block.name + "]\n";
        }
        for (const auto& code : block.lines) {
            out.text += code + '\n';
        }
        out.text += '\n';
    }
    return out;
}

} // namespace sphaira::ui::menu::hats::detail
