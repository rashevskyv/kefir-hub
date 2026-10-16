// Host test for sphaira/include/battery_wizard.hpp

#include "battery_wizard.hpp"

#include <cassert>
#include <cstdio>

using namespace sphaira::battery::wizard;

namespace {

Mark MarkOf(const std::vector<View>& v, Step s) {
    for (const auto& x : v) {
        if (x.step == s) {
            return x.mark;
        }
    }
    return Mark::Later;
}

} // namespace

int main() {
    // round trip
    State s{1760600000, EMUMMC, 37196, 1760610000, 0, {0, 0}};
    assert(Parse(Format(s)) == s);
    assert(Parse("garbage\nreset_at=5\n") .reset_at == 5);

    // nothing done: check the other MMC first
    State none;
    Live l{37196, false, 60, EMUMMC, false, 100};
    auto v = Steps(none, l);
    assert(MarkOf(v, Step::CheckOther) == Mark::Next && MarkOf(v, Step::Reset) == Mark::Later);
    l.other_known = true;
    v = Steps(none, l);
    assert(MarkOf(v, Step::CheckOther) == Mark::Done && MarkOf(v, Step::Reset) == Mark::Next);

    // after the reset: the first cycle needs 0.90 discharged, then full on the charger
    State st{1000, EMUMMC, 37196, 0, 0, {0, 0}};
    l = {37196 + 50, true, 100, EMUMMC, true, 2000};
    assert(!Advance(st, l));                       // only 0.50 discharged
    assert(Steps(st, l)[2].mark == Mark::Next && Steps(st, l)[2].discharged == 50);
    l.total = 37196 + 95;
    l.charger = false;
    assert(!Advance(st, l));                       // discharged, not on the charger yet
    l.charger = true;
    l.pct = 99;
    assert(!Advance(st, l));                       // not full
    l.pct = 100;
    assert(Advance(st, l) && st.full1_at == 2000);
    assert(!Advance(st, l));                       // the same full does not count twice
    l.total = 37196 + 185;
    l.now = 3000;
    assert(Advance(st, l) && st.full2_at == 3000);

    // the other MMC must match after the second cycle
    v = Steps(st, l);
    assert(MarkOf(v, Step::Cycle2) == Mark::Done && MarkOf(v, Step::MatchOther) == Mark::Next);
    st.match_at[SYSMMC] = 2500;                     // a match before the cycles finished does not count
    assert(MarkOf(Steps(st, l), Step::MatchOther) == Mark::Next);
    st.match_at[SYSMMC] = 3500;
    v = Steps(st, l);
    assert(MarkOf(v, Step::MatchOther) == Mark::Done && MarkOf(v, Step::Finished) == Mark::Done);

    // the drain time survives a Hub restart; the time after 0 % runs up to the last time the page saw the console
    State d;
    assert(NoteDrain(d, 1, 100, 80) && d.drain_at[0] == 100);
    assert(!NoteDrain(d, 1, 160, 79) && d.drain_at[0] == 100 && d.alive_at[0] == 160); // only alive moved
    assert(AfterZero(d, 1) == 0);
    d = Parse(Format(d));                                   // the Hub restarts
    assert(d.drain_at[0] == 100 && d.alive_at[0] == 160);
    assert(!NoteDrain(d, 1, 200, -1) && d.drain_at[0] == 100); // -1: not read yet, not a 0 %
    assert(NoteDrain(d, 1, 900, 0) && d.zero_at[0] == 900);
    assert(!NoteDrain(d, 1, 1320, 0) && d.zero_at[0] == 900);   // the first 0 % stays
    assert(AfterZero(d, 1) == 420 && AfterZero(d, 2) == 0);
    assert(NoteDrain(d, 2, 5000, 90) && d.drain_at[1] == 5000 && d.drain_at[0] == 100);
    assert(Parse(Format(d)) == d);

    std::puts("test_battery_wizard: ok");
}
