#pragma once

// Internet speed test (Tools). The number formatting is free of libnx: tests/test_speed_test.cpp runs on the host.

#include <cstdint>
#include <cstdio>
#include <string>

namespace sphaira::speedtest {

// "6.2 MB/s (50 Mbit/s)" for `bytes` moved in `ms` milliseconds. "-" when nothing was measured.
inline std::string FormatRate(int64_t bytes, int64_t ms) {
    if (bytes <= 0 || ms <= 0) {
        return "-";
    }
    const double bps = static_cast<double>(bytes) * 1000.0 / static_cast<double>(ms);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.1f MB/s (%.0f Mbit/s)", bps / 1e6, bps * 8.0 / 1e6);
    return buf;
}

void Open(); // asks first, then runs the tests in a progress box

} // namespace sphaira::speedtest
