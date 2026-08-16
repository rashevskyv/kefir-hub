#pragma once

#include "ui/widget.hpp"
#include "ui/scrolling_text.hpp"
#include "ui/list.hpp"
#include <optional>

namespace sphaira::ui {

class PopupList final : public Widget {
public:
    using Items = std::vector<std::string>;
    using Callback = std::function<void(std::optional<s64>)>;

public:
    explicit PopupList(std::string title, Items items, Callback cb, s64 index = 0);
    PopupList(std::string title, Items items, Callback cb, std::string index);
    PopupList(std::string title, Items items, std::string& index_str_ref, s64& index);
    PopupList(std::string title, Items items, std::string& index_ref);
    PopupList(std::string title, Items items, s64& index_ref);

    auto Update(Controller* controller, TouchInfo* touch) -> void override;
    auto Draw(NVGcontext* vg, Theme* theme) -> void override;
    auto OnFocusGained() noexcept -> void override;
    auto OnFocusLost() noexcept -> void override;
    auto IsModal() const -> bool override { return true; }

    // render as an action menu rather than a value chooser: no "current value"
    // tick on the starting row, a right chevron on every row hinting it opens a
    // further step.
    auto SetMenuStyle(bool menu) -> PopupList& { m_menu_style = menu; return *this; }

    // per-row cloud marker drawn in a reserved left gutter (used to flag
    // remote-origin backups). the gutter is reserved for ALL rows so the text
    // stays aligned; the cloud only appears on marked rows. size must match the
    // item count or it is ignored.
    auto SetRemoteMarkers(std::vector<bool> markers) -> PopupList& { m_markers = std::move(markers); return *this; }

    auto SetIcons(std::vector<std::optional<ActionIcon>> icons) -> PopupList& { m_icons = std::move(icons); return *this; }

private:
    void SetIndex(s64 index);

private:
    static constexpr Vec2 m_title_pos{70.f, 28.f};
    static constexpr Vec4 m_block{280.f, 110.f, SCREEN_HEIGHT, 60.f};
    static constexpr float m_text_xoffset{15.f};
    static constexpr float m_line_width{1220.f};

    std::string m_title{};
    Items m_items{};
    Callback m_callback{};
    s64 m_index{}; // index in list array
    s64 m_starting_index{};
    bool m_menu_style{};
    std::vector<bool> m_markers{};
    std::vector<std::optional<ActionIcon>> m_icons{};

    std::unique_ptr<List> m_list{};
    ScrollingText m_scroll_text{};

    float m_yoff{};
    float m_line_top{};
    float m_line_bottom{};
};

} // namespace sphaira::ui
