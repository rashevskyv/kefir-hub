#pragma once

#include "ui/nvg_util.hpp"
#include "ui/menus/save_menu.hpp"

namespace sphaira::ui::menu::save {

auto SaveBadgeColour(const char* label) -> NVGcolor;
void DrawSaveBadges(NVGcontext* vg, Theme* theme, const Vec4& image, const Entry& entry);
auto MeasureSaveListBadges(NVGcontext* vg, const Entry& entry) -> float;
void DrawSaveListBadges(NVGcontext* vg, const Vec4& row, const Entry& entry, bool has_size);

} // namespace sphaira::ui::menu::save
