#if ENABLE_NETWORK_INSTALL

#include "ui/menus/dbi/dbi_internal.hpp"
#include "ui/menus/dbi/usb_status_text.hpp"
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
            const std::string usb_status_text = i18n::get(UsbStatusTextKey(
                state == State::WaitingForUsb && usb_state == UsbState_Detached, is_super_speed, is_usb3_forced));

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
            const float warn_y = WaitingWarningY(text_bounds[3]);
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
                if (!TakesNoSpace(entry)) {
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
        });
        DrawStatRow(vg, info_col, 70.f, GetY() + 36.f, 15.f, {
            {"microSD free"_i18n, utils::formatSizeStorage(std::max<s64>(0, spaces.sd_free - reserve_sd))},
            {"NAND free"_i18n, utils::formatSizeStorage(std::max<s64>(0, spaces.nand_free - reserve_nand))},
            {"Reserve"_i18n, utils::formatSizeStorage(reserve_sd) + " / " + utils::formatSizeStorage(reserve_nand)},
        });

        m_list->Draw(vg, theme, m_queue.size(), m_index, [this](NVGcontext* vg, Theme* theme, Vec4 v, s64 index) {
            const auto& entry = m_queue[index];
            if (index == m_index) gfx::drawRectOutline(vg, theme, 4.f, v);
            const auto colour = R_FAILED(entry.analysis_result) ? theme->GetColour(ThemeEntryID_ERROR) : theme->GetColour(ThemeEntryID_TEXT);
            if (R_SUCCEEDED(entry.analysis_result)) DrawCheckbox(vg, theme, v, entry.selected);
            gfx::drawTextArgs(vg, v.x + (R_SUCCEEDED(entry.analysis_result) ? 42.f : 12.f), v.y + 8.f, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, colour,
                R_FAILED(entry.analysis_result) ? "! %s" : "%s", entry.file_name.c_str());
            if (R_FAILED(entry.analysis_result)) {
                gfx::drawTextArgs(vg, v.x + 42.f, v.y + 40.f, 14.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, colour,
                    "%s: %s", "Analysis failed"_i18n.c_str(), ResultText(entry.analysis_result).c_str());
            } else {
                const auto info_col = theme->GetColour(ThemeEntryID_TEXT_INFO);
                std::string prefix;
                if (entry.analysis_deferred) {
                    const auto source_size = entry.analysis.source_size > 0
                        ? utils::formatSizeStorage(entry.analysis.source_size) : "Unknown"_i18n;
                    prefix = "Package size"_i18n + ": " + source_size + "    " +
                             "Install size"_i18n + ": " + "Calculated during install"_i18n + "    " +
                             "Target"_i18n + ": ";
                } else {
                    const auto kind = entry.analysis.size_kind == yati::AnalysisSizeKind::Exact ? "Exact"_i18n : "Estimate"_i18n;
                    prefix = "Package size"_i18n + ": " + utils::formatSizeStorage(entry.analysis.source_size) + "    " +
                             "Install size"_i18n + ": " + utils::formatSizeStorage(entry.analysis.install_size) +
                             " (" + kind + ")    " +
                             "Target"_i18n + ": ";
                }
                gfx::drawText(vg, v.x + 42.f, v.y + 40.f, 14.f, info_col, prefix.c_str());
                nvgFontSize(vg, 14.f);
                nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
                float x = v.x + 42.f + nvgTextBounds(vg, 0.f, 0.f, prefix.c_str(), nullptr, nullptr);

                const bool is_sd = (entry.target == InstallTarget::Auto) ? entry.planned_sd : (entry.target == InstallTarget::Sd);
                const auto dest_col = theme->GetColour(is_sd ? ThemeEntryID_HIGHLIGHT_1 : ThemeEntryID_HIGHLIGHT_2);
                const auto dest_str = is_sd ? "microSD"_i18n : "System memory"_i18n;
                if (entry.target == InstallTarget::Auto) {
                    const auto auto_prefix = "Auto"_i18n + " → ";
                    gfx::drawText(vg, x, v.y + 40.f, 14.f, info_col, auto_prefix.c_str());
                    x += nvgTextBounds(vg, 0.f, 0.f, auto_prefix.c_str(), nullptr, nullptr);
                }
                gfx::drawText(vg, x, v.y + 40.f, 14.f, dest_col, dest_str.c_str());
            }
        });
        return;
    }
}

