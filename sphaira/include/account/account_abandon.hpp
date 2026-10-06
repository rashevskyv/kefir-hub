#pragma once

#include "path_util.hpp"

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::account_restore {

enum class AbandonChoice {
    Cancel = 0,
    DontRemindAgain = 1,
    Retry = 2,
};

inline auto ResolveAbandonChoice(std::optional<int64_t> op) -> AbandonChoice {
    if (!op || (*op != 1 && *op != 2)) {
        return AbandonChoice::Cancel;
    }
    if (*op == 1) {
        return AbandonChoice::DontRemindAgain;
    }
    return AbandonChoice::Retry;
}

// Validates whether path is an abandoned staging directory owned by the operation.
// Strictly requires path to be a validated _restore_ or _staging_ child under /config/kefir/nand_transfer/.
// Arbitrary pack directories, safety backups, and completed .zip archives return false.
inline auto IsSafeAbandonStagingDir(std::string_view path) -> bool {
    return sphaira::path::IsSafeRestoreStagingDir(path) || sphaira::path::IsSafeBackupStagingDir(path);
}

// Surgically removes the Kefir-owned Ultrahand boot hook block, preserving other hooks/sections.
inline auto RemoveUltrahandBootHookText(std::string_view text) -> std::string {
    constexpr std::string_view kHookBegin = "; kefir-hub-reopen-begin";
    constexpr std::string_view kHookEnd = "; kefir-hub-reopen-end";

    const auto begin = text.find(kHookBegin);
    if (begin == std::string_view::npos) {
        return std::string(text);
    }
    const auto end = text.find(kHookEnd, begin);
    if (end == std::string_view::npos) {
        return std::string(text);
    }
    auto after = end + kHookEnd.size();
    while (after < text.size() && (text[after] == '\n' || text[after] == '\r')) {
        after++;
    }

    std::string result;
    result.reserve(text.size() - (after - begin));
    result.append(text.substr(0, begin));
    result.append(text.substr(after));
    return result;
}

// Normalizes script content for strict byte/text comparison (strips CR, trims trailing newlines/spaces).
inline auto NormalizeScript(std::string_view text) -> std::string {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (c != '\r') {
            out.push_back(c);
        }
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ' || out.back() == '\t')) {
        out.pop_back();
    }
    return out;
}

// Verifies whether actual /startup.te content strictly matches the production script content.
// Never uses substring matching, so unrelated user scripts mentioning script names in comments are preserved.
inline auto IsStartupTeContentOwned(std::string_view actual, std::string_view expected) -> bool {
    if (actual.empty() || expected.empty()) {
        return false;
    }
    return NormalizeScript(actual) == NormalizeScript(expected);
}

inline auto IsStartupTeContentOwned(std::string_view actual, const std::vector<std::string>& candidates) -> bool {
    for (const auto& c : candidates) {
        if (IsStartupTeContentOwned(actual, c)) {
            return true;
        }
    }
    return false;
}

// Minimal injected filesystem seam for production cleanup and connected host tests.
struct AbandonFsOps {
    virtual auto DirExists(std::string_view path) -> bool = 0;
    virtual auto FileExists(std::string_view path) -> bool = 0;
    virtual auto ReadEntireFile(std::string_view path, std::vector<uint8_t>& out) -> int64_t = 0;
    virtual auto WriteEntireFile(std::string_view path, const std::vector<uint8_t>& data) -> int64_t = 0;
    virtual auto DeleteFile(std::string_view path) -> int64_t = 0;
    virtual auto DeleteDirectoryRecursively(std::string_view path) -> int64_t = 0;
    virtual auto Commit() -> int64_t { return 0; }
    virtual ~AbandonFsOps() = default;
};

struct AbandonPendingInfo {
    std::string staging_dir;
    std::vector<std::string> pack_dirs;
    std::string pending_dir = "/config/kefir/restore_pending";
    std::vector<std::string> tegra_scripts = {
        "account_0010_dump.te",
        "account_0010_apply_link.te",
        "nand_transfer_restore_auto.te",
        "nand_transfer_dump_auto.te",
        "account_0010_rollback.te",
    };
};

