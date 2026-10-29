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
    l.pct = 80;
    assert(!Advance(st, l));                       // discharged, not full yet
    l.charger = true;
    l.pct = 99;
    assert(!Advance(st, l));                       // not full
    l.pct = 100;
    assert(Advance(st, l) && st.full1_at == 2000);
    assert(!Advance(st, l));                       // the same full does not count twice
    l.total = 37196 + 185;
    l.now = 3000;
    assert(Advance(st, l) && st.full2_at == 3000);

    // 100 % with the charger already pulled still counts
    State un{1000, EMUMMC, 37196, 0, 0, {0, 0}};
    Live out{37196 + 98, false, 100, EMUMMC, true, 2500};
    assert(Advance(un, out) && un.full1_at == 2500);

    // the other MMC must match after the second cycle
    v = Steps(st, l);
    assert(MarkOf(v, Step::Cycle2) == Mark::Done && MarkOf(v, Step::MatchOther) == Mark::Next);
    State early = st;
    early.full2_at = 0;
    early.match_at[SYSMMC] = 100;                   // a match before the second cycle counted is voided when it counts
    Live fullnow = l;
    fullnow.pct = 100;
    fullnow.total = st.total_at_reset + 2 * CYCLE_DONE + 10;
    early.full1_at = 1;
    early.start[1] = st.total_at_reset + CYCLE_DONE;
    assert(Advance(early, fullnow) && early.full2_at && early.match_at[SYSMMC] == 0);
    st.match_at[SYSMMC] = 1;                        // the other MMC's clock may be far behind: any stamp counts
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
    d.drain_s[1] = 23400;
    assert(Parse(Format(d)).drain_s[1] == 23400 && Parse(Format(d)) == d);

    // a cut-off counts as a whole discharge when the drain began on a full battery, even if the cycle total lagged
    State k;
    k.reset_at = 1; k.total_at_reset = 1000;
    NoteDrain(k, 1, 100, -1);                                // the page had not read the battery yet
    NoteDrain(k, 1, 101, 100);
    assert(k.drain_from[0] == 100 && Parse(Format(k)).drain_from[0] == 100);
    assert(CutoffReached(k, 1, 1040) && Got(k, Live{1040}, 1) == CYCLE_DONE);   // the total showed only 40
    assert(!CutoffReached(k, 1, 1200));                      // the total already showed enough: nothing to fix
    State low;
    low.reset_at = 1; low.total_at_reset = 1000;
    NoteDrain(low, 1, 100, 60);                              // began at 60 %: a cut-off is not a whole discharge
    assert(!CutoffReached(low, 1, 1030) && !low.start[0]);

    // the drain page's phases: the night on .141 (drained, put on the charger, full in the morning)
    State c{1000, EMUMMC, 37196, 0, 0, {0, 0}};
    Live on{37196 + 40, false, 30, EMUMMC, false, 2000};
    assert(DrainStepOf(c, on, 1) == DrainStep::Draining);
    on.charger = true;
    assert(DrainStepOf(c, on, 1) == DrainStep::NotEmptyYet);  // plugged in too early
    on.total = 37196 + 98;
    on.pct = 94;
    assert(!Advance(c, on) && DrainStepOf(c, on, 1) == DrainStep::Charging);
    on.pct = 100;
    on.now = 3000;
    assert(Advance(c, on) && DrainStepOf(c, on, 1) == DrainStep::CycleCounted);
    assert(DrainStepOf(c, on, 2) == DrainStep::NotEmptyYet);   // the second cycle: unplug to start
    on.charger = false;
    assert(DrainStepOf(c, on, 2) == DrainStep::Draining);

    // "Do this cycle again": the .141 case, cycle 2 counted although it was not really drained
    State r{1000, EMUMMC, 37196, 1500, 2500, {0, 0}};
    r.start[1] = 37196 + 98;
    Live now{37196 + 190, false, 100, EMUMMC, true, 3000};
    assert(Got(r, now, 2) == 92);
    Redo(r, 2, now.total);
    assert(r.full2_at == 0 && r.full1_at == 1500 && r.start[1] == now.total);
    assert(Got(r, now, 2) == 0 && !Advance(r, now));          // 100 % at once does not count it again
    now.total += 91;
    assert(Advance(r, now) && r.full2_at == 3000);            // 91 more discharged, full: counted
    Redo(r, 1, now.total);                                    // redoing cycle 1 redoes cycle 2 with it
    assert(r.full1_at == 0 && r.full2_at == 0 && r.start[0] == now.total && r.start[1] == 0);
    assert(Parse(Format(r)) == r);

    std::puts("test_battery_wizard: ok");
}
