#include "ui/sidebar.hpp"
#include "ui/menus/file_picker.hpp"
#include "app.hpp"
#include "ui/popup_list.hpp"
#include "ui/nvg_util.hpp"
#include "i18n.hpp"
#include "swkbd.hpp"
#include <algorithm>

namespace sphaira::ui {
namespace {

auto DisabledTextColour() -> NVGcolor {
    return nvgRGBA(135, 138, 148, 255);
}

} // namespace

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
    const float extra_right = m_has_submenu ? 20.f : 0.f;
    SidebarEntryBase::DrawEntry(vg, theme, m_title, "", false, extra_right);

    if (m_has_submenu) {
        const float y = m_pos.y + (m_pos.h / 2.f);
        const float x1 = m_pos.x + m_pos.w - 24.f;
        nvgBeginPath(vg);
        nvgMoveTo(vg, x1 - 8.f, y - 8.f);
        nvgLineTo(vg, x1, y);
        nvgLineTo(vg, x1 - 8.f, y + 8.f);
        nvgStrokeColor(vg, colour);
        nvgStrokeWidth(vg, 3.f);
        nvgLineCap(vg, NVG_ROUND);
        nvgLineJoin(vg, NVG_ROUND);
        nvgStroke(vg);
    }
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

} // namespace sphaira::ui
