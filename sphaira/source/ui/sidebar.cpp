#include "ui/sidebar.hpp"
#include "ui/layout.hpp"
#include "ui/menus/file_picker.hpp"
#include "app.hpp"
#include "ui/popup_list.hpp"
#include "ui/nvg_util.hpp"
#include "i18n.hpp"
#include "swkbd.hpp"
#include <algorithm>

namespace sphaira::ui {
namespace {

constexpr float SIDEBAR_TOP_PAD = 10.f;

auto DisabledTextColour() -> NVGcolor {
    return nvgRGBA(135, 138, 148, 255);
}

} // namespace

SidebarEntryBase::SidebarEntryBase(const std::string& title, const std::string& info)
: m_title{title}, m_info{info} {

}

void SidebarEntryBase::Draw(NVGcontext* vg, Theme* theme, const Vec4& root_pos, bool left) {
    // draw spacers or highlight box if in focus (selected)
    if (HasFocus()) {
        gfx::drawRectOutline(vg, theme, 4.f, m_pos);

        const auto& info = IsEnabled() ? m_info : m_depends_info;

        if (!info.empty()) {
            // the box is drawn beside the panel, outside the list's clip, so
            // reset the scissor - but only to the content band: a long info
            // text must be cut at the footer, not painted across it.
            nvgSave(vg);
            nvgScissor(vg, 0, layout::CONTENT_TOP + SIDEBAR_TOP_PAD, SCREEN_WIDTH, layout::CONTENT_HEIGHT - SIDEBAR_TOP_PAD);
            ON_SCOPE_EXIT(nvgRestore(vg));

            Vec4 info_box{};
            info_box.y = layout::CONTENT_TOP + SIDEBAR_TOP_PAD;
            info_box.w = 400;

            if (left) {
                info_box.x = root_pos.x + root_pos.w + 10;
            } else {
                info_box.x = root_pos.x - info_box.w - 10;
            }

            const float info_pad = 30;
            const float title_font_size = 18;
            const float info_font_size = 18;
            const float pad_after_title = title_font_size + info_pad;
            const float x = info_box.x + info_pad;
            const auto end_w = info_box.w - info_pad * 2;

            float bounds[4];
            nvgFontSize(vg, info_font_size);
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            nvgTextLineHeight(vg, 1.7);
            nvgTextBoxBounds(vg, 0, 0, end_w, info.c_str(), nullptr, bounds);
            info_box.h = pad_after_title + info_pad * 2 + bounds[3] - bounds[1];

            gfx::drawRect(vg, info_box, theme->GetColour(ThemeEntryID_SIDEBAR), 5);

            float y = info_box.y + info_pad;
            m_scolling_title.Draw(vg, true, x, y, end_w, title_font_size, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), m_title.c_str(), true);

            y += pad_after_title;
            gfx::drawTextBox(vg, x, y, info_font_size, end_w, theme->GetColour(ThemeEntryID_TEXT), info.c_str());
        }
    }
}

auto SidebarEntryBase::OnFocusGained() noexcept -> void {
    Widget::OnFocusGained();
}

auto SidebarEntryBase::OnFocusLost() noexcept -> void {
    Widget::OnFocusLost();
    m_scolling_title.Reset();
    m_scolling_value.Reset();
}

