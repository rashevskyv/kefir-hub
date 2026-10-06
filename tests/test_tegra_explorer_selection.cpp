// Host unit tests for TegraExplorer metadata and payload selection policy.
// Runs directly on the host without Switch / libnx dependencies:
//
//     g++ -std=c++20 -Wall -Wextra -Werror -I sphaira/include tests/test_tegra_explorer_selection.cpp -o /tmp/t && /tmp/t

#include "utils/tegra_explorer_meta.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>

using namespace sphaira::utils;

static int g_checks = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(expr)) {                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);       \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static std::vector<uint8_t> makePayloadWithFooter(
    size_t total_size,
    size_t footer_offset_from_end,
    uint8_t maj,
    uint8_t min,
    uint8_t pat,
    uint32_t kefir,
    const char* magic_start = "KFRP",
    const char* magic_end = "PRFK"
) {
    std::vector<uint8_t> data(total_size, 0xAA);
    if (total_size < sizeof(TegraExplorerFooter) || footer_offset_from_end < sizeof(TegraExplorerFooter)) {
        return data;
    }
    const size_t pos = total_size - footer_offset_from_end;
    if (pos + sizeof(TegraExplorerFooter) > total_size) {
        return data;
    }

    TegraExplorerFooter footer{};
    std::memcpy(footer.magic, magic_start, 4);
    footer.format_version = 1;
    footer.payload_type = 1;
    footer.app_version_major = maj;
    footer.app_version_minor = min;
    footer.app_version_patch = pat;
    footer.reserved[0] = 0;
    footer.reserved[1] = 0;
    footer.reserved[2] = 0;
    footer.kefir_version = kefir;
    std::memcpy(footer.magic_end, magic_end, 4);

    std::memcpy(&data[pos], &footer, sizeof(footer));
    return data;
}

static int test_version_comparison() {
    // Exact equality across all components
    const TegraExplorerVersion v1{4, 2, 25, 922};
    const TegraExplorerVersion v2{4, 2, 25, 922};
    CHECK(v1 == v2);
    CHECK(!(v1 < v2));
    CHECK(!(v1 > v2));

    // Major component precedence
    const TegraExplorerVersion v_major_low{4, 9, 99, 999};
    const TegraExplorerVersion v_major_high{5, 0, 0, 0};
    CHECK(v_major_low < v_major_high);
    CHECK(v_major_high > v_major_low);

    // Minor component precedence
    const TegraExplorerVersion v_minor_low{4, 2, 99, 999};
    const TegraExplorerVersion v_minor_high{4, 3, 0, 0};
    CHECK(v_minor_low < v_minor_high);

    // Patch component precedence
    const TegraExplorerVersion v_patch_low{4, 2, 25, 999};
    const TegraExplorerVersion v_patch_high{4, 2, 26, 0};
    CHECK(v_patch_low < v_patch_high);

    // Kefir footer component comparison (tie-breaker when app version matches)
    const TegraExplorerVersion v_kefir_921{4, 2, 25, 921};
    const TegraExplorerVersion v_kefir_922{4, 2, 25, 922};
    const TegraExplorerVersion v_kefir_923{4, 2, 25, 923};
    CHECK(v_kefir_921 < v_kefir_922);
    CHECK(v_kefir_922 < v_kefir_923);
    CHECK(v_kefir_923 > v_kefir_922);
    CHECK(v_kefir_922 > v_kefir_921);

    return 0;
}

