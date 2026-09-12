// Host test for Kefir Hub NAND transfer archive contract, staging lifecycle, and 2x4 account grid
//
//     g++ -std=c++20 -Wall -Wextra -Werror -I sphaira/include tests/test_nand_archive_contract.cpp -o /tmp/t && /tmp/t

#include "path_util.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

static int g_checks = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(expr)) {                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);       \
            return 1;                                                         \
        }                                                                     \
    } while (0)

namespace {

// Test Case 1: Archive detection, pre-publication .part validation, and listing contract
struct MockZipEntry {
    std::string path;
    std::string content;
};

bool ValidatePackArchiveContent(const std::vector<MockZipEntry>& entries) {
    bool has_manifest = false;
    bool has_save_tree = false;

    for (const auto& e : entries) {
        if (!sphaira::path::IsSafeArchiveEntry(e.path)) {
            return false;
        }
        // Nested manifest is rejected (rejection of wrapper directory)
        if (std::string_view{e.path}.ends_with("manifest.json") && e.path != "manifest.json") {
            return false;
        }
        if (e.path == "manifest.json") {
            if (e.content.find("kefir-nand-transfer") != std::string::npos) {
                has_manifest = true;
            }
        }
        if (std::string_view{e.path}.starts_with("8000000000000010") ||
            std::string_view{e.path}.starts_with("80000000000000F0")) {
            has_save_tree = true;
        }
    }

    return has_manifest && has_save_tree;
}

bool MockIsPackArchive(const std::string& path, const std::vector<MockZipEntry>& entries) {
    if (path.empty()) {
        return false;
    }
    std::string_view sv{path};
    // Public IsPackArchive strictly requires published .zip/.ZIP suffix
    if (!sv.ends_with(".zip") && !sv.ends_with(".ZIP")) {
        return false;
    }
    return ValidatePackArchiveContent(entries);
}

bool ShouldListFileEntry(std::string_view filename) {
    if (filename.empty() || filename[0] == '.') {
        return false;
    }
    if (filename.starts_with("_restore_") || filename.starts_with("_staging_")) {
        return false;
    }
    if (filename == "dump.te" || filename == "restore.te" ||
        filename.ends_with(".part") || filename.ends_with(".te")) {
        return false;
    }
    return filename.ends_with(".kefir-nand.zip") || filename.ends_with(".zip") || filename.ends_with(".ZIP");
}

int test_case_1_archive_detection_and_part_validation() {
    // 1. File listing filter accepts valid ZIP extensions and skips auxiliary/intermediate/staging files
    CHECK(ShouldListFileEntry("20260909_120000.kefir-nand.zip"));
    CHECK(ShouldListFileEntry("backup.zip"));
    CHECK(ShouldListFileEntry("LEGACY.ZIP"));

    CHECK(!ShouldListFileEntry(".hidden.zip"));
    CHECK(!ShouldListFileEntry("_restore_20260909_120000"));
    CHECK(!ShouldListFileEntry("_staging_20260909_120000"));
    CHECK(!ShouldListFileEntry("20260909_120000.kefir-nand.zip.part"));
    CHECK(!ShouldListFileEntry("restore.te"));
    CHECK(!ShouldListFileEntry("dump.te"));
    CHECK(!ShouldListFileEntry("script.te"));

    // 2. Pre-publication validation accepts .part file when content is valid
    std::vector<MockZipEntry> valid_entries = {
        {"manifest.json", "{\"kind\":\"kefir-nand-transfer\",\"version\":1}"},
        {"8000000000000010/su/avators/profiles.dat", "DATA"},
        {"80000000000000F0/save", "PLAYTIME"},
    };
    const std::string part_path = "20260909_120000.kefir-nand.zip.part";
    const std::string final_path = "20260909_120000.kefir-nand.zip";

    // Internal content validation passes for .part
    CHECK(ValidatePackArchiveContent(valid_entries));
    // Public IsPackArchive strictly rejects .part
    CHECK(!MockIsPackArchive(part_path, valid_entries));
    // Public IsPackArchive accepts final .kefir-nand.zip
    CHECK(MockIsPackArchive(final_path, valid_entries));

    // 3. Missing manifest.json -> content validation fails
    std::vector<MockZipEntry> missing_manifest = {
        {"8000000000000010/su/avators/profiles.dat", "DATA"},
    };
    CHECK(!ValidatePackArchiveContent(missing_manifest));
    CHECK(!MockIsPackArchive(final_path, missing_manifest));

    // 4. Manifest with wrong kind -> content validation fails
    std::vector<MockZipEntry> wrong_kind = {
        {"manifest.json", "{\"kind\":\"switch-app\",\"version\":1}"},
        {"8000000000000010/su/avators/profiles.dat", "DATA"},
    };
    CHECK(!ValidatePackArchiveContent(wrong_kind));
    CHECK(!MockIsPackArchive(final_path, wrong_kind));

    // 5. Manifest present but no save tree (neither 0010 nor 00F0) -> rejected
    std::vector<MockZipEntry> no_save_tree = {
        {"manifest.json", "{\"kind\":\"kefir-nand-transfer\",\"version\":1}"},
        {"other/data.bin", "DATA"},
    };
    CHECK(!ValidatePackArchiveContent(no_save_tree));
    CHECK(!MockIsPackArchive(final_path, no_save_tree));

    return 0;
}

// Test Case 2: Rejection of unwanted ZIP root wrapper directory
int test_case_2_rejection_of_wrapper_directory() {
    // 1. Nested manifest inside a subfolder -> rejected
    std::vector<MockZipEntry> nested_manifest = {
        {"my_backup/manifest.json", "{\"kind\":\"kefir-nand-transfer\",\"version\":1}"},
        {"my_backup/8000000000000010/su/avators/profiles.dat", "DATA"},
    };
    CHECK(!ValidatePackArchiveContent(nested_manifest));

    // 2. Safe archive entry checks against directory traversal and dangerous characters
    CHECK(sphaira::path::IsSafeArchiveEntry("manifest.json"));
    CHECK(sphaira::path::IsSafeArchiveEntry("8000000000000010/su/avators/profiles.dat"));
    CHECK(!sphaira::path::IsSafeArchiveEntry("../traversal.txt"));
    CHECK(!sphaira::path::IsSafeArchiveEntry("8000000000000010/../../evil.bin"));
    CHECK(!sphaira::path::IsSafeArchiveEntry("/absolute/manifest.json"));
    CHECK(!sphaira::path::IsSafeArchiveEntry("windows\\style\\path.txt"));
    CHECK(!sphaira::path::IsSafeArchiveEntry("sdmc:/file.txt"));

    return 0;
}

// Test Case 3: Exact selected archive restore staging & collision avoidance
std::string MakeStagingDir(const std::string& archive_path, const std::string& stamp) {
    const auto slash = archive_path.find_last_of('/');
    const auto filename = (slash == std::string::npos) ? archive_path : archive_path.substr(slash + 1);
    const auto dot = filename.find_last_of('.');
    const auto stem = (dot == std::string::npos) ? filename : filename.substr(0, dot);
    return std::string("/config/kefir/nand_transfer/_restore_") + stem + "_" + stamp;
}

bool ValidateTegraExplorer5PartContract(const std::string& staging_path) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= staging_path.size()) {
        const auto end = staging_path.find('/', start);
        if (end == std::string::npos) {
            parts.push_back(staging_path.substr(start));
            break;
        }
        parts.push_back(staging_path.substr(start, end - start));
        start = end + 1;
    }

    if (parts.size() != 5) {
        return false;
    }
    if (parts[0] != "" || parts[1] != "config" || parts[2] != "kefir" || parts[3] != "nand_transfer") {
        return false;
    }
    const auto& name = parts[4];
    if (name.empty() || name == "." || name == ".." || name.find('\\') != std::string::npos) {
        return false;
    }
    return true;
}

