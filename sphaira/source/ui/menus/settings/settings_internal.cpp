#include "ui/menus/settings/settings_internal.hpp"

#include "app.hpp"
#include "i18n.hpp"
#include "ui/nvg_util.hpp"

#include <algorithm>
#include <cmath>

namespace sphaira::ui::menu::settings {

auto ClampIndex(long index, long count) -> long {
    if (count <= 0) {
        return 0;
    }

    index %= count;
    if (index < 0) {
        index += count;
    }
    return index;
}

auto OnOff(bool enabled) -> std::string {
    return enabled ? "On"_i18n : "Off"_i18n;
}

auto SettingsValueColour(Theme* theme, const std::string& value, bool selected) -> NVGcolor {
    if (value == "On"_i18n) {
        return nvgRGBA(78, 210, 112, 255);
    }
    if (value == "Off"_i18n) {
        return nvgRGBA(135, 138, 148, 255);
    }
    return theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT_INFO);
}


auto MakeHeader(std::string label) -> SettingsItem {
    return { std::move(label), {}, {}, {}, SettingsItemKind::Header };
}


// headers are captions, not rows: the cursor steps over them in the direction
// it was already travelling and wraps around the ends, so a category that
// starts with a header can still be left by pressing up on its first row.
auto ResolveItemIndex(const std::vector<SettingsItem>& items, s64 index, s64 from) -> s64 {
    const auto count = static_cast<s64>(items.size());
    if (count <= 0) {
        return 0;
    }

    auto target = std::clamp<s64>(index, 0, count - 1);
    from = std::clamp<s64>(from, 0, count - 1);

    // the list wraps the index itself; comparing the two ends would read as a
    // move in the opposite direction, so name those two cases outright.
    s64 step;
    if (from == count - 1 && !target) {
        step = 1;
    } else if (!from && target == count - 1) {
        step = -1;
    } else {
        step = target >= from ? 1 : -1;
    }

    for (s64 i = 0; i < count && items[target].kind == SettingsItemKind::Header; i++) {
        target = (target + step + count) % count;
    }

    // all headers: nothing to land on, stay put.
    return items[target].kind == SettingsItemKind::Header ? from : target;
}

// a row that opens its own page in the right pane of the settings menu, with
// the category column left standing. The items are built when the folder is
// opened, so they always show current state.
auto MakeFolderItem(std::string label, std::string description, std::function<std::vector<SettingsItem>()> builder) -> SettingsItem {
    SettingsItem item{
        std::move(label),
        std::move(description),
        [](){ return std::string{}; },
        {},
        SettingsItemKind::Folder,
    };
    item.folder_items = std::move(builder);
    return item;
}

auto MakeBoolItem(std::string label, std::string description, std::function<bool()> get, std::function<void(bool)> set) -> SettingsItem {
    return {
        std::move(label),
        std::move(description),
        [get](){
            return OnOff(get());
        },
        [get, set](){
            set(!get());
        }
    };
}

auto MakeOptionItem(std::string label, std::string description, option::OptionBool& option) -> SettingsItem {
    return MakeBoolItem(
        std::move(label),
        std::move(description),
        [&option](){
            return option.Get();
        },
        [&option](bool enabled){
            option.Set(enabled);
        }
    );
}


auto SettingsItemTextX(const SettingsItem& item, float x) -> float {
    switch (item.kind) {
        case SettingsItemKind::Normal:
        case SettingsItemKind::Header:
            return x + 18.f;
        default:
            return x + 74.f;
    }
}


