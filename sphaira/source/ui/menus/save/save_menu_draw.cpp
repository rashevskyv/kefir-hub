#include "app.hpp"
#include "i18n.hpp"
#include "ui/menus/save_menu.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "ui/menus/save/save_locations.hpp"
#include "ui/menus/save/save_menu_detail.hpp"
#include "save_menu_internal.hpp"
#include "ui/nvg_util.hpp"
#include "ui/layout.hpp"

#include <algorithm>
#include <array>
#include <string>

namespace sphaira::ui::menu::save {
namespace {

// a thick border drawn *inside* the tile that fades from the given colour at
// the edge to fully transparent toward the centre, so the category of a save
// (deleted / backup) reads at a glance without a hard outer frame.
void DrawInnerBorder(NVGcontext* vg, const Vec4& v, const NVGcolor& col, float thickness, float radius) {
    const auto transparent = nvgRGBAf(col.r, col.g, col.b, 0.f);
    // box gradient: inside the inset rectangle -> transparent, blending out to
    // the solid colour over `thickness` toward the tile edge.
    const auto paint = nvgBoxGradient(vg,
        v.x + thickness, v.y + thickness, v.w - thickness * 2.f, v.h - thickness * 2.f,
        radius, thickness, transparent, col);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, v.x, v.y, v.w, v.h, radius);
    nvgFillPaint(vg, paint);
    nvgFill(vg);
}

// right-hand column of a list row, DBI-style: save category in brackets, then
// its allocated size (backup rows show archive count when > 1).
auto FormatListInfo(const Entry& e) -> std::string {
    const std::string label = "[" + FormatSaveTypeLabel(e.save_data_type) + "]";
    if (e.is_backup) {
        return e.backup_count > 1 ? label + "  " + std::to_string(e.backup_count) + " archives" : label;
    }
    return e.size ? label + "  " + grid::FormatBytes(e.size) : label;
}

auto FormatBackupRankMarker(const Entry& e) -> std::string {
    if (!e.backup_rank_known) {
        return "rk:?";
    }
    return (e.save_data_rank == FsSaveDataRank_Secondary) ? "rk:1" : "rk:0";
}

} // namespace

auto Menu::ComputeGridSections() const -> GridSections {
    GridSections g;
    g.row = std::max<s64>(1, m_list ? m_list->GetRow() : 1);
    g.horizontal = m_list && m_list->GetLayout() == List::Layout::HOME;

    const auto total = static_cast<s64>(m_entries.size());
    g.live_count = std::clamp<s64>(m_backup_start, 0, total);
    g.backup_count = total - g.live_count;

    if (m_category == Category::Backups) {
        if (!m_entries.empty()) {
            s64 cur_disp = 0;
            size_t idx = 0;
            while (idx < m_entries.size()) {
                const auto src = m_entries[idx].backup_source;
                size_t end = idx + 1;
                while (end < m_entries.size() && m_entries[end].backup_source == src) {
                    end++;
                }
                const s64 count = static_cast<s64>(end - idx);

                if (g.sections.empty()) {
                    // The grid already starts below the tabs, leaving room for
                    // the first label without a whole empty row.
                    cur_disp = (m_layout.Get() == grid::LayoutType_Grid && !m_app_id_filter) ? 0 : g.row;
                } else {
                    const s64 rem = cur_disp % g.row;
                    if (rem != 0) {
                        cur_disp += (g.row - rem);
                    }
                    cur_disp += g.row;
                }

                GridSections::Section sec;
                sec.label = i18n::get(GetBackupSourceLabel(src));
                sec.entry_start = static_cast<s64>(idx);
                sec.entry_count = count;
                sec.first_display = cur_disp;
                sec.has_divider = true;

                cur_disp += count;
                g.sections.emplace_back(std::move(sec));
                idx = end;
            }

            g.has_backups = true;
            g.first_backup_display = g.sections.empty() ? 0 : g.sections[0].first_display;
            g.display_count = cur_disp;
        }
        return g;
    }

    if (g.backup_count > 0 && g.live_count > 0) {
        g.has_backups = true;
        // fill the remainder of the last live row, then add one empty row that
        // hosts the "Backups" divider label.
        g.base_fill = (g.row - g.live_count % g.row) % g.row;
        g.pad = g.base_fill + g.row;
    }

    g.first_backup_display = g.live_count + g.pad;
    g.display_count = g.live_count + g.pad + g.backup_count;

    if (g.live_count > 0) {
        GridSections::Section sec;
        sec.entry_start = 0;
        sec.entry_count = g.live_count;
        sec.first_display = 0;
        sec.has_divider = false;
        g.sections.emplace_back(std::move(sec));
    }
    if (g.backup_count > 0) {
        GridSections::Section sec;
        sec.label = "Backups"_i18n;
        sec.entry_start = g.live_count;
        sec.entry_count = g.backup_count;
        sec.first_display = g.first_backup_display;
        sec.has_divider = (g.live_count > 0);
        g.sections.emplace_back(std::move(sec));
    }

    return g;
}

