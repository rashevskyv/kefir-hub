#pragma once

#include "ui/nvg_util.hpp"
#include "i18n.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "ui/menus/save_menu.hpp"
#include <string>
#include <functional>
#include <optional>
#include <atomic>
#include <mutex>
#include <vector>

namespace sphaira::ui::menu::save {

constexpr float TAB_BAR_X = 40.f;
constexpr float TAB_BAR_W = 1200.f;
constexpr float TAB_BAR_TOP = 94.f;
constexpr float TAB_BAR_H = 44.f;
constexpr float TAB_BAR_GAP = 4.f;
constexpr float TAB_BAR_COUNT = 3.f;
constexpr float TAB_ITEM_W = (TAB_BAR_W - TAB_BAR_GAP * (TAB_BAR_COUNT - 1.f)) / TAB_BAR_COUNT;
constexpr Vec4 TAB_BAR_RECT{TAB_BAR_X, TAB_BAR_TOP, TAB_BAR_W, TAB_BAR_H};

extern UEvent g_change_uevent;

// the backup library scan: ReadBackupEntries() on its own thread. the UI reads done/total/path
// while it runs and takes `out` only once `finished` is set.
struct Menu::BackupScanJob {
    const Menu* menu{};
    Thread thread{};
    bool started{};
    std::vector<Entry> out{};
    std::atomic<size_t> done{};
    std::atomic<size_t> total{};
    std::atomic<bool> cancel{};
    std::atomic<bool> finished{};
    std::mutex mutex{}; // guards path
    std::string path{};
};

inline auto FormatSaveTypeLabel(u8 data_type) -> std::string {
    if (data_type == FsSaveDataType_Account) {
        return "Account";
    }
    return i18n::get(GetSaveTypeLabel(data_type));
}

void PlanRestoreCreation(
    const Entry& group,
    const AccountUid& dest_uid,
    const fs::FsPath& archive_path,
    std::function<void(std::optional<Entry>)> cb);

} // namespace sphaira::ui::menu::save
