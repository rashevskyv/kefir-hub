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
    // larger than the factory value
    v = Judge(9900, 9900, 9200);
    assert(v.wear == Wear::Larger && v.loss_pct < 0);
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

    std::puts("test_battery_report: ok");
    return 0;
}
