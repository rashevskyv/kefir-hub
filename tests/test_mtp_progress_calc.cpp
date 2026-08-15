// Host test for MTP progress and transfer calculation logic
//
// Build and run command:
// g++ -std=c++20 -Wall -Wextra -Werror -I sphaira/include tests/test_mtp_progress_calc.cpp -o /tmp/test_mtp_progress_calc && /tmp/test_mtp_progress_calc

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>

static int g_checks = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(expr)) {                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);       \
            return 1;                                                         \
        }                                                                     \
    } while (0)

// Helper replicating the safe transfer accumulation in haze_callback
static int64_t CalcTransferredBytes(int64_t offset, int64_t chunk) {
    if (offset >= 0 && chunk >= 0 && offset <= INT64_MAX - chunk) {
        return offset + chunk;
    }
    return offset;
}

// Helper replicating ProgressBox percentage calculation
static uint32_t CalcProgressPercentage(int64_t offset, int64_t total) {
    if (total <= 0) return 0;
    const int64_t draw_offset = std::clamp<int64_t>(offset, 0, total);
    return static_cast<uint32_t>(((double)draw_offset / (double)total) * 100.0);
}

// Helper replicating ETA remaining seconds calculation
static int64_t CalcRemainingSeconds(int64_t total, int64_t last_offset, int64_t speed) {
    if (speed <= 0 || total <= 0) return 0;
    const int64_t left = std::max<int64_t>(0, total - last_offset);
    return left / speed;
}

// Helpers replicating safe total conversions in patched libhaze
static int64_t CalcSafeTotalFromPropList(uint64_t prop_size) {
    if (prop_size > 0 && prop_size <= static_cast<uint64_t>(INT64_MAX)) {
        return static_cast<int64_t>(prop_size);
    }
    return 0;
}

static int64_t CalcSafeTotalFromContainerHeader(uint32_t header_length, uint32_t container_header_size) {
    if (header_length > container_header_size && header_length != 0xFFFFFFFFU) {
        return static_cast<int64_t>(header_length - container_header_size);
    }
    return 0;
}

static int test_transfer_accumulation() {
    // Normal chunk transfers
    CHECK(CalcTransferredBytes(0, 524288) == 524288);
    CHECK(CalcTransferredBytes(524288, 524288) == 1048576);
    CHECK(CalcTransferredBytes(104857600, 1048576) == 105906176);

    // Large transfers (> 4 GiB)
    const int64_t five_gib = 5368709120LL;
    CHECK(CalcTransferredBytes(five_gib, 1048576) == five_gib + 1048576);

    // Overflow protection edge cases
    CHECK(CalcTransferredBytes(INT64_MAX, 1) == INT64_MAX);
    CHECK(CalcTransferredBytes(INT64_MAX - 10, 20) == INT64_MAX - 10);
    CHECK(CalcTransferredBytes(-1, 100) == -1);
    CHECK(CalcTransferredBytes(100, -1) == 100);

    return 0;
}

static int test_safe_total_rules() {
    // MTP property list size validation
    CHECK(CalcSafeTotalFromPropList(0) == 0);
    CHECK(CalcSafeTotalFromPropList(1048576) == 1048576);
    CHECK(CalcSafeTotalFromPropList(static_cast<uint64_t>(INT64_MAX)) == INT64_MAX);
    CHECK(CalcSafeTotalFromPropList(static_cast<uint64_t>(INT64_MAX) + 1ULL) == 0);
    CHECK(CalcSafeTotalFromPropList(UINT64_MAX) == 0);

    // PTP USB container header length validation
    constexpr uint32_t kHeaderSize = 12; // sizeof(PtpUsbBulkContainer)
    CHECK(CalcSafeTotalFromContainerHeader(0xFFFFFFFFU, kHeaderSize) == 0);
    CHECK(CalcSafeTotalFromContainerHeader(kHeaderSize, kHeaderSize) == 0);
    CHECK(CalcSafeTotalFromContainerHeader(10, kHeaderSize) == 0);
    CHECK(CalcSafeTotalFromContainerHeader(0, kHeaderSize) == 0);
    CHECK(CalcSafeTotalFromContainerHeader(1012, kHeaderSize) == 1000);
    CHECK(CalcSafeTotalFromContainerHeader(1048576 + kHeaderSize, kHeaderSize) == 1048576);

    return 0;
}

static int test_percentage_and_eta() {
    const int64_t total_size = 100000000LL; // 100 MB

    // Start
    CHECK(CalcProgressPercentage(0, total_size) == 0);
    // Midpoint
    CHECK(CalcProgressPercentage(50000000LL, total_size) == 50);
    // Almost done
    CHECK(CalcProgressPercentage(99000000LL, total_size) == 99);
    // Complete
    CHECK(CalcProgressPercentage(100000000LL, total_size) == 100);
    // Clamping on overshoot (should not exceed 100%)
    CHECK(CalcProgressPercentage(150000000LL, total_size) == 100);

    // Unknown total (0) should yield 0%
    CHECK(CalcProgressPercentage(50000000LL, 0) == 0);

    // ETA calculations
    const int64_t speed = 10000000LL; // 10 MB/s
    // 50 MB remaining at 10 MB/s = 5 seconds
    CHECK(CalcRemainingSeconds(total_size, 50000000LL, speed) == 5);
    // 100 MB remaining at 10 MB/s = 10 seconds
    CHECK(CalcRemainingSeconds(total_size, 0, speed) == 10);
    // 0 remaining = 0 seconds
    CHECK(CalcRemainingSeconds(total_size, total_size, speed) == 0);
    // No ETA for unknown total
    CHECK(CalcRemainingSeconds(0, 50000000LL, speed) == 0);
    // No ETA for zero speed
    CHECK(CalcRemainingSeconds(total_size, 50000000LL, 0) == 0);

    return 0;
}

static int test_indeterminate_state() {
    // Mode selection: size > 0 is determinate, size == 0 && offset > 0 is indeterminate
    const int64_t known_total = 1048576LL;
    const int64_t unknown_total = 0;
    const int64_t offset = 524288LL;

    const bool is_known = (known_total > 0);
    const bool is_indeterminate = (!is_known && offset > 0);
    CHECK(is_known == true);
    CHECK(is_indeterminate == false);

    const bool unk_is_known = (unknown_total > 0);
    const bool unk_is_indeterminate = (!unk_is_known && offset > 0);
    CHECK(unk_is_known == false);
    CHECK(unk_is_indeterminate == true);

    return 0;
}

int main() {
    if (test_transfer_accumulation()) return 1;
    if (test_safe_total_rules()) return 1;
    if (test_percentage_and_eta()) return 1;
    if (test_indeterminate_state()) return 1;

    std::printf("ok  mtp_progress_calc: %d checks passed\n", g_checks);
    return 0;
}
