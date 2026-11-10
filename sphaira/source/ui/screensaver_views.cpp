#include "ui/screensaver_views.hpp"
#include "ui/screensaver.hpp"
#include "ui/nvg_util.hpp"
#include "ui/battery_gauge.hpp"
#include "ui/menus/menu_base.hpp"
#include "app.hpp"
#include "i18n.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sphaira::ui::saver_view {
namespace {

// the transfer readout's width: it drifts inside the margin left over, so a two hour install does not burn its outline into an OLED.
constexpr float BLOCK_W = 840.f;

auto FormatElapsed(u64 ns) -> std::string {
    const auto secs = ns / 1000000000ULL;
    char buf[32]{};
    if (secs >= 3600) {
        std::snprintf(buf, sizeof(buf), "%zuh %zum", (size_t)(secs / 3600), (size_t)(secs % 3600 / 60));
    } else {
        std::snprintf(buf, sizeof(buf), "%zum %zus", (size_t)(secs / 60), (size_t)(secs % 60));
    }
    return buf;
}

auto FieldEnabled(SaverField field) -> bool {
    return App::GetSaverFields() & field;
}

void DrawClock(NVGcontext* vg, float cx, float y, float size, NVGcolor col, const menu::PolledData& pdata) {
    if (App::Get12HourTimeEnable()) {
        gfx::drawTextArgs(vg, cx, y, size, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, col, "%02u:%02u %s",
            (pdata.tm.tm_hour == 0 || pdata.tm.tm_hour == 12) ? 12 : pdata.tm.tm_hour % 12, pdata.tm.tm_min, pdata.tm.tm_hour < 12 ? "AM" : "PM");
    } else {
        gfx::drawTextArgs(vg, cx, y, size, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, col, "%02u:%02u", pdata.tm.tm_hour, pdata.tm.tm_min);
    }
}

// ---- the battery calibration's readout: clock, status, a battery with an approximate percent, up to five columns of figures

// the clock's top to the column values' bottom (cy-190 .. cy+145), five 120 px columns across
constexpr SaverExtent BATTERY_EXTENT{330.f, 190.f, 145.f};

void DrawBattery(NVGcontext* vg, const SaverInfo& info, SaverRect block, const menu::PolledData& pdata) {
    // on the lit drain background the readout is dark: thin dark strokes leave the most pixels lit
    const auto text_col = info.bright ? nvgRGB(16, 16, 20) : nvgRGB(232, 232, 232);
    const auto dim_col = info.bright ? nvgRGB(56, 60, 70) : nvgRGB(140, 144, 154);
    const float cx = block.x + block.w / 2.f;
    const float y0 = block.y;

    DrawClock(vg, cx, y0, 100.f, text_col, pdata);
    gfx::drawTextArgs(vg, cx, y0 + 112.f, 26.f, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, dim_col, "%s", info.status.c_str());

    // the battery
    const float bw = 220.f, bh = 96.f, bx = cx - bw / 2.f - 8.f, by = y0 + 156.f;
    const float pct = std::clamp(info.battery_percent, 0, 100) / 100.f;
    const auto level = info.battery_dir > 0 ? nvgRGB(0x4C, 0xAF, 0x50) // charging is green, as on the drain page
        : pct > 0.5f ? nvgLerpRGBA(nvgRGB(0xF5, 0x9E, 0x0B), nvgRGB(0x4C, 0xAF, 0x50), (pct - 0.5f) * 2.f)
                                  : nvgLerpRGBA(nvgRGB(0xE5, 0x39, 0x35), nvgRGB(0xF5, 0x9E, 0x0B), pct * 2.f);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, bx, by, bw, bh, 12.f);
    nvgStrokeColor(vg, text_col);
    nvgStrokeWidth(vg, 4.f);
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, bx + bw, by + bh / 2.f - 20.f, 14.f, 40.f, 4.f);
    nvgFillColor(vg, text_col);
    nvgFill(vg);
    gfx::drawBatteryLevel(vg, bx + 8.f, by + 8.f, bw - 16.f, bh - 16.f, 7.f, pct, level, info.battery_dir);
    gfx::drawTextArgs(vg, bx + bw / 2.f, by + bh / 2.f, 44.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, text_col, "%d%%", info.battery_percent);
    gfx::drawBatteryArrow(vg, bx + bw - 34.f, by + bh / 2.f, 34.f, info.battery_dir, text_col);

    // the columns
    const float cols_y = by + bh + 30.f, col_w = 120.f;
    const size_t n = std::min<size_t>(info.columns.size(), 5);
    for (size_t i = 0; i < n; i++) {
        const float x = cx + (static_cast<float>(i) - (static_cast<float>(n) - 1.f) / 2.f) * col_w;
        gfx::drawTextArgs(vg, x, cols_y, 15.f, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, dim_col, "%s", info.columns[i].first.c_str());
        gfx::drawTextArgs(vg, x, cols_y + 22.f, 21.f, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, text_col, "%s", info.columns[i].second.c_str());
    }
}

