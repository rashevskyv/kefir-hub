#include "ui/menus/cheats/cheat_download_menu.hpp"
#include "ui/menus/cheats/cheat_files_menu.hpp"
#include "ui/menus/cheats/cheats_dmnt.hpp"
#include "ui/menus/cheats/cheats_lookup.hpp"
#include "ui/menus/cheats/cheats_db.hpp"

#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/error_box.hpp"

#include "app.hpp"
#include "log.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "yyjson_helper.hpp"
#include "swkbd.hpp"
#include "utils/utils.hpp"

#include <yyjson.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <format>
#include <optional>
#include <ranges>
#include <sstream>
#include <switch.h>
#include <algorithm>
#include <set>
#include <map>

namespace sphaira::ui::menu::hats::detail {

auto CleanCheatContent(const std::string& content) -> std::string {
    std::istringstream stream(content);
    std::string line;
    std::string cleaned_content;
    bool in_cheat = false;

    while (std::getline(stream, line)) {
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        // Skip empty lines
        if (line.empty()) {
            if (in_cheat) {
                cleaned_content += "\n";
            }
            continue;
        }

        // Check if this is a cheat title/master-code line [Title] or {Title}
        if (line.size() > 2 &&
            ((line.front() == '[' && line.back() == ']') ||
             (line.front() == '{' && line.back() == '}'))) {
            std::string title = line.substr(1, line.length() - 2);
            std::string lower_title = title;
            std::transform(lower_title.begin(), lower_title.end(), lower_title.begin(), ::tolower);

            // Check if this should be skipped
            bool should_skip = false;
            if (lower_title.find("www.") != std::string::npos) should_skip = true;  // Website URLs
            if (lower_title.find("credits:") == 0) should_skip = true;  // credits: author
            if (lower_title.find("credit:") == 0) should_skip = true;   // credit: author
            if (lower_title == "credits") should_skip = true;
            if (lower_title == "credit") should_skip = true;

            if (should_skip) {
                // Skip this entry and its content until next cheat
                in_cheat = false;
                continue;
            }

            // Valid cheat title, add it
            cleaned_content += line + "\n";
            in_cheat = true;
        } else if (in_cheat) {
            // Add content lines if we're in a valid cheat
            cleaned_content += line + "\n";
        }
    }

    // Remove trailing newlines
    while (!cleaned_content.empty() && (cleaned_content.back() == '\n' || cleaned_content.back() == '\r')) {
        cleaned_content.pop_back();
    }

    return cleaned_content;
}

auto ParseCheatslipsCheats(const std::string& json_str, const std::string& target_build_id) -> std::vector<CheatEntry> {
    std::vector<CheatEntry> cheats;

    // Log raw response for debugging
    log_write("[Cheats] Parsing API response, target Build ID: %s\n", target_build_id.c_str());

    yyjson_doc* doc = yyjson_read(json_str.data(), json_str.size(), 0);
    if (!doc) {
        log_write("[Cheats] Failed to parse CheatSlips JSON\n");
        return cheats;
    }

    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    yyjson_val* root = yyjson_doc_get_root(doc);

    // Response is an array with a single game object
    if (yyjson_is_arr(root)) {
        log_write("[Cheats] Response is an array\n");
        // Get the first (and usually only) game object
        size_t idx, max;
        yyjson_val* game_val;
        yyjson_arr_foreach(root, idx, max, game_val) {
            if (!yyjson_is_obj(game_val)) continue;

            // Get game name for context
            yyjson_val* name_val = yyjson_obj_get(game_val, "name");
            std::string game_name = name_val && yyjson_is_str(name_val) ? yyjson_get_str(name_val) : "";

            // Get cheats array for this version
            yyjson_val* cheats_arr = yyjson_obj_get(game_val, "cheats");
            if (!cheats_arr || !yyjson_is_arr(cheats_arr)) continue;

            log_write("[Cheats] Processing game: %s with %zu cheat entries\n", game_name.c_str(), yyjson_arr_size(cheats_arr));

            size_t cheat_idx, cheat_max;
            yyjson_val* cheat_val;
            yyjson_arr_foreach(cheats_arr, cheat_idx, cheat_max, cheat_val) {
                if (!yyjson_is_obj(cheat_val)) continue;

                // Get buildid (note: lowercase field name in API)
                yyjson_val* build_id_val = yyjson_obj_get(cheat_val, "buildid");
                std::string build_id = build_id_val && yyjson_is_str(build_id_val) ? yyjson_get_str(build_id_val) : "";

                // Get content
                yyjson_val* content_val = yyjson_obj_get(cheat_val, "content");
                if (!content_val || !yyjson_is_str(content_val)) continue;

                const char* content = yyjson_get_str(content_val);

                // Check if API returned quota exceeded message
                if (strstr(content, "Quota exceeded") || strstr(content, "quota exceeded")) {
                    log_write("[Cheats] API quota exceeded, skipping cheat\n");
                    continue; // Skip quota-exceeded cheats entirely
                }

                // Only add cheats matching the target Build ID (case-insensitive)
                if (!target_build_id.empty() && !StringsEqualIgnoreCase(build_id, target_build_id)) {
                    log_write("[Cheats] Skipping cheat with Build ID: %s (target: %s)\n",
                               build_id.c_str(), target_build_id.c_str());
                    continue;
                }

                // Get titles array and parse into individual cheat entries
                // CheatSlips returns ALL cheats in a single content field
                // We need to parse it and create individual entries for each cheat
                std::string raw_content = yyjson_get_str(content_val);
                std::string cleaned_content = CleanCheatContent(raw_content);

                // Parse the cleaned content to extract individual cheats
                std::istringstream content_stream(cleaned_content);
                std::string line;
                std::string current_cheat_name;
                std::string current_cheat_content;
                bool in_cheat = false;

                while (std::getline(content_stream, line)) {
                    // Trim whitespace
                    line.erase(0, line.find_first_not_of(" \t\r\n"));
                    line.erase(line.find_last_not_of(" \t\r\n") + 1);

                    // Check for cheat title/master-code format [Title] or {Title}
                    if (line.size() > 2 &&
                        ((line.front() == '[' && line.back() == ']') ||
                         (line.front() == '{' && line.back() == '}'))) {
                        // Save previous cheat if exists
                        if (in_cheat && !current_cheat_name.empty()) {
                            CheatEntry entry;
                            entry.name = current_cheat_name;
                            entry.content = current_cheat_content;
                            entry.build_id = build_id;
                            entry.source = CheatSource::Cheatslips;
                            entry.selected = false;
                            cheats.push_back(std::move(entry));
                            log_write("[Cheats] Parsed cheat: %s\n", current_cheat_name.c_str());
                        }

                        // Start new cheat
                        current_cheat_name = line.substr(1, line.length() - 2);
                        current_cheat_content = line + "\n";
                        in_cheat = true;
                    } else if (in_cheat) {
                        // Add line to current cheat content
                        current_cheat_content += line + "\n";
                    }
                }

                // Don't forget the last cheat
                if (in_cheat && !current_cheat_name.empty()) {
                    CheatEntry entry;
                    entry.name = current_cheat_name;
                    entry.content = current_cheat_content;
                    entry.build_id = build_id;
                    entry.source = CheatSource::Cheatslips;
                    entry.selected = false;
                    cheats.push_back(std::move(entry));
                    log_write("[Cheats] Parsed cheat: %s\n", current_cheat_name.c_str());
                }

                log_write("[Cheats] Total cheats parsed from CheatSlips: %zu\n", cheats.size());
            }
        }
    } else if (yyjson_is_obj(root)) {
        log_write("[Cheats] Response is an object (error or single game)\n");
        // Check for error message
        yyjson_val* error_val = yyjson_obj_get(root, "error");
        if (error_val && yyjson_is_str(error_val)) {
            log_write("[Cheats] API Error: %s\n", yyjson_get_str(error_val));
        }
        // Check for message (like "Quota exceeded")
        yyjson_val* msg_val = yyjson_obj_get(root, "message");
        if (msg_val && yyjson_is_str(msg_val)) {
            log_write("[Cheats] API Message: %s\n", yyjson_get_str(msg_val));
        }

        // Check if response has "cheats" field directly
        yyjson_val* cheats_arr = yyjson_obj_get(root, "cheats");
        if (cheats_arr && yyjson_is_arr(cheats_arr)) {
            log_write("[Cheats] Found cheats array in object response\n");
            size_t cheat_idx, cheat_max;
            yyjson_val* cheat_val;
            yyjson_arr_foreach(cheats_arr, cheat_idx, cheat_max, cheat_val) {
                if (!yyjson_is_obj(cheat_val)) continue;

                yyjson_val* build_id_val = yyjson_obj_get(cheat_val, "buildid");
                std::string build_id = build_id_val && yyjson_is_str(build_id_val) ? yyjson_get_str(build_id_val) : "";

                yyjson_val* content_val = yyjson_obj_get(cheat_val, "content");
                if (!content_val || !yyjson_is_str(content_val)) continue;

                // Only add cheats matching the target Build ID (case-insensitive)
                if (!target_build_id.empty() && !StringsEqualIgnoreCase(build_id, target_build_id)) {
                    continue;
                }

                // Parse content into individual cheat entries
                std::string raw_content = yyjson_get_str(content_val);
                std::string cleaned_content = CleanCheatContent(raw_content);

                // Parse the cleaned content to extract individual cheats
                std::istringstream content_stream(cleaned_content);
                std::string line;
                std::string current_cheat_name;
                std::string current_cheat_content;
                bool in_cheat = false;

                while (std::getline(content_stream, line)) {
                    // Trim whitespace
                    line.erase(0, line.find_first_not_of(" \t\r\n"));
                    line.erase(line.find_last_not_of(" \t\r\n") + 1);

                    // Check for cheat title/master-code format [Title] or {Title}
                    if (line.size() > 2 &&
                        ((line.front() == '[' && line.back() == ']') ||
                         (line.front() == '{' && line.back() == '}'))) {
                        // Save previous cheat if exists
                        if (in_cheat && !current_cheat_name.empty()) {
                            CheatEntry entry;
                            entry.name = current_cheat_name;
                            entry.content = current_cheat_content;
                            entry.build_id = build_id;
                            entry.source = CheatSource::Cheatslips;
                            entry.selected = false;
                            cheats.push_back(std::move(entry));
                        }

                        // Start new cheat
                        current_cheat_name = line.substr(1, line.length() - 2);
                        current_cheat_content = line + "\n";
                        in_cheat = true;
                    } else if (in_cheat) {
                        // Add line to current cheat content
                        current_cheat_content += line + "\n";
                    }
                }

                // Don't forget the last cheat
                if (in_cheat && !current_cheat_name.empty()) {
                    CheatEntry entry;
                    entry.name = current_cheat_name;
                    entry.content = current_cheat_content;
                    entry.build_id = build_id;
                    entry.source = CheatSource::Cheatslips;
                    entry.selected = false;
                    cheats.push_back(std::move(entry));
                }
            }
        }
    }

    log_write("[Cheats] Parsed %zu cheats from CheatSlips matching Build ID %s\n",
              cheats.size(), target_build_id.c_str());
    return cheats;
}

auto ParseNxDbCheats(const std::string& json_str, const std::string& target_build_id) -> std::vector<CheatEntry> {
    std::vector<CheatEntry> cheats;
    const auto normalized_build_id = NormalizeBuildId(target_build_id);

    log_write("[Cheats] Parsing nx-cheats-db JSON, target Build ID: %s\n", normalized_build_id.c_str());

    yyjson_doc* doc = yyjson_read(json_str.data(), json_str.size(), 0);
    if (!doc) {
        log_write("[Cheats] Failed to parse nx-cheats-db JSON\n");
        return cheats;
    }

    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        log_write("[Cheats] nx-cheats-db JSON root is not an object\n");
        return cheats;
    }

