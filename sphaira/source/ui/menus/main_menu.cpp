#include "ui/menus/main_menu.hpp"

#include "ui/menus/homebrew.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/menus/tools_menu.hpp"
#include "ui/menus/settings_menu.hpp"
#include "ui/menus/themezer.hpp"
#include "ui/menus/ghdl.hpp"
#include "ui/menus/dbi_menu.hpp"
#include "ui/menus/gc_menu.hpp"
#include "ui/menus/game_menu.hpp"
#include "ui/menus/save_menu.hpp"
#include "ui/menus/save/save_hub_menu.hpp"
#include "ui/menus/appstore.hpp"

#include "app.hpp"
#include "auto_update.hpp"
#include "log.hpp"
#include "download.hpp"
#include "defines.hpp"
#include "i18n.hpp"

#include <yyjson.h>
#include <iomanip>

namespace sphaira::ui::menu::main {
namespace {

constexpr const char* GITHUB_URL{"https://api.github.com/repos/rashevskyv/kefir-hub/releases/latest"};
constexpr fs::FsPath CACHE_PATH{"/switch/sphaira/cache/sphaira_latest.json"};
constexpr fs::FsPath UPDATE_TEMP_PATH{"/switch/sphaira/cache/sphaira_update.temp"};
constexpr long HTTP_NOT_FOUND{404};

template<typename T>
auto MiscMenuFuncGenerator(u32 flags) {
    return std::make_unique<T>(flags);
}

const MiscMenuEntry MISC_MENU_ENTRIES[] = {
    { .name = "Appstore", .title = "Appstore", .func = MiscMenuFuncGenerator<ui::menu::appstore::Menu>, .flag = MiscMenuFlag_Shortcut, .info =
        "Download and update apps.\n\n"\
        "Internet connection required." },

    { .name = "Games", .title = "Games", .func = MiscMenuFuncGenerator<ui::menu::game::Menu>, .flag = MiscMenuFlag_Shortcut, .info =
        "View all installed games. "\
        "In this menu you can launch, backup, create savedata and much more." },

    { .name = "FileBrowser", .title = "FileBrowser", .func = MiscMenuFuncGenerator<ui::menu::filebrowser::Menu>, .flag = MiscMenuFlag_Shortcut, .info =
        "Browse files on you SD Card. "\
        "You can move, copy, delete, extract zip, create zip, upload and much more.\n\n"\
        "A connected USB/HDD can be opened by mounting it in the advanced options." },

    { .name = "Saves", .title = "Saves", .func = MiscMenuFuncGenerator<ui::menu::save::SaveHubMenu>, .flag = MiscMenuFlag_Shortcut, .info =
        "View save data for each user. "\
        "You can backup and restore saves.\n\n"\
        "Experimental support for backing up system saves is possible." },

    { .name = "Themezer", .title = "Themezer", .func = MiscMenuFuncGenerator<ui::menu::themezer::Menu>, .flag = MiscMenuFlag_Shortcut, .info =
        "Download themes from themezer.net. "\
        "Themes are downloaded to /themes/sphaira\n"\
        "To install the themes, NXThemesInstaller needs to be installed (can be downloaded via the AppStore)." },

    { .name = "GitHub", .title = "GitHub", .func = MiscMenuFuncGenerator<ui::menu::gh::Menu>, .flag = MiscMenuFlag_Shortcut, .info =
        "Download releases directly from GitHub. "\
        "Custom entries can be added to /config/kefir/github" },

    { .name = "GameCard", .title = "GameCard", .func = MiscMenuFuncGenerator<ui::menu::gc::Menu>, .flag = MiscMenuFlag_Shortcut, .info =
        "View info on the inserted Game Card (GC). "\
        "You can backup and install the inserted GC. "\
        "To swap GC's, simply remove the old GC and insert the new one. "\
        "You do not need to exit the menu." },
};

} // namespace

auto GetMiscMenuEntries() -> std::span<const MiscMenuEntry> {
    return MISC_MENU_ENTRIES;
}

MainMenu::MainMenu() {
    curl::Api().ToFileAsync(
        curl::Url{GITHUB_URL},
        curl::Path{CACHE_PATH},
        curl::Flags{curl::Flag_Cache},
        curl::StopToken{this->GetToken()},
        curl::Header{
            { "Accept", "application/vnd.github+json" },
        },
        curl::OnComplete{[this](auto& result){
            log_write("inside github download\n");
            m_update_state = UpdateState::Error;
            ON_SCOPE_EXIT( log_write("update status: %u\n", (u8)m_update_state) );

            if (!result.success) {
                if (result.code == HTTP_NOT_FOUND) {
                    m_update_state = UpdateState::None;
                    log_write("no github release found for update check\n");
                    return true;
                }

                return false;
            }

            auto json = yyjson_read_file(CACHE_PATH, YYJSON_READ_NOFLAG, nullptr, nullptr);
            R_UNLESS(json, false);
            ON_SCOPE_EXIT(yyjson_doc_free(json));

            auto root = yyjson_doc_get_root(json);
            R_UNLESS(root, false);

            auto tag_key = yyjson_obj_get(root, "tag_name");
            R_UNLESS(tag_key, false);

            const auto version = yyjson_get_str(tag_key);
            R_UNLESS(version, false);
            if (!App::IsVersionNewer(APP_VERSION, version)) {
                m_update_state = UpdateState::None;
                return true;
            }

            auto body_key = yyjson_obj_get(root, "body");
            const auto body = body_key ? yyjson_get_str(body_key) : "";

            auto assets_val = yyjson_obj_get(root, "assets");
            std::vector<auto_update::ReleaseAsset> assets;
            if (assets_val && yyjson_is_arr(assets_val)) {
                size_t idx, max;
                yyjson_val* asset_item;
                yyjson_arr_foreach(assets_val, idx, max, asset_item) {
                    if (!yyjson_is_obj(asset_item)) continue;
                    auto name_val = yyjson_obj_get(asset_item, "name");
                    auto url_val = yyjson_obj_get(asset_item, "browser_download_url");
                    auto type_val = yyjson_obj_get(asset_item, "content_type");
                    auto size_val = yyjson_obj_get(asset_item, "size");
                    if (name_val && url_val) {
                        assets.push_back({
                            .name = yyjson_get_str(name_val) ? yyjson_get_str(name_val) : "",
                            .browser_download_url = yyjson_get_str(url_val) ? yyjson_get_str(url_val) : "",
                            .content_type = (type_val && yyjson_get_str(type_val)) ? yyjson_get_str(type_val) : "",
                            .size = size_val ? yyjson_get_uint(size_val) : 0,
                        });
                    }
                }
            }

            m_update_version = version;
            m_update_description = body ? body : "";
            m_update_state = UpdateState::Update;

            const int best_idx = auto_update::SelectBestAsset(assets, App::GetExePath().s);
            if (best_idx >= 0 && best_idx < static_cast<int>(assets.size())) {
                m_update_url = assets[best_idx].browser_download_url;
            }

            if (App::GetAutoUpdateEnable() && best_idx >= 0) {
                const auto& target_asset = assets[best_idx];
                log_write("[AutoUpdate] Background downloading update %s (%s)\n", version, target_asset.name.c_str());

                fs::FsNativeSd().CreateDirectoryRecursively("/switch/sphaira/cache");

                const std::string ver_str = version;
                const std::string asset_name = target_asset.name;

                curl::Api().ToFileAsync(
                    curl::Url{target_asset.browser_download_url},
                    curl::Path{UPDATE_TEMP_PATH},
                    curl::StopToken{this->GetToken()},
                    curl::OnComplete{[ver_str, asset_name](auto& dl_result) {
                        if (!dl_result.success) {
                            log_write("[AutoUpdate] Download failed (code: %ld)\n", dl_result.code);
                            fs::FsNativeSd().DeleteFile(UPDATE_TEMP_PATH);
                            return false;
                        }

                        const fs::FsPath exe_path = App::GetExePath();
                        const fs::FsPath dest_path = auto_update::ResolveInstallDestination(exe_path);
                        const bool replace_hbmenu = App::GetReplaceHbmenuEnable();

                        log_write("[AutoUpdate] Installing update to %s (replace_hbmenu=%d)\n", dest_path.s, replace_hbmenu);

                        if (auto_update::InstallNroUpdate(UPDATE_TEMP_PATH, dest_path, replace_hbmenu)) {
                            log_write("[AutoUpdate] Update applied silently to %s\n", dest_path.s);
                        } else {
                            log_write("[AutoUpdate] Failed to install update to %s\n", dest_path.s);
                        }

                        fs::FsNativeSd().DeleteFile(UPDATE_TEMP_PATH);
                        return true;
                    }}
                );
            } else {
                log_write("[UpdateCheck] Newer version available: %s (auto-update disabled)\n", m_update_version.c_str());
            }

            return true;
        }
    });

    this->SetActions(
        std::make_pair(Button::START, Action{"Options"_i18n, [this](){
            if (m_current_menu) {
                m_current_menu->FireAction(Button::START);
            }
        }}),
        std::make_pair(Button::B, Action{"Exit"_i18n, App::Exit}),
        std::make_pair(Button::SELECT, Action{App::Exit})
    );

    m_centre_menu = std::make_unique<homebrew::Menu>();
    m_tools_menu = std::make_unique<tools::Menu>();
    m_current_menu = m_centre_menu.get();

    AddOnLRPress();

    for (auto [button, action] : m_actions) {
        if (button != Button::START) {
            m_current_menu->SetAction(button, action);
        }
    }
}

MainMenu::~MainMenu() {

}

void MainMenu::Update(Controller* controller, TouchInfo* touch) {
    m_current_menu->Update(controller, touch);
}

void MainMenu::Draw(NVGcontext* vg, Theme* theme) {
    m_current_menu->Draw(vg, theme);
}

void MainMenu::OnFocusGained() {
    Widget::OnFocusGained();
    m_current_menu->OnFocusGained();
}

void MainMenu::OnFocusLost() {
    Widget::OnFocusLost();
    m_current_menu->OnFocusLost();
}

void MainMenu::SwitchTo(MenuBase* menu) {
    if (m_current_menu == menu) {
        return;
    }

    m_current_menu->OnFocusLost();
    m_current_menu = menu;
    AddOnLRPress();
    m_current_menu->OnFocusGained();

    for (auto [button, action] : m_actions) {
        if (button != Button::START) {
            m_current_menu->SetAction(button, action);
        }
    }
}

void MainMenu::AddOnLRPress() {
    RemoveAction(Button::L);
    RemoveAction(Button::R);

    if (m_current_menu == m_centre_menu.get()) {
        SetAction(Button::R, Action{i18n::get(m_tools_menu->GetShortTitle()), [this]{
            SwitchTo(m_tools_menu.get());
        }});
    } else {
        SetAction(Button::L, Action{i18n::get(m_centre_menu->GetShortTitle()), [this]{
            SwitchTo(m_centre_menu.get());
        }});
    }
}

} // namespace sphaira::ui::menu::main
