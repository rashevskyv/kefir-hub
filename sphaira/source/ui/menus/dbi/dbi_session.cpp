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
void Menu::CancelSession() {
    m_cancel_requested = true;
    ueventSignal(&m_cancel_event);
    if (m_local_fs && !m_local_fs->IsNative()) {
        devoptab::common::CancelActiveCurlTransfers();
    }
    const auto state = m_state.load();
    if (state == State::WaitingForUsb || state == State::WaitingForList || state == State::Analysing || state == State::Installing) {
        if (m_usb_source) m_usb_source->SignalCancel();
    }
    SetPop();
}

void Menu::SkipCurrentPackage(size_t expected_package) {
    {
        SCOPED_MUTEX(&m_mutex);
        if (m_state.load() != State::Installing || m_current_package != expected_package) {
            return;
        }
        m_skip_requested = true;
    }
    ueventSignal(&m_cancel_event);
    if (m_local_fs && !m_local_fs->IsNative()) {
        devoptab::common::CancelActiveCurlTransfers();
    }
    const auto state = m_state.load();
    if (state == State::Installing) {
        if (m_usb_source) m_usb_source->SignalCancel();
    }
}

auto Menu::TargetName(InstallTarget target) -> std::string {
    if (target == InstallTarget::Sd) return "microSD"_i18n;
    if (target == InstallTarget::Nand) return "System memory"_i18n;
    return "Auto"_i18n;
}

void Menu::ToggleErrorView() {
    {
        SCOPED_MUTEX(&m_mutex);
        if (m_errors.empty()) {
            return;
        }
        m_show_errors = !m_show_errors;
        m_error_index = 0;
        m_error_list->SetYoff(0.f);
        // the log view follows its tail; make it re-snap when it comes back.
        m_log_last_seen_size = 0;
    }
    m_actions_dirty = true;
}

void Menu::BeginSessionStats() {
    SCOPED_MUTEX(&m_mutex);
    m_stats = {};
    m_errors.clear();
    m_error_index = 0;
    m_show_errors = false;
    m_peak_write_bps = 0;
    m_compat_warnings.clear();
    m_pending_warning_popups.clear();
    m_session_timestamp.Update();
}

void Menu::RecordPackageResult(size_t index, Result rc, bool cancelled, bool user_skipped, bool to_sd, s64 read_delta, s64 write_delta) {
    SCOPED_MUTEX(&m_mutex);
    m_queue[index].install_result = rc;
    m_queue[index].installed = !user_skipped && R_SUCCEEDED(rc);
    m_stats.read_bytes += std::max<s64>(0, read_delta);
    m_stats.write_bytes += std::max<s64>(0, write_delta);
    // the package is off the queue either way; book its whole planned size so
    // the overall bar advances even when it was skipped or failed.
    AddSizeSaturated(m_plan_done_bytes, PlanSize(m_queue[index]));
    m_package_write_start = m_total_write.load();

    if (user_skipped) {
        m_stats.skipped++;
    } else if (R_SUCCEEDED(rc)) {
        m_queue[index].selected = false; // uncheck a package that went through
        if (m_current_file_skipped) {
            m_stats.skipped++;
        } else {
            m_stats.installed++;
        }
        // where the payload actually landed, for the summary breakdown.
        (to_sd ? m_stats.sd_bytes : m_stats.nand_bytes) += std::max<s64>(0, write_delta);
    } else if (!cancelled) {
        m_stats.failed++;
    }
}

void Menu::AddError(const std::string& name, const std::string& stage, Result rc) {
    SessionError error{};
    error.name = name;
    error.stage = stage;
    error.rc = rc;
    if (const auto code_name = GetResultCodeName(rc)) {
        error.code_name = code_name;
    }
    error.detail = GetResultDescription(rc);

    // written unconditionally: the user may have file logging switched off, and
    // a failed queue is exactly when the trace is needed afterwards.
    log_write_error("install queue: %s failed for \"%s\" -- %s%s%s",
        stage.c_str(), name.c_str(), ResultText(rc).c_str(),
        error.code_name.empty() ? "" : " ", error.code_name.c_str());

    SCOPED_MUTEX(&m_mutex);
    m_errors.emplace_back(std::move(error));
}

