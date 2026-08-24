#pragma once

#include "ui/object.hpp"
#include <vector>
#include <memory>
#include <map>
#include <unordered_map>
#include <concepts>
#include <string_view>

namespace sphaira::ui::menu {
struct MenuBase;
} // namespace sphaira::ui::menu

namespace sphaira::ui {

struct uiButton final : Object {
    uiButton(Button button, const std::string& button_str, const std::string& action_str);
    uiButton(Button button, const std::string& action_str);

    auto Draw(NVGcontext* vg, Theme* theme) -> void override;

    Button m_button;
    std::string m_button_str;
    std::string m_action_str;
    Vec4 m_button_pos{};
    Vec4 m_hint_pos{};
    // font sizes the row was laid out at. The whole row shrinks together when
    // it would otherwise run off the left of the footer.
    float m_button_size{26.f};
    float m_hint_size{20.f};
};

struct Widget : public Object {
    using Actions = std::map<Button, Action>;
    using uiButtons = std::vector<uiButton>;

    virtual ~Widget() = default;

    virtual void Update(Controller* controller, TouchInfo* touch);
    virtual void Draw(NVGcontext* vg, Theme* theme);

    virtual void OnFocusGained() {
        m_focus = true;
    }

    virtual void OnFocusLost() {
        m_focus = false;
    }

    virtual auto HasFocus() const -> bool {
        return m_focus;
    }

    virtual auto IsMenu() const -> bool {
        return false;
    }

    virtual auto IsModal() const -> bool {
        return false;
    }

    // a full-screen ProgressBox owns the GPU for the frame; menus under it
    // skip Draw so the dialog can still take B/Stop. no RTTI (Switch is -fno-rtti).
    virtual auto BlocksDrawUnder() const -> bool {
        return false;
    }

    // USB mass-storage unplug: file browsers on that mount pop themselves.
    virtual void OnUsbMountRemoved(std::string_view) {}

    virtual auto WantsChrome() const -> bool {
        return true;
    }

    // The widget that owns the footer hint row on this one's behalf. A
    // container (MainMenu) hands it to the page it is currently showing, which
    // is what the user actually drives; everything else owns its own row.
    // Only one widget in the stack draws hints - see App::OwnsFooter().
    virtual auto GetFooterOwner() -> Widget* {
        return this;
    }

    // The menu whose header/footer chrome represents this widget. App draws it
    // as a top layer once the menu body is down, so no list row can paint over
    // the header or the footer. Containers forward to the page they show.
    virtual auto GetChromeOwner() -> menu::MenuBase* {
        return nullptr;
    }

    // Region this widget paints over opaquely. The menu underneath skips the
    // header chrome that falls inside it, so a side panel owns that corner
    // outright rather than letting the status block ghost through its
    // background. An empty rect (the default) occludes nothing.
    virtual auto GetChromeOcclusion() const -> Vec4 {
        return {};
    }

    auto HasAction(Button button) const -> bool;
    void SetAction(Button button, Action action);
    void SetActions(std::same_as<std::pair<Button, Action>> auto ...args) {
        const std::array list = {args...};
        for (const auto& [button, action] : list) {
            SetAction(button, action);
        }
    }

    // bulk insert. Goes through SetAction rather than touching m_actions, so
    // the laid out hint row is invalidated with it.
    void SetActions(const Actions& actions) {
        for (const auto& [button, action] : actions) {
            SetAction(button, action);
        }
    }

    auto GetActions() const {
        return m_actions;
    }

    void RemoveAction(Button button);

    void RemoveActions() {
        m_actions.clear();
        m_ui_buttons_dirty = true;
    }

    void RemoveActions(const Actions& actions) {
        for (auto& e : actions) {
            RemoveAction(e.first);
        }
    }

    auto FireAction(Button button, u8 type = ActionType::DOWN) -> bool;

    void SetPop(bool pop = true) {
        m_pop = pop;
    }

    auto ShouldPop() const -> bool {
        return m_pop;
    }

    auto SetUiButtonPos(Vec2 pos) {
        m_button_pos = pos;
        m_ui_buttons_dirty = true;
    }

    void SetUiButtonSort(bool sort = true) {
        m_sort_ui_buttons = sort;
        m_ui_buttons_dirty = true;
    }

    // The laid out hint row. Measuring it costs an nvgTextBounds per label and
    // per glyph, so it is kept until something that decides the layout changes:
    // the action set or the anchor. The font is loaded once at startup and the
    // hints are baked when the Action is built, so nothing else moves it.
    auto GetUiButtons() -> uiButtons&;
    static void SetupUiButtons(uiButtons& buttons, const Vec2& button_pos = {1220, 675});
    static auto GetUiButtons(const Actions& actions, const Vec2& button_pos = {1220, 675}, bool sort = false) -> uiButtons;

    Actions m_actions{};
    uiButtons m_ui_buttons{};
    bool m_ui_buttons_dirty{true};
    bool m_sort_ui_buttons{};
    Vec2 m_button_pos{1220, 675};
    bool m_focus{false};
    bool m_pop{false};
    Button m_pending_button{Button::NONE};
};

template<typename T>
concept DerivedFromWidget = std::is_base_of_v<Widget, T>;

} // namespace sphaira::ui
