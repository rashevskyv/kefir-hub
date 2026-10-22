#include "ui/progress_box.hpp"
#include "ui/nvg_util.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace sphaira::ui {
namespace {

// small corner badge shown while a detached transfer is minimised via R3.
// speed_str may be empty if no sample has landed yet.
void DrawMiniBadge(NVGcontext* vg, Theme* theme, const std::string& title, s64 offset, s64 size, const std::string& speed_str) {
    // clamp the displayed fraction: see the note in ProgressBox::Draw().
    const s64 draw_offset = std::clamp<s64>(offset, 0, size);
    const u32 percentage = size ? (u32)(((double)draw_offset / (double)size) * 100.0) : 0;

    char text_buf[512];
    if (!size && offset > 0) {
        // total unknown (stream install): show what actually arrived instead of
        // a percentage that can't be computed. see ProgressBox::Draw().
        char done_str[32];
        if (offset >= 1024LL*1024LL) {
            std::snprintf(done_str, sizeof(done_str), "%.1f MiB", (double)offset / (1024.0 * 1024.0));
        } else {
            std::snprintf(done_str, sizeof(done_str), "%.1f KiB", (double)offset / 1024.0);
        }
        if (speed_str.empty()) {
            std::snprintf(text_buf, sizeof(text_buf), "%s (%s)", title.c_str(), done_str);
        } else {
            std::snprintf(text_buf, sizeof(text_buf), "%s (%s - %s)", title.c_str(), done_str, speed_str.c_str());
        }
    } else if (!size) {
        // idle badge (e.g. web server waiting, or between MTP files): a "0%" here
        // would just be noise, so show only the label.
        std::snprintf(text_buf, sizeof(text_buf), "%s", title.c_str());
    } else if (speed_str.empty()) {
        std::snprintf(text_buf, sizeof(text_buf), "%s (%u%%)", title.c_str(), percentage);
    } else {
        std::snprintf(text_buf, sizeof(text_buf), "%s (%u%% - %s)", title.c_str(), percentage, speed_str.c_str());
    }

    char expand_buf[64];
    std::snprintf(expand_buf, sizeof(expand_buf), " %s", "Expand"_i18n.c_str());

    float bounds[4];
    nvgFontSize(vg, 15.f);
    gfx::textBounds(vg, 0, 0, bounds, text_buf);
    float text_w = bounds[2] - bounds[0];

    nvgFontSize(vg, 13.f);
    gfx::textBounds(vg, 0, 0, bounds, expand_buf);
    float expand_w = bounds[2] - bounds[0];

    float w = std::max(340.f, 15.f + text_w + 20.f + expand_w + 15.f);
    w = std::min(w, SCREEN_WIDTH - 40.f);

    constexpr float h = 64.f;
    const float x = SCREEN_WIDTH - w - 20.f;
    constexpr float y = 20.f;

    gfx::drawRect(vg, x, y, w, h, theme->GetColour(ThemeEntryID_POPUP), 6);

    const float bar_x = x + 15.f;
    const float bar_y = y + h - 18.f;
    const float bar_w = w - 30.f;
    gfx::drawRect(vg, bar_x, bar_y, bar_w, 8.f, theme->GetColour(ThemeEntryID_PROGRESSBAR_BACKGROUND), 3);
    if (size) {
        gfx::drawRect(vg, bar_x, bar_y, ((float)draw_offset / (float)size) * bar_w, 8.f, theme->GetColour(ThemeEntryID_PROGRESSBAR), 3);
    }

    gfx::drawTextArgs(vg, x + 15.f, y + 8.f, 15.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "%s", text_buf);
    gfx::drawTextArgs(vg, x + w - 15.f, y + 8.f, 13.f, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT_INFO), "%s", expand_buf);
}

// write-speed plot as in the DBI install view: 1/2/5 x 10^n MiB/s grid, newest sample on the right,
auto ShortDuration(double seconds) -> std::string {
    const auto s = static_cast<u64>(std::max(0.0, seconds));
    char buf[32]{};
    if (s >= 3600) {
        std::snprintf(buf, sizeof(buf), "%lluh %02llum", (unsigned long long)(s / 3600), (unsigned long long)(s % 3600 / 60));
    } else {
        std::snprintf(buf, sizeof(buf), "%llum %02llus", (unsigned long long)(s / 60), (unsigned long long)(s % 60));
    }
    return buf;
}

