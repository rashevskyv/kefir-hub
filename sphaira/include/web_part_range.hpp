#pragma once

// Pure checks for the multi-connection upload (PUT /upload?part=<id>&offset=N).
// Kept free of libnx so tests/test_web_part_range.cpp can run on the host.

#include <cstdint>

namespace sphaira {

constexpr int64_t WEB_PART_MAX = 4 * 1024 * 1024; // one part body is held in RAM

// A part must lie fully inside the preallocated file and stay under WEB_PART_MAX.
// Overlapping parts are allowed on purpose: a client retry rewrites the same bytes.
constexpr bool WebPartRangeOk(int64_t offset, int64_t len, int64_t total) {
    return offset >= 0 && len > 0 && len <= WEB_PART_MAX && total >= 0 && offset <= total && len <= total - offset;
}

} // namespace sphaira
