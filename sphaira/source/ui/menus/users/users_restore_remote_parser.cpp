#include "ui/menus/users/users_restore_remote.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "log.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sphaira::ui::menu::users {

namespace {

void SkipJsonWhitespace(const std::string& s, size_t& pos) {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\r' || s[pos] == '\n')) {
        pos++;
    }
}

auto ParseJsonString(const std::string& s, size_t& pos) -> std::optional<std::string> {
    SkipJsonWhitespace(s, pos);
    if (pos >= s.size() || s[pos] != '"') {
        return std::nullopt;
    }
    pos++;
    std::string out;
    while (pos < s.size()) {
        char c = s[pos++];
        if (c == '"') {
            return out;
        }
        if (c == '\\') {
            if (pos >= s.size()) {
                return std::nullopt;
            }
            char esc = s[pos++];
            switch (esc) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                default: out += esc; break;
            }
        } else {
            out += c;
        }
    }
    return std::nullopt;
}

auto ParseJsonInt(const std::string& s, size_t& pos) -> std::optional<s64> {
    SkipJsonWhitespace(s, pos);
    if (pos >= s.size()) {
        return std::nullopt;
    }
    size_t start = pos;
    if (s[pos] == '-' || s[pos] == '+') {
        pos++;
    }
    if (pos >= s.size() || !std::isdigit(static_cast<unsigned char>(s[pos]))) {
        return std::nullopt;
    }
    while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) {
        pos++;
    }
    const auto num_str = s.substr(start, pos - start);
    char* endptr = nullptr;
    const auto val = std::strtoll(num_str.c_str(), &endptr, 10);
    if (endptr == num_str.c_str()) {
        return std::nullopt;
    }
    return val;
}

} // namespace

auto ParseRemoteListResponse(const std::string& json) -> std::optional<std::pair<std::string, std::vector<RemotePackEntry>>> {
    size_t path_pos = json.find("\"path\"");
    if (path_pos == std::string::npos) {
        return std::nullopt;
    }
    size_t colon_pos = json.find(':', path_pos + 6);
    if (colon_pos == std::string::npos) {
        return std::nullopt;
    }
    colon_pos++;
    auto root_path_opt = ParseJsonString(json, colon_pos);
    if (!root_path_opt) {
        return std::nullopt;
    }
    std::string root_path = *root_path_opt;

    size_t entries_pos = json.find("\"entries\"", colon_pos);
    if (entries_pos == std::string::npos) {
        entries_pos = json.find("\"entries\"");
        if (entries_pos == std::string::npos) {
            return std::nullopt;
        }
    }
    size_t array_open = json.find('[', entries_pos);
    if (array_open == std::string::npos) {
        return std::nullopt;
    }

    std::vector<RemotePackEntry> out_entries;
    size_t pos = array_open + 1;
    while (pos < json.size()) {
        SkipJsonWhitespace(json, pos);
        if (pos >= json.size() || json[pos] == ']') {
            break;
        }
        if (json[pos] == ',') {
            pos++;
            continue;
        }
        if (json[pos] != '{') {
            pos++;
            continue;
        }
        pos++;

        std::string entry_name;
        int entry_type = -1;
        s64 entry_size = 0;
        while (pos < json.size() && json[pos] != '}') {
            SkipJsonWhitespace(json, pos);
            if (pos >= json.size() || json[pos] == '}') {
                break;
            }
            if (json[pos] == ',') {
                pos++;
                continue;
            }
            auto key_opt = ParseJsonString(json, pos);
            if (!key_opt) {
                break;
            }
            SkipJsonWhitespace(json, pos);
            if (pos < json.size() && json[pos] == ':') {
                pos++;
            }
            SkipJsonWhitespace(json, pos);
            if (*key_opt == "name") {
                auto name_val = ParseJsonString(json, pos);
                if (name_val) {
                    entry_name = *name_val;
                }
            } else if (*key_opt == "type") {
                auto type_val = ParseJsonInt(json, pos);
                if (type_val) {
                    entry_type = static_cast<int>(*type_val);
                }
            } else if (*key_opt == "size") {
                auto size_val = ParseJsonInt(json, pos);
                if (size_val) {
                    entry_size = *size_val;
                }
            } else if (pos < json.size() && json[pos] == '"') {
                ParseJsonString(json, pos);
            } else {
                while (pos < json.size() && json[pos] != ',' && json[pos] != '}') {
                    pos++;
                }
            }
        }
        if (pos < json.size() && json[pos] == '}') {
            pos++;
        }

        if (entry_name.empty() || entry_name == "." || entry_name == "..") {
            continue;
        }
        if (entry_name.find('/') != std::string::npos || entry_name.find('\\') != std::string::npos) {
            continue;
        }

        bool is_dir = (entry_type == 1);
        bool is_zip = (!is_dir && entry_name.size() > 4 &&
            (entry_name.ends_with(".zip") || entry_name.ends_with(".ZIP")));

        if (!is_dir && !is_zip) {
            continue;
        }

        std::string full_remote_path = root_path;
        if (!full_remote_path.empty() && full_remote_path.back() != '/') {
            full_remote_path += '/';
        }
        full_remote_path += entry_name;

        out_entries.push_back(RemotePackEntry{
            .name = entry_name,
            .remote_path = std::move(full_remote_path),
            .is_archive = is_zip,
            .size = entry_size,
        });
    }

    return std::make_pair(std::move(root_path), std::move(out_entries));
}