auto Menu::EntryToDisplay(s64 entry, const GridSections& g) const -> s64 {
    for (const auto& sec : g.sections) {
        if (entry >= sec.entry_start && entry < sec.entry_start + sec.entry_count) {
            return sec.first_display + (entry - sec.entry_start);
        }
    }
    if (entry < g.live_count) {
        return entry;
    }
    return entry + g.pad;
}

auto Menu::DisplayToEntry(s64 display, const GridSections& g) const -> s64 {
    for (const auto& sec : g.sections) {
        if (display >= sec.first_display && display < sec.first_display + sec.entry_count) {
            return sec.entry_start + (display - sec.first_display);
        }
    }
    return -1;
}

auto Menu::ResolveDisplay(s64 display, s64 from, const GridSections& g) const -> s64 {
    const auto entry = DisplayToEntry(display, g);
    if (entry >= 0) {
        return entry;
    }
    const auto total = static_cast<s64>(m_entries.size());
    if (total <= 0) {
        return -1;
    }

    if (display >= from) {
        for (s64 i = 0; i < total; i++) {
            if (EntryToDisplay(i, g) > display) {
                return i;
            }
        }
        return total - 1;
    } else {
        for (s64 i = total - 1; i >= 0; i--) {
            if (EntryToDisplay(i, g) < display) {
                return i;
            }
        }
        return 0;
    }
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    if (!m_app_id_filter) {
        DrawCategoryTabs(vg, theme);
    }

    if (m_entries.empty()) {
        const float empty_y = !m_app_id_filter ? (TAB_BAR_TOP + TAB_BAR_H + layout::FOOTER_LINE_Y) * 0.5f : (GetY() + GetH() / 2.f);
        gfx::drawTextArgs(vg, GetX() + GetW() / 2.f, empty_y, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Empty..."_i18n.c_str());
        return;
    }

    if (m_layout.Get() == grid::LayoutType_HbMenu) {
        auto& e = m_entries[m_index];
        u64 id = IsSystemLikeSave(e.save_data_type) ? e.system_save_data_id : e.application_id;
        char title_id[33];
        std::snprintf(title_id, sizeof(title_id), "%016lX", id);

        const auto account = e.is_backup ?
            FormatBackupAccount(e, m_accounts) + "  •  " + FormatBackupRankMarker(e) :
            ((e.save_data_type == FsSaveDataType_Account && !m_all_accounts) ?
                GetAccountName(e.uid) : GetAccountSummary());

        const auto author_text = e.is_backup ?
            FormatBackupTimestamp(e.backup_timestamp) : std::string{e.GetAuthor()};

        if (!m_app_id_filter) {
            nvgSave(vg);
            nvgTranslate(vg, 0.f, 26.f);
            DrawHbMenuHeader(vg, theme, e.image, e.GetName(), author_text.c_str(), title_id, account.c_str());
            nvgRestore(vg);
        } else {
            DrawHbMenuHeader(vg, theme, e.image, e.GetName(), author_text.c_str(), title_id, account.c_str());
        }
    }

    // max images per frame, in order to not hit io / gpu too hard.
    const int image_load_max = 2;
    int image_load_count = 0;

    BackupColumnLayout backup_cols{};
    if (m_layout.Get() == grid::LayoutType_List) {
        nvgFontSize(vg, 15.f);
        nvgTextAlign(vg, NVG_ALIGN_LEFT);
        float bounds[4]{};

        for (const auto& e : m_entries) {
            if (!e.is_backup) {
                continue;
            }
            const auto cols = GetBackupSecondaryColumns(e, m_accounts);
            if (!cols.title_id.empty()) {
                gfx::textBounds(vg, 0, 0, bounds, cols.title_id.c_str());
                backup_cols.max_title_w = std::max(backup_cols.max_title_w, bounds[2] - bounds[0]);
            }
            if (!cols.account.empty()) {
                gfx::textBounds(vg, 0, 0, bounds, cols.account.c_str());
                backup_cols.max_account_w = std::max(backup_cols.max_account_w, bounds[2] - bounds[0]);
            }
            if (!cols.timestamp.empty()) {
                gfx::textBounds(vg, 0, 0, bounds, cols.timestamp.c_str());
                backup_cols.max_date_w = std::max(backup_cols.max_date_w, bounds[2] - bounds[0]);
            }
        }
    }

    const auto g = ComputeGridSections();
    m_list->Draw(vg, theme, g.display_count, EntryToDisplay(m_index, g), [this, &image_load_count, g, &backup_cols](NVGcontext* vg, Theme* theme, Vec4 v, s64 disp) {
        const auto entry = DisplayToEntry(disp, g);
        if (entry < 0) {
            return; // empty divider gap; the label is drawn with the first backup tile.
        }
        auto& e = m_entries[entry];

        if (e.status == title::NacpLoadStatus::None) {
            if (!IsSystemLikeSave(e.save_data_type)) {
                title::PushAsync(e.application_id);
                e.status = title::NacpLoadStatus::Progress;
            } else {
                detail::FakeNacpEntryForSystem(e);
            }
        } else if (e.status == title::NacpLoadStatus::Progress) {
            detail::LoadResultIntoEntry(e, title::GetAsync(e.application_id));
        }

        // lazy load image
        if (image_load_count < image_load_max) {
            if (detail::LoadControlImage(e, title::GetAsync(e.application_id))) {
                image_load_count++;
            }
        }

        const auto selected = entry == m_index;
        Vec4 image_v = v;
        const auto info = (m_layout.Get() == grid::LayoutType_List || m_layout.Get() == grid::LayoutType_GridDetail) ? FormatListInfo(e) : std::string{};
        const bool is_list = (m_layout.Get() == grid::LayoutType_List);
        const auto author_str = (e.is_backup && is_list)
            ? std::string{}
            : (e.is_backup ? FormatBackupSecondaryText(e, m_accounts) : std::string{e.GetAuthor()});

        if (!IsSystemLikeSave(e.save_data_type)) {
            image_v = DrawEntry(vg, theme, m_layout.Get(), v, selected, e.image, e.GetName(), author_str.c_str(), info.c_str(), e.selected);
        } else {
            image_v = DrawEntryNoImage(vg, theme, m_layout.Get(), v, selected, e.GetName(), author_str.c_str(), info.c_str(), e.selected);
            gfx::drawRect(vg, v, theme->GetColour(ThemeEntryID_GRID), 5);
            gfx::drawTextArgs(vg, image_v.x + image_v.w / 2, image_v.y + image_v.w / 2, 20, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT), detail::GetSystemSaveName(e.system_save_data_id));
        }

        if (e.is_backup && is_list) {
            DrawBackupSecondaryColumns(vg, theme, v, image_v, e, backup_cols, info.c_str());
        }

        // grey for deleted-game saves, yellow for backups, nothing otherwise.
        // framed on the whole tile so it reads in every layout.
        DrawCategoryBorder(vg, theme, v, e);

        DrawSelectionMark(vg, theme, m_layout.Get(), v, image_v, e.selected, m_selected_count > 0);

        // The divider rides above the first tile; later sections use an empty row.
        for (const auto& sec : g.sections) {
            if (sec.has_divider && disp == sec.first_display) {
                DrawSectionDivider(vg, theme, v, g, sec.label, sec.entry_start == 0 && sec.first_display == 0);
                break;
            }
        }
    });
}

