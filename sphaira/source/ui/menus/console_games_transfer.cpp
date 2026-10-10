#include "ui/menus/console_games_transfer.hpp"
#include "ui/menus/install_share.hpp"
#include "ui/menus/dbi_menu.hpp"
#include "ui/menus/game/game_internal.hpp"
#include "ui/menus/grid_menu_base.hpp"
#include "ui/popup_list.hpp"
#include "ui/popup_multi_select.hpp"
#include "ui/progress_box.hpp"
#include "ui/sidebar.hpp"
#include "ui/option_box.hpp"

#include "app.hpp"
#include "defines.hpp"
#include "download.hpp"
#include "dumper.hpp"
#include "title_nsp.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "net.hpp"
#include "title_info.hpp"
#include "web.hpp"
#include "web_games.hpp"
#include "yati/yati.hpp"
#include "yati/source/http.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include <yyjson.h>

namespace sphaira::ui::menu::games_transfer {
namespace {

using grid::FormatBytes;

struct LocalGame {
    u64 app_id{};
    std::string name{};
};

struct RemoteFile {
    u32 n{};
    std::string name{};
    s64 size{};
};

struct RemoteGame {
    std::string id{};
    std::string name{};
    std::vector<RemoteFile> files{};

    auto Size() const -> s64 {
        s64 total{};
        for (const auto& f : files) {
            total += f.size;
        }
        return total;
    }
};

auto CountTicked(const std::vector<u8>& ticks) -> size_t {
    return static_cast<size_t>(std::count(ticks.begin(), ticks.end(), 1));
}

auto PickedLabel(size_t ticked, size_t total) -> std::string {
    return std::to_string(ticked) + " / " + std::to_string(total);
}

void StartSharing(std::vector<u64> ids) {
    net::RequireConnection([ids = std::move(ids)]() {
        WebGamesSetShared(ids);

        WebShareResult result;
        const auto rc = WebStartServer("", result);
        if (R_FAILED(rc)) {
            WebGamesClear();
            App::PushErrorBox(rc, "Failed to start folder server"_i18n);
            return;
        }

        if (WebGetProgressBox()) {
            nvgDeleteImage(App::GetVg(), result.qr_image);
            App::Notify("Sharing "_i18n + std::to_string(ids.size()) + " " + "games"_i18n);
            return;
        }

        WebPushServerProgressBox(result.ip_url, result.qr_image, "Send installed games"_i18n);
    });
}

// cable: this console is the USB host (usb:hs) and speaks the tinfoil protocol
// to the other console's PC Install (USB), exactly like Dump → USB transfer
// (Switch 2 Switch); one NSP per installed component of every chosen game.
void StartSendingByCable(std::vector<u64> ids) {
    if (R_FAILED(title::Init())) {
        App::Notify("Could not read the installed games"_i18n);
        return;
    }

    std::vector<title::NspEntry> entries;
    for (const auto id : ids) {
        std::string name;
        if (const auto data = title::Get(id); data && data->status == title::NacpLoadStatus::Loaded && data->lang.name[0]) {
            name = data->lang.name;
        }
        if (const auto rc = title::BuildNspEntries(id, name.c_str(), title::ContentFlag_All, false, entries); R_FAILED(rc)) {
            title::Exit();
            App::PushErrorBox(rc, "Failed to prepare NSP dump"_i18n);
            return;
        }
    }
    if (entries.empty()) {
        title::Exit();
        App::Notify("No matching installed content to dump"_i18n);
        return;
    }

    std::vector<fs::FsPath> paths;
    for (const auto& e : entries) {
        paths.emplace_back(e.path);
    }

    dump::DumpLocation location{};
    location.entry = {dump::DumpLocationType_UsbS2S, 0};
    location.usb_stream = false;
    auto source = std::make_shared<game::NspSource>(entries);
    dump::Dump(source, location, paths, [](Result){
        title::Exit();
    });
}

// every application with installed content, by name.
auto ScanInstalled(ProgressBox* pbox, std::vector<LocalGame>& out) -> Result {
    R_TRY(title::Init());
    ON_SCOPE_EXIT(title::Exit());

    title::ForEachApplicationRecord([&](std::span<const NsApplicationRecord> records) {
        for (const auto& rec : records) {
            if (pbox->ShouldExit()) {
                return;
            }
            title::MetaEntries installed;
            if (R_FAILED(title::GetMetaEntries(rec.application_id, installed)) || installed.empty()) {
                continue;
            }
            LocalGame game{rec.application_id};
            if (const auto data = title::Get(rec.application_id); data && data->status == title::NacpLoadStatus::Loaded && data->lang.name[0]) {
                game.name = data->lang.name;
            } else {
                char buf[17];
                std::snprintf(buf, sizeof(buf), "%016lX", rec.application_id);
                game.name = buf;
            }
            out.push_back(std::move(game));
        }
    });

    std::sort(out.begin(), out.end(), [](const LocalGame& a, const LocalGame& b){ return strcasecmp(a.name.c_str(), b.name.c_str()) < 0; });
    return pbox->ShouldExit() ? pbox->ShouldExitResult() : Result{0};
}

void ShowSendPicker(std::shared_ptr<std::vector<LocalGame>> games) {
    auto ticks = std::make_shared<std::vector<u8>>(games->size(), 0);

    auto options = std::make_unique<Sidebar>("Send installed games"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    const auto pick = options->Add<SidebarEntryTextBase>("Choose games"_i18n, PickedLabel(0, games->size()), SidebarEntryTextBase::Callback{},
        "Tick the games to send. X ticks all."_i18n);
    pick->SetCallback([games, ticks, pick](){
        PopupMultiSelect::Items items;
        for (const auto& g : *games) {
            items.emplace_back(PopupMultiSelect::Item{g.name, {}, false});
        }
        App::Push<PopupMultiSelect>("Choose games"_i18n, items, *ticks, [games, ticks, pick](){
            pick->SetValue(PickedLabel(CountTicked(*ticks), games->size()));
        });
    });

    const auto chosen = [games, ticks]() {
        std::vector<u64> ids;
        for (size_t i = 0; i < games->size(); i++) {
            if ((*ticks)[i]) {
                ids.push_back((*games)[i].app_id);
            }
        }
        if (ids.empty()) {
            App::Notify("No games chosen"_i18n);
        }
        return ids;
    };

    options->Add<SidebarEntryCallback>("Send over Wi-Fi"_i18n, [chosen](){
        if (auto ids = chosen(); !ids.empty()) {
            StartSharing(std::move(ids));
        }
    }, true, "The other console opens Console Transfer → Receive games → Wi-Fi and enters this console's address."_i18n);

    options->Add<SidebarEntryCallback>("Send by USB cable"_i18n, [chosen](){
        if (auto ids = chosen(); !ids.empty()) {
            StartSendingByCable(std::move(ids));
        }
    }, true, "Connect the two consoles with a USB-C cable. The other console opens Console Transfer → Receive games → USB cable."_i18n);
}

auto FetchRemoteGames(ProgressBox* pbox, const std::string& base_url, std::vector<RemoteGame>& out) -> Result {
    curl::Api api;
    api.SetOption(curl::Url{base_url + "/games"});
    api.SetOption(curl::OnProgress{[pbox](s64, s64, s64, s64) { return !pbox->ShouldExit(); }});
    const auto res = curl::ToMemory(api);
    if (pbox->ShouldExit()) {
        return Result_TransferCancelled;
    }
    R_UNLESS(res.success && res.code == 200 && !res.data.empty(), Result_FsInvalidType);

    yyjson_doc* doc = yyjson_read(reinterpret_cast<const char*>(res.data.data()), res.data.size(), 0);
    R_UNLESS(doc, Result_FsInvalidType);
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    yyjson_val* games = yyjson_obj_get(yyjson_doc_get_root(doc), "games");
    R_UNLESS(yyjson_is_arr(games), Result_FsInvalidType);

    size_t gi, gmax;
    yyjson_val* game;
    yyjson_arr_foreach(games, gi, gmax, game) {
        RemoteGame rg{};
        const char* id = yyjson_get_str(yyjson_obj_get(game, "id"));
        const char* name = yyjson_get_str(yyjson_obj_get(game, "name"));
        if (!id || !*id) {
            continue;
        }
        rg.id = id;
        rg.name = name && *name ? name : id;

        yyjson_val* files = yyjson_obj_get(game, "files");
        size_t fi, fmax;
        yyjson_val* file;
        if (yyjson_is_arr(files)) {
            yyjson_arr_foreach(files, fi, fmax, file) {
                const char* fname = yyjson_get_str(yyjson_obj_get(file, "name"));
                rg.files.push_back({
                    static_cast<u32>(yyjson_get_uint(yyjson_obj_get(file, "n"))),
                    fname ? fname : "game.nsp",
                    static_cast<s64>(yyjson_get_sint(yyjson_obj_get(file, "size"))),
                });
            }
        }
        if (!rg.files.empty()) {
            out.push_back(std::move(rg));
        }
    }
    R_SUCCEED();
}

void InstallRemote(const std::string& base_url, std::shared_ptr<std::vector<RemoteGame>> games, std::shared_ptr<std::vector<u8>> ticks) {
    std::vector<RemoteGame> chosen;
    for (size_t i = 0; i < games->size(); i++) {
        if ((*ticks)[i]) {
            chosen.push_back((*games)[i]);
        }
    }
    if (chosen.empty()) {
        App::Notify("No games chosen"_i18n);
        return;
    }

    auto installed = std::make_shared<size_t>(0);
    App::PopToMenu();
    App::Push<ProgressBox>(0, "Installing "_i18n, chosen.front().name, [base_url, chosen, installed](ProgressBox* pbox) -> Result {
        for (const auto& game : chosen) {
            pbox->SetTitle(game.name);
            for (const auto& file : game.files) {
                const auto url = base_url + "/games/file?id=" + game.id + "&n=" + std::to_string(file.n);
                yati::source::Http source{url, "", ""};
                // yati picks the container by the path's extension.
                R_TRY(yati::InstallFromSource(pbox, &source, file.name));
            }
            (*installed)++;
        }
        R_SUCCEED();
    }, [installed](Result rc) {
        App::PushErrorBox(rc, "Install failed!"_i18n);
        if (R_SUCCEEDED(rc)) {
            App::Notify("Installed "_i18n + std::to_string(*installed) + " " + "games"_i18n);
        }
    });
}

void ShowReceivePicker(const std::string& base_url, std::shared_ptr<std::vector<RemoteGame>> games) {
    auto ticks = std::make_shared<std::vector<u8>>(games->size(), 1);

    auto options = std::make_unique<Sidebar>("Receive games"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    const auto pick = options->Add<SidebarEntryTextBase>("Choose games"_i18n, PickedLabel(games->size(), games->size()), SidebarEntryTextBase::Callback{},
        "All offered games are ticked. Untick what you do not want."_i18n);
    pick->SetCallback([games, ticks, pick](){
        PopupMultiSelect::Items items;
        for (const auto& g : *games) {
            items.emplace_back(PopupMultiSelect::Item{g.name, FormatBytes(static_cast<u64>(g.Size())) + " · " + std::to_string(g.files.size()) + " " + "files"_i18n, false});
        }
        App::Push<PopupMultiSelect>("Choose games"_i18n, items, *ticks, [games, ticks, pick](){
            pick->SetValue(PickedLabel(CountTicked(*ticks), games->size()));
        });
    });

    options->Add<SidebarEntryCallback>("Install"_i18n, [base_url, games, ticks](){
        InstallRemote(base_url, games, ticks);
    }, true, "Base game, updates and DLC are installed one after another."_i18n);
}

} // namespace

void Send(std::vector<u64> preselected) {
    if (!preselected.empty()) {
        StartSharing(std::move(preselected));
        return;
    }

    auto games = std::make_shared<std::vector<LocalGame>>();
    App::Push<ProgressBox>(0, "Reading"_i18n, "Installed games"_i18n, [games](ProgressBox* pbox) -> Result {
        return ScanInstalled(pbox, *games);
    }, [games](Result rc){
        if (R_FAILED(rc)) {
            if (rc != Result_TransferCancelled) {
                App::PushErrorBox(rc, "Could not read the installed games"_i18n);
            }
            return;
        }
        if (games->empty()) {
            App::Push<OptionBox>("No installed games to send."_i18n, "OK"_i18n);
            return;
        }
        ShowSendPicker(games);
    }, 1, PRIO_PREEMPTIVE, 1024 * 128, false);
}

void ReceiveOverWifi();

void Receive() {
    if (!App::GetInstallEnable()) {
        App::ShowEnableInstallPrompt();
        return;
    }

    PopupList::Items items{"Wi-Fi (same network)"_i18n, "USB cable"_i18n};
    App::Push<PopupList>("How is the other console connected?"_i18n, items, [](auto op_index){
        if (!op_index) {
            return;
        }
        if (*op_index == 0) {
            ReceiveOverWifi();
            return;
        }
#if ENABLE_NETWORK_INSTALL
        // the other console sends the list the moment it sees this one; the
        // usual USB install screen takes it from there.
        App::Push<ui::menu::dbi::Menu>(ui::menu::MenuFlag_None);
#else
        App::Notify("USB install is not part of this build"_i18n);
#endif
    });
}

void ReceiveOverWifi() {
    ConnectConsoleTransfer([](const std::string& base_url) {
        auto games = std::make_shared<std::vector<RemoteGame>>();
        App::Push<ProgressBox>(0, "Fetching game list..."_i18n, base_url, [base_url, games](ProgressBox* pbox) -> Result {
            return FetchRemoteGames(pbox, base_url, *games);
        }, [base_url, games](Result rc){
            if (R_FAILED(rc)) {
                if (rc != Result_TransferCancelled) {
                    App::Push<OptionBox>("The other console offers no games.\n\nOn it, open Console Transfer → Send installed games first."_i18n, "OK"_i18n);
                }
                return;
            }
            if (games->empty()) {
                App::Push<OptionBox>("The other console offers no games.\n\nOn it, open Console Transfer → Send installed games first."_i18n, "OK"_i18n);
                return;
            }
            ShowReceivePicker(base_url, games);
        }, 1, PRIO_PREEMPTIVE, 1024 * 128, false);
    });
}

} // namespace sphaira::ui::menu::games_transfer
