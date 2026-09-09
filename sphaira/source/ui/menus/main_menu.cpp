#include "ui/menus/main_menu.hpp"

#include "ui/menus/homebrew.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/menus/tools_menu.hpp"
#include "ui/menus/users_menu.hpp"
#include "ui/menus/settings_menu.hpp"
#include "ui/menus/themezer.hpp"
#include "ui/menus/ghdl.hpp"
#include "ui/menus/dbi_menu.hpp"
#include "ui/menus/gc_menu.hpp"
#include "ui/menus/game_menu.hpp"
#include "ui/menus/save_menu.hpp"
#include "ui/menus/save/save_hub_menu.hpp"
#include "ui/menus/appstore.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"

#include "account/account_link.hpp"
#include "app.hpp"
#include "auto_update.hpp"
#include "log.hpp"
#include "download.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "utils/utils.hpp"
#include "version_compare.hpp"

#include <yyjson.h>
#include <iomanip>

namespace sphaira::ui::menu::main {
namespace {

constexpr const char* GITHUB_URL{"https://api.github.com/repos/rashevskyv/kefir-hub/releases/latest"};
constexpr fs::FsPath CACHE_PATH{"/switch/sphaira/cache/sphaira_latest.json"};
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

void StartLaunchAccountLink() {
    App::Push<ProgressBox>(0, "Link Nintendo Account"_i18n, "Linking account..."_i18n,
        [](auto pbox) -> Result {
            pbox->NewTransfer("Applying Nintendo Account link"_i18n);
            u32 count = 0;
            R_TRY(account_link::LinkAllFromRomfsDonor(count));
            R_SUCCEED();
        },
        [](Result rc) {
            if (R_FAILED(rc)) {
                if (account_link::ConsumeAccountDaemonsTerminated()) {
                    utils::requestForcedReboot();
                    return;
                }
                App::Push<OptionBox>("Failed to link Nintendo Account."_i18n, "OK"_i18n);
            } else {
                utils::requestForcedReboot();
            }
        }, 1, PRIO_PREEMPTIVE, 1024 * 128, false);
}

void CheckLaunchAccountLinkPrompt() {
    if (!account_link::CanOfferLaunchLink()) {
        return;
    }
    account_link::SetLaunchLinkPrompted(true);

    // 3-button layout matches updates: top Minus = don't remind; bottom B = Later, + = Link.
    App::Push<OptionBox>(
        "Some user profiles are not linked to a Nintendo Account.\n\n"
        "Kefir Hub can safely link them using built-in donors. Each unlinked profile receives a different donor. This does not delete or modify game saves. "
        "Some games require a linked Nintendo Account to start. Already linked profiles are left unchanged.\n\n"
        "The console will reboot after linking."_i18n,
        "Later"_i18n, "Don't remind again"_i18n, "Link and reboot"_i18n, 2,
        [](auto op) {
            if (!op) {
                return;
            }
            if (*op == 2) {
                StartLaunchAccountLink();
            } else if (*op == 1) {
                App::SetAccountLinkPromptSkip(true);
                App::Push<OptionBox>(
                    "You won't be asked again at launch. To link manually later: Tools → Users → Link Nintendo Account."_i18n,
                    "OK"_i18n);
            } else {
                App::Push<OptionBox>(
                    "You can link later in Tools → Users → Link Nintendo Account. Some games require a linked account to start."_i18n,
                    "OK"_i18n);
            }
        });
}

} // namespace

auto GetMiscMenuEntries() -> std::span<const MiscMenuEntry> {
    return MISC_MENU_ENTRIES;
}

MainMenu::MainMenu() {
    // Launch account-link prompt is deferred to OnFocusGained: Push from this
    // constructor would emplace OptionBox before MainMenu is on the stack, so
    // MainMenu would cover the dialog and it would never receive input.

    const auto update_mode = static_cast<auto_update::Mode>(App::GetAutoUpdateMode());
    if (update_mode != auto_update::Mode::Off) {
        auto_update::SetJobState(auto_update::JobState::Checking);
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
                        auto_update::SetJobState(auto_update::JobState::Idle);
                        log_write("[UpdateCheck] no github release found for update check\n");
                        return true;
                    }
                    log_write("[UpdateCheck] github release request failed: code %ld\n", result.code);
                    auto_update::SetJobState(auto_update::JobState::Failed);
                    return false;
                }

                if (result.code != 200 && result.code != 304) {
                    log_write("[UpdateCheck] non-successful HTTP code: %ld, skipping update check\n", result.code);
                    m_update_state = UpdateState::None;
                    auto_update::SetJobState(auto_update::JobState::Idle);
                    return true;
                }

