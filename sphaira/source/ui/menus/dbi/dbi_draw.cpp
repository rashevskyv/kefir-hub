#if ENABLE_NETWORK_INSTALL

#include "ui/menus/dbi/dbi_internal.hpp"
#include "ui/menus/install_plan.hpp"
#include "path_util.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "ui/error_box.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/sidebar.hpp"
#include "swkbd.hpp"
#include "usb/usbds.hpp"
#include "utils/devoptab_curl_thread.hpp"
#include "utils/utils.hpp"
#include "yati/source/file.hpp"
#include <usbhsfs.h>

#include "title_info.hpp"
#include "version_compare.hpp"
#include "ui/menus/settings/settings_fs_utils.hpp"

#include <algorithm>
#include <cstdio>
#include <optional>
#include <ranges>

namespace sphaira::ui::menu::dbi {
void Menu::Draw(NVGcontext* vg, Theme* theme) {
    if (m_minimized) {
        DrawMiniBadge(vg, theme);
        return;
    }

    // anything stacked above this menu takes Update() with it, so the blank
    // would have no way left to wake: bring the panel back rather than hide a
    // dialog nobody can read behind it.
    if (m_screensaver.IsActive() && !App::OwnsFooter(this)) {
        m_screensaver.Stop();
    }

    if (m_screensaver.OwnsScreen()) {
        m_screensaver.Draw(vg, theme, ComputeSaverInfo());
        return;
    }

    const auto state = m_state.load();
    if (state != State::WaitingForUsb && state != State::WaitingForList && state != State::Analysing && state != State::ReviewQueue) {
        InstallSession::Draw(vg, theme);
        return;
    }

    MenuBase::Draw(vg, theme);

    if (state == State::WaitingForUsb || state == State::WaitingForList || state == State::Analysing) {
        UsbState usb_state{UsbState_Detached};
        usbDsGetState(&usb_state);

        UsbDeviceSpeed speed{(UsbDeviceSpeed)UsbDeviceSpeed_None};
        usbDsGetSpeed(&speed);

        const bool is_usb3_forced = !settings::detail::IniValueEquals("/atmosphere/config/system_settings.ini", "usb", "usb30_force_enabled", "u8!0x0");
        const bool is_super_speed = (speed == UsbDeviceSpeed_Super);
        const bool is_usb3 = is_usb3_forced || is_super_speed;

        // Draw USB Status Pill/Badge at y = 180.f
        {
            std::string usb_status_text;
            if (state == State::WaitingForUsb && usb_state == UsbState_Detached) {
                usb_status_text = is_usb3
                    ? "USB 3.0 Enabled · Waiting for PC connection"_i18n
                    : "USB 2.0 · Waiting for PC connection"_i18n;
            } else if (is_super_speed) {
                usb_status_text = "USB 3.0 SuperSpeed (5 Gbps)"_i18n;
            } else if (is_usb3_forced) {
                usb_status_text = "USB 3.0 Enabled · Link: USB 2.0 High Speed (480 Mbps)"_i18n;
            } else {
                usb_status_text = "USB 2.0 High Speed (480 Mbps)"_i18n;
            }

            const float badge_h = 36.f;
            const float badge_y = 180.f;
            const float badge_font = 16.f;
            nvgFontSize(vg, badge_font);
            float b[4]{};
            gfx::textBounds(vg, 0, 0, b, usb_status_text.c_str());
            const float text_w = b[2] - b[0];
            const float icon_sz = 22.f;
            const float badge_w = text_w + icon_sz + 36.f;
            const float badge_x = (SCREEN_WIDTH - badge_w) / 2.f;

            const NVGcolor bg_col = is_usb3
                ? nvgRGBA(20, 50, 75, 200)
                : nvgRGBA(45, 45, 50, 180);
            const NVGcolor border_col = is_usb3
                ? nvgRGBA(80, 170, 255, 180)
                : nvgRGBA(90, 90, 95, 150);
            const NVGcolor icon_col = is_usb3
                ? nvgRGBA(80, 170, 255, 255)
                : theme->GetColour(ThemeEntryID_TEXT_INFO);
            const NVGcolor text_col = is_usb3
                ? nvgRGBA(230, 245, 255, 255)
                : theme->GetColour(ThemeEntryID_TEXT);

            gfx::drawRect(vg, badge_x, badge_y, badge_w, badge_h, bg_col, 8.f);
            nvgBeginPath(vg);
            nvgRoundedRect(vg, badge_x, badge_y, badge_w, badge_h, 8.f);
            nvgStrokeColor(vg, border_col);
            nvgStrokeWidth(vg, 1.5f);
            nvgStroke(vg);

            // Draw USB trident icon
            gfx::drawUsbIcon(vg, badge_x + 12.f, badge_y + (badge_h - icon_sz) / 2.f, icon_sz, icon_col);

            // Draw status text
            gfx::drawTextArgs(vg, badge_x + 12.f + icon_sz + 8.f, badge_y + badge_h * 0.5f, badge_font,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, text_col, "%s", usb_status_text.c_str());
        }

        const auto text = state == State::WaitingForUsb
            ? "Waiting for PC connection. Connect the USB cable and make sure the console is detected."_i18n
            : state == State::WaitingForList
                ? "PC connected. Now pick the packages in your PC app and start the transfer.\n\nDBI Backend, ns-usbloader (Awoo/Tinfoil or GoldLeaf) and fluffy are all understood."_i18n
                : "Analysing packages (nothing is being installed)..."_i18n;

        const float main_text_y = 250.f;
        const float main_text_w = 1000.f;
        const float main_text_x = (SCREEN_WIDTH - main_text_w) / 2.f;

        float text_bounds[4]{};
        nvgFontSize(vg, 24.f);
        nvgTextBoxBounds(vg, main_text_x, main_text_y, main_text_w, text.c_str(), nullptr, text_bounds);

        gfx::drawTextBox(vg, main_text_x, main_text_y, 24.f, main_text_w,
            theme->GetColour(ThemeEntryID_TEXT_INFO), text.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP, nullptr, 1.35f);

        if (!m_local_fs && App::IsApplet()) {
            const auto warning = "Applet Mode has limited memory. NSZ packages are unlikely to install. Use Title Mode for reliable installation."_i18n;
            const float warn_y = std::max(text_bounds[3] + 35.f, 470.f);
            const float warn_w = 900.f;
            const float warn_x = (SCREEN_WIDTH - warn_w) / 2.f;
            const float warn_pad_y = 12.f;

            float warn_bounds[4]{};
            nvgFontSize(vg, 19.f);
            nvgTextBoxBounds(vg, warn_x + 20.f, warn_y + warn_pad_y, warn_w - 40.f, warning.c_str(), nullptr, warn_bounds);
            const float warn_h = (warn_bounds[3] - warn_bounds[1]) + warn_pad_y * 2.f;

            // Soft warning card container
            gfx::drawRect(vg, warn_x, warn_y, warn_w, warn_h, nvgRGBA(140, 40, 35, 45), 6.f);
            nvgBeginPath(vg);
            nvgRoundedRect(vg, warn_x, warn_y, warn_w, warn_h, 6.f);
            nvgStrokeColor(vg, nvgRGBA(220, 80, 70, 140));
            nvgStrokeWidth(vg, 1.5f);
            nvgStroke(vg);

            gfx::drawTextBox(vg, warn_x + 20.f, warn_y + warn_pad_y, 19.f, warn_w - 40.f,
                nvgRGBA(255, 180, 175, 255), warning.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP, nullptr, 1.3f);
        }
        return;
    }
    if (state == State::Failed) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f - 40.f, 28.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_ERROR), "USB session failed"_i18n.c_str());
        if (!m_fail_reason.empty()) {
            gfx::drawTextBox(vg, (SCREEN_WIDTH - 1000.f) / 2.f, SCREEN_HEIGHT / 2.f, 22.f, 1000.f,
                theme->GetColour(ThemeEntryID_TEXT_INFO), m_fail_reason.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP, nullptr, 1.3f);
        }
        return;
    }

    SCOPED_MUTEX(&m_mutex);
    if (state == State::ReviewQueue) {
        // decide up front where every selected package lands, so the bars, the
        // per row "Target" text and the install itself all agree.
        RecomputePlan();

        s64 selected_size{};
        size_t selected_count{};
        s64 sd_required{}, nand_required{};
        s64 sd_focus{}, nand_focus{};
        for (s64 i = 0; i < static_cast<s64>(m_queue.size()); i++) {
            const auto& entry = m_queue[i];
            if (entry.selected && R_SUCCEEDED(entry.analysis_result)) {
                const auto size = PlanSize(entry);
                const u64 title_id = GetQueueEntryTitleId(entry);
                if (!IsTitleAlreadyInstalled(title_id)) {
                    AddSizeSaturated(selected_size, size);
                    AddSizeSaturated(entry.planned_sd ? sd_required : nand_required, size);
                    // the row under the cursor gets its own colour inside the bar.
                    if (i == m_index) {
                        (entry.planned_sd ? sd_focus : nand_focus) = size;
                    }
                }
                selected_count++;
            }
        }
        // preview the required space on the NAND/SD bars in the status area.
        SetStorageProjection(nand_required, sd_required, nand_focus, sd_focus);
        const auto spaces = GetPolledData();
        const bool global = App::GetSaveSettingsGlobally();
        const auto reserve_nand = static_cast<s64>(global ? App::GetInstallReserveMb() : m_session_reserve_mb) * 1024 * 1024;
        const auto reserve_sd = static_cast<s64>(global ? App::GetInstallReserveSdMb() : m_session_reserve_sd_mb) * 1024 * 1024;
        const auto info_col = theme->GetColour(ThemeEntryID_TEXT_INFO);
        DrawStatRow(vg, info_col, 70.f, GetY() + 10.f, 17.f, {
            {"Targets"_i18n, "Per package"_i18n},
            {"Selected"_i18n, std::to_string(selected_count) + " / " + std::to_string(m_queue.size())},
            {"Required"_i18n, utils::formatSizeStorage(selected_size)},
            {"microSD"_i18n, utils::formatSizeStorage(sd_required)},
            {"NAND"_i18n, utils::formatSizeStorage(nand_required)},
        });
        DrawStatRow(vg, info_col, 70.f, GetY() + 36.f, 15.f, {
            {"microSD free"_i18n, utils::formatSizeStorage(std::max<s64>(0, spaces.sd_free - reserve_sd))},
            {"NAND free"_i18n, utils::formatSizeStorage(std::max<s64>(0, spaces.nand_free - reserve_nand))},
            {"Reserve"_i18n, utils::formatSizeStorage(reserve_sd) + " / " + utils::formatSizeStorage(reserve_nand)},
        });

        m_list->Draw(vg, theme, m_queue.size(), [this](NVGcontext* vg, Theme* theme, Vec4 v, s64 index) {
            const auto& entry = m_queue[index];
            if (index == m_index) gfx::drawRectOutline(vg, theme, 4.f, v);
            const auto colour = R_FAILED(entry.analysis_result) ? theme->GetColour(ThemeEntryID_ERROR) : theme->GetColour(ThemeEntryID_TEXT);
            if (R_SUCCEEDED(entry.analysis_result)) DrawCheckbox(vg, theme, v, entry.selected);
            gfx::drawTextArgs(vg, v.x + (R_SUCCEEDED(entry.analysis_result) ? 42.f : 12.f), v.y + 8.f, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, colour,
                R_FAILED(entry.analysis_result) ? "! %s" : "%s", entry.file_name.c_str());
            if (R_FAILED(entry.analysis_result)) {
                gfx::drawTextArgs(vg, v.x + 42.f, v.y + 40.f, 14.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, colour,
                    "%s: %s", "Analysis failed"_i18n.c_str(), ResultText(entry.analysis_result).c_str());
            } else if (entry.analysis_deferred) {
                const auto source_size = entry.analysis.source_size > 0
                    ? utils::formatSizeStorage(entry.analysis.source_size) : "Unknown"_i18n;
                const auto target = entry.target == InstallTarget::Auto
                    ? TargetName(entry.target) + " → " + (entry.planned_sd ? "microSD"_i18n : "System memory"_i18n)
                    : TargetName(entry.target);
                gfx::drawTextArgs(vg, v.x + 42.f, v.y + 40.f, 14.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
                    theme->GetColour(ThemeEntryID_TEXT_INFO), "%s: %s    %s: %s    %s: %s",
                    "Package size"_i18n.c_str(), source_size.c_str(),
                    "Install size"_i18n.c_str(), "Calculated during install"_i18n.c_str(),
                    "Target"_i18n.c_str(), target.c_str());
            } else {
                const auto kind = entry.analysis.size_kind == yati::AnalysisSizeKind::Exact ? "Exact"_i18n : "Estimate"_i18n;
                const auto target = entry.target == InstallTarget::Auto
                    ? TargetName(entry.target) + " → " + (entry.planned_sd ? "microSD"_i18n : "System memory"_i18n)
                    : TargetName(entry.target);
                gfx::drawTextArgs(vg, v.x + 42.f, v.y + 40.f, 14.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
                    theme->GetColour(ThemeEntryID_TEXT_INFO), "%s: %s    %s: %s (%s)    %s: %s",
                    "Package size"_i18n.c_str(), utils::formatSizeStorage(entry.analysis.source_size).c_str(),
                    "Install size"_i18n.c_str(), utils::formatSizeStorage(entry.analysis.install_size).c_str(), kind.c_str(),
                    "Target"_i18n.c_str(), target.c_str());
            }
        });
        return;
    }
}