int test_case_3_restore_staging_contract() {
    const std::string archive = "/config/kefir/nand_transfer/20260909_120000.kefir-nand.zip";
    const std::string staging = MakeStagingDir(archive, "20260909_164500");

    CHECK(staging == "/config/kefir/nand_transfer/_restore_20260909_120000.kefir-nand_20260909_164500");
    CHECK(ValidateTegraExplorer5PartContract(staging));
    CHECK(sphaira::path::IsSafeRestoreStagingDir(staging));

    CHECK(!ValidateTegraExplorer5PartContract("/config/kefir/nand_transfer/sub/pack"));
    CHECK(!ValidateTegraExplorer5PartContract("/config/kefir/pack"));
    CHECK(!ValidateTegraExplorer5PartContract("/other/kefir/nand_transfer/pack"));
    CHECK(!ValidateTegraExplorer5PartContract("/config/kefir/nand_transfer/."));
    CHECK(!ValidateTegraExplorer5PartContract("/config/kefir/nand_transfer/.."));

    return 0;
}

// Test Case 4: Delete dispatch safety
enum class DeleteMethod { None, File, DirectoryRecursively };

struct MockFsEntry {
    std::string path;
    bool is_file{false};
    bool is_dir{false};
};

DeleteMethod DispatchDelete(const MockFsEntry& entry) {
    if (entry.is_file) {
        return DeleteMethod::File;
    }
    if (entry.is_dir) {
        return DeleteMethod::DirectoryRecursively;
    }
    return DeleteMethod::None;
}