                auto json = yyjson_read_file(CACHE_PATH, YYJSON_READ_NOFLAG, nullptr, nullptr);
                if (!json) {
                    log_write("[UpdateCheck] failed to parse cached release JSON\n");
                    auto_update::SetJobState(auto_update::JobState::Failed);
                    return false;
                }
                ON_SCOPE_EXIT(yyjson_doc_free(json));

                auto root = yyjson_doc_get_root(json);
                auto tag_key = root ? yyjson_obj_get(root, "tag_name") : nullptr;
                const auto version = tag_key ? yyjson_get_str(tag_key) : nullptr;
                if (!version || *version == '\0') {
                    log_write("[UpdateCheck] tag_name missing or empty in release JSON\n");
                    auto_update::SetJobState(auto_update::JobState::Failed);
                    return false;
                }

                if (!version::IsNewer(APP_VERSION, version)) {
                    log_write("[UpdateCheck] Installed %s >= remote %s; no update required\n", APP_VERSION, version);
                    m_update_state = UpdateState::None;
                    auto_update::SetJobState(auto_update::JobState::Idle);
                    return true;
                }

                log_write("[UpdateCheck] Found newer remote release %s (installed: %s)\n", version, APP_VERSION);

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

                std::string url;
                const int best_idx = auto_update::SelectBestAsset(assets, App::GetExePath().s);
                if (best_idx >= 0 && best_idx < static_cast<int>(assets.size())) {
                    url = assets[best_idx].browser_download_url;
                    m_update_url = url;
                }

                if (url.empty()) {
                    auto_update::SetJobState(auto_update::JobState::Failed);
                    return true;
                }

                if (version::IsEqual(App::GetAutoUpdateSkip(), version)) {
                    log_write("[UpdateCheck] %s skipped\n", version);
                    auto_update::SetJobState(auto_update::JobState::Idle);
                    m_update_state = UpdateState::None;
                    return true;
                }

                auto_update::SetAvailable(version, url);
                const auto mode = static_cast<auto_update::Mode>(App::GetAutoUpdateMode());
                if (mode == auto_update::Mode::Silent) {
                    auto_update::StartDownload();
                } else if (mode == auto_update::Mode::Notify && auto_update::ConsumeNotifyPrompt()) {
                    App::Push<OptionBox>(
                        "A new version is available. Update now?"_i18n,
                        "Later"_i18n, "Skip this update"_i18n, "Update"_i18n, 2,
                        [ver = std::string(version)](auto op) {
                            if (!op) {
                                return;
                            }
                            if (*op == 2) {
                                auto_update::StartDownload();
                            } else if (*op == 1) {
                                App::SetAutoUpdateSkip(ver);
                                auto_update::SetJobState(auto_update::JobState::Idle);
                            }
                        }
                    );
                } else {
                    log_write("[UpdateCheck] %s available (mode=%ld)\n", version, App::GetAutoUpdateMode());
                }

                return true;
            }
        });
    }

    this->SetActions(
        std::make_pair(Button::START, Action{"Options"_i18n, [this](){
            if (m_current_menu) {
                m_current_menu->FireAction(Button::START);
            }
        }}),
        std::make_pair(Button::SELECT, Action{App::HandleMinus})
    );

    m_centre_menu = std::make_unique<homebrew::Menu>();
    m_tools_menu = std::make_unique<tools::Menu>();
    m_current_menu = m_centre_menu.get();

    UpdateBackAction();
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

    if (!m_launch_link_prompt_checked) {
        m_launch_link_prompt_checked = true;
        if (!users::OfferPendingRestore()) {
            CheckLaunchAccountLinkPrompt();
        }
    }
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
    UpdateBackAction();
    AddOnLRPress();
    m_current_menu->OnFocusGained();

    for (auto [button, action] : m_actions) {
        if (button != Button::START) {
            m_current_menu->SetAction(button, action);
        }
    }
}

void MainMenu::UpdateBackAction() {
    if (m_current_menu == m_tools_menu.get()) {
        SetAction(Button::B, Action{"Back"_i18n, [this]{
            SwitchTo(m_centre_menu.get());
        }});
    } else {
        SetAction(Button::B, Action{"Exit"_i18n, App::Exit});
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

auto MainMenu::IsMainScreen() const -> bool {
    return m_current_menu == m_centre_menu.get();
}

void MainMenu::OpenMainScreen() {
    SwitchTo(m_centre_menu.get());
}

} // namespace sphaira::ui::menu::main
