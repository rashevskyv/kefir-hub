#pragma once

#include "fs.hpp"
#include "ui/list.hpp"
#include "ui/menus/menu_base.hpp"

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
    void InstallKefir(const UpdaterEntry& entry);
    void DownloadFirmware(const UpdaterEntry& entry);
    void PromptInstallFirmware(const std::string& display_name, const fs::FsPath& path = "/firmware");
    void InstallFirmware(const std::string& display_name, const fs::FsPath& path = "/firmware");
    void UpdateSubheading();
    void RefreshSystemInfo();
    bool IsDowngrade(const std::string& target_version) const;

private:
    std::vector<UpdaterEntry> m_entries;
    s64 m_index{};
    std::unique_ptr<List> m_list;
    bool m_loading{};
    bool m_loaded{};
    std::string m_error_message;
    std::string m_current_kefir;
    std::string m_current_firmware;
    std::string m_console_revision;
    std::string m_latest_kefir;
};

} // namespace sphaira::ui::menu::kefir
