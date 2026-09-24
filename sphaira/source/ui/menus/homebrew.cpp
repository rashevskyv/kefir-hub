#include "app.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "path_util.hpp"
#include "ui/menus/homebrew.hpp"
#include "ui/menus/homebrew_internal.hpp"
#include "ui/menus/install_share.hpp"
#include "ui/sidebar.hpp"
#include "ui/error_box.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/nvg_util.hpp"
#include "ui/forwarder_editor.hpp"
#include "nacp_util.hpp"
#include "owo.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "image.hpp"

#include <minIni.h>
#include <cstdio>
#include <optional>
#include <utility>
#include <algorithm>
#include <functional>

namespace sphaira::ui::menu::homebrew {

Menu* g_menu{};
constinit UEvent g_change_uevent;
option::OptionBool g_kefir_updater_notice_ack{"homebrew", "kefir_updater_notice_ack", false};

void SignalChange() {
    ueventSignal(&g_change_uevent);
}

auto GetNroEntries() -> std::span<const NroEntry> {
    if (!g_menu) {
        return {};
    }

    return g_menu->GetHomebrewList();
}
Menu::Menu() : grid::Menu{"Homebrew"_i18n, MenuFlag_Tab} {
    g_menu = this;

    this->SetActions(
        std::make_pair(Button::A, Action{"Launch"_i18n, [this](){
            if (!g_kefir_updater_notice_ack.Get() && IsKefirUpdaterEntry(GetEntry())) {
                ShowKefirUpdaterRemovedDialog();
            } else {
                nro_launch(GetEntry().path);
            }
        }}),
        std::make_pair(Button::B, Action{"Exit"_i18n, [this](){
            if (m_selected_count) {
                ClearSelection();
            } else {
                App::Exit();
            }
        }}),
        std::make_pair(Button::X, Action{"Select"_i18n, [this](){
            ToggleCurrentSelection();
        }}),
        std::make_pair(Button::Y, Action{"Invert"_i18n, [this](){
            InvertSelection();
        }}),
        std::make_pair(Button::START, Action{"Options"_i18n, [this](){
            DisplayOptions();
        }})
    );

    OnLayoutChange();
    ueventCreate(&g_change_uevent, true);
}

Menu::~Menu() {
    *m_alive = false;
    g_menu = {};
    FreeEntries();
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    if (R_SUCCEEDED(waitSingle(waiterForUEvent(&g_change_uevent), 0))) {
        m_dirty = true;
    }

    if (m_dirty) {
        SortAndFindLastFile(true);
    }

    MenuBase::Update(controller, touch);
    m_list->OnUpdate(controller, touch, m_index, m_entries.size(), [this](bool touch, auto i) {
        if (touch && m_index == i) {
            FireAction(Button::A);
        } else {
            App::PlaySoundEffect(SoundEffect_Focus);
            SetIndex(i);
        }
    }, this);
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    if (m_layout.Get() == grid::LayoutType_HbMenu && !m_entries_current.empty()) {
        const auto index = m_entries_current[m_index];
        auto& e = m_entries[index];
        bool has_star = false;
        if (IsStarEnabled()) {
            if (!e.has_star.has_value()) {
                e.has_star = fs::FsNativeSd().FileExists(GenerateStarPath(e.path));
            }
            has_star = e.has_star.value();
        }
        std::string title_text = GetNroFilename(e);
        if (has_star) {
            title_text = std::string("\u2605 ") + title_text;
        }
        DrawHbMenuHeader(vg, theme, e.image, title_text.c_str(), e.GetAuthor(), e.GetDisplayVersion(), e.GetName());
    }

    // max images per frame, in order to not hit io / gpu too hard.
    const int image_load_max = 2;
    int image_load_count = 0;

    m_list->Draw(vg, theme, m_entries_current.size(), m_index, [this, &image_load_count](auto* vg, auto* theme, auto v, auto pos) {
        const auto index = m_entries_current[pos];
        auto& e = m_entries[index];

        // lazy load image
        if (image_load_count < image_load_max) {
            if (!e.image && e.icon_size && e.icon_offset) {
                // NOTE: it seems that images can be any size. SuperTux uses a 1024x1024
                // ~300Kb image, which takes a few frames to completely load.
                // really, switch-tools should handle this by resizing the image before
                // adding it to the nro, as well as validate its a valid jpeg.
                const auto icon = nro_get_icon(e.path, e.icon_size, e.icon_offset);
                TimeStamp ts;
                if (!icon.empty()) {
                    const auto image = ImageLoadIcon(icon);
                    if (!image.data.empty() && image.w == 256 && image.h == 256) {
                        e.image = nvgCreateImageRGBA(vg, image.w, image.h, 0, image.data.data());
                        log_write("\t[image load] time taken: %.2fs %zums\n", ts.GetSecondsD(), ts.GetMs());
                        image_load_count++;
                    } else {
                        // prevent loading of this icon again as it's already failed.
                        e.icon_offset = e.icon_size = 0;
                    }
                } else {
                    // prevent loading of this icon again as it's already failed.
                    e.icon_offset = e.icon_size = 0;
                }
            }
        }


        bool has_star = false;
        if (IsStarEnabled()) {
            if (!e.has_star.has_value()) {
                e.has_star = fs::FsNativeSd().FileExists(GenerateStarPath(e.path));
            }
            has_star = e.has_star.value();
        }

        std::string card_name;
        std::string card_version;
        if (m_layout.Get() == grid::LayoutType_HbMenu) {
            std::string nro_fn = GetNroFilename(e);
            if (has_star) {
                card_name = std::string("\u2605 ") + nro_fn;
            } else {
                card_name = nro_fn;
            }
            card_version = e.GetName();
        } else {
            if (has_star) {
                card_name = std::string("\u2605 ") + e.GetName();
            } else {
                card_name = e.GetName();
            }
            card_version = e.GetDisplayVersion();
            if (m_layout.Get() == grid::LayoutType_List) {
                // right column, DBI-style: version in brackets, then the nro size.
                card_version = (card_version.empty() ? "" : "[" + card_version + "]  ") + grid::FormatBytes(e.size);
            }
        }

        const auto layout = m_layout.Get();
        const auto selected = pos == m_index;
        DrawEntry(vg, theme, layout, v, selected, e.image, card_name.c_str(), e.GetAuthor(), card_version.c_str(), e.selected);
        DrawSelectionMark(vg, theme, layout, v, v, e.selected, m_selected_count > 0);
    });
}

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();
    if (m_entries.empty()) {
        ScanHomebrew();
    }
}