    // Look for the target Build ID in the JSON. Build IDs must still refer to the
    // same exact hex value, but tolerate letter-case differences between sources.
    yyjson_val* build_id_val = yyjson_obj_get(root, normalized_build_id.c_str());
    std::string resolved_build_id = normalized_build_id;

    if (!build_id_val || !yyjson_is_obj(build_id_val)) {
        yyjson_val* key;
        yyjson_obj_iter iter;
        yyjson_obj_iter_init(root, &iter);

        while ((key = yyjson_obj_iter_next(&iter))) {
            const char* key_str = yyjson_get_str(key);
            yyjson_val* value = yyjson_obj_iter_get_val(key);
            if (!key_str || !yyjson_is_obj(value)) {
                continue;
            }
            if (StringsEqualIgnoreCase(key_str, normalized_build_id)) {
                build_id_val = value;
                resolved_build_id = NormalizeBuildId(key_str);
                break;
            }
        }
    }

    // If not found, return empty cheats - NO fallback
    if (!build_id_val || !yyjson_is_obj(build_id_val)) {
        log_write("[Cheats] Build ID %s not found in nx-cheats-db\n", normalized_build_id.c_str());
        log_write("[Cheats] Available build IDs in this file:\n");

        // List available build IDs for debugging
        yyjson_val* key;
        yyjson_obj_iter iter;
        yyjson_obj_iter_init(root, &iter);
        while ((key = yyjson_obj_iter_next(&iter))) {
            const char* key_str = yyjson_get_str(key);
            if (key_str && std::string(key_str) != "attribution") {
                log_write("[Cheats]   - %s\n", key_str);
            }
        }

        log_write("[Cheats] No cheats will be shown - Build ID must match exactly\n");
        return cheats;
    }

