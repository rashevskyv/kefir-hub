#include "ui/menus/cheats_menu.hpp"
#include "ui/menus/cheats/cheats_ops.hpp"
#include "ui/menus/cheats/cheat_files_menu.hpp"
#include "ui/menus/cheats/cheat_game_select_menu.hpp"
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
#include "image.hpp"
#include "i18n.hpp"
#include "nacp_util.hpp"
#include "swkbd.hpp"
#include "title_info.hpp"
#include "utils/devoptab.hpp"
#include "utils/utils.hpp"

#include <switch.h>
#include <algorithm>
#include <ranges>
#include <vector>

namespace sphaira::ui::menu::hats {

using namespace detail;

// CheatsMenu - Main menu with cheat management options
// ============================================================

CheatsMenu::CheatsMenu() : MenuBase{"Cheats"_i18n, MenuFlag_None} {
    // Main cheat management options
    m_items = {
        {"Download Kefir Cheats"_i18n, "Full KefirUpdater cheats pack"_i18n},
        {"Download 60FPS/GFX Cheats"_i18n, "KefirUpdater performance/graphics pack"_i18n},
        {"Download Exact Cheats"_i18n, "Select game and match Build ID"_i18n},
        {"Import From File"_i18n, "Import a local cheat .txt file"_i18n},
        {"View Cheats"_i18n, "View installed cheat codes"_i18n},
        {"Delete All Cheats"_i18n, "Delete all existing cheat codes"_i18n},
        {"Delete Orphaned"_i18n, "Delete cheats for uninstalled games"_i18n},
        {"Clear Cheats Cache"_i18n, "Delete cached cheats database"_i18n}
    };

    this->SetActions(
        std::make_pair(Button::A, Action{"Select"_i18n, [this](){
            OnSelect();
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }})
    );

    const Vec4 v{75, GetY() + 42.f, 1220.f - 150.f, 60.f};
    m_list = std::make_unique<List>(1, 8, m_pos, v);
    m_list->SetLayout(List::Layout::GRID);
}

CheatsMenu::~CheatsMenu() {
}

void CheatsMenu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    m_list->OnUpdate(controller, touch, m_index, m_items.size(), [this](bool touch, auto i) {
        if (touch && m_index == i) {
            FireAction(Button::A);
        } else {
            App::PlaySoundEffect(SoundEffect_Focus);
            SetIndex(i);
        }
    }, this);
}

void CheatsMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    constexpr float text_xoffset{15.f};

    m_list->Draw(vg, theme, m_items.size(), m_index, [this](auto* vg, auto* theme, Vec4 v, auto i) {
        const auto& [x, y, w, h] = v;
        const auto& item = m_items[i];

        auto text_id = ThemeEntryID_TEXT;
        if (m_index == i) {
            text_id = ThemeEntryID_TEXT_SELECTED;
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else {
            if (i != m_items.size() - 1) {
                gfx::drawRect(vg, x, y + h, w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
            }
        }

        gfx::drawTextArgs(vg, x + text_xoffset, y + h / 2.f - 6.f, 20.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
            theme->GetColour(text_id),
            "%s", item.first.c_str());

        gfx::drawTextArgs(vg, x + text_xoffset, y + h / 2.f + 14.f, 14.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "%s", item.second.c_str());
    });
}

void CheatsMenu::OnFocusGained() {
    MenuBase::OnFocusGained();
}

void CheatsMenu::SetIndex(s64 index) {
    m_index = index;
    if (!m_index) {
        m_list->SetYoff(0);
    }
}

void CheatsMenu::OnSelect() {
    switch (m_index) {
        case 0: // Download full KefirUpdater cheats pack
            PromptKefirCheatsDownload("Kefir Cheats", KEFIR_CHEATS_URL);
            break;
        case 1: // Download KefirUpdater 60FPS/GFX pack
            PromptKefirCheatsDownload("60FPS/GFX Cheats", KEFIR_CHEATS_GFX_URL);
            break;
        case 2: // Download exact cheats from nx-cheats-db
            App::Push<CheatGameSelectMenu>(CheatSource::NxDb);
            break;
        case 3: // Import local cheat file
            App::Push<filepicker::Menu>(
                [](const fs::FsPath& path) -> bool {
                    App::Push<CheatGameSelectMenu>(CheatSource::ManualFile, path);
                    return true;
                },
                std::vector<std::string>{"txt"}
            );
            break;
        case 4: // View Cheats
            App::Push<CheatViewMenu>();
            break;
        case 5: // Delete All Cheats
            App::Push<OptionBox>(
                "Delete all existing cheat codes?\nThis will remove ALL cheat files\nfor ALL installed games."_i18n,
                "Cancel"_i18n, "Delete"_i18n, 1,
                [](auto op_index) {
                    if (!op_index || *op_index != 1) {
                        return;
                    }
                    App::Push<ProgressBox>(0, "Deleting..."_i18n, "Cheats"_i18n,
                        [](auto pbox) -> Result {
                            return DeleteAllCheats();
                        },
                        [](Result rc) {
                            if (R_SUCCEEDED(rc)) {
                                App::Notify("Deleted all cheat codes"_i18n);
                            } else {
                                App::Push<ErrorBox>(rc, "Failed to delete cheats"_i18n);
                            }
                        }
                    );
                }
            );
            break;
        case 6: // Delete Orphaned Cheats
            App::Push<ProgressBox>(0, "Scanning..."_i18n, "Cheats"_i18n,
                [](auto pbox) -> Result {
                    return DeleteOrphanedCheats();
                },
                [](Result rc) {
                    if (rc == 0) {
                        App::Notify("No orphaned cheats found"_i18n);
                    } else if (rc > 0) {
                        char buf[128];
                        std::snprintf(buf, sizeof(buf), "Deleted %d orphaned cheats"_i18n.c_str(), (int)rc);
                        App::Notify(buf);
                    } else {
                        App::Push<ErrorBox>(rc, "Failed to delete orphaned cheats"_i18n);
                    }
                }
            );
            break;
        case 7: // Clear Cheats Cache
            App::Push<OptionBox>(
                "Clear cached cheats database?"_i18n,
                "Cancel"_i18n, "Clear"_i18n, 0,
                [](auto op_index) {
                    if (!op_index || *op_index != 1) {
                        return;
                    }
                    App::Push<ProgressBox>(0, "Clearing Cache"_i18n, "Cheats"_i18n,
                        [](auto pbox) -> Result {
                            return ClearCheatsCache();
                        },
                        [](Result rc) {
                            if (R_SUCCEEDED(rc)) {
                                App::Notify("Cheats cache cleared successfully"_i18n);
                            } else {
                                App::Push<ErrorBox>(rc, "Failed to clear cheats cache"_i18n);
                            }
                        }
                    );
                }
            );
            break;
    }
}

// ============================================================
// CheatViewMenu - View installed cheats
// ============================================================

CheatViewMenu::CheatViewMenu() : MenuBase{"Installed Cheats", MenuFlag_None} {
    this->SetActions(
        std::make_pair(Button::A, Action{"View"_i18n, [this](){
            if (!m_games.empty()) {
                OnSelect();
            }
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }}),
        std::make_pair(Button::Y, Action{"Delete"_i18n, [this](){
            if (!m_games.empty()) {
                OnDelete();
            }
        }})
    );

    const Vec4 v{75, GetY() + 42.f, 1220.f - 150.f, 60.f};
    m_list = std::make_unique<List>(1, 8, m_pos, v);
    m_list->SetLayout(List::Layout::GRID);
}

CheatViewMenu::~CheatViewMenu() {
}

void CheatViewMenu::Update(Controller* controller, TouchInfo* touch) {
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

void CheatViewMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    if (m_scanning) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "%s", "Scanning for cheats..."_i18n.c_str());
        return;
    }

    if (m_games.empty() && m_loaded) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "%s", "No installed cheats found"_i18n.c_str());
        return;
    }

    if (!m_games.empty()) {
        // Save and restore scissor to clip list drawing area
        nvgSave(vg);
        // Clip area starts below the header text; inflated by the selection
        // outline pad so the highlight of edge rows isn't clipped.
        const float p = gfx::SELECTION_OUTLINE_PAD;
        nvgScissor(vg, 75.f - p, GetY() + 40.f - p, 1220.f - 150.f + p * 2, 720.f - GetY() - 40.f + p * 2);
        ON_SCOPE_EXIT(nvgRestore(vg));

        constexpr float text_xoffset{15.f};

        m_list->Draw(vg, theme, m_games.size(), m_index, [this](auto* vg, auto* theme, Vec4 v, auto i) {
            const auto& [x, y, w, h] = v;
            const auto& game = m_games[i];

            auto text_id = ThemeEntryID_TEXT;
            if (m_index == i) {
                text_id = ThemeEntryID_TEXT_SELECTED;
                gfx::drawRectOutline(vg, theme, 4.f, v);
            } else {
                if (i != m_games.size() - 1) {
                    gfx::drawRect(vg, x, y + h, w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
                }
            }

            // Game name
            gfx::drawTextArgs(vg, x + text_xoffset, y + h / 2.f - 6.f, 20.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                theme->GetColour(text_id),
                "%s", game.name.c_str());

            // Title ID and cheat count
            gfx::drawTextArgs(vg, x + text_xoffset, y + h / 2.f + 14.f, 14.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                theme->GetColour(ThemeEntryID_TEXT_INFO),
                "%016lX - %zu cheat(s)", game.title_id, game.cheat_count);
        });
    }
}

