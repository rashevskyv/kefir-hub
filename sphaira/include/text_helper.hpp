#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

struct Page {
    int64_t page_index{0};
    int64_t start_offset{0};
    int64_t end_offset{0};
    int64_t logical_end_offset{0};
    int64_t start_line{1};
    int64_t logical_end_line{1};
    std::vector<std::string> lines{};
    bool is_eof{false};
    bool is_error{false};
};

template <typename ReadFunc>
inline auto ReadPage(ReadFunc&& read_func, int64_t file_size, int64_t start_offset, int64_t start_line, int64_t page_rows, int64_t logical_rows = 0, int64_t max_line_len = 4096) -> Page {
    if (logical_rows <= 0) {
        logical_rows = page_rows;
    }

    Page page;
    page.start_offset = std::clamp<int64_t>(start_offset, 0, std::max<int64_t>(0, file_size));
    page.start_line = std::max<int64_t>(1, start_line);
    page.logical_end_offset = page.start_offset;
    page.logical_end_line = page.start_line;
    page.lines.reserve(page_rows > 0 ? page_rows : 1);

    if (page.start_offset >= file_size || page_rows <= 0) {
        page.end_offset = page.start_offset;
        page.logical_end_offset = page.start_offset;
        page.logical_end_line = page.start_line;
        page.is_eof = (page.start_offset >= file_size);
        if (file_size == 0 && page.lines.empty()) {
            page.lines.emplace_back();
        }
        return page;
    }

    constexpr int64_t CHUNK_SIZE = 4096;
    char chunk[CHUNK_SIZE];
    int64_t offset = page.start_offset;
    std::string current_line;
    current_line.reserve(128);
    bool logical_captured = false;

    while (static_cast<int64_t>(page.lines.size()) < page_rows && offset < file_size) {
        const int64_t to_read = std::min<int64_t>(CHUNK_SIZE, file_size - offset);
        const int64_t bytes_read = read_func(offset, chunk, to_read);
        if (bytes_read <= 0) {
            break;
        }

        int64_t pos = 0;
        while (pos < bytes_read && static_cast<int64_t>(page.lines.size()) < page_rows) {
            const char c = chunk[pos++];
            if (c == '\n') {
                if (!current_line.empty() && current_line.back() == '\r') {
                    current_line.pop_back();
                }
                page.lines.emplace_back(std::move(current_line));
                current_line.clear();
                current_line.reserve(128);

                if (!logical_captured && static_cast<int64_t>(page.lines.size()) == logical_rows) {
                    page.logical_end_offset = offset + pos;
                    page.logical_end_line = page.start_line + page.lines.size();
                    logical_captured = true;
                }

                if (static_cast<int64_t>(page.lines.size()) == page_rows) {
                    offset += pos;
                    goto page_done;
                }
            } else {
                if (static_cast<int64_t>(current_line.size()) < max_line_len) {
                    current_line.push_back(c);
                } else {
                    page.lines.emplace_back(std::move(current_line));
                    current_line.clear();
                    current_line.reserve(128);
                    current_line.push_back(c);

                    if (!logical_captured && static_cast<int64_t>(page.lines.size()) == logical_rows) {
                        page.logical_end_offset = offset + pos;
                        page.logical_end_line = page.start_line + page.lines.size();
                        logical_captured = true;
                    }

                    if (static_cast<int64_t>(page.lines.size()) == page_rows) {
                        offset += pos;
                        goto page_done;
                    }
                }
            }
        }
        offset += pos;
    }

page_done:
    if (offset >= file_size) {
        if (!current_line.empty() || (page.lines.empty() && page.start_offset == 0)) {
            if (!current_line.empty() && current_line.back() == '\r') {
                current_line.pop_back();
            }
            if (static_cast<int64_t>(page.lines.size()) < page_rows || page.lines.empty()) {
                page.lines.emplace_back(std::move(current_line));
            }
        }
        page.is_eof = true;
    } else if (static_cast<int64_t>(page.lines.size()) < page_rows) {
        page.is_error = true;
    }

    if (!logical_captured) {
        page.logical_end_offset = offset;
        page.logical_end_line = page.start_line + page.lines.size();
    }

    if (page.lines.empty() && !page.is_error) {
        page.lines.emplace_back();
    }

    page.end_offset = offset;
    return page;
}

} // namespace text_helper
