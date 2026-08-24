#pragma once

#include "ui/menus/menu_base.hpp"
#include "ui/scrolling_text.hpp"
#include "ui/list.hpp"
#include <string>
#include <memory>

namespace sphaira::ui::menu::grid {

enum LayoutType {
    LayoutType_List,
    LayoutType_Grid,
    LayoutType_GridDetail,
    LayoutType_HbMenu,
};

// "1.38 GB" / "294 KB", for the right-hand column of a list row.
auto FormatBytes(u64 bytes) -> std::string;

// gap between the flags/badges column and the size column. the sample is the
// widest string FormatBytes produces, so every row's size sits on the same x.
inline constexpr float LIST_INFO_COL_GAP = 10.f;
inline constexpr const char* LIST_INFO_VALUE_SAMPLE = "1023.99 MB";

struct Menu : MenuBase {
    using MenuBase::MenuBase;

protected:
    void OnLayoutChange(std::unique_ptr<List>& list, int layout);
    // `marked` is multi-select, not the cursor: a list row draws it as a tinted
    // band, which has to happen here because this is what fills the row.
    Vec4 DrawEntry(NVGcontext* vg, Theme* theme, int layout, const Vec4& v, bool selected, int image, const char* name, const char* author, const char* version, bool marked = false, float extra_right = 0.f);
    // same as above but doesn't draw image and returns image dimension.
    Vec4 DrawEntryNoImage(NVGcontext* vg, Theme* theme, int layout, const Vec4& v, bool selected, const char* name, const char* author, const char* version, bool marked = false, float extra_right = 0.f);
    void DrawHbMenuHeader(NVGcontext* vg, Theme* theme, int image, const char* name, const char* author, const char* version, const char* description = nullptr);
    // multi-select mark for one entry. `row` is the whole entry rect, `overlay`
    // the part a tile layout tints (usually its icon).
    void DrawSelectionMark(NVGcontext* vg, Theme* theme, int layout, const Vec4& row, const Vec4& overlay, bool marked, bool any_marked);

private:
    Vec4 DrawEntry(NVGcontext* vg, Theme* theme, bool draw_image, int layout, const Vec4& v, bool selected, int image, const char* name, const char* author, const char* version, bool marked, float extra_right);

private:
    ScrollingText m_scroll_name{};
    ScrollingText m_scroll_author{};
    ScrollingText m_scroll_version{};
};

} // namespace sphaira::ui::menu::grid
