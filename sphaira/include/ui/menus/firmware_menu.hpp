#pragma once

#include "ui/list.hpp"
#include "ui/menus/menu_base.hpp"
#include "fs.hpp"

#include <memory>
#include <string>
#include <vector>

namespace sphaira::ui::menu::firmware {

struct FirmwareEntry {
    std::string name;
    std::string url;
};

struct Menu final : MenuBase {
    Menu();
    ~Menu();

    auto GetShortTitle() const -> const char* override { return "Firmware"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

private:
    void FetchLinks();
    void SetIndex(s64 index);
    void DownloadSelected();
    void PromptInstallFirmware(const std::string& display_name, const fs::FsPath& path = "/firmware");
    void InstallFirmware(const std::string& display_name, const fs::FsPath& path = "/firmware");
    void UpdateSubheading();
    bool IsDowngrade(const std::string& target_version) const;

private:
    std::vector<FirmwareEntry> m_entries;
    s64 m_index{};
    std::unique_ptr<List> m_list;
    bool m_loading{};
    bool m_loaded{};
    std::string m_error_message;
    std::string m_current_firmware;
};

} // namespace sphaira::ui::menu::firmware
