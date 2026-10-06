#pragma once

#include "path_util.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace sphaira::ui::menu::kefir::detail {

constexpr const char* DOWNLOAD_FIRMWARE_DEST = "/firmware";
constexpr const char* MANUAL_FIRMWARE_DEST = "/config/kefir-updater/firmware_manual";

// Pure predicate: whether CleanupFirmwareFiles should automatically remove staged files.
// A manual folder is NEVER automatically cleaned up, even if it is located at /firmware.
inline auto ShouldAutomaticCleanupFirmware(std::string_view path, bool is_manual_folder) -> bool {
    if (is_manual_folder) {
        return false;
    }
    std::string_view p = path.empty() ? DOWNLOAD_FIRMWARE_DEST : path;
    if (path::StartsWithIC(p, "sdmc:")) {
        p.remove_prefix(5);
    }
    const auto norm = path::NormalizeAbsoluteSdPath(p);
    if (!norm) {
        return false;
    }
    return (*norm == DOWNLOAD_FIRMWARE_DEST || *norm == MANUAL_FIRMWARE_DEST);
}

// Pure predicate: whether the downloaded firmware archive should be removed during automatic cleanup.
inline auto ShouldAutomaticCleanupZip(std::string_view path, bool is_manual_folder) -> bool {
    if (is_manual_folder) {
        return false;
    }
    std::string_view p = path.empty() ? DOWNLOAD_FIRMWARE_DEST : path;
    if (path::StartsWithIC(p, "sdmc:")) {
        p.remove_prefix(5);
    }
    const auto norm = path::NormalizeAbsoluteSdPath(p);
    return norm && *norm == DOWNLOAD_FIRMWARE_DEST;
}

// Pure predicate: determines if manual folder cleanup prompt should be presented.
// Only after confirmed successful manual folder installation.
inline auto ShouldPromptManualFolderCleanup(bool is_manual_folder, bool install_succeeded, bool is_cancelled) -> bool {
    return is_manual_folder && install_succeeded && !is_cancelled;
}

// Decision helper on prompt response:
// Returns true if deletion should be executed; Keep (0), B/dismissal, or cancelled return false.
inline auto ShouldExecuteFolderDeletion(std::optional<std::int64_t> op_index) -> bool {
    return op_index.has_value() && *op_index == 1;
}

// Validates a candidate manual firmware folder for installation or deletion:
// - Must be an absolute SD path (optional "sdmc:" prefix allowed)
// - Must not be empty, root, or a root alias ("/", "sdmc:/", "//")
// - Rejects traversal components ('.', '..'), '\\', colons (unsupported devices), and control characters
// - Returns normalized absolute SD path without trailing slash
inline auto ValidateManualFirmwareFolder(std::string_view path) -> std::optional<std::string> {
    return path::SdFolderFromConfigValue(path);
}

} // namespace sphaira::ui::menu::kefir::detail
