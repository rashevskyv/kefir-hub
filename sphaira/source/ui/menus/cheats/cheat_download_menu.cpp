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

} // namespace

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

    m_list->Draw(vg, theme, m_cheats.size(), [this](auto* vg, auto* theme, Vec4 v, auto i) {
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

// Fetch cheats from nx-cheats-db on GitHub
void CheatDownloadMenu::FetchCheatsFromNxDb() {
    log_write("[Cheats] Fetching cheats from nx-cheats-db (GitHub)\n");
    m_should_close = false;

    const auto lookup = LookupBuildIdForCheats(m_game.title_id);
    if (!lookup.build_id.empty()) {
        m_game.build_id = lookup.build_id;
        SaveDetectedBuildIdToCache(m_game, m_game.build_id, lookup.source.c_str());
        log_write("[Cheats] Got Build ID from %s: %s\n", lookup.source.c_str(), m_game.build_id.c_str());
        FetchNxDbCheatsFromGithub(m_game.build_id);
        return;
    }

    log_write("[Cheats] Local exact Build ID lookup failed, falling back to KefirUpdater versions map (reason=%d)\n",
              static_cast<int>(lookup.failure_reason));
    FetchKefirBuildIdFromVersionMap();
}

void CheatDownloadMenu::FetchKefirBuildIdFromVersionMap() {
    const auto title_id_str = FormatTitleId(m_game.title_id);
    const auto version_url = std::string(KEFIR_VERSIONS_DIRECTORY) + title_id_str + ".json";

    log_write("[Cheats] Fetching KefirUpdater versions map: %s\n", version_url.c_str());

    curl::Api().ToMemoryAsync(
        curl::Url{version_url},
        curl::Header{},
        curl::StopToken{this->GetToken()},
        curl::OnComplete{[this](auto& result) {
            if (!result.success || result.code == 404) {
                log_write("[Cheats] KefirUpdater versions map unavailable, falling back to cheats JSON scan (HTTP %ld)\n",
                          result.code);
                FetchKefirCheatsFromGithub("");
                return true;
            }

            const std::string content(result.data.begin(), result.data.end());
            yyjson_doc* doc = yyjson_read(content.data(), content.size(), 0);
            if (!doc) {
                log_write("[Cheats] Failed to parse KefirUpdater versions map\n");
                FetchKefirCheatsFromGithub("");
                return true;
            }

            ON_SCOPE_EXIT(yyjson_doc_free(doc));

            yyjson_val* root = yyjson_doc_get_root(doc);
            const auto version_key = std::to_string(m_game.version);
            yyjson_val* bid_val = yyjson_is_obj(root) ? yyjson_obj_get(root, version_key.c_str()) : nullptr;

            if (bid_val && yyjson_is_str(bid_val)) {
                m_game.build_id = NormalizeBuildId(yyjson_get_str(bid_val));
                SaveDetectedBuildIdToCache(m_game, m_game.build_id, "kefir-versions");
                log_write("[Cheats] KefirUpdater versions map resolved version %u to Build ID %s\n",
                          m_game.version, m_game.build_id.c_str());
                FetchKefirCheatsFromGithub(m_game.build_id);
                return true;
            }

            log_write("[Cheats] KefirUpdater versions map has no Build ID for version %u\n", m_game.version);
            FetchKefirCheatsFromGithub("");
            return true;
        }}
    );
}

// Inspect the nx-cheats-db cheats file directly and extract candidate Build IDs.
void CheatDownloadMenu::FetchCheatsFileAndExtractBuildIds() {
    const auto title_id_str = FormatTitleId(m_game.title_id);
    const auto cheat_url = std::string(NX_DB_GITHUB_BASE) + "/cheats/" + title_id_str + ".json";

    log_write("[Cheats] Fetching cheats file directly to inspect Build IDs: %s\n", cheat_url.c_str());

    curl::Api().ToMemoryAsync(
        curl::Url{cheat_url},
        curl::Header{},
        curl::StopToken{this->GetToken()},
        curl::OnComplete{[this](auto& result) {
            if (!result.success || result.code == 404) {
                m_loading = false;
                m_loaded = true;
                m_error_message.clear();
                log_write("[Cheats] nx-cheats-db cheats file not found, HTTP code: %ld\n", result.code);
                App::Notify("Cheats Not Found"_i18n);
                SetPop();
                return true;
            }

            std::string content(result.data.begin(), result.data.end());
            const auto build_ids = ExtractNxDbBuildIds(content);
            log_write("[Cheats] Direct cheats file inspection found %zu Build ID(s)\n", build_ids.size());

            if (build_ids.empty()) {
                m_loading = false;
                m_loaded = true;
                m_error_message.clear();
                App::Notify("Cheats Not Found"_i18n);
                SetPop();
                return true;
            }

            if (build_ids.size() == 1) {
                m_game.build_id = build_ids[0];
                log_write("[Cheats] Only one Build ID found, using %s\n", m_game.build_id.c_str());
                FetchNxDbCheatsFromGithub(m_game.build_id);
                return true;
            }

            m_loading = false;
            m_loaded = true;
            log_write("[Cheats] Multiple candidate Build IDs found, refusing to guess\n");
            m_error_message.clear();
            App::Notify("Cheats Not Found"_i18n);
            SetPop();
            return true;
        }}
    );
}

void CheatDownloadMenu::FetchKefirCheatsFromGithub(const std::string& build_id) {
    const auto title_id_str = FormatTitleId(m_game.title_id);
    const auto cheat_url = std::string(KEFIR_CHEATS_GBATEMP_DIRECTORY) + title_id_str + ".json";

    log_write("[Cheats] Fetching KefirUpdater individual cheats from: %s\n", cheat_url.c_str());

    curl::Api().ToMemoryAsync(
        curl::Url{cheat_url},
        curl::Header{},
        curl::StopToken{this->GetToken()},
        curl::OnComplete{[this, build_id, title_id_str](auto& result) {
            m_loading = false;
            m_loaded = true;
            m_index = -1;

            if (!result.success || result.code == 404) {
                m_cheats.clear();
                m_error_message.clear();
                log_write("[Cheats] KefirUpdater cheats file not found, HTTP code: %ld\n", result.code);
                App::Notify("Cheats Not Found"_i18n);
                SetPop();
                return true;
            }

            const std::string content(result.data.begin(), result.data.end());
            std::string resolved_build_id = NormalizeBuildId(build_id);

            if (!resolved_build_id.empty()) {
                m_cheats = ParseNxDbCheats(content, resolved_build_id);
                if (m_cheats.empty()) {
                    const auto reversed_build_id = ReverseBuildIdBytes(resolved_build_id);
                    if (reversed_build_id != resolved_build_id) {
                        log_write("[Cheats] KefirUpdater retry with reversed-byte Build ID: %s -> %s\n",
                                  resolved_build_id.c_str(), reversed_build_id.c_str());
                        m_cheats = ParseNxDbCheats(content, reversed_build_id);
                        if (!m_cheats.empty()) {
                            resolved_build_id = reversed_build_id;
                        }
                    }
                }
            }

            const auto build_ids = ExtractNxDbBuildIds(content);
            if (m_cheats.empty() && resolved_build_id.empty() && build_ids.size() == 1) {
                resolved_build_id = build_ids[0];
                m_cheats = ParseNxDbCheats(content, resolved_build_id);
            }

            if (m_cheats.empty()) {
                std::ostringstream out;
                out << "Cheats found, but no matching Build ID.\n\n";
                out << "Title ID: " << title_id_str << "\n";
                out << "Version: " << m_game.version << "\n";
                if (!resolved_build_id.empty()) {
                    out << "Detected Build ID: " << resolved_build_id << "\n";
                }
                if (!build_ids.empty()) {
                    out << "\nAvailable Build ID(s):\n";
                    for (const auto& id : build_ids) {
                        out << id << "\n";
                    }
                }
                m_error_message = out.str();
                log_write("[Cheats] KefirUpdater exact cheats found no matching Build ID\n");
                return true;
            }

            m_game.build_id = resolved_build_id;
            SaveDetectedBuildIdToCache(m_game, m_game.build_id, "kefir-cheats");
            m_index = 0;
            CacheNxDbCheatFile(content);
            log_write("[Cheats] Successfully fetched %zu KefirUpdater exact cheats\n", m_cheats.size());
            return true;
        }}
    );
}

// Fetch cheat JSON file from GitHub
void CheatDownloadMenu::FetchNxDbCheatsFromGithub(const std::string& build_id) {
    const auto title_id_str = FormatTitleId(m_game.title_id);  // UPPERCASE for nx-cheats-db
    const auto cheat_url = std::string(NX_DB_GITHUB_BASE) + "/cheats/" + title_id_str + ".json";

    log_write("[Cheats] Fetching cheats from: %s\n", cheat_url.c_str());

    curl::Api().ToMemoryAsync(
        curl::Url{cheat_url},
        curl::Header{},
        curl::StopToken{this->GetToken()},
        curl::OnComplete{[this, build_id](auto& result) {
            m_loading = false;
            m_loaded = true;
            m_index = -1; // Reset index

            // Check for HTTP 404 (Not Found) - immediately notify user
            if (result.code == 404) {
                m_cheats.clear();
                m_error_message.clear();
                log_write("[Cheats] Cheats not found in nx-cheats-db (HTTP 404)\n");
                App::Notify("Cheats Not Found"_i18n);
                SetPop();
                return true;
            }

            if (!result.success) {
                m_cheats.clear();
                m_error_message = "Failed to fetch cheats from nx-cheats-db.\nTitle may not be supported.\nCheck your internet connection.";
                log_write("[Cheats] Failed to fetch cheat file, HTTP code: %ld\n", result.code);
                App::Notify("Failed to fetch from nx-cheats-db");
                SetPop();
                return true;
            }

            std::string content(result.data.begin(), result.data.end());
            log_write("[Cheats] Cheat file response size: %zu bytes\n", content.size());

            // Parse the cheat JSON
            m_cheats = ParseNxDbCheats(content, build_id);

            if (m_cheats.empty()) {
                const auto reversed_build_id = ReverseBuildIdBytes(build_id);
                if (reversed_build_id != NormalizeBuildId(build_id)) {
                    log_write("[Cheats] Retrying with reversed-byte Build ID: %s -> %s\n",
                              build_id.c_str(), reversed_build_id.c_str());
                    m_cheats = ParseNxDbCheats(content, reversed_build_id);
                    if (!m_cheats.empty()) {
                        m_game.build_id = reversed_build_id;
                        SaveDetectedBuildIdToCache(m_game, m_game.build_id, "byte-swap-fix");
                    }
                }
            }

            if (m_cheats.empty()) {
                log_write("[Cheats] Build ID %s not found in cheats file\n", build_id.c_str());
                m_error_message.clear();
                App::Notify("Cheats Not Found"_i18n);
                SetPop();
            } else {
                m_index = 0; // Set to first item when cheats are found
                log_write("[Cheats] Successfully fetched %zu cheats from nx-cheats-db\n", m_cheats.size());
                // Optionally cache the cheat file locally
                CacheNxDbCheatFile(content);
            }

            return true;
        }}
    );
}

// Cache the fetched cheat file locally for offline use
void CheatDownloadMenu::CacheNxDbCheatFile(const std::string& content) {
    fs::FsNativeSd fs;

    // Create cache directory
    fs.CreateDirectoryRecursively(NX_DB_PATH);

    // Write cheat file (use UPPERCASE for nx-cheats-db)
    fs::FsPath cache_path;
    const auto title_id_str = FormatTitleId(m_game.title_id);  // UPPERCASE for nx-cheats-db
    std::snprintf(cache_path, sizeof(cache_path), "%s/%s.json", NX_DB_PATH, title_id_str.c_str());

    const auto content_data = std::vector<u8>(
        reinterpret_cast<const u8*>(content.data()),
        reinterpret_cast<const u8*>(content.data()) + content.size()
    );
    if (R_SUCCEEDED(fs.write_entire_file(cache_path, content_data))) {
        log_write("[Cheats] Cached cheat file to: %s\n", cache_path.s);
    }
}

void CheatDownloadMenu::FetchCheatsFromApi(const std::string& build_id) {
    // Get token (optional - API works without it but has lower quota)
    auto token = GetCheatslipsToken();

    const auto title_id_str = FormatTitleId(m_game.title_id);
    // CheatSlips API URL: /api/v1/cheats/{title_id} (build_id is for filtering, not in URL)
    const auto url = std::string(CHEATSLIPS_API_URL) + "/" + title_id_str;

    log_write("[Cheats] Fetching cheats from CheatSlips: %s\n", url.c_str());

    // Prepare headers - token is optional
    if (!token.empty()) {
        log_write("[Cheats] Using authenticated request (higher quota)\n");
        curl::Api().ToMemoryAsync(
            curl::Url{url},
            curl::Header{
                std::pair<const std::string, std::string>{"Accept", "application/json"},
                std::pair<const std::string, std::string>{"X-API-TOKEN", token}
            },
            curl::StopToken{this->GetToken()},
            curl::OnComplete{[this, build_id](auto& result) {
                log_write("[Cheats] DEBUG: CheatSlips AUTH callback triggered\n");
                log_write("[Cheats] CheatSlips request completed - success: %d, HTTP code: %ld\n", result.success, result.code);
                log_write("[Cheats] DEBUG: Response data size: %zu bytes\n", result.data.size());

                m_loading = false;
                m_loaded = true;
                m_index = -1; // Reset index when loading completes

                // Check for HTTP 404 (Not Found) - immediately notify user
                if (result.code == 404) {
                    log_write("[Cheats] DEBUG: HTTP 404 detected (AUTH) - cheats not found on CheatSlips\n");
                    m_cheats.clear();
                    m_index = -1;
                    m_error_message.clear();
                    log_write("[Cheats] Cheats not found on CheatSlips (HTTP 404)\n");
                    log_write("[Cheats] DEBUG: Setting m_should_close = true (404 case)\n");
                    App::Notify("Cheats Not Found"_i18n);
                    m_should_close = true;
                    log_write("[Cheats] DEBUG: m_should_close set to: %d\n", m_should_close);
                    return true;
                }

                if (!result.success) {
                    log_write("[Cheats] DEBUG: Request failed (AUTH) - success=false, HTTP code: %ld\n", result.code);
                    m_cheats.clear();
                    m_index = -1;
                    m_error_message = "Failed to fetch cheats from CheatSlips.\nCheck your internet connection.";
                    log_write("[Cheats] Failed to fetch CheatSlips cheats, HTTP code: %ld\n", result.code);
                    log_write("[Cheats] DEBUG: Setting m_should_close = true (failure case)\n");
                    // Auto-exit with notification
                    App::Notify("Failed to fetch from CheatSlips");
                    m_should_close = true;
                    log_write("[Cheats] DEBUG: m_should_close set to: %d\n", m_should_close);
                    return true;
                }

                std::string content(result.data.begin(), result.data.end());
                log_write("[Cheats] CheatSlips response size: %zu bytes\n", content.size());
                log_write("[Cheats] DEBUG: Response content preview (first 200 chars): %s\n",
                    content.substr(0, std::min(size_t(200), content.size())).c_str());

                // Check if response is empty or just "[]"
                if (content.empty() || content == "[]" || content == "null") {
                    log_write("[Cheats] DEBUG: Empty response detected (AUTH) - content: '%s'\n",
                        content.empty() ? "(empty)" : content.c_str());
                    m_cheats.clear();
                    m_index = -1;
                    m_error_message.clear();
                    log_write("[Cheats] Empty response from CheatSlips\n");
                    log_write("[Cheats] DEBUG: Setting m_should_close = true (empty response case)\n");
                    App::Notify("Cheats Not Found"_i18n);
                    m_should_close = true;
                    log_write("[Cheats] DEBUG: m_should_close set to: %d\n", m_should_close);
                    return true;
                }

                log_write("[Cheats] DEBUG: Parsing CheatSlips response (AUTH)...\n");
                m_cheats = ParseCheatslipsCheats(content, build_id);
                log_write("[Cheats] DEBUG: Parsing complete, cheats count: %zu\n", m_cheats.size());

                if (m_cheats.empty()) {
                    log_write("[Cheats] DEBUG: Parsed cheats list is empty (AUTH)\n");
                    // Check if response contains quota error
                    if (content.find("Quota exceeded") != std::string::npos ||
                        content.find("quota") != std::string::npos) {
                        m_error_message = "Daily quota exceeded.\nAdd a token for higher limits.";
                        App::Notify("Daily quota exceeded - Add token for higher limits");
                    } else {
                        m_error_message.clear();
                        App::Notify("Cheats Not Found"_i18n);
                    }
                    log_write("[Cheats] No cheats found, error: %s\n", m_error_message.c_str());
                    log_write("[Cheats] DEBUG: Setting m_should_close = true (no cheats case)\n");
                    // Auto-exit
                    m_should_close = true;
                    log_write("[Cheats] DEBUG: m_should_close set to: %d\n", m_should_close);
                } else {
                    m_index = 0; // Set to first item when cheats are found
                    log_write("[Cheats] Successfully fetched %zu cheats\n", m_cheats.size());
                    log_write("[Cheats] DEBUG: Leaving m_should_close = false (success case)\n");
                }

                return true;
            }}
        );
    } else {
        log_write("[Cheats] Using unauthenticated request (limited quota)\n");
        curl::Api().ToMemoryAsync(
            curl::Url{url},
            curl::Header{
                {"Accept", "application/json"}
            },
            curl::StopToken{this->GetToken()},
            curl::OnComplete{[this, build_id](auto& result) {
                log_write("[Cheats] DEBUG: CheatSlips NO-AUTH callback triggered\n");
                log_write("[Cheats] CheatSlips request completed - success: %d, HTTP code: %ld\n", result.success, result.code);
                log_write("[Cheats] DEBUG: Response data size: %zu bytes\n", result.data.size());

                m_loading = false;
                m_loaded = true;
                m_index = -1; // Reset index when loading completes

                // Check for HTTP 404 (Not Found) - immediately notify user
                if (result.code == 404) {
                    log_write("[Cheats] DEBUG: HTTP 404 detected (NO-AUTH) - cheats not found on CheatSlips\n");
                    m_cheats.clear();
                    m_index = -1;
                    m_error_message.clear();
                    log_write("[Cheats] Cheats not found on CheatSlips (HTTP 404)\n");
                    log_write("[Cheats] DEBUG: Setting m_should_close = true (404 case)\n");
                    App::Notify("Cheats Not Found"_i18n);
                    m_should_close = true;
                    log_write("[Cheats] DEBUG: m_should_close set to: %d\n", m_should_close);
                    return true;
                }

                if (!result.success) {
                    log_write("[Cheats] DEBUG: Request failed (NO-AUTH) - success=false, HTTP code: %ld\n", result.code);
                    m_cheats.clear();
                    m_index = -1;
                    m_error_message = "Failed to fetch cheats from CheatSlips.\nCheck your internet connection.";
                    log_write("[Cheats] Failed to fetch CheatSlips cheats, HTTP code: %ld\n", result.code);
                    log_write("[Cheats] DEBUG: Setting m_should_close = true (failure case)\n");
                    // Auto-exit with notification
                    App::Notify("Failed to fetch from CheatSlips");
                    m_should_close = true;
                    log_write("[Cheats] DEBUG: m_should_close set to: %d\n", m_should_close);
                    return true;
                }

                std::string content(result.data.begin(), result.data.end());
                log_write("[Cheats] CheatSlips response size: %zu bytes\n", content.size());
                log_write("[Cheats] DEBUG: Response content preview (first 200 chars): %s\n",
                    content.substr(0, std::min(size_t(200), content.size())).c_str());

                // Check if response is empty or just "[]"
                if (content.empty() || content == "[]" || content == "null") {
                    log_write("[Cheats] DEBUG: Empty response detected (NO-AUTH) - content: '%s'\n",
                        content.empty() ? "(empty)" : content.c_str());
                    m_cheats.clear();
                    m_index = -1;
                    m_error_message.clear();
                    log_write("[Cheats] Empty response from CheatSlips\n");
                    log_write("[Cheats] DEBUG: Setting m_should_close = true (empty response case)\n");
                    App::Notify("Cheats Not Found"_i18n);
                    m_should_close = true;
                    log_write("[Cheats] DEBUG: m_should_close set to: %d\n", m_should_close);
                    return true;
                }

                log_write("[Cheats] DEBUG: Parsing CheatSlips response (NO-AUTH)...\n");
                m_cheats = ParseCheatslipsCheats(content, build_id);
                log_write("[Cheats] DEBUG: Parsing complete, cheats count: %zu\n", m_cheats.size());

                if (m_cheats.empty()) {
                    log_write("[Cheats] DEBUG: Parsed cheats list is empty (NO-AUTH)\n");
                    // Check if response contains quota error
                    if (content.find("Quota exceeded") != std::string::npos ||
                        content.find("quota") != std::string::npos) {
                        m_error_message = "Daily quota exceeded.\nAdd a token for higher limits.";
                        App::Notify("Daily quota exceeded - Add token for higher limits");
                    } else {
                        m_error_message.clear();
                        App::Notify("Cheats Not Found"_i18n);
                    }
                    log_write("[Cheats] No cheats found, error: %s\n", m_error_message.c_str());
                    log_write("[Cheats] DEBUG: Setting m_should_close = true (no cheats case)\n");
                    // Auto-exit
                    m_should_close = true;
                    log_write("[Cheats] DEBUG: m_should_close set to: %d\n", m_should_close);
                } else {
                    m_index = 0; // Set to first item when cheats are found
                    log_write("[Cheats] Successfully fetched %zu cheats\n", m_cheats.size());
                    log_write("[Cheats] DEBUG: Leaving m_should_close = false (success case)\n");
                }

                return true;
            }}
        );
    }
}

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
                        App::Push<ErrorBox>(rc, "Failed to download cheats");
                    }
                }
            );
        }
    );
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
    msg += "─────────────────────────────\n";

    // Check if content is empty or just whitespace
    std::string content = cheat.content;
    if (content.empty() || content.find_first_not_of(" \t\r\n") == std::string::npos) {
        msg += "[BLANK OR QUOTA EXCEEDED]\n";
        msg += "\n⚠️ WARNING: This cheat has no content!\n";
        msg += "This can happen when CheatSlips quota is exceeded.\n";
    } else {
        // Truncate content if too long for display (max ~500 chars)
        if (content.length() > 500) {
            content = content.substr(0, 497) + "...";
        }
        msg += content;
    }

    msg += "\n─────────────────────────────";

    // Show preview dialog
    App::Push<OptionBox>(msg, "Close"_i18n, "", 0, [](auto) {});
}

namespace detail {

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
auto SanitizeCheatContentForAtmosphere(const std::string& content) -> std::string {
    std::istringstream stream(content);
    std::ostringstream sanitized;
    std::string line;
    std::string pending_header;
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

        if (trimmed.empty()) {
            continue;
        }

        if (trimmed.rfind("//", 0) == 0 || IsParenthesizedNoteLine(trimmed)) {
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

        if (IsHexCodeLine(trimmed)) {
            if (!pending_header.empty() && !wrote_code_for_header) {
                sanitized << pending_header << '\n';
                wrote_code_for_header = true;
            }
            sanitized << NormalizeHexCodeLine(trimmed) << '\n';
        }
    }

    return sanitized.str();
}

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
