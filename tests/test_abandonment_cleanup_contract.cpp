// Host test executing production abandonment choices, staging path validation,
// Ultrahand boot hook cleanup, /startup.te ownership rules, and production cleanup
// error propagation / retry ordering via PerformAbandonCleanup.

#include "account/account_abandon.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

static auto ReadFileToString(const std::string& path) -> std::string {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        return {};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

namespace {

using namespace sphaira::account_restore;

struct MockAbandonFs : public AbandonFsOps {
    std::map<std::string, std::string> files;
    std::set<std::string> dirs;

    int64_t fail_startup_read{0};
    std::string fail_delete_file;
    bool fail_hook_write{false};
    std::string fail_delete_dir;
    int64_t fail_commit{0};

    auto DirExists(std::string_view p) -> bool override {
        return dirs.find(std::string(p)) != dirs.end();
    }
    auto FileExists(std::string_view p) -> bool override {
        return files.find(std::string(p)) != files.end();
    }
    auto ReadEntireFile(std::string_view p, std::vector<uint8_t>& out) -> int64_t override {
        if (p == "/startup.te" && fail_startup_read != 0) {
            return fail_startup_read;
        }
        auto it = files.find(std::string(p));
        if (it == files.end()) return 0x202;
        out.assign(it->second.begin(), it->second.end());
        return 0;
    }
    auto WriteEntireFile(std::string_view p, const std::vector<uint8_t>& data) -> int64_t override {
        if (p == "/switch/.packages/boot_package.ini" && fail_hook_write) {
            return 0x301;
        }
        files[std::string(p)] = std::string(data.begin(), data.end());
        return 0;
    }
    auto DeleteFile(std::string_view p) -> int64_t override {
        if (!fail_delete_file.empty() && fail_delete_file == p) {
            return 0x401;
        }
        files.erase(std::string(p));
        return 0;
    }
    auto DeleteDirectoryRecursively(std::string_view p) -> int64_t override {
        if (!fail_delete_dir.empty() && fail_delete_dir == p) {
            return 0x501;
        }
        dirs.erase(std::string(p));
        const auto prefix = std::string(p) + "/";
        for (auto it = files.begin(); it != files.end();) {
            if (it->first.starts_with(prefix)) {
                it = files.erase(it);
            } else {
                ++it;
            }
        }
        return 0;
    }
    auto Commit() -> int64_t override {
        return fail_commit;
    }
};

} // namespace

auto main() -> int {
    using namespace sphaira::account_restore;

    // 1. Choice resolution tests
    assert(ResolveAbandonChoice(std::nullopt) == AbandonChoice::Later);
    assert(ResolveAbandonChoice(0) == AbandonChoice::Later);
    assert(ResolveAbandonChoice(1) == AbandonChoice::CancelOperation);
    assert(ResolveAbandonChoice(2) == AbandonChoice::Retry);
    assert(ResolveAbandonChoice(99) == AbandonChoice::Later);
    assert(ResolveAbandonChoice(-1) == AbandonChoice::Later);

    // 2. Staging directory validation tests
    assert(IsSafeAbandonStagingDir("/config/kefir/nand_transfer/_staging_20261005_120000"));
    assert(IsSafeAbandonStagingDir("/config/kefir/nand_transfer/_restore_backup_20261005_120000"));
    assert(IsSafeAbandonStagingDir("sdmc:/config/kefir/nand_transfer/_staging_abc"));

    // Completed archives must NEVER be recognized as staging folders (MUST be preserved)
    assert(!IsSafeAbandonStagingDir("/config/kefir/nand_transfer/20261005_120000.kefir-nand.zip"));
    assert(!IsSafeAbandonStagingDir("/config/kefir/nand_transfer/backup.zip"));

    // Source backup and safety backup directories must NEVER be recognized as staging
    assert(!IsSafeAbandonStagingDir("/config/kefir/account_backups/user_profile"));
    assert(!IsSafeAbandonStagingDir("/config/kefir/safety_backup/op_1234"));
    assert(!IsSafeAbandonStagingDir("/config/kefir/safety_backup"));
    assert(!IsSafeAbandonStagingDir("/config/kefir/restore_pending"));

    // Dangerous/malformed/arbitrary path tests
    assert(!IsSafeAbandonStagingDir(""));
    assert(!IsSafeAbandonStagingDir("/"));
    assert(!IsSafeAbandonStagingDir("/config/kefir/nand_transfer"));
    assert(!IsSafeAbandonStagingDir("/config/kefir/nand_transfer/"));
    assert(!IsSafeAbandonStagingDir("/config/kefir/nand_transfer/../atmosphere"));
    assert(!IsSafeAbandonStagingDir("/config/kefir/nand_transfer/sub/_staging_nested"));
    assert(!IsSafeAbandonStagingDir("/config/kefir/nand_transfer\\_staging_win"));
    assert(!IsSafeAbandonStagingDir("/save/8000000000000010"));

    // 3. Ultrahand boot hook removal pure logic
    {
        const std::string original_ini =
            "[general]\n"
            "autoboot=true\n"
            "\n"
            "[on-boot]\n"
            "; other-user-hook-begin\n"
            "echo \"starting\"\n"
            "; other-user-hook-end\n"
            "; kefir-hub-reopen-begin\n"
            "try:\n"
            "path_exists /config/kefir/reopen_hub.flag\n"
            "notify-now \"Open Kefir Hub\" 26 center word 0 \"Kefir Hub\" false kefir\n"
            "delete /config/kefir/reopen_hub.flag\n"
            "; kefir-hub-reopen-end\n"
            "payload /bootloader/payloads/fusee.bin\n";

        const std::string expected_ini =
            "[general]\n"
            "autoboot=true\n"
            "\n"
            "[on-boot]\n"
            "; other-user-hook-begin\n"
            "echo \"starting\"\n"
            "; other-user-hook-end\n"
            "payload /bootloader/payloads/fusee.bin\n";

        const auto cleaned = RemoveUltrahandBootHookText(original_ini);
        assert(cleaned == expected_ini);
        assert(RemoveUltrahandBootHookText(expected_ini) == expected_ini);
        assert(RemoveUltrahandBootHookText("").empty());
    }

    // 4. Strict startup script ownership checks using actual production romfs script assets
    const auto dump_script = ReadFileToString("assets/romfs/tegra/nand_transfer_dump_auto.te");
    const auto restore_script = ReadFileToString("assets/romfs/tegra/nand_transfer_restore_auto.te");
    const auto dump0010_script = ReadFileToString("assets/romfs/tegra/account_0010_dump.te");
    const auto link_script = ReadFileToString("assets/romfs/tegra/account_0010_apply_link.te");
    assert(!dump_script.empty() && !restore_script.empty());
    assert(!dump0010_script.empty() && !link_script.empty());

    const std::vector<std::string> prod_candidates = {
        dump_script, restore_script, dump0010_script, link_script
    };

    assert(IsStartupTeContentOwned(dump_script, dump_script));
    assert(IsStartupTeContentOwned(dump_script, prod_candidates));
    assert(IsStartupTeContentOwned(restore_script, prod_candidates));
    assert(!IsStartupTeContentOwned(dump_script, restore_script));
    assert(!IsStartupTeContentOwned("", dump_script));
    assert(!IsStartupTeContentOwned(dump_script, ""));

    // Regression: Unrelated custom startup script mentioning a known script name in a comment MUST NOT be owned
    const std::string unrelated_script_with_comment =
        "# Custom bootloader script by user\n"
        "# Replaces account_0010_dump.te and nand_transfer_dump_auto.te\n"
        "# Has restore_pending/link/ and account save backup comments\n"
        "println(\"Launching custom payload...\")\n"
        "rc = payload(\"sd:/bootloader/payloads/fusee.bin\")\n";

    assert(!IsStartupTeContentOwned(unrelated_script_with_comment, dump_script));
    assert(!IsStartupTeContentOwned(unrelated_script_with_comment, dump0010_script));
    assert(!IsStartupTeContentOwned(unrelated_script_with_comment, prod_candidates));

    // 5. Production cleanup ordering and error propagation tests via PerformAbandonCleanup
    AbandonPendingInfo base_info;
    base_info.staging_dir = "/config/kefir/nand_transfer/_staging_20261005_120000";
    base_info.pack_dirs = { "/config/kefir/nand_transfer/_staging_extra" };
    base_info.pending_dir = "/config/kefir/restore_pending";

    const std::string boot_ini_with_hook =
        "[on-boot]\n"
        "; kefir-hub-reopen-begin\n"
        "delete /config/kefir/reopen_hub.flag\n"
        "; kefir-hub-reopen-end\n";

    auto populate_valid_fs = [&](MockAbandonFs& fs) {
        fs.dirs.insert(base_info.staging_dir);
        fs.dirs.insert("/config/kefir/nand_transfer/_staging_extra");
        fs.dirs.insert(base_info.pending_dir);
        fs.files[base_info.pending_dir + "/state.json"] = "{\"phase\":\"wait_nand_dump\"}";
        fs.files["/TegraExplorer/scripts/nand_transfer_dump_auto.te"] = dump_script;
        fs.files["/config/kefir/reopen_hub.flag"] = "flag";
        fs.files["/config/ultrahand/notifications/kefir-reopen.notify"] = "{}";
        fs.files["/switch/.packages/boot_package.ini"] = boot_ini_with_hook;
        fs.files["/startup.te"] = dump_script;
    };

    // 5a. Startup read failure propagates error and retains pending_dir
    {
        MockAbandonFs fs;
        populate_valid_fs(fs);
        fs.fail_startup_read = 0x501;

        const auto rc = PerformAbandonCleanup(fs, base_info, prod_candidates);
        assert(rc == 0x501);
        // Recovery context must be retained!
        assert(fs.DirExists(base_info.pending_dir));
        assert(fs.FileExists(base_info.pending_dir + "/state.json"));
        // /startup.te was NOT deleted because read failed
        assert(fs.FileExists("/startup.te"));
    }

    // 5b. Owned-script deletion failure propagates error and retains pending_dir
    {
        MockAbandonFs fs;
        populate_valid_fs(fs);
        fs.fail_delete_file = "/startup.te";

        const auto rc = PerformAbandonCleanup(fs, base_info, prod_candidates);
        assert(rc == 0x401);
        assert(fs.DirExists(base_info.pending_dir));
    }
    {
        MockAbandonFs fs;
        populate_valid_fs(fs);
        fs.fail_delete_file = "/TegraExplorer/scripts/nand_transfer_dump_auto.te";

        const auto rc = PerformAbandonCleanup(fs, base_info, prod_candidates);
        assert(rc == 0x401);
        assert(fs.DirExists(base_info.pending_dir));
    }

    // 5c. Notification & hook failure propagates error and retains pending_dir
    {
        MockAbandonFs fs;
        populate_valid_fs(fs);
        fs.fail_delete_file = "/config/kefir/reopen_hub.flag";

        const auto rc = PerformAbandonCleanup(fs, base_info, prod_candidates);
        assert(rc == 0x401);
        assert(fs.DirExists(base_info.pending_dir));
    }
    {
        MockAbandonFs fs;
        populate_valid_fs(fs);
        fs.fail_hook_write = true;

        const auto rc = PerformAbandonCleanup(fs, base_info, prod_candidates);
        assert(rc == 0x301);
        assert(fs.DirExists(base_info.pending_dir));
    }

    // 5d. Staging deletion failure propagates real error (not synthetic) and retains pending_dir
    {
        MockAbandonFs fs;
        populate_valid_fs(fs);
        fs.fail_delete_dir = base_info.staging_dir;

        const auto rc = PerformAbandonCleanup(fs, base_info, prod_candidates);
        assert(rc == 0x501);
        assert(fs.DirExists(base_info.pending_dir));
    }

    // 5e. Retry after partial cleanup:
    // First attempt partially succeeds but fails on /startup.te deletion.
    // Retry with error cleared cleans remaining artifacts and removes pending_dir.
    {
        MockAbandonFs fs;
        populate_valid_fs(fs);
        fs.fail_delete_file = "/startup.te";

        // Attempt 1: fails
        const auto rc1 = PerformAbandonCleanup(fs, base_info, prod_candidates);
        assert(rc1 == 0x401);
        assert(fs.DirExists(base_info.pending_dir));
        // Staging was already cleaned in attempt 1
        assert(!fs.DirExists(base_info.staging_dir));

        // Attempt 2 (retry): error cleared, missing files handled idempotently
        fs.fail_delete_file.clear();
        const auto rc2 = PerformAbandonCleanup(fs, base_info, prod_candidates);
        assert(rc2 == 0);
        assert(!fs.FileExists("/startup.te"));
        assert(!fs.DirExists(base_info.pending_dir));
    }

    // A prerequisite commit failure must keep the pending metadata for a retry.
    {
        MockAbandonFs fs;
        populate_valid_fs(fs);
        fs.fail_commit = 0x601;
        assert(PerformAbandonCleanup(fs, base_info, prod_candidates) == 0x601);
        assert(fs.DirExists(base_info.pending_dir));
        assert(fs.FileExists(base_info.pending_dir + "/state.json"));
        fs.fail_commit = 0;
        assert(PerformAbandonCleanup(fs, base_info, prod_candidates) == 0);
        assert(!fs.DirExists(base_info.pending_dir));
        assert(!fs.FileExists(base_info.pending_dir + "/state.json"));
    }

    // 5f. Successful idempotent cleanup
    {
        MockAbandonFs fs;
        populate_valid_fs(fs);

        // Run 1: everything cleaned
        const auto rc1 = PerformAbandonCleanup(fs, base_info, prod_candidates);
        assert(rc1 == 0);
        assert(!fs.DirExists(base_info.staging_dir));
        assert(!fs.DirExists(base_info.pending_dir));
        assert(!fs.FileExists("/startup.te"));
        assert(!fs.FileExists("/config/kefir/reopen_hub.flag"));

        // Run 2: idempotent cleanup on already-clean system succeeds
        const auto rc2 = PerformAbandonCleanup(fs, base_info, prod_candidates);
        assert(rc2 == 0);
    }

    // 5g. Unrelated /startup.te with known script name in comment is preserved, while pending_dir is cleaned
    {
        MockAbandonFs fs;
        populate_valid_fs(fs);
        fs.files["/startup.te"] = unrelated_script_with_comment;

        const auto rc = PerformAbandonCleanup(fs, base_info, prod_candidates);
        assert(rc == 0);
        // User startup script MUST be preserved!
        assert(fs.FileExists("/startup.te"));
        assert(fs.files["/startup.te"] == unrelated_script_with_comment);
        // Operation pending_dir MUST be cleaned
        assert(!fs.DirExists(base_info.pending_dir));
    }

    std::printf("ok  test_abandonment_cleanup_contract: all checks passed\n");
    return 0;
}
