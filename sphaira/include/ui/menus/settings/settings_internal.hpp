#pragma once

#include "ui/menus/settings_menu.hpp"
#include "option.hpp"
#include "ui/types.hpp"

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace sphaira::ui::menu::settings {

inline constexpr std::array TEXT_SCROLL_SPEED_ITEMS{
    "Slow",
    "Normal",
    "Fast",
};

// marks a Sources row as a user-added network location, so the per-location
// options menu is only offered on rows that actually are one.
inline constexpr const char* NETWORK_LOCATION_ID = "network_location";

auto ClampIndex(long index, long count) -> long;
auto OnOff(bool enabled) -> std::string;
auto SettingsValueColour(Theme* theme, const std::string& value, bool selected) -> NVGcolor;

auto MakeHeader(std::string label) -> SettingsItem;
auto MakeSeparator() -> SettingsItem;
auto MakeFolderItem(std::string label, std::string description, std::function<std::vector<SettingsItem>()> builder) -> SettingsItem;
auto MakeBoolItem(std::string label, std::string description, std::function<bool()> get, std::function<void(bool)> set) -> SettingsItem;
auto MakeOptionItem(std::string label, std::string description, option::OptionBool& option) -> SettingsItem;
auto ResolveItemIndex(const std::vector<SettingsItem>& items, s64 index, s64 from) -> s64;

auto SettingsItemTextX(const SettingsItem& item, float x) -> float;
void DrawSettingsItemKindIcon(NVGcontext* vg, Theme* theme, const SettingsItem& item, Vec4 v, bool selected);
void DrawActionListItem(NVGcontext* vg, Theme* theme, Vec4 v, const SettingsItem& item, bool selected);

auto ThemeValue() -> std::string;
auto BuildAutoUpdateItems() -> std::vector<SettingsItem>;
auto BuildHomebrewSearchPathsItems() -> std::vector<SettingsItem>;
auto BuildSaveBackupSearchPathsItems() -> std::vector<SettingsItem>;
auto BuildSavesCategoryItems() -> std::vector<SettingsItem>;
auto BuildForwarderItems() -> std::vector<SettingsItem>;
auto BuildScreenOffItems() -> std::vector<SettingsItem>;
auto BuildMtpStorageItems() -> std::vector<SettingsItem>;
auto BuildFtpItems() -> std::vector<SettingsItem>;
auto BuildThemeOptionItems() -> std::vector<SettingsItem>;

} // namespace sphaira::ui::menu::settings