void InstallSession::DrawMiniBadge(NVGcontext* vg, Theme* theme) {
    SCOPED_MUTEX(&m_mutex);
    const float bw = 320.f;
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
        case TransportOrigin::Usb: origin_str = "USB"; break;
        default:                   origin_str = "DBI"; break;
    }

    std::string pkg_str;
    const auto state = m_state.load();
    // MTP/FTP sessions stay in Installing after the last package, waiting for more files.
    const bool finished = state == State::Summary || (state == State::Installing && AllQueueEntriesTerminal(m_queue));
    if (state == State::WaitingForUsb || state == State::WaitingForList) {
        pkg_str = origin_str + " · " + "Waiting for PC"_i18n;
    } else if (state == State::Analysing) {
        pkg_str = origin_str + " · " + "Analysing"_i18n;
    } else if (state == State::ReviewQueue) {
        pkg_str = origin_str + " · " + "Ready to install"_i18n;
    } else if (finished) {
        pkg_str = origin_str + " · " + "Finished"_i18n;
    } else if (state == State::Cancelled) {
        pkg_str = origin_str + " · " + "Cancelled"_i18n;
    } else if (m_queue.empty()) {
        pkg_str = origin_str;
    } else {
        pkg_str = origin_str + " · " + std::to_string(std::min(m_current_package + 1, m_queue.size())) + "/" + std::to_string(m_queue.size());
    }
    gfx::drawText(vg, bx + 10.f, by + 10.f, 13.f, text_col, pkg_str.c_str());

    bool has_deferred_plan = (m_plan_total_bytes <= 0);
    for (const auto& e : m_queue) {
        if (e.selected && (e.analysis_deferred || e.source_size <= 0)) {
            has_deferred_plan = true;
            break;
        }
    }

    const s64 overall_done = OverallDone();
    const bool has_known_overall = !has_deferred_plan && m_plan_total_bytes > 0;
    const bool has_known_package = (m_progress_size > 0);

    const double ratio = has_known_overall
        ? std::clamp<double>((double)overall_done / (double)m_plan_total_bytes, 0.0, 1.0)
        : (has_known_package ? std::clamp<double>((double)m_progress_offset / (double)m_progress_size, 0.0, 1.0) : 0.0);

    char right_buf[64]{};
    if (state == State::Installing && !finished) {
        if (has_known_overall || has_known_package) {
            std::snprintf(right_buf, sizeof(right_buf), "%.0f%%   %s", ratio * 100.0, "Expand"_i18n.c_str());
        } else {
            std::snprintf(right_buf, sizeof(right_buf), "--   %s", "Expand"_i18n.c_str());
        }
    } else {
        std::snprintf(right_buf, sizeof(right_buf), " %s", "Expand"_i18n.c_str());
    }
    gfx::drawText(vg, bx + bw - 10.f, by + 10.f, 13.f, info_col, right_buf, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);

    // Mini progress bar
    const Vec4 bar{bx + 10.f, by + 28.f, bw - 20.f, 4.f};
    gfx::drawRect(vg, bar, theme->GetColour(ThemeEntryID_PROGRESSBAR_BACKGROUND), 2.f);
    if (finished) {
        // everything is done: a full bar, whatever the byte counters were reset to.
        gfx::drawRect(vg, bar, theme->GetColour(ThemeEntryID_HIGHLIGHT_1), 2.f);
    } else if ((has_known_overall || has_known_package) && ratio > 0.0) {
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
    SCOPED_MUTEX(&m_mutex);
    const auto state = m_state.load();
    if (state == State::Summary || state == State::Cancelled || state == State::Failed) {
        DrawSummaryPanel(vg, theme, Vec4{70.f, GetY() + 8.f, 1140.f, 214.f});
        DrawBottomList(vg, theme);
        return;
    }

    DrawInstalling(vg, theme);
}

} // namespace sphaira::ui::menu::dbi

#endif
