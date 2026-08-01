#include "app.hpp"
#include "ui/menus/grid_menu_base.hpp"
#include "ui/nvg_util.hpp"

#include <string_view>
#include <utility>

namespace sphaira::ui::menu::grid {
namespace {

// gap between the "[flags]" column and the size column, and the widest size
// string the latter has to hold ("1023.99 MB" is as long as FormatBytes gets).
constexpr float LIST_INFO_COL_GAP = 10.f;
constexpr const char* LIST_INFO_VALUE_SAMPLE = "1023.99 MB";

// callers build the right column as "[flags]  value" (see FormatListInfo in the
// game and save menus). Split it back apart so each half gets its own column;
// a string with no "]  " is all label if it is bracketed, all value otherwise.
auto SplitListInfo(std::string_view info) -> std::pair<std::string, std::string> {
    if (const auto sep = info.find("]  "); sep != info.npos) {
        return {std::string{info.substr(0, sep + 1)}, std::string{info.substr(sep + 3)}};
    }

    if (info.starts_with('[')) {
        return {std::string{info}, {}};
    }

    return {{}, std::string{info}};
}

} // namespace

auto FormatBytes(u64 bytes) -> std::string {
    char out[32];
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        std::snprintf(out, sizeof(out), "%.2f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024ULL * 1024ULL) {
        std::snprintf(out, sizeof(out), "%.2f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else if (bytes >= 1024ULL) {
        std::snprintf(out, sizeof(out), "%.2f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(out, sizeof(out), "%lu B", bytes);
    }
    return out;
}

void Menu::DrawSelectionMark(NVGcontext* vg, Theme* theme, int layout, const Vec4& row, const Vec4& overlay, bool marked, bool any_marked) {
    // a list row reads like a file browser row, so it marks like one: a
    // checkbox in the left gutter, drawn on every row while a selection is
    // under way so the ticked ones can be seen against the empty ones.
    if (layout == LayoutType_List) {
        if (any_marked) {
            gfx::drawCheckbox(vg, theme, row.x - 30.f, row.y + (row.h - gfx::CHECKBOX_SIZE) / 2.f, gfx::CHECKBOX_SIZE, marked);
        }
        return;
    }

    // a tile has no gutter to put a checkbox in, so it is tinted instead.
    if (marked) {
        gfx::drawRect(vg, overlay, theme->GetColour(ThemeEntryID_FOCUS), 5);
        gfx::drawText(vg, overlay.x + overlay.w / 2.f, overlay.y + overlay.h / 2.f, 24.f, "", nullptr, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_SELECTED));
    }
}

Vec4 Menu::DrawEntry(NVGcontext* vg, Theme* theme, int layout, const Vec4& v, bool selected, int image, const char* name, const char* author, const char* version, bool marked) {
    return DrawEntry(vg, theme, true, layout, v, selected, image, name, author, version, marked);
}

Vec4 Menu::DrawEntryNoImage(NVGcontext* vg, Theme* theme, int layout, const Vec4& v, bool selected, const char* name, const char* author, const char* version, bool marked) {
    return DrawEntry(vg, theme, false, layout, v, selected, 0, name, author, version, marked);
}

Vec4 Menu::DrawEntry(NVGcontext* vg, Theme* theme, bool draw_image, int layout, const Vec4& v, bool selected, int image, const char* name, const char* author, const char* version, bool marked) {
    const auto& [x, y, w, h] = v;

    auto text_id = ThemeEntryID_TEXT;
    if (selected) {
        text_id = ThemeEntryID_TEXT_SELECTED;
        gfx::drawRectOutline(vg, theme, 4.f, v, 5.f);
    } else if (layout != LayoutType_HbMenu) {
        DrawElement(v, ThemeEntryID_GRID);
    }

    if (layout == LayoutType_HbMenu) {
        // Draw card background (dark grey/black)
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, w, h, 5.f);
        nvgFillColor(vg, nvgRGB(32, 32, 32));
        nvgFill(vg);

        // Draw image (overlapping slightly with top banner to hide its top rounded corners)
        if (draw_image) {
            gfx::drawImage(vg, x, y + 23.f, w, h - 23.f, image ?: App::GetDefaultImage(), 5.f);
        }

        // Draw top banner (white) on top of the image
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, w, 28.f, 5.f);
        nvgFillColor(vg, nvgRGB(255, 255, 255));
        nvgFill(vg);
        
        // Flatten the bottom of the top banner
        nvgBeginPath(vg);
        nvgRect(vg, x, y + 5.f, w, 23.f);
        nvgFillColor(vg, nvgRGB(255, 255, 255));
        nvgFill(vg);

        // Keep the title inside the banner. The focused card scrolls long names;
        // unfocused cards show the beginning of the clipped title.
        constexpr float title_pad = 6.f;
        m_scroll_name.Draw(vg, selected, x + title_pad, y + 14.f, w - title_pad * 2.f,
            15.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, nvgRGB(32, 32, 32), name);

        return v;
    }

