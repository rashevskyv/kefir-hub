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
#include "ui/file_icon.hpp"
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

namespace {

// loads the icon of an app row; true when an image was made (counts against the per-frame budget)
auto LoadAppIcon(NVGcontext* vg, NroEntry& e) -> bool {
    if (e.image || !e.icon_size || !e.icon_offset) {
        return false;
    }
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
            return true;
        }
    }
    // prevent loading of this icon again as it's already failed.
    e.icon_offset = e.icon_size = 0;
    return false;
}

// a folder without its own picture: its app icons, 1 nearly full size, up to 4 in 2x2, up to 9 in 3x3
void DrawAppGrid(NVGcontext* vg, Theme* theme, const Vec4& dest, const std::vector<int>& images) {
    auto tint = theme->GetColour(ThemeEntryID_TEXT);
    tint.a *= 0.12f;
    gfx::drawRect(vg, dest, tint, 6.f);

    const int n = static_cast<int>(images.size());
    const int cols = n == 1 ? 1 : n <= 4 ? 2 : 3;
    const float side = std::min(dest.w, dest.h);
    const float pad = side * 0.06f;
    const float gap = side * 0.05f;
    const float cell = (side - pad * 2.f - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
    const float x0 = dest.x + (dest.w - side) / 2.f + pad;
    const float y0 = dest.y + (dest.h - side) / 2.f + pad;
    for (int i = 0; i < n && i < cols * cols; i++) {
        const Vec4 r{x0 + static_cast<float>(i % cols) * (cell + gap), y0 + static_cast<float>(i / cols) * (cell + gap), cell, cell};
        gfx::drawImage(vg, r, images[i] ?: App::GetDefaultImage(), cols == 1 ? 5.f : 3.f);
    }
}

} // namespace

auto GetNroEntries() -> std::span<const NroEntry> {
    if (!g_menu) {
        return {};
    }

    return g_menu->GetHomebrewList();
}
Menu::Menu() : grid::Menu{"Homebrew"_i18n, MenuFlag_Tab} {
    g_menu = this;

    this->UpdateActions(); // A and B: they change with the folder
    this->SetActions(
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
    m_list->OnUpdate(controller, touch, m_index, m_entries_current.size(), [this](bool touch, auto i) {
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
        auto& e = GetEntry();
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
        auto& e = GetEntry(pos);
        // a folder without its own icon shows the icons of its apps
        const auto* preview = e.is_dir ? &m_folder_previews[m_entries_current[pos] - m_entries.size()] : nullptr;

        // lazy load image
        if (image_load_count < image_load_max) {
            if (e.is_dir && !e.image && e.icon_size) {
                LoadFolderIcon(vg, e);
                image_load_count++;
            } else if (e.is_dir && !e.image) {
                for (const auto i : *preview) {
                    if (image_load_count < image_load_max && LoadAppIcon(vg, m_entries[i])) {
                        image_load_count++;
                    }
                }
            } else if (!e.is_dir && LoadAppIcon(vg, e)) {
                image_load_count++;
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
        // the file name takes the author's line: builds of one app share the title and the author, only the file differs
        auto subtitle = layout != grid::LayoutType_HbMenu ? GetNroFilename(e) : std::string{e.GetAuthor()};
        if (e.is_dir) {
            card_name = e.GetName();
            card_version = "Apps"_i18n + ": " + std::to_string(e.size);
            subtitle = layout != grid::LayoutType_HbMenu ? "Folder"_i18n : std::string{};
            if (layout == grid::LayoutType_List) {
                card_version = "[" + card_version + "]";
            }
        }

        if (e.is_dir && !e.image) {
            // no picture chosen: the icons of its apps, or the folder shape when it is empty
            const auto r = DrawEntryNoImage(vg, theme, layout, v, selected, card_name.c_str(), subtitle.c_str(), card_version.c_str(), e.selected);
            const auto shape = layout == grid::LayoutType_HbMenu ? Vec4{r.x, r.y + 28.f, r.w, r.h - 28.f} : r;
            if (preview->empty()) {
                file_icon::DrawFolderShape(vg, theme, shape);
            } else {
                std::vector<int> images;
                for (const auto i : *preview) {
                    images.push_back(m_entries[i].image);
                }
                DrawAppGrid(vg, theme, shape, images);
            }
        } else {
            DrawEntry(vg, theme, layout, v, selected, e.image, card_name.c_str(), subtitle.c_str(), card_version.c_str(), e.selected);
        }
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
        UpdateActions();
        m_index = 0;
        m_list->SetYoff(0);
        RemoveAction(Button::L3);
        SetTitleSubHeading("");
        this->SetSubHeading("0 / 0");
        return;
    }

    m_index = std::clamp<s64>(index, 0, m_entries_current.size() - 1);
    if (!m_index) {
        m_list->SetYoff(0);
    }

    UpdateActions();

    if (IsStarEnabled() && !GetEntry().is_dir && !IsKefirUpdaterStub(GetEntry())) {
        const auto star_path = GenerateStarPath(GetEntry().path);
        if (fs::FsNativeSd().FileExists(star_path)) {
            SetAction(Button::L3, Action{"Unstar"_i18n, [this](){
                fs::FsNativeSd().DeleteFile(GenerateStarPath(GetEntry().path));
                App::Notify("Unstarred "_i18n + GetEntry().GetName());
                SortAndFindLastFile();
            }});
        } else {
            SetAction(Button::L3, Action{"Star"_i18n, [this](){
                fs::FsNativeSd().CreateFile(GenerateStarPath(GetEntry().path));
                App::Notify("Starred "_i18n + GetEntry().GetName());
                SortAndFindLastFile();
            }});
        }
    } else {
        RemoveAction(Button::L3);
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
    if (!entry.is_dir && !IsKefirUpdaterStub(entry)) {
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
    // only the apps of the folder on screen
    for (const auto index : m_entries_index[Filter_All]) {
        auto& entry = Entry(index);
        if (entry.is_dir || IsKefirUpdaterStub(entry)) {
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
        const auto& focused = m_entries_current[m_index] < m_entries.size()
            ? m_entries[m_entries_current[m_index]] : m_folders[m_entries_current[m_index] - m_entries.size()];
        if (!focused.is_dir && !IsKefirUpdaterStub(focused)) {
            out.emplace_back(focused);
        }
    }

    return out;
}


} // namespace sphaira::ui::menu::homebrew