void SidebarEntryBase::DrawEntry(NVGcontext* vg, Theme* theme, const std::string& left, const std::string& right, bool use_selected) {
    const auto colour = IsEnabled() ? theme->GetColour(ThemeEntryID_TEXT) : DisabledTextColour();
    const auto value_colour = IsEnabled() ? theme->GetColour(use_selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT) : DisabledTextColour();

    const float pad = 15.f;
    const float gap = 10.f;
    const float usable_w = m_pos.w - pad * 2.f;
    const float mid_y = m_pos.y + (m_pos.h / 2.f);

    nvgFontSize(vg, 20);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    float label_bounds[4];
    nvgTextBounds(vg, 0, 0, left.c_str(), nullptr, label_bounds);
    const float label_w = label_bounds[2];
    float value_bounds[4];
    nvgTextBounds(vg, 0, 0, right.c_str(), nullptr, value_bounds);
    const float value_w = value_bounds[2];

    // wrapping onto a second line is the last resort, used only when BOTH
    // sides are individually so long (over 3/4 of the row each) that
    // neither could reasonably sit beside the other. A long label with a
    // short value ("Auto-sync after backup: On") must stay on one line -
    // the label simply takes whatever the value doesn't need and scrolls
    // on focus if that still isn't enough. Same the other way around.
    if (label_w > usable_w * 0.75f && value_w > usable_w * 0.75f) {
        const float line_gap = 24.f;
        const float top_y = mid_y - line_gap / 2.f;
        const float bottom_y = mid_y + line_gap / 2.f;

        m_scolling_title.Draw(vg, HasFocus(), m_pos.x + pad, top_y, usable_w, 20.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, colour, left, true);
        // each line scrolls on focus if even the full row width isn't enough.
        m_scolling_value.Draw(vg, HasFocus(), m_pos.x + pad, bottom_y, usable_w, 20.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, value_colour, right);
        return;
    }

    // single line: the shorter side keeps its natural width (capped at 3/4
    // so the longer side always keeps at least a quarter of the row), the
    // longer side takes the remainder and scrolls on focus if it overflows.
    // for the common short-value case ("On"/"Off", a checkbox glyph) this is
    // identical to the old look: value flush right, label gets the rest.
    const float side_cap = usable_w * 0.75f;
    const float side_gap = right.empty() ? 0.f : gap;
    float left_w, right_w;
    if (label_w <= value_w) {
        left_w = std::min(label_w, side_cap);
        right_w = std::max(0.f, std::min(value_w, usable_w - left_w - side_gap));
    } else {
        right_w = std::min(value_w, side_cap);
        left_w = std::max(0.f, std::min(label_w, usable_w - right_w - side_gap));
    }
    const float right_x = m_pos.x + m_pos.w - pad - right_w;

    m_scolling_value.Draw(vg, HasFocus(), right_x, mid_y, right_w, 20.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, value_colour, right);
    m_scolling_title.Draw(vg, HasFocus(), m_pos.x + pad, mid_y, left_w, 20.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, colour, left, true);
}

SidebarEntryBool::SidebarEntryBool(const std::string& title, bool option, Callback cb, const std::string& info, const std::string& true_str, const std::string& false_str)
: SidebarEntryBase{title, info}
, m_option{option}
, m_callback{cb}
, m_true_str{true_str}
, m_false_str{false_str} {

    if (m_true_str == "On") {
        m_true_str = i18n::get(m_true_str);
    }
    if (m_false_str == "Off") {
        m_false_str = i18n::get(m_false_str);
    }

    SetAction(Button::A, Action{"OK"_i18n, [this](){
        if (!IsEnabled()) {
            DependsClick();
        } else {
            m_option ^= 1;
            m_callback(m_option);
        } }
    });
}

SidebarEntryBool::SidebarEntryBool(const std::string& title, bool& option, const std::string& info, const std::string& true_str, const std::string& false_str)
: SidebarEntryBool{title, option, Callback{}, info, true_str, false_str} {
    m_callback = [&option](bool&){
        option ^= 1;
    };
}

SidebarEntryBool::SidebarEntryBool(const std::string& title, option::OptionBool& option, const Callback& cb, const std::string& info, const std::string& true_str, const std::string& false_str)
: SidebarEntryBool{title, option.Get(), Callback{}, info, true_str, false_str} {
    m_callback = [&option, cb](bool& v_out){
        if (cb) {
            cb(v_out);
        }
        option.Set(v_out);
    };
}

