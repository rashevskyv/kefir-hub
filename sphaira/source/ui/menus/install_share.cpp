#include "ui/menus/install_share.hpp"

#include "ui/menus/settings_menu.hpp"
#include "ui/menus/dbi_menu.hpp"
#include "ui/sidebar.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "haze_helper.hpp"

#include "app.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "net.hpp"
#include "nro.hpp"
#include "nacp_util.hpp"
#include "web.hpp"

#include <memory>

namespace sphaira::ui::menu {
namespace {

void StartShareServerNow() {
    // the web server has nothing to share without a network the browser can
    // reach it on. bring the connection up (or tell the user it is off) before
    // starting, so a failure never surfaces as a raw nifm/fs result code.
    net::RequireConnection([](){
        WebShareResult result;
        Result rc = WebShareFolder("/", result);

        if (R_FAILED(rc)) {
            App::PushErrorBox(rc, "Failed to start web server"_i18n);
            return;
        }

        if (!result.listener_self_test) {
            App::Notify("Web listener started, but its local self-test failed; check the log or use Title Mode"_i18n);
        }

        WebPushServerProgressBox(result.url, result.qr_image, "Web Sharing Server"_i18n);
    });
}

void InstallTitleModeForwarder() {
    OwoConfig config{};
    config.nro_path = App::GetExePath().toString();

    const auto rc = nro_get_nacp(App::GetExePath(), config.nacp);
    if (R_FAILED(rc)) {
        App::PushErrorBox(rc, "Failed to read the current NRO metadata"_i18n);
        return;
    }

    config.icon = nro_get_icon(App::GetExePath());
    if (config.icon.empty()) {
        App::PushErrorBox(FsError_PathNotFound, "The current NRO does not contain a forwarder icon"_i18n);
        return;
    }
    config.name = nacp_util::GetName(config.nacp);
    config.author = nacp_util::GetAuthor(config.nacp);

    App::Push<OptionBox>(
        "Install a HOME Menu forwarder for Kefir Hub?\n\n"
        "This creates a Title Mode entry for the current NRO. After installation, "
        "return to HOME and launch the new icon manually."_i18n,
        "Back"_i18n, "Install"_i18n, 0, [config = std::move(config)](auto op_index) mutable {
            if (op_index && *op_index) {
                App::Install(config);
            }
        }
    );
}

void StartShareServerFromTools() {
    if (App::IsApplication()) {
        StartShareServerNow();
        return;
    }

    auto options = std::make_unique<Sidebar>("Web Server — Applet Mode"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    options->Add<SidebarEntryCallback>("Start anyway"_i18n, [](){
        StartShareServerNow();
    }, "Applet Mode is supported with smaller network buffers, but transfers may be slower or less reliable."_i18n);

    options->Add<SidebarEntryCallback>("Install Title Mode forwarder"_i18n, [](){
        InstallTitleModeForwarder();
    }, true, "Install a HOME Menu icon for the current Kefir Hub NRO. Confirmation is required."_i18n);

    options->Add<SidebarEntryCallback>("How to enter Title Mode"_i18n, [](){
        App::ShowTitleModeHelp();
    }, "Show the standard title-takeover instructions."_i18n);
}

} // namespace

void AddInstallShareOptions(Sidebar* options) {
    options->Add<SidebarEntryHeader>("INSTALL & SHARE"_i18n);

    auto web_entry = options->Add<SidebarEntryCallback>("Web Server"_i18n, [](){
        StartShareServerFromTools();
    }, "Start the web sharing server to transfer files via web browser."_i18n);

    // drawn greyed out while the console is offline; pressing it then explains
    // that the internet is off (and offers to connect) instead of opening a
    // server that no browser could reach.
    web_entry->Depends(
        [](){ return net::IsConnectedCached(); },
        "The console has no internet connection."_i18n,
        [](){ net::RequireConnection([](){ StartShareServerFromTools(); }); }
    );

    struct MtpState {
        SidebarEntryCallback* entry{nullptr};
    };
    auto mtp_state = std::make_shared<MtpState>();

    const std::string mtp_title = haze::IsRunning() ? "MTP: Active"_i18n : "Mount MTP"_i18n;

    mtp_state->entry = options->Add<SidebarEntryCallback>(mtp_title, [mtp_state](){
        if (haze::IsRunning()) {
            App::SetMtpEnable(false);
            App::Notify("MTP stopped"_i18n);
            if (mtp_state->entry) {
                mtp_state->entry->SetTitle("Mount MTP"_i18n);
            }
        } else {
            App::SetMtpEnable(true);
            if (haze::IsRunning()) {
                App::Notify("MTP started"_i18n);
                if (mtp_state->entry) {
                    mtp_state->entry->SetTitle("MTP: Active"_i18n);
                }
            } else {
                App::Notify("Failed to start MTP"_i18n);
            }
        }
    }, "Toggle the MTP responder to browse SD card files on PC."_i18n);

#if ENABLE_NETWORK_INSTALL
    options->Add<SidebarEntryCallback>("PC Install (USB)"_i18n, [](){
        App::Push<ui::menu::dbi::Menu>(MenuFlag_None);
    }, "Install games from a PC over USB using DBI backend."_i18n);
#endif
}

void AddSettingsOption(Sidebar* options) {
    options->Add<SidebarEntryHeader>("SETTINGS"_i18n);

    options->Add<SidebarEntryCallback>("Settings"_i18n, [](){
        App::Push<ui::menu::settings::Menu>();
    }, "Open Kefir Hub application settings."_i18n);
}

} // namespace sphaira::ui::menu
