#pragma once

#include "ui/nvg_util.hpp"
#include "i18n.hpp"
#include "ui/menus/save/save_paths.hpp"
#include <string>

namespace sphaira::ui::menu::save {

constexpr float TAB_BAR_X = 40.f;
constexpr float TAB_BAR_W = 1200.f;
constexpr float TAB_BAR_TOP = 94.f;
constexpr float TAB_BAR_H = 44.f;
constexpr float TAB_BAR_GAP = 4.f;
constexpr float TAB_BAR_COUNT = 3.f;
constexpr float TAB_ITEM_W = (TAB_BAR_W - TAB_BAR_GAP * (TAB_BAR_COUNT - 1.f)) / TAB_BAR_COUNT;
constexpr Vec4 TAB_BAR_RECT{TAB_BAR_X, TAB_BAR_TOP, TAB_BAR_W, TAB_BAR_H};

extern UEvent g_change_uevent;

inline auto FormatSaveTypeLabel(u8 data_type) -> std::string {
    if (data_type == FsSaveDataType_Account) {
        return "Account";
    }
    return i18n::get(GetSaveTypeLabel(data_type));
}

} // namespace sphaira::ui::menu::save
