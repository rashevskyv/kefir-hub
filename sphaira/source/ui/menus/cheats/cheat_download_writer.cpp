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

namespace sphaira::ui::menu::hats {

using namespace detail;

void CheatDownloadMenu::DownloadCheats() {
    // Check if we have a valid build ID
    if (m_game.build_id.empty()) {
        App::Notify("No Build ID detected!");
        return;
    }

    // Check if cheats list is empty or still loading
    if (m_loading) {
        App::Notify("Still loading cheats, please wait...");
        return;
    }

    if (m_cheats.empty()) {
        App::Notify("No cheats available to download!");
        return;
    }

    // Count selected cheats
    size_t selected_count = 0;
    for (const auto& cheat : m_cheats) {
        if (cheat.selected) selected_count++;
    }

    if (selected_count == 0) {
        App::Notify("No cheats selected!");
        return;
    }

    auto prompt = "Download " + std::to_string(selected_count) + " cheat(s)?";
    if (selected_count > ATMOSPHERE_MAX_CHEATS_PER_FILE) {
        prompt += "\n\nAtmosphere/Edizon supports 128 cheats per file.\nOnly the first 128 new cheats will be installed.";
    }

    App::Push<OptionBox>(
        prompt,
        "Cancel"_i18n, "Download", 1,
        [this, selected_count](auto op_index) {
            if (!op_index || *op_index != 1) {
                return;
            }

            App::Push<ProgressBox>(0, "Downloading"_i18n, m_game.name,
                [this](auto pbox) -> Result {
                    // Collect selected cheats
                    std::vector<CheatEntry> selected;
                    for (const auto& cheat : m_cheats) {
                        if (cheat.selected) {
                            selected.push_back(cheat);
                        }
                    }

                    return WriteCheatFile(m_game.title_id, m_game.build_id, selected);
                },
                [this, selected_count](Result rc) {
                    if (R_SUCCEEDED(rc)) {
                        if (selected_count > ATMOSPHERE_MAX_CHEATS_PER_FILE) {
                            App::Notify("Cheats installed up to the 128-entry limit");
                        } else {
                            App::Notify("Cheats installed for " + m_game.name);
                        }
                        SetPop();
                    } else {
                        App::PushErrorBox(rc, "Failed to download cheats"_i18n);
                    }
                }
            );
        }
    );
}


namespace detail {

auto WriteCheatFile(u64 title_id, const std::string& build_id, const std::vector<CheatEntry>& cheats) -> Result {
    fs::FsNativeSd fs;

    // Create cheats directory path: /atmosphere/contents/{titleid}/cheats/
    const auto cheats_dir = GetCheatsDirPath(title_id);
    fs.CreateDirectoryRecursively(cheats_dir.c_str());

    // Create file path: /atmosphere/contents/{titleid}/cheats/{buildid}.txt
    fs::FsPath file_path;
    std::snprintf(file_path, sizeof(file_path), "%s/%s.txt", cheats_dir.c_str(), build_id.c_str());

    log_write("[Cheats] Saving cheats to: %s\n", file_path.s);
    log_write("[Cheats] Build ID: %s, Title ID: %016lx\n", build_id.c_str(), title_id);

    // Parse existing file to get already saved cheats
    std::set<std::string> existing_cheat_names;
    size_t existing_cheat_count = 0;
    if (fs.FileExists(file_path)) {
        std::vector<u8> existing_data;
        if (R_SUCCEEDED(fs.read_entire_file(file_path, existing_data))) {
            std::string existing_content(existing_data.begin(), existing_data.end());
            // Parse cheat names from existing content
            std::istringstream stream(existing_content);
            std::string line;
            while (std::getline(stream, line)) {
                // Trim whitespace
                line.erase(0, line.find_first_not_of(" \t\r\n"));
                line.erase(line.find_last_not_of(" \t\r\n") + 1);

                // Check for cheat title/master-code format [Title] or {Title}
                if (IsCheatHeaderLine(line)) {
                    std::string name = GetCheatHeaderName(line);
                    if (existing_cheat_names.insert(name).second) {
                        existing_cheat_count++;
                    }
                }
            }
            log_write("[Cheats] Found %zu existing cheats in file\n", existing_cheat_names.size());
        }
    }

    // Build Atmosphere-safe cheat file content.
    std::string content;

    // Group cheats by source
    std::map<CheatSource, std::vector<const CheatEntry*>> cheats_by_source;
    for (const auto& cheat : cheats) {
        if (!cheat.selected) continue;
        cheats_by_source[cheat.source].push_back(&cheat);
    }

    // Atmosphere's standard cheat tooling is limited to 128 cheats per file.
    size_t total_cheat_count = existing_cheat_count;
    size_t added_count = 0;
    size_t duplicate_count = 0;
    size_t limit_skipped_count = 0;
    for (const auto& [source, source_cheats] : cheats_by_source) {
        // Only process if we have cheats from this source
        if (source_cheats.empty()) continue;

        // Add cheats from this source
        for (const auto* cheat : source_cheats) {
            // Skip if this cheat already exists (by name)
            if (existing_cheat_names.count(cheat->name)) {
                log_write("[Cheats] Skipping duplicate cheat: %s\n", cheat->name.c_str());
                duplicate_count++;
                continue;
            }

            if (total_cheat_count >= ATMOSPHERE_MAX_CHEATS_PER_FILE) {
                log_write("[Cheats] Skipping cheat due to Atmosphere limit (%zu): %s\n",
                    ATMOSPHERE_MAX_CHEATS_PER_FILE, cheat->name.c_str());
                limit_skipped_count++;
                continue;
            }

            // Check if content already starts with [cheat_name]
            // If so, don't duplicate it (nx-cheats-db format already has it)
            std::string content_to_write = cheat->content;
            if (!content_to_write.empty()) {
                // Check if first line is [Name]
                size_t first_newline = content_to_write.find('\n');
                if (first_newline != std::string::npos) {
                    std::string first_line = content_to_write.substr(0, first_newline);
                    // Remove brackets for comparison
                    if (IsCheatHeaderLine(first_line)) {
                        std::string first_line_name = GetCheatHeaderName(first_line);
                        // Check if it matches the cheat name
                        if (first_line_name == cheat->name) {
                            // Content already has [name] prefix, use it as-is
                            content_to_write += "\n";
                        } else {
                            // First line is different, add our prefix
                            content_to_write = "[" + cheat->name + "]\n" + content_to_write + "\n";
                        }
                    } else {
                        // No [name] prefix in content, add it
                        content_to_write = "[" + cheat->name + "]\n" + content_to_write + "\n";
                    }
                } else {
                    // Single line or no newlines, add prefix
                    content_to_write = "[" + cheat->name + "]\n" + content_to_write + "\n";
                }
            }

            content += SanitizeCheatContentForAtmosphere(content_to_write);
            existing_cheat_names.insert(cheat->name); // Mark as added
            total_cheat_count++;
            added_count++;
        }

        content += "\n";
    }

    // If no new cheats to add (all were duplicates)
    if (content.empty()) {
        log_write("[Cheats] No new cheats to add (all duplicates)\n");
        if (limit_skipped_count) {
            App::Notify("Cheat file already has 128 entries");
        } else {
            App::Notify("All cheats already exist!");
        }
        return 0;
    }

    // If file exists, append to it; otherwise create new
    const auto content_data = std::vector<u8>(
        reinterpret_cast<const u8*>(content.data()),
        reinterpret_cast<const u8*>(content.data()) + content.size()
    );

    if (fs.FileExists(file_path)) {
        // Append to existing file
        FILE* f = fopen(file_path.s, "a");
        if (f) {
            fwrite(content.data(), 1, content.size(), f);
            fclose(f);
            log_write("[Cheats] Appended %zu cheats to %s (%zu duplicates, %zu limit-skipped)\n",
                added_count, file_path.s, duplicate_count, limit_skipped_count);
        } else {
            log_write("[Cheats] Failed to open file for appending: %s\n", file_path.s);
            return 1;
        }
    } else {
        // Write new file
        if (R_FAILED(fs.write_entire_file(file_path, content_data))) {
            log_write("[Cheats] Failed to write cheat file %s\n", file_path.s);
            return 1;
        }
        log_write("[Cheats] Wrote %zu cheats to %s (%zu duplicates, %zu limit-skipped)\n",
            added_count, file_path.s, duplicate_count, limit_skipped_count);
    }

    if (limit_skipped_count) {
        App::Notify("Installed first 128 cheats; skipped " + std::to_string(limit_skipped_count));
    }

    return 0;
}


} // namespace detail
} // namespace sphaira::ui::menu::hats
