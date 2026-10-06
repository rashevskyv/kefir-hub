// Host unit tests for manual firmware cleanup eligibility, path validation,
// and exact-target deletion isolation.
// Runs directly on the host without Switch / libnx dependencies:
//
//     g++ -std=c++20 -Wall -Wextra -Werror -I sphaira/include tests/test_manual_firmware_cleanup.cpp -o /tmp/t && /tmp/t

#include "ui/menus/kefir/kefir_firmware_cleanup.hpp"
#include "path_util.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

using namespace sphaira::ui::menu::kefir::detail;

static int g_checks = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(expr)) {                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);       \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static int test_cleanup_eligibility() {
    // 1. Manual folder selection must NEVER be automatically cleaned up,
    // even if the user manually selected the /firmware folder!
    CHECK(!ShouldAutomaticCleanupFirmware("/firmware", /*is_manual_folder=*/true));
    CHECK(!ShouldAutomaticCleanupFirmware("/firmware/", /*is_manual_folder=*/true));
    CHECK(!ShouldAutomaticCleanupFirmware("sdmc:/firmware", /*is_manual_folder=*/true));
    CHECK(!ShouldAutomaticCleanupFirmware("", /*is_manual_folder=*/true));

    // Ordinary nested manual source must not be automatically cleaned up
    CHECK(!ShouldAutomaticCleanupFirmware("/firmware/18.0.0", /*is_manual_folder=*/true));
    CHECK(!ShouldAutomaticCleanupFirmware("/switch/fw", /*is_manual_folder=*/true));

    // 2. Network downloads (is_manual_folder=false) to /firmware MUST be automatically cleaned up
    CHECK(ShouldAutomaticCleanupFirmware("/firmware", /*is_manual_folder=*/false));
    CHECK(ShouldAutomaticCleanupFirmware("/firmware/", /*is_manual_folder=*/false));
    CHECK(ShouldAutomaticCleanupFirmware("sdmc:/firmware", /*is_manual_folder=*/false));
    CHECK(ShouldAutomaticCleanupFirmware("", /*is_manual_folder=*/false)); // default destination is /firmware

    // 3. Staging directory for manual zip extraction MUST be automatically cleaned up
    CHECK(ShouldAutomaticCleanupFirmware(MANUAL_FIRMWARE_DEST, /*is_manual_folder=*/false));
    CHECK(!ShouldAutomaticCleanupFirmware(MANUAL_FIRMWARE_DEST, /*is_manual_folder=*/true));

    // 4. Any arbitrary path when not a download dest or manual staging is not cleaned up
    CHECK(!ShouldAutomaticCleanupFirmware("/custom_path", /*is_manual_folder=*/false));
    CHECK(!ShouldAutomaticCleanupFirmware("/firmware/18.0.0", /*is_manual_folder=*/false));

    // 5. Automatic ZIP cleanup: only for network downloads to FIRMWARE_DEST, never for manual folders
    CHECK(ShouldAutomaticCleanupZip("/firmware", /*is_manual_folder=*/false));
    CHECK(ShouldAutomaticCleanupZip("", /*is_manual_folder=*/false));
    CHECK(!ShouldAutomaticCleanupZip("/firmware", /*is_manual_folder=*/true));
    CHECK(!ShouldAutomaticCleanupZip(MANUAL_FIRMWARE_DEST, /*is_manual_folder=*/false));
    CHECK(!ShouldAutomaticCleanupZip("/custom_path", /*is_manual_folder=*/false));

    return 0;
}

static int test_deletion_prompt_and_decision() {
    // Prompt eligibility: only for manual folder installs that succeeded without cancellation
    CHECK(ShouldPromptManualFolderCleanup(/*is_manual_folder=*/true, /*install_succeeded=*/true, /*is_cancelled=*/false));
    // Never on failure
    CHECK(!ShouldPromptManualFolderCleanup(/*is_manual_folder=*/true, /*install_succeeded=*/false, /*is_cancelled=*/false));
    // Never on cancellation
    CHECK(!ShouldPromptManualFolderCleanup(/*is_manual_folder=*/true, /*install_succeeded=*/true, /*is_cancelled=*/true));
    // Never for network download
    CHECK(!ShouldPromptManualFolderCleanup(/*is_manual_folder=*/false, /*install_succeeded=*/true, /*is_cancelled=*/false));

    // Decision helper:
    // Option index 1 is "Delete"
    CHECK(ShouldExecuteFolderDeletion(1));
    // Option index 0 is "Keep" (default) -> must NOT execute deletion
    CHECK(!ShouldExecuteFolderDeletion(0));
    // Dismissal / B press (index 0 or nullopt) -> must NOT execute deletion
    CHECK(!ShouldExecuteFolderDeletion(std::nullopt));
    CHECK(!ShouldExecuteFolderDeletion(2));

    return 0;
}

