#include "ui/menus/main_menu.hpp"

#include "ui/menus/homebrew.hpp"
#include "ui/menus/tools_menu.hpp"
#include "ui/menus/kefir_menu.hpp"
#include "ui/first_run_hint.hpp"
#include "ui/menus/users_menu.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"

#include "account/account_link.hpp"
#include "app.hpp"
#include "auto_update.hpp"
#include "log.hpp"
#include "download.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "net.hpp"
#include "utils/utils.hpp"
#include "version_compare.hpp"

#include <yyjson.h>
#include <iomanip>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include "ui/nvg_util.hpp"

namespace sphaira::ui::menu::main {
namespace {

constexpr const char* GITHUB_URL{"https://api.github.com/repos/rashevskyv/kefir-hub/releases/latest"};
constexpr fs::FsPath CACHE_PATH{"/switch/sphaira/cache/sphaira_latest.json"};
constexpr long HTTP_NOT_FOUND{404};


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

// one look at the newest GitHub release; at the start, and again when the console gets a network after being offline
void MainMenu::CheckForUpdate() {
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
}

MainMenu::MainMenu() {
    // Launch account-link prompt is deferred to OnFocusGained: Push from this
    // constructor would emplace OptionBox before MainMenu is on the stack, so
    // MainMenu would cover the dialog and it would never receive input.

    CheckForUpdate();

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

    CopyActionsToCurrentMenu();
}

MainMenu::~MainMenu() {

}

void MainMenu::Update(Controller* controller, TouchInfo* touch) {
    // Update runs only while the Hub is on top, so a launch prompt shows first and the hint follows it.
    // the hint closed (Update runs again): the page slides a little to the left and back, so Tools shows behind it.
    if (m_peek_pending) {
        m_peek_pending = false;
        m_peek_frame = 0;
    } else if (m_peek_frame >= 0 && ++m_peek_frame > PEEK_FRAMES) {
        m_peek_frame = -1;
    }

    // Kefir's Safe Mode (hekate `safemode=1` → exosphere ConfigItem 65014): mods, modules, theme and translation are
    // off. Usually it is the way out of a Kefir that no longer boots after a firmware update, so say what to do now.
    if (!m_safe_mode_checked) {
        m_safe_mode_checked = true;
        u64 safe = 0;
        if (R_SUCCEEDED(splInitialize())) {
            if (R_FAILED(splGetConfig(static_cast<SplConfigItem>(65014), &safe))) {
                safe = 0; // an Atmosphère without the Kefir safe-mode item
            }
            splExit();
        }
        // hekate leaves this flag on a safe-mode boot for the Ultrahand notice, which deletes it; if Ultrahand did
        // not run, the flag must not show a stale notice on the next normal boot
        if (fs::FsNativeSd sd; sd.FileExists("/config/kefir/safemode_notice.flag")) {
            sd.DeleteFile("/config/kefir/safemode_notice.flag");
        }
        if (safe) {
            log_write("[SafeMode] Kefir safe mode is on\n");
            App::Push<OptionBox>(
                "Safe mode\n\nModules, mods, the theme and the translation are off.\n\n"
                "Update Kefir now. Until you do, the console may not start normally."_i18n,
                "Later"_i18n, "Open Updater"_i18n, 1,
                [](auto op_index) {
                    if (op_index && *op_index == 1) {
                        App::Push<menu::kefir::Menu>();
                    }
                });
            return;
        }
    }

    if (m_current_menu == m_centre_menu.get() && hint::TakeFirst(FirstRunHint::Main)) {
        App::Push<HintBox>("Press R to open Tools. Games, saves, Kefir and firmware updates, themes and Kefir settings are there."_i18n,
                           m_current_menu->GetUiButtons(), Button::R);
        m_peek_pending = true;
    }

    // a tap on the page dots flips the page, like R / L
    if (touch && touch->is_clicked && touch->in_range(PageDotsRect())) {
        SwitchTo(m_current_menu == m_centre_menu.get() ? m_tools_menu.get() : m_centre_menu.get());
    }

    // A sideways drag moves the page with the finger, like the next tab on a phone; on release it settles on the
    // page it is closer to (a fifth of the way is enough), and Draw eases it there. Mostly-vertical drags stay
    // list scrolls; once a drag is sideways, the page below gets no touch until the finger is up.
    TouchInfo no_touch{};
    TouchInfo* child_touch = touch;
    if (touch && touch->is_touching && !touch->is_pinch) {
        const float dx = float((s32)touch->cur.x - (s32)touch->initial.x);
        const float dy = float((s32)touch->cur.y - (s32)touch->initial.y);
        if (!m_dragging && std::abs(dx) > 30.f && std::abs(dx) > 2.f * std::abs(dy)) {
            m_dragging = true;
            m_drag_base = SlideRest();
        }
        if (m_dragging) {
            m_slide = std::clamp(m_drag_base - dx, 0.f, float(SCREEN_WIDTH));
            child_touch = &no_touch;
        }
    } else if (m_dragging) {
        m_dragging = false;
        child_touch = &no_touch; // the release ends the drag: not a tap or a fling in the list
        const bool to_tools = m_drag_base == 0.f ? m_slide > SCREEN_WIDTH * 0.2f : m_slide > SCREEN_WIDTH * 0.8f;
        auto* target = to_tools ? m_tools_menu.get() : m_centre_menu.get();
        if (target != m_current_menu) {
            SwitchTo(target);
        }
    }

    m_current_menu->Update(controller, child_touch);

    // offline at the start: the check failed. Look again when the network comes up (also after a later reconnect),
    // unless an update is already found, downloading or installed.
    const bool net_up = net::IsConnectedCached();
    if (net_up && !m_net_up && static_cast<auto_update::Mode>(App::GetAutoUpdateMode()) != auto_update::Mode::Off) {
        const auto state = auto_update::GetJob().state;
        if (state == auto_update::JobState::Failed || state == auto_update::JobState::Idle) {
            log_write("[UpdateCheck] network is up: checking again\n");
            CheckForUpdate();
        }
    }
    m_net_up = net_up;
}

void MainMenu::Draw(NVGcontext* vg, Theme* theme) {
    // m_slide: 0 = the main screen, SCREEN_WIDTH = Tools. Away from a drag it eases to the current page, so R / L,
    // the dots and a released swipe all slide. Eased here, not in Update: Update stops while a hint is on top.
    if (!m_dragging) {
        const float rest = SlideRest();
        m_slide += (rest - m_slide) * 0.25f;
        if (std::abs(rest - m_slide) < 0.5f) {
            m_slide = rest;
        }
    }
    const float peek = m_peek_frame >= 0 && m_current_menu == m_centre_menu.get() && !m_dragging
        ? PEEK_DIST * std::sin(3.14159265f * m_peek_frame / PEEK_FRAMES) : 0.f;
    const float p = m_slide + peek;

    if (p < 0.5f) {
        m_centre_menu->Draw(vg, theme);
    } else if (p > SCREEN_WIDTH - 0.5f) {
        m_tools_menu->Draw(vg, theme);
    } else {
        nvgSave(vg);
        nvgTranslate(vg, -p, 0.f);
        m_centre_menu->Draw(vg, theme);
        nvgRestore(vg);
        nvgSave(vg);
        nvgTranslate(vg, SCREEN_WIDTH - p, 0.f);
        m_tools_menu->Draw(vg, theme);
        nvgRestore(vg);
    }

    // page dots, like a phone home screen: the main screen and Tools. A pill keeps them readable over tiles.
    const auto r = PageDotsRect();
    gfx::drawRect(vg, r, nvgRGBA(0, 0, 0, 110), r.h / 2.f);
    const bool on_tools = p > SCREEN_WIDTH / 2.f;
    for (int i = 0; i < 2; i++) {
        const float cx = r.x + r.w / 2.f + (i ? 12.f : -12.f);
        nvgBeginPath(vg);
        nvgCircle(vg, cx, r.y + r.h / 2.f, 5.f);
        if ((i == 1) == on_tools) {
            nvgFillColor(vg, theme->GetColour(ThemeEntryID_TEXT));
            nvgFill(vg);
        } else {
            nvgStrokeColor(vg, theme->GetColour(ThemeEntryID_TEXT));
            nvgStrokeWidth(vg, 1.5f);
            nvgStroke(vg);
        }
    }
}

auto MainMenu::SlideRest() const -> float {
    return m_current_menu == m_tools_menu.get() ? float(SCREEN_WIDTH) : 0.f;
}

auto MainMenu::PageDotsRect() -> Vec4 {
    return Vec4{SCREEN_WIDTH / 2.f - 30.f, 612.f, 60.f, 22.f};
}

void MainMenu::OnFocusGained() {
    Widget::OnFocusGained();
    m_current_menu->OnFocusGained();

    if (!m_launch_link_prompt_checked && !App::NeedsLanguageSelection()) {
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

    CopyActionsToCurrentMenu();
}

// The main screen keeps its own B: it clears a selection made with X / Y and only then exits.
// Copying "Exit" over it closed Kefir Hub with titles still selected.
void MainMenu::CopyActionsToCurrentMenu() {
    for (auto [button, action] : m_actions) {
        if (button == Button::START) {
            continue;
        }
        if (button == Button::B && m_current_menu == m_centre_menu.get()) {
            continue;
        }
        m_current_menu->SetAction(button, action);
    }
}

void MainMenu::UpdateBackAction() {
    if (m_current_menu == m_tools_menu.get()) {
        SetAction(Button::B, Action{"Back"_i18n, [this]{
            SwitchTo(m_centre_menu.get());
        }});
    } else {
        SetAction(Button::B, Action{"Exit"_i18n, App::ConfirmExit});
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