void Menu::SetIndex(s64 index) {
    if (m_entries_current.empty()) {
        m_index = 0;
        m_list->SetYoff(0);
        RemoveAction(Button::R3);
        SetTitleSubHeading("");
        this->SetSubHeading("0 / 0");
        return;
    }

    m_index = std::clamp<s64>(index, 0, m_entries_current.size() - 1);
    if (!m_index) {
        m_list->SetYoff(0);
    }

    if (IsStarEnabled() && !IsKefirUpdaterStub(GetEntry())) {
        const auto star_path = GenerateStarPath(GetEntry().path);
        if (fs::FsNativeSd().FileExists(star_path)) {
            SetAction(Button::R3, Action{"Unstar"_i18n, [this](){
                fs::FsNativeSd().DeleteFile(GenerateStarPath(GetEntry().path));
                App::Notify("Unstarred "_i18n + GetEntry().GetName());
                SortAndFindLastFile();
            }});
        } else {
            SetAction(Button::R3, Action{"Star"_i18n, [this](){
                fs::FsNativeSd().CreateFile(GenerateStarPath(GetEntry().path));
                App::Notify("Starred "_i18n + GetEntry().GetName());
                SortAndFindLastFile();
            }});
        }
    } else {
        RemoveAction(Button::R3);
    }

    // TimeCalendarTime caltime;
    // timeToCalendarTimeWithMyRule()
    // todo: fix GetFileTimeStampRaw being different to timeGetCurrentTime
    // log_write("name: %s hbini.ts: %lu file.ts: %lu smaller: %s\n", e.GetName(), e.hbini.timestamp, e.timestamp.modified, e.hbini.timestamp < e.timestamp.modified ? "true" : "false");

    SetTitleSubHeading(GetEntry().path, true);
    this->SetSubHeading(std::to_string(m_index + 1) + " / " + std::to_string(m_entries_current.size()));
}


void Menu::OnLayoutChange() {
    m_index = 0;
    grid::Menu::OnLayoutChange(m_list, m_layout.Get());
}


void Menu::ToggleCurrentSelection() {
    if (m_entries_current.empty()) {
        return;
    }

    auto& entry = GetEntry();
    if (!IsKefirUpdaterStub(entry)) {
        entry.selected ^= 1;
        m_selected_count += entry.selected ? 1 : -1;
    }

    if (m_index + 1 < static_cast<s64>(m_entries_current.size())) {
        SetIndex(m_index + 1);
        m_list->EnsureVisible(m_index, m_entries_current.size());
    }
}

void Menu::InvertSelection() {
    m_selected_count = 0;
    for (auto& entry : m_entries) {
        if (IsKefirUpdaterStub(entry)) {
            entry.selected = false;
            continue;
        }
        if (entry.hbini.hidden && !m_show_hidden.Get()) {
            entry.selected = false;
            continue;
        }
        entry.selected ^= 1;
        if (entry.selected) {
            m_selected_count++;
        }
    }
}

void Menu::ClearSelection() {
    for (auto& entry : m_entries) {
        entry.selected = false;
    }
    m_selected_count = 0;
}

auto Menu::GetSelectedEntries() const -> std::vector<NroEntry> {
    std::vector<NroEntry> out;
    if (m_selected_count > 0) {
        for (const auto& e : m_entries) {
            if (e.selected && !IsKefirUpdaterStub(e)) {
                out.emplace_back(e);
            }
        }
    }

    if (out.empty() && !m_entries_current.empty()) {
        const auto& focused = m_entries[m_entries_current[m_index]];
        if (!IsKefirUpdaterStub(focused)) {
            out.emplace_back(focused);
        }
    }

    return out;
}


} // namespace sphaira::ui::menu::homebrew
