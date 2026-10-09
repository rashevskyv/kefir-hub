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
    if (selected_count > DMNT_MAX_CHEATS) {
        prompt += "\n\nAtmosphere loads up to 127 cheats per file.\nThe rest will be skipped.";
    }

    App::Push<OptionBox>(
        prompt,
        "Cancel"_i18n, "Download", 1,
        [this](auto op_index) {
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
                [this](Result rc) {
                    if (R_SUCCEEDED(rc)) {
                        App::Notify("Cheats installed for " + m_game.name);
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

    // /atmosphere/contents/{titleid}/cheats/{buildid}.txt
    const auto cheats_dir = GetCheatsDirPath(title_id);
    fs.CreateDirectoryRecursively(cheats_dir.c_str());
    fs::FsPath file_path;
    std::snprintf(file_path, sizeof(file_path), "%s/%s.txt", cheats_dir.c_str(), build_id.c_str());

    log_write("[Cheats] Saving cheats to: %s\n", file_path.s);

    // The new cheats go after the ones already in the file, and the whole file is
    // sanitized again: dmnt rejects all of it if one part breaks its rules.
    std::string content;
    std::set<std::string> names;
    if (std::vector<u8> existing; fs.FileExists(file_path) && R_SUCCEEDED(fs.read_entire_file(file_path, existing))) {
        content.assign(existing.begin(), existing.end());
        std::istringstream stream(content);
        for (std::string line; std::getline(stream, line);) {
            line.erase(0, line.find_first_not_of(" \t"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);
            if (IsCheatHeaderLine(line)) {
                names.insert(GetCheatHeaderName(line));
            }
        }
    }

    size_t added_count = 0;
    for (const auto& cheat : cheats) {
        if (!cheat.selected || !names.insert(cheat.name).second) {
            continue;
        }

        // nx-cheats-db content already starts with [name]; others get one.
        const auto first_line = cheat.content.substr(0, cheat.content.find('\n'));
        content += "\n";
        if (!IsCheatHeaderLine(first_line) || GetCheatHeaderName(first_line) != cheat.name) {
            content += "[" + cheat.name + "]\n";
        }
        content += cheat.content + "\n";
        added_count++;
    }

    if (!added_count) {
        App::Notify("All cheats already exist!");
        return 0;
    }

    const auto sanitized = SanitizeCheatText(content);
    R_UNLESS(!sanitized.text.empty(), 1);

    const std::vector<u8> data(sanitized.text.begin(), sanitized.text.end());
    if (R_FAILED(fs.write_entire_file(file_path, data))) {
        log_write("[Cheats] Failed to write cheat file %s\n", file_path.s);
        return 1;
    }
    log_write("[Cheats] Wrote %s: %zu cheats, %zu new, %zu dropped\n",
        file_path.s, sanitized.cheats, added_count, sanitized.dropped);

    if (sanitized.dropped) {
        App::Notify("Atmosphere limits: skipped " + std::to_string(sanitized.dropped) + " cheat(s)");
    }

    return 0;
}

} // namespace detail
} // namespace sphaira::ui::menu::hats