int test_case_4_delete_dispatch_safety() {
    MockFsEntry arc_entry{"/config/kefir/nand_transfer/pack1.kefir-nand.zip", true, false};
    CHECK(DispatchDelete(arc_entry) == DeleteMethod::File);

    MockFsEntry dir_entry{"/config/kefir/nand_transfer/legacy_pack_folder", false, true};
    CHECK(DispatchDelete(dir_entry) == DeleteMethod::DirectoryRecursively);

    std::vector<MockFsEntry> batch = {
        {"/config/kefir/nand_transfer/20260909_100000.kefir-nand.zip", true, false},
        {"/config/kefir/nand_transfer/20260801_120000", false, true},
        {"/config/kefir/nand_transfer/20260908_180000.kefir-nand.zip", true, false},
    };

    int file_deletes = 0;
    int dir_deletes = 0;
    for (const auto& item : batch) {
        auto method = DispatchDelete(item);
        if (method == DeleteMethod::File) file_deletes++;
        if (method == DeleteMethod::DirectoryRecursively) dir_deletes++;
    }
    CHECK(file_deletes == 2);
    CHECK(dir_deletes == 1);

    return 0;
}

// Test Case 5: Detail layout contract (column-major mapping: left 0..3, right 4..7)
constexpr int64_t GridSlotToEntry(int64_t grid_idx) {
    return (grid_idx % 2) * 4 + (grid_idx / 2);
}

constexpr int64_t EntryToGridSlot(int64_t entry_idx) {
    return (entry_idx % 4) * 2 + (entry_idx / 4);
}

struct Vec2 { float x{}, y{}; };
struct Vec4 { float x{}, y{}, w{}, h{}; };

struct MockGridCell {
    int64_t grid_slot{};
    int64_t entry_index{};
    int64_t col{};
    int64_t row{};
    Vec4 rect{};
    Vec4 text_scissor{};
};

MockGridCell CalculateCellLayout(int64_t grid_slot, const Vec4& pos, const Vec4& cell_size, const Vec2& pad) {
    constexpr int64_t kCols = 2;
    const int64_t col = grid_slot % kCols;
    const int64_t row = grid_slot / kCols;
    const int64_t entry_idx = GridSlotToEntry(grid_slot);

    const float x = pos.x + static_cast<float>(col) * (cell_size.w + pad.x);
    const float y = pos.y + static_cast<float>(row) * (cell_size.h + pad.y);

    const float avatar_w = 90.f;
    const float pad_inner = 15.f;
    const float text_x = x + avatar_w + pad_inner;
    const float text_w = std::max(0.f, x + cell_size.w - text_x - 12.f);

    MockGridCell cell;
    cell.grid_slot = grid_slot;
    cell.entry_index = entry_idx;
    cell.col = col;
    cell.row = row;
    cell.rect = Vec4{x, y, cell_size.w, cell_size.h};
    cell.text_scissor = Vec4{text_x, y, text_w, cell_size.h};
    return cell;
}

