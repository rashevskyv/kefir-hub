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
// a "Now" figure averaged over the last few samples and the "Average" of the whole transfer.
// `history` is a ring, `index` the next slot.
template<size_t N>
void DrawSpeedPlot(NVGcontext* vg, Theme* theme, const Vec4& plot, const std::array<s64, N>& history, size_t count, size_t index, double average) {
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
    const auto figure = [&](float y, const std::string& label, double mib) {
        gfx::drawTextArgs(vg, plot.x + plot.w + 12.f, y, 13.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT_INFO), "%s", label.c_str());
        char buf[32]{};
        std::snprintf(buf, sizeof(buf), "%.1f MiB/s", mib);
        gfx::drawTextBold(vg, plot.x + plot.w + 12.f, y + 18.f, 16.f, blue, buf, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    };
    figure(plot.y + 8.f, "Now"_i18n, now_mib);
    figure(plot.y + plot.h * 0.5f + 8.f, "Average"_i18n, average / MIB);

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
    if (elapsed >= 0.5) {
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
    const double average_speed = transfer_seconds > 0.0 ? static_cast<double>(offset) / transfer_seconds : 0.0;
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

    if (m_detached) {
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

    nvgSave(vg);
    nvgIntersectScissor(vg, GetX(), GetY(), GetW(), GetH());

    if (image) {
        gfx::drawImage(vg, GetX() + 25, GetY() + 25, 120, 120, image, 5);
    }

    if (m_show_graph && !m_detached) {
        DrawSpeedPlot(vg, theme, Vec4{m_pos.x + 70.f, m_pos.y + 196.f, m_pos.w - 220.f, 120.f}, graph, graph_count, graph_index, average_speed);
    }

    // shapes.
    if (size) {
        const auto font_size = 18.F;
        const auto pad = 15.F;
        const float rounding = 5;

        gfx::drawRect(vg, prog_bar, theme->GetColour(ThemeEntryID_PROGRESSBAR_BACKGROUND), rounding);
        // some transfer sources (e.g. the MTP copy callback) report a running
        // byte offset against a per-chunk size rather than the file total, which
        // would drive the bar/percentage past 100%. clamp the displayed fraction
        // so the bar never overflows; raw offset is still used for speed/ETA.
        const s64 draw_offset = std::clamp<s64>(offset, 0, size);
        const u32 percentage = ((double)draw_offset / (double)size) * 100.0;
        gfx::drawRect(vg, prog_bar.x, prog_bar.y, ((float)draw_offset / (float)size) * prog_bar.w, prog_bar.h, theme->GetColour(ThemeEntryID_PROGRESSBAR), rounding);
        gfx::drawTextArgs(vg, prog_bar.x + prog_bar.w + pad, prog_bar.y, font_size, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "%u%%", percentage);

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
            const auto left_seconds = left / speed;
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

        gfx::drawRect(vg, prog_bar, theme->GetColour(ThemeEntryID_PROGRESSBAR_BACKGROUND), rounding);
        // the segment travels from fully outside the left edge to fully past
        // the right edge, clipped to the bar, so it leaves and re-enters
        // instead of vanishing the moment it touches the right end.
        const float chase_w = prog_bar.w * 0.25f;
        const double phase = std::fmod((double)(armTicksToNs(armGetSystemTick()) / 1000000ULL) / 2000.0, 1.0);
        const float chase_x = prog_bar.x - chase_w + (float)phase * (prog_bar.w + chase_w);
        nvgSave(vg);
        nvgIntersectScissor(vg, prog_bar.x, prog_bar.y, prog_bar.w, prog_bar.h);
        gfx::drawRect(vg, chase_x, prog_bar.y, chase_w, prog_bar.h, theme->GetColour(ThemeEntryID_PROGRESSBAR), rounding);
        nvgRestore(vg);

        char done_str[32];
        if (offset >= 1024LL*1024LL) {
            std::snprintf(done_str, sizeof(done_str), "%.1f MiB", (double)offset / (1024.0 * 1024.0));
        } else {
            std::snprintf(done_str, sizeof(done_str), "%.1f KiB", (double)offset / 1024.0);
        }
        gfx::drawTextArgs(vg, prog_bar.x + prog_bar.w + pad, prog_bar.y, font_size, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "%s", done_str);

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
