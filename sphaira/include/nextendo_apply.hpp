#pragma once

// Nextendo on the SD card: settings, state and the run of the steps from nextendo_plan.hpp.
// Pure rules live in nextendo_hosts.hpp / nextendo_plan.hpp (host tests); this part touches files and libnx.
// Required Notice: Copyright 2026 Nextendo Network (PolyForm Shield 1.0.0, assets/romfs/nextendo/LICENSE.md)

#include "nextendo_plan.hpp"

#include <switch.h>

#include <optional>
#include <string>

namespace sphaira::nextendo {

// Settings kept in the Hub config (section "nextendo"). Both default off: Nextendo is used only after the user switches it on, and a stored choice is never overwritten.
auto EmuEnabled() -> bool;
void SetEmuEnabled(bool enabled);
auto SysEnabled() -> bool;
void SetSysEnabled(bool enabled);

auto CurrentMode() -> Mode;
// A file or key changed since the console started: dns_mitm and exosphere read them only at boot.
auto RestartNeeded() -> bool;
// This Hub carries an ssl patch for the build ID of the running ssl module (no firmware list to keep).
// True also when the ID cannot be read: then the games only fail to connect, Nintendo is never reached.
auto FirmwareOk() -> bool;
// Build ID of the running ssl module (40 hex digits), empty when it cannot be read. For the Nextendo page.
auto SslBuildIdText() -> std::string;

// The server IPs in use: the ones refreshed from the newest release, else the pinned snapshot.
auto ServerIpsInUse() -> ServerIps;
void StoreServerIps(const ServerIps& ips);
// Background check of the newest release (at most once a day, only while Nextendo is active, no call otherwise).
void RefreshServerIpsAsync();
// Nextendo is in use on this boot: switched on for the current mode and the firmware is patched.
auto Active() -> bool;

// Online player counts for the games list badge (main thread). Tick every frame while the list is open:
// it reads Active() and asks the server at most once a minute. Recheck when the list gets the focus back: it
// re-reads Active() (a setting changed meanwhile applies at once, no extra request otherwise).
// Players: nullopt when Nextendo is off or the game unsupported.
void OnlineCountsTick();
void OnlineCountsRecheck();
auto OnlinePlayers(u64 app_id) -> std::optional<int>;

// Make the card match the settings (see MakePlan). `changed` is set when a file or key was really written:
// the console must restart for dns_mitm and exosphere to read it. Cheap when nothing differs; no network.
auto Reconcile(bool& changed) -> Result;

} // namespace sphaira::nextendo