// a "Now" figure averaged over the last few samples, the "Average" of the whole transfer, the time
// since it began and the time left at the average. `history` is a ring, `index` the next slot.
template<size_t N>
void DrawSpeedPlot(NVGcontext* vg, Theme* theme, const Vec4& plot, const std::array<s64, N>& history, size_t count, size_t index,
                   double average, double elapsed, s64 left_bytes) {
    const auto blue = nvgRGBA(52, 152, 219, 255);
    const float pad = 4.f;
    constexpr double MIB = 1024.0 * 1024.0;

    gfx::drawRect(vg, plot, theme->GetColour(ThemeEntryID_PROGRESSBAR_BACKGROUND), 3.f);

    double now_mib = 0.0;
    if (count) {
        const size_t n = std::min<size_t>(count, 4);
        s64 sum = 0;
        for (size_t i = 0; i < n; i++) {
            sum += history[(index + N - 1 - i) % N];
        }
        now_mib = (double)sum / (double)n / MIB;
    }
    const auto figure = [&](int row, const std::string& label, const std::string& value) {
        const float y = plot.y + row * plot.h / 4.f;
        gfx::drawTextArgs(vg, plot.x + plot.w + 12.f, y, 12.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT_INFO), "%s", label.c_str());
        gfx::drawTextBold(vg, plot.x + plot.w + 12.f, y + 13.f, 15.f, blue, value.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    };
    const auto mib = [](double v) {
        char buf[32]{};
        std::snprintf(buf, sizeof(buf), "%.1f MiB/s", v);
        return std::string{buf};
    };
    figure(0, "Now"_i18n, mib(now_mib));
    figure(1, "Average"_i18n, mib(average / MIB));
    figure(2, "Time elapsed"_i18n, ShortDuration(elapsed));
    figure(3, "Time left"_i18n, average > 0.0 ? ShortDuration((double)left_bytes / average) : std::string{"-"});

    if (count < 2) {
        return;
    }

    s64 peak = 1;
    for (size_t i = 0; i < count; i++) {
        peak = std::max(peak, history[(index + N - count + i) % N]);
    }
    const double peak_mib = (double)peak / MIB;
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
    const double top = step_mib * steps * MIB;

    nvgSave(vg);
    nvgIntersectScissor(vg, plot.x, plot.y, plot.w, plot.h);
    auto grid = theme->GetColour(ThemeEntryID_TEXT);
    grid.a = 0.12f;
    const float inner_h = plot.h - pad * 2.f;
    for (int k = 0; k <= steps; k++) {
        const float gy = plot.y + plot.h - pad - inner_h * (float)k / (float)steps;
        nvgBeginPath(vg);
        nvgMoveTo(vg, plot.x + pad, gy);
        nvgLineTo(vg, plot.x + plot.w - pad, gy);
        nvgStrokeColor(vg, grid);
        nvgStrokeWidth(vg, 1.f);
        nvgStroke(vg);
        if (k > 0) {
            gfx::drawTextArgs(vg, plot.x + pad + 4.f, gy + 2.f, 12.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT_INFO),
                k == steps ? "%g MiB/s" : "%g", step_mib * k);
        }
    }

    nvgBeginPath(vg);
    for (size_t i = 0; i < count; i++) {
        const auto slot = N - count + i; // the newest sample sits on the right edge
        const float x = plot.x + pad + (plot.w - pad * 2.f) * slot / (N - 1);
        const double frac = std::clamp((double)history[(index + N - count + i) % N] / top, 0.0, 1.0);
        const float y = plot.y + plot.h - pad - inner_h * (float)frac;
        if (i == 0) nvgMoveTo(vg, x, y);
        else nvgLineTo(vg, x, y);
    }
    nvgStrokeColor(vg, blue);
    nvgStrokeWidth(vg, 2.f);
    nvgStroke(vg);
    nvgRestore(vg);
}

} // namespace

