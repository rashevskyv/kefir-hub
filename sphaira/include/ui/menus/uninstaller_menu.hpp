#pragma once

#include "ui/menus/menu_base.hpp"
#include "ui/list.hpp"

#include <cstdint>
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
    u64 memory_bytes{};
    std::string description;
    std::string repository;
    std::string github_description;
};

enum class ModuleSort : u8 {
    Name,
    Running,
    Memory,
    Autostart,
};

// name of a sysmodule / homebrew program id, from its toolbox.json or the
// module catalog the Module Manager uses. empty when it isn't a known module.
auto GetModuleName(u64 program_id) -> std::string;

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
    void SortItems(u64 keep_program_id = 0);
    void ShowContextMenu();
    void ShowSortMenu();
    void ShowInfo();
    void ShowInfoBox(const ModuleItem& item);

private:
    std::vector<ModuleItem> m_items;
    s64 m_index{};
    std::unique_ptr<List> m_list;
    bool m_loaded{false};
    bool m_catalog_update_attempted{false};
    bool m_catalog_update_pending{false};
    ModuleSort m_sort{ModuleSort::Name};
    u64 m_ram_used{};
    u64 m_ram_total{};
    std::string m_error_message;
};

} // namespace sphaira::ui::menu::hats