SidebarEntryBool::SidebarEntryBool(const std::string& title, option::OptionBool& option, const std::string& info, const std::string& true_str, const std::string& false_str)
: SidebarEntryBool{title, option, Callback{}, info, true_str, false_str} {
}

void SidebarEntryBool::Draw(NVGcontext* vg, Theme* theme, const Vec4& root_pos, bool left) {
    SidebarEntryBase::Draw(vg, theme, root_pos, left);
    SidebarEntryBase::DrawEntry(vg, theme, m_title, m_option ? m_true_str : m_false_str, m_option);
}

SidebarEntryCheckbox::SidebarEntryCheckbox(const std::string& title, Getter getter, Callback cb, const std::string& info)
: SidebarEntryBase{title, info}
, m_getter{getter}
, m_callback{cb} {
    SetAction(Button::A, Action{"OK"_i18n, [this](){
        if (!IsEnabled()) {
            DependsClick();
        } else if (m_callback) {
            m_callback(!m_getter());
        }
    }});
}

void SidebarEntryCheckbox::Draw(NVGcontext* vg, Theme* theme, const Vec4& root_pos, bool left) {
    SidebarEntryBase::Draw(vg, theme, root_pos, left);
    SidebarEntryBase::DrawEntry(vg, theme, m_title, m_getter() ? "\uE14B" : "", m_getter());
}

SidebarEntryHeader::SidebarEntryHeader(const std::string& title, const std::string& info)
: SidebarEntryBase{title, info} {
}

void SidebarEntryHeader::Draw(NVGcontext* vg, Theme* theme, const Vec4& root_pos, bool left) {
    SidebarEntryBase::Draw(vg, theme, root_pos, left);

    // headers sit a step above the 20px entry titles so the grouping reads
    // as a section label rather than another option.
    gfx::drawTextBold(
        vg,
        m_pos.x + 15.f, m_pos.y + (m_pos.h / 2.f) + 10.f,
        24.f,
        theme->GetColour(ThemeEntryID_TEXT_SELECTED),
        m_title.c_str(),
        NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE
    );
}

SidebarEntryCallback::SidebarEntryCallback(const std::string& title, Callback cb, bool pop_on_click, const std::string& info)
: SidebarEntryBase{title, info}
, m_callback{cb}
, m_pop_on_click{pop_on_click} {
    SetAction(Button::A, Action{"OK"_i18n, [this](){
        if (!IsEnabled()) {
            DependsClick();
        } else {
            m_callback();
            if (m_pop_on_click) {
                SetPop();
            }
        }}
    });
}

SidebarEntryCallback::SidebarEntryCallback(const std::string& title, Callback cb, const std::string& info)
: SidebarEntryCallback{title, cb, false, info} {

}

void SidebarEntryCallback::Draw(NVGcontext* vg, Theme* theme, const Vec4& root_pos, bool left) {
    SidebarEntryBase::Draw(vg, theme, root_pos, left);

    const auto colour = IsEnabled() ? theme->GetColour(ThemeEntryID_TEXT) : DisabledTextColour();
    const float x = m_pos.x + 15.f;
    const float y = m_pos.y + (m_pos.h / 2.f);
    float max_w = m_pos.w - 30.f;

    if (m_has_submenu) {
        max_w -= 20.f;
        const float x1 = m_pos.x + m_pos.w - 24.f;
        const float y1 = y;
        nvgBeginPath(vg);
        nvgMoveTo(vg, x1 - 8.f, y1 - 8.f);
        nvgLineTo(vg, x1, y1);
        nvgLineTo(vg, x1 - 8.f, y1 + 8.f);
        nvgStrokeColor(vg, colour);
        nvgStrokeWidth(vg, 3.f);
        nvgLineCap(vg, NVG_ROUND);
        nvgLineJoin(vg, NVG_ROUND);
        nvgStroke(vg);
    }

    m_scolling_entry_title.Draw(vg, HasFocus(), x, y, max_w, 20.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, colour, m_title, true);
}

