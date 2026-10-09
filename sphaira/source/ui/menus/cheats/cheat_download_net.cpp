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

} // namespace sphaira::ui::menu::hats