static int test_footer_parsing() {
    TegraExplorerVersion ver{};

    // Null or empty buffer
    CHECK(!parseTegraExplorerVersion(nullptr, 100, ver));
    CHECK(inspectTegraExplorerPayloadData(nullptr, 100, ver) == PayloadMetaStatus::Missing);
    CHECK(inspectTegraExplorerPayloadData(nullptr, 0, ver) == PayloadMetaStatus::Empty);
    std::vector<uint8_t> empty;
    CHECK(!parseTegraExplorerVersion(empty.data(), 0, ver));
    CHECK(inspectTegraExplorerPayloadData(empty.data(), 0, ver) == PayloadMetaStatus::Empty);

    // Truncated data (< 20 bytes)
    std::vector<uint8_t> short_buf(19, 0x00);
    CHECK(!parseTegraExplorerVersion(short_buf.data(), short_buf.size(), ver));
    CHECK(inspectTegraExplorerPayloadData(short_buf.data(), short_buf.size(), ver) == PayloadMetaStatus::Unrecognized);

    // Valid footer at exact end of buffer
    auto valid_buf = makePayloadWithFooter(1024, 20, 4, 2, 25, 922);
    CHECK(parseTegraExplorerVersion(valid_buf.data(), valid_buf.size(), ver));
    CHECK(ver.major == 4 && ver.minor == 2 && ver.patch == 25 && ver.kefir == 922);
    CHECK(inspectTegraExplorerPayloadData(valid_buf.data(), valid_buf.size(), ver) == PayloadMetaStatus::Valid);

    // Valid footer located within 512 bytes from end (e.g. 100 bytes before end)
    auto inner_buf = makePayloadWithFooter(1024, 100, 4, 2, 26, 930);
    CHECK(parseTegraExplorerVersion(inner_buf.data(), inner_buf.size(), ver));
    CHECK(ver.major == 4 && ver.minor == 2 && ver.patch == 26 && ver.kefir == 930);

    // Footer placed outside 512-byte search window (e.g. 600 bytes from end)
    auto too_far_buf = makePayloadWithFooter(1024, 600, 4, 2, 25, 922);
    CHECK(!parseTegraExplorerVersion(too_far_buf.data(), too_far_buf.size(), ver));
    CHECK(inspectTegraExplorerPayloadData(too_far_buf.data(), too_far_buf.size(), ver) == PayloadMetaStatus::Unrecognized);

    auto unsupported = makePayloadWithFooter(20, 20, 4, 2, 25, 922);
    unsupported[4] = 2;
    CHECK(!parseTegraExplorerVersion(unsupported.data(), unsupported.size(), ver));
    unsupported[4] = 1;
    unsupported[5] = 2;
    CHECK(!parseTegraExplorerVersion(unsupported.data(), unsupported.size(), ver));
    auto boundary = makePayloadWithFooter(1024, 512, 4, 2, 25, 922);
    CHECK(parseTegraExplorerVersion(boundary.data(), boundary.size(), ver));

    // Corrupted start magic
    auto bad_magic_start = makePayloadWithFooter(512, 20, 4, 2, 25, 922, "BADP", "PRFK");
    CHECK(!parseTegraExplorerVersion(bad_magic_start.data(), bad_magic_start.size(), ver));

    // Corrupted end magic
    auto bad_magic_end = makePayloadWithFooter(512, 20, 4, 2, 25, 922, "KFRP", "BADK");
    CHECK(!parseTegraExplorerVersion(bad_magic_end.data(), bad_magic_end.size(), ver));

    // Verify against actual embedded binary in assets/romfs
    std::ifstream file("assets/romfs/tegra/TegraExplorer.bin", std::ios::binary);
    CHECK(file.is_open());
    {
        std::vector<uint8_t> romfs_bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        CHECK(!romfs_bytes.empty());
        CHECK(inspectTegraExplorerPayloadData(romfs_bytes.data(), romfs_bytes.size(), ver) == PayloadMetaStatus::Valid);
        CHECK(ver.major > 0);
    }

    return 0;
}