auto SidebarEntryCallback::OnFocusLost() noexcept -> void {
    SidebarEntryBase::OnFocusLost();
    m_scolling_entry_title.Reset();
}

SidebarEntryArray::SidebarEntryArray(const std::string& title, const Items& items, std::string& index, const std::string& info)
: SidebarEntryArray{title, items, Callback{}, 0, info} {

    const auto it = std::find(m_items.cbegin(), m_items.cend(), index);
    if (it != m_items.cend()) {
        m_index = std::distance(m_items.cbegin(), it);
    }

    m_list_callback = [&index, this]() {
        App::Push<PopupList>(
            m_title, m_items, index, m_index
        );
    };
}

SidebarEntryArray::SidebarEntryArray(const std::string& title, const Items& items, Callback cb, const std::string& index, const std::string& info)
: SidebarEntryArray{title, items, cb, 0, info} {

    const auto it = std::find(m_items.cbegin(), m_items.cend(), index);
    if (it != m_items.cend()) {
        m_index = std::distance(m_items.cbegin(), it);
    }
}

SidebarEntryArray::SidebarEntryArray(const std::string& title, const Items& items, Callback cb, s64 index, const std::string& info)
: SidebarEntryBase{title, info}
, m_items{items}
, m_callback{cb}
, m_index{index} {

    m_list_callback = [this]() {
        App::Push<PopupList>(
            m_title, m_items, [this](auto op_idx){
                if (op_idx) {
                    m_index = *op_idx;
                    m_callback(m_index);
                }
            }, m_index
        );
    };

    SetAction(Button::A, Action{"OK"_i18n, [this](){
        if (!IsEnabled()) {
            DependsClick();
        } else {
            if (m_items.size() == 2) {
                m_index = (m_index + 1) % 2;
                m_callback(m_index);
            } else {
                m_list_callback();
            }
        }}
    });
}

void SidebarEntryArray::Draw(NVGcontext* vg, Theme* theme, const Vec4& root_pos, bool left) {
    SidebarEntryBase::Draw(vg, theme, root_pos, left);
    SidebarEntryBase::DrawEntry(vg, theme, m_title, m_items[m_index], true);
}

SidebarEntryTextBase::SidebarEntryTextBase(const std::string& title, const std::string& value, const Callback& cb, const std::string& info)
: SidebarEntryBase{title, info}
, m_value{value}
, m_callback{cb} {
    SetAction(Button::A, Action{"OK"_i18n, [this](){
        if (m_callback) {
            m_callback();
        }
    }});
}

void SidebarEntryTextBase::Draw(NVGcontext* vg, Theme* theme, const Vec4& root_pos, bool left) {
    SidebarEntryBase::Draw(vg, theme, root_pos, left);
    SidebarEntryBase::DrawEntry(vg, theme, m_title, m_value, true);
}

SidebarEntryTextInput::SidebarEntryTextInput(const std::string& title, const std::string& value, const std::string& guide, s64 len_min, s64 len_max, const std::string& info)
: SidebarEntryTextBase{title, value, {}, info}
, m_guide{guide}
, m_len_min{len_min}
, m_len_max{len_max} {

    SetCallback([this](){
        std::string out;
        if (R_SUCCEEDED(swkbd::ShowText(out, m_guide.c_str(), GetValue().c_str(), m_len_min, m_len_max))) {
            SetValue(out);
        }
    });
}

SidebarEntryFilePicker::SidebarEntryFilePicker(const std::string& title, const std::string& value, const std::vector<std::string>& filter, const std::string& info)
: SidebarEntryTextBase{title, value, {}, info}, m_filter{filter} {

    SetCallback([this](){
        App::Push<menu::filepicker::Menu>(
            [this](const fs::FsPath& path) {
                SetValue(path);
                return true;
            },
            m_filter
        );
    });
}

Sidebar::Sidebar(const std::string& title, Side side, Items&& items)
: Sidebar{title, "", side, std::forward<decltype(items)>(items)} {
}

