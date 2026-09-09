#pragma once

#include <functional>
#include <string>

namespace sphaira::ui::menu::users {

enum class NandLibraryMode {
    Manage,
    Restore,
};

void PromptNandPackRestore(
    const std::string& dir,
    bool save_00F0,
    std::function<void(const std::string& dir, bool restore_play_hours)> on_restore,
    std::function<void()> before_restore = nullptr);

void OpenNandPackLibrary(
    std::function<void(const std::string& dir, bool restore_play_hours)> on_restore,
    NandLibraryMode mode = NandLibraryMode::Manage);

void OpenRemoteNandTransfer(
    NandLibraryMode mode,
    std::function<void(const std::string& dir, bool restore_play_hours)> on_restore = nullptr,
    std::function<void()> on_refresh = nullptr);

} // namespace sphaira::ui::menu::users
