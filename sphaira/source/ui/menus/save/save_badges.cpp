#include "ui/menus/save/save_badges.hpp"
#include "ui/menus/save_list_info.hpp"
#include "ui/menus/grid_menu_base.hpp"

#include <algorithm>
#include <cstring>

namespace sphaira::ui::menu::save {

auto SaveBadgeColour(const char* label) -> NVGcolor {
    if (!std::strcmp(label, "Account")) {
        return nvgRGBA(0, 78, 190, 255);
    }
    if (!std::strcmp(label, "Device")) {
        return nvgRGBA(0, 128, 160, 255);
    }
    if (!std::strcmp(label, "BCAT")) {
        return nvgRGBA(112, 35, 175, 255);
    }
    if (!std::strcmp(label, "Cache")) {
        return nvgRGBA(190, 76, 0, 255);
    }
    if (!std::strcmp(label, "Temporary")) {
        return nvgRGBA(90, 100, 130, 255);
    }
    if (!std::strcmp(label, "System")) {
        return nvgRGBA(180, 24, 24, 255);
    }
    if (!std::strcmp(label, "System BCAT")) {
        return nvgRGBA(135, 28, 92, 255);
    }
    return nvgRGBA(100, 100, 100, 255);
}

void DrawSaveBadges(NVGcontext* vg, Theme*, const Vec4& image, const Entry& entry) {
    std::array<const char*, kMaxSaveBadges> labels{};
    const auto count = CollectSaveBadgeLabels(entry.save_types_mask, labels);
    if (!count) {
        return;
    }

    const bool compact = image.w < 80.f;
    const float margin = compact ? 2.f : 5.f;
    const float gap = compact ? 1.f : 3.f;
    const float padding = compact ? 5.f : 7.f;
    float font = std::min(compact ? 8.f : (image.w < 130.f ? 11.f : 13.f),
        (image.h - margin * 2.f - gap * (count - 1)) / count - padding);
    float bounds[4]{};
    float width = compact ? 20.f : 26.f;
    nvgFontSize(vg, font);
    gfx::textBounds(vg, 0, 0, bounds, "Account");
    width = std::max(width, bounds[2] - bounds[0] + (compact ? 6.f : 12.f));
    for (size_t i = 0; i < count; i++) {
        gfx::textBounds(vg, 0, 0, bounds, labels[i]);
        width = std::max(width, bounds[2] - bounds[0] + (compact ? 6.f : 12.f));
    }
    const float max_width = image.w - margin * 2.f;
    if (width > max_width) {
        const float text_padding = compact ? 6.f : 12.f;
        font *= (max_width - text_padding) / (width - text_padding);
        nvgFontSize(vg, font);
        width = max_width;
    }
    const float height = font + padding;

    const float x = image.x + margin;
    float y = image.y + margin;
    for (size_t i = 0; i < count; i++) {
        gfx::drawRect(vg, x - 1.f, y - 1.f, width + 2.f, height + 2.f, nvgRGBA(0, 0, 0, 255), 5.f);
        gfx::drawRect(vg, x, y, width, height, SaveBadgeColour(labels[i]), 4.f);
        nvgSave(vg);
        nvgIntersectScissor(vg, x, y, width, height);
        gfx::drawText(vg, x + width * 0.5f, y + height * 0.5f, font,
            nvgRGBA(255, 255, 255, 255), labels[i], NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgRestore(vg);
        y += height + gap;
    }
}

constexpr float LIST_BADGE_FONT = 12.f;
constexpr float LIST_BADGE_PAD_X = 7.f;
constexpr float LIST_BADGE_H = 20.f;
constexpr float LIST_BADGE_GAP = 4.f;

auto MeasureSaveListBadges(NVGcontext* vg, const Entry& entry) -> float {
    std::array<const char*, kMaxSaveBadges> labels{};
    const auto count = CollectSaveBadgeLabels(entry.save_types_mask, labels);
    if (!count) {
        return 0.f;
    }
    nvgFontSize(vg, LIST_BADGE_FONT);
    float bounds[4]{};
    float total = 0.f;
    for (size_t i = 0; i < count; i++) {
        gfx::textBounds(vg, 0, 0, bounds, labels[i]);
        total += bounds[2] - bounds[0] + LIST_BADGE_PAD_X * 2.f;
        if (i) {
            total += LIST_BADGE_GAP;
        }
    }
    return total;
}

void DrawSaveListBadges(NVGcontext* vg, const Vec4& row, const Entry& entry, bool has_size) {
    std::array<const char*, kMaxSaveBadges> labels{};
    const auto count = CollectSaveBadgeLabels(entry.save_types_mask, labels);
    if (!count) {
        return;
    }

    nvgFontSize(vg, LIST_BADGE_FONT);
    float widths[kMaxSaveBadges]{};
    float bounds[4]{};
    float total = 0.f;
    for (size_t i = 0; i < count; i++) {
        gfx::textBounds(vg, 0, 0, bounds, labels[i]);
        widths[i] = bounds[2] - bounds[0] + LIST_BADGE_PAD_X * 2.f;
        total += widths[i];
        if (i) {
            total += LIST_BADGE_GAP;
        }
    }

    float x_right = row.x + row.w - 15.f;
    if (has_size) {
        nvgFontSize(vg, 18.f);
        gfx::textBounds(vg, 0, 0, bounds, grid::LIST_INFO_VALUE_SAMPLE);
        x_right -= (bounds[2] - bounds[0]) + grid::LIST_INFO_COL_GAP;
        nvgFontSize(vg, LIST_BADGE_FONT);
    }

    const float y = row.y + (row.h - LIST_BADGE_H) / 2.f;
    float x = x_right - total;
    for (size_t i = 0; i < count; i++) {
        gfx::drawRect(vg, x - 1.f, y - 1.f, widths[i] + 2.f, LIST_BADGE_H + 2.f, nvgRGBA(0, 0, 0, 255), 4.f);
        gfx::drawRect(vg, x, y, widths[i], LIST_BADGE_H, SaveBadgeColour(labels[i]), 3.f);
        gfx::drawText(vg, x + widths[i] * 0.5f, y + LIST_BADGE_H * 0.5f, LIST_BADGE_FONT,
            nvgRGBA(255, 255, 255, 255), labels[i], NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        x += widths[i] + LIST_BADGE_GAP;
    }
}

} // namespace sphaira::ui::menu::save
