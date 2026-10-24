// Host test for sphaira/include/battery_cutoff.hpp

#include "battery_cutoff.hpp"

#include <cassert>
#include <cstdio>

using namespace sphaira::battery::cutoff;

int main() {
    // the load plan: max first, min from 3400 mV, and it stays min when the voltage jumps back up
    LoadPlan plan;
    assert(NextLoad(plan, 3900, true) == Load::Max);
    assert(NextLoad(plan, 3401, true) == Load::Max);
    assert(NextLoad(plan, 3400, true) == Load::Min);
    assert(NextLoad(plan, 3650, true) == Load::Min); // the sag recovered: still latched
    LoadPlan off;
    assert(NextLoad(off, 3300, false) == Load::Max); // a skipped tail never lowers the load
    assert(TailWorthIt(std::nullopt) && TailWorthIt(50) && TailWorthIt(300) && !TailWorthIt(49));

    // the system id: sysMMC, a partition emuMMC by sector, a file-based one by folder
    assert(SystemId(false, false, 0, "") == "sys");
    assert(SystemId(true, true, 0x2F5000, "") == "emu-p-2f5000" && SystemId(true, true, 0, "") == "emu-p-0");
    assert(SystemId(true, false, 0, "emuMMC/SD01") == "emu-f-emuMMC/SD01");

    assert(SystemName("sys") == "sysMMC" && SystemName("emu-p-2f5000") == "emuMMC (sector 0x2f5000)");
    assert(SystemName("emu-f-emuMMC/SD01") == "emuMMC emuMMC/SD01" && SystemName("other") == "other");

    // the ledger: calibrated in emuMMC A; sysMMC and emuMMC B get it by themselves, once
    Ledger l;
    assert(Decide(l, "sys", "m2") == Decision::NoCalibration);
    RecordCalibration(l, "emu-p-a", "m2", true, 1000);
    assert(l.seq == 1 && WrittenSeq(l, "emu-p-a") == 1);
    assert(Decide(l, "emu-p-a", "m2") == Decision::UpToDate);    // the origin holds it
    assert(Decide(l, "sys", "m2") == Decision::Apply);
    assert(Decide(l, "emu-p-b", "m2") == Decision::Apply);
    assert(Decide(l, "sys", "m1") == Decision::DifferentModel);  // another battery
    RecordWritten(l, "sys", 1100);
    assert(Decide(l, "sys", "m2") == Decision::UpToDate && Decide(l, "emu-p-b", "m2") == Decision::Apply);

    // a calibration that did not pass is not spread; a newer one makes the older writes stale
    Ledger bad;
    RecordCalibration(bad, "emu-p-a", "m2", false, 5);
    assert(Decide(bad, "sys", "m2") == Decision::NotGood);
    RecordCalibration(l, "sys", "m2", true, 2000); // calibrated again, in sysMMC
    assert(l.seq == 2 && Decide(l, "sys", "m2") == Decision::UpToDate && Decide(l, "emu-p-a", "m2") == Decision::Apply);

    // the file: round trip, and a broken line is skipped
    const auto back = Parse(Format(l));
    assert(back.seq == 2 && back.origin == "sys" && back.good && back.written.size() == 1 && back.time == 2000);
    const auto hand = Parse("cal\t3\t9\temu-f-x\tm2\t1\r\nw\tsys\t3\t10\r\nw\tbad\r\nnonsense\r\n");
    assert(hand.seq == 3 && hand.written.size() == 1 && hand.written[0].system == "sys");

    std::puts("test_battery_cutoff: ok");
    return 0;
}
