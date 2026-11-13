#pragma once

#include <functional>

// Nextendo account linking: a Nextendo account is not a Nintendo account, so a profile goes online on
// Nextendo only when System Settings links it to a Nextendo account. A Linkalho or donor link blocks that.
namespace sphaira::ui::nextendo_link {

// The steps with a QR code to the Nextendo sign-up page.
void PushGuide();

// Before Nextendo on emuMMC is switched on: the account steps, then a choice of linked profiles to unlink.
// switch_on is the plain toggle; it runs when no profile is linked.
void PushSwitchOn(const std::function<void()>& switch_on);

} // namespace sphaira::ui::nextendo_link
