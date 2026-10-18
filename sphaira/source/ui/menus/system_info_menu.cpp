#include "ui/menus/system_info_menu.hpp"
#include "ui/option_box.hpp"
#include "ui/nvg_util.hpp"
#include "ui/progress_box.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "i18n.hpp"

#include <atomic>
#include <algorithm>
#include <memory>

namespace sphaira::ui::menu::sysinfo {

// orange, for values that need action; the theme has only a red error colour.
const NVGcolor WARN_COLOUR = nvgRGB(0xF5, 0x9E, 0x0B);
// green, for finished wizard steps.
const NVGcolor DONE_COLOUR = nvgRGB(0x4C, 0xAF, 0x50);
namespace {

constexpr float ROW_H = 56.f;
constexpr float METRICS_H = 78.f;
// the value column starts here (fraction of the row width); both columns are
// clipped to their own box so a long value never runs under the next column.
constexpr float VALUE_X = 0.42f;

void DrawChevron(NVGcontext* vg, float x, float y, bool open, const NVGcolor& c) {
    nvgBeginPath(vg);
    if (open) {
        nvgMoveTo(vg, x - 7.f, y - 4.f);
        nvgLineTo(vg, x + 7.f, y - 4.f);
        nvgLineTo(vg, x, y + 5.f);
    } else {
        nvgMoveTo(vg, x - 4.f, y - 7.f);
        nvgLineTo(vg, x + 5.f, y);
        nvgLineTo(vg, x - 4.f, y + 7.f);
    }
    nvgClosePath(vg);
    nvgFillColor(vg, c);
    nvgFill(vg);
}

// no bold face is loaded: bold is faked by over-drawing with a sub-pixel x
// offset, the way the install session draws its event lines.
void DrawClipped(NVGcontext* vg, float x, float y, float w, float size, const NVGcolor& c, const char* text, bool bold = false) {
    nvgSave(vg);
    nvgIntersectScissor(vg, x, y - size, w, size * 2.f);
    gfx::drawText(vg, x, y, size, c, text, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    if (bold) {
        gfx::drawText(vg, x + 0.7f, y, size, c, text, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    }
    nvgRestore(vg);
}

std::atomic<bool> g_rebuild_requested{false};

// a value that does not fit on one line wraps inside the row (up to three smaller lines) instead of being cut off
void DrawValue(NVGcontext* vg, float x, float row_y, float row_h, float w, const NVGcolor& c, const std::string& text) {
    float b[4]{};
    nvgSave(vg);
    nvgFontSize(vg, 18.f);
    nvgTextBounds(vg, 0.f, 0.f, text.c_str(), nullptr, b);
    nvgRestore(vg);
    if (b[2] - b[0] <= w) {
        DrawClipped(vg, x, row_y + row_h / 2.f, w, 18.f, c, text.c_str());
        return;
    }
    nvgSave(vg);
    nvgIntersectScissor(vg, x, row_y + 2.f, w, row_h - 4.f);
    gfx::drawTextBox(vg, x, row_y + 6.f, 15.f, w, c, text.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_TOP, nullptr, 1.12f);
    nvgRestore(vg);
}

} // namespace

// a group marked `metrics` leaves the list and becomes the strip of figures above it
void Menu::TakeMetrics(std::vector<system_info::Group>& groups) {
    m_metrics.clear();
    std::vector<system_info::Group> rest;
    for (auto& g : groups) {
        if (g.metrics) {
            for (auto& r : g.rows) {
                m_metrics.push_back(std::move(r));
            }
        } else {
            rest.push_back(std::move(g));
        }
    }
    groups = std::move(rest);
}

Menu::Menu(std::vector<system_info::Group> groups) : MenuBase{"System information"_i18n, MenuFlag_None} {
    TakeMetrics(groups);
    m_groups = std::move(groups);
    m_open.assign(m_groups.size(), false);
    if (!m_open.empty()) {
        m_open[0] = true;
    }

    this->SetActions(
        std::make_pair(Button::A, Action{"Open / Close"_i18n, [this](){
            if (!m_rows.empty()) {
                ToggleGroup(GroupOf(m_index));
            }
        }}),
        std::make_pair(Button::Y, Action{"Open all / Close all"_i18n, [this](){ ToggleAll(); }}),
        // L / R walk the groups, opening each one they land on; left / right
        // page through the rows like the file browser.
        std::make_pair(Button::R, Action{"Next group"_i18n, [this](){ JumpGroup(1); }}),
        std::make_pair(Button::L, Action{"Previous group"_i18n, [this](){ JumpGroup(-1); }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){ SetPop(); }})
    );

    // the strip of figures takes the top of the page; the list starts below it
    const float top = m_metrics.empty() ? 0.f : METRICS_H + 10.f;
    m_list = std::make_unique<List>(1, 8, Vec4{75.f, 132.f + top, 1145.f, 462.f - top}, Vec4{75.f, 132.f + top, 1130.f, ROW_H});
    m_list->SetLayout(List::Layout::GRID);
    m_list->SetPageJump(true);
    Rebuild();
    SetIndex(0);
}

Menu::Menu(std::string title, std::vector<system_info::Group> groups, std::string x_label, std::function<void()> x_action,
           std::string plus_label, std::function<void()> plus_action, std::function<void()> tick,
           std::function<std::vector<system_info::Group>()> rebuild)
    : Menu{std::move(groups)} {
    m_tick = std::move(tick);
    m_rebuild = std::move(rebuild);
    if (plus_action) {
        SetAction(Button::START, Action{std::move(plus_label), std::move(plus_action)});
    }
    SetTitle(std::move(title));
    m_open.assign(m_groups.size(), true);
    SetAction(Button::X, Action{std::move(x_label), std::move(x_action)});
    // A on a row explains it; A on a section caption still opens and closes it.
    SetAction(Button::A, Action{"Open / Help"_i18n, [this](){
        if (m_rows.empty()) {
            return;
        }
        const auto& at = m_rows[m_index];
        if (at.row < 0) {
            ToggleGroup(at.group);
            return;
        }
        const auto& r = m_groups[at.group].rows[at.row];
        if (r.action) {
            const auto action = r.action; // the action may close this page
            action();
            return;
        }
        App::Push<OptionBox>(r.label + "\n\n" + (r.help.empty() ? "No help for this row."_i18n : r.help), "OK"_i18n);
    }});
    Rebuild();
    SetIndex(0);
}

void Menu::Rebuild() {
    m_rows.clear();
    for (s64 g = 0; g < static_cast<s64>(m_groups.size()); g++) {
        m_rows.push_back({g, -1});
        if (m_open[g]) {
            for (s64 r = 0; r < static_cast<s64>(m_groups[g].rows.size()); r++) {
                m_rows.push_back({g, r});
            }
        }
    }
}

auto Menu::GroupOf(s64 index) const -> s64 {
    return m_rows.empty() ? 0 : m_rows[std::clamp<s64>(index, 0, m_rows.size() - 1)].group;
}

void Menu::ToggleGroup(s64 group) {
    if (group < 0 || group >= static_cast<s64>(m_open.size())) {
        return;
    }
    m_open[group] = !m_open[group];
    Rebuild();
    // land on the group's caption, whichever way it went.
    const auto it = std::find_if(m_rows.begin(), m_rows.end(), [group](const Row& r){ return r.group == group && r.row < 0; });
    SetIndex(it == m_rows.end() ? 0 : std::distance(m_rows.begin(), it));
    App::PlaySoundEffect(SoundEffect_Focus);
}

void Menu::JumpGroup(int step) {
    if (m_groups.empty()) {
        return;
    }
    const auto count = static_cast<s64>(m_groups.size());
    const auto group = (GroupOf(m_index) + step + count) % count;
    m_open[group] = true;
    Rebuild();
    const auto it = std::find_if(m_rows.begin(), m_rows.end(), [group](const Row& r){ return r.group == group && r.row < 0; });
    SetIndex(it == m_rows.end() ? 0 : std::distance(m_rows.begin(), it));
    App::PlaySoundEffect(SoundEffect_Focus);
}

void Menu::ToggleAll() {
    const bool any_closed = std::any_of(m_open.begin(), m_open.end(), [](bool o){ return !o; });
    std::fill(m_open.begin(), m_open.end(), any_closed);
    const auto group = GroupOf(m_index);
    Rebuild();
    const auto it = std::find_if(m_rows.begin(), m_rows.end(), [group](const Row& r){ return r.group == group && r.row < 0; });
    SetIndex(it == m_rows.end() ? 0 : std::distance(m_rows.begin(), it));
    App::PlaySoundEffect(SoundEffect_Focus);
}

void Menu::SetIndex(s64 index) {
    if (m_rows.empty()) {
        m_index = 0;
        return;
    }
    m_index = std::clamp<s64>(index, 0, m_rows.size() - 1);
    if (!m_index) {
        m_list->SetYoff(0);
    } else {
        m_list->EnsureVisible(m_index, m_rows.size());
    }
    SetTitleSubHeading(m_groups[m_rows[m_index].group].title, true);
    SetSubHeading("");
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);
    if (m_tick && m_tick_ts.GetSeconds() >= 1) {
        m_tick_ts.Update();
        m_tick();
    }
    if (m_rebuild && (g_rebuild_requested.exchange(false) || m_rebuild_ts.GetSeconds() >= 3)) {
        m_rebuild_ts.Update();
        if (auto fresh = m_rebuild(); !fresh.empty()) {
            SetGroups(std::move(fresh));
        }
    }
    m_list->OnUpdate(controller, touch, m_index, m_rows.size(), [this](bool touch, auto i) {
        if (i < 0 || i >= static_cast<s64>(m_rows.size())) {
            return;
        }
        // a tap on a caption opens it at once; a tap on a value only selects it.
        if (touch && m_rows[i].row < 0) {
            m_index = i;
            ToggleGroup(m_rows[i].group);
            return;
        }
        App::PlaySoundEffect(SoundEffect_Focus);
        SetIndex(i);
    }, this);
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    if (!m_metrics.empty()) {
        // the figures of the moment: label small, value large, one cell each
        const float x = 75.f, y = 128.f, w = 1130.f;
        gfx::drawRect(vg, x, y, w, METRICS_H, theme->GetColour(ThemeEntryID_SELECTED_BACKGROUND), 8.f);
        const float cell = w / static_cast<float>(m_metrics.size());
        for (size_t i = 0; i < m_metrics.size(); i++) {
            const auto& r = m_metrics[i];
            const float cx = x + cell * static_cast<float>(i) + 18.f;
            const auto col = r.warn ? WARN_COLOUR : r.done ? DONE_COLOUR : theme->GetColour(ThemeEntryID_HIGHLIGHT_1);
            gfx::drawText(vg, cx, y + 8.f, 14.f, theme->GetColour(ThemeEntryID_TEXT_INFO), r.label.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            float b[4]{};
            nvgSave(vg);
            nvgFontSize(vg, 24.f);
            nvgTextBounds(vg, 0.f, 0.f, r.value.c_str(), nullptr, b);
            nvgRestore(vg);
            if (b[2] - b[0] <= cell - 36.f) {
                gfx::drawText(vg, cx, y + 30.f, 24.f, col, r.value.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            } else {
                gfx::drawTextBox(vg, cx, y + 28.f, 15.f, cell - 36.f, col, r.value.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_TOP, nullptr, 1.1f);
            }
        }
    }

    m_list->Draw(vg, theme, m_rows.size(), m_index, [this](auto* vg, auto* theme, Vec4 v, auto i) {
        const auto& row = m_rows[i];
        const auto& group = m_groups[row.group];
        const bool selected = m_index == static_cast<s64>(i);
        const bool open = m_open[row.group];
        const auto accent = theme->GetColour(ThemeEntryID_HIGHLIGHT_1);
        const float mid_y = v.y + v.h / 2.f;

        if (row.row < 0) {
            // a group is a filled band; while open its title takes the accent
            // colour, so the caption and its rows never read as one list.
            gfx::drawRect(vg, v.x, v.y + 3.f, v.w, v.h - 6.f, theme->GetColour(ThemeEntryID_GRID), 6.f);
            if (selected) {
                gfx::drawRectOutline(vg, theme, 4.f, v.x, v.y + 3.f, v.w, v.h - 6.f, 6.f);
            }
            const auto title_col = open ? accent : theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT);
            DrawChevron(vg, v.x + 30.f, mid_y, open, title_col);
            DrawClipped(vg, v.x + 54.f, mid_y, v.w - 140.f, 24.f, title_col, group.title.c_str(), true);
            gfx::drawTextArgs(vg, v.x + v.w - 20.f, mid_y, 15.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
                theme->GetColour(ThemeEntryID_TEXT_INFO), "%zu", group.rows.size());
            return;
        }

        // a value row is indented under its caption.
        if (selected) {
            gfx::drawRect(vg, v.x + 14.f, v.y + 2.f, v.w - 14.f, v.h - 4.f, theme->GetColour(ThemeEntryID_SELECTED_BACKGROUND), 5.f);
            gfx::drawRectOutline(vg, theme, 4.f, v.x + 14.f, v.y + 2.f, v.w - 14.f, v.h - 4.f);
        } else {
            gfx::drawRect(vg, v.x + 30.f, v.y + v.h - 1.f, v.w - 50.f, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
        }

        const auto& r = group.rows[row.row];
        const float value_x = v.x + v.w * VALUE_X;
        const auto label_col = theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT);
        DrawClipped(vg, v.x + 54.f, mid_y, value_x - v.x - 66.f, 18.f, label_col, r.label.c_str(), true);
        DrawValue(vg, value_x, v.y, v.h, v.x + v.w - 20.f - value_x, r.warn ? WARN_COLOUR : r.done ? DONE_COLOUR : accent, r.value);
    });
}

void Menu::SetGroups(std::vector<system_info::Group> groups) {
    const auto old_open = m_open;
    TakeMetrics(groups);
    m_groups = std::move(groups);
    m_open.assign(m_groups.size(), true);
    for (size_t i = 0; i < m_open.size() && i < old_open.size(); i++) {
        m_open[i] = old_open[i];
    }
    Rebuild();
    if (m_index >= static_cast<s64>(m_rows.size())) {
        m_index = std::max<s64>(0, static_cast<s64>(m_rows.size()) - 1);
    }
}

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();
    if (m_rebuild) {
        if (auto fresh = m_rebuild(); !fresh.empty()) {
            SetGroups(std::move(fresh));
        }
        m_rebuild_ts.Update();
    }
    SetIndex(m_index);
}

void RequestRebuild() {
    g_rebuild_requested = true;
}

void Open() {
    auto groups = std::make_shared<std::vector<system_info::Group>>();
    App::Push<ProgressBox>(0, "Reading"_i18n, "System information"_i18n, [groups](auto) -> Result {
        *groups = system_info::Collect();
        R_SUCCEED();
    }, [groups](Result rc){
        if (R_SUCCEEDED(rc)) {
            App::Push<Menu>(std::move(*groups));
        }
    }, 1, PRIO_PREEMPTIVE, 1024 * 128, false);
}

} // namespace sphaira::ui::menu::sysinfo
