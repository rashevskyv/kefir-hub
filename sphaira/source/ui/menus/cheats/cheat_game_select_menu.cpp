#include "ui/menus/cheats/cheat_game_select_menu.hpp"
#include "ui/menus/cheats/cheat_download_menu.hpp"
#include "ui/menus/cheats/cheat_files_menu.hpp"
#include "ui/menus/cheats/cheats_dmnt.hpp"
#include "ui/menus/cheats/cheats_lookup.hpp"
#include "ui/menus/cheats/cheats_db.hpp"

#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/error_box.hpp"
#include "ui/scrollable_text.hpp"
#include "ui/sidebar.hpp"
#include "ui/menus/file_picker.hpp"

#include "app.hpp"
#include "log.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "threaded_file_transfer.hpp"
#include "image.hpp"
#include "i18n.hpp"
#include "nacp_util.hpp"
#include "yyjson_helper.hpp"
#include "swkbd.hpp"
#include "title_info.hpp"
#include "utils/devoptab.hpp"
#include "utils/utils.hpp"
#include "yati/nx/ns.hpp"
#include "yati/nx/es.hpp"
#include "yati/nx/ncm.hpp"
#include "yati/nx/nca.hpp"

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
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <map>

namespace sphaira::ui::menu::hats {

using namespace detail;

namespace {

bool LoadGameControlImage(GameCheatInfo& game, title::ThreadResultData* result) {
    if (!game.image && result && !result->icon.empty()) {
        const auto image = ImageLoadFromMemory(result->icon, ImageFlag_JPEG);
        if (!image.data.empty()) {
            game.image = nvgCreateImageRGBA(App::GetVg(), image.w, image.h, 0, image.data.data());
            return true;
        }
    }

    return false;
}

void LoadGameResult(GameCheatInfo& game, title::ThreadResultData* result) {
    if (!result) {
        return;
    }

    game.status = result->status;
    game.lang = result->lang;
    if (game.lang.name[0] == '\0') {
        std::snprintf(game.lang.name, sizeof(game.lang.name), "%s", game.name.c_str());
    }
}

auto GetGameDisplayName(const GameCheatInfo& game) -> const char* {
    return game.lang.name[0] != '\0' ? game.lang.name : game.name.c_str();
}

auto GetGameDisplayAuthor(const GameCheatInfo& game) -> const char* {
    return game.lang.author[0] != '\0' ? game.lang.author : "Installed Title";
}

void FreeGameEntry(NVGcontext* vg, GameCheatInfo& game) {
    if (game.image) {
        nvgDeleteImage(vg, game.image);
        game.image = 0;
    }
}

} // namespace

// ============================================================
// CheatGameSelectMenu - Select installed game
// ============================================================

CheatGameSelectMenu::CheatGameSelectMenu(CheatSource source)
    : grid::Menu{"Select Game", MenuFlag_None}, m_source(source) {

    // Add logout option for CheatSlips
    if (m_source == CheatSource::Cheatslips) {
        this->SetActions(
            std::make_pair(Button::A, Action{"Select"_i18n, [this](){
                if (!m_games.empty() && !m_scanning) {
                    OnSelect();
                }
            }}),
            std::make_pair(Button::B, Action{"Back"_i18n, [this](){
                SetPop();
            }}),
            std::make_pair(Button::X, Action{"Refresh"_i18n, [this](){
                m_loaded = false;
                ScanGames();
            }}),
            std::make_pair(Button::START, Action{"Options"_i18n, [this](){
                DisplayOptions();
            }}),
            std::make_pair(Button::Y, Action{"Account"_i18n, [this](){
                auto token = GetCheatslipsToken();
                if (!token.empty()) {
                    // Logged in - show logout option
                    App::Push<OptionBox>(
                        "Logged in to CheatSlips.\nLog out?",
                        "Cancel"_i18n, "Logout", 1,
                        [](auto op_index) {
                            if (!op_index || *op_index != 1) {
                                return;
                            }
                            // Delete token file
                            fs::FsNativeSd fs;
                            fs.DeleteFile(TOKEN_PATH);
                            // Also try AIO path
                            fs.DeleteFile(AIO_TOKEN_PATH);
                            App::Notify("Logged out from CheatSlips");
                        }
                    );
                } else {
                    // Logged out - show login option
                    App::Push<OptionBox>(
                        "Not logged in to CheatSlips.\nLog in for higher quotas?",
                        "Cancel"_i18n, "Login", 1,
                        [](auto op_index) {
                            if (!op_index || *op_index != 1) {
                                return;
                            }

                            // Push the login menu (OptionBox will close automatically)
                            App::Push<CheatslipsLoginMenu>();
                        }
                    );
                }
            }})
        );
    } else {
        this->SetActions(
            std::make_pair(Button::A, Action{"Select"_i18n, [this](){
                if (!m_games.empty() && !m_scanning) {
                    OnSelect();
                }
            }}),
            std::make_pair(Button::B, Action{"Back"_i18n, [this](){
                SetPop();
            }}),
            std::make_pair(Button::X, Action{"Refresh"_i18n, [this](){
                m_loaded = false;
                ScanGames();
            }}),
            std::make_pair(Button::START, Action{"Options"_i18n, [this](){
                DisplayOptions();
            }})
        );
    }

    OnLayoutChange();
    title::Init();
}

CheatGameSelectMenu::CheatGameSelectMenu(CheatSource source, const fs::FsPath& manual_cheat_path)
    : CheatGameSelectMenu(source) {
    m_manual_cheat_path = manual_cheat_path;
}

CheatGameSelectMenu::~CheatGameSelectMenu() {
    FreeGames();
    title::Exit();
}

void CheatGameSelectMenu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    if (!m_games.empty()) {
        m_list->OnUpdate(controller, touch, m_index, m_games.size(), [this](bool touch, auto i) {
            if (touch && m_index == i) {
                FireAction(Button::A);
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                SetIndex(i);
            }
        }, this);
    }
}

void CheatGameSelectMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    if (m_scanning) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "Scanning games...");
        return;
    }

    if (m_games.empty() && m_loaded) {
        // Show "no games" message
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f - 20.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "No games found");
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f + 20.f, 18.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "Press X to refresh");
        return;
    }

    if (!m_games.empty()) {
        if (m_layout.Get() == grid::LayoutType_HbMenu) {
            auto& game = m_games[m_index];
            char title_id[33];
            std::snprintf(title_id, sizeof(title_id), "%016lX v%u", game.title_id, game.version);
            DrawHbMenuHeader(vg, theme, game.image, GetGameDisplayName(game), GetGameDisplayAuthor(game), title_id, game.build_id.c_str());
        }

        const int image_load_max = 2;
        int image_load_count = 0;

        m_list->Draw(vg, theme, m_games.size(), [this, &image_load_count](auto* vg, auto* theme, Vec4 v, auto i) {
            auto& game = m_games[i];

            if (game.status == title::NacpLoadStatus::None) {
                std::snprintf(game.lang.name, sizeof(game.lang.name), "%s", game.name.c_str());
                title::PushAsync(game.title_id);
                game.status = title::NacpLoadStatus::Progress;
            } else if (game.status == title::NacpLoadStatus::Progress) {
                LoadGameResult(game, title::GetAsync(game.title_id));
            }

            if (image_load_count < image_load_max) {
                if (LoadGameControlImage(game, title::GetAsync(game.title_id))) {
                    image_load_count++;
                }
            }

            char title_id[33];
            std::snprintf(title_id, sizeof(title_id), "%016lX v%u", game.title_id, game.version);

            const auto selected = m_index == i;
            DrawEntry(vg, theme, m_layout.Get(), v, selected, game.image, GetGameDisplayName(game), GetGameDisplayAuthor(game), title_id);
        });
    }
}

