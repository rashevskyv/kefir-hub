#include "ui/menus/task_manager.hpp"
#include "app.hpp"
#include "ui/nvg_util.hpp"
#include "i18n.hpp"
#include "utils/utils.hpp"

#include <algorithm>
#include <cstdio>

namespace sphaira::ui::menu::tools {

TaskManagerMenu::TaskManagerMenu() : MenuBase{"Task Manager"_i18n, MenuFlag_None} {
    this->SetActions(
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){ SetPop(); }}),
        std::make_pair(Button::X, Action{"Refresh"_i18n, [this](){ Refresh(); }})
    );

    const float list_x = 75.f;
    const float list_y = GetY() + 60.f;
    const float list_w = 1070.f;
    const float list_h = 559.f;
    const float row_h = 62.f;
    m_list = std::make_unique<List>(1, 9, Vec4{list_x, list_y, list_w, list_h}, Vec4{list_x, list_y, list_w, row_h});
    m_list->SetLayout(List::Layout::GRID);
}

void TaskManagerMenu::Refresh() {
    const auto keep = (m_index >= 0 && m_index < static_cast<s64>(m_procs.size()))
        ? m_procs[m_index].pid : 0;
    meminfo::QueryBreakdown(m_ram);
    m_procs = meminfo::ListProcesses();
    m_listed_bytes = 0;
    for (const auto& p : m_procs) {
        m_listed_bytes += p.memory_bytes;
    }
    s64 index = 0;
    if (keep) {
        for (s64 i = 0; i < static_cast<s64>(m_procs.size()); i++) {
            if (m_procs[i].pid == keep) {
                index = i;
                break;
            }
        }
    }
    SetIndex(index);
}

void TaskManagerMenu::OnFocusGained() {
    MenuBase::OnFocusGained();
    Refresh();
}

void TaskManagerMenu::SetIndex(s64 index) {
    if (m_procs.empty()) {
        m_index = 0;
        SetTitleSubHeading("No processes"_i18n, true);
        return;
    }
    m_index = std::clamp<s64>(index, 0, static_cast<s64>(m_procs.size() - 1));
    if (!m_index) {
        m_list->SetYoff(0);
    }
    const auto& p = m_procs[m_index];
    char tid[17]{};
    std::snprintf(tid, sizeof(tid), "%016llX", static_cast<unsigned long long>(p.program_id));
    SetTitleSubHeading(std::string{tid} + "  pid " + std::to_string(p.pid), true);
}

void TaskManagerMenu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);
    if (m_procs.empty()) {
        return;
    }
    m_list->OnUpdate(controller, touch, m_index, m_procs.size(), [this](bool touch, auto i) {
        if (touch && m_index == i) {
            return;
        }
        App::PlaySoundEffect(SoundEffect_Focus);
        SetIndex(i);
    }, this);
}

void TaskManagerMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    meminfo::DrawBreakdown(vg, theme, 80.f, GetY() + 6.f, m_ram);

    if (m_procs.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "%s", "No processes"_i18n.c_str());
        return;
    }

    const u64 denom = std::max<u64>(1, m_listed_bytes);
    const u64 peak = std::max<u64>(1, m_procs.front().memory_bytes);

    nvgSave(vg);
    const float list_x = 75.f;
    const float list_y = GetY() + 60.f;
    const float list_w = 1070.f;
    const float list_h = 559.f;
    const float p = gfx::SELECTION_OUTLINE_PAD;
    nvgScissor(vg, list_x - p, list_y - p, list_w + p * 2, list_h + p * 2);

    m_list->Draw(vg, theme, m_procs.size(), [this, denom, peak](auto* vg, auto* theme, Vec4 v, auto i) {
        const auto& [x, y, w, h] = v;
        const auto& proc = m_procs[i];
        const auto selected = m_index == static_cast<s64>(i);
        const auto text_id = selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;
        if (selected) {
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else if (i + 1 != m_procs.size()) {
            gfx::drawRect(vg, x, y + h, w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
        }

        gfx::drawTextArgs(vg, x + 20.f, y + h / 2.f - 8.f, 18.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
            theme->GetColour(text_id),
            "%s", proc.name.c_str());

        const float bar_x = x + 20.f;
        const float bar_y = y + h / 2.f + 12.f;
        const float bar_w = 420.f;
        const float bar_h = 7.f;
        gfx::drawRect(vg, bar_x, bar_y, bar_w, bar_h, nvgRGBA(255, 255, 255, 28), 3.f);
        const float fill_w = bar_w * static_cast<float>(proc.memory_bytes) / static_cast<float>(peak);
        gfx::drawRect(vg, bar_x, bar_y, fill_w, bar_h, nvgRGBA(80, 160, 230, 220), 3.f);

        const auto pct = static_cast<int>((proc.memory_bytes * 1000) / denom);
        gfx::drawTextArgs(vg, x + w - 15.f, y + h / 2.f, 18.f,
            NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
            theme->GetColour(text_id),
            "%s  %d.%d%%",
            utils::formatSizeStorage(proc.memory_bytes).c_str(),
            pct / 10, pct % 10);
    });

    nvgRestore(vg);
}

} // namespace sphaira::ui::menu::tools