    // Parse cheats from the build ID object
    yyjson_val* key;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(build_id_val, &iter);

    while ((key = yyjson_obj_iter_next(&iter))) {
        const char* cheat_name = yyjson_get_str(key);
        yyjson_val* cheat_content_val = yyjson_obj_iter_get_val(key);

        if (!cheat_content_val || !yyjson_is_str(cheat_content_val)) {
            continue;
        }

        const char* content = yyjson_get_str(cheat_content_val);

        // Try to extract name from key first
        std::string name_str;
        std::string content_str = content;

        if (cheat_name && strlen(cheat_name) > 0) {
            name_str = cheat_name;

            // Check if this is a game header in format {- Game Name -}
            // Convert it to [Game Name] format
            if (name_str.size() > 2 && name_str[0] == '{' && name_str[name_str.size() - 1] == '}') {
                // Remove braces and dashes
                std::string inner = name_str.substr(1, name_str.size() - 2);
                // Remove leading/trailing dashes and spaces
                size_t start = inner.find_first_not_of("- ");
                size_t end = inner.find_last_not_of("- ");
                if (start != std::string::npos && end != std::string::npos) {
                    inner = inner.substr(start, end - start + 1);
                }
                name_str = inner;
            }
            // Extract cheat name without brackets if present
            else if (name_str.size() > 2 && name_str[0] == '[' && name_str[name_str.size() - 1] == ']') {
                name_str = name_str.substr(1, name_str.size() - 2);
            }
        } else {
            // Key is empty, try to extract name from content (format: "[Name]\n{codes}")
            size_t bracket_start = content_str.find('[');
            size_t bracket_end = content_str.find(']', bracket_start);
            if (bracket_start != std::string::npos && bracket_end != std::string::npos) {
                name_str = content_str.substr(bracket_start + 1, bracket_end - bracket_start - 1);
            } else {
                name_str = "Unknown Cheat";
            }
        }

        // Filter out non-cheat entries
        std::string lower_name = name_str;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);

