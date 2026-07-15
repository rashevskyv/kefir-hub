#pragma once

#include "ui/menus/menu_base.hpp"
#include "ui/list.hpp"

#include <string>
#include <vector>

namespace sphaira::ui::menu::hats {

struct ModuleItem {
    u64 program_id{};
    std::string program_id_text;
    std::string name;
    bool requires_reboot{};
    bool running{};
    bool autostart{};
    std::string description;
};

struct UninstallerMenu final : MenuBase {
    UninstallerMenu();
    ~UninstallerMenu();

    auto GetShortTitle() const -> const char* override { return "Modules"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

private:
    void SetIndex(s64 index);
    void LoadModules();
    void RefreshStatuses();
    void ToggleSelectedModule();
    void ToggleSelectedAutostart();
    void UpdateSubheading();
    void RequestCatalogUpdate(bool force = false);

private:
    std::vector<ModuleItem> m_items;
    s64 m_index{};
    std::unique_ptr<List> m_list;
    bool m_loaded{false};
    bool m_catalog_update_attempted{false};
    bool m_catalog_update_pending{false};
    std::string m_error_message;
};

} // namespace sphaira::ui::menu::hats
