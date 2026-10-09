#include "ui/menus/cheats/cheat_download_menu.hpp"
#include "ui/menus/cheats/cheat_files_menu.hpp"
#include "ui/menus/cheats/cheats_dmnt.hpp"
#include "ui/menus/cheats/cheats_lookup.hpp"
#include "ui/menus/cheats/cheats_ops.hpp"
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

namespace {

auto GetBuildIdFailureMessage(BuildIdFailureReason reason) -> std::string {
    switch (reason) {
        case BuildIdFailureReason::ProdKeysMissing:
            return "prod.keys not found";
        case BuildIdFailureReason::GameNotFound:
            return "Game Not Found";
        case BuildIdFailureReason::ExactBuildIdUnavailable:
            return "Unable to determine the exact Build ID.\n\n"
                   "For reliable cheat matching, launch the game first\n"
                   "or retry from a mode where installed-title code can be read.";
        case BuildIdFailureReason::None:
        default:
            return {};
    }
}

bool WritePayloadLaunchConfig(const fs::FsPath& payload_path) {
    fs::FsNativeSd fs;
    fs.CreateDirectoryRecursively("/config/hats-tools");

    std::string payload_path_fatfs = static_cast<const char*>(payload_path);
    if (payload_path_fatfs.starts_with('/')) {
        payload_path_fatfs = "sd:" + payload_path_fatfs;
    }

    FILE* f = std::fopen(PAYLOAD_LAUNCH_CONFIG_PATH, "wb");
    if (!f) {
        log_write("[Cheats] failed to open payload launch config for writing\n");
        return false;
    }

    const int written = std::fprintf(
        f,
        "[payload]\n"
        "launch_path=%s\n",
        payload_path_fatfs.c_str()
    );
    std::fclose(f);
    fsdevCommitDevice("sdmc");

    return written > 0;
}

} // namespace

void ShowProdKeysMissingDialog() {
    fs::FsPath lockpick_payload;
    if (!utils::findLockpickPayload(lockpick_payload)) {
        App::Push<OptionBox>(
            "prod.keys not found.\nPlace Lockpick_RCM in /bootloader/payloads"_i18n,
            "OK"_i18n
        );
        return;
    }

    App::Push<OptionBox>(
        "prod.keys not found.\nLaunch Lockpick_RCM payload now?"_i18n,
        "Cancel"_i18n,
        "Launch"_i18n,
        1,
        [lockpick_payload](auto op_index) {
            if (!op_index || *op_index != 1) {
                return;
            }

            log_write("[Cheats] launching Lockpick through hekate autoboot payload: %s\n",
                      static_cast<const char*>(lockpick_payload));

            if (!WritePayloadLaunchConfig(lockpick_payload)) {
                App::Push<ErrorBox>("Failed to configure payload launch");
                return;
            }

            if (!utils::setHekateAutobootPayload(static_cast<const char*>(lockpick_payload))) {
                App::Push<ErrorBox>("Failed to configure hekate");
                return;
            }

            const Result rc = utils::requestForcedReboot();
            if (R_FAILED(rc)) {
                App::Push<ErrorBox>(rc, "Failed to reboot");
            }
        }
    );
}

// ============================================================
// CheatDownloadMenu - Select and download cheats
// ============================================================

