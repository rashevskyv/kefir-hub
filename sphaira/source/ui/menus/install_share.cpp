#include "ui/menus/install_share.hpp"

#include "ui/menus/console_games_transfer.hpp"
#include "ui/menus/settings_menu.hpp"
#include "ui/menus/dbi_menu.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "ui/menus/homebrew.hpp"
#include "ui/menus/ownfoil.hpp"
#include "ui/sidebar.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/nvg_util.hpp"
#include "haze_helper.hpp"

#include "account/account_user.hpp"
#include "app.hpp"
#include "app_paths.hpp"
#include "defines.hpp"
#include "forwarder_auto_plan.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "net.hpp"
#include "nro.hpp"
#include "nacp_util.hpp"
#include "download.hpp"
#include "swkbd.hpp"
#include "web.hpp"
#include "web_screenshots.hpp"

#include <algorithm>
#include <memory>
#include <vector>
#include <yyjson.h>

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

        WebPushServerProgressBox(result.ip_url, result.qr_image, "Web Sharing Server"_i18n);
    });
}

void StartConsoleTransferShare(const std::vector<fs::FsPath>& targets) {
    if (targets.empty()) {
        App::PushErrorBox(Result_FsEmpty, "Failed to start folder server"_i18n);
        return;
    }

    net::RequireConnection([targets]() {
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

        App::SetMountedFolders(valid_targets);

        WebShareResult result;
        const auto rc = WebStartServer("", result);
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

        WebPushServerProgressBox(result.ip_url, result.qr_image, "Console Transfer"_i18n);
    });
}

void StartConsoleTransferShare(const std::vector<std::string>& roots) {
    std::vector<fs::FsPath> targets;
    targets.reserve(roots.size());
    for (const auto& r : roots) {
        targets.emplace_back(r);
    }
    StartConsoleTransferShare(targets);
}