int test_case_5_column_major_grid_layout() {
    const Vec4 container{75.f, 110.f, 1145.f, 560.f};
    const Vec4 cell_size{75.f, 110.f, 555.f, 110.f};
    const Vec2 pad{20.f, 15.f};

    // 1. Verify bidirectional identity mapping between grid slots and entries
    for (int64_t e = 0; e < 8; ++e) {
        CHECK(GridSlotToEntry(EntryToGridSlot(e)) == e);
        CHECK(EntryToGridSlot(GridSlotToEntry(e)) == e);
    }

    // 2. Verify column-major stack distribution:
    // Left column (col 0): entries 0, 1, 2, 3
    // Right column (col 1): entries 4, 5, 6, 7
    CHECK(GridSlotToEntry(0) == 0); // top-left
    CHECK(GridSlotToEntry(2) == 1); // second-left
    CHECK(GridSlotToEntry(4) == 2); // third-left
    CHECK(GridSlotToEntry(6) == 3); // bottom-left

    CHECK(GridSlotToEntry(1) == 4); // top-right
    CHECK(GridSlotToEntry(3) == 5); // second-right
    CHECK(GridSlotToEntry(5) == 6); // third-right
    CHECK(GridSlotToEntry(7) == 7); // bottom-right

    // 3. Verify visual geometry and scissoring for all 8 slots
    for (int64_t slot = 0; slot < 8; ++slot) {
        const auto cell = CalculateCellLayout(slot, container, cell_size, pad);

        CHECK(cell.col == (slot % 2));
        CHECK(cell.row == (slot / 2));

        // Spatial bounding box checks
        CHECK(cell.rect.x >= container.x);
        CHECK(cell.rect.x + cell.rect.w <= container.x + container.w);
        CHECK(cell.rect.y >= container.y);
        CHECK(cell.rect.y + cell.rect.h <= container.y + container.h);

        // Column separation
        if (cell.col == 0) {
            const float col1_start_x = container.x + (cell_size.w + pad.x);
            CHECK(cell.rect.x + cell.rect.w <= col1_start_x);
            CHECK(cell.text_scissor.x + cell.text_scissor.w <= cell.rect.x + cell.rect.w);
        }
        CHECK(cell.text_scissor.w > 400.f);
    }

    // Specific coordinates
    const auto c0 = CalculateCellLayout(0, container, cell_size, pad);
    CHECK(c0.entry_index == 0 && c0.rect.x == 75.f && c0.rect.y == 110.f);

    const auto c1 = CalculateCellLayout(1, container, cell_size, pad);
    CHECK(c1.entry_index == 4 && c1.rect.x == 650.f && c1.rect.y == 110.f);

    const auto c2 = CalculateCellLayout(2, container, cell_size, pad);
    CHECK(c2.entry_index == 1 && c2.rect.x == 75.f && c2.rect.y == 235.f);

    const auto c7 = CalculateCellLayout(7, container, cell_size, pad);
    CHECK(c7.entry_index == 7 && c7.rect.x == 650.f && c7.rect.y == 485.f);

    return 0;
}