Sidebar::Sidebar(const std::string& title, Side side)
: Sidebar{title, "", side, {}} {
}

Sidebar::Sidebar(const std::string& title, const std::string& sub, Side side, Items&& items)
: m_title{title}
, m_sub{sub}
, m_side{side}
, m_items{std::forward<decltype(items)>(items)} {
    switch (m_side) {
        case Side::LEFT:
            SetPos(Vec4{0.f, 0.f, 450.f, SCREEN_HEIGHT});
            break;

        case Side::RIGHT:
            SetPos(Vec4{SCREEN_WIDTH - 450.f, 0.f, 450.f, SCREEN_HEIGHT});
            break;
    }

    // the panel's own separators sit on the shared header/footer lines, so the
    // bands read as continuous across the panel edge.
    m_top_bar = Vec4{m_pos.x + 15.f, layout::HEADER_LINE_Y, m_pos.w - 30.f, 1.f};
    m_bottom_bar = Vec4{m_pos.x + 15.f, layout::FOOTER_LINE_Y, m_pos.w - 30.f, 1.f};
    m_title_pos = Vec2{m_pos.x + 30.f, m_pos.y + 40.f};

    // Rows are anchored to the top of the content band rather than floated
    // below it. Scrolling moves the list by exactly one row height, so with the
    // origins aligned a row that scrolls out lands wholly outside the band
    // instead of leaving its text stranded on the header separator. Anchoring
    // also reclaims the 34px gap the old +120 origin left, fitting a 7th row.
    const Vec4 pos{m_pos.x + 15.f, layout::CONTENT_TOP + SIDEBAR_TOP_PAD, m_pos.w - 30.f, layout::CONTENT_HEIGHT - SIDEBAR_TOP_PAD};
    m_base_pos = Vec4{GetX() + 30.f, pos.y, m_pos.w - (30.f * 2.f), 70.f};

    // set button positions
    SetUiButtonPos({m_pos.x + m_pos.w - 60.f, layout::BUTTON_POS.y});

    m_list = std::make_unique<List>(1, 7, pos, m_base_pos);
    m_list->SetWrap(true);
    m_list->SetScrollBarPos(GetX() + GetW() - 20, pos.y, pos.h);
}

Sidebar::Sidebar(const std::string& title, const std::string& sub, Side side)
: Sidebar{title, sub, side, {}} {
}


auto Sidebar::Update(Controller* controller, TouchInfo* touch) -> void {
    Widget::Update(controller, touch);

    // if touched out of bounds, pop the sidebar and all widgets below it.
    if (touch->is_clicked && !touch->in_range(GetPos())) {
        App::PopToMenu();
    } else {
        m_list->OnUpdate(controller, touch, m_index, m_items.size(), [this, controller](bool touch, auto i) {
            // a touch on a group header selects nothing.
            if (touch && !IsFocusable(i)) {
                return;
            }

            if (!touch) {
                // skip past headers in the direction the user actually moved,
                // taken from the button rather than from where the index
                // landed: after a wrap the index alone is ambiguous (moving up
                // off the top focusable lands on index 0, same as moving down
                // and wrapping there), which used to strand the cursor and
                // block the top->bottom wrap. SkipUnfocusable wraps too, so a
                // header at either end carries on around.
                bool forward;
                if (controller->GotDown(Button::R2)) {
                    // jump to the end, then step back onto the last real entry.
                    forward = false;
                } else if (controller->GotDown(Button::L2)) {
                    // jump to the start, then step onto the first real entry.
                    forward = true;
                } else if (controller->GotDown(Button::DOWN) || controller->GotDown(Button::RIGHT) || controller->GotDown(Button::R)) {
                    forward = true;
                } else if (controller->GotDown(Button::UP) || controller->GotDown(Button::LEFT) || controller->GotDown(Button::L)) {
                    forward = false;
                } else {
                    // touch drag / unknown source: resolve away from the ends.
                    const s64 last = (s64)m_items.size() - 1;
                    forward = !i ? true : (i == last ? false : i > m_index);
                }

                i = SkipUnfocusable(i, forward);
            }

            SetIndex(i);
            if (touch) {
                FireAction(Button::A);
            }
        }, this);
    }

    if (m_items[m_index]->ShouldPop()) {
        SetPop();
    }
}

