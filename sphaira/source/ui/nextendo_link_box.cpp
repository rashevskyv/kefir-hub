// Nextendo account linking boxes. The account itself is entered in System Settings, never in Kefir Hub.

#include "ui/nextendo_link_box.hpp"

#include "ui/option_box.hpp"
#include "ui/popup_multi_select.hpp"
#include "ui/progress_box.hpp"

#include "account/account_link.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "nextendo_apply.hpp"
#include "utils/utils.hpp"
#include "web.hpp"

#include <memory>
#include <string>
#include <vector>

namespace sphaira::ui::nextendo_link {
namespace {

constexpr const char* kSignUpUrl{"https://nextendo.network/register"};

void SwitchOnAndUnlink(std::vector<AccountUid> uids) {
    std::string gate_reason;
    if (!uids.empty() && account_link::IsLinkGated(gate_reason)) {
        App::Push<OptionBox>(gate_reason, "OK"_i18n);
        return;
    }

    App::Push<ProgressBox>(0, "Enabling"_i18n, "Nextendo on emuMMC"_i18n,
        [uids = std::move(uids)](auto pbox) -> Result {
            ::sphaira::nextendo::SetEmuEnabled(true);
            bool changed{};
            R_TRY(::sphaira::nextendo::Reconcile(changed));
            if (!uids.empty()) {
                pbox->NewTransfer("Removing Nintendo Account link"_i18n);
                u32 count{};
                R_TRY(account_link::UnlinkLinkedProfiles(uids, count));
            }
            R_SUCCEED();
        },
        [](Result rc) {
            // a failed unlink after the account services stopped still needs the restart
            if (R_FAILED(rc) && !account_link::ConsumeAccountDaemonsTerminated()) {
                App::PushErrorBox(rc, "Failed to apply Kefir setting"_i18n);
                return;
            }
            fsdevCommitDevice("sdmc");
            utils::requestForcedReboot();
        }, 1, PRIO_PREEMPTIVE, 1024 * 128, false);
}

void PushProfilePicker(std::vector<account_link::User> linked) {
    auto users = std::make_shared<std::vector<account_link::User>>(std::move(linked));
    auto ticks = std::make_shared<std::vector<u8>>(users->size(), 0);

    // the confirm waits under the list and shows when the list closes
    App::Push<OptionBox>(
        "Switch on Nextendo now? Kefir Hub unlinks the ticked profiles. Their game saves stay. The console restarts."_i18n,
        "Cancel"_i18n, "Switch on"_i18n, 1,
        [users, ticks](auto op) {
            if (!op || *op != 1) {
                return;
            }
            std::vector<AccountUid> uids;
            for (size_t i = 0; i < users->size(); i++) {
                if ((*ticks)[i]) {
                    uids.push_back((*users)[i].uid);
                }
            }
            SwitchOnAndUnlink(std::move(uids));
        });

    PopupMultiSelect::Items items;
    for (const auto& u : *users) {
        items.emplace_back(PopupMultiSelect::Item{u.nickname, {}, false});
    }
    App::Push<PopupMultiSelect>("Profiles for Nextendo"_i18n, items, *ticks);
}

} // namespace

void PushGuide() {
    App::Push<OptionBox>(
        "Online play on Nextendo needs a free Nextendo account.\n"
        "1. Scan the code and create the account. If sign-ups are closed, the Nextendo Discord says when they open.\n"
        "2. Switch on Nextendo: Kefir Settings → Nextendo.\n"
        "3. After the restart: System Settings → Users → the profile → Link Nintendo Account. Sign in with the Nextendo account."_i18n,
        "OK"_i18n, [](auto) {}, CreateQrImage(kSignUpUrl), true);
}

void PushSwitchOn(const std::function<void()>& switch_on) {
    std::vector<account_link::User> linked;
    for (auto& u : account_link::ListUsers()) {
        if (u.linked_known && u.horizon_linked) {
            linked.push_back(std::move(u));
        }
    }

    if (linked.empty()) {
        App::Push<OptionBox>(
            "Online play on Nextendo needs a free Nextendo account: scan the code to create one.\n"
            "After the restart, link each profile that plays online: System Settings → Users → the profile → Link Nintendo Account. Sign in with the Nextendo account."_i18n,
            "Cancel"_i18n, "Continue"_i18n, 1,
            [switch_on](auto op) {
                if (op && *op == 1) {
                    switch_on();
                }
            }, CreateQrImage(kSignUpUrl), true);
        return;
    }

    App::Push<OptionBox>(
        "Online play on Nextendo needs a free Nextendo account: scan the code to create one.\n"
        "A profile linked with Linkalho or a Kefir Hub donor cannot play online on Nextendo. Choose the profiles that play online: Kefir Hub unlinks them and restarts.\n"
        "Then link each one: System Settings → Users → the profile → Link Nintendo Account. Sign in with the Nextendo account."_i18n,
        "Cancel"_i18n, "Choose profiles"_i18n, 1,
        [linked = std::move(linked)](auto op) mutable {
            if (op && *op == 1) {
                PushProfilePicker(std::move(linked));
            }
        }, CreateQrImage(kSignUpUrl), true);
}

} // namespace sphaira::ui::nextendo_link
