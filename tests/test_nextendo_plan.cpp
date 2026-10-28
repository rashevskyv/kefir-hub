// Host test for sphaira/include/nextendo_plan.hpp

#include "nextendo_plan.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>

using namespace sphaira::nextendo;

static bool Has(const std::vector<Step>& p, Step s) {
    return std::find(p.begin(), p.end(), s) != p.end();
}

int main() {
    // emuMMC, Nextendo on, supported firmware, nothing on the card: write hosts, real PRODINFO, dns keys
    Inputs in;
    in.mode = Mode::EmuMMC;
    in.firmware_ok = true;
    auto p = MakePlan(in);
    assert((p == std::vector<Step>{Step::WriteEmuHosts, Step::BlankProdinfoEmu0, Step::DnsMitmForNextendo}));

    // up to date: only the idempotent key steps remain (the executor writes a key only if it differs)
    in.emu_file = FileState::Current;
    p = MakePlan(in);
    assert(!Has(p, Step::WriteEmuHosts) && Has(p, Step::BlankProdinfoEmu0));

    // stale own file: rewritten, no backup
    in.emu_file = FileState::Stale;
    p = MakePlan(in);
    assert(Has(p, Step::WriteEmuHosts) && !Has(p, Step::BackupEmuUserHosts));

    // user file: backup first, then write; the backup step comes before the write
    in.emu_file = FileState::UserOwned;
    p = MakePlan(in);
    assert(p[0] == Step::BackupEmuUserHosts && p[1] == Step::WriteEmuHosts);

    // unsupported firmware in emuMMC: our file goes, PRODINFO is blanked again, dns defaults return
    in.emu_file = FileState::Current;
    in.firmware_ok = false;
    p = MakePlan(in);
    assert((p == std::vector<Step>{Step::RemoveEmuHosts, Step::RestoreEmuUserHosts, Step::BlankProdinfoEmu1, Step::DnsMitmDefaults}));

    // unsupported firmware and a user file: leave it alone, change nothing
    in.emu_file = FileState::UserOwned;
    assert(MakePlan(in).empty());
    in.emu_file = FileState::Missing;
    assert(MakePlan(in).empty());

    // user switched Nextendo off on supported firmware: same removal
    in.firmware_ok = true;
    in.emu_enabled = false;
    in.emu_file = FileState::Stale;
    assert(Has(MakePlan(in), Step::RemoveEmuHosts) && Has(MakePlan(in), Step::BlankProdinfoEmu1));

    // Semi-stock: the emuMMC side is not touched at all, whatever its files say
    Inputs ss;
    ss.mode = Mode::SemiStock;
    ss.firmware_ok = true;
    ss.emu_file = FileState::Current;
    ss.emu_enabled = false;
    assert(MakePlan(ss).empty()); // sysNAND option is off by default and no sysmmc.txt of ours exists

    // Semi-stock, option on: sysmmc.txt, real PRODINFO for sysNAND, never emuMMC steps
    ss.sys_enabled = true;
    p = MakePlan(ss);
    assert((p == std::vector<Step>{Step::WriteSysHosts, Step::BlankProdinfoSys0, Step::DnsMitmForNextendo}));
    assert(!Has(p, Step::BlankProdinfoEmu0) && !Has(p, Step::WriteEmuHosts));

    // Semi-stock, option off again with our file on the card: remove it, restore the user's backup,
    // and NEVER write blank_prodinfo_sysmmc=1 (Semi-stock owns that way back)
    ss.sys_enabled = false;
    ss.sys_file = FileState::Current;
    p = MakePlan(ss);
    assert((p == std::vector<Step>{Step::RemoveSysHosts, Step::RestoreSysUserHosts}));

    // Semi-stock on firmware outside the table: the option is switched off by force
    ss.sys_enabled = true;
    ss.firmware_ok = false;
    assert(Has(MakePlan(ss), Step::RemoveSysHosts) && !Has(MakePlan(ss), Step::WriteSysHosts));

    // back in emuMMC (Semi-stock was switched off) with our sysmmc.txt still on the card: it is removed
    Inputs back;
    back.mode = Mode::EmuMMC;
    back.firmware_ok = true;
    back.emu_file = FileState::Current;
    back.sys_file = FileState::Stale;
    p = MakePlan(back);
    assert(Has(p, Step::RemoveSysHosts) && Has(p, Step::RestoreSysUserHosts) && !Has(p, Step::WriteSysHosts));
    back.sys_file = FileState::UserOwned; // a user sysmmc.txt is not touched
    assert(!Has(MakePlan(back), Step::RemoveSysHosts));

    // patches follow "active": emuMMC on + stash from an earlier Semi-stock -> unstash
    Inputs pa;
    pa.mode = Mode::EmuMMC;
    pa.firmware_ok = true;
    pa.emu_file = FileState::Current;
    pa.stash_present = true;
    assert(Has(MakePlan(pa), Step::UnstashPatches) && !Has(MakePlan(pa), Step::StashPatches));
    // emuMMC with Nextendo off (or unsupported firmware): live patches go to the stash, hosts file untouched if not ours
    pa.stash_present = false;
    pa.patches_present = true;
    pa.emu_enabled = false;
    pa.emu_file = FileState::UserOwned;
    p = MakePlan(pa);
    assert((p == std::vector<Step>{Step::StashPatches}));
    pa.emu_enabled = true;
    pa.firmware_ok = false;
    assert(Has(MakePlan(pa), Step::StashPatches));
    // Semi-stock with the option off: stash; with the option on: unstash
    Inputs sp;
    sp.mode = Mode::SemiStock;
    sp.firmware_ok = true;
    sp.patches_present = true;
    assert((MakePlan(sp) == std::vector<Step>{Step::StashPatches}));
    sp.patches_present = false;
    sp.stash_present = true;
    sp.sys_enabled = true;
    p = MakePlan(sp);
    assert(Has(p, Step::UnstashPatches) && Has(p, Step::WriteSysHosts));
    // nothing live, nothing stashed: no stash steps
    sp.stash_present = false;
    assert(!Has(MakePlan(sp), Step::UnstashPatches) && !Has(MakePlan(sp), Step::StashPatches));

    // plain sysNAND: nothing, even with stale files lying around
    Inputs other;
    other.emu_file = FileState::Current;
    other.sys_file = FileState::Current;
    other.sys_enabled = true;
    assert(MakePlan(other).empty());

    std::puts("test_nextendo_plan: ok");
    return 0;
}
