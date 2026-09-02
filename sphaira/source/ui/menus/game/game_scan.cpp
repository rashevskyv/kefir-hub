#include "ui/menus/game/game_internal.hpp"
#include "ui/menus/game_menu.hpp"

#include "app.hpp"
#include "log.hpp"
#include "i18n.hpp"
#include "swkbd.hpp"
#include "ui/progress_box.hpp"
#include "yati/nx/ncm.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <minIni.h>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace sphaira::ui::menu::game {

void Menu::AppendGameCardEntries() {
    NcmContentMetaDatabase db{};
    if (R_FAILED(ncmOpenContentMetaDatabase(&db, NcmStorageId_GameCard))) {
        return;
    }
    ON_SCOPE_EXIT(ncmContentMetaDatabaseClose(&db));

    std::vector<NcmContentMetaKey> keys(16);
    s32 total{};
    s32 written{};
    if (R_FAILED(ncmContentMetaDatabaseList(&db, &total, &written, keys.data(), keys.size(),
            NcmContentMetaType_Unknown, 0, 0, UINT64_MAX, NcmContentInstallType_Full))) {
        return;
    }
    if (total > written && total > static_cast<s32>(keys.size())) {
        keys.resize(total);
        if (R_FAILED(ncmContentMetaDatabaseList(&db, &total, &written, keys.data(), keys.size(),
                NcmContentMetaType_Unknown, 0, 0, UINT64_MAX, NcmContentInstallType_Full))) {
            return;
        }
    }
    keys.resize(written);

    for (const auto& key : keys) {
        if (key.type != NcmContentMetaType_Application && key.type != NcmContentMetaType_Patch
            && key.type != NcmContentMetaType_AddOnContent) {
            continue;
        }

        const u64 app_id = ncm::GetAppId(key);
        if (!app_id) {
            continue;
        }

        auto it = std::ranges::find_if(m_entries, [app_id](const Entry& e) {
            return e.app_id == app_id;
        });
        if (it != m_entries.end()) {
            it->on_gamecard = true;
            continue;
        }

        m_entries.emplace_back(app_id, 0, 0);
        m_entries.back().on_gamecard = true;
    }
}

void Menu::ScanHomebrew() {
    constexpr auto ENTRY_CHUNK_COUNT = 1000;
    const auto show_unavailable = m_show_unavailable.Get();
    const auto hide_forwarders = m_hide_forwarders.Get();
    TimeStamp ts;

    App::SetBoostMode(true);
    ON_SCOPE_EXIT(App::SetBoostMode(false));

    FreeEntries();
    m_entries.reserve(ENTRY_CHUNK_COUNT);

    std::vector<NsApplicationRecord> record_list(ENTRY_CHUNK_COUNT);
    s32 offset{};
    while (true) {
        s32 record_count{};
        if (R_FAILED(nsListApplicationRecord(record_list.data(), record_list.size(), offset, &record_count))) {
            log_write("failed to list application records at offset: %d\n", offset);
        }

        // finished parsing all entries.
        if (!record_count) {
            break;
        }

        for (s32 i = 0; i < record_count; i++) {
            const auto& e = record_list[i];

            if (hide_forwarders && (e.application_id & 0x0500000000000000) == 0x0500000000000000) {
                continue;
            }

            if (!show_unavailable) {
                title::MetaEntries installed_content;
                if (R_FAILED(title::GetMetaEntries(e.application_id, installed_content)) || installed_content.empty()) {
                    continue;
                }
            }

            m_entries.emplace_back(e.application_id, e.last_event, e.last_updated);
        }

        offset += record_count;
    }

    AppendGameCardEntries();
    LoadPlayStats();

    // filtering here rather than keeping a second, filtered copy of the list:
    // selection, sizes and deletes all index m_entries directly.
    if (!m_search_query.empty()) {
        auto query = m_search_query;
        std::ranges::transform(query, query.begin(), ::tolower);

        std::erase_if(m_entries, [this, &query](Entry& e) {
            LoadControlEntry(e);
            std::string name = e.GetName();
            std::ranges::transform(name, name.begin(), ::tolower);
            return name.find(query) == std::string::npos;
        });
    }

    m_dirty = false;
    log_write("games found: %zu time_taken: %.2f seconds %zu ms %zu ns\n", m_entries.size(), ts.GetSecondsD(), ts.GetMs(), ts.GetNs());
    this->Sort();
    SetIndex(0);
    ClearSelection();
}