void Menu::DrawBackupSecondaryColumns(NVGcontext* vg, Theme* theme, const Vec4& v, const Vec4& image_v, const Entry& e, const BackupColumnLayout& layout, const char* info) const {
    const float text_x = image_v.x + image_v.w + 14.f;
    const float text_y = v.y + v.h / 2.f + 12.f;
    const float text_clip_w = GetListTextClipWidth(vg, v, text_x, info);
    if (text_clip_w <= 0.f) {
        return;
    }

    const auto cols = GetBackupSecondaryColumns(e, m_accounts);
    const auto col = theme->GetColour(ThemeEntryID_TEXT_INFO);

    nvgSave(vg);
    nvgIntersectScissor(vg, text_x, 0.f, text_clip_w, SCREEN_HEIGHT);

    float cur_x = text_x;
    if (!cols.title_id.empty()) {
        gfx::drawText(vg, cur_x, text_y, 15.f, col, cols.title_id.c_str(), NVG_ALIGN_LEFT);
    }
    cur_x = text_x + layout.max_title_w;

    if (!cols.account.empty()) {
        gfx::drawText(vg, cur_x, text_y, 15.f, col, cols.account.c_str(), NVG_ALIGN_LEFT);
    }
    cur_x = cur_x + layout.max_account_w;

    if (!cols.timestamp.empty()) {
        gfx::drawText(vg, cur_x, text_y, 15.f, col, cols.timestamp.c_str(), NVG_ALIGN_LEFT);
    }
    cur_x = cur_x + layout.max_date_w;

    if (!cols.archive_count.empty()) {
        gfx::drawText(vg, cur_x, text_y, 15.f, col, cols.archive_count.c_str(), NVG_ALIGN_LEFT);
    }

    nvgRestore(vg);
}