void CheatGameSelectMenu::OnFocusGained() {
    MenuBase::OnFocusGained();

    if (!m_loaded && !m_scanning) {
        ScanGames();
    }
}

void CheatGameSelectMenu::SetIndex(s64 index) {
    if (m_games.empty()) {
        m_index = 0;
        this->SetSubHeading("0 / 0");
        return;
    }

    m_index = index;
    if (!m_index) {
        m_list->SetYoff(0);
    }

    this->SetSubHeading(std::to_string(m_index + 1) + " / " + std::to_string(m_games.size()));
    SetTitleSubHeading(GetGameDisplayName(m_games[m_index]), true);
}

void CheatGameSelectMenu::OnLayoutChange() {
    m_index = 0;
    grid::Menu::OnLayoutChange(m_list, m_layout.Get());
}

void CheatGameSelectMenu::DisplayOptions() {
    auto options = std::make_unique<Sidebar>("Cheats Options"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    SidebarEntryArray::Items layout_items;
    layout_items.push_back("Icon"_i18n);
    layout_items.push_back("Grid"_i18n);
    layout_items.push_back("HB Menu"_i18n);

    auto current_layout = m_layout.Get();
    if (current_layout == grid::LayoutType_List) {
        current_layout = grid::LayoutType_Grid;
        m_layout.Set(current_layout);
    }
    options->Add<SidebarEntryArray>("Layout"_i18n, layout_items, [this](s64& index_out){
        m_layout.Set(index_out + 1);
        OnLayoutChange();
    }, current_layout - 1, "Choose how the cheat game list is displayed on screen."_i18n);
}

void CheatGameSelectMenu::FreeGames() {
    auto* vg = App::GetVg();
    for (auto& game : m_games) {
        FreeGameEntry(vg, game);
    }

    m_games.clear();
}

void CheatGameSelectMenu::OnSelect() {
    if (m_games.empty() || m_index >= (s64)m_games.size()) {
        return;
    }

    const auto& game = m_games[m_index];

    if (m_source == CheatSource::ManualFile) {
        const auto build_id = ResolveManualTargetBuildId(game, &m_manual_cheat_path);
        if (!IsValidBuildId(build_id)) {
            App::Notify("Could not determine target Build ID");
            return;
        }

        fs::FsNativeSd fs;
        const auto dest_path = GetManualCheatImportPath(game.title_id, build_id);
        const auto source_build_id = NormalizeBuildId(GetFileStem(m_manual_cheat_path.s));
        auto prompt = "Import cheats to this game?\n\nTarget Build ID: " + build_id;
        if (IsValidBuildId(source_build_id) && source_build_id != build_id) {
            prompt += "\nSource Build ID: " + source_build_id;
            prompt += "\n\nThe file will be renamed to the target Build ID.";
        } else if (!IsValidBuildId(source_build_id)) {
            prompt += "\n\nThe file will be renamed to the target Build ID.";
        }

        const auto action = [game, path = m_manual_cheat_path, build_id](auto op_index) {
            if (!op_index || *op_index != 1) {
                return;
            }

            const auto rc = ImportManualCheatFile(game.title_id, path, build_id);
            if (R_SUCCEEDED(rc)) {
                App::Notify("Cheat file imported");
            } else {
                App::Push<ErrorBox>(rc, "Failed to import cheat file");
            }
        };

        if (fs.FileExists(dest_path)) {
            App::Push<OptionBox>(
                prompt + "\n\nExisting cheat file will be overwritten.",
                "Cancel"_i18n, "Import", 1,
                action
            );
            return;
        }

        App::Push<OptionBox>(
            prompt,
            "Cancel"_i18n, "Import", 1,
            action
        );
        return;
    }

    // For CheatSlips, check if we have a token
    if (m_source == CheatSource::Cheatslips) {
        auto token = GetCheatslipsToken();
        if (token.empty()) {
            // No token, prompt for login (like aio-switch-updater)
            App::Push<OptionBox>(
                "No CheatSlips token found.\nLogin for higher quotas?",
                "Cancel"_i18n, "Login", 1,
                [this, game](auto op_index) {
                    if (!op_index || *op_index != 1) {
                        // User cancelled, proceed without login
                        App::Push<CheatDownloadMenu>(m_source, game);
                        return;
                    }

                    // Push the login menu (OptionBox will close automatically)
                    App::Push<CheatslipsLoginMenu>();
                    // After login, proceed to download
                    App::Push<CheatDownloadMenu>(m_source, game);
                }
            );
            return;
        }
    }

    App::Push<CheatDownloadMenu>(m_source, game);
}

void CheatGameSelectMenu::ScanGames() {
    m_scanning = true;
    FreeGames();

    std::unordered_map<u64, CachedCheatMetadata> cached_entries;
    {
        mutexLock(&g_cheat_metadata_cache_mutex);
        ON_SCOPE_EXIT(mutexUnlock(&g_cheat_metadata_cache_mutex));
        cached_entries = LoadCheatMetadataCacheUnlocked();
    }

    // Initialize ns service (like original sphaira game_menu)
    Result rc = nsInitialize();
    if (R_FAILED(rc)) {
        log_write("[Cheats] nsInitialize failed: %x\n", rc);
        m_scanning = false;
        m_loaded = true;
        return;
    }

    // Use chunked approach like original sphaira (game_menu.cpp ScanHomebrew)
    std::vector<NsApplicationRecord> record_list(ENTRY_CHUNK_COUNT);
    std::unordered_set<u64> seen_title_ids;
    s32 offset = 0;

    while (true) {
        s32 record_count = 0;
        rc = nsListApplicationRecord(record_list.data(), record_list.size(), offset, &record_count);

        if (R_FAILED(rc)) {
            log_write("[Cheats] nsListApplicationRecord failed at offset %d: %x\n", offset, rc);
            break;
        }

        // Finished parsing all entries
        if (record_count == 0) {
            break;
        }

        log_write("[Cheats] Got %d records at offset %d\n", record_count, offset);

        // Process each record
        for (s32 i = 0; i < record_count; i++) {
            const auto& record = record_list[i];
            if (record.application_id == 0) continue;
            const auto base_title_id = GetBaseApplicationTitleId(record.application_id);
            if (!seen_title_ids.insert(base_title_id).second) {
                continue;
            }

            log_write("[Cheats] Processing %016lX\n", base_title_id);

            // Get version
            u32 version = GetTitleVersion(base_title_id);

            // Get title name using nsGetApplicationControlData
            std::string name = GetTitleName(base_title_id);
            if (name.empty()) {
                // Use placeholder name if we couldn't get it
                char placeholder[64];
                std::snprintf(placeholder, sizeof(placeholder), "Game %016llX", (unsigned long long)base_title_id);
                name = placeholder;
            }

            GameCheatInfo info;
            info.title_id = base_title_id;
            info.name = name;
            info.version = version;
            std::snprintf(info.lang.name, sizeof(info.lang.name), "%s", info.name.c_str());
            if (const auto it = cached_entries.find(info.title_id); it != cached_entries.end() &&
                IsValidBuildId(it->second.build_id)) {
                info.build_id = it->second.build_id;
            }

            m_games.push_back(std::move(info));
        }

        offset += record_count;
    }

    AppendGameCardGames(m_games, seen_title_ids);

    // Exit ns service when done
    nsExit();

    bool needs_cache_refresh = cached_entries.empty();
    if (!needs_cache_refresh && App::IsApplication()) {
        needs_cache_refresh = std::ranges::any_of(m_games, [&](const auto& game) {
            return !IsValidBuildId(game.build_id);
        });
    }

    if (needs_cache_refresh && App::IsApplication()) {
        log_write("[Cheats] Refreshing cheat metadata cache from game select menu\n");
        RefreshCheatMetadataCache();

        mutexLock(&g_cheat_metadata_cache_mutex);
        ON_SCOPE_EXIT(mutexUnlock(&g_cheat_metadata_cache_mutex));
        cached_entries = LoadCheatMetadataCacheUnlocked();

        for (auto& game : m_games) {
            if (const auto it = cached_entries.find(game.title_id); it != cached_entries.end() &&
                IsValidBuildId(it->second.build_id)) {
                game.build_id = it->second.build_id;
            }
        }
    }

    m_scanning = false;
    m_loaded = true;

    if (!m_games.empty()) {
        SetIndex(0);
    }

    log_write("[Cheats] Total: Found %zu games\n", m_games.size());
}

// ============================================================
// Manual Cheat Import Helpers
// ============================================================

namespace detail {

auto SanitizeManualCheatContent(const std::vector<u8>& data, std::string& out) -> bool {
    if (data.empty()) {
        return false;
    }

    // Atmosphere/EdiZon cheat parsers can be picky about BOM-prefixed files.
    size_t offset = 0;
    if (data.size() >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        offset = 3;
    } else if (data.size() >= 2 &&
               ((data[0] == 0xFF && data[1] == 0xFE) || (data[0] == 0xFE && data[1] == 0xFF))) {
        log_write("[Cheats] Manual import rejected UTF-16 cheat file\n");
        return false;
    }

    std::string text(reinterpret_cast<const char*>(data.data() + offset), data.size() - offset);
    if (!text.empty() && static_cast<unsigned char>(text.front()) == 0xEF) {
        return false;
    }

    std::istringstream stream(text);
    std::ostringstream sanitized;
    std::string line;
    std::string pending_header;
    bool wrote_any_code = false;
    bool wrote_code_for_header = false;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) {
            line.pop_back();
        }

        std::string trimmed = line;
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));

        if (trimmed.empty() || trimmed.rfind("//", 0) == 0 || IsParenthesizedNoteLine(trimmed)) {
            continue;
        }

        if (IsCheatHeaderLine(trimmed)) {
            pending_header = trimmed;
            wrote_code_for_header = false;
            continue;
        }

        trimmed = StripInlineCheatComment(trimmed);
        if (trimmed.empty()) {
            continue;
        }

        if (!IsHexCodeLine(trimmed) || pending_header.empty()) {
            continue;
        }

        if (!wrote_code_for_header) {
            sanitized << pending_header << '\n';
            wrote_code_for_header = true;
        }

        sanitized << NormalizeHexCodeLine(trimmed) << '\n';
        wrote_any_code = true;
    }

    out = sanitized.str();
    return wrote_any_code;
}