// last-played comes straight from pdm and is cheap enough to read on every
// scan. total playtime is one service call per title per user, so it is only
// read from the cache here and refreshed on demand by LoadPlaytime().
void Menu::LoadPlayStats() {
    if (m_entries.empty()) {
        return;
    }

    for (auto& e : m_entries) {
        char section[17];
        std::snprintf(section, sizeof(section), "%016lX", e.app_id);
        const auto cached = ini_getl(section, "playtime_mins", -1, App::PLAYLOG_PATH);
        e.playtime = cached < 0 ? 0 : (u64)cached * 60000000000ULL;
    }

    if (!m_pdm_initialized) {
        return;
    }

    std::vector<u64> ids;
    ids.reserve(m_entries.size());
    for (const auto& e : m_entries) {
        ids.push_back(e.app_id);
    }

    std::vector<PdmLastPlayTime> play_times(ids.size());
    s32 count{};
    if (R_FAILED(pdmqryQueryLastPlayTime(true, play_times.data(), ids.data(), ids.size(), &count))) {
        return;
    }

    for (s32 i = 0; i < count; i++) {
        const auto& play_time = play_times[i];
        if (!play_time.flag) {
            continue;
        }

        const auto it = std::ranges::find_if(m_entries, [&play_time](const auto& e){
            return e.app_id == play_time.application_id;
        });
        if (it != m_entries.end()) {
            it->last_played = pdmPlayTimestampToPosix(play_time.timestamp_user);
        }
    }
}

void Menu::LoadPlaytime() {
    if (!m_pdm_initialized) {
        App::Notify("Play statistics are unavailable"_i18n);
        return;
    }

    if (m_entries.empty()) {
        return;
    }

    const auto accounts = App::GetAccountList();

    // Snapshot app IDs on the UI thread to isolate the worker from m_entries
    std::vector<u64> app_ids;
    app_ids.reserve(m_entries.size());
    for (const auto& e : m_entries) {
        app_ids.push_back(e.app_id);
    }

    struct PlaytimeResult {
        u64 app_id;
        u64 playtime;
    };

    auto results = std::make_shared<std::vector<PlaytimeResult>>();
    results->resize(app_ids.size());

    App::Push<ProgressBox>(0, "Updating play statistics"_i18n, "", [accounts, app_ids, results](auto pbox) -> Result {
        pbox->UpdateTransfer(0, app_ids.size());

        for (size_t i = 0; i < app_ids.size(); i++) {
            R_TRY(pbox->ShouldExitResult());

            const u64 app_id = app_ids[i];
            u64 total{};
            for (const auto& account : accounts) {
                PdmPlayStatistics stats{};
                if (R_SUCCEEDED(pdmqryQueryPlayStatisticsByApplicationIdAndUserAccountId(app_id, account.uid, true, &stats))) {
                    total += stats.playtime;
                }
            }

            // no profiles (or none of them ever launched it): fall back to the
            // console-wide figure so a played title never reads as zero.
            if (!total) {
                PdmPlayStatistics stats{};
                if (R_SUCCEEDED(pdmqryQueryPlayStatisticsByApplicationId(app_id, true, &stats))) {
                    total = stats.playtime;
                }
            }

            (*results)[i] = {app_id, total};

            char section[17];
            std::snprintf(section, sizeof(section), "%016lX", app_id);
            ini_putl(section, "playtime_mins", total / 60000000000ULL, App::PLAYLOG_PATH);

            pbox->UpdateTransfer(i + 1, app_ids.size());
        }

        R_SUCCEED();
    }, [this, results](Result rc){
        if (R_FAILED(rc)) {
            return;
        }

        // Apply playtime results to UI-owned entries strictly on the UI thread
        for (const auto& res : *results) {
            for (auto& e : m_entries) {
                if (e.app_id == res.app_id) {
                    e.playtime = res.playtime;
                    break;
                }
            }
        }

        m_sort.Set(SortType_PlayTime);
        SortAndFindLastFile(false);
    });
}

void Menu::SetSearch() {
    std::string out;
    if (R_FAILED(swkbd::ShowText(out, "Search games"_i18n.c_str(), m_search_query.c_str(), 0, 128))) {
        return;
    }

    m_search_query = out;
    m_dirty = true;
}