void InstallSession::DrawMiniBadge(NVGcontext* vg, Theme* theme) {
    const float bw = 260.f;
    const float bh = 40.f;
    const float bx = SCREEN_WIDTH - bw - 20.f;
    const float by = 12.f;

    gfx::drawRect(vg, bx, by, bw, bh, nvgRGBA(25, 25, 30, 230), 6.f);
    gfx::drawRectOutline(vg, theme, 1.f, bx, by, bw, bh, 6.f);

    const auto text_col = theme->GetColour(ThemeEntryID_TEXT);
    const auto info_col = theme->GetColour(ThemeEntryID_TEXT_INFO);

    std::string origin_str;
    switch (m_origin) {
        case TransportOrigin::Mtp: origin_str = "MTP"; break;
        case TransportOrigin::Ftp: origin_str = "FTP"; break;
        case TransportOrigin::Web: origin_str = "Web"; break;
        default:                   origin_str = "DBI"; break;
    }

    const auto pkg_str = origin_str + " · " + std::to_string(std::min(m_current_package + 1, m_queue.size())) + "/" + std::to_string(m_queue.size());
    gfx::drawText(vg, bx + 10.f, by + 10.f, 13.f, text_col, pkg_str.c_str());

    bool has_deferred_plan = (m_plan_total_bytes <= 0);
    for (const auto& e : m_queue) {
        if (e.selected && (e.analysis_deferred || e.source_size <= 0)) {
            has_deferred_plan = true;
            break;
        }
    }

    const s64 overall_done = OverallDone();
    const double ratio = (!has_deferred_plan && m_plan_total_bytes > 0)
        ? std::clamp<double>((double)overall_done / (double)m_plan_total_bytes, 0.0, 1.0) : 0.0;
    char pct_buf[16]{};
    if (has_deferred_plan) {
        std::snprintf(pct_buf, sizeof(pct_buf), "--");
    } else {
        std::snprintf(pct_buf, sizeof(pct_buf), "%.0f%%", ratio * 100.0);
    }
    gfx::drawText(vg, bx + bw - 10.f, by + 10.f, 13.f, info_col, pct_buf, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);

    // Mini progress bar
    const Vec4 bar{bx + 10.f, by + 28.f, bw - 20.f, 4.f};
    gfx::drawRect(vg, bar, theme->GetColour(ThemeEntryID_PROGRESSBAR_BACKGROUND), 2.f);
    if (!has_deferred_plan && ratio > 0.0) {
        gfx::drawRect(vg, bar.x, bar.y, bar.w * static_cast<float>(ratio), bar.h, theme->GetColour(ThemeEntryID_HIGHLIGHT_1), 2.f);
    }
}