auto ParseManifestResponse(const std::string& json, const std::string& remote_pack_root)
    -> std::optional<std::vector<ManifestFile>> {
    size_t root_pos = json.find("\"root\"");
    if (root_pos == std::string::npos) {
        return std::nullopt;
    }
    size_t colon_pos = json.find(':', root_pos + 6);
    if (colon_pos == std::string::npos) {
        return std::nullopt;
    }
    colon_pos++;
    auto root_opt = ParseJsonString(json, colon_pos);
    if (!root_opt) {
        return std::nullopt;
    }
    std::string root_prefix = *root_opt;
    if (!root_prefix.empty() && root_prefix.back() != '/') {
        root_prefix += '/';
    }

    size_t files_pos = json.find("\"files\"", colon_pos);
    if (files_pos == std::string::npos) {
        files_pos = json.find("\"files\"");
        if (files_pos == std::string::npos) {
            return std::nullopt;
        }
    }
    size_t array_open = json.find('[', files_pos);
    if (array_open == std::string::npos) {
        return std::nullopt;
    }

    std::vector<ManifestFile> files;
    size_t pos = array_open + 1;
    while (pos < json.size()) {
        SkipJsonWhitespace(json, pos);
        if (pos >= json.size() || json[pos] == ']') {
            break;
        }
        if (json[pos] == ',') {
            pos++;
            continue;
        }
        if (json[pos] != '{') {
            pos++;
            continue;
        }
        pos++;

        std::string file_path;
        std::optional<s64> file_size;

        while (pos < json.size() && json[pos] != '}') {
            SkipJsonWhitespace(json, pos);
            if (pos >= json.size() || json[pos] == '}') {
                break;
            }
            if (json[pos] == ',') {
                pos++;
                continue;
            }
            auto key_opt = ParseJsonString(json, pos);
            if (!key_opt) {
                break;
            }
            SkipJsonWhitespace(json, pos);
            if (pos < json.size() && json[pos] == ':') {
                pos++;
            }
            SkipJsonWhitespace(json, pos);
            if (*key_opt == "path") {
                auto path_val = ParseJsonString(json, pos);
                if (path_val) {
                    file_path = *path_val;
                }
            } else if (*key_opt == "size") {
                auto size_val = ParseJsonInt(json, pos);
                if (size_val && *size_val >= 0) {
                    file_size = *size_val;
                }
            } else if (pos < json.size() && json[pos] == '"') {
                ParseJsonString(json, pos);
            } else {
                while (pos < json.size() && json[pos] != ',' && json[pos] != '}') {
                    pos++;
                }
            }
        }
        if (pos < json.size() && json[pos] == '}') {
            pos++;
        }

        if (file_path.empty() || !file_size.has_value() || *file_size < 0) {
            return std::nullopt;
        }

        if (!file_path.starts_with(root_prefix)) {
            log_write("[USER_TRANSFER] Manifest path %s does not start with root %s\n",
                file_path.c_str(), root_prefix.c_str());
            return std::nullopt;
        }

        std::string rel = file_path.substr(root_prefix.size());
        if (rel.empty() || rel.front() == '/' || rel.back() == '/' ||
            rel.find('\\') != std::string::npos || rel.find(':') != std::string::npos) {
            log_write("[USER_TRANSFER] Invalid relative path: %s\n", rel.c_str());
            return std::nullopt;
        }

        size_t start = 0;
        bool valid_comps = true;
        while (start < rel.size()) {
            size_t slash = rel.find('/', start);
            std::string comp = (slash == std::string::npos) ? rel.substr(start) : rel.substr(start, slash - start);
            if (comp.empty() || comp == "." || comp == "..") {
                valid_comps = false;
                break;
            }
            if (slash == std::string::npos) {
                break;
            }
            start = slash + 1;
        }
        if (!valid_comps) {
            log_write("[USER_TRANSFER] Invalid relative component in: %s\n", rel.c_str());
            return std::nullopt;
        }

        files.push_back({file_path, rel, *file_size});
    }

    if (files.empty()) {
        return std::nullopt;
    }

    return files;
}

} // namespace sphaira::ui::menu::users
