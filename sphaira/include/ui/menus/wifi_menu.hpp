#pragma once

#include "ui/menus/grid_menu_base.hpp"
#include "ui/list.hpp"
#include "wifi_manager.hpp"

#include <memory>
#include <string>
#include <vector>

namespace sphaira::ui::menu::wifi {

struct Menu final : grid::Menu {
    Menu();
    ~Menu() override;

    auto GetShortTitle() const -> const char* override { return "Wi-Fi"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

private:
    struct Item : sphaira::wifi::WifiProfile {
    };

    void SetIndex(s64 index);
    void Refresh();
    void OnLayoutChange();
    void ShowContextMenu();
    void ToggleCurrentSelection();
    void InvertSelection();
    void ClearSelection();
    void SelectAll();
    auto SelectedProfiles() const -> std::vector<sphaira::wifi::WifiProfile>;

    void ConfirmConnect(const sphaira::wifi::WifiProfile& profile);
    void ConfirmDeleteSingle(const sphaira::wifi::WifiProfile& profile);
    void ConfirmDeleteBatch();
    void ConfirmRename(const sphaira::wifi::WifiProfile& profile);
    void ConfirmChangePassword(const sphaira::wifi::WifiProfile& profile);
    void ConfirmChangeSsid(const sphaira::wifi::WifiProfile& profile);
    void ShowDetails(const sphaira::wifi::WifiProfile& profile);

    void DrawWifiIcon(NVGcontext* vg, Theme* theme, const Vec4& v, bool is_connected, bool selected);

    std::vector<Item> m_items{};
    s64 m_index{0};
    s64 m_selected_count{0};
    std::unique_ptr<List> m_list{};

    bool m_connecting{false};
    Uuid m_connecting_uuid{};
    std::string m_connecting_name{};
    TimeStamp m_connect_ts{};
};

} // namespace sphaira::ui::menu::wifi
