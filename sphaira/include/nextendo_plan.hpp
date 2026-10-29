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
// "Patches" = the nextendo_* folders in exefs_patches and nro_patches and the browser certs in
// contents/0100000000000803. The Hub carries them in its romfs (nextendo_patches.hpp) and writes them itself.
// They disable certificate checks, so they must be live only while Nextendo is active. Semi-stock (Ultrahand / Sphaira
// package) moves them to /config/semistock/nextendo; the Hub never needs that copy (it has its own) and drops it.

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
    InstallPatches,        // write the patch files from the Hub romfs; nextendo_* folders not in the list go (active)
    RemovePatches,         // delete them (inactive)
    DropStash,             // delete /config/semistock/nextendo: the Hub writes fresh copies (active)
};

struct Inputs {
    Mode mode = Mode::Other;
    bool firmware_ok = false;
    bool emu_enabled = true;    // user setting, default on
    bool sys_enabled = false;   // user setting, default off, only meaningful in Semi-stock
    FileState emu_file = FileState::Missing;
    FileState sys_file = FileState::Missing;
    bool patches_present = false; // any of our patch files or nextendo_* folders is live
    bool patches_current = false; // live and exactly the files of this Hub (tag, list, sizes)
    bool stash_present = false;   // Semi-stock moved them to /config/semistock/nextendo
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
                out.push_back(Step::DropStash);
            }
            if (!in.patches_current) {
                out.push_back(Step::InstallPatches);
            }
        } else {
            if (PlanRemove(in.emu_file) == RemoveAction::Delete) {
                out.push_back(Step::RemoveEmuHosts);
                out.push_back(Step::RestoreEmuUserHosts);
                out.push_back(Step::BlankProdinfoEmu1);
                out.push_back(Step::DnsMitmDefaults);
            }
            if (in.patches_present) {
                out.push_back(Step::RemovePatches); // cert checks must not stay off while Nextendo is off
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
                out.push_back(Step::DropStash);
            }
            if (!in.patches_current) {
                out.push_back(Step::InstallPatches);
            }
        } else {
            if (PlanRemove(in.sys_file) == RemoveAction::Delete) {
                out.push_back(Step::RemoveSysHosts);
                out.push_back(Step::RestoreSysUserHosts);
            }
            if (in.patches_present) {
                out.push_back(Step::RemovePatches);
            }
        }
    }
    return out;
}

} // namespace sphaira::nextendo