        // Skip metadata files (attribution entries)
        if (lower_name.find(".txt") != std::string::npos) continue;

        // Skip credits, website headers, etc.
        if (lower_name.find("www.") != std::string::npos) continue;
        if (lower_name.find("cheatslips") != std::string::npos) continue;
        if (lower_name.find("credits:") == 0) continue;
        if (lower_name.find("credit:") == 0) continue;
        if (lower_name == "credits") continue;
        if (lower_name == "credit") continue;
        if (lower_name.find("original code by") == 0) continue;  // Skip attribution entries

        // Skip entries with empty content (metadata headers only)
        std::string content_check = content_str;
        size_t first_newline_temp = content_check.find('\n');
        if (first_newline_temp != std::string::npos) {
            content_check = content_check.substr(first_newline_temp + 1);
        }
        // Trim whitespace
        content_check.erase(0, content_check.find_first_not_of(" \t\r\n"));
        content_check.erase(content_check.find_last_not_of(" \t\r\n") + 1);
        if (content_check.empty()) {
            log_write("[Cheats] Skipping entry with empty content: %s\n", name_str.c_str());
            continue;
        }

        // Process content: remove duplicate title line if it exists
        // The content might start with "{- Game Name -}\n" or "[Game Name]\n"
        // We need to remove this first line to avoid duplication
        std::string processed_content = content_str;
        size_t first_newline = processed_content.find('\n');
        if (first_newline != std::string::npos) {
            std::string first_line = processed_content.substr(0, first_newline);
            std::string first_line_lower = first_line;
            std::transform(first_line_lower.begin(), first_line_lower.end(), first_line_lower.begin(), ::tolower);

            // Check if first line matches the name (with or without brackets/dashes)
            bool matches = false;
            if (first_line.size() > 2 && first_line[0] == '[' && first_line[first_line.size() - 1] == ']') {
                std::string first_line_name = first_line.substr(1, first_line.size() - 2);
                if (first_line_name == name_str || first_line_lower.find(lower_name) != std::string::npos) {
                    matches = true;
                }
            }

            // Check for {- Game Name -} format
            if (first_line.size() > 2 && first_line[0] == '{' && first_line[first_line.size() - 1] == '}') {
                if (first_line_lower.find(lower_name) != std::string::npos) {
                    matches = true;
                }
            }

            // Remove first line if it's a duplicate title
            if (matches) {
                processed_content = processed_content.substr(first_newline + 1);
                log_write("[Cheats] Removed duplicate title line from content\n");
            }
        }