auto ProgressBox::Draw(NVGcontext* vg, Theme* theme) -> void {
    mutexLock(&m_mutex);
    std::vector<u8> image_data{};
    std::swap(m_image_data, image_data);
    const double elapsed = m_timestamp.GetSecondsD();
    // once a second: every figure on the box changes with the samples, faster reads as flicker.
    if (elapsed >= 1.0) {
        m_timestamp.Update();
        const s64 bytes_sent = m_offset - m_last_offset;
        m_last_offset = m_offset;

        double current_speed = static_cast<double>(bytes_sent) / elapsed;
        if (current_speed < 0) {
            current_speed = 0;
        }

        m_speed_samples[m_speed_sample_index] = static_cast<s64>(current_speed);
        m_speed_sample_index = (m_speed_sample_index + 1) % m_speed_samples.size();
        m_speed_sample_count = std::min(m_speed_sample_count + 1, m_speed_samples.size());
        m_graph[m_graph_index] = static_cast<s64>(current_speed);
        m_graph_index = (m_graph_index + 1) % m_graph.size();
        m_graph_count = std::min(m_graph_count + 1, m_graph.size());

        s64 speed_sum{};
        for (size_t i = 0; i < m_speed_sample_count; i++) {
            speed_sum += m_speed_samples[i];
        }
        m_speed = speed_sum / static_cast<s64>(m_speed_sample_count);

        const double since_start = m_transfer_start.GetSecondsD();
        m_average = since_start > 0.0 ? static_cast<double>(m_offset) / since_start : 0.0;
    }

    const auto action = m_action;
    const auto title = m_title;
    const auto transfer = m_transfer;
    const auto size = m_size;
    const auto offset = m_offset;
    const auto speed = m_speed;
    const auto speed_sample_count = m_speed_sample_count;
    const auto last_offset = m_last_offset;
    const auto graph = m_graph;
    const auto graph_count = m_graph_count;
    const auto graph_index = m_graph_index;
    const double transfer_seconds = m_transfer_start.GetSecondsD();
    const double average_speed = m_average;
    // "left" from the sampled offset, so it moves with the other figures once a second.
    const s64 left_bytes = std::max<s64>(0, size - m_last_offset);
    // synthetic-unit transfers keep a meaningful ETA (units-left / units-per-sec
    // is still real seconds) but a bytes-derived "MiB/s" would be nonsense, so
    // only the speed figure is suppressed, not the time remaining.
    const auto hide_speed = m_hide_speed.load();
    auto image = m_image;

    if (m_is_image_pending) {
        FreeImage();
        image = m_image = m_image_pending;
        m_image_pending = 0;
        m_is_image_pending = false;
    }
    mutexUnlock(&m_mutex);

    if (!image_data.empty()) {
        FreeImage();
        image = m_image = nvgCreateImageMem(vg, 0, image_data.data(), image_data.size());
        m_own_image = true;
    }

    if (m_saver.OwnsScreen()) {
        SaverInfo info{};
        info.status = action;
        info.file = transfer;
        info.ratio = size > 0 ? std::clamp(static_cast<double>(offset) / static_cast<double>(size), 0.0, 1.0) : 0.0;
        info.speed_mib = average_speed / (1024.0 * 1024.0);
        // "Now" as on the plot: the last few samples.
        if (graph_count) {
            const size_t k = std::min<size_t>(graph_count, 4);
            s64 sum = 0;
            for (size_t i = 0; i < k; i++) {
                sum += graph[(graph_index + graph.size() - 1 - i) % graph.size()];
            }
            info.now_mib = static_cast<double>(sum) / static_cast<double>(k) / (1024.0 * 1024.0);
        }
        if (average_speed >= 1.0) {
            info.eta = ShortDuration(static_cast<double>(left_bytes) / average_speed);
        }
        info.elapsed_ns = static_cast<u64>(transfer_seconds * 1e9);
        // the saver plots 96 samples of a read and a write line; a box has only the one speed.
        info.has_graph = true;
        info.has_read = false;
        const size_t n = std::min<size_t>(graph_count, info.write_history.size());
        for (size_t i = 0; i < n; i++) {
            info.write_history[i] = graph[(graph_index + graph.size() - n + i) % graph.size()];
        }
        info.history_count = n;
        info.history_index = n % info.write_history.size();
        m_saver.Draw(vg, theme, info);
        return;
    }

    if (m_minimized) {
        std::string speed_str{};
        if (speed_sample_count >= 3 && speed > 0 && !hide_speed) {
            const double speed_mb = (double)speed / (1024.0 * 1024.0);
            char buf[32];
            if (speed_mb >= 0.01) {
                std::snprintf(buf, sizeof(buf), "%.1f MiB/s", speed_mb);
            } else {
                std::snprintf(buf, sizeof(buf), "%.1f KiB/s", (double)speed / 1024.0);
            }
            speed_str = buf;
        }
        DrawMiniBadge(vg, theme, transfer.empty() ? title : transfer, offset, size, speed_str);
        return;
    }

    // a job over many items (a bulk restore) has a second bar for the whole job: room for it.
    const s64 overall_total = m_overall_total.load();
    const s64 overall_done = std::clamp<s64>(m_overall_done.load(), 0, std::max<s64>(overall_total, 0));
    const bool overall_open = m_overall_open.load();
    const bool show_overall = (overall_total > 0 || overall_open) && !m_show_graph;
    if (m_detached || show_overall) {
        m_pos.h = 360.f;
        m_pos.y = (SCREEN_HEIGHT / 2.f) - (m_pos.h / 2.f);
    }

    if (!m_detached) {
        gfx::dimBackground(vg);
    }
    gfx::drawRect(vg, m_pos, theme->GetColour(ThemeEntryID_POPUP), 5);

    // The pop up shape.
    // const Vec4 box = { 255, 145, 770, 430 };
    const auto center_x = m_pos.x + m_pos.w/2;
    const auto end_y = m_pos.y + m_pos.h;
    const auto progress_bar_w = m_pos.w - 230;
    const Vec4 prog_bar = { center_x - progress_bar_w / 2, end_y - (m_detached ? 160.f : 115.f), progress_bar_w, 12 };
    // a job over many items: the current item's bar on top, the whole job's bar at the bottom; both as thin (8 px),
    // so the two read as one pair wherever this box is used (save restore, backups, ...)
    constexpr float PAIR_BAR_H = 8.f;
    // the detached box (MTP) has no room above its bar: the job bar goes below it and the item bar keeps its place
    const Vec4 item_bar = (show_overall && !m_detached) ? Vec4{prog_bar.x, prog_bar.y - 40.f, prog_bar.w, PAIR_BAR_H} : prog_bar;

    nvgSave(vg);
    nvgIntersectScissor(vg, GetX(), GetY(), GetW(), GetH());

    if (image) {
        gfx::drawImage(vg, GetX() + 25, GetY() + 25, 120, 120, image, 5);
    }

    if (m_show_graph && !m_detached) {
        DrawSpeedPlot(vg, theme, Vec4{m_pos.x + 70.f, m_pos.y + 196.f, m_pos.w - 220.f, 120.f}, graph, graph_count, graph_index,
            average_speed, transfer_seconds, left_bytes);
    }

    if (show_overall) {
        // the whole job: "Done: 3 / 40" and its own bar, below the bar of the current item, as thin as it.
        const Vec4 bar = m_detached ? Vec4{prog_bar.x, prog_bar.y + 44.f, prog_bar.w, PAIR_BAR_H}
                                    : Vec4{prog_bar.x, prog_bar.y + (prog_bar.h - PAIR_BAR_H) / 2.f, prog_bar.w, PAIR_BAR_H};
        gfx::drawRect(vg, bar, theme->GetColour(ThemeEntryID_PROGRESSBAR_BACKGROUND), 5);
        if (overall_total > 0) {
            const auto fraction = static_cast<float>(overall_done) / static_cast<float>(overall_total);
            gfx::drawRect(vg, bar.x, bar.y, bar.w * fraction, bar.h, theme->GetColour(ThemeEntryID_PROGRESSBAR), 5);
            gfx::drawTextArgs(vg, bar.x, bar.y - 6.f, 16.f, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, theme->GetColour(ThemeEntryID_TEXT_INFO),
                "%s %lld / %lld", "Done:"_i18n.c_str(), static_cast<long long>(overall_done), static_cast<long long>(overall_total));
            gfx::drawTextArgs(vg, bar.x + bar.w + 15.f, bar.y + bar.h / 2.f, 16.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT),
                "%d%%", static_cast<int>(fraction * 100.f));
        } else {
            // the total is not known: a segment runs along the bar, the line above counts what is copied
            const double t = std::fmod(static_cast<double>(armTicksToNs(armGetSystemTick())) / 1e9, 1.6) / 1.6;
            const float seg = bar.w * 0.25f;
            const float x0 = bar.x + (bar.w + seg) * static_cast<float>(t) - seg;
            const float xa = std::max(x0, bar.x);
            const float xb = std::min(x0 + seg, bar.x + bar.w);
            if (xb > xa) {
                gfx::drawRect(vg, xa, bar.y, xb - xa, bar.h, theme->GetColour(ThemeEntryID_PROGRESSBAR), 5);
            }
            const double mib = static_cast<double>(m_overall_bytes.load()) / (1024.0 * 1024.0);
            char size[32];
            if (mib >= 1024.0) {
                std::snprintf(size, sizeof(size), "%.2f GiB", mib / 1024.0);
            } else {
                std::snprintf(size, sizeof(size), "%.1f MiB", mib);
            }
            gfx::drawTextArgs(vg, bar.x, bar.y - 6.f, 16.f, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, theme->GetColour(ThemeEntryID_TEXT_INFO),
                "%s %lld %s, %s", "Copied so far:"_i18n.c_str(), static_cast<long long>(m_overall_files.load()), "files"_i18n.c_str(), size);
        }
    }

    // shapes.
    if (size) {
        const auto font_size = 18.F;
        const auto pad = 15.F;
        const float rounding = 5;

        gfx::drawRect(vg, item_bar, theme->GetColour(ThemeEntryID_PROGRESSBAR_BACKGROUND), rounding);
        // some transfer sources (e.g. the MTP copy callback) report a running
        // byte offset against a per-chunk size rather than the file total, which
        // would drive the bar/percentage past 100%. clamp the displayed fraction
        // so the bar never overflows; raw offset is still used for speed/ETA.
        const s64 draw_offset = std::clamp<s64>(offset, 0, size);
        const u32 percentage = ((double)draw_offset / (double)size) * 100.0;
        gfx::drawRect(vg, item_bar.x, item_bar.y, ((float)draw_offset / (float)size) * item_bar.w, item_bar.h, theme->GetColour(ThemeEntryID_PROGRESSBAR), rounding);
        gfx::drawTextArgs(vg, item_bar.x + item_bar.w + pad, item_bar.y + item_bar.h / 2.f, font_size, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT), "%u%%", percentage);

        if (speed_sample_count >= 3 && speed > 0) {
            const double speed_mb = (double)speed / (1024.0 * 1024.0);
            const double speed_kb = (double)speed / (1024.0);

            char speed_str[32];
            if (speed_mb >= 0.01) {
                std::snprintf(speed_str, sizeof(speed_str), "%.2f MiB/s", speed_mb);
            } else {
                std::snprintf(speed_str, sizeof(speed_str), "%.2f KiB/s", speed_kb);
            }

            const auto left = std::max<s64>(0, size - last_offset);
            // with the plot on screen the time left follows its "Average", not the jumpy recent speed.
            const s64 eta_speed = m_show_graph && average_speed >= 1.0 ? static_cast<s64>(average_speed) : speed;
            const auto left_seconds = left / eta_speed;
            const auto hours = left_seconds / (60 * 60);
            const auto minutes = left_seconds % (60 * 60) / 60;
            const auto seconds = left_seconds % 60;

            char time_str[64];
            if (hours) {
                std::snprintf(time_str, sizeof(time_str), "%zu hours %zu minutes remaining"_i18n.c_str(), hours, minutes);
            } else if (minutes) {
                std::snprintf(time_str, sizeof(time_str), "%zu minutes %zu seconds remaining"_i18n.c_str(), minutes, seconds);
            } else {
                std::snprintf(time_str, sizeof(time_str), "%zu seconds remaining"_i18n.c_str(), seconds);
            }

            if (hide_speed) {
                gfx::drawTextArgs(vg, center_x, prog_bar.y + prog_bar.h + 8.f, 18, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "%s", time_str);
            } else {
                gfx::drawTextArgs(vg, center_x, prog_bar.y + prog_bar.h + 8.f, 18, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "%s (%s)", time_str, speed_str);
            }
        }
    } else if (offset > 0) {
        // the total isn't known (a stream install: the transport tells us how
        // many bytes arrived, never how many are coming). reporting offset
        // against itself would peg the bar at 100% for the whole transfer, so
        // show a sliding bar plus the honest numbers we do have.
        const auto font_size = 18.F;
        const auto pad = 15.F;
        const float rounding = 5;

        gfx::drawRect(vg, item_bar, theme->GetColour(ThemeEntryID_PROGRESSBAR_BACKGROUND), rounding);
        // the segment travels from fully outside the left edge to fully past
        // the right edge, clipped to the bar, so it leaves and re-enters
        // instead of vanishing the moment it touches the right end.
        const float chase_w = item_bar.w * 0.25f;
        const double phase = std::fmod((double)(armTicksToNs(armGetSystemTick()) / 1000000ULL) / 2000.0, 1.0);
        const float chase_x = item_bar.x - chase_w + (float)phase * (item_bar.w + chase_w);
        nvgSave(vg);
        nvgIntersectScissor(vg, item_bar.x, item_bar.y, item_bar.w, item_bar.h);
        gfx::drawRect(vg, chase_x, item_bar.y, chase_w, item_bar.h, theme->GetColour(ThemeEntryID_PROGRESSBAR), rounding);
        nvgRestore(vg);

        char done_str[32];
        if (offset >= 1024LL*1024LL) {
            std::snprintf(done_str, sizeof(done_str), "%.1f MiB", (double)offset / (1024.0 * 1024.0));
        } else {
            std::snprintf(done_str, sizeof(done_str), "%.1f KiB", (double)offset / 1024.0);
        }
        gfx::drawTextArgs(vg, item_bar.x + item_bar.w + pad, item_bar.y + item_bar.h / 2.f, font_size, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT), "%s", done_str);

        if (speed_sample_count >= 3 && speed > 0 && !hide_speed) {
            const double speed_mb = (double)speed / (1024.0 * 1024.0);
            char speed_str[32];
            if (speed_mb >= 0.01) {
                std::snprintf(speed_str, sizeof(speed_str), "%.2f MiB/s", speed_mb);
            } else {
                std::snprintf(speed_str, sizeof(speed_str), "%.2f KiB/s", (double)speed / 1024.0);
            }
            gfx::drawTextArgs(vg, center_x, prog_bar.y + prog_bar.h + 8.f, 18, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "%s (%s)", "Receiving..."_i18n.c_str(), speed_str);
        } else {
            gfx::drawTextArgs(vg, center_x, prog_bar.y + prog_bar.h + 8.f, 18, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "%s", "Receiving..."_i18n.c_str());
        }
    }

    const auto draw_text = [&](ScrollingText& scroll, const std::string& txt, float y, float size, float pad, ThemeEntryID id){
        float bounds[4];
        nvgFontSize(vg, size);
        gfx::textBounds(vg, 0, 0, bounds, txt.c_str());

        const auto min_x = GetX() + pad;
        const auto title_x = std::max(min_x, center_x - (bounds[2] - bounds[0]) / 2);

        scroll.Draw(vg, true, title_x, y, GetW() - pad * 2, size, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(id), txt.c_str());
    };

    // route the action line through ScrollingText too: a long action such as
    // "Uploading: <long filename>.zip" would otherwise be drawn centred with no
    // width constraint and spill past the popup edges (only hard-clipped by the
    // outer scissor), which reads as overflow. now it scrolls within the box.
    draw_text(m_scroll_action, action, m_pos.y + 40, 24, 160, ThemeEntryID_TEXT);
    draw_text(m_scroll_title, title, m_pos.y + 100, 22, 160, ThemeEntryID_TEXT);
    if (!transfer.empty()) {
        draw_text(m_scroll_transfer, transfer, m_pos.y + 160, 18, 12, ThemeEntryID_TEXT_INFO);
    }

    // every transfer gets a stop button, including one with a known total: a
    // long copy is exactly the case where the user wants a visible way out, and
    // B alone is not discoverable. detached boxes never receive Update(), so
    // there the button is drawn for consistency but B on the owning menu is
    // what actually cancels.
    {
        const float btn_w = 200.f;
        const float btn_h = 40.f;
        const float btn_x = center_x - btn_w / 2.f;
        const float btn_y = m_detached ? (m_pos.y + m_pos.h - 95.f) : (m_pos.y + m_pos.h - 65.f);

        gfx::drawRect(vg, btn_x, btn_y, btn_w, btn_h, nvgRGBA(239, 68, 68, 255), 6.f);

        nvgFontSize(vg, 18.f);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
        nvgText(vg, center_x, btn_y + 10.f, "Stop"_i18n.c_str(), nullptr);
    }

    if (m_detached) {
        const auto minimize_hint = " " + "Minimize"_i18n;
        // Home suspends the whole console (all threads, including this transfer)
        // rather than just this app - we can't intercept the press itself, only warn.
        gfx::drawTextArgs(vg, center_x, end_y - 20.f, 14.f, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM, theme->GetColour(ThemeEntryID_TEXT_INFO),
            "%s    %s", minimize_hint.c_str(), "Pressing HOME will pause this transfer"_i18n.c_str());
    }

    nvgRestore(vg);
    if (!m_detached) {
        Widget::Draw(vg, theme);
    }
}

} // namespace sphaira::ui