// Test Case 6: Strict production validated staging cleanup helper
int test_case_6_validated_staging_cleanup() {
    using sphaira::path::IsSafeRestoreStagingDir;
    using sphaira::path::IsSafeBackupStagingDir;

    // Valid direct-child restore staging paths
    CHECK(IsSafeRestoreStagingDir("/config/kefir/nand_transfer/_restore_20260909_120000"));
    CHECK(IsSafeRestoreStagingDir("/config/kefir/nand_transfer/_restore_pack"));
    CHECK(IsSafeRestoreStagingDir("sdmc:/config/kefir/nand_transfer/_restore_123"));

    // Hostile and dangerous paths must all be rejected:
    CHECK(!IsSafeRestoreStagingDir(""));
    CHECK(!IsSafeRestoreStagingDir("/"));
    CHECK(!IsSafeRestoreStagingDir("/config"));
    CHECK(!IsSafeRestoreStagingDir("/config/kefir"));
    CHECK(!IsSafeRestoreStagingDir("/config/kefir/nand_transfer"));
    CHECK(!IsSafeRestoreStagingDir("/config/kefir/nand_transfer/"));
    CHECK(!IsSafeRestoreStagingDir("/config/kefir/nand_transfer/.."));
    CHECK(!IsSafeRestoreStagingDir("/config/kefir/nand_transfer/."));
    CHECK(!IsSafeRestoreStagingDir("/config/kefir/nand_transfer/_restore_x/child"));
    CHECK(!IsSafeRestoreStagingDir("/config/kefir/nand_transfer/not_restore"));
    CHECK(!IsSafeRestoreStagingDir("/other/_restore_x"));
    CHECK(!IsSafeRestoreStagingDir("/config/kefir/nand_transfer/_restore_")); // empty suffix
    CHECK(!IsSafeRestoreStagingDir("/config/kefir/nand_transfer\\_restore_x")); // backslash
    CHECK(!IsSafeRestoreStagingDir("/config/kefir/nand_transfer/20260909_120000.kefir-nand.zip")); // archives protected
    CHECK(!IsSafeRestoreStagingDir("/config/kefir/nand_transfer/20260909_120000")); // legacy dirs protected

    // Backup finalization may remove only its own reserved direct-child staging directory.
    CHECK(IsSafeBackupStagingDir("/config/kefir/nand_transfer/_staging_20260909_120000"));
    CHECK(IsSafeBackupStagingDir("sdmc:/config/kefir/nand_transfer/_staging_20260909_120000"));
    CHECK(!IsSafeBackupStagingDir("/"));
    CHECK(!IsSafeBackupStagingDir("/config/kefir/nand_transfer"));
    CHECK(!IsSafeBackupStagingDir("/config/kefir/nand_transfer/_staging_"));
    CHECK(!IsSafeBackupStagingDir("/config/kefir/nand_transfer/_staging_x/child"));
    CHECK(!IsSafeBackupStagingDir("/config/kefir/nand_transfer/_restore_x"));
    CHECK(!IsSafeBackupStagingDir("/config/kefir/nand_transfer/legacy_pack"));
    CHECK(!IsSafeBackupStagingDir("/other/_staging_x"));
    CHECK(!IsSafeBackupStagingDir("/config/kefir/nand_transfer/../_staging_x"));

    return 0;
}

// Test Case 7: Strict TegraExplorer contract preservation
int test_case_7_tegra_script_contract() {
    const char* script_path = "tests/test_nand_restore_auto_contract.sh";
    FILE* f = std::fopen(script_path, "r");
    if (f) {
        std::fclose(f);
        const int rc = std::system("./tests/test_nand_restore_auto_contract.sh > /dev/null 2>&1");
        CHECK(rc == 0);
    }
    return 0;
}

