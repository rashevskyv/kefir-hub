// Nextendo account box. The account itself is entered on the console's own screen, never in Kefir Hub.

#include "ui/nextendo_link_box.hpp"

#include "ui/option_box.hpp"

#include "account/account_link.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "nextendo_accounts.hpp"
#include "nextendo_apply.hpp"
#include "option.hpp"
#include "ui/popup_list.hpp"
#include "ui/menus/settings/settings_translations.hpp"
#include "web.hpp"

#include <switch/applets/psel.h>

#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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

// Profiles the Hub signed in to Nextendo (the console does not say which service a link belongs to).
option::OptionString g_signed_in{"nextendo", "signed_in_uids", ""};

auto MarkText(nx::AccountMark m) -> std::string {
    switch (m) {
        case nx::AccountMark::NotLinked: return "Not linked: can sign in"_i18n;
        case nx::AccountMark::Nextendo: return "Linked to Nextendo"_i18n;
        case nx::AccountMark::OtherLink: return "Linked to another account: unlink it first"_i18n;
        case nx::AccountMark::NextendoUnread: return "Nextendo (link status cannot be read now)"_i18n;
        default: return "Link status cannot be read"_i18n;
    }
}

// The console's own selector shows no link state. This list shows it, so a profile that is linked to Nextendo
// (or to something else) is seen before the sign-in starts.
void PickProfile(void (*then)(const AccountUid&)) {
    auto record = nx::ParseRecord(g_signed_in.Get());
    const auto users = account_link::ListUsers();
    if (users.empty()) {
        return;
    }

    auto marks = std::make_shared<std::vector<nx::AccountMark>>();
    PopupList::Items names;
    std::vector<std::string> notes;
    for (const auto& u : users) {
        auto m = nx::MarkFor(u.linked_known, u.horizon_linked, nx::IsRecorded(record, u.uid_hex));
        if (u.linked_known && !u.horizon_linked) {
            record = nx::Forget(std::move(record), u.uid_hex); // unlinked meanwhile
        }
        marks->push_back(m);
        names.push_back(u.nickname);
        notes.push_back(MarkText(m));
    }
    g_signed_in.Set(nx::FormatRecord(record));

    auto list = std::make_unique<PopupList>(
        "Nextendo account"_i18n, names,
        [users, marks, then](std::optional<s64> i) {
            if (!i || *i < 0 || *i >= static_cast<s64>(users.size())) {
                return;
            }
            const auto m = (*marks)[*i];
            if (nx::CanSignIn(m)) {
                then(users[*i].uid);
            } else {
                App::Push<OptionBox>(MarkText(m) + ".", "OK"_i18n);
            }
        });
    list->SetMenuStyle(true).SetDescriptions(std::move(notes));
    App::Push(std::move(list));
}

void ConsoleLink(const AccountUid& uid) {
    // ponytail: EnsureNetworkServiceAccountAvailable is what games call for an unlinked profile;
    // not checked on a console yet. If it fails, the box gives the System Settings steps.
    PselUiSettings ui{};
    auto rc = pselUiCreate(&ui, PselUiMode_EnsureNetworkServiceAccountAvailable);
    if (R_SUCCEEDED(rc)) {
        ui.settings.invalid_uid_list[0] = uid;
        AccountUid out{};
        rc = pselUiShow(&ui, &out);
    }
    App::ResetTouchAfterApplet();
    if (R_FAILED(rc)) {
        if (!IsCancel(rc)) {
            PushManualSteps(rc);
        }
        return;
    }
    // remembered so that the account list can mark the profile as Nextendo
    g_signed_in.Set(nx::FormatRecord(nx::Remember(nx::ParseRecord(g_signed_in.Get()), account_link::UidHex(uid))));
}

void SignIn() {
    // without Nextendo loaded on this boot, the link screen would go to Nintendo
    if (!nx::Active() || nx::RestartNeeded()) {
        SwitchOnFirst();
        return;
    }
    PickProfile(ConsoleLink);
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
