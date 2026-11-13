// Nextendo account box. The account itself is entered on the console's own screen, never in Kefir Hub.

#include "ui/nextendo_link_box.hpp"

#include "ui/option_box.hpp"

#include "account/account_link.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "nextendo_apply.hpp"
#include "ui/menus/settings/settings_translations.hpp"
#include "web.hpp"

#include <switch/applets/psel.h>

#include <cstring>

namespace sphaira::ui::nextendo_link {
namespace {

namespace nx = ::sphaira::nextendo;

// the form itself walks through the account, the console link and the first game
constexpr const char* kSignUpUrl{"https://nextendo.network/register"};

auto IsCancel(Result rc) -> bool {
    return rc == AccountError_Cancelled || rc == AccountError_CancelledByUser;
}

void PushManualSteps(Result rc) {
    App::PushErrorBox(rc,
        "Kefir Hub could not open the sign-in. Sign in from System Settings → Users → the profile → Link Nintendo Account, with the Nextendo e-mail and password."_i18n);
}

// Nextendo is not loaded on this boot. On the emuMMC Kefir Hub switches it on and restarts; after the
// restart the startup link reminder leads back to Sign in. Semi-stock keeps its own warning on the Nextendo page.
void SwitchOnFirst() {
    if (!nx::FirmwareOk()) {
        App::Push<OptionBox>(
            "This Kefir Hub has no Nextendo patch for this system firmware yet, so Nextendo is off. Kefir Hub updates itself when Prelude-Nextendo adds one."_i18n,
            "OK"_i18n);
        return;
    }
    switch (nx::CurrentMode()) {
        case nx::Mode::EmuMMC:
            break;
        case nx::Mode::SemiStock:
            App::Push<OptionBox>(
                "Switch on Nextendo first: Kefir Settings → Nextendo. Sign in after the console restarts."_i18n,
                "OK"_i18n);
            return;
        default:
            App::Push<OptionBox>("Nextendo works on the emuMMC, or on the sysNAND while Semi-stock is on."_i18n, "OK"_i18n);
            return;
    }

    App::Push<OptionBox>(
        "Kefir Hub switches on Nextendo on the emuMMC and restarts the console. After the restart, select Sign in again."_i18n,
        "Cancel"_i18n, "Switch on"_i18n, 1,
        [](auto op) {
            if (!op || *op != 1) {
                return;
            }
            nx::SetEmuEnabled(true);
            bool changed{};
            if (const auto rc = nx::Reconcile(changed); R_FAILED(rc)) {
                App::PushErrorBox(rc, "Failed to apply Kefir setting"_i18n);
                return;
            }
            // also when nothing changed now: an earlier change still waits for the restart
            menu::settings::detail::RebootAfterSetting();
        });
}

void SignIn() {
    // without Nextendo loaded on this boot, the link screen would go to Nintendo
    if (!nx::Active() || nx::RestartNeeded()) {
        SwitchOnFirst();
        return;
    }

    AccountUid uid{};
    const PselUserSelectionSettings select{};
    auto rc = pselShowUserSelector(&uid, &select);
    App::ResetTouchAfterApplet();
    if (R_FAILED(rc) || !accountUidIsValid(&uid)) {
        if (R_FAILED(rc) && !IsCancel(rc)) {
            PushManualSteps(rc);
        }
        return;
    }

    // a Linkalho, donor or Nintendo link blocks a new link to Nextendo
    for (const auto& u : account_link::ListUsers()) {
        if (!std::memcmp(&u.uid, &uid, sizeof(uid)) && u.linked_known && u.horizon_linked) {
            App::Push<OptionBox>(
                "This profile is already linked to an account. Unlink it first: Tools → Users → Unlink Nintendo Account. Then sign in again."_i18n,
                "OK"_i18n);
            return;
        }
    }

    // ponytail: EnsureNetworkServiceAccountAvailable is what games call for an unlinked profile;
    // not checked on a console yet. If it fails, the box gives the System Settings steps.
    PselUiSettings ui{};
    rc = pselUiCreate(&ui, PselUiMode_EnsureNetworkServiceAccountAvailable);
    if (R_SUCCEEDED(rc)) {
        ui.settings.invalid_uid_list[0] = uid;
        AccountUid out{};
        rc = pselUiShow(&ui, &out);
    }
    App::ResetTouchAfterApplet();
    if (R_FAILED(rc) && !IsCancel(rc)) {
        PushManualSteps(rc);
    }
}

} // namespace

void PushGuide() {
    App::Push<OptionBox>(
        "Online play on Nextendo needs a free Nextendo account.\n"
        "No account yet: scan the code and sign up on the phone.\n"
        "Then select Sign in and choose the profile. Kefir Hub switches on Nextendo if needed; you only enter the Nextendo e-mail and password."_i18n,
        "Later"_i18n, "Sign in"_i18n, 1,
        [](auto op) {
            if (op && *op == 1) {
                SignIn();
            }
        }, CreateQrImage(kSignUpUrl), true);
}

} // namespace sphaira::ui::nextendo_link
