#include "hekate_clock.hpp"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>

using namespace sphaira::hekate_clock;

namespace {

int64_t Posix(int y, unsigned m, unsigned d, unsigned hh, unsigned mm, unsigned ss) {
    using namespace std::chrono;
    const sys_days date{year{y} / month{m} / day{d}};
    return date.time_since_epoch().count() * 86400LL + hh * 3600 + mm * 60 + ss;
}

// What Nyx shows: max77620_rtc_get_time_adjusted() with the parsed offset.
int64_t Shown(const RtcDate& rtc, uint32_t stored, bool auto_dst) {
    const auto offset = static_cast<int>(std::strtoul(Hex(stored).c_str(), nullptr, 16));
    if (offset == 1) {
        return RtcEpoch(rtc);
    }
    int64_t e = static_cast<uint32_t>(static_cast<int64_t>(RtcEpoch(rtc)) + offset);
    if (auto_dst && InDstWindow(rtc)) {
        e += 3600;
    }
    return e;
}

} // namespace

int main() {
    // hekate's date_to_epoch matches the real calendar, leap days and century rules included.
    for (const RtcDate t : {RtcDate{2000, 1, 1, 0, 0, 0}, RtcDate{2000, 2, 29, 23, 59, 59},
                            RtcDate{2019, 3, 1, 12, 0, 0}, RtcDate{2024, 12, 31, 7, 8, 9},
                            RtcDate{2026, 11, 12, 15, 30, 1}, RtcDate{2099, 6, 15, 1, 2, 3}}) {
        assert(RtcEpoch(t) == Posix(t.year, t.month, t.day, t.hour, t.min, t.sec));
    }

    // registers 0x07..0x0D: sec, min, hour, weekday, month, year, day.
    const uint8_t regs[7] = {0x05, 0x2A, 0x11, 0x08, 0x07, 0x13, 0x1E};
    const auto rtc = DecodeRtc(regs, 0x02 | 0x01);
    assert(rtc && rtc->year == 2019 && rtc->month == 7 && rtc->day == 30);
    assert(rtc->hour == 17 && rtc->min == 42 && rtc->sec == 5);
    // 12-hour mode: hekate drops the PM bit, so 0x51 (PM, 11) reads as 1 (0x51 & 0x1F & 0xF).
    const uint8_t pm[7] = {0, 0, 0x51, 1, 1, 0x1A, 1};
    assert(DecodeRtc(pm, 0x01)->hour == 1);
    const uint8_t bad_month[7] = {0, 0, 0, 1, 0, 0x1A, 1};
    assert(!DecodeRtc(bad_month, 0x03));
    const uint8_t bad_day[7] = {0, 0, 0, 1, 1, 0x1A, 0};
    assert(!DecodeRtc(bad_day, 0x03));

    // rtc behind the real time, winter, kyiv (+2 h): Nyx shows local time.
    const RtcDate winter{2019, 1, 10, 3, 0, 0};
    const int64_t utc = Posix(2026, 11, 12, 13, 0, 0);
    const int64_t kyiv = utc + 2 * 3600;
    for (const bool dst : {false, true}) {
        assert(Shown(winter, Offset(kyiv, winter, dst), dst) == kyiv);
    }

    // rtc in the dst window with timedst=1: the stored offset is one hour smaller,
    // hekate adds that hour back.
    const RtcDate summer{2019, 7, 30, 17, 42, 5};
    const int64_t kyiv_summer = Posix(2026, 7, 1, 9, 0, 0) + 3 * 3600;
    assert(Offset(kyiv_summer, summer, true) + 3600 == Offset(kyiv_summer, summer, false));
    assert(Shown(summer, Offset(kyiv_summer, summer, true), true) == kyiv_summer);
    assert(Shown(summer, Offset(kyiv_summer, summer, false), false) == kyiv_summer);

    // rtc ahead of the real time: negative offset is written as u32 hex, like hekate's itoa.
    const RtcDate ahead{2030, 1, 1, 0, 0, 0};
    const int64_t past = Posix(2026, 1, 1, 0, 0, 0);
    const auto neg = Offset(past, ahead, false);
    assert(static_cast<int32_t>(neg) < 0);
    assert(Hex(neg).size() == 8 && Hex(neg)[0] == 'f');
    assert(Shown(ahead, neg, false) == past);
    assert(Hex(0x1234ABCD) == "1234abcd");

    // 0 (never set) and 1 (cancelled) are markers and never written.
    const int64_t at_rtc = RtcEpoch(winter);
    assert(Offset(at_rtc, winter, false) == 1);
    assert(Offset(at_rtc + 1, winter, false) == 2);
    assert(Offset(at_rtc + 2, winter, false) == 2);

    // one second of jitter does not rewrite nyx.ini; two do, across the u32 wrap too.
    assert(!NeedsWrite(100, 101) && !NeedsWrite(101, 100) && !NeedsWrite(100, 100));
    assert(NeedsWrite(100, 102) && NeedsWrite(102, 100));
    assert(!NeedsWrite(0xFFFFFFFFu, 0) && NeedsWrite(0xFFFFFFFFu, 1));
    // a missing key or timeoffset=0 makes Nyx ask for the clock: always replace it.
    assert(NeedsWrite(0, 1) && NeedsWrite(0, 2));

    std::puts("hekate_clock: ok");
    return 0;
}