// ---- the transfer readout: clock, status, counter, file, bar, one line of stats, errors, the completion line or the speed graph

constexpr float CLOCK_H = 86.f, STATUS_H = 32.f, COUNTER_H = 34.f;
constexpr float FILE_H = 32.f, BAR_H = 52.f, STATS_H = 32.f, ERRORS_H = 30.f;
constexpr float GRAPH_H = 115.f, COMPLETION_H = 50.f;

// which rows are on and how tall they make the block: the one place both Measure and Draw read it from
struct TransferRows {
    std::string stats{};
    bool clock{}, completion{}, status{}, counter{}, file{}, bar{}, errors{}, graph{};
    float total{};
};

auto BuildTransfer(const SaverInfo& info, const menu::PolledData& pdata) -> TransferRows {
    TransferRows r{};
    // speed / eta / elapsed / battery share one line, so how many of them are on decides whether that line takes any height at all.
    const auto add_stat = [&r](const std::string& value) {
        if (!value.empty()) {
            r.stats += r.stats.empty() ? value : "   ·   " + value;
        }
    };

    if (!info.stats_text.empty()) {
        add_stat(info.stats_text);
    }
    if (info.stats_text.empty() && FieldEnabled(SaverField_Speed)) {
        char buf[96]{};
        if (info.now_mib >= 0.0) {
            std::snprintf(buf, sizeof(buf), "%.1f MiB/s (%s %.1f)", info.now_mib, "Average"_i18n.c_str(), info.speed_mib);
        } else {
            std::snprintf(buf, sizeof(buf), "%.1f MiB/s", info.speed_mib);
        }
        add_stat(buf);
    }
    if (info.stats_text.empty() && FieldEnabled(SaverField_Eta) && !info.eta.empty()) {
        add_stat(info.eta + " " + "left"_i18n);
    }
    if (info.stats_text.empty() && FieldEnabled(SaverField_Elapsed) && info.elapsed_ns) {
        add_stat(FormatElapsed(info.elapsed_ns));
    }
    if (FieldEnabled(SaverField_Battery)) {
        char buf[32]{};
        std::snprintf(buf, sizeof(buf), "%s%u%%", pdata.charger_type != 0 ? "+" : "", pdata.battery_percetange);
        add_stat(buf);
    }

    r.clock = FieldEnabled(SaverField_Clock);
    r.completion = info.is_complete;
    r.status = FieldEnabled(SaverField_Status) && !info.status.empty() && !r.completion;
    r.counter = FieldEnabled(SaverField_Counter) && info.package_count;
    r.file = FieldEnabled(SaverField_File) && !info.file.empty();
    r.bar = FieldEnabled(SaverField_Bar);
    r.errors = FieldEnabled(SaverField_Errors) && info.failed;
    r.graph = FieldEnabled(SaverField_Graph) && info.has_graph && info.history_count >= 2 && !r.completion;

    r.total += r.clock ? CLOCK_H : 0.f;
    r.total += r.status ? STATUS_H : 0.f;
    r.total += r.counter ? COUNTER_H : 0.f;
    r.total += r.file ? FILE_H : 0.f;
    r.total += r.bar ? BAR_H : 0.f;
    r.total += !r.stats.empty() ? STATS_H : 0.f;
    r.total += r.errors ? ERRORS_H : 0.f;
    r.total += r.graph ? GRAPH_H + 10.f : 0.f;
    r.total += r.completion ? COMPLETION_H + 10.f : 0.f;
    return r;
}

