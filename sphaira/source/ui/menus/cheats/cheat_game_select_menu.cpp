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

        m_list->Draw(vg, theme, m_games.size(), m_index, [this, &image_load_count](auto* vg, auto* theme, Vec4 v, auto i) {
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
    }, current_layout - 1, "Choose how the cheat game list is displayed on screen."_i18n)->SetIcon(ActionIcon::Layout);
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

} // namespace sphaira::ui::menu::hats