static int test_path_recognition() {
    // Valid standard and alternative names with casing
    CHECK(isTegraExplorerPayload("TegraExplorer.bin"));
    CHECK(isTegraExplorerPayload("tegraexplorer.bin"));
    CHECK(isTegraExplorerPayload("TEGRAEXPLORER.BIN"));
    CHECK(isTegraExplorerPayload("tegra_explorer.bin"));
    CHECK(isTegraExplorerPayload("TEGRA_EXPLORER.BIN"));
    CHECK(isTegraExplorerPayload("Tegra_Explorer.bin"));

    // Full path variants
    CHECK(isTegraExplorerPayload("/bootloader/payloads/TegraExplorer.bin"));
    CHECK(isTegraExplorerPayload("/bootloader/payloads/tegra_explorer.bin"));
    CHECK(isTegraExplorerPayload("sdmc:/bootloader/payloads/TegraExplorer.bin"));
    CHECK(isTegraExplorerPayload("bootloader\\payloads\\TegraExplorer.bin"));

    // Non-matching payloads and extensions
    CHECK(!isTegraExplorerPayload("fusee.bin"));
    CHECK(!isTegraExplorerPayload("Lockpick_RCM.bin"));
    CHECK(!isTegraExplorerPayload("lockpick_rcm_pro.bin"));
    CHECK(!isTegraExplorerPayload("hekate_ctcaer.bin"));
    CHECK(!isTegraExplorerPayload("tegraexplorer.te"));
    CHECK(!isTegraExplorerPayload("TegraExplorer"));
    CHECK(!isTegraExplorerPayload(""));

    return 0;
}