void CheatViewMenu::OnFocusGained() {
    MenuBase::OnFocusGained();

    if (!m_loaded && !m_scanning) {
        ScanGamesWithCheats();
    }
}

void CheatViewMenu::SetIndex(s64 index) {
    m_index = index;
    if (!m_index) {
        m_list->SetYoff(0);
    }
}

void CheatViewMenu::OnSelect() {
    if (m_games.empty() || m_index >= (s64)m_games.size()) {
        return;
    }

    const auto& game = m_games[m_index];
    App::Push<CheatFilesMenu>(game);
}

void CheatViewMenu::OnDelete() {
    if (m_games.empty() || m_index >= (s64)m_games.size()) {
        return;
    }

    const auto& game = m_games[m_index];
    App::Push<OptionBox>(
        "Delete all cheats for "_i18n + game.name + "?",
        "Cancel"_i18n, "Delete"_i18n, 1,
        [this, game](auto op_index) {
            if (!op_index || *op_index != 1) {
                return;
            }

            if (DeleteAllCheatsForTitle(game.title_id)) {
                App::Notify("Deleted cheats for "_i18n + game.name);
                m_loaded = false; // Rescan
                ScanGamesWithCheats();
            } else {
                App::Notify("Failed to delete cheats"_i18n);
            }
        }
    );
}

void CheatViewMenu::ScanGamesWithCheats() {
    m_scanning = true;
    m_games.clear();

    // Initialize ns service
    Result rc = nsInitialize();
    if (R_FAILED(rc)) {
        log_write("[Cheats] nsInitialize failed: %x\n", rc);
        m_scanning = false;
        m_loaded = true;
        return;
    }

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

        if (record_count == 0) {
            break;
        }

        // Process each record
        for (s32 i = 0; i < record_count; i++) {
            const auto& record = record_list[i];
            if (record.application_id == 0) continue;
            const auto base_title_id = GetBaseApplicationTitleId(record.application_id);
            if (!seen_title_ids.insert(base_title_id).second) {
                continue;
            }

            // Check if this game has any cheats
            auto existing = GetExistingCheats(base_title_id);
            if (!existing.empty()) {
                // Get game name
                std::string name = GetTitleName(base_title_id);
                if (name.empty()) {
                    char placeholder[64];
                    std::snprintf(placeholder, sizeof(placeholder), "Game %016llX", (unsigned long long)base_title_id);
                    name = placeholder;
                }

                GameCheatInfo info;
                info.title_id = base_title_id;
                info.name = name;
                info.build_id = "";
                info.version = GetTitleVersion(base_title_id);
                info.cheat_count = existing.size();

                m_games.push_back(std::move(info));
            }
        }

        offset += record_count;
    }

    nsExit();

    m_scanning = false;
    m_loaded = true;

    if (!m_games.empty()) {
        SetIndex(0);
    }

    log_write("[Cheats] Total: Found %zu games with cheats\n", m_games.size());
}





// CheatslipsLoginMenu implementation
CheatslipsLoginMenu::CheatslipsLoginMenu()
    : MenuBase{"CheatSlips Login", MenuFlag_None} {
    this->SetActions(
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }})
    );
}

CheatslipsLoginMenu::~CheatslipsLoginMenu() = default;

void CheatslipsLoginMenu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);
}

void CheatslipsLoginMenu::Draw(NVGcontext* vg, Theme* theme) {
    // Don't draw anything - keyboard provides the UI
    (void)vg;
    (void)theme;
}

void CheatslipsLoginMenu::OnFocusGained() {
    m_keyboard_shown = false;
    ShowEmailKeyboard();
}

void CheatslipsLoginMenu::ShowEmailKeyboard() {
    if (m_keyboard_shown) return;
    m_keyboard_shown = true;

    std::string email;
    Result rc = swkbd::ShowText(email, "CheatSlips Email", "", 0, 32);
    if (R_FAILED(rc) || email.empty()) {
        SetPop();
        return;
    }

    m_email = email;
    m_state = LoginState::Password;
    m_keyboard_shown = false;
    ShowPasswordKeyboard();
}

void CheatslipsLoginMenu::ShowPasswordKeyboard() {
    if (m_keyboard_shown) return;
    m_keyboard_shown = true;

    std::string password;
    Result rc = swkbd::ShowText(password, "CheatSlips Password", "", 0, 32);
    if (R_FAILED(rc) || password.empty()) {
        SetPop();
        return;
    }

    m_password = password;
    Authenticate();
}

void CheatslipsLoginMenu::Authenticate() {
    auto token = AuthenticateCheatslips(m_email, m_password);
    if (token.empty()) {
        App::Notify("Login failed. Check your credentials."_i18n);
    } else {
        SaveCheatslipsToken(token);
        App::Notify("Logged in successfully!"_i18n);
    }
    SetPop();
}

} // namespace sphaira::ui::menu::hats
