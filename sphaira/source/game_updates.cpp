#include "game_updates.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "download.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "ui/progress_box.hpp"
#include "ui/menus/tools_menu.hpp"
#include "ui/menus/settings/settings_internal.hpp"
#include "ui/menus/cheats_menu.hpp"
#include "ui/menus/cheats/cheat_game_select_menu.hpp"

#include <yyjson.h>
#include <cstdio>
#include <memory>

namespace sphaira::game_updates {
namespace {

// the list Kefir Updater checked against: every game of switch-cheats-db with its newest update.
auto ListUrl() -> std::string {
    return std::string{ui::menu::hats::VERSIONS_DB_URL} + ".json";
}

auto ParseLatest(const std::vector<u8>& data, std::unordered_map<std::uint64_t, std::uint32_t>& out) -> Result {
    yyjson_doc* doc = yyjson_read(reinterpret_cast<const char*>(data.data()), data.size(), 0);
    R_UNLESS(doc, Result_GameUpdatesListParse);
    ON_SCOPE_EXIT(yyjson_doc_free(doc));
    yyjson_val* root = yyjson_doc_get_root(doc);
    R_UNLESS(yyjson_is_obj(root), Result_GameUpdatesListParse);

    yyjson_obj_iter iter;
    yyjson_obj_iter_init(root, &iter);
    while (yyjson_val* key = yyjson_obj_iter_next(&iter)) {
        const char* id_str = yyjson_get_str(key);
        yyjson_val* latest = yyjson_obj_get(yyjson_obj_iter_get_val(key), "latest");
        unsigned long long id{};
        if (!id_str || !yyjson_is_uint(latest) || std::sscanf(id_str, "%016llx", &id) != 1) {
            continue;
        }
        out[id] = static_cast<std::uint32_t>(yyjson_get_uint(latest));
    }
    R_UNLESS(!out.empty(), Result_GameUpdatesListParse);
    R_SUCCEED();
}

void ShowReport(const Report& report) {
    if (report.outdated.empty()) {
        App::Notify("All checked games are up to date"_i18n);
        return;
    }
    std::vector<ui::menu::settings::SettingsItem> rows;
    rows.push_back(ui::menu::settings::MakeHeader("Updates available: "_i18n + std::to_string(report.outdated.size())));
    for (const auto& game : report.outdated) {
        rows.push_back({game.name, UpdateLabel(game.installed) + " → " + UpdateLabel(game.latest)});
    }
    if (report.unknown) {
        rows.push_back(ui::menu::settings::MakeHeader("Not in the list, not checked: "_i18n + std::to_string(report.unknown)));
    }
    App::Push<ui::menu::tools::SystemToolsMenu>("Game updates"_i18n, std::move(rows));
}

} // namespace

void Open() {
    auto report = std::make_shared<Report>();
    App::Push<ui::ProgressBox>(0, "Check game updates"_i18n, "", [report](auto pbox) -> Result {
        pbox->NewTransfer("Downloading the list of updates"_i18n);
        const auto result = curl::Api().ToMemory(
            curl::Url{ListUrl()},
            curl::OnProgress{pbox->OnDownloadProgressCallback()}
        );
        R_TRY(pbox->ShouldExitResult());
        if (!result.success || result.data.empty()) {
            log_write("[updates] list download failed, http %ld\n", result.code);
            R_THROW(Result_GameUpdatesListDownload);
        }

        std::unordered_map<std::uint64_t, std::uint32_t> latest;
        R_TRY(ParseLatest(result.data, latest));

        pbox->NewTransfer("Reading installed games"_i18n);
        std::vector<Installed> installed;
        for (auto& game : ui::menu::hats::detail::EnumerateInstalledGames()) {
            installed.push_back({game.title_id, std::move(game.name), game.version});
        }
        *report = FindOutdated(installed, latest);
        log_write("[updates] list %zu titles, installed %zu, outdated %zu, not in the list %zu\n",
            latest.size(), installed.size(), report->outdated.size(), report->unknown);
        R_SUCCEED();
    }, [report](Result rc) {
        if (R_SUCCEEDED(rc)) {
            ShowReport(*report);
        } else if (rc != Result_TransferCancelled) {
            App::PushErrorBox(rc, "Could not check game updates"_i18n);
        }
    });
}

} // namespace sphaira::game_updates
