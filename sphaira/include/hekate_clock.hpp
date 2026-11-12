#pragma once

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

// hekate's gui (Nyx) has no clock of its own: it shows the raw max77620 rtc plus an
// offset kept on the sd card, sdmc:/bootloader/nyx.ini [config] timeoffset=<hex>.
//   - read:  nyx/nyx_gui/nyx.c:233   strtol(val, 16), applied unless it is 1
//   - shown: bdk/rtc/max77620-rtc.c:165  rtc epoch + offset, +1 hour when timedst=1
//            and the RAW rtc date is in [28 March, 28 October)
//   - saved: nyx/nyx_gui/config.c:216  itoa(u32, 16), so a negative offset is u32 hex
//   - set:   nyx/nyx_gui/frontend/gui_options.c:955  the user types local wall time
// 0 means "never set" (Nyx asks for the clock on boot), 1 means "user cancelled".
// Nothing here knows about timezones: Nyx shows whatever wall time the offset makes.
namespace sphaira::hekate_clock {

inline constexpr const char* NYX_INI = "/bootloader/nyx.ini";

struct RtcDate {
    uint32_t year, month, day, hour, min, sec;
};

// regs = rtc registers 0x07..0x0D (sec, min, hour, weekday, month, year, day),
// control = register 0x03. Decoded exactly like hekate's max77620_rtc_get_time(),
// including its 12-hour quirk: it masks the PM bit away before it tests it.
inline std::optional<RtcDate> DecodeRtc(const uint8_t regs[7], uint8_t control) {
    RtcDate d{};
    d.sec = regs[0] & 0x7F;
    d.min = regs[1] & 0x7F;
    d.hour = regs[2] & 0x1F;
    if (!(control & 0x02)) {
        d.hour &= 0xF;
    }
    d.month = regs[4] & 0xF;
    d.year = (regs[5] & 0x7F) + 2000u;
    d.day = regs[6] & 0x1F;
    if (d.sec > 59 || d.min > 59 || d.hour > 23 || d.month < 1 || d.month > 12 || d.day < 1) {
        return std::nullopt;
    }
    return d;
}

// hekate's max77620_rtc_date_to_epoch(), same u32 arithmetic.
inline uint32_t RtcEpoch(const RtcDate& t) {
    uint32_t year = t.year;
    uint32_t month = t.month;
    if (month < 3) {
        month += 12;
        year--;
    }
    uint32_t epoch = (365 * year) + (year >> 2) - (year / 100) + (year / 400);
    epoch += (30 * month) + (3 * (month + 1) / 5) + t.day;
    epoch -= 719561;
    epoch *= 86400;
    epoch += (3600 * t.hour) + (60 * t.min) + t.sec;
    return epoch;
}

// The window in which hekate's auto dst adds an hour, judged on the raw rtc date.
inline bool InDstWindow(const RtcDate& rtc) {
    const uint32_t md = (rtc.month << 8) | rtc.day;
    return md >= 0x31C && md < 0xA1C;
}

// The timeoffset that makes Nyx show local_now (utc + the console's timezone offset,
// dst already included) while the rtc reads rtc.
inline uint32_t Offset(int64_t local_now, const RtcDate& rtc, bool auto_dst) {
    int64_t off = local_now - static_cast<int64_t>(RtcEpoch(rtc));
    if (auto_dst && InDstWindow(rtc)) {
        off -= 3600; // hekate stores the non-dst offset and adds the hour back itself
    }
    auto v = static_cast<uint32_t>(off);
    if (v == 0 || v == 1) {
        v++; // 0 and 1 are markers, not offsets; one second off is invisible
    }
    return v;
}

// Skip the sd write when Nyx would show the same time within a second.
// A stored 0 is "never set" (Nyx asks on boot), so it is always replaced.
inline bool NeedsWrite(uint32_t stored, uint32_t wanted) {
    if (stored == 0) {
        return true;
    }
    const auto diff = static_cast<int32_t>(wanted - stored);
    return diff >= 2 || diff <= -2;
}

// The form hekate itself writes: lowercase hex of the u32, no prefix.
inline std::string Hex(uint32_t v) {
    char buf[12];
    std::snprintf(buf, sizeof(buf), "%x", static_cast<unsigned>(v));
    return buf;
}

} // namespace sphaira::hekate_clock
