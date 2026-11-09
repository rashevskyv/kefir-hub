#pragma once

#include "ui/widget.hpp"
#include "ui/list.hpp"
#include "ui/scrolling_text.hpp"
#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace sphaira::ui {

// a scrollable list of on/off choices, drawn over the dim background like a
// PopupList. X marks a row and steps on, Y inverts (ui/marks.hpp), A or B close;
// the marks are reported back into `selected` as they change, not when it closes.
class PopupMultiSelect final : public Widget {
public:
    struct Item {
        std::string name{};
        std::string note{}; // drawn after the name, in info text colour.
        bool disabled{};    // greyed out and untouchable.
    };
    using Items = std::vector<Item>;
    using Callback = std::function<void()>;

public:
    PopupMultiSelect(const std::string& title, const Items& items, std::vector<u8>& selected, const Callback& cb = {});

    auto Update(Controller* controller, TouchInfo* touch) -> void override;
    auto Draw(NVGcontext* vg, Theme* theme) -> void override;
    auto OnFocusGained() noexcept -> void override;
    auto OnFocusLost() noexcept -> void override;

private:
    auto Toggle(s64 index) -> s64;
    void Invert();
    auto Mark() { return [this](std::size_t i) -> u8& { return m_selected[i]; }; }
    auto Mark() const { return [this](std::size_t i) { return m_selected[i] != 0; }; }
    auto Markable() const { return [this](std::size_t i) { return i < m_items.size() && !m_items[i].disabled; }; }
    auto GetSelectedCount() const -> s64;
    auto IsSelected(s64 index) const -> bool;
    void OnChanged();

private:
    static constexpr Vec2 m_title_pos{70.f, 28.f};
    // centred like a PopupList's rows and half as wide again, which is what a
    // name with a tag after it needs.
    static constexpr Vec4 m_block{140.f, 110.f, 1000.f, 60.f};
    static constexpr float m_text_xoffset{15.f};
    static constexpr float m_line_width{1220.f};

    const std::string m_title;
    const Items m_items;
    std::vector<u8>& m_selected;
    const Callback m_callback;
    s64 m_index{}; // the highlighted row, which A ticks.

    std::unique_ptr<List> m_list{};
    ScrollingText m_scroll_title{};
    ScrollingText m_scroll_text{};

    float m_line_top{};
    float m_line_bottom{};
};

} // namespace sphaira::ui
