#pragma once

// Nextendo: what to change on the SD card so it matches the settings, as a list of steps (pure, no I/O).
// nextendo_apply.cpp runs the steps. Host test tests/test_nextendo_plan.cpp.
//
// Where the Hub runs decides what it may touch:
//  - EmuMMC:    manages emummc.txt, blank_prodinfo_emummc and the dns_mitm keys. It also removes a sysmmc.txt of ours:
//               the sysNAND option lives in Semi-stock only, so leaving Semi-stock switches it off.
//  - SemiStock: manages the optional sysmmc.txt and keeps the patches out of the way while it is off.
//               Semi-stock itself owns blank_prodinfo_sysmmc (it sets 0 when it is switched on and restores the
//               backup when it is switched off), so Nextendo never writes 1 there.
//  - Other:     plain sysNAND without Semi-stock. Nothing is changed.
// "Patches" = the game patch folders (nextendo_*) in exefs_patches and the browser certs in contents/0100000000000803.
// They disable certificate checks, so they must be live only while Nextendo is active. The Kefir build names them
// with the prefix nextendo_; Semi-stock (Ultrahand / Sphaira package) moves them to /config/semistock/nextendo.

#include "nextendo_hosts.hpp"

#include <vector>

namespace sphaira::nextendo {

enum class Mode { EmuMMC, SemiStock, Other };

enum class Step {
    BackupEmuUserHosts,    // copy the user's emummc.txt to switch/prelude_hosts_backup/ (keeps an older backup)
    WriteEmuHosts,
    RemoveEmuHosts,
    RestoreEmuUserHosts,   // put the backup back, only when emummc.txt is now absent
    BackupSysUserHosts,
    WriteSysHosts,
    RemoveSysHosts,
    RestoreSysUserHosts,
    BlankProdinfoEmu0,     // exosphere.ini blank_prodinfo_emummc=0: Nextendo needs the real PRODINFO
    BlankProdinfoEmu1,
    BlankProdinfoSys0,     // only ever 0; Semi-stock owns the way back
    DnsMitmForNextendo,    // system_settings.ini: enable_dns_mitm=1, add_defaults_to_dns_hosts=0 (as Prelude does)
    DnsMitmDefaults,       // add_defaults_to_dns_hosts=1 (the Atmosphere default)
    StashPatches,          // game patches + browser certs of Nextendo -> /config/semistock/nextendo (inactive)
    UnstashPatches,        // and back (active)
};

struct Inputs {
    Mode mode = Mode::Other;
    bool firmware_ok = false;
    bool emu_enabled = true;    // user setting, default on
    bool sys_enabled = false;   // user setting, default off, only meaningful in Semi-stock
    FileState emu_file = FileState::Missing;
    FileState sys_file = FileState::Missing;
    bool patches_present = false; // nextendo_* folders in exefs_patches or contents/0100000000000803 are live
    bool stash_present = false;   // the same, moved out by Semi-stock or by an earlier stash
};

inline auto MakePlan(const Inputs& in) -> std::vector<Step> {
    std::vector<Step> out;
    if (in.mode == Mode::EmuMMC) {
        if (in.emu_enabled && in.firmware_ok) {
            if (PlanApply(in.emu_file) == ApplyAction::BackupThenWrite) {
                out.push_back(Step::BackupEmuUserHosts);
            }
            if (PlanApply(in.emu_file) != ApplyAction::Skip) {
                out.push_back(Step::WriteEmuHosts);
            }
            out.push_back(Step::BlankProdinfoEmu0);
            out.push_back(Step::DnsMitmForNextendo);
            if (in.stash_present) {
                out.push_back(Step::UnstashPatches);
            }
        } else {
            if (PlanRemove(in.emu_file) == RemoveAction::Delete) {
                out.push_back(Step::RemoveEmuHosts);
                out.push_back(Step::RestoreEmuUserHosts);
                out.push_back(Step::BlankProdinfoEmu1);
                out.push_back(Step::DnsMitmDefaults);
            }
            if (in.patches_present) {
                out.push_back(Step::StashPatches); // cert checks must not stay off while Nextendo is off
            }
        }
        if (PlanRemove(in.sys_file) == RemoveAction::Delete) {
            out.push_back(Step::RemoveSysHosts);
            out.push_back(Step::RestoreSysUserHosts);
        }
    } else if (in.mode == Mode::SemiStock) {
        if (in.sys_enabled && in.firmware_ok) {
            if (PlanApply(in.sys_file) == ApplyAction::BackupThenWrite) {
                out.push_back(Step::BackupSysUserHosts);
            }
            if (PlanApply(in.sys_file) != ApplyAction::Skip) {
                out.push_back(Step::WriteSysHosts);
            }
            out.push_back(Step::BlankProdinfoSys0);
            out.push_back(Step::DnsMitmForNextendo);
            if (in.stash_present) {
                out.push_back(Step::UnstashPatches);
            }
        } else {
            if (PlanRemove(in.sys_file) == RemoveAction::Delete) {
                out.push_back(Step::RemoveSysHosts);
                out.push_back(Step::RestoreSysUserHosts);
            }
            if (in.patches_present) {
                out.push_back(Step::StashPatches);
            }
        }
    }
    return out;
}

} // namespace sphaira::nextendo
