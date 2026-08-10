#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>

namespace text_helper {

inline auto ToLower(std::string_view str) -> std::string {
    std::string out;
    out.reserve(str.size());
    for (const char c : str) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

inline auto GetFileName(std::string_view path) -> std::string_view {
    const auto slash = path.find_last_of("/\\");
    if (slash == std::string_view::npos) {
        return path;
    }
    return path.substr(slash + 1);
}

inline auto GetExtension(std::string_view filename) -> std::string_view {
    const auto name = GetFileName(filename);
    const auto dot = name.find_last_of('.');
    if (dot == std::string_view::npos || dot == 0) {
        return {};
    }
    return name.substr(dot + 1);
}

inline auto IsTextFile(std::string_view path_or_name) -> bool {
    const auto name = GetFileName(path_or_name);
    const auto lower_name = ToLower(name);

    static constexpr std::string_view known_basenames[] = {
        "readme",
        "license",
        "changelog",
        "makefile",
        "cmakelists.txt",
        "dockerfile",
        ".gitignore",
        ".gitattributes",
        ".editorconfig",
    };

    for (const auto base : known_basenames) {
        if (lower_name == base) {
            return true;
        }
    }

    const auto ext = ToLower(GetExtension(name));
    if (ext.empty()) {
        return false;
    }

    static constexpr std::string_view known_extensions[] = {
        "txt", "ini", "cfg", "conf", "config", "json", "json5", "xml", "yaml", "yml", "toml",
        "csv", "tsv", "log", "md", "markdown", "nfo", "cue", "m3u", "m3u8", "pls", "srt", "ass",
        "vtt", "html", "htm", "css", "js", "mjs", "cjs", "ts", "jsx", "tsx", "lua", "py", "rb",
        "php", "sh", "bash", "zsh", "fish", "ps1", "bat", "cmd", "c", "h", "cc", "cpp", "cxx",
        "hpp", "hxx", "cs", "java", "kt", "kts", "rs", "go", "swift", "sql", "env",
        "properties", "desktop", "service", "manifest"
    };

    for (const auto e : known_extensions) {
        if (ext == e) {
            return true;
        }
    }

    return false;
}

inline auto IsIniFile(std::string_view path_or_name) -> bool {
    return ToLower(GetExtension(path_or_name)) == "ini";
}

enum class IniLineType {
    Plain,
    Comment,
    Section,
    KeyValue
};

struct IniLineInfo {
    IniLineType type{IniLineType::Plain};
    std::string_view key{};
    std::string_view eq{};
    std::string_view val{};
};

inline auto ParseIniLine(std::string_view line) -> IniLineInfo {
    size_t start = 0;
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
        start++;
    }

    if (start >= line.size()) {
        return {IniLineType::Plain, {}, {}, {}};
    }

    const char first = line[start];
    if (first == ';' || first == '#') {
        return {IniLineType::Comment, {}, {}, {}};
    }

    if (first == '[') {
        size_t end = line.size();
        while (end > start && (line[end - 1] == ' ' || line[end - 1] == '\t' || line[end - 1] == '\r')) {
            end--;
        }
        if (end > start + 1 && line[end - 1] == ']') {
            return {IniLineType::Section, {}, {}, {}};
        }
    }

    const auto eq_pos = line.find('=', start);
    if (eq_pos != std::string_view::npos) {
        IniLineInfo info;
        info.type = IniLineType::KeyValue;
        info.key = line.substr(0, eq_pos);
        info.eq = line.substr(eq_pos, 1);
        info.val = line.substr(eq_pos + 1);
        return info;
    }

    return {IniLineType::Plain, {}, {}, {}};
}

struct ToggleResult {
    bool toggled{false};
    std::string new_line{};
};

inline auto ToggleIniBoolean(std::string_view line) -> ToggleResult {
    const auto info = ParseIniLine(line);
    if (info.type != IniLineType::KeyValue) {
        return {false, {}};
    }

    const auto val = info.val;
    size_t v_start = 0;
    while (v_start < val.size() && (val[v_start] == ' ' || val[v_start] == '\t')) {
        v_start++;
    }

    if (v_start >= val.size()) {
        return {false, {}};
    }

    size_t v_end = v_start;
    while (v_end < val.size() && val[v_end] != ' ' && val[v_end] != '\t' && val[v_end] != ';' && val[v_end] != '#' && val[v_end] != '\r') {
        v_end++;
    }

    const auto token = val.substr(v_start, v_end - v_start);
    const auto lower_token = ToLower(token);

    std::string new_token;
    if (lower_token == "true") {
        bool all_upper = true;
        for (const char c : token) {
            if (std::isalpha(static_cast<unsigned char>(c)) && !std::isupper(static_cast<unsigned char>(c))) {
                all_upper = false;
                break;
            }
        }

        if (all_upper) {
            new_token = "FALSE";
        } else if (std::isupper(static_cast<unsigned char>(token[0]))) {
            new_token = "False";
        } else {
            new_token = "false";
        }
    } else if (lower_token == "false") {
        bool all_upper = true;
        for (const char c : token) {
            if (std::isalpha(static_cast<unsigned char>(c)) && !std::isupper(static_cast<unsigned char>(c))) {
                all_upper = false;
                break;
            }
        }

        if (all_upper) {
            new_token = "TRUE";
        } else if (std::isupper(static_cast<unsigned char>(token[0]))) {
            new_token = "True";
        } else {
            new_token = "true";
        }
    } else if (token.size() == 6 &&
               (token[0] == 'u' || token[0] == 'U') &&
               token[1] == '8' &&
               token[2] == '!' &&
               token[3] == '0' &&
               (token[4] == 'x' || token[4] == 'X') &&
               (token[5] == '0' || token[5] == '1')) {
        new_token = std::string(token);
        new_token[5] = (token[5] == '0') ? '1' : '0';
    } else {
        return {false, {}};
    }

    const auto prefix = line.substr(0, (info.val.data() - line.data()) + v_start);
    const auto suffix = line.substr((info.val.data() - line.data()) + v_end);

    std::string result;
    result.reserve(prefix.size() + new_token.size() + suffix.size());
    result.append(prefix);
    result.append(new_token);
    result.append(suffix);

    return {true, result};
}

} // namespace text_helper
