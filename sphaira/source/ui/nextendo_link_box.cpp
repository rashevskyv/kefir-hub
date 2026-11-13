// Nextendo account box. The account itself is entered on the console's own screen, never in Kefir Hub.

#include "ui/nextendo_link_box.hpp"

#include "ui/option_box.hpp"

#include "account/account_link.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "nextendo_apply.hpp"
#include "web.hpp"

#include <switch/applets/psel.h>

#include <cstring>

namespace sphaira::ui::nextendo_link {
namespace {

constexpr const char* kSignUpUrl{"https://wiki.nextendo.network/setup/create-account"};

auto IsCancel(Result rc) -> bool {
    return rc == AccountError_Cancelled || rc == AccountError_CancelledByUser;
}

void PushManualSteps(Result rc) {
    App::PushErrorBox(rc,
        "Kefir Hub could not open the sign-in. Sign in from System Settings → Users → the profile → Link Nintendo Account, with the Nextendo e-mail and password."_i18n);
}

void SignIn() {
    // without Nextendo loaded on this boot, the link screen would go to Nintendo
    if (!::sphaira::nextendo::Active() || ::sphaira::nextendo::RestartNeeded()) {
        App::Push<OptionBox>(
            "Switch on Nextendo first: Kefir Settings → Nextendo. Sign in after the console restarts."_i18n,
            "OK"_i18n);
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
        "No account yet: scan the code for the sign-up steps.\n"
        "Have an account: switch on Nextendo in Kefir Settings → Nextendo, then select Sign in and choose the profile."_i18n,
        "Later"_i18n, "Sign in"_i18n, 1,
        [](auto op) {
            if (op && *op == 1) {
                SignIn();
            }
        }, CreateQrImage(kSignUpUrl), true);
}

} // namespace sphaira::ui::nextendo_link