void InstallTitleModeForwarder() {
    const bool replace_hbmenu = App::GetReplaceHbmenuEnable();
    OwoConfig config{};
    config.nro_path = replace_hbmenu ? "/hbmenu.nro" : App::GetExePath().toString();
    config.title_id = forwarder_auto::KEFIR_HUB_FORWARDER_TID;

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

    const auto prompt = replace_hbmenu
        ? "Install a HOME Menu forwarder for Kefir Hub?\n\n"
          "This creates a Title Mode entry for /hbmenu.nro. After installation, "
          "return to HOME and launch the new icon manually."_i18n
        : "Install a HOME Menu forwarder for Kefir Hub?\n\n"
          "This creates a Title Mode entry for the current NRO. After installation, "
          "return to HOME and launch the new icon manually."_i18n;

    App::Push<OptionBox>(
        prompt,
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
            "Send installed games"_i18n,
            "Offer installed games (base, updates, DLC) to another console on the same Wi-Fi."_i18n,
            [](){
                games_transfer::Send();
            }
        },
        {
            "Receive games"_i18n,
            "Install games offered by another console. Enter its address."_i18n,
            [](){
                games_transfer::Receive();
            }
        },
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
                StartConsoleTransferShareUserBackups();
            }
        },
        {
            "Share Profiles & Play Hours"_i18n,
            "Share profiles and play hours NAND backup packs."_i18n,
            [](){
                StartConsoleTransferShareNandBackups();
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

    m_list->Draw(vg, theme, m_items.size(), m_index, [vg, theme, this](auto*, auto*, Vec4 v, auto i) {
        const auto& item = m_items[i];
        const bool focused = (m_index == static_cast<s64>(i));
        if (focused) {
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else {
            DrawElement(v, ThemeEntryID_GRID);
        }
        gfx::drawText(vg, v.x + 20.f, v.y + v.h / 2.f - 10.f, 18.f,
            theme->GetColour(focused ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT), item.label.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
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

    options->Add<SidebarEntryCallback>("Ownfoil"_i18n, [](){
        App::Push<ui::menu::ownfoil::Menu>(MenuFlag_None);
    }, "Browse and install titles from a self-hosted Ownfoil server."_i18n);
}

void AddSettingsOption(Sidebar* options) {
    options->Add<SidebarEntryHeader>("SETTINGS"_i18n);

    options->Add<SidebarEntryCallback>("Install Title Mode forwarder"_i18n, [](){
        InstallTitleModeForwarder();
    }, "Install a HOME Menu icon for the current Kefir Hub NRO. Confirmation is required."_i18n);

    options->Add<SidebarEntryCallback>("Settings"_i18n, [](){
        App::Push<ui::menu::settings::Menu>();
    }, "Open Kefir Hub application settings."_i18n);
}

void StartConsoleTransferShareUserBackups() {
    StartConsoleTransferShare(account_user::GetShareableUserBackupRoots());
}

void StartConsoleTransferShareNandBackups() {
    fs::FsNativeSd sd;
    const auto root = paths::DATA_ROOT + "/nand_transfer";
    if (!sd.DirExists(root.c_str())) {
        sd.CreateDirectoryRecursively(root.c_str());
    }
    StartConsoleTransferShare(std::vector<std::string>{ root });
}

void ConnectConsoleTransfer(std::function<void(const std::string& base_url)> on_connected) {
#if DOCS_DEMO
    // docs screenshots: Eden's keyboard opens outside the frame, so the fictional sending console is taken as
    // typed; its replies are http fixtures (demo_http.cpp).
    if (on_connected) {
        on_connected("http://192.168.0.51:8080");
    }
    return;
#endif
    net::RequireConnection([on_connected](){
        u32 ip{};
        std::string initial_prefix;
        if (R_SUCCEEDED(nifmGetCurrentIpAddress(&ip)) && ip != 0) {
            char prefix[32]{};
            std::snprintf(prefix, sizeof(prefix), "%u.%u.%u.",
                ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF);
            initial_prefix = prefix;
        }

        std::string input;
        if (R_FAILED(swkbd::ShowText(input, "Enter sending console IP address"_i18n.c_str(),
                initial_prefix.empty() ? nullptr : initial_prefix.c_str())) || input.empty()) {
            return;
        }
        while (!input.empty() && (input.front() == ' ' || input.front() == '\t' || input.front() == '\r' || input.front() == '\n')) {
            input.erase(input.begin());
        }
        while (!input.empty() && (input.back() == ' ' || input.back() == '\t' || input.back() == '\r' || input.back() == '\n')) {
            input.pop_back();
        }
        if (input.empty()) {
            return;
        }

        auto responding_url = std::make_shared<std::string>();
        auto probed_ok = std::make_shared<bool>(false);

        App::Push<ProgressBox>(
            0,
            "Testing Connection..."_i18n,
            input,
            [input, responding_url, probed_ok](auto pbox) -> Result {
                std::string base_input = input;
                while (!base_input.empty() && (base_input.back() == '/' || base_input.back() == '\\')) {
                    base_input.pop_back();
                }

                std::vector<std::string> candidate_urls;
                if (base_input.rfind("http://", 0) == 0 || base_input.rfind("https://", 0) == 0) {
                    candidate_urls.push_back(base_input);
                } else if (base_input.find(':') != std::string::npos) {
                    candidate_urls.push_back("http://" + base_input);
                } else {
                    candidate_urls.push_back("http://" + base_input);
                    for (u16 port = 8080; port <= 8090; ++port) {
                        candidate_urls.push_back("http://" + base_input + ":" + std::to_string(port));
                    }
                }

                for (const auto& url : candidate_urls) {
                    if (pbox->ShouldExit()) {
                        return pbox->ShouldExitResult();
                    }
                    pbox->SetTransfer(url);
                    curl::Api api;
                    api.SetOption(curl::Url{url + "/list"});
                    api.SetOption(curl::OnProgress{[pbox](s64, s64, s64, s64) {
                        return !pbox->ShouldExit();
                    }});
                    const auto res = curl::ToMemory(api);
                    if (pbox->ShouldExit()) {
                        return pbox->ShouldExitResult();
                    }
                    if (res.success && res.code == 200 && !res.data.empty()) {
                        yyjson_doc* doc = yyjson_read(reinterpret_cast<const char*>(res.data.data()), res.data.size(), 0);
                        if (doc) {
                            yyjson_val* root = yyjson_doc_get_root(doc);
                            if (yyjson_is_obj(root)) {
                                yyjson_val* entries = yyjson_obj_get(root, "entries");
                                if (yyjson_is_arr(entries)) {
                                    *responding_url = url;
                                    *probed_ok = true;
                                    yyjson_doc_free(doc);
                                    break;
                                }
                            }
                            yyjson_doc_free(doc);
                        }
                    }
                }

                return (*probed_ok && !responding_url->empty()) ? Result{0} : static_cast<Result>(Result_FsEmpty);
            },
            [responding_url, probed_ok, on_connected](Result rc) {
                if (rc == Result_TransferCancelled) {
                    return;
                }
                if (!*probed_ok || responding_url->empty()) {
                    App::Push<OptionBox>(
                        "Could not connect to the remote console.\n\n"
                        "Confirm that both consoles are connected to the same local network and that the source console has Share active."_i18n,
                        "OK"_i18n
                    );
                    return;
                }
                if (on_connected) {
                    on_connected(*responding_url);
                }
            }
        );
    });
}

} // namespace sphaira::ui::menu