void Menu::DrawCategoryBorder(NVGcontext* vg, Theme* theme, const Vec4& v, const Entry& e) {
    NVGcolor col;
    if (e.is_backup) {
        col = nvgRGB(0xF2, 0xC5, 0x22); // yellow: backup archive
    } else if (IsSystemLikeSave(e.save_data_type)) {
        return; // system saves are not games; leave them unframed
    } else if (m_installed_app_ids.contains(e.application_id)) {
        return; // installed game: ordinary save, no border
    } else {
        col = nvgRGB(0x9A, 0x9A, 0x9A); // grey: deleted-game save
    }

    const float thickness = (m_layout.Get() == grid::LayoutType_List) ? 2.f : 12.f;
    DrawInnerBorder(vg, v, col, thickness, 5.f);
}

void Menu::DrawSectionDivider(NVGcontext* vg, Theme* theme, const Vec4& first_v, const GridSections& g, const std::string& label, bool compact_first) const {
    const auto text_col = theme->GetColour(ThemeEntryID_TEXT_INFO);
    const auto line_col = theme->GetColour(ThemeEntryID_LINE_SEPARATOR);

    if (g.horizontal) {
        // sideways layout: a vertical rule in the empty column, label on top.
        const float cx = first_v.x - m_list->GetMaxX() + first_v.w / 2.f;
        gfx::drawRect(vg, cx - 1.f, first_v.y, 2.f, first_v.h, line_col);
        gfx::drawText(vg, cx, first_v.y - 8.f, 20.f, text_col, label.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
        return;
    }

    // In the grid's first section, use the space below the tabs; later
    // sections centre the rule in their reserved empty row.
    const float cy = compact_first ? first_v.y - 22.f : first_v.y - m_list->GetMaxY() + first_v.h / 2.f;
    const float dl = m_list->GetX();
    const float dr = m_list->GetX() + m_list->GetW();
    const float mid = (dl + dr) / 2.f;
    constexpr float font = 22.f;

    float bounds[4];
    nvgFontSize(vg, font);
    gfx::textBounds(vg, 0, 0, bounds, label.c_str());
    const float half_w = (bounds[2] - bounds[0]) / 2.f;
    constexpr float gap = 16.f;

    gfx::drawRect(vg, dl, cy - 1.f, std::max(0.f, (mid - half_w - gap) - dl), 2.f, line_col);
    const float rx = mid + half_w + gap;
    gfx::drawRect(vg, rx, cy - 1.f, std::max(0.f, dr - rx), 2.f, line_col);
    gfx::drawText(vg, mid, cy, font, text_col, label.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
}

void Menu::DrawCategoryTabs(NVGcontext* vg, Theme* theme) {
    const std::array<std::string, 3> tab_names{
        "Installed Games"_i18n,
        "Deleted Games"_i18n,
        "Backups"_i18n,
    };

    s64 selected_tab = -1;
    switch (m_category) {
        case Category::Installed: selected_tab = 0; break;
        case Category::Deleted:   selected_tab = 1; break;
        case Category::Backups:   selected_tab = 2; break;
        default:                  selected_tab = -1; break;
    }

    const float body_top = TAB_BAR_TOP + TAB_BAR_H;
    const float radius = 8.f;

    const auto surface = theme->GetColour(ThemeEntryID_POPUP);
    const auto recessed = theme->GetColour(ThemeEntryID_GRID);
    const auto accent = theme->GetColour(ThemeEntryID_HIGHLIGHT_1);
    const auto line = theme->GetColour(ThemeEntryID_LINE);

    for (size_t i = 0; i < tab_names.size(); i++) {
        if (static_cast<s64>(i) == selected_tab) {
            continue;
        }
        const float x = TAB_BAR_X + i * (TAB_ITEM_W + TAB_BAR_GAP);
        const float y = TAB_BAR_TOP + 5.f;
        const float h = body_top - y;
        gfx::drawRectVarying(vg, Vec4{x, y, TAB_ITEM_W, h}, recessed, radius, radius, 0.f, 0.f);
        const auto label_colour = theme->GetColour(ThemeEntryID_TEXT_INFO);
        const float text_y = (y + body_top) * 0.5f;
        gfx::drawText(vg, x + TAB_ITEM_W * 0.5f, text_y, 20.f, label_colour, tab_names[i].c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }

    gfx::drawRect(vg, TAB_BAR_X, body_top, TAB_BAR_W, 1.f, line);

    if (selected_tab >= 0 && selected_tab < static_cast<s64>(tab_names.size())) {
        const float x = TAB_BAR_X + selected_tab * (TAB_ITEM_W + TAB_BAR_GAP);
        const float y = TAB_BAR_TOP;
        const float h = body_top - y + 1.f;
        gfx::drawRectVarying(vg, Vec4{x, y, TAB_ITEM_W, h}, surface, radius, radius, 0.f, 0.f);
        gfx::drawRectVarying(vg, Vec4{x, y, TAB_ITEM_W, 3.f}, accent, radius, radius, 0.f, 0.f);
        const auto label_colour = theme->GetColour(ThemeEntryID_TEXT);
        const float text_y = (y + body_top) * 0.5f;
        gfx::drawTextBold(vg, x + TAB_ITEM_W * 0.5f, text_y, 20.f, label_colour, tab_names[selected_tab].c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }
}

} // namespace sphaira::ui::menu::save