    // List layout: small icon + name/author on one row
    if (layout == LayoutType_List) {
        if (!selected) {
            DrawElement(v, ThemeEntryID_GRID);
        } else {
            gfx::drawRectOutline(vg, theme, 4.f, v, 5.f);
        }

        // over the row fill, under the content: which rows are in the selection
        // reads at a glance rather than one checkbox at a time.
        if (marked) {
            auto tint = theme->GetColour(ThemeEntryID_FOCUS);
            tint.a *= 0.35f;
            gfx::drawRect(vg, v, tint, 5.f);
        }

        const float icon_size = 46.f;
        const float icon_x = x + 10.f;
        const float icon_y = y + (h - icon_size) / 2.f;
        if (draw_image) {
            gfx::drawImage(vg, Vec4{icon_x, icon_y, icon_size, icon_size}, image ?: App::GetDefaultImage(), 4);
        }
        const float text_x = icon_x + icon_size + 14.f;

        // right column, DBI-style. The caller hands it over as "[flags]  size";
        // the two halves get their own columns so every "[...]" lands on the
        // same x instead of being pushed around by the width of its own size.
        float right_w = 0.f;
        if (version && *version) {
            const auto info_col = theme->GetColour(ThemeEntryID_TEXT_INFO);
            const auto [label, value] = SplitListInfo(version);
            const float right = x + w - 15.f;
            float bounds[4]{};

            nvgFontSize(vg, 18.f);
            // the column is as wide as the widest size string, measured rather
            // than guessed so a theme with a wider font doesn't clip it.
            gfx::textBounds(vg, 0, 0, bounds, LIST_INFO_VALUE_SAMPLE);
            const float value_w = value.empty() && label.empty() ? 0.f : bounds[2] - bounds[0];

            if (!value.empty()) {
                gfx::drawText(vg, right, y + h / 2.f, 18.f, info_col, value.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            }

            float label_w = 0.f;
            if (!label.empty()) {
                gfx::textBounds(vg, 0, 0, bounds, label.c_str());
                label_w = bounds[2] - bounds[0];
                gfx::drawText(vg, right - value_w - LIST_INFO_COL_GAP, y + h / 2.f, 18.f, info_col, label.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            }

            right_w = value_w + LIST_INFO_COL_GAP + label_w + 20.f;
        }

        const float text_clip_w = w - text_x + x - 15.f - right_w;
        m_scroll_name.Draw(vg, selected, text_x, y + h / 2.f - 12.f, text_clip_w, 18.f, NVG_ALIGN_LEFT, theme->GetColour(text_id), name);
        m_scroll_author.Draw(vg, selected, text_x, y + h / 2.f + 12.f, text_clip_w, 15.f, NVG_ALIGN_LEFT, theme->GetColour(ThemeEntryID_TEXT_INFO), author);
        return Vec4{icon_x, icon_y, icon_size, icon_size};
    }

    Vec4 image_v = v;

    if (layout == LayoutType_GridDetail) {
        image_v.x += 20;
        image_v.y += 20;
        image_v.w = 115;
        image_v.h = 115;

        const auto text_off = 148;
        const auto text_x = x + text_off;
        const auto text_clip_w = w - 30.f - text_off;
        const float font_size = 18;
        m_scroll_name.Draw(vg, selected, text_x, y + 45, text_clip_w, font_size, NVG_ALIGN_LEFT, theme->GetColour(text_id), name);
        m_scroll_author.Draw(vg, selected, text_x, y + 80, text_clip_w, font_size, NVG_ALIGN_LEFT, theme->GetColour(text_id), author);
        m_scroll_version.Draw(vg, selected, text_x, y + 115, text_clip_w, font_size, NVG_ALIGN_LEFT, theme->GetColour(text_id), version);
    } else {
        if (selected && layout != LayoutType_HbMenu) {
            gfx::drawAppLable(vg, theme, m_scroll_name, x, y, w, name);
        }
    }

    if (draw_image) {
        gfx::drawImage(vg, image_v, image ?: App::GetDefaultImage(), 5);
    }

    return image_v;
}

void Menu::OnLayoutChange(std::unique_ptr<List>& list, int layout) {
    m_scroll_name.Reset();
    m_scroll_author.Reset();
    m_scroll_version.Reset();

    // Keep list touch and clipping bounds between the persistent header/footer.
    // SELECTION_OUTLINE_PAD expands this to exactly x=30..1250, y=87..646.
    const Vec4 content_pos{40, 97, 1200, 539};

    switch (layout) {
        case LayoutType_List: {
            const Vec2 pad{0, 2};
            const Vec4 v{75, 110, 1130, 70};
            list = std::make_unique<List>(1, 7, content_pos, v, pad);
        }   break;

        case LayoutType_Grid: {
            const Vec2 pad{10, 10};
            const Vec4 v{93, 186, 174, 174};
            list = std::make_unique<List>(6, 6*2, content_pos, v, pad);
        }   break;

        case LayoutType_GridDetail: {
            const Vec2 pad{10, 10};
            const Vec4 v{75, 110, 370, 155};
            list = std::make_unique<List>(3, 3*3, content_pos, v, pad);
        }   break;

        case LayoutType_HbMenu: {
            const Vec2 pad{16, 0};
            const Vec4 v{80, 450, 140, 168};
            list = std::make_unique<List>(1, 7, content_pos, v, pad);
            list->SetLayout(List::Layout::HOME);
        }   break;

        default:
            break;
    }
}

void Menu::DrawHbMenuHeader(NVGcontext* vg, Theme* theme, int image, const char* name, const char* author, const char* version, const char* description) {
    // Large icon on the left
    const Vec4 icon_rect{80.f, 120.f, 200.f, 200.f};
    
    // Draw outline border first (with animation glow)
    gfx::drawRectOutline(vg, theme, 4.f, icon_rect, 10.f);
    
    // Draw image itself
    gfx::drawImage(vg, icon_rect, image ?: App::GetDefaultImage(), 10.f);

    // Text details on the right
    const float text_x = 310.f;

    // Title
    gfx::drawText(vg, text_x, 130.f, 32.f, theme->GetColour(ThemeEntryID_TEXT_SELECTED), name, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

    // Author
    float bounds[4]{};
    nvgSave(vg);
    nvgFontSize(vg, 20.f);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    
    const float author_y = 195.f;
    const char* author_prefix = "Author: ";
    nvgTextBounds(vg, text_x, author_y, author_prefix, nullptr, bounds);
    float author_val_x = bounds[2] + 10.f;
    
    // Draw Author Label (Bold - 3 passes)
    gfx::drawText(vg, text_x, author_y, 20.f, theme->GetColour(ThemeEntryID_TEXT_INFO), author_prefix, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    gfx::drawText(vg, text_x + 0.6f, author_y, 20.f, theme->GetColour(ThemeEntryID_TEXT_INFO), author_prefix, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    gfx::drawText(vg, text_x + 1.2f, author_y, 20.f, theme->GetColour(ThemeEntryID_TEXT_INFO), author_prefix, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    
    // Draw Author Value (Normal)
    const char* author_str = (author && strlen(author) > 0) ? author : "-";
    gfx::drawText(vg, author_val_x, author_y, 20.f, theme->GetColour(ThemeEntryID_TEXT), author_str, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

    // Version
    if (version && strlen(version) > 0) {
        const float version_y = 225.f;
        const char* version_prefix = "Version: ";
        nvgTextBounds(vg, text_x, version_y, version_prefix, nullptr, bounds);
        float version_val_x = bounds[2] + 10.f;
        
        // Draw Version Label (Bold - 3 passes)
        gfx::drawText(vg, text_x, version_y, 20.f, theme->GetColour(ThemeEntryID_TEXT_INFO), version_prefix, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        gfx::drawText(vg, text_x + 0.6f, version_y, 20.f, theme->GetColour(ThemeEntryID_TEXT_INFO), version_prefix, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        gfx::drawText(vg, text_x + 1.2f, version_y, 20.f, theme->GetColour(ThemeEntryID_TEXT_INFO), version_prefix, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        
        // Draw Version Value (Normal)
        gfx::drawText(vg, version_val_x, version_y, 20.f, theme->GetColour(ThemeEntryID_TEXT), version, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    }
    nvgRestore(vg);
}

} // namespace sphaira::ui::menu::grid