auto ImportManualCheatFile(u64 title_id, const fs::FsPath& source_path, const std::string& target_build_id) -> Result {
    fs::FsNativeSd fs;

    const auto build_id = NormalizeBuildId(target_build_id);
    if (!IsValidBuildId(build_id)) {
        log_write("[Cheats] Manual import rejected invalid target Build ID: %s\n", target_build_id.c_str());
        return 1;
    }

    std::vector<u8> data;
    R_TRY(fs.read_entire_file(source_path, data));
    std::string sanitized_content;
    if (!SanitizeManualCheatContent(data, sanitized_content)) {
        log_write("[Cheats] Manual import rejected invalid cheat file: %s\n", source_path.s);
        return 1;
    }

    const auto cheats_dir = GetCheatsDirPath(title_id);
    R_TRY(fs.CreateDirectoryRecursively(cheats_dir.c_str()));

    const auto dest_path = GetManualCheatImportPath(title_id, build_id);
    const auto content = std::vector<u8>(
        reinterpret_cast<const u8*>(sanitized_content.data()),
        reinterpret_cast<const u8*>(sanitized_content.data()) + sanitized_content.size()
    );
    R_TRY(fs.write_entire_file(dest_path, content));

    log_write("[Cheats] Imported manual cheat %s to %s\n", source_path.s, dest_path.s);
    return 0;
}

} // namespace detail

} // namespace sphaira::ui::menu::hats