void DrawGraph(NVGcontext* vg, const SaverInfo& info, float cx, float y) {
    const auto red = nvgRGBA(231, 76, 60, 255);
    const auto blue = nvgRGBA(52, 152, 219, 255);
    const float graph_w = 600.f;
    const Vec4 plot{cx - graph_w / 2.f, y, graph_w, 95.f};
    const float pad = 4.f;

    gfx::drawRect(vg, plot, nvgRGBA(20, 20, 25, 220), 5.f);

    s64 peak = 1;
    for (size_t i = 0; i < info.history_count; i++) {
        const auto idx = (info.history_index + 96 - info.history_count + i) % 96;
        peak = std::max({peak, info.read_history[idx], info.write_history[idx]});
    }
    const double peak_mib = (double)peak / (1024.0 * 1024.0);

    const auto nice_step = [](double range) -> double {
        double s = 1.0;
        while (true) {
            if (range <= s) return s;
            if (range <= s * 2.0) return s * 2.0;
            if (range <= s * 5.0) return s * 5.0;
            s *= 10.0;
        }
    };
    const double step_mib = nice_step(std::max(peak_mib, 1.0) / 4.0);
    int steps = 1;
    while (step_mib * steps < peak_mib) steps++;
    const double top = step_mib * steps * 1024.0 * 1024.0;

    nvgSave(vg);
    nvgIntersectScissor(vg, plot.x, plot.y, plot.w, plot.h);

    const auto grid_col = nvgRGBA(255, 255, 255, 30);
    const auto label_col = nvgRGBA(140, 144, 154, 255);
    const float inner_h = plot.h - pad * 2.f;
    for (int k = 0; k <= steps; k++) {
        const float gy = plot.y + plot.h - pad - inner_h * (float)k / (float)steps;
        nvgBeginPath(vg);
        nvgMoveTo(vg, plot.x + pad, gy);
        nvgLineTo(vg, plot.x + plot.w - pad, gy);
        nvgStrokeColor(vg, grid_col);
        nvgStrokeWidth(vg, 1.f);
        nvgStroke(vg);
        if (k > 0 && k == steps) {
            gfx::drawTextArgs(vg, plot.x + pad + 4.f, gy + 2.f, 11.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, label_col, "%g MiB/s", step_mib * k);
        }
    }

    const auto draw_line = [&](const std::array<s64, 96>& history, NVGcolor colour) {
        nvgBeginPath(vg);
        for (size_t i = 0; i < info.history_count; i++) {
            const auto idx = (info.history_index + 96 - info.history_count + i) % 96;
            const auto slot = 96 - info.history_count + i;
            const float px = plot.x + pad + (plot.w - pad * 2.f) * slot / 95.f;
            const double frac = std::clamp((double)history[idx] / top, 0.0, 1.0);
            const float py = plot.y + plot.h - pad - inner_h * (float)frac;
            if (i == 0) nvgMoveTo(vg, px, py);
            else nvgLineTo(vg, px, py);
        }
        nvgStrokeColor(vg, colour);
        nvgStrokeWidth(vg, 2.f);
        nvgStroke(vg);
    };

    nvgGlobalCompositeOperation(vg, NVG_LIGHTER);
    if (info.has_read) {
        draw_line(info.read_history, red);
    }
    draw_line(info.write_history, blue);
    nvgRestore(vg);
}

void DrawFileName(NVGcontext* vg, const SaverInfo& info, float cx, float left, float y, NVGcolor col) {
    nvgSave(vg);
    nvgIntersectScissor(vg, left, y, BLOCK_W, FILE_H);

    float font_sz = 22.f;
    nvgFontSize(vg, font_sz);
    float bounds[4]{};
    gfx::textBounds(vg, 0, 0, bounds, info.file.c_str());
    float text_w = bounds[2] - bounds[0];

    // slightly too long for BLOCK_W: shrink the font down to 18 px to fit
    if (text_w > BLOCK_W && font_sz > 18.f) {
        font_sz = std::max(18.f, font_sz * (BLOCK_W / text_w));
        nvgFontSize(vg, font_sz);
        gfx::textBounds(vg, 0, 0, bounds, info.file.c_str());
        text_w = bounds[2] - bounds[0];
    }

    if (text_w <= BLOCK_W) {
        gfx::drawTextArgs(vg, cx, y, font_sz, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, col, "%s", info.file.c_str());
    } else {
        // still too long: start at the left edge so the beginning of the title is never clipped
        gfx::drawTextArgs(vg, left, y, font_sz, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, col, "%s", info.file.c_str());
    }
    nvgRestore(vg);
}