void InstallSession::Draw(NVGcontext* vg, Theme* theme) {
    if (m_minimized) {
        DrawMiniBadge(vg, theme);
        return;
    }

    if (m_screensaver.IsActive() && !App::OwnsFooter(this)) {
        m_screensaver.Stop();
    }

    if (m_screensaver.OwnsScreen()) {
        m_screensaver.Draw(vg, theme, ComputeSaverInfo());
        return;
    }

    MenuBase::Draw(vg, theme);
    const auto state = m_state.load();
    if (state == State::Summary || state == State::Cancelled || state == State::Failed) {
        DrawSummaryPanel(vg, theme, Vec4{70.f, GetY() + 8.f, 1140.f, 214.f});
        DrawBottomList(vg, theme);
        return;
    }

    DrawInstalling(vg, theme);
}

void InstallSession::DrawInstalling(NVGcontext* vg, Theme* theme) {
    const auto state = m_state.load();
    if (state == State::Installing) {
        // yellow bar = remaining bytes of the package being written, on its dest only.
        // Label displays cumulative written / total progress without projecting the queue.
        s64 sd_written{}, nand_written{};
        s64 sd_total{}, nand_total{};
        bool show = false;
        if (m_current_package < m_queue.size()) {
            const auto& entry = m_queue[m_current_package];
            if (entry.install_selected && !entry.installed
                && R_SUCCEEDED(entry.analysis_result) && (!entry.install_result.has_value() || R_SUCCEEDED(*entry.install_result))) {
                const auto size = PlanSize(entry);
                const auto written = std::clamp<s64>(m_total_write.load() - m_package_write_start, 0, size);
                if (size > 0) {
                    if (entry.install_sd) {
                        sd_written = written;
                        sd_total = size;
                    } else {
                        nand_written = written;
                        nand_total = size;
                    }
                    show = true;
                }
            }
        }
        if (show) {
            SetStorageInstallProgress(nand_written, nand_total, sd_written, sd_total);
        } else {
            ClearStorageHighlight();
        }
    } else {
        ClearStorageHighlight();
    }

    // once the queue has ended the live header, progress bar and graph have
    // nothing left to say, so that space goes to the session summary instead.
    if (state == State::Summary || state == State::Cancelled || state == State::Failed) {
        DrawSummaryPanel(vg, theme, Vec4{70.f, GetY() + 8.f, 1140.f, 214.f});
        DrawBottomList(vg, theme);
        return;
    }

    // Header speed and ETA use the average write rate over the whole graph
    // history window (~48 s, near a minute), not the instantaneous rate. The
    // moment-to-moment R/W speeds are shown per line on the graph below, so the
    // header stays a single stable "how fast is this going overall" number.
    const s64 avg_write_bps = AvgWriteBps();
    const double speed_mib = static_cast<double>(avg_write_bps) / (1024.0 * 1024.0);

    bool has_deferred_plan = (m_plan_total_bytes <= 0);
    for (const auto& e : m_queue) {
        if (e.selected && (e.analysis_deferred || e.source_size <= 0)) {
            has_deferred_plan = true;
            break;
        }
    }

    const s64 overall_done = OverallDone();
    const double overall_ratio = (!has_deferred_plan && m_plan_total_bytes > 0)
        ? std::clamp<double>((double)overall_done / (double)m_plan_total_bytes, 0.0, 1.0) : 0.0;

    const auto format_eta = [&](s64 bytes_left) -> std::string {
        // under four samples the rate is still settling and the figure jumps
        // around by minutes between frames.
        return m_history_count < 4 ? std::string{} : FormatEta(bytes_left, avg_write_bps);
    };
    const auto file_eta = (m_progress_size > 0 && m_progress_size >= m_progress_offset)
        ? format_eta(m_progress_size - m_progress_offset) : std::string{};
    const auto total_eta = (!has_deferred_plan && m_plan_total_bytes > overall_done)
        ? format_eta(m_plan_total_bytes - overall_done) : std::string{};

    char avg_buf[32]{};
    std::snprintf(avg_buf, sizeof(avg_buf), "%.2f MiB/s", speed_mib);
    char overall_buf[16]{};
    if (has_deferred_plan) {
        std::snprintf(overall_buf, sizeof(overall_buf), "--");
    } else {
        std::snprintf(overall_buf, sizeof(overall_buf), "%.0f%%", overall_ratio * 100.0);
    }
    std::vector<StatItem> header{
        {"Package"_i18n, std::to_string(std::min(m_current_package + 1, m_queue.size())) + "/" + std::to_string(m_queue.size())},
        {"Overall"_i18n, overall_buf, theme->GetColour(ThemeEntryID_TEXT_SELECTED)},
        {"Installed"_i18n, std::to_string(m_stats.installed)},
        {"Failed"_i18n, std::to_string(m_stats.failed), m_stats.failed ? std::optional{theme->GetColour(ThemeEntryID_ERROR)} : std::nullopt},
        // the R/W readout beside the graph is the momentary rate and is where the
        // eye lands; this is the whole-window average, accented so it is not lost.
        {"Average speed"_i18n, avg_buf, theme->GetColour(ThemeEntryID_TEXT_SELECTED)},
    };
    // "this file / whole queue", so the second number answers "when am I done".
    if (!file_eta.empty() || !total_eta.empty()) {
        header.push_back({"Remaining"_i18n,
            (file_eta.empty() ? "--" : file_eta) + " / " + (total_eta.empty() ? "--" : total_eta)});
    }
    DrawStatRow(vg, theme->GetColour(ThemeEntryID_TEXT_INFO), 70.f, GetY() + 10.f, 18.f, header);
    const auto display_title = !m_current_title.empty()
        ? m_current_title
        : (m_current_package < m_queue.size() ? m_queue[m_current_package].file_name : "");
    if (!display_title.empty()) {
        std::string title = display_title;
        if (!m_current_transfer.empty() &&
            !path::EndsWithIC(m_current_transfer, ".nca") &&
            !path::EndsWithIC(m_current_transfer, ".ncz") &&
            m_current_transfer.find(".nca") == std::string::npos &&
            m_current_transfer.find(".ncz") == std::string::npos) {
            title += " — " + m_current_transfer;
        }
        nvgSave(vg);
        nvgIntersectScissor(vg, 70.f, GetY() + 38.f, 1140.f, 25.f);
        gfx::drawTextArgs(vg, 70.f, GetY() + 38.f, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
            theme->GetColour(ThemeEntryID_TEXT), "%s", title.c_str());
        nvgRestore(vg);
    }
    // two bars: the current transfer on top, the whole queue underneath.
    if (m_progress_size > 0) {
        const Vec4 bar{70.f, GetY() + 65.f, 1140.f, 10.f};
        gfx::drawRect(vg, bar, theme->GetColour(ThemeEntryID_PROGRESSBAR_BACKGROUND), 3.f);
        gfx::drawRect(vg, bar.x, bar.y, bar.w * std::clamp<double>((double)m_progress_offset / m_progress_size, 0.0, 1.0), bar.h,
            theme->GetColour(ThemeEntryID_PROGRESSBAR), 3.f);
    }
    if (!has_deferred_plan && m_plan_total_bytes > 0) {
        const Vec4 bar{70.f, GetY() + 78.f, 1140.f, 10.f};
        gfx::drawRect(vg, bar, theme->GetColour(ThemeEntryID_PROGRESSBAR_BACKGROUND), 3.f);
        gfx::drawRect(vg, bar.x, bar.y, bar.w * static_cast<float>(overall_ratio), bar.h,
            theme->GetColour(ThemeEntryID_HIGHLIGHT_1), 3.f);
    }

    // R/W speed graph: red = source read, blue = storage write.
    {

        const auto red = nvgRGBA(231, 76, 60, 255);
        const auto blue = nvgRGBA(52, 152, 219, 255);
        const Vec4 plot{110.f, GetY() + 95.f, 930.f, 125.f};
        const float pad = 4.f;

        gfx::drawRect(vg, plot, theme->GetColour(ThemeEntryID_PROGRESSBAR_BACKGROUND), 3.f);

        // labels to the left of the plot.
        gfx::drawTextArgs(vg, plot.x - 14.f, plot.y + plot.h * 0.30f, 20.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, red, "R");
        gfx::drawTextArgs(vg, plot.x - 14.f, plot.y + plot.h * 0.70f, 20.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, blue, "W");

        // readout is averaged over the last few samples so a single idle window
        // (read waiting on decompress/write) does not make it flicker to 0.
        const auto avg_mib = [&](const std::array<s64, SPEED_HISTORY>& history) -> double {
            const size_t n = std::min<size_t>(m_history_count, 4);
            if (!n) return 0.0;
            s64 sum = 0;
            for (size_t i = 0; i < n; i++) {
                const auto idx = (m_history_index + SPEED_HISTORY - 1 - i) % SPEED_HISTORY;
                sum += history[idx];
            }
            return (double)sum / (double)n / (1024.0 * 1024.0);
        };
        // captioned "now" so it reads as the momentary rate, distinct from the
        // averaged figure in the header line above.
        gfx::drawTextArgs(vg, plot.x + plot.w + 14.f, plot.y + 2.f, 13.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
            theme->GetColour(ThemeEntryID_TEXT_INFO), "%s", "Now"_i18n.c_str());
        const auto draw_readout = [&](float ry, NVGcolor colour, double mib) {
            char buf[32]{};
            std::snprintf(buf, sizeof(buf), "%.1f MiB/s", mib);
            gfx::drawTextBold(vg, plot.x + plot.w + 14.f, ry, 18.f, colour, buf, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        };
        draw_readout(plot.y + plot.h * 0.30f, red, avg_mib(m_read_history));
        draw_readout(plot.y + plot.h * 0.70f, blue, avg_mib(m_write_history));

        if (m_history_count >= 2) {
            s64 peak = 1;
            for (size_t i = 0; i < m_history_count; i++) {
                const auto idx = (m_history_index + SPEED_HISTORY - m_history_count + i) % SPEED_HISTORY;
                peak = std::max({peak, m_read_history[idx], m_write_history[idx]});
            }
            const double peak_mib = (double)peak / (1024.0 * 1024.0);

            // round the gridline step to a "nice" 1/2/5 x 10^n MiB/s, then pick
            // the top of scale as a whole number of steps that clears the peak.
            // A 1 MiB/s floor keeps slow transfers from filling the whole plot.
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
            const double top_mib = step_mib * steps;
            const double top = top_mib * 1024.0 * 1024.0;

            // clip lines and grid to the plot so nothing bleeds past its edges.
            nvgSave(vg);
            nvgIntersectScissor(vg, plot.x, plot.y, plot.w, plot.h);

            auto grid_col = theme->GetColour(ThemeEntryID_TEXT);
            grid_col.a = 0.12f;
            const auto label_col = theme->GetColour(ThemeEntryID_TEXT_INFO);
            const float inner_h = plot.h - pad * 2.f;
            for (int k = 0; k <= steps; k++) {
                const float gy = plot.y + plot.h - pad - inner_h * (float)k / (float)steps;
                nvgBeginPath(vg);
                nvgMoveTo(vg, plot.x + pad, gy);
                nvgLineTo(vg, plot.x + plot.w - pad, gy);
                nvgStrokeColor(vg, grid_col);
                nvgStrokeWidth(vg, 1.f);
                nvgStroke(vg);
                if (k > 0) {
                    const double val = step_mib * k;
                    if (k == steps) {
                        gfx::drawTextArgs(vg, plot.x + pad + 4.f, gy + 2.f, 12.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, label_col, "%g MiB/s", val);
                    } else {
                        gfx::drawTextArgs(vg, plot.x + pad + 4.f, gy + 2.f, 12.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, label_col, "%g", val);
                    }
                }
            }

            const auto draw_line = [&](const std::array<s64, SPEED_HISTORY>& history, NVGcolor colour) {
                nvgBeginPath(vg);
                for (size_t i = 0; i < m_history_count; i++) {
                    const auto idx = (m_history_index + SPEED_HISTORY - m_history_count + i) % SPEED_HISTORY;
                    // newest sample is pinned to the right edge.
                    const auto slot = SPEED_HISTORY - m_history_count + i;
                    const float x = plot.x + pad + (plot.w - pad * 2.f) * slot / (SPEED_HISTORY - 1);
                    const double frac = std::clamp((double)history[idx] / top, 0.0, 1.0);
                    const float y = plot.y + plot.h - pad - inner_h * (float)frac;
                    if (i == 0) nvgMoveTo(vg, x, y);
                    else nvgLineTo(vg, x, y);
                }
                nvgStrokeColor(vg, colour);
                nvgStrokeWidth(vg, 2.f);
                nvgStroke(vg);
            };
            // additive blend so where the red (R) and blue (W) lines overlap
            // they sum into a bright mixed colour, making crossings obvious
            // instead of one line simply hiding the other. Restored with the
            // enclosing nvgRestore (composite op is part of the saved state).
            nvgGlobalCompositeOperation(vg, NVG_LIGHTER);
            draw_line(m_read_history, red);
            draw_line(m_write_history, blue);

            nvgRestore(vg);
        }
    }

    DrawBottomList(vg, theme);
}

void InstallSession::DrawBottomList(NVGcontext* vg, Theme* theme) {
    // caller holds m_mutex.
    if (m_show_errors) {
        const auto error_col = theme->GetColour(ThemeEntryID_ERROR);
        const auto info_col = theme->GetColour(ThemeEntryID_TEXT_INFO);
        m_error_list->Draw(vg, theme, m_errors.size(), [this, error_col, info_col](NVGcontext* vg, Theme* theme, Vec4 v, s64 index) {
            const auto& error = m_errors[index];
            if (index == m_error_index) {
                gfx::drawRectOutline(vg, theme, 2.f, v);
            }
            gfx::drawTextArgs(vg, v.x + 10.f, v.y + 5.f, 16.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, error_col,
                "%ld. %s — %s", index + 1, error.stage.c_str(), error.name.c_str());

            // second line: the raw code, its symbolic name, and the plain
            // language explanation when sphaira has one for it.
            auto text = ResultText(error.rc);
            if (!error.code_name.empty()) {
                text += "  " + error.code_name;
            }
            if (!error.detail.empty()) {
                text += "  —  " + error.detail;
            }
            gfx::drawTextArgs(vg, v.x + 26.f, v.y + 28.f, 14.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, info_col, "%s", text.c_str());
        });
        return;
    }

    m_log_list->Draw(vg, theme, m_log.size(), [this](NVGcontext* vg, Theme* theme, Vec4 v, s64 index) {
        const auto& entry = m_log[index];
        NVGcolor colour;
        switch (entry.kind) {
            case LogKind::Success: colour = nvgRGB(80, 200, 120); break;
            case LogKind::Warning: colour = nvgRGB(230, 170, 50); break;
            case LogKind::Error:   colour = theme->GetColour(ThemeEntryID_ERROR); break;
            default:               colour = theme->GetColour(ThemeEntryID_TEXT); break;
        }
        gfx::drawTextArgs(vg, v.x + 4.f, v.y + 5.f, 15.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, colour, "%s", entry.text.c_str());
        // no bold font face is loaded, so fake bold for events by over-drawing
        // with a sub-pixel x offset to thicken the strokes.
        if (entry.kind == LogKind::Event) {
            gfx::drawTextArgs(vg, v.x + 4.7f, v.y + 5.f, 15.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, colour, "%s", entry.text.c_str());
        }
    });
}

void InstallSession::DrawSummaryPanel(NVGcontext* vg, Theme* theme, const Vec4& area) {
    // caller holds m_mutex.
    gfx::drawRect(vg, area, theme->GetColour(ThemeEntryID_PROGRESSBAR_BACKGROUND), 3.f);

    const auto info_col = theme->GetColour(ThemeEntryID_TEXT_INFO);
    const auto text_col = theme->GetColour(ThemeEntryID_TEXT);
    const auto error_col = theme->GetColour(ThemeEntryID_ERROR);
    const auto good_col = nvgRGB(80, 200, 120);
    const auto warn_col = nvgRGB(230, 170, 50);

    const float x = area.x + 20.f;
    float y = area.y + 10.f;

    // headline: what the run ended as.
    const auto outcome = m_state == State::Cancelled ? "Session cancelled"_i18n
        : m_session_failed ? "Session failed"_i18n : "Queue finished"_i18n;
    const auto outcome_col = m_state == State::Cancelled ? warn_col
        : m_session_failed ? error_col : good_col;
    gfx::drawTextArgs(vg, x, y, 20.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, text_col, "%s", "Session summary"_i18n.c_str());
    gfx::drawTextArgs(vg, x + 0.7f, y, 20.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, text_col, "%s", "Session summary"_i18n.c_str());
    gfx::drawTextArgs(vg, area.x + area.w - 20.f, y, 20.f, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, outcome_col, "%s", outcome.c_str());
    y += 32.f;

    const auto seconds = m_stats.elapsed_ns / 1000000000ULL;
    const double avg_mib = seconds
        ? (double)m_stats.write_bytes / (double)seconds / (1024.0 * 1024.0) : 0.0;
    const double peak_mib = (double)m_peak_write_bps.load() / (1024.0 * 1024.0);
    const auto fmt_speed = [](double mib) {
        char buf[32]{};
        std::snprintf(buf, sizeof(buf), "%.2f MiB/s", mib);
        return std::string{buf};
    };

    DrawStatRow(vg, info_col, x, y, 17.f, {
        {"Installed"_i18n, std::to_string(m_stats.installed), m_stats.installed ? std::optional{good_col} : std::nullopt},
        {"Skipped"_i18n, std::to_string(m_stats.skipped)},
        {"Failed"_i18n, std::to_string(m_stats.failed), m_stats.failed ? std::optional{error_col} : std::nullopt},
        {"Packages"_i18n, std::to_string(m_queue.size())},
    });
    y += 28.f;

    DrawStatRow(vg, info_col, x, y, 17.f, {
        {"Duration"_i18n, FormatDuration(m_stats.elapsed_ns)},
        {"Average speed"_i18n, fmt_speed(avg_mib)},
        {"Peak speed"_i18n, fmt_speed(peak_mib)},
    });
    y += 28.f;

    // read is what came off the source (compressed, over usb); written is what
    // actually landed in storage after decompression, so the two differ for nsz.
    DrawStatRow(vg, info_col, x, y, 17.f, {
        {"Received"_i18n, utils::formatSizeStorage(std::max<s64>(0, m_stats.read_bytes))},
        {"Written"_i18n, utils::formatSizeStorage(std::max<s64>(0, m_stats.write_bytes))},
    });
    y += 28.f;

    DrawStatRow(vg, info_col, x, y, 17.f, {
        {"microSD"_i18n, utils::formatSizeStorage(std::max<s64>(0, m_stats.sd_bytes))},
        {"System memory"_i18n, utils::formatSizeStorage(std::max<s64>(0, m_stats.nand_bytes))},
    });
    y += 30.f;

    if (!m_errors.empty()) {
        gfx::drawTextArgs(vg, x, y, 15.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, error_col, "%s — %s",
            (std::to_string(m_errors.size()) + " " + "error(s) recorded"_i18n).c_str(),
            "press Y to review them, they are also saved to errors.txt"_i18n.c_str());
    } else {
        gfx::drawTextArgs(vg, x, y, 15.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, good_col, "%s",
            "No errors recorded."_i18n.c_str());
    }

    if (!m_compat_warnings.empty()) {
        y += 24.f;
        gfx::drawTextArgs(vg, x, y, 15.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, warn_col, "%s (%zu)",
            "Compatibility warning: firmware update required to launch"_i18n.c_str(),
            m_compat_warnings.size());
    }
}
} // namespace sphaira::ui::menu::dbi

#endif