static int test_path_validation() {
    // Empty paths rejected
    CHECK(!ValidateManualFirmwareFolder("").has_value());

    // Root rejected
    CHECK(!ValidateManualFirmwareFolder("/").has_value());

    // Root aliases rejected
    CHECK(!ValidateManualFirmwareFolder("//").has_value());
    CHECK(!ValidateManualFirmwareFolder("///").has_value());
    CHECK(!ValidateManualFirmwareFolder("sdmc:").has_value());
    CHECK(!ValidateManualFirmwareFolder("sdmc:/").has_value());
    CHECK(!ValidateManualFirmwareFolder("sdmc://").has_value());
    CHECK(!ValidateManualFirmwareFolder("SDMC:/").has_value());
    CHECK(!ValidateManualFirmwareFolder("Sdmc:").has_value());

    // Relative paths rejected
    CHECK(!ValidateManualFirmwareFolder("firmware").has_value());
    CHECK(!ValidateManualFirmwareFolder("./firmware").has_value());
    CHECK(!ValidateManualFirmwareFolder("../firmware").has_value());
    CHECK(!ValidateManualFirmwareFolder("sdmc:firmware").has_value());

    // Traversal components rejected
    CHECK(!ValidateManualFirmwareFolder("/firmware/..").has_value());
    CHECK(!ValidateManualFirmwareFolder("/firmware/.").has_value());
    CHECK(!ValidateManualFirmwareFolder("/firmware/../other").has_value());
    CHECK(!ValidateManualFirmwareFolder("/firmware/./other").has_value());
    CHECK(!ValidateManualFirmwareFolder("/a/b/../c").has_value());
    CHECK(!ValidateManualFirmwareFolder("sdmc:/firmware/..").has_value());

    // Unsupported devices rejected
    CHECK(!ValidateManualFirmwareFolder("ums0:/firmware").has_value());
    CHECK(!ValidateManualFirmwareFolder("ums1:/firmware").has_value());
    CHECK(!ValidateManualFirmwareFolder("usb:/firmware").has_value());
    CHECK(!ValidateManualFirmwareFolder("ftp:/firmware").has_value());
    CHECK(!ValidateManualFirmwareFolder("c:/firmware").has_value());

    // Backslashes and control characters rejected
    CHECK(!ValidateManualFirmwareFolder("\\firmware").has_value());
    CHECK(!ValidateManualFirmwareFolder("/firmware\\sub").has_value());
    CHECK(!ValidateManualFirmwareFolder("/firmware\x01").has_value());
    CHECK(!ValidateManualFirmwareFolder("/firmware\x1F").has_value());
    CHECK(!ValidateManualFirmwareFolder("/firmware\x7F").has_value());

    // Valid paths normalized correctly
    CHECK(ValidateManualFirmwareFolder("/firmware") == "/firmware");
    CHECK(ValidateManualFirmwareFolder("/firmware/") == "/firmware");
    CHECK(ValidateManualFirmwareFolder("//firmware///") == "/firmware");
    CHECK(ValidateManualFirmwareFolder("sdmc:/firmware") == "/firmware");
    CHECK(ValidateManualFirmwareFolder("sdmc:/firmware/") == "/firmware");
    CHECK(ValidateManualFirmwareFolder("SDMC:/firmware") == "/firmware");

    // Nested valid paths
    CHECK(ValidateManualFirmwareFolder("/firmware/18.0.0") == "/firmware/18.0.0");
    CHECK(ValidateManualFirmwareFolder("/firmware/18.0.0/") == "/firmware/18.0.0");
    CHECK(ValidateManualFirmwareFolder("sdmc:/switch/fw_dump") == "/switch/fw_dump");
    CHECK(ValidateManualFirmwareFolder("sdmc:/switch/fw_dump/") == "/switch/fw_dump");

    return 0;
}

static int test_exact_target_filesystem_isolation() {
    namespace fs = std::filesystem;

    const auto temp_base = fs::temp_directory_path() / "sphaira_test_fw_cleanup";
    std::error_code ec;
    fs::remove_all(temp_base, ec);
    fs::create_directories(temp_base, ec);

    const auto parent = temp_base / "parent_dir";
    const auto target = parent / "selected_firmware";
    const auto target_sub = target / "subfolder";
    const auto sibling = parent / "sibling_folder";
    const auto parent_file = parent / "important_note.txt";

    fs::create_directories(target_sub, ec);
    fs::create_directories(sibling, ec);

    // Create files in target
    {
        std::ofstream(target / "package1.bin") << "fw_data_1";
        std::ofstream(target_sub / "package2.bin") << "fw_data_2";
    }

    // Create files in sibling and parent
    {
        std::ofstream(sibling / "keep_me.bin") << "sibling_data";
        std::ofstream(parent_file) << "do_not_delete";
    }

    CHECK(fs::exists(target / "package1.bin"));
    CHECK(fs::exists(target_sub / "package2.bin"));
    CHECK(fs::exists(sibling / "keep_me.bin"));
    CHECK(fs::exists(parent_file));

    // Simulate exact-target deletion of the validated folder
    const auto norm = ValidateManualFirmwareFolder("/parent_dir/selected_firmware");
    CHECK(norm.has_value());
    CHECK(*norm == "/parent_dir/selected_firmware");

    // Recursively delete target
    fs::remove_all(temp_base / std::filesystem::path(*norm).relative_path(), ec);
    CHECK(!ec);

    // Target must be gone
    CHECK(!fs::exists(target));
    CHECK(!fs::exists(target / "package1.bin"));
    CHECK(!fs::exists(target_sub / "package2.bin"));

    // Sibling directory and its contents must remain completely intact!
    CHECK(fs::exists(sibling));
    CHECK(fs::exists(sibling / "keep_me.bin"));

    // Parent directory and parent files must remain completely intact!
    CHECK(fs::exists(parent));
    CHECK(fs::exists(parent_file));

    // Clean up
    fs::remove_all(temp_base, ec);

    return 0;
}

int main() {
    if (test_cleanup_eligibility() != 0) return 1;
    if (test_deletion_prompt_and_decision() != 0) return 1;
    if (test_path_validation() != 0) return 1;
    if (test_exact_target_filesystem_isolation() != 0) return 1;

    std::printf("PASS: test_manual_firmware_cleanup (%d checks)\n", g_checks);
    return 0;
}