CheatDownloadMenu::CheatDownloadMenu(CheatSource source, const GameCheatInfo& game)
    : MenuBase{"Select Cheats", MenuFlag_None}, m_source(source), m_game(game) {

    log_write("[Cheats] DEBUG: CheatDownloadMenu constructor called\n");
    log_write("[Cheats] DEBUG: Source: %d, Game: %s, TitleID: %016lX, BuildID: %s\n",
        static_cast<int>(source), game.name.c_str(), game.title_id, game.build_id.c_str());
    log_write("[Cheats] DEBUG: m_should_close initial value: %d\n", m_should_close);

    // Set different actions based on cheat source
    if (m_source == CheatSource::Cheatslips) {
        // CheatSlips: No individual selection (content is bundled), select all and download
        this->SetActions(
            std::make_pair(Button::A, Action{"Download All"_i18n, [this](){
                if (!m_cheats.empty() && !m_loading) {
                    // Select all cheats and download
                    for (auto& cheat : m_cheats) {
                        cheat.selected = true;
                    }
                    DownloadCheats();
                }
            }}),
            std::make_pair(Button::B, Action{"Back"_i18n, [this](){
                SetPop();
            }}),
            std::make_pair(Button::X, Action{"Preview"_i18n, [this](){
                if (!m_cheats.empty() && !m_loading && m_index < (s64)m_cheats.size()) {
                    PreviewCheat();
                }
            }})
        );
    } else {
        // NxDb: Individual selection + Select All + Download + Manage + Preview
        this->SetActions(
            std::make_pair(Button::A, Action{"Toggle"_i18n, [this](){
                if (!m_cheats.empty() && !m_loading) {
                    if (m_index < (s64)m_cheats.size()) {
                        m_cheats[m_index].selected = !m_cheats[m_index].selected;
                    }
                }
            }}),
            std::make_pair(Button::B, Action{"Back"_i18n, [this](){
                SetPop();
            }}),
            std::make_pair(Button::X, Action{"Select All"_i18n, [this](){
                if (!m_cheats.empty() && !m_loading) {
                    // Select/deselect all cheats
                    bool all_selected = std::all_of(m_cheats.begin(), m_cheats.end(),
                        [](const CheatEntry& c) { return c.selected; });
                    for (auto& cheat : m_cheats) {
                        cheat.selected = !all_selected;
                    }
                }
            }}),
            std::make_pair(Button::Y, Action{"Download"_i18n, [this](){
                if (!m_cheats.empty() && !m_loading) {
                    DownloadCheats();
                }
            }}),
            std::make_pair(Button::R, Action{"Preview"_i18n, [this](){
                if (!m_cheats.empty() && !m_loading && m_index < (s64)m_cheats.size()) {
                    PreviewCheat();
                }
            }})
        );
    }

    const Vec4 v{75, GetY() + 42.f, 1220.f - 150.f, 60.f};
    m_list = std::make_unique<List>(1, 8, m_pos, v);
    m_list->SetLayout(List::Layout::GRID);
}

CheatDownloadMenu::~CheatDownloadMenu() {
    log_write("[Cheats] DEBUG: CheatDownloadMenu destructor called\n");
    log_write("[Cheats] DEBUG: Cheats list size: %zu, m_should_close: %d\n", m_cheats.size(), m_should_close);
}

void CheatDownloadMenu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    // Check if we should close (from callback)
    if (m_should_close) {
        log_write("[Cheats] DEBUG: Update() - m_should_close flag detected, calling SetPop()\n");
        log_write("[Cheats] DEBUG: Cheats list size: %zu, Index: %ld\n", m_cheats.size(), m_index);
        log_write("[Cheats] DEBUG: Error message: %s\n", m_error_message.c_str());
        SetPop();
        return;
    }

    // Reset index if cheats list is empty
    if (m_cheats.empty()) {
        m_index = -1;
    } else {
        // Ensure index is valid
        if (m_index < 0 || m_index >= (s64)m_cheats.size()) {
            m_index = 0;
        }

        m_list->OnUpdate(controller, touch, m_index, m_cheats.size(), [this](bool touch, auto i) {
            if (touch && m_index == i) {
                FireAction(Button::A);
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                SetIndex(i);
            }
        }, this);
    }
}

void CheatDownloadMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    // Draw game info with Build ID
    if (!m_game.build_id.empty()) {
        gfx::drawTextArgs(vg, 80.f, GetY() + 10.f, 16.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "%s | %s", m_game.name.c_str(), m_game.build_id.c_str());
    } else {
        gfx::drawTextArgs(vg, 80.f, GetY() + 10.f, 16.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "%s", m_game.name.c_str());
    }

    if (m_loading) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "Loading cheats...");
        return;
    }

    if (!m_error_message.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 20.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_ERROR),
            "%s", m_error_message.c_str());
        return;
    }

    if (m_cheats.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "%s", "Cheats Not Found"_i18n.c_str());
        return;
    }

    // Save and restore scissor to clip list drawing area
    nvgSave(vg);
    // Clip area starts below the header text; inflated by the selection outline
    // pad so the highlight of edge rows isn't clipped.
    const float p = gfx::SELECTION_OUTLINE_PAD;
    nvgScissor(vg, 75.f - p, GetY() + 40.f - p, 1220.f - 150.f + p * 2, 720.f - GetY() - 40.f + p * 2);
    ON_SCOPE_EXIT(nvgRestore(vg));

    constexpr float text_xoffset{15.f};

    m_list->Draw(vg, theme, m_cheats.size(), m_index, [this](auto* vg, auto* theme, Vec4 v, auto i) {
        const auto& [x, y, w, h] = v;
        const auto& cheat = m_cheats[i];

        auto text_id = ThemeEntryID_TEXT;
        if (m_index == i) {
            text_id = ThemeEntryID_TEXT_SELECTED;
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else {
            if (i != m_cheats.size() - 1) {
                gfx::drawRect(vg, x, y + h, w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
            }
        }

        // Selection indicator - only for NxDb, not for CheatSlips
        if (m_source != CheatSource::Cheatslips) {
            if (cheat.selected) {
                gfx::drawTextArgs(vg, x + text_xoffset, y + h / 2.f, 20.f,
                    NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                    theme->GetColour(ThemeEntryID_HIGHLIGHT_1),
                    "[X]");
            } else {
                gfx::drawTextArgs(vg, x + text_xoffset, y + h / 2.f, 20.f,
                    NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                    theme->GetColour(ThemeEntryID_TEXT_INFO),
                    "[ ]");
            }
        }

        // Cheat name (truncated)
        std::string name = cheat.name;
        if (name.length() > 60) {
            name = name.substr(0, 57) + "...";
        }

        // Adjust text offset based on source type
        float text_offset = (m_source == CheatSource::Cheatslips) ? text_xoffset : text_xoffset + 50.f;

        // Draw cheat name (removed source badges - they're only in View Cheats)
        gfx::drawTextArgs(vg, x + text_offset, y + h / 2.f, 18.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
            theme->GetColour(text_id),
            "%s", name.c_str());
    });
}

void CheatDownloadMenu::OnFocusGained() {
    MenuBase::OnFocusGained();

    if (!m_loaded && !m_loading) {
        FetchCheats();
    }
}

void CheatDownloadMenu::SetIndex(s64 index) {
    m_index = index;
    if (!m_index) {
        m_list->SetYoff(0);
    }
}

void CheatDownloadMenu::FetchCheats() {
    m_loading = true;
    m_error_message.clear();
    m_cheats.clear();
    m_should_close = false;

    // nx-cheats-db and CheatSlips both require an exact Build ID match.
    if (m_source == CheatSource::NxDb) {
        FetchCheatsFromNxDb();
        return;
    }

    // For CheatSlips, only trust exact Build IDs from the live process,
    // the installed Program NCA, or the installed title's main NSO.
    // Do not guess from version maps.
    const auto lookup = LookupBuildIdForCheats(m_game.title_id);
    if (!lookup.build_id.empty()) {
        m_game.build_id = lookup.build_id;
        SaveDetectedBuildIdToCache(m_game, m_game.build_id, lookup.source.c_str());
        log_write("[Cheats] Got Build ID from %s: %s\n", lookup.source.c_str(), m_game.build_id.c_str());
        FetchCheatsFromApi(m_game.build_id);
        return;
    }

    m_loading = false;
    m_loaded = true;
    m_error_message = GetBuildIdFailureMessage(lookup.failure_reason);
    log_write("[Cheats] Exact Build ID detection failed for CheatSlips, reason=%d\n",
              static_cast<int>(lookup.failure_reason));

    if (lookup.failure_reason == BuildIdFailureReason::ProdKeysMissing) {
        ShowProdKeysMissingDialog();
        m_should_close = true;
    } else if (lookup.failure_reason == BuildIdFailureReason::GameNotFound) {
        App::Notify(m_error_message);
        m_should_close = true;
    }
}


void CheatDownloadMenu::PreviewCheat() {
    if (m_index < 0 || m_index >= (s64)m_cheats.size()) {
        return;
    }

    const auto& cheat = m_cheats[m_index];

    // Build preview message
    std::string msg = "Cheat Preview:\n\n";
    msg += "Name: " + cheat.name + "\n";
    msg += "Build ID: " + cheat.build_id + "\n";

    // Add source info
    const char* source_str = "Unknown";
    switch (cheat.source) {
        case CheatSource::Cheatslips:
            source_str = "CheatSlips";
            break;
        case CheatSource::NxDb:
            source_str = "nx-cheats-db";
            break;
        case CheatSource::Gbatemp:
            source_str = "GBATemp";
            break;
        default:
            break;
    }
    msg += "Source: " + std::string(source_str) + "\n\n";

    // Add content
    msg += "Content:\n";
    msg += "в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ\n";

    // Check if content is empty or just whitespace
    std::string content = cheat.content;
    if (content.empty() || content.find_first_not_of(" \t\r\n") == std::string::npos) {
        msg += "[BLANK OR QUOTA EXCEEDED]\n";
        msg += "\nвљ пёЏ WARNING: This cheat has no content!\n";
        msg += "This can happen when CheatSlips quota is exceeded.\n";
    } else {
        // Truncate content if too long for display (max ~500 chars)
        if (content.length() > 500) {
            content = content.substr(0, 497) + "...";
        }
        msg += content;
    }

    msg += "\nв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ";

    // Show preview dialog
    App::Push<OptionBox>(msg, "Close"_i18n, "", 0, [](auto) {});
}

} // namespace sphaira::ui::menu::hats