// Executes the complete abandonment cleanup ritual with strict ordering:
// 1. Cleans validated operation-owned staging dirs (propagating filesystem errors).
// 2. Cleans operation-owned scripts in /TegraExplorer/scripts/.
// 3. Cleans reopen flags and Ultrahand boot hook (propagating read/write/delete errors).
// 4. Cleans /startup.te if and only if owned (propagating read errors; preserving unrelated user scripts).
// 5. Retains pending_dir until steps 1-4 succeed; deletes pending_dir only on full success so retries remain possible.
// 6. Commits filesystem changes and returns real filesystem result.
inline auto PerformAbandonCleanup(
    AbandonFsOps& fs,
    const AbandonPendingInfo& info,
    const std::vector<std::string>& candidate_startup_contents) -> int64_t
{
    auto clean_staging = [&](const std::string& path) -> int64_t {
        if (path.empty()) {
            return 0;
        }
        if (!IsSafeAbandonStagingDir(path)) {
            if (fs.DirExists(path)) {
                return 0x202;
            }
            return 0;
        }
        if (fs.DirExists(path)) {
            const auto rc = fs.DeleteDirectoryRecursively(path);
            if (rc != 0 && fs.DirExists(path)) {
                return rc;
            }
        }
        return 0;
    };

    if (const auto rc = clean_staging(info.staging_dir); rc != 0) {
        return rc;
    }
    for (const auto& pack : info.pack_dirs) {
        if (IsSafeAbandonStagingDir(pack)) {
            if (const auto rc = clean_staging(pack); rc != 0) {
                return rc;
            }
        }
    }

    for (const auto& name : info.tegra_scripts) {
        const std::string path = "/TegraExplorer/scripts/" + name;
        if (fs.FileExists(path)) {
            const auto rc = fs.DeleteFile(path);
            if (rc != 0 && fs.FileExists(path)) {
                return rc;
            }
        }
    }

    if (fs.FileExists("/config/kefir/reopen_hub.flag")) {
        const auto rc = fs.DeleteFile("/config/kefir/reopen_hub.flag");
        if (rc != 0 && fs.FileExists("/config/kefir/reopen_hub.flag")) {
            return rc;
        }
    }
    if (fs.FileExists("/config/ultrahand/notifications/kefir-reopen.notify")) {
        const auto rc = fs.DeleteFile("/config/ultrahand/notifications/kefir-reopen.notify");
        if (rc != 0 && fs.FileExists("/config/ultrahand/notifications/kefir-reopen.notify")) {
            return rc;
        }
    }
    if (fs.FileExists("/switch/.packages/boot_package.ini")) {
        std::vector<uint8_t> raw;
        const auto read_rc = fs.ReadEntireFile("/switch/.packages/boot_package.ini", raw);
        if (read_rc != 0) {
            return read_rc;
        }
        const std::string_view text(reinterpret_cast<const char*>(raw.data()), raw.size());
        const auto cleaned = RemoveUltrahandBootHookText(text);
        if (cleaned != text) {
            const std::vector<uint8_t> cleaned_bytes(cleaned.begin(), cleaned.end());
            const auto write_rc = fs.WriteEntireFile("/switch/.packages/boot_package.ini", cleaned_bytes);
            if (write_rc != 0) {
                return write_rc;
            }
        }
    }

    if (fs.FileExists("/startup.te")) {
        std::vector<uint8_t> raw;
        const auto read_rc = fs.ReadEntireFile("/startup.te", raw);
        if (read_rc != 0) {
            return read_rc;
        }
        const std::string_view actual(reinterpret_cast<const char*>(raw.data()), raw.size());
        if (IsStartupTeContentOwned(actual, candidate_startup_contents)) {
            const auto del_rc = fs.DeleteFile("/startup.te");
            if (del_rc != 0 && fs.FileExists("/startup.te")) {
                return del_rc;
            }
        }
    }

    // Keep retry metadata until prerequisite changes are committed.
    if (const auto rc = fs.Commit(); rc != 0) {
        return rc;
    }
    if (!info.pending_dir.empty() && fs.DirExists(info.pending_dir)) {
        const auto rc = fs.DeleteDirectoryRecursively(info.pending_dir);
        if (rc != 0 && fs.DirExists(info.pending_dir)) {
            return rc;
        }
    }

    return fs.Commit();
}

} // namespace sphaira::account_restore