static int test_selection_policy() {
    const TegraExplorerVersion v_921{4, 2, 25, 921};
    const TegraExplorerVersion v_922{4, 2, 25, 922};
    const TegraExplorerVersion v_923{4, 2, 25, 923};

    // 1. ROMFS newer than SD: must upgrade SD
    {
        TegraExplorerPayloadState sd{PayloadMetaStatus::Valid, v_921};
        TegraExplorerPayloadState romfs{PayloadMetaStatus::Valid, v_922};
        CHECK(decideTegraExplorerPayloadAction(sd, romfs) == TegraExplorerSelectionAction::UpgradeSdFromRomfs);
    }
    {
        TegraExplorerPayloadState sd{PayloadMetaStatus::Valid, TegraExplorerVersion{4, 1, 0, 0}};
        TegraExplorerPayloadState romfs{PayloadMetaStatus::Valid, v_922};
        CHECK(decideTegraExplorerPayloadAction(sd, romfs) == TegraExplorerSelectionAction::UpgradeSdFromRomfs);
    }

    // 2. SD equal to ROMFS: must keep SD payload
    {
        TegraExplorerPayloadState sd{PayloadMetaStatus::Valid, v_922};
        TegraExplorerPayloadState romfs{PayloadMetaStatus::Valid, v_922};
        CHECK(decideTegraExplorerPayloadAction(sd, romfs) == TegraExplorerSelectionAction::UseExistingSd);
    }

    // 3. SD newer than ROMFS: must keep SD payload (do not force ROMFS)
    {
        TegraExplorerPayloadState sd{PayloadMetaStatus::Valid, v_923};
        TegraExplorerPayloadState romfs{PayloadMetaStatus::Valid, v_922};
        CHECK(decideTegraExplorerPayloadAction(sd, romfs) == TegraExplorerSelectionAction::UseExistingSd);
    }
    {
        TegraExplorerPayloadState sd{PayloadMetaStatus::Valid, TegraExplorerVersion{4, 3, 0, 0}};
        TegraExplorerPayloadState romfs{PayloadMetaStatus::Valid, v_922};
        CHECK(decideTegraExplorerPayloadAction(sd, romfs) == TegraExplorerSelectionAction::UseExistingSd);
    }

    // 4. Absent SD: install embedded ROMFS payload
    {
        TegraExplorerPayloadState sd{PayloadMetaStatus::Missing, {}};
        TegraExplorerPayloadState romfs{PayloadMetaStatus::Valid, v_922};
        CHECK(decideTegraExplorerPayloadAction(sd, romfs) == TegraExplorerSelectionAction::InstallSdFromRomfs);
    }
    {
        TegraExplorerPayloadState sd{PayloadMetaStatus::Empty, {}};
        TegraExplorerPayloadState romfs{PayloadMetaStatus::Valid, v_922};
        CHECK(decideTegraExplorerPayloadAction(sd, romfs) == TegraExplorerSelectionAction::InstallSdFromRomfs);
    }
    {
        TegraExplorerPayloadState sd{PayloadMetaStatus::Unreadable, {}};
        TegraExplorerPayloadState romfs{PayloadMetaStatus::Valid, v_922};
        CHECK(decideTegraExplorerPayloadAction(sd, romfs) == TegraExplorerSelectionAction::InstallSdFromRomfs);
    }

    // 5. Both absent or unavailable: fail
    {
        TegraExplorerPayloadState sd{PayloadMetaStatus::Missing, {}};
        TegraExplorerPayloadState romfs{PayloadMetaStatus::Missing, {}};
        CHECK(decideTegraExplorerPayloadAction(sd, romfs) == TegraExplorerSelectionAction::FailNoPayload);
    }
    {
        TegraExplorerPayloadState sd{PayloadMetaStatus::Missing, {}};
        TegraExplorerPayloadState romfs{PayloadMetaStatus::Empty, {}};
        CHECK(decideTegraExplorerPayloadAction(sd, romfs) == TegraExplorerSelectionAction::FailNoPayload);
    }

    // 6. ROMFS unavailable, readable SD: preserve readable SD fallback
    {
        TegraExplorerPayloadState sd{PayloadMetaStatus::Valid, v_922};
        TegraExplorerPayloadState romfs{PayloadMetaStatus::Missing, {}};
        CHECK(decideTegraExplorerPayloadAction(sd, romfs) == TegraExplorerSelectionAction::UseExistingSd);
    }
    {
        TegraExplorerPayloadState sd{PayloadMetaStatus::Valid, v_922};
        TegraExplorerPayloadState romfs{PayloadMetaStatus::Empty, {}};
        CHECK(decideTegraExplorerPayloadAction(sd, romfs) == TegraExplorerSelectionAction::UseExistingSd);
    }
    {
        TegraExplorerPayloadState sd{PayloadMetaStatus::Unrecognized, {}};
        TegraExplorerPayloadState romfs{PayloadMetaStatus::Missing, {}};
        CHECK(decideTegraExplorerPayloadAction(sd, romfs) == TegraExplorerSelectionAction::UseExistingSd);
    }

    // 7. Never overwrite known-version SD payload with unrecognized ROMFS data
    {
        TegraExplorerPayloadState sd{PayloadMetaStatus::Valid, v_922};
        TegraExplorerPayloadState romfs{PayloadMetaStatus::Unrecognized, {}};
        CHECK(decideTegraExplorerPayloadAction(sd, romfs) == TegraExplorerSelectionAction::UseExistingSd);
    }

    // 8. Corrupted/unrecognized SD payload replaced by known-valid ROMFS
    {
        TegraExplorerPayloadState sd{PayloadMetaStatus::Unrecognized, {}};
        TegraExplorerPayloadState romfs{PayloadMetaStatus::Valid, v_922};
        CHECK(decideTegraExplorerPayloadAction(sd, romfs) == TegraExplorerSelectionAction::UpgradeSdFromRomfs);
    }

    // 9. Both unrecognized: preserve SD
    {
        TegraExplorerPayloadState sd{PayloadMetaStatus::Unrecognized, {}};
        TegraExplorerPayloadState romfs{PayloadMetaStatus::Unrecognized, {}};
        CHECK(decideTegraExplorerPayloadAction(sd, romfs) == TegraExplorerSelectionAction::UseExistingSd);
    }

    return 0;
}

int main() {
    if (test_version_comparison() != 0) return 1;
    if (test_footer_parsing() != 0) return 1;
    if (test_path_recognition() != 0) return 1;
    if (test_selection_policy() != 0) return 1;

    std::printf("PASS: test_tegra_explorer_selection (%d checks)\n", g_checks);
    return 0;
}
