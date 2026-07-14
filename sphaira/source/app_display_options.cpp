#include "app.hpp"
#include "i18n.hpp"
#include "swkbd.hpp"
#include "web.hpp"
#include "ui/sidebar.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/menus/main_menu.hpp"
#include "fs.hpp"
#include "log.hpp"
#include <switch.h>
#include <string>
#include <vector>
#include <cstring>
#include <algorithm>

namespace sphaira {

extern App* g_app;
extern const fs::FsPath DEFAULT_MUSIC_PATH;

void App::DisplayThemeOptions(bool left_side) {
    ui::SidebarEntryArray::Items theme_items{};
    const auto theme_meta = App::GetThemeMetaList();
    for (auto& p : theme_meta) {
        theme_items.emplace_back(p.name);
    }

    auto options = std::make_unique<ui::Sidebar>("Theme Options"_i18n, left_side ? ui::Sidebar::Side::LEFT : ui::Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    options->Add<ui::SidebarEntryArray>("Select Theme"_i18n, theme_items, [](s64& index_out){
        App::SetTheme(index_out);
    }, App::GetThemeIndex(), "Customise the look of Kefir Hub by changing the theme"_i18n);

    options->Add<ui::SidebarEntryBool>("Music"_i18n, App::GetThemeMusicEnable(), [](bool& enable){
        App::SetThemeMusicEnable(enable);
    },  "Enable background music.\n"\
        "Each theme can have it's own music file. "\
        "If a theme does not set a music file, the default music is loaded instead (if it exists)."_i18n);

    options->Add<ui::SidebarEntryBool>("12 Hour Time"_i18n, App::Get12HourTimeEnable(), [](bool& enable){
        App::Set12HourTimeEnable(enable);
    }, "Changes the clock to 12 hour"_i18n);

    options->Add<ui::SidebarEntryCallback>("Download Default Music"_i18n, [](){
        // check if we already have music
        if (fs::FileExists(DEFAULT_MUSIC_PATH)) {
            App::Push<ui::OptionBox>(
                "Overwrite current default music?"_i18n,
                "No"_i18n, "Yes"_i18n, 0, [](auto op_index){
                    if (op_index && *op_index) {
                        download_default_music();
                    }
                }
            );

        } else {
            download_default_music();
        }
    },  "Downloads the default background music for Kefir Hub."_i18n);
}

void App::DisplayNetworkOptions(bool left_side) {

}

void App::DisplayMiscOptions(bool left_side) {
    auto options = std::make_unique<ui::Sidebar>("Misc Options"_i18n, left_side ? ui::Sidebar::Side::LEFT : ui::Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    for (auto& e : ui::menu::main::GetMiscMenuEntries()) {
        if (e.name == g_app->m_left_menu.Get()) {
            continue;
        } else if (e.name == g_app->m_right_menu.Get()) {
            continue;
        }

        auto entry = options->Add<ui::SidebarEntryCallback>(i18n::get(e.title), [e](){
            App::Push(e.func(ui::menu::MenuFlag_None));
        }, i18n::get(e.info));

        if (e.IsInstall()) {
            entry->Depends(App::GetInstallEnable, i18n::get(App::INSTALL_DEPENDS_STR), App::ShowEnableInstallPrompt);
        }
    }

    if (App::IsApplication()) {
        options->Add<ui::SidebarEntryCallback>("Web"_i18n, [](){
            // add some default entries, will use a config file soon so users can set their own.
            ui::PopupList::Items items;
            items.emplace_back("https://lite.duckduckgo.com/lite");
            items.emplace_back("https://dns.switchbru.com");
            items.emplace_back("https://gbatemp.net");
            items.emplace_back("https://github.com/ITotalJustice/sphaira/wiki");
            items.emplace_back("Enter custom URL"_i18n);

            App::Push<ui::PopupList>(
                "Select URL"_i18n, items, [items](auto op_index){
                    if (op_index) {
                        const auto index = *op_index;
                        if (index == items.size() - 1) {
                            std::string out;
                            if (R_SUCCEEDED(swkbd::ShowText(out, "Enter URL"_i18n.c_str(), "https://")) && !out.empty()) {
                                WebShow(out);
                            }
                        } else {
                            WebShow(items[index]);
                        }
                    }
                }
            );
        },
        "Launch the built-in web browser.\n\n",
        "NOTE: The browser is very limted, some websites will fail to load and there's a 30 minute timeout which closes the browser"_i18n);
    }
}

void App::DisplayAdvancedOptions(bool left_side) {
    auto options = std::make_unique<ui::Sidebar>("Advanced Options"_i18n, left_side ? ui::Sidebar::Side::LEFT : ui::Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    ui::SidebarEntryArray::Items text_scroll_speed_items;
    text_scroll_speed_items.push_back("Slow"_i18n);
    text_scroll_speed_items.push_back("Normal"_i18n);
    text_scroll_speed_items.push_back("Fast"_i18n);

    std::vector<std::string> menu_names;
    ui::SidebarEntryArray::Items menu_items;
    for (auto& e : ui::menu::main::GetMiscMenuEntries()) {
        if (!e.IsShortcut()) {
            continue;
        }

        menu_names.emplace_back(e.name);
        menu_items.push_back(i18n::get(e.name));
    }

    options->Add<ui::SidebarEntryBool>("Logging"_i18n, App::GetLogEnable(), [](bool& enable){
        App::SetLogEnable(enable);
    }, "Logs to /config/kefir/log.txt"_i18n);

    options->Add<ui::SidebarEntryBool>("Replace hbmenu on exit"_i18n, App::GetReplaceHbmenuEnable(), [](bool& enable){
        App::SetReplaceHbmenuEnable(enable);
    }, "When enabled, it replaces /hbmenu.nro with Kefir Hub, creating a backup of hbmenu to /switch/hbmenu.nro\n\n" \
       "Disabling will give you the option to restore hbmenu."_i18n);

    options->Add<ui::SidebarEntryBool>("Boost CPU during transfer"_i18n, App::GetApp()->m_progress_boost_mode,
        "Enables boost mode during transfers which can improve transfer speed. "\
        "This sets the CPU to 1785mhz and lowers the GPU 76mhz"_i18n);

    options->Add<ui::SidebarEntryArray>("Text scroll speed"_i18n, text_scroll_speed_items, [](s64& index_out){
        App::SetTextScrollSpeed(index_out);
    }, App::GetTextScrollSpeed(), "Change how fast the scrolling text updates"_i18n);

    options->Add<ui::SidebarEntryArray>("Set left-side menu"_i18n, menu_items, [menu_names](s64& index_out){
        const auto e = menu_names[index_out];
        if (g_app->m_left_menu.Get() != e) {
            // swap menus around.
            if (g_app->m_right_menu.Get() == e) {
                g_app->m_right_menu.Set(g_app->m_left_menu.Get());
            }
            g_app->m_left_menu.Set(e);

            App::Push<ui::OptionBox>(
                "Press OK to restart Kefir Hub"_i18n, "OK"_i18n, [](auto){
                    App::ExitRestart();
                }
            );
        }
    }, i18n::get(g_app->m_left_menu.Get()), "Set the menu that appears on the left tab."_i18n);

    options->Add<ui::SidebarEntryArray>("Set right-side menu"_i18n, menu_items, [menu_names](s64& index_out){
        const auto e = menu_names[index_out];
        if (g_app->m_right_menu.Get() != e) {
            // swap menus around.
            if (g_app->m_left_menu.Get() == e) {
                g_app->m_left_menu.Set(g_app->m_right_menu.Get());
            }
            g_app->m_right_menu.Set(e);

            App::Push<ui::OptionBox>(
                "Press OK to restart Kefir Hub"_i18n, "OK"_i18n, [](auto){
                    App::ExitRestart();
                }
            );
        }
    }, i18n::get(g_app->m_right_menu.Get()), "Set the menu that appears on the right tab."_i18n);

    options->Add<ui::SidebarEntryCallback>("Install options"_i18n, [left_side](){
        App::DisplayInstallOptions(left_side);
    },  "Change the install options.\n"\
        "You can enable installing from here."_i18n);

    options->Add<ui::SidebarEntryCallback>("Dump options"_i18n, [left_side](){
        App::DisplayDumpOptions(left_side);
    },  "Change the dump options."_i18n);

    static const char* erpt_path = "/atmosphere/erpt_reports";
    options->Add<ui::SidebarEntryBool>("Disable erpt_reports"_i18n, fs::FsNativeSd().FileExists(erpt_path), [](bool& enable){
        fs::FsNativeSd fs;
        if (enable) {
            Result rc;
            // it's possible for erpt to generate a report in between deleting the folder and creating the file.
            for (int i = 0; i < 10; i++) {
                fs.DeleteDirectoryRecursively(erpt_path);
                if (R_SUCCEEDED(rc = fs.CreateFile(erpt_path))) {
                    break;
                }
            }
            enable = R_SUCCEEDED(rc);
        } else {
            fs.DeleteFile(erpt_path);
            fs.CreateDirectory(erpt_path);
        }
    }, "Disables error reports generated in /atmosphere/erpt_reports."_i18n);
}

void App::DisplayInstallOptions(bool left_side) {
    auto options = std::make_unique<ui::Sidebar>("Install Options"_i18n, left_side ? ui::Sidebar::Side::LEFT : ui::Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    ui::SidebarEntryArray::Items install_items;
    install_items.push_back("microSD card only"_i18n);
    install_items.push_back("System memory only"_i18n);
    install_items.push_back("System first, then SD"_i18n);
    install_items.push_back("SD first, then system"_i18n);
    install_items.push_back("Automatic"_i18n);

    options->Add<ui::SidebarEntryBool>("Enable sysmmc"_i18n, App::GetInstallSysmmcEnable(), [](bool& enable){
        ShowEnableInstallPromptOption(g_app->m_install_sysmmc, enable);
    }, "Enables installing whilst in sysMMC mode."_i18n);

    options->Add<ui::SidebarEntryBool>("Enable emummc"_i18n, App::GetInstallEmummcEnable(), [](bool& enable){
        ShowEnableInstallPromptOption(g_app->m_install_emummc, enable);
    }, "Enables installing whilst in emuMMC mode."_i18n);

    options->Add<ui::SidebarEntryArray>("Install location"_i18n, install_items, [](s64& index_out){
        App::SetInstallLocation(index_out);
    }, (s64)App::GetInstallLocation());

    auto reserve_entry_ptr = std::make_unique<ui::SidebarEntryTextBase>("Reserve free space"_i18n,
        std::to_string(App::GetInstallReserveMb()) + " MB",
        nullptr,
        "Set the threshold of free space to reserve on installation target (MB)."_i18n
    );
    auto* reserve_entry = reserve_entry_ptr.get();
    reserve_entry->SetCallback([reserve_entry](){
        s64 out = App::GetInstallReserveMb();
        if (R_SUCCEEDED(swkbd::ShowNumPad(out, "Enter Reserve Free Space (MB)"_i18n.c_str(), std::to_string(out).c_str(), 1, 5))) {
            if (out >= 0 && out <= 32768) {
                App::SetInstallReserveMb(out);
                reserve_entry->SetValue(std::to_string(out) + " MB");
            }
        }
    });
    options->Add(std::move(reserve_entry_ptr));

    options->Add<ui::SidebarEntryBool>("Allow downgrade"_i18n, App::GetApp()->m_allow_downgrade,
        "Allows for installing title updates that are lower than the currently installed update."_i18n);

    options->Add<ui::SidebarEntryBool>("Skip if already installed"_i18n, App::GetApp()->m_skip_if_already_installed,
        "Skips installing titles / ncas if they're already installed."_i18n);

    options->Add<ui::SidebarEntryBool>("Ticket only"_i18n, App::GetApp()->m_ticket_only,
        "Installs tickets only, useful if the title was already installed however the tickets were missing or corrupted."_i18n);

    options->Add<ui::SidebarEntryBool>("Skip base"_i18n, App::GetApp()->m_skip_base,
        "Skips installing the base application."_i18n);

    options->Add<ui::SidebarEntryBool>("Skip patch"_i18n, App::GetApp()->m_skip_patch,
        "Skips installing updates."_i18n);

    options->Add<ui::SidebarEntryBool>("Skip dlc"_i18n, App::GetApp()->m_skip_addon,
        "Skips installing DLC."_i18n);

    options->Add<ui::SidebarEntryBool>("Skip data patch"_i18n, App::GetApp()->m_skip_data_patch,
        "Skips installing DLC update (data patch)."_i18n);

    options->Add<ui::SidebarEntryBool>("Skip ticket"_i18n, App::GetApp()->m_skip_ticket,
        "Skips installing tickets, not recommended."_i18n);

    options->Add<ui::SidebarEntryBool>("Skip NCA hash verify"_i18n, App::GetApp()->m_skip_nca_hash_verify,
        "Enables the option to skip sha256 verification. This is a hash over the entire NCA. "\
        "It is used to verify that the NCA is valid / not corrupted. "\
        "You may have seen the option for \"checking for corrupted data\" when a corrupted game is installed. "\
        "That check performs various hash checks, including the hash over the NCA.\n\n"\
        "It is recommended to keep this disabled."_i18n);

    options->Add<ui::SidebarEntryBool>("Skip RSA header verify"_i18n, App::GetApp()->m_skip_rsa_header_fixed_key_verify,
        "Enables the option to skip RSA NCA fixed key verification. "\
        "This is a hash over the NCA header. It is used to verify that the header has not been modified. "\
        "The header is signed by nintendo, thus it cannot be forged, and is reliable to detect modified NCA headers (such as NSP/XCI converts).\n\n"\
        "It is recommended to keep this disabled, unless you need to install nsp/xci converts."_i18n);

    options->Add<ui::SidebarEntryBool>("Skip RSA NPDM verify"_i18n, App::GetApp()->m_skip_rsa_npdm_fixed_key_verify,
        "Enables the option to skip RSA NPDM fixed key verification.\n\n"\
        "Currently, this option is stubbed (not implemented)."_i18n);

    options->Add<ui::SidebarEntryBool>("Ignore distribution bit"_i18n, App::GetApp()->m_ignore_distribution_bit,
        "If set, it will ignore the distribution bit in the NCA header. "\
        "The distribution bit is used to signify whether a NCA is Eshop or GameCard. "\
        "You cannot (normally) launch install games that have the distruction bit set to GameCard.\n\n"\
        "It is recommended to keep this disabled."_i18n);

    options->Add<ui::SidebarEntryBool>("Convert to common ticket"_i18n, App::GetApp()->m_convert_to_common_ticket,
        "[Requires keys] Converts personalised tickets to common (fake) tickets.\n\n"\
        "It is recommended to keep this enabled."_i18n);

    options->Add<ui::SidebarEntryBool>("Convert to standard crypto"_i18n, App::GetApp()->m_convert_to_standard_crypto,
        "[Requires keys] Converts titlekey to standard crypto, also known as \"ticketless\".\n\n"\
        "It is recommended to keep this disabled."_i18n);

    options->Add<ui::SidebarEntryBool>("Lower master key"_i18n, App::GetApp()->m_lower_master_key,
        "[Requires keys] Encrypts the keak (key area key) with master key 0, which allows the game to be launched on every fw. "\
        "Implicitly performs standard crypto.\n\n"\
        "Do note that just because the game can be launched on any fw (as it can be decrypted), doesn't mean it will work. It is strongly recommened to update your firmware and Atmosphere version in order to play the game, rather than enabling this option.\n\n"\
        "It is recommended to keep this disabled."_i18n);

    options->Add<ui::SidebarEntryBool>("Lower system version"_i18n, App::GetApp()->m_lower_system_version,
        "Sets the system_firmware field in the cnmt extended header to 0. "\
        "Note: if the master key is higher than fw version, the game still won't launch as the fw won't have the key to decrypt keak (see above).\n\n"\
        "It is recommended to keep this disabled."_i18n);
}

void App::DisplayDumpOptions(bool left_side) {
    auto options = std::make_unique<ui::Sidebar>("Dump Options"_i18n, left_side ? ui::Sidebar::Side::LEFT : ui::Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    options->Add<ui::SidebarEntryBool>(
        "Created nested folder"_i18n, App::GetApp()->m_dump_app_folder,
        "Creates a folder using the name of the game.\n"\
        "For example, /dumps/XCI/name/name.xci"\
        "Disabling this would use /dumps/XCI/name.xci"_i18n
    );
    options->Add<ui::SidebarEntryBool>(
        "Append folder with .xci"_i18n, App::GetApp()->m_dump_append_folder_with_xci,
        "XCI dumps will name the folder with the .xci extension.\n"\
        "For example, /dumps/XCI/name.xci/name.xci\n\n"
        "Some devices only function is the xci folder is named exactly the same as the xci."_i18n
    );
    options->Add<ui::SidebarEntryBool>(
        "Trim XCI"_i18n, App::GetApp()->m_dump_trim_xci,
        "Removes the unused data at the end of the XCI, making the output smaller."_i18n
    );
    options->Add<ui::SidebarEntryBool>(
        "Label trimmed XCI"_i18n, App::GetApp()->m_dump_label_trim_xci,
        "Names the trimmed xci.\n"
        "For example, /dumps/XCI/name/name (trimmed).xci"_i18n
    );
    options->Add<ui::SidebarEntryBool>(
        "Convert to common ticket"_i18n, App::GetApp()->m_dump_convert_to_common_ticket,
        "Converts personalised ticket to a fake common ticket."_i18n
    );
}

void App::ShowEnableInstallPrompt() {
    // warn the user the dangers of installing.
    App::Push<ui::OptionBox>(
        "Installing is disabled, enable now?"_i18n,
        "Back"_i18n, "Enable"_i18n, 0, [](auto op_index){
            if (op_index && *op_index) {
                // get the install option based on sysmmc/emummc.
                auto& option = IsEmummc() ? g_app->m_install_emummc : g_app->m_install_sysmmc;

                // dummy ref.
                static bool enable{};
                enable = true;

                return ShowEnableInstallPromptOption(option, enable);
            }
        }
    );
}

void App::ShowEnableInstallPromptOption(option::OptionBool& option, bool& enable) {
    if (enable) {
        // warn the user the dangers of installing.
        App::Push<ui::OptionBox>(
            "WARNING: Installing apps will lead to a ban!"_i18n,
            "Back"_i18n, "Enable"_i18n, 0, [&option, &enable](auto op_index){
                if (op_index && *op_index) {
                    option.Set(true);
                    App::Notify("Installing enabled!"_i18n);
                } else {
                    enable = false;
                }
            }
        );
    } else {
        option.Set(false);
    }
}

void App::DisplayMtpStorageOptions(bool left_side) {
    auto options = std::make_unique<ui::Sidebar>("MTP Storages"_i18n, left_side ? ui::Sidebar::Side::LEFT : ui::Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    options->Add<ui::SidebarEntryBool>("Show microSD card"_i18n, App::GetMtpShowSd(), [](bool& enable){
        App::SetMtpShowSd(enable);
    }, "Enable or disable microSD card storage in MTP."_i18n);

    options->Add<ui::SidebarEntryBool>("Show Install folder"_i18n, App::GetMtpShowInstall(), [](bool& enable){
        App::SetMtpShowInstall(enable);
    }, "Enable or disable Install folder in MTP."_i18n);

    options->Add<ui::SidebarEntryBool>("Show Saves (read-only)"_i18n, App::GetMtpShowSaves(), [](bool& enable){
        App::SetMtpShowSaves(enable);
    }, "Show a read-only drive with decrypted game saves. Files can be copied to the PC; writing is disabled."_i18n);

    auto sd_name_entry_ptr = std::make_unique<ui::SidebarEntryTextBase>("microSD card name"_i18n,
        App::GetMtpNameSd().empty() ? "Default"_i18n : App::GetMtpNameSd(),
        nullptr,
        "Set custom name for microSD card in MTP."_i18n
    );
    auto* sd_name_entry = sd_name_entry_ptr.get();
    sd_name_entry->SetCallback([sd_name_entry]() {
        std::string value = App::GetMtpNameSd();
        if (R_SUCCEEDED(swkbd::ShowText(value, "microSD card name"_i18n.c_str(), value.c_str()))) {
            App::SetMtpNameSd(value);
            sd_name_entry->SetValue(value.empty() ? "Default"_i18n : value);
        }
    });
    options->Add(std::move(sd_name_entry_ptr));

    auto install_name_entry_ptr = std::make_unique<ui::SidebarEntryTextBase>("Install folder name"_i18n,
        App::GetMtpNameInstall().empty() ? "Default"_i18n : App::GetMtpNameInstall(),
        nullptr,
        "Set custom name for Install folder in MTP."_i18n
    );
    auto* install_name_entry = install_name_entry_ptr.get();
    install_name_entry->SetCallback([install_name_entry]() {
        std::string value = App::GetMtpNameInstall();
        if (R_SUCCEEDED(swkbd::ShowText(value, "Install folder name"_i18n.c_str(), value.c_str()))) {
            App::SetMtpNameInstall(value);
            install_name_entry->SetValue(value.empty() ? "Default"_i18n : value);
        }
    });
    options->Add(std::move(install_name_entry_ptr));
}

void App::DisplayWebdavOptions(bool left_side) {
    auto options = std::make_unique<ui::Sidebar>("WebDAV Settings"_i18n, left_side ? ui::Sidebar::Side::LEFT : ui::Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    std::string display_url = App::GetWebdavUrl();
    if (display_url.starts_with("webdav://")) {
        display_url = display_url.substr(9);
    }
    
    auto url_entry_ptr = std::make_unique<ui::SidebarEntryTextBase>("Server address"_i18n,
        display_url.empty() ? "None"_i18n : display_url,
        nullptr,
        "Configure the WebDAV server address."_i18n
    );
    auto* url_entry = url_entry_ptr.get();
    url_entry->SetCallback([url_entry]() {
        std::string value = App::GetWebdavUrl();
        if (value.starts_with("webdav://")) {
            value = value.substr(9);
        }
        if (R_SUCCEEDED(swkbd::ShowText(value, "Server address"_i18n.c_str(), value.c_str()))) {
            if (value.empty()) {
                App::SetWebdavUrl("");
                url_entry->SetValue("None"_i18n);
                return;
            }
            if (value.starts_with("http://")) {
                App::Notify("HTTP protocol is not allowed. Please use HTTPS/WebDAV."_i18n);
                return;
            }
            std::string final_url = value;
            if (final_url.starts_with("https://")) {
                final_url = "webdav://" + final_url.substr(8);
            } else if (!final_url.starts_with("webdav://")) {
                final_url = "webdav://" + final_url;
            }
            App::SetWebdavUrl(final_url);
            
            std::string clean_display = final_url;
            if (clean_display.starts_with("webdav://")) {
                clean_display = clean_display.substr(9);
            }
            url_entry->SetValue(clean_display);
        }
    });
    options->Add(std::move(url_entry_ptr));

    auto user_name_entry_ptr = std::make_unique<ui::SidebarEntryTextBase>("Username"_i18n,
        App::GetWebdavUser().empty() ? "None"_i18n : App::GetWebdavUser(),
        nullptr,
        "Set the WebDAV server username."_i18n
    );
    auto* user_name_entry = user_name_entry_ptr.get();
    user_name_entry->SetCallback([user_name_entry]() {
        std::string value = App::GetWebdavUser();
        if (R_SUCCEEDED(swkbd::ShowText(value, "Username"_i18n.c_str(), value.c_str()))) {
            App::SetWebdavUser(value);
            user_name_entry->SetValue(value.empty() ? "None"_i18n : value);
        }
    });
    options->Add(std::move(user_name_entry_ptr));

    auto password_entry_ptr = std::make_unique<ui::SidebarEntryTextBase>("Password"_i18n,
        App::GetWebdavPass().empty() ? "None"_i18n : std::string(8, '*'),
        nullptr,
        "Set the WebDAV server password."_i18n
    );
    auto* password_entry = password_entry_ptr.get();
    password_entry->SetCallback([password_entry]() {
        std::string value = "";
        if (R_SUCCEEDED(swkbd::ShowText(value, "Password"_i18n.c_str()))) {
            App::SetWebdavPass(value);
            password_entry->SetValue(value.empty() ? "None"_i18n : std::string(8, '*'));
        }
    });
    options->Add(std::move(password_entry_ptr));
}

} // namespace sphaira