        CheatEntry entry;
        entry.name = name_str;
        entry.content = processed_content;
        entry.build_id = resolved_build_id;
        entry.source = CheatSource::NxDb;
        entry.selected = false;

        cheats.push_back(std::move(entry));
        log_write("[Cheats] Added nx-cheats-db cheat: %s\n", entry.name.c_str());
    }

    log_write("[Cheats] Parsed %zu cheats from nx-cheats-db for Build ID %s\n",
              cheats.size(), resolved_build_id.c_str());
    return cheats;
}

auto ExtractNxDbBuildIds(const std::string& json_str) -> std::vector<std::string> {
    std::vector<std::string> build_ids;

    yyjson_doc* doc = yyjson_read(json_str.data(), json_str.size(), 0);
    if (!doc) {
        log_write("[Cheats] Failed to parse nx-cheats-db JSON while extracting Build IDs\n");
        return build_ids;
    }

    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        log_write("[Cheats] nx-cheats-db JSON root is not an object while extracting Build IDs\n");
        return build_ids;
    }

    yyjson_val* key;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(root, &iter);

    while ((key = yyjson_obj_iter_next(&iter))) {
        const char* key_str = yyjson_get_str(key);
        if (!key_str || std::strcmp(key_str, "attribution") == 0) {
            continue;
        }

        const auto len = std::strlen(key_str);
        if (len == 16 || len == 32) {
            build_ids.emplace_back(NormalizeBuildId(key_str));
        }
    }

    std::sort(build_ids.begin(), build_ids.end());
    build_ids.erase(std::unique(build_ids.begin(), build_ids.end()), build_ids.end());
    return build_ids;
}
} // namespace sphaira::ui::menu::hats::detail
