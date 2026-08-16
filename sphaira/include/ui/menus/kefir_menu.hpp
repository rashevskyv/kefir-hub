#pragma once

#include "fs.hpp"
#include "option.hpp"
#include "ui/list.hpp"
#include "ui/menus/menu_base.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sphaira::ui::menu::kefir {

// how the downgrade fix (deleting system save 8000000000000073) is applied when
// installing a lower firmware. order matches the settings picker items.
enum DowngradeFixMode {
    DowngradeFixMode_Automatic, // apply automatically on a downgrade
    DowngradeFixMode_Optional,  // ask each time (Yes/No window)
    DowngradeFixMode_Off,       // never apply
};

enum class UpdaterEntryType {
    Section,
    Network,
    CustomLink,
    Kefir,
    Firmware,
    // "Install manually": opens the file browser as a folder picker so an
    // already-downloaded firmware dump on the SD card can be installed.
    FirmwareManual,
};

enum class UpdaterViewMode {
    List,
    Tiles,
};

struct UpdaterEntry {
    UpdaterEntryType type{};
    std::string name;
    std::string url;
    bool pack{};
};

struct Menu final : MenuBase {
    Menu();
    ~Menu();

    auto GetShortTitle() const -> const char* override { return "Updater"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

private:
    void FetchLinks();
    void SetIndex(s64 index);
    void OpenSelected();
    void DisplayOptions();
    void OnLayoutChange();
    void DrawList(NVGcontext* vg, Theme* theme);
    void DrawTiles(NVGcontext* vg, Theme* theme);
    void EnsureTileVisible();
    bool MoveTileSelection(s64 step);
    void InstallKefir(const UpdaterEntry& entry, std::function<void()> on_success = {});
    void DownloadFirmware(const UpdaterEntry& entry, bool skip_support_check = false);
    void OpenManualFirmwarePicker();
    // acked_downgrade_fix carries a downgrade already acknowledged before the
    // download started, so the warning is not shown a second time afterwards.
    void PromptInstallFirmware(const std::string& display_name, const fs::FsPath& path = "/firmware", std::optional<bool> acked_downgrade_fix = std::nullopt);
    // asks the downgrade warning + downgrade-fix question up front, then runs
    // on_ack with the chosen fix policy. returns false if not a downgrade.
    bool PromptDowngradeAck(const std::string& target_version, const std::string& confirm_label, std::function<void(bool)> on_ack);
    void StartFirmwareDownload(const UpdaterEntry& entry, std::optional<bool> acked_downgrade_fix);
    void InstallFirmware(const std::string& display_name, const fs::FsPath& path = "/firmware", bool apply_downgrade_fix = false);
    void UpdateSubheading();
    void RefreshSystemInfo();
    bool IsDowngrade(const std::string& target_version) const;
    bool IsFirmwareSupported(const std::string& target_version) const;
    bool IsKefirUpdate(const UpdaterEntry& entry) const;
    bool FindKefirUpdate(UpdaterEntry& out) const;
    void PromptKefirThenFirmware(const UpdaterEntry& firmware_entry);

private:
    std::vector<UpdaterEntry> m_entries;
    std::vector<s64> m_tile_entries;
    s64 m_index{};
    s64 m_tile_index{};
    std::unique_ptr<List> m_list;
    option::OptionLong m_view_mode{"updater", "view_mode", static_cast<s64>(UpdaterViewMode::List)};
    option::OptionLong m_downgrade_fix_mode{"updater", "downgrade_fix_mode", DowngradeFixMode_Optional};
    bool m_loading{};
    bool m_loaded{};
    bool m_retry_on_connect{};
    std::string m_error_message;
    std::string m_current_kefir;
    std::string m_current_firmware;
    std::string m_supported_firmware;
    std::string m_console_revision;
    std::string m_latest_kefir;
    // folder chosen in the manual-install file browser, consumed on refocus.
    std::optional<fs::FsPath> m_pending_manual_firmware;
};

} // namespace sphaira::ui::menu::kefir