void DrawTransfer(NVGcontext* vg, const SaverInfo& info, SaverRect block, const menu::PolledData& pdata) {
    // the readout is drawn on a black page, so it uses its own palette rather than the theme: a light theme would paint black text onto it.
    const auto text_col = nvgRGB(232, 232, 232);
    const auto dim_col = nvgRGB(140, 144, 154);
    const auto accent_col = nvgRGB(80, 170, 255);
    const auto bad_col = nvgRGB(235, 90, 80);
    const bool oled = App::GetSaverOled();

    const auto rows = BuildTransfer(info, pdata);
    const float left = block.x;
    const float cx = block.x + block.w / 2.f;
    float y = block.y;

    // the clock is deliberately the biggest thing on the panel: it is what a glance from across the room can actually resolve.
    if (rows.clock) {
        DrawClock(vg, cx, y, 76.f, text_col, pdata);
        y += CLOCK_H;
    }

    if (rows.status) {
        gfx::drawTextArgs(vg, cx, y, 24.f, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, info.failed ? bad_col : dim_col, "%s", info.status.c_str());
        y += STATUS_H;
    }

    if (rows.counter) {
        gfx::drawTextArgs(vg, cx, y, 26.f, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, text_col, "%s %zu / %zu",
            "Package"_i18n.c_str(), info.package, info.package_count);
        y += COUNTER_H;
    }

    if (rows.file) {
        DrawFileName(vg, info, cx, left, y, dim_col);
        y += FILE_H;
    }

    if (rows.bar) {
        const Vec4 bar{left, y + 28.f, BLOCK_W, 14.f};
        // a finished session has reset its byte counters, so its ratio reads 0; finished means the whole bar.
        const double ratio = rows.completion ? 1.0 : info.ratio;
        // OLED mode lights only the pixels that carry information, so the empty part of the track stays unlit; the filled length still reads.
        if (!oled) {
            gfx::drawRect(vg, bar, nvgRGBA(255, 255, 255, 40), 7.f);
        }
        gfx::drawRect(vg, bar.x, bar.y, std::max(4.f, bar.w * (float)ratio), bar.h, accent_col, 7.f);
        if (info.package_count) {
            gfx::drawTextArgs(vg, left, y, 22.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, dim_col, "%s: %zu", "Installed"_i18n.c_str(), info.installed);
        }
        gfx::drawTextArgs(vg, left + BLOCK_W, y, 22.f, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, accent_col, "%.0f%%", ratio * 100.0);
        y += BAR_H;
    }

    if (!rows.stats.empty()) {
        gfx::drawTextArgs(vg, cx, y, 22.f, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, dim_col, "%s", rows.stats.c_str());
        y += STATS_H;
    }

    if (rows.errors) {
        gfx::drawTextArgs(vg, cx, y, 22.f, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, bad_col, "%s: %zu", "Failed"_i18n.c_str(), info.failed);
        y += ERRORS_H;
    }

    if (rows.completion) {
        y += 10.f;
        const auto comp_col = info.is_failed ? bad_col : accent_col;
        const auto& comp_text = !info.status.empty() ? info.status : (info.is_failed ? "Finished with errors"_i18n : "Finished"_i18n);
        gfx::drawTextArgs(vg, cx, y, 32.f, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, comp_col, "%s", comp_text.c_str());
        y += COMPLETION_H;
    }

    if (rows.graph) {
        DrawGraph(vg, info, cx, y + 6.f);
    }
}

} // namespace

auto Measure(const SaverInfo& info, const menu::PolledData& pdata) -> SaverExtent {
    if (info.battery_view) {
        return BATTERY_EXTENT;
    }
    const float total = BuildTransfer(info, pdata).total;
    return {BLOCK_W / 2.f, total / 2.f, total / 2.f};
}

void Draw(NVGcontext* vg, const SaverInfo& info, SaverRect block, const menu::PolledData& pdata) {
    if (info.battery_view) {
        DrawBattery(vg, info, block, pdata);
    } else {
        DrawTransfer(vg, info, block, pdata);
    }
}

} // namespace sphaira::ui::saver_view