void Menu::Sort() {
    const auto sort = m_sort.Get();
    const auto order = m_order.Get();

    // every sort below falls back to comparing names on a tie, and ties are the
    // common case for the play stats (unplayed titles are all zero), so the
    // control data has to be in before sorting.
    if (sort != SortType_Updated) {
        for (auto& e : m_entries) {
            LoadControlEntry(e);
        }
    }

    const auto name_cmp = [order](const Entry& lhs, const Entry& rhs) -> bool {
        auto r = strcasecmp(lhs.GetName(), rhs.GetName());
        if (!r) {
            r = strcasecmp(lhs.GetAuthor(), rhs.GetAuthor());
        }

        if (order == OrderType_Descending) {
            return r < 0;
        } else {
            return r > 0;
        }
    };

    const auto publisher_cmp = [order](const Entry& lhs, const Entry& rhs) -> bool {
        auto r = strcasecmp(lhs.GetAuthor(), rhs.GetAuthor());
        if (!r) {
            r = strcasecmp(lhs.GetName(), rhs.GetName());
        }

        if (order == OrderType_Descending) {
            return r < 0;
        } else {
            return r > 0;
        }
    };

    const auto sorter = [sort, order, &name_cmp, &publisher_cmp](const Entry& lhs, const Entry& rhs) -> bool {
        if (lhs.on_gamecard != rhs.on_gamecard) {
            return lhs.on_gamecard;
        }
        switch (sort) {
            case SortType_Updated: {
                if (lhs.last_updated == rhs.last_updated) {
                    if (lhs.last_event == rhs.last_event) {
                        return lhs.app_id < rhs.app_id;
                    } else if (order == OrderType_Descending) {
                        return lhs.last_event > rhs.last_event;
                    } else {
                        return lhs.last_event < rhs.last_event;
                    }
                } else if (order == OrderType_Descending) {
                    return lhs.last_updated > rhs.last_updated;
                } else {
                    return lhs.last_updated < rhs.last_updated;
                }
            } break;

            case SortType_Alphabetical: {
                return name_cmp(lhs, rhs);
            } break;

            case SortType_Publisher: {
                return publisher_cmp(lhs, rhs);
            } break;

            case SortType_LastPlayed: {
                if (lhs.last_played == rhs.last_played) {
                    return name_cmp(lhs, rhs);
                }
                return order == OrderType_Descending
                    ? lhs.last_played > rhs.last_played
                    : lhs.last_played < rhs.last_played;
            } break;

            case SortType_PlayTime: {
                if (lhs.playtime == rhs.playtime) {
                    return name_cmp(lhs, rhs);
                }
                return order == OrderType_Descending
                    ? lhs.playtime > rhs.playtime
                    : lhs.playtime < rhs.playtime;
            } break;

            case SortType_Storage: {
                const auto storage_rank = [](const Entry& e) -> int {
                    if (e.sd_size && e.nand_size) return 1;
                    if (e.sd_size) return 2;
                    if (e.nand_size) return 3;
                    return 4;
                };
                const auto rank_lhs = storage_rank(lhs);
                const auto rank_rhs = storage_rank(rhs);
                if (rank_lhs != rank_rhs) {
                    return order == OrderType_Descending ? rank_lhs < rank_rhs : rank_lhs > rank_rhs;
                }
                return name_cmp(lhs, rhs);
            } break;
        }

        std::unreachable();
    };

    std::sort(m_entries.begin(), m_entries.end(), sorter);
}

void Menu::SortAndFindLastFile(bool scan) {
    if (m_entries.empty()) {
        if (scan) {
            ScanHomebrew();
        } else {
            Sort();
            SetIndex(0);
        }
        return;
    }

    const auto app_id = m_entries[m_index].app_id;
    if (scan) {
        ScanHomebrew();
    } else {
        Sort();
    }
    SetIndex(0);

    s64 index = -1;
    for (u64 i = 0; i < m_entries.size(); i++) {
        if (app_id == m_entries[i].app_id) {
            index = i;
            break;
        }
    }

    if (index >= 0) {
        const auto row = m_list->GetRow();
        const auto page = m_list->GetPage();
        // guesstimate where the position is
        if (index >= page) {
            m_list->SetYoff((((index - page) + row) / row) * m_list->GetMaxY());
        } else {
            m_list->SetYoff(0);
        }
        SetIndex(index);
    }
}

void Menu::FreeEntries() {
    auto vg = App::GetVg();

    for (auto&p : m_entries) {
        FreeEntry(vg, p);
    }

    m_entries.clear();
}

} // namespace sphaira::ui::menu::game