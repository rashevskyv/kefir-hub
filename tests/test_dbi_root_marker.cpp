// Host test for the DBI save-archive root marker ("//") and the save-archive
// entry normalisation in sphaira/include/path_util.hpp. Scenarios come from the
// former Python model in test_dbi_restore_admission_contract.py (A and B).
//
//     g++ -std=c++20 -Wall -Wextra -Werror -I sphaira/include tests/test_dbi_root_marker.cpp -o /tmp/t && /tmp/t

#include "path_util.hpp"

#include <cstdint>
#include <cstdio>

using namespace sphaira;

static int g_checks = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(expr)) {                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);       \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static constexpr std::uint32_t S_IFDIR_FA = 0040000u << 16;
static constexpr std::uint32_t S_IFREG_FA = 0100000u << 16;
static constexpr std::uint32_t MSDOS_DIR = 0x10;

// A. The exact DBI marker: "//", zero bytes, directory semantics.
static int test_marker_accepted() {
    CHECK(path::IsDbiRootMarkerEntry("//", 0, S_IFDIR_FA | MSDOS_DIR));
    CHECK(path::IsDbiRootMarkerEntry("//", 0, S_IFDIR_FA));
    CHECK(path::IsDbiRootMarkerEntry("//", 0, MSDOS_DIR));
    CHECK(path::IsDbiRootMarkerEntry("//", 0, 0)); // minizip name-based directory
    return 0;
}

static int test_marker_rejected() {
    CHECK(!path::IsDbiRootMarkerEntry("//", 1, S_IFDIR_FA));  // carries data
    CHECK(!path::IsDbiRootMarkerEntry("//", 0, S_IFREG_FA));  // regular file
    CHECK(!path::IsDbiRootMarkerEntry("///", 0, S_IFDIR_FA)); // not the exact name
    CHECK(!path::IsDbiRootMarkerEntry("/", 0, S_IFDIR_FA));
    return 0;
}

// B. Malformed entries never normalise; one leading slash is still accepted.
static int test_malformed_matrix() {
    const char* malformed[] = {
        "//evil", "///", "//../evil", "\\\\evil", "c:/evil", "evil\x01", "foo/../bar", "foo/./bar",
    };
    for (const char* raw : malformed) {
        CHECK(!path::NormalizeSaveArchiveEntry(raw).has_value());
    }

    const auto payload = path::NormalizeSaveArchiveEntry("/savedata.bin");
    CHECK(payload.has_value());
    CHECK(*payload == "savedata.bin");
    return 0;
}

int main() {
    if (test_marker_accepted() || test_marker_rejected() || test_malformed_matrix()) {
        return 1;
    }
    std::printf("ok  dbi_root_marker: %d checks passed\n", g_checks);
    return 0;
}
