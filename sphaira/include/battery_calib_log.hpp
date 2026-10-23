#pragma once

// The per-second log of a calibration drain or charge (/config/kefir/battery/calib-log.csv): one line a second, so that
// after a cut-off the Hub still knows what the gauge believed in the last second (the value X of the zero).
// No libnx: host-tested by tests/test_battery_calib_log.cpp.

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace sphaira::battery::calib {

inline constexpr const char* LOG_HEADER = "time,kind,psm_pct,gauge_pct,rem_mah,full_mah,mv,ma,temp_c,load,brightness,charger\n";

struct Sample {
    std::int64_t time{};   // unix time
    char kind{'D'};        // 'D' drain, 'C' charge
    int psm_pct{-1};       // what the console shows
    int gauge_pct{-1};     // the gauge's own RepSOC
    int rem_mah{-1};       // the gauge's remaining charge
    int full_mah{-1};      // the gauge's full capacity
    int mv{};
    int ma{};
    double temp_c{};
    bool load{};           // the Hub's CPU / GPU load is on
    int brightness{-1};    // percent the Hub set, -1 not its own
    bool charger{};
};

inline auto FormatLine(const Sample& s) -> std::string {
    char buf[192];
    std::snprintf(buf, sizeof(buf), "%lld,%c,%d,%d,%d,%d,%d,%d,%.1f,%d,%d,%d\n", static_cast<long long>(s.time), s.kind, s.psm_pct,
        s.gauge_pct, s.rem_mah, s.full_mah, s.mv, s.ma, s.temp_c, s.load ? 1 : 0, s.brightness, s.charger ? 1 : 0);
    return buf;
}

// one line back to a sample; nullopt for the header or a broken (half-written) line
inline auto ParseLine(std::string_view line) -> std::optional<Sample> {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.remove_suffix(1);
    }
    long long t = 0;
    char kind = 0;
    Sample s;
    int load = 0, charger = 0;
    const std::string copy{line};
    const int n = std::sscanf(copy.c_str(), "%lld,%c,%d,%d,%d,%d,%d,%d,%lf,%d,%d,%d", &t, &kind, &s.psm_pct, &s.gauge_pct, &s.rem_mah,
        &s.full_mah, &s.mv, &s.ma, &s.temp_c, &load, &s.brightness, &charger);
    if (n != 12 || (kind != 'D' && kind != 'C')) {
        return std::nullopt;
    }
    s.time = t;
    s.kind = kind;
    s.load = load != 0;
    s.charger = charger != 0;
    return s;
}

// the last whole sample of a text (the file's tail): a half-written last line is skipped
inline auto LastSample(std::string_view text) -> std::optional<Sample> {
    while (!text.empty()) {
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
            text.remove_suffix(1);
        }
        const auto nl = text.rfind('\n');
        const auto line = nl == std::string_view::npos ? text : text.substr(nl + 1);
        if (const auto s = ParseLine(line)) {
            return s;
        }
        if (nl == std::string_view::npos) {
            break;
        }
        text = text.substr(0, nl);
    }
    return std::nullopt;
}

// X: the gauge's remaining charge in the last drain sample, -1 when there is none (no log, or only charge lines).
// A broken last line (a cut-off in the middle of a write) is skipped.
inline auto LastDrainRemainingMah(std::string_view text) -> int {
    while (!text.empty()) {
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
            text.remove_suffix(1);
        }
        const auto nl = text.rfind('\n');
        const auto line = nl == std::string_view::npos ? text : text.substr(nl + 1);
        if (const auto s = ParseLine(line); s && s->kind == 'D' && s->rem_mah >= 0) {
            return s->rem_mah;
        }
        if (nl == std::string_view::npos) {
            break;
        }
        text = text.substr(0, nl);
    }
    return -1;
}

} // namespace sphaira::battery::calib
