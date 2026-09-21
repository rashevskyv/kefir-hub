#pragma once

#include "ui/widget.hpp"
#include "ui/menus/menu_base.hpp"

namespace sphaira::ui::menu::main {

enum class UpdateState {
    // still downloading json from github
    Pending,
    // no update available.
    None,
    // update available!
    Update,
    // there was an error whilst checking for updates.
    Error,
};

// this holds the Homebrew and Tools pages and allows switching between them.
struct MainMenu final : Widget {
    MainMenu();
    ~MainMenu();

    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;
    void OnFocusLost() override;

    auto IsMenu() const -> bool override {
        return true;
    }

    auto IsMainScreen() const -> bool override;
    void OpenMainScreen() override;

    // this is a shell around the page it is showing: that page draws the body,
    // owns the chrome and owns the hint row (MainMenu copies its own L/R tab
    // actions into it rather than drawing a second row).
    auto GetFooterOwner() -> Widget* override {
        return m_current_menu;
    }

    auto GetChromeOwner() -> MenuBase* override {
        return m_current_menu;
    }

private:
    void SwitchTo(MenuBase* menu);
    void AddOnLRPress();
    void UpdateBackAction();

private:
    std::unique_ptr<MenuBase> m_centre_menu{};
    std::unique_ptr<MenuBase> m_tools_menu{};
    MenuBase* m_current_menu{};

    std::string m_update_url{};
    std::string m_update_version{};
    std::string m_update_description{};
    UpdateState m_update_state{UpdateState::Pending};
    bool m_launch_link_prompt_checked{};
};

} // namespace sphaira::ui::menu::main