auto Sidebar::Draw(NVGcontext* vg, Theme* theme) -> void {
    gfx::drawRect(vg, m_pos, theme->GetColour(ThemeEntryID_SIDEBAR));
    gfx::drawText(vg, m_title_pos, m_title_size, theme->GetColour(ThemeEntryID_TEXT), m_title.c_str());
    if (!m_sub.empty()) {
        gfx::drawTextArgs(vg, m_pos.x + m_pos.w - 30.f, m_title_pos.y + 10.f, 16, NVG_ALIGN_TOP | NVG_ALIGN_RIGHT, theme->GetColour(ThemeEntryID_TEXT_INFO), m_sub.c_str());
    }
    gfx::drawRect(vg, m_top_bar, theme->GetColour(ThemeEntryID_LINE));
    gfx::drawRect(vg, m_bottom_bar, theme->GetColour(ThemeEntryID_LINE));

    Widget::Draw(vg, theme);

    m_list->Draw(vg, theme, m_items.size(), [this](auto* vg, auto* theme, auto v, auto i) {
        const auto& [x, y, w, h] = v;

        if (i != m_items.size() - 1) {
            gfx::drawRect(vg, x, y + h, w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
        }

        m_items[i]->SetY(y);
        m_items[i]->Draw(vg, theme, m_pos, m_side == Side::LEFT);
    });
}

auto Sidebar::OnFocusGained() noexcept -> void {
    Widget::OnFocusGained();
    SetHidden(false);
}

auto Sidebar::OnFocusLost() noexcept -> void {
    Widget::OnFocusLost();
    SetHidden(true);
}

auto Sidebar::Add(std::unique_ptr<SidebarEntryBase>&& _entry) -> SidebarEntryBase* {
    auto& entry = m_items.emplace_back(std::forward<decltype(_entry)>(_entry));
    entry->SetPos(m_base_pos);

    // give focus to the first selectable entry, which is not necessarily the
    // first one when the sidebar opens with a group header.
    if (!m_has_focus && entry->IsFocusable()) {
        m_has_focus = true;
        m_index = (s64)m_items.size() - 1;
        entry->OnFocusGained();
        SetupButtons();
    }

    return entry.get();
}

auto Sidebar::IsFocusable(s64 index) const -> bool {
    if (index < 0 || index >= (s64)m_items.size()) {
        return false;
    }

    return m_items[index]->IsFocusable();
}

auto Sidebar::SkipUnfocusable(s64 index, bool forward) -> s64 {
    const s64 count = m_items.size();

    // bounded by count so an all-header sidebar cannot spin forever.
    for (s64 guard = 0; guard < count && !IsFocusable(index); ++guard) {
        const auto moved = forward
            ? m_list->ScrollDown(index, 1, count)
            : m_list->ScrollUp(index, 1, count);

        if (!moved) {
            break;
        }
    }

    return index;
}

void Sidebar::SetIndex(s64 index) {
    // if we moved
    if (m_index != index) {
        m_items[m_index]->OnFocusLost();
        m_index = index;
        m_items[m_index]->OnFocusGained();
        SetupButtons();
    }
}

void Sidebar::SetupButtons() {
    RemoveActions();

    // add entry actions
    for (const auto& [button, action] : m_items[m_index]->GetActions()) {
        SetAction(button, action);
    }

    // add default actions, overriding if needed.
    this->SetActions(
        // each item has it's own Action, but we take over B
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }}),
        std::make_pair(Button::START, Action{"", [this](){
            SetPop();
        }})
    );
}

} // namespace sphaira::ui