void DrawSettingsItemKindIcon(NVGcontext* vg, Theme* theme, const SettingsItem& item, Vec4 v, bool selected) {
    if (item.kind == SettingsItemKind::Normal || item.kind == SettingsItemKind::Header) {
        return;
    }

    const auto colour = theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT_INFO);
    const auto x = v.x + 18.f;
    const auto y = v.y + 15.f;

    nvgSave(vg);
    nvgStrokeColor(vg, colour);
    nvgStrokeWidth(vg, 3.f);
    nvgLineCap(vg, NVG_ROUND);
    nvgLineJoin(vg, NVG_ROUND);

    if (item.kind == SettingsItemKind::Folder) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y + 8.f, 42.f, 28.f, 5.f);
        nvgMoveTo(vg, x + 5.f, y + 9.f);
        nvgLineTo(vg, x + 5.f, y + 4.f);
        nvgLineTo(vg, x + 19.f, y + 4.f);
        nvgLineTo(vg, x + 24.f, y + 9.f);
        nvgLineTo(vg, x + 37.f, y + 9.f);
        nvgStroke(vg);
    } else if (item.kind == SettingsItemKind::Download) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, x + 21.f, y + 3.f);
        nvgLineTo(vg, x + 21.f, y + 25.f);
        nvgMoveTo(vg, x + 10.f, y + 16.f);
        nvgLineTo(vg, x + 21.f, y + 27.f);
        nvgLineTo(vg, x + 32.f, y + 16.f);
        nvgMoveTo(vg, x + 8.f, y + 36.f);
        nvgLineTo(vg, x + 34.f, y + 36.f);
        nvgStroke(vg);
    } else if (item.kind == SettingsItemKind::Favorite) {
        const float cx = x + 21.f;
        const float cy = y + 20.f;
        const float rOut = 15.f;
        const float rIn = 7.f;
        nvgBeginPath(vg);
        for (int i = 0; i < 10; ++i) {
            float r = (i % 2 == 0) ? rOut : rIn;
            float angle = -3.14159265f / 2.f + i * 3.14159265f / 5.f;
            float px = cx + r * std::cos(angle);
            float py = cy + r * std::sin(angle);
            if (i == 0) {
                nvgMoveTo(vg, px, py);
            } else {
                nvgLineTo(vg, px, py);
            }
        }
        nvgClosePath(vg);
        nvgStroke(vg);
    }

    nvgRestore(vg);
}


void DrawActionListItem(NVGcontext* vg, Theme* theme, Vec4 v, const SettingsItem& item, bool selected) {
    if (item.kind == SettingsItemKind::Header) {
        const auto colour = theme->GetColour(ThemeEntryID_TEXT_INFO);
        const float text_x = v.x + 18.f;
        const float text_y = v.y + v.h - 22.f;
        gfx::drawTextArgs(vg, text_x, text_y, 15.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, colour, "%s", item.label.c_str());
        float bounds[4];
        nvgFontSize(vg, 15.f);
        gfx::textBounds(vg, 0, 0, bounds, item.label.c_str());
        const float rule_x = text_x + (bounds[2] - bounds[0]) + 12.f;
        gfx::drawRect(vg, rule_x, text_y + 9.f, std::max(0.f, v.x + v.w - 20.f - rule_x), 1.f,
            theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
        return;
    }

    const auto label_id = selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;
    const auto value = item.value ? item.value() : std::string{};
    const auto value_width = value.empty() ? 0.f : 224.f;

    if (selected) {
        gfx::drawRect(vg, v, theme->GetColour(ThemeEntryID_SELECTED_BACKGROUND), 5.f);
        gfx::drawRectOutline(vg, theme, 4.f, v);
    } else {
        gfx::drawRect(vg, v.x, v.y + v.h, v.w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
    }

    DrawSettingsItemKindIcon(vg, theme, item, v, selected);
    const auto text_x = SettingsItemTextX(item, v.x);
    const auto text_offset = text_x - v.x;

    gfx::drawTextBox(
        vg, text_x, v.y + 10.f, 20.f, v.w - 18.f - value_width - text_offset,
        theme->GetColour(label_id), item.label.c_str()
    );
    if (!item.description.empty()) {
        gfx::drawTextBox(
            vg, text_x, v.y + 39.f, 14.f, v.w - 18.f - value_width - text_offset,
            theme->GetColour(ThemeEntryID_TEXT_INFO), item.description.c_str()
        );
    }

    if (!value.empty()) {
        gfx::drawText(
            vg, v.x + v.w - 20.f, v.y + 21.f, 18.f,
            SettingsValueColour(theme, value, selected),
            value.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP
        );
    }

    if (item.kind == SettingsItemKind::Folder) {
        const float x1 = v.x + v.w - 24.f;
        const float y1 = v.y + v.h / 2.f;
        nvgBeginPath(vg);
        nvgMoveTo(vg, x1 - 8.f, y1 - 8.f);
        nvgLineTo(vg, x1, y1);
        nvgLineTo(vg, x1 - 8.f, y1 + 8.f);
        nvgStrokeColor(vg, theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT_INFO));
        nvgStrokeWidth(vg, 3.f);
        nvgLineCap(vg, NVG_ROUND);
        nvgLineJoin(vg, NVG_ROUND);
        nvgStroke(vg);
    }
}


} // namespace sphaira::ui::menu::settings
