#include "ui/menus/install_share.hpp"

#include "ui/menus/settings_menu.hpp"
#include "ui/menus/dbi_menu.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "ui/menus/homebrew.hpp"
#include "ui/sidebar.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/nvg_util.hpp"
#include "haze_helper.hpp"

#include "account_user.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "net.hpp"
#include "nro.hpp"
#include "nacp_util.hpp"
#include "web.hpp"
#include "web_screenshots.hpp"

#include <algorithm>
#include <memory>
#include <vector>

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

void StartConsoleTransferShare(const std::vector<fs::FsPath>& targets, bool plain_root = false) {
    if (targets.empty()) {
        App::PushErrorBox(Result_FsEmpty, "Failed to start folder server"_i18n);
        return;
    }

    net::RequireConnection([targets, plain_root]() {
        fs::FsNativeSd fs;
        std::vector<fs::FsPath> valid_targets;
        for (const auto& target : targets) {
            const auto s = target.toString();
            if (s == "/" || fs.DirExists(target)) {
                valid_targets.push_back(target);
            }
        }

        if (valid_targets.empty()) {
            App::PushErrorBox(Result_FsInvalidType, "Failed to start folder server"_i18n);
            return;
        }

        const auto& primary = valid_targets.front();
        App::SetMountedFolders(valid_targets);

        WebShareResult result;
        const auto rc = plain_root ? WebStartServer("", result) : WebShareFolder(primary, result);
        if (R_FAILED(rc)) {
            App::PushErrorBox(rc, "Failed to start folder server"_i18n);
            return;
        }

        if (!result.listener_self_test) {
            App::Notify("Web listener started, but its local self-test failed; check the log or use Title Mode"_i18n);
        }

        if (WebGetProgressBox()) {
            nvgDeleteImage(App::GetVg(), result.qr_image);
            App::Notify("Mounted over HTTP: "_i18n + result.url);
            return;
        }

        WebPushServerProgressBox(result.url, result.qr_image, "Console Transfer"_i18n);
    });
}

void StartConsoleTransferShare(const std::vector<std::string>& roots, bool plain_root = false) {
    std::vector<fs::FsPath> targets;
    targets.reserve(roots.size());
    for (const auto& r : roots) {
        targets.emplace_back(r);
    }
    StartConsoleTransferShare(targets, plain_root);
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

ConsoleTransferMenu::ConsoleTransferMenu() : MenuBase{"Console Transfer"_i18n, MenuFlag_None} {
    m_items = {
        {
            "Share Entire microSD"_i18n,
            "Share all files and folders on the microSD card."_i18n,
            [](){
                StartConsoleTransferShare(std::vector<fs::FsPath>{ fs::FsPath{"/"} });
            }
        },
        {
            "Share Save Backups"_i18n,
            "Share save data backups from configured locations, including DBI backups."_i18n,
            [](){
                StartConsoleTransferShare(save::GetShareableSaveBackupRoots());
            }
        },
        {
            "Share User Backups"_i18n,
            "Share user packages and profile backups."_i18n,
            [](){
                StartConsoleTransferShare(std::vector<std::string>{ account_user::GetUserPacksRoot() }, true);
            }
        },
        {
            "Share Screenshots & Videos"_i18n,
            "Share album screenshots and captured gameplay videos."_i18n,
            [](){
                StartConsoleTransferShare(std::vector<std::string>{ GetAlbumRoot() });
            }
        },
        {
            "Share switch Folder"_i18n,
            "Share homebrew applications and tools from the selected source."_i18n,
            [](){
                StartConsoleTransferShare(homebrew::GetShareableHomebrewRoots());
            }
        },
        {
            "Choose Folder..."_i18n,
            "Select a custom folder on the microSD card to share."_i18n,
            [](){
                auto browser = std::make_unique<filebrowser::Menu>(MenuFlag_None);
                browser->SetFolderPicker([](const fs::FsPath& folder){
                    StartConsoleTransferShare(std::vector<fs::FsPath>{ folder });
                });
                App::Push(std::move(browser));
            }
        },
    };

    this->SetActions(
        std::make_pair(Button::A, Action{"Open"_i18n, [this](){ OnSelect(); }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){ SetPop(); }})
    );

    m_list = std::make_unique<List>(1, 6, Vec4{75.f, 132.f, 1145.f, 462.f}, Vec4{75.f, 132.f, 1130.f, 66.f});
    m_list->SetLayout(List::Layout::GRID);
    m_list->SetPageJump(false);
    SetIndex(0);
}

void ConsoleTransferMenu::Update(Controller* controller, TouchInfo* touch) {
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

void ConsoleTransferMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    m_list->Draw(vg, theme, m_items.size(), [vg, theme, this](auto*, auto*, Vec4 v, auto i) {
        const auto& item = m_items[i];
        const auto is_selected = m_index == static_cast<s64>(i);
        const auto text_id = is_selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;
        if (is_selected) {
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else {
            DrawElement(v, ThemeEntryID_GRID);
        }
        gfx::drawText(vg, v.x + 20.f, v.y + v.h / 2.f - 10.f, 18.f,
            theme->GetColour(text_id), item.label.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        gfx::drawText(vg, v.x + 20.f, v.y + v.h / 2.f + 14.f, 14.f,
            theme->GetColour(ThemeEntryID_TEXT_INFO), item.description.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    });
}

void ConsoleTransferMenu::OnFocusGained() {
    MenuBase::OnFocusGained();
    SetIndex(m_index);
}

void ConsoleTransferMenu::SetIndex(s64 index) {
    if (m_items.empty()) {
        m_index = 0;
        return;
    }
    m_index = std::clamp<s64>(index, 0, static_cast<s64>(m_items.size() - 1));
    if (!m_index) {
        m_list->SetYoff(0);
    }
    SetTitleSubHeading(m_items[m_index].description, true);
    SetSubHeading("");
}

void ConsoleTransferMenu::OnSelect() {
    if (!m_items.empty() && m_items[m_index].action) {
        m_items[m_index].action();
    }
}

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
    }, "Install games from a PC over USB: DBI Backend, ns-usbloader (Awoo/Tinfoil or GoldLeaf) and fluffy."_i18n);
#endif
}

void AddSettingsOption(Sidebar* options) {
    options->Add<SidebarEntryHeader>("SETTINGS"_i18n);

    options->Add<SidebarEntryCallback>("Settings"_i18n, [](){
        App::Push<ui::menu::settings::Menu>();
    }, "Open Kefir Hub application settings."_i18n);
}

} // namespace sphaira::ui::menu
