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
    void CopyActionsToCurrentMenu();
    void CheckForUpdate();
    static auto PageDotsRect() -> Vec4;
    auto SlideRest() const -> float; // m_slide of the current page at rest

    static constexpr int PEEK_FRAMES = 60;     // about 1 s at 60 fps
    static constexpr float PEEK_DIST = 180.f; // how far Tools shows at the peak

private:
    std::unique_ptr<MenuBase> m_centre_menu{};
    std::unique_ptr<MenuBase> m_tools_menu{};
    MenuBase* m_current_menu{};

    std::string m_update_url{};
    std::string m_update_version{};
    std::string m_update_description{};
    UpdateState m_update_state{UpdateState::Pending};
    bool m_launch_link_prompt_checked{};
    bool m_net_up{true}; // the network state seen last frame
    float m_slide{};        // 0 = the main screen shown, SCREEN_WIDTH = Tools shown; between = sliding
    bool m_dragging{};      // a sideways drag holds the page
    float m_drag_base{};    // m_slide when the drag began
    bool m_safe_mode_checked{}; // the Kefir safe-mode notice is looked at once per start
    bool m_peek_pending{}; // the first-run hint is up; when it closes, the page peeks at Tools
    int m_peek_frame{-1};  // frame of the peek animation, -1 = not running
};

} // namespace sphaira::ui::menu::main
