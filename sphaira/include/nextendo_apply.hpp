#pragma once

// Nextendo on the SD card: settings, state and the run of the steps from nextendo_plan.hpp.
// Pure rules live in nextendo_hosts.hpp / nextendo_plan.hpp (host tests); this part touches files and libnx.
// Required Notice: Copyright 2026 Nextendo Network (PolyForm Shield 1.0.0, assets/romfs/nextendo/LICENSE.md)

#include "nextendo_plan.hpp"

#include <switch.h>

#include <string>

namespace sphaira::nextendo {

// Settings kept in the Hub config (section "nextendo"). Emu default on, sysNAND default off.
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

// Make the card match the settings (see MakePlan). `changed` is set when a file or key was really written:
// the console must restart for dns_mitm and exosphere to read it. Cheap when nothing differs; no network.
auto Reconcile(bool& changed) -> Result;

} // namespace sphaira::nextendo
