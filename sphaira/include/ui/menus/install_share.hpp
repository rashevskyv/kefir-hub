#pragma once

#include "ui/menus/menu_base.hpp"
#include "ui/list.hpp"
#include "ui/sidebar.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sphaira::ui::menu {

struct ConsoleTransferItem {
    std::string label;
    std::string description;
    std::function<void()> action;
};

struct ConsoleTransferMenu final : MenuBase {
    ConsoleTransferMenu();
    ~ConsoleTransferMenu() = default;

    auto GetShortTitle() const -> const char* override { return "Console Transfer"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

private:
    void SetIndex(s64 index);
    void OnSelect();

private:
    std::vector<ConsoleTransferItem> m_items;
    s64 m_index{};
    std::unique_ptr<List> m_list;
};

// Appends the "Install & Share" group (Web Server, MTP, PC Install over USB)
// to the given sidebar, preceded by a section header. Shared between the
// Tools and Homebrew menus so both can reach the web server / installer from
// their START menu.
void AddInstallShareOptions(Sidebar* sidebar);

// Appends a "Settings" shortcut that opens the Kefir Hub application settings,
// preceded by a section header.
void AddSettingsOption(Sidebar* sidebar);

// Starts the Console Transfer share server for user backups (/config/kefir/account_backups).
void StartConsoleTransferShareUserBackups();

// Starts the Console Transfer share server for profiles & play hours packs (/config/kefir/nand_transfer).
void StartConsoleTransferShareNandBackups();

// Prompts for a remote console IP address, tests HTTP connectivity on Console Transfer ports (8080..8090),
// and invokes on_connected with the responding base URL (e.g. "http://192.168.1.50:8080").
void ConnectConsoleTransfer(std::function<void(const std::string& base_url)> on_connected);

} // namespace sphaira::ui::menu
