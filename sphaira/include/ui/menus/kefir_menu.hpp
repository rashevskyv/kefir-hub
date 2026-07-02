#pragma once

#include "fs.hpp"
#include "option.hpp"
#include "ui/list.hpp"
#include "ui/menus/menu_base.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sphaira::ui::menu::kefir {

enum class UpdaterEntryType {
    Section,
    Network,
    CustomLink,
    Kefir,
    Firmware,
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
    void PromptInstallFirmware(const std::string& display_name, const fs::FsPath& path = "/firmware");
    void InstallFirmware(const std::string& display_name, const fs::FsPath& path = "/firmware", bool apply_downgrade_fix = false);
    void UpdateSubheading();
    void RefreshSystemInfo();
    bool IsDowngrade(const std::string& target_version) const;
    bool IsFirmwareSupported(const std::string& target_version) const;
    bool FindKefirUpdate(UpdaterEntry& out) const;
    void PromptKefirThenFirmware(const UpdaterEntry& firmware_entry);

private:
    std::vector<UpdaterEntry> m_entries;
    std::vector<s64> m_tile_entries;
    s64 m_index{};
    s64 m_tile_index{};
    std::unique_ptr<List> m_list;
    option::OptionLong m_view_mode{"updater", "view_mode", static_cast<s64>(UpdaterViewMode::List)};
    bool m_loading{};
    bool m_loaded{};
    std::string m_error_message;
    std::string m_current_kefir;
    std::string m_current_firmware;
    std::string m_supported_firmware;
    std::string m_console_revision;
    std::string m_latest_kefir;
};

} // namespace sphaira::ui::menu::kefir
