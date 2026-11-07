// Host test for sphaira/include/battery_report.hpp

#include "battery_report.hpp"

#include <cassert>
#include <cstdio>

using namespace sphaira::battery::report;

int main() {
    // the owner's console: 4300 of 4600 mAh is about 6 % below, ordinary wear
    auto v = Judge(8600, 8600, 9200);
    assert(v.wear == Wear::Normal && v.loss_pct == 6 && !v.inconsistent);
    // up to 5 % is like new
    assert(Judge(8740, 8740, 9200).wear == Wear::New);   // 5 %
    assert(Judge(9200, 9200, 9200).wear == Wear::New);   // 0 %
    assert(Judge(8600, 8600, 9200).wear == Wear::Normal);
    // the edges: 15 % normal, 16 % worn, 30 % worn, 31 % very worn
    assert(Judge(7820, 7820, 9200).wear == Wear::Normal); // 15 %
    assert(Judge(7728, 7728, 9200).wear == Wear::Worn);   // 16 %
    assert(Judge(6440, 6440, 9200).wear == Wear::Worn);   // 30 %
    assert(Judge(6348, 6348, 9200).wear == Wear::VeryWorn); // 31 %
    // above the factory value: a wrong gauge value, never a "larger battery"; the owner's 7066 mAh against 4667 (2026-11-05)
    v = Judge(9900, 9900, 9200);
    assert(v.wear == Wear::AboveFactory && v.loss_pct < 0);
    assert(Judge(14132, 14132, 9334).wear == Wear::AboveFactory);
    assert(Judge(9500, 9500, 9200).wear == Wear::New);    // 3 % more is still the same battery
    // the Switch Lite case of 2026-10-31: FullCap 2688 against a factory 3600 mAh (7200), nominal 6228 mAh (12456)
    v = Judge(5376, 12456, 7200);
    assert(v.wear == Wear::Worn && v.inconsistent);
    // the owner's own console after cycle 1: 4845 mAh learned, nominal 6411, design 4667
    v = Judge(9690, 12822, 9334);
    assert(v.inconsistent);
    // no design value: nothing to compare with
    assert(Judge(8600, 8600, 0).wear == Wear::Unknown);
    // the nominal missing: not called inconsistent
    assert(!Judge(8600, 0, 9200).inconsistent);

    // the capacity check: the QH charge from cycle 2's cut-off to full against the learned FullCap, less the cut-off's %
    assert(CheckCapacity(7300, 7600, 0) == Check::Pass);     // 3.9 % off
    assert(CheckCapacity(7300, 7600, 2) == Check::Pass);     // expected 7448: 2 % off
    assert(CheckCapacity(6500, 7600, 0) == Check::Fail);     // 14 % off: the gauge learned wrong
    assert(CheckCapacity(9000, 7600, 0) == Check::Fail);     // more came in than the gauge thinks it holds
    assert(CheckCapacity(6500, 7600, 15) == Check::Pass);    // a cut-off at 15 %: 6460 expected
    assert(CapacityDiffPct(7068, 7600, 0) == 7 && CheckCapacity(7068, 7600, 0) == Check::Pass); // the edge
    assert(CheckCapacity(0, 7600, 0) == Check::Unknown);     // nothing counted
    assert(CheckCapacity(7300, 1, 0) == Check::Unknown);     // the chip gave no FullCap at the full
    // the zero check: the gauge at cycle 2's cut-off
    assert(CheckZero(0) == Check::Pass && CheckZero(3) == Check::Pass);
    assert(CheckZero(4) == Check::Fail && CheckZero(91) == Check::Fail);
    assert(CheckZero(-1) == Check::Unknown);
    // good for the other systems: a worn battery measured right is good; a wrong gauge or a failed check is not
    const auto worn = Judge(6900, 6900, 9200);                // 25 % below
    assert(Good(worn, Check::Pass, Check::Pass));
    assert(Good(worn, Check::Unknown, Check::Unknown));
    assert(!Good(worn, Check::Fail, Check::Pass));
    assert(!Good(worn, Check::Pass, Check::Fail));
    assert(!Good(Judge(9900, 9900, 9200), Check::Pass, Check::Pass));
    assert(!Good(Judge(5376, 12456, 7200), Check::Pass, Check::Pass));

    std::puts("test_battery_report: ok");
    return 0;
}
