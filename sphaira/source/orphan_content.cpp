#include "orphan_content.hpp"

#include "app.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "title_info.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/error_box.hpp"
#include "ui/menus/game/game_internal.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace sphaira::orphan_content {
namespace {

// MTP and FTP install one file at a time with no "batch done" signal, and a base
// game may arrive after its update in the same batch, so the check waits for quiet.
constexpr u64 QUIET_NS = 5'000'000'000ULL;

Mutex g_mutex{};
std::set<u64> g_pending{};
// tick of the last NoteInstalled; 0 = nothing pending.
std::atomic<u64> g_last_tick{};

bool HasBase(u64 app_id) {
    title::MetaEntries entries;
    if (R_FAILED(title::GetMetaEntries(app_id, entries))) {
        return true; // no record or ns failed: nothing to offer
    }
    return std::ranges::any_of(entries, [](const auto& s) {
        return s.meta_type == NcmContentMetaType_Application;
    });
}

auto TitleName(u64 app_id) -> std::string {
    if (const auto* data = title::Get(app_id); data && data->lang.name[0]) {
        return data->lang.name;
    }
    char id[17];
    std::snprintf(id, sizeof(id), "%016lX", app_id);
    return id;
}

void Ask(std::vector<u64> ids) {
    std::string message = "Installed without the base game:"_i18n + "\n";
    for (const auto id : ids) {
        message += TitleName(id) + "\n";
    }
    message += "\n" + "Updates and DLC do not work without their base game.\nKeep them if the game is on a game card."_i18n;

    App::Push<ui::OptionBox>(message, "Keep"_i18n, "Delete"_i18n, 0, [ids](auto op_index) {
        if (!op_index || *op_index != 1) {
            return;
        }
        App::Push<ui::ProgressBox>(0, "Deleting"_i18n, "", [ids](auto) -> Result {
            R_TRY(title::Init());
            ON_SCOPE_EXIT(title::Exit());
            for (const auto id : ids) {
                // installed content and record only; saves stay.
                R_TRY(ui::menu::game::DeleteApplicationKeepSave(id));
            }
            R_SUCCEED();
        }, [](Result rc) {
            if (R_FAILED(rc)) {
                App::PushErrorBox(rc, "Delete failed!"_i18n);
            }
        });
    });
}

} // namespace

void NoteInstalled(u64 app_id) {
    mutexLock(&g_mutex);
    ON_SCOPE_EXIT(mutexUnlock(&g_mutex));
    g_pending.insert(app_id);
    g_last_tick = armGetSystemTick();
}

void Poll() {
    const auto last = g_last_tick.load();
    if (!last || App::GetProgressActive() || armTicksToNs(armGetSystemTick() - last) < QUIET_NS) {
        return;
    }

    std::vector<u64> ids;
    {
        mutexLock(&g_mutex);
        ON_SCOPE_EXIT(mutexUnlock(&g_mutex));
        ids.assign(g_pending.begin(), g_pending.end());
        g_pending.clear();
        g_last_tick = 0;
    }

    // ns and the title cache, for the base check and the names.
    if (R_FAILED(title::Init())) {
        return;
    }
    ON_SCOPE_EXIT(title::Exit());

    std::erase_if(ids, HasBase);
    if (ids.empty()) {
        return;
    }

    log_write("[orphan] %zu installed title(s) without a base game\n", ids.size());
    Ask(std::move(ids));
}

} // namespace sphaira::orphan_content
