#include "ui/menus/settings/settings_fs_utils.hpp"
#include "fs.hpp"
#include <minIni.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <array>

namespace sphaira::ui::menu::settings::detail {

auto Trim(std::string str) -> std::string {
    const auto first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = str.find_last_not_of(" \t\r\n");
    str = str.substr(first, last - first + 1);
    if (str.size() >= 2 && ((str.front() == '\'' && str.back() == '\'') || (str.front() == '"' && str.back() == '"'))) {
        str = str.substr(1, str.size() - 2);
    }
    return str;
}

auto ReadTextFile(const std::string& path) -> std::string {
    std::ifstream file{path};
    if (!file) {
        return {};
    }
    return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

auto ReadLines(const std::string& path) -> std::vector<std::string> {
    std::vector<std::string> lines;
    std::ifstream file{path};
    for (std::string line; std::getline(file, line);) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.emplace_back(std::move(line));
    }
    return lines;
}

auto WriteLines(const std::string& path, const std::vector<std::string>& lines) -> Result {
    R_TRY(EnsureParentDirectory(path));

    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    if (!file) {
        R_THROW(Result_FsUnknownStdioError);
    }

    for (const auto& line : lines) {
        file << line << '\n';
        if (!file) {
            R_THROW(Result_FsUnknownStdioError);
        }
    }

    R_SUCCEED();
}

auto ExtractBracketName(const std::string& line) -> std::string {
    const auto trimmed = Trim(line);
    if (trimmed.size() < 3 || trimmed.front() != '[' || trimmed.back() != ']') {
        return {};
    }

    auto name = trimmed.substr(1, trimmed.size() - 2);
    if (!name.empty() && name.front() == '*') {
        name.erase(name.begin());
    }
    return Trim(name);
}

auto ExtractIniKey(const std::string& line) -> std::string {
    const auto trimmed = Trim(line);
    if (trimmed.empty() || trimmed.front() == ';' || trimmed.front() == '#') {
        return {};
    }

    const auto pos = trimmed.find_first_of("=:");
    if (pos == std::string::npos) {
        return {};
    }

    return Trim(trimmed.substr(0, pos));
}

auto StartsWith(const std::string& str, const char* prefix) -> bool {
    return str.rfind(prefix, 0) == 0;
}

auto SplitCommand(const std::string& line) -> std::vector<std::string> {
    std::vector<std::string> out;
    std::string current;
    char quote{};

    for (const auto ch : line) {
        if (quote) {
            if (ch == quote) {
                quote = 0;
            } else {
                current += ch;
            }
            continue;
        }

        if (ch == '\'' || ch == '"') {
            quote = ch;
        } else if (ch == ' ' || ch == '\t') {
            if (!current.empty()) {
                out.emplace_back(std::move(current));
                current.clear();
            }
        } else {
            current += ch;
        }
    }

    if (!current.empty()) {
        out.emplace_back(std::move(current));
    }
    return out;
}

auto ExtractJsonStringField(const std::string& json, const std::string& field) -> std::string {
    const auto key = "\"" + field + "\"";
    const auto key_pos = json.find(key);
    if (key_pos == std::string::npos) {
        return {};
    }

    const auto colon = json.find(':', key_pos + key.size());
    const auto first_quote = json.find('"', colon == std::string::npos ? key_pos : colon);
    if (first_quote == std::string::npos) {
        return {};
    }

    const auto second_quote = json.find('"', first_quote + 1);
    if (second_quote == std::string::npos) {
        return {};
    }

    return json.substr(first_quote + 1, second_quote - first_quote - 1);
}

auto FileExists(const char* path) -> bool {
    struct stat st {};
    return stat(path, &st) == 0;
}

auto DirectoryExists(const char* path) -> bool {
    struct stat st {};
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

auto ParentPath(const std::string& path) -> std::string {
    const auto pos = path.find_last_of('/');
    if (pos == std::string::npos || pos == 0) {
        return "/";
    }
    return path.substr(0, pos);
}

auto EnsureParentDirectory(const std::string& path) -> Result {
    fs::FsNativeSd fs;
    R_TRY(fs.GetFsOpenResult());
    R_TRY(fs.CreateDirectoryRecursively(ParentPath(path)));
    R_SUCCEED();
}

auto CopyFileSimple(const std::string& src, const std::string& dst) -> Result {
    R_TRY(EnsureParentDirectory(dst));

    FILE* in = std::fopen(src.c_str(), "rb");
    if (!in) {
        R_THROW(fsdevGetLastResult());
    }

    FILE* out = std::fopen(dst.c_str(), "wb");
    if (!out) {
        std::fclose(in);
        R_THROW(fsdevGetLastResult());
    }

    std::array<char, 64 * 1024> buf{};
    while (const auto read = std::fread(buf.data(), 1, buf.size(), in)) {
        if (std::fwrite(buf.data(), 1, read, out) != read) {
            std::fclose(out);
            std::fclose(in);
            R_THROW(fsdevGetLastResult());
        }
    }

    std::fclose(out);
    std::fclose(in);
    R_SUCCEED();
}

auto DeletePath(const std::string& path) -> Result {
    if (!FileExists(path.c_str())) {
        R_SUCCEED();
    }

    if (DirectoryExists(path.c_str())) {
        DIR* dir = opendir(path.c_str());
        if (!dir) {
            R_THROW(fsdevGetLastResult());
        }

        while (auto* ent = readdir(dir)) {
            if (!std::strcmp(ent->d_name, ".") || !std::strcmp(ent->d_name, "..")) {
                continue;
            }
            R_TRY(DeletePath(path + "/" + ent->d_name));
        }
        closedir(dir);

        if (rmdir(path.c_str()) != 0) {
            R_THROW(fsdevGetLastResult());
        }
    } else if (std::remove(path.c_str()) != 0) {
        R_THROW(fsdevGetLastResult());
    }

    R_SUCCEED();
}

auto CopyDirectoryContents(const std::string& src, const std::string& dst) -> Result {
    DIR* dir = opendir(src.c_str());
    if (!dir) {
        R_THROW(fsdevGetLastResult());
    }

    while (auto* ent = readdir(dir)) {
        if (!std::strcmp(ent->d_name, ".") || !std::strcmp(ent->d_name, "..")) {
            continue;
        }

        const auto src_path = src + "/" + ent->d_name;
        const auto dst_path = dst == "/" ? "/" + std::string{ent->d_name} : dst + "/" + ent->d_name;
        if (DirectoryExists(src_path.c_str())) {
            fs::FsNativeSd fs;
            R_TRY(fs.CreateDirectoryRecursively(dst_path));
            R_TRY(CopyDirectoryContents(src_path, dst_path));
        } else {
            R_TRY(CopyFileSimple(src_path, dst_path));
        }
    }
    closedir(dir);
    R_SUCCEED();
}

auto MovePath(const std::string& src, const std::string& dst) -> Result {
    if (!FileExists(src.c_str())) {
        R_SUCCEED();
    }

    R_TRY(EnsureParentDirectory(dst));
    R_TRY(DeletePath(dst));
    if (std::rename(src.c_str(), dst.c_str()) == 0) {
        R_SUCCEED();
    }

    if (DirectoryExists(src.c_str())) {
        fs::FsNativeSd fs;
        R_TRY(fs.CreateDirectoryRecursively(dst));
        R_TRY(CopyDirectoryContents(src, dst));
    } else {
        R_TRY(CopyFileSimple(src, dst));
    }
    R_TRY(DeletePath(src));
    R_SUCCEED();
}

auto IniValueEquals(const char* path, const char* section, const char* key, const char* value) -> bool {
    char buf[64]{};
    return ini_gets(section, key, "", buf, sizeof(buf), path) && !std::strcmp(buf, value);
}

auto SetIniValue(const char* path, const char* section, const char* key, const char* value) -> Result {
    R_TRY(EnsureParentDirectory(path));
    if (!ini_puts(section, key, value, path)) {
        R_THROW(fsdevGetLastResult());
    }
    R_SUCCEED();
}

auto ReadIniRawValue(const std::string& path, const std::string& section, const std::string& key) -> std::string {
    const auto lines = ReadLines(path);
    bool in_section{};

    for (const auto& line : lines) {
        const auto section_name = ExtractBracketName(line);
        if (!section_name.empty()) {
            in_section = section_name == section;
            continue;
        }

        if (!in_section || ExtractIniKey(line) != key) {
            continue;
        }

        const auto pos = line.find_first_of("=:");
        if (pos == std::string::npos) {
            return {};
        }

        return Trim(line.substr(pos + 1));
    }

    return {};
}

auto SetIniRawValue(const std::string& path, const std::string& section, const std::string& key, const std::string& value) -> Result {
    auto lines = ReadLines(path);
    const auto new_line = key + " = " + value;

    auto section_begin = lines.end();
    auto section_end = lines.end();

    for (auto it = lines.begin(); it != lines.end(); ++it) {
        const auto section_name = ExtractBracketName(*it);
        if (section_name.empty()) {
            continue;
        }

        if (section_begin == lines.end()) {
            if (section_name == section) {
                section_begin = it;
            }
        } else {
            section_end = it;
            break;
        }
    }

    if (section_begin == lines.end()) {
        if (!lines.empty() && !Trim(lines.back()).empty()) {
            lines.emplace_back();
        }
        lines.emplace_back("[" + section + "]");
        lines.emplace_back(new_line);
        return WriteLines(path, lines);
    }

    if (section_end == lines.end()) {
        section_end = lines.end();
    }

    for (auto it = std::next(section_begin); it != section_end; ++it) {
        if (ExtractIniKey(*it) == key) {
            *it = new_line;
            return WriteLines(path, lines);
        }
    }

    lines.insert(section_end, new_line);
    return WriteLines(path, lines);
}

} // namespace sphaira::ui::menu::settings::detail