auto Menu::FormatDuration(u64 ns) -> std::string {
    const auto total = ns / 1000000000ULL;
    char buf[32]{};
    if (total >= 3600) {
        std::snprintf(buf, sizeof(buf), "%lluh %llum %llus", total / 3600, total % 3600 / 60, total % 60);
    } else if (total >= 60) {
        std::snprintf(buf, sizeof(buf), "%llum %llus", total / 60, total % 60);
    } else {
        std::snprintf(buf, sizeof(buf), "%llus", total);
    }
    return buf;
}

void Menu::AddLog(const std::string& text, LogKind kind) {
    SCOPED_MUTEX(&m_mutex);
    const bool follow_tail = m_log.empty() || m_log_index >= static_cast<s64>(m_log.size()) - 1;
    if (m_log.size() == MAX_LOG_LINES) {
        m_log.erase(m_log.begin());
        if (!follow_tail && m_log_index > 0) m_log_index--;
    }
    m_log.emplace_back(LogEntry{text, kind});
    if (follow_tail) m_log_index = m_log.size() - 1;
}

void Menu::OnInstallSkipped() {
    // yati reached a title that is already installed and skipped it. Flag the
    // current queue item so it is logged as skipped rather than installed.
    m_current_file_skipped = true;
}

Result Menu::CheckCancelled() {
    R_UNLESS(!m_cancel_requested && !m_skip_requested && !GetToken().stop_requested(), Result_TransferCancelled);
    R_SUCCEED();
}

void Menu::SetInstallTitle(const std::string& title) {
    SCOPED_MUTEX(&m_mutex);
    m_current_title = title;
}

void Menu::SetInstallTransfer(const std::string& transfer) {
    SCOPED_MUTEX(&m_mutex);
    m_current_transfer = transfer;
    m_progress_offset = 0;
    m_progress_size = 0;
    m_progress_last_offset = 0;
    m_progress_speed = 0;
    m_progress_speed_samples.fill(0);
    m_progress_speed_sample_count = 0;
    m_progress_speed_sample_index = 0;
    m_progress_timestamp.Update();
}

void Menu::UpdateInstallTransfer(s64 offset, s64 size) {
    SCOPED_MUTEX(&m_mutex);
    m_progress_offset = offset;
    m_progress_size = size;
}

void Menu::UpdateInstallReadWrite(s64 read_offset, s64 write_offset) {
    // offsets reset for every nca; fold them into monotonic totals.
    auto delta_read = read_offset - m_last_file_read;
    if (delta_read < 0) {
        delta_read = read_offset;
    }
    auto delta_write = write_offset - m_last_file_write;
    if (delta_write < 0) {
        delta_write = write_offset;
    }
    m_last_file_read = read_offset;
    m_last_file_write = write_offset;
    m_total_read += delta_read;
    m_total_write += delta_write;
}

void Menu::InstallYield() {
    svcSleepThread(1e+6);
}

bool Menu::PromptReinstall(const std::string& title_name) {
    {
        SCOPED_MUTEX(&m_mutex);
        if (m_current_file_reinstall_choice.has_value()) {
            return *m_current_file_reinstall_choice;
        }
    }

    std::string display_name = m_current_title;
    if (display_name.empty()) {
        display_name = title_name;
    }

    auto data = std::make_shared<PromptData>();
    data->title = display_name;

    {
        SCOPED_MUTEX(&m_mutex);
        m_prompt_data = data;
    }

    while (data->choice == -1 && !m_cancel_requested && !m_skip_requested && !GetToken().stop_requested()) {
        svcSleepThread(10'000'000ULL); // 10ms
    }

    if (m_cancel_requested || m_skip_requested || GetToken().stop_requested()) {
        return false;
    }

    bool result = data->choice == 1;
    {
        SCOPED_MUTEX(&m_mutex);
        m_current_file_reinstall_choice = result;
        m_prompt_data = nullptr;
    }
    return result;
}

void Menu::OnCompatibilityWarning(const CompatibilityWarning& warning) {
    SCOPED_MUTEX(&m_mutex);
    auto w = warning;
    if (w.title_name.empty() && !m_current_title.empty()) {
        w.title_name = m_current_title;
    }
    for (auto& existing : m_compat_warnings) {
        if (existing.title_id == w.title_id) {
            if (version::IsLower(existing.required_hos, w.required_hos)) {
                existing = w;
                for (auto& pending : m_pending_warning_popups) {
                    if (pending.title_id == w.title_id) {
                        pending = w;
                    }
                }
            }
            return;
        }
    }
    m_compat_warnings.push_back(w);
    m_pending_warning_popups.push_back(w);
}
} // namespace sphaira::ui::menu::dbi

#endif