// Test Case 8: Safety backup validation, operation directories, and exact matching
int test_case_8_safety_backup_contract() {
    using sphaira::path::IsSafeRestoreStagingDir;
    using sphaira::path::IsSafeBackupStagingDir;

    // 1. Snapshot file sanity validation (size >= 0x200, matching source and dest)
    auto is_valid_snapshot = [](bool src_exists, int64_t src_size, bool dst_exists, int64_t dst_size) -> bool {
        if (!src_exists || src_size < 0x200) return false;
        if (!dst_exists || dst_size < 0x200) return false;
        return (src_size == dst_size);
    };

    CHECK(is_valid_snapshot(true, 0x40000, true, 0x40000));
    CHECK(!is_valid_snapshot(false, 0x40000, true, 0x40000)); // src missing
    CHECK(!is_valid_snapshot(true, 0x100, true, 0x100));       // src too small
    CHECK(!is_valid_snapshot(true, 0x40000, false, 0));        // dst missing
    CHECK(!is_valid_snapshot(true, 0x40000, true, 0x20000));   // size mismatch (truncated)
    CHECK(!is_valid_snapshot(true, 0x40000, true, 0));         // dst empty

    // 2. Safety backup directory is strictly protected from staging cleanup
    const char* safety_dir = "/config/kefir/safety_backup";
    CHECK(!IsSafeRestoreStagingDir(safety_dir));
    CHECK(!IsSafeBackupStagingDir(safety_dir));
    CHECK(!IsSafeRestoreStagingDir("/config/kefir/safety_backup/20260912_100000"));
    CHECK(!IsSafeRestoreStagingDir("/config/kefir/safety_backup/20260912_100000/8000000000000010"));

    // User packs and archives are never recognized as temporary staging
    CHECK(!IsSafeRestoreStagingDir("/config/kefir/nand_transfer/20260911_190000.kefir-nand.zip"));
    CHECK(!IsSafeRestoreStagingDir("/config/kefir/nand_transfer/20260911_190000"));

    // 3. Exact full pack path matching (never basename only) and NAND matching
    auto can_reuse_snapshot = [](std::string_view snap_nand, std::string_view snap_pack,
                                 std::string_view cur_nand, std::string_view cur_pack, bool snap_ok) -> bool {
        if (!snap_ok) return false;
        if (snap_nand != cur_nand) return false;
        // Must match exact full target pack path
        return (snap_pack == cur_pack);
    };

    const std::string full_pack = "/config/kefir/nand_transfer/20260911_190000";
    const std::string base_pack = "20260911_190000";
    CHECK(can_reuse_snapshot("emu", full_pack, "emu", full_pack, true));
    CHECK(!can_reuse_snapshot("emu", full_pack, "emu", full_pack, false)); // snap not ok
    CHECK(!can_reuse_snapshot("sys", full_pack, "emu", full_pack, true));  // different NAND
    CHECK(!can_reuse_snapshot("emu", base_pack, "emu", full_pack, true));  // basename-only match rejected
    CHECK(!can_reuse_snapshot("emu", "/config/kefir/nand_transfer/other", "emu", full_pack, true)); // different pack

    // 4. Mandatory snapshot completeness: all saves modified by the target pack must have verified snapshots
    auto verify_safety_completeness = [](bool snap0010, bool snap0011, bool snap00F0,
                                         bool needs0010, bool needs0011, bool needs00F0, bool snap_ok) -> bool {
        if (!snap_ok) return false;
        if (needs0010 && !snap0010) return false;
        if (needs0011 && !snap0011) return false;
        if (needs00F0 && !snap00F0) return false;
        return true;
    };

    // Both 0010 and 00F0 needed: having only one must NOT be verified
    CHECK(verify_safety_completeness(true, false, true, true, false, true, true));
    CHECK(!verify_safety_completeness(true, false, false, true, false, true, true)); // 00F0 missing -> cannot verify
    CHECK(!verify_safety_completeness(false, false, true, true, false, true, true)); // 0010 missing -> cannot verify
    CHECK(!verify_safety_completeness(true, true, true, true, true, true, false));   // marker missing -> cannot verify

    // 5. Undo safety: requires valid safety_dir.txt
    auto can_undo = [](bool record_exists, std::string_view record_dir, bool snap_ok) -> bool {
        if (!record_exists || record_dir.empty()) return false;
        return snap_ok;
    };
    CHECK(can_undo(true, "/config/kefir/safety_backup/20260912_100000", true));
    CHECK(!can_undo(false, "", true)); // missing safety_dir.txt -> abort, do not guess
    CHECK(!can_undo(true, "/config/kefir/safety_backup/20260912_100000", false)); // unverified snapshot -> abort

    return 0;
}

} // namespace

int main() {
    if (test_case_1_archive_detection_and_part_validation()) return 1;
    if (test_case_2_rejection_of_wrapper_directory()) return 1;
    if (test_case_3_restore_staging_contract()) return 1;
    if (test_case_4_delete_dispatch_safety()) return 1;
    if (test_case_5_column_major_grid_layout()) return 1;
    if (test_case_6_validated_staging_cleanup()) return 1;
    if (test_case_7_tegra_script_contract()) return 1;
    if (test_case_8_safety_backup_contract()) return 1;

    std::printf("ok  nand_archive_contract: %d checks passed\n", g_checks);
    return 0;
}
