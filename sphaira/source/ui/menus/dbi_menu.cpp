#if ENABLE_NETWORK_INSTALL

#include "ui/menus/dbi/dbi_internal.hpp"
#include "ui/menus/install_stream_menu_base.hpp"
#include "ui/menus/install_plan.hpp"
#include "haze_helper.hpp"
#include "ftpsrv_helper.hpp"
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
#include "web.hpp"

#include "title_info.hpp"
#include "version_compare.hpp"
#include "ui/menus/settings/settings_fs_utils.hpp"

#include <algorithm>
#include <cstdio>
#include <optional>
#include <ranges>

namespace sphaira::ui::menu::dbi {
InstallSession::InstallSession(const std::string& title, u32 flags, TransportOrigin origin)
    : MenuBase{title, flags}, m_origin{origin} {
    mutexInit(&m_mutex);
    ueventCreate(&m_cancel_event, false);

    const Vec4 log_pos{70.f, GetY() + 235.f, 1140.f, 310.f};
    const Vec4 log_row{log_pos.x, log_pos.y, log_pos.w, 30.f};
    m_log_list = std::make_unique<List>(1, 10, log_pos, log_row);
    m_log_list->SetLayout(List::Layout::GRID);
    m_error_list = std::make_unique<List>(1, 5, log_pos, Vec4{log_pos.x, log_pos.y, log_pos.w, 55.f});
    m_error_list->SetLayout(List::Layout::GRID);
    m_graph_timestamp.Update();
    m_inactivity_timestamp.Update();
    UpdateActions();
}

InstallSession::~InstallSession() {
    m_cancel_requested = true;
    m_stop_source.request_stop();
    ueventSignal(&m_cancel_event);
    if (m_stream) {
        m_stream->Disable();
    }
    m_screensaver.FlushPendingBrightness();
}

Menu::Menu(u32 flags) : InstallSession{"PC Install (USB)"_i18n, flags, TransportOrigin::Usb} {
    m_state = State::WaitingForUsb;

    m_session_skip_if_already_installed = App::GetApp()->m_skip_if_already_installed.Get();
    m_session_install_location = App::GetInstallLocation();
    m_session_reserve_mb = App::GetInstallReserveMb();
    m_session_reserve_sd_mb = App::GetInstallReserveSdMb();

    const Vec4 queue_pos{70.f, GetY() + 63.f, 1140.f, 470.f};
    const Vec4 row{queue_pos.x, queue_pos.y, queue_pos.w, 78.f};
    m_list = std::make_unique<List>(1, 6, queue_pos, row);
    m_list->SetLayout(List::Layout::GRID);
    UpdateActions();

#if DOCS_DEMO
    // docs screenshots: Eden has no USB device stack, so the screen stays on "Waiting for PC".
    return;
#endif

    m_was_mtp_enabled = App::GetMtpEnable();
    if (m_was_mtp_enabled) {
        App::Notify("Disable MTP for usb install"_i18n);
        App::SetMtpEnable(false);
    }

    if (App::GetHddEnable()) {
        usbHsFsExit();
    }

    m_usb_source = std::make_unique<yati::source::Usb>(TRANSFER_TIMEOUT);
    if (R_FAILED(m_usb_source->GetOpenResult())) {
        m_state = State::Failed;
        m_actions_dirty = true;
        return;
    }

    const auto create_rc = threadCreate(&m_thread, thread_func, this, nullptr, 1024 * 128, PRIO_PREEMPTIVE, 1);
    if (R_SUCCEEDED(create_rc)) {
        const auto start_rc = threadStart(&m_thread);
        if (R_SUCCEEDED(start_rc)) {
            m_thread_created = true;
        } else {
            threadClose(&m_thread);
        }
    }
    if (!m_thread_created) {
        m_state = State::Failed;
        m_actions_dirty = true;
    }
}

Menu::Menu(u32 flags, fs::Fs* fs, std::vector<fs::FsPath> paths, std::vector<s64> source_sizes, bool defer_analysis)
    : InstallSession{"Install queue"_i18n, flags, TransportOrigin::Dbi}, m_local_fs{fs}, m_local_paths{std::move(paths)},
      m_local_source_sizes{std::move(source_sizes)}, m_defer_local_analysis{defer_analysis} {
    m_state = State::Analysing;

    m_session_skip_if_already_installed = App::GetApp()->m_skip_if_already_installed.Get();
    m_session_install_location = App::GetInstallLocation();
    m_session_reserve_mb = App::GetInstallReserveMb();
    m_session_reserve_sd_mb = App::GetInstallReserveSdMb();

    const Vec4 queue_pos{70.f, GetY() + 63.f, 1140.f, 470.f};
    m_list = std::make_unique<List>(1, 6, queue_pos, Vec4{queue_pos.x, queue_pos.y, queue_pos.w, 78.f});
    m_list->SetLayout(List::Layout::GRID);
    UpdateActions();

    const auto create_rc = threadCreate(&m_thread, thread_func, this, nullptr, 1024 * 128, PRIO_PREEMPTIVE, 1);
    if (R_SUCCEEDED(create_rc)) {
        const auto start_rc = threadStart(&m_thread);
        if (R_SUCCEEDED(start_rc)) {
            m_thread_created = true;
        } else {
            threadClose(&m_thread);
        }
    }
    if (!m_thread_created) {
        m_state = State::Failed;
        m_actions_dirty = true;
    }
}

Menu::~Menu() {
    m_cancel_requested = true;
    m_stop_source.request_stop();
    ueventSignal(&m_cancel_event);
    if (m_local_fs && !m_local_fs->IsNative()) {
        devoptab::common::CancelActiveCurlTransfers();
    }
    if (m_usb_source) {
        m_usb_source->SignalCancel();
    }
    if (m_thread_created) {
        threadWaitForExit(&m_thread);
        threadClose(&m_thread);
    }
    m_screensaver.FlushPendingBrightness();
    m_usb_source.reset();

    if (m_was_mtp_enabled) {
        App::Notify("Re-enabled MTP"_i18n);
        App::SetMtpEnable(true);
    } else {
        if (App::GetHddEnable()) {
            if (App::GetWriteProtect()) {
                usbHsFsSetFileSystemMountFlags(UsbHsFsMountFlags_ReadOnly);
            }
            usbHsFsInitialize(1);
        }
    }
}

void InstallSession::UpdateActions() {
    RemoveActions();
    const auto state = m_state.load();
    const bool is_streaming = (m_origin == TransportOrigin::Mtp ||
                               m_origin == TransportOrigin::Ftp ||
                               m_origin == TransportOrigin::Web);

    if (state == State::Installing) {
        const auto cancel_label = is_streaming
            ? "Cancel installation"_i18n
            : "Cancel queue"_i18n;
        const auto cancel_prompt = is_streaming
            ? "Cancel installation?"_i18n
            : "Cancel installation queue?"_i18n;
        SetAction(Button::X, Action{cancel_label, [this, cancel_prompt]() {
            App::Push<OptionBox>(cancel_prompt, "No"_i18n, "Yes"_i18n, 0, [this](auto choice) {
                if (choice && *choice == 1) CancelSession();
            });
        }});
        if (!is_streaming) {
            if (m_summary_grace_timestamp.has_value() || (AllPackagesTerminal() && !HasKnownBatchTotals(m_origin))) {
                SetAction(Button::B, Action{"Done"_i18n, [this]() {
                    TransitionToSummary();
                    m_summary_grace_timestamp.reset();
                }});
            } else {
                SetAction(Button::B, Action{"Skip package"_i18n, [this]() {
                    size_t active_pkg{};
                    {
                        SCOPED_MUTEX(&m_mutex);
                        active_pkg = m_current_package;
                    }
                    App::Push<OptionBox>("Skip this package?"_i18n, "No"_i18n, "Yes"_i18n, 1, [this, active_pkg](auto choice) {
                        if (choice && *choice == 1) SkipCurrentPackage(active_pkg);
                    });
                }});
            }
        }
        SetAction(Button::R3, Action{m_minimized ? "Expand"_i18n : "Minimize"_i18n, [this]() { ToggleMinimized(); }});
    } else if (state == State::Summary || state == State::Cancelled) {
        SetAction(Button::B, Action{"Back"_i18n, [this]() {
            m_should_exit = true;
            SetPop();
        }});
        size_t error_count{};
        {
            SCOPED_MUTEX(&m_mutex);
            error_count = m_errors.size();
        }
        if (error_count) {
            const auto label = m_show_errors
                ? "Session log"_i18n
                : "Errors"_i18n + " (" + std::to_string(error_count) + ")";
            SetAction(Button::Y, Action{label, [this]() { ToggleErrorView(); }});
        }
    } else if (state == State::Failed) {
        SetAction(Button::B, Action{"Back"_i18n, [this]() {
            m_should_exit = true;
            SetPop();
        }});
    } else {
        if (is_streaming) {
            SetAction(Button::X, Action{"Cancel installation"_i18n, [this]() { CancelSession(); }});
        } else {
            SetAction(Button::B, Action{"Cancel session"_i18n, [this]() { CancelSession(); }});
            SetAction(Button::R3, Action{m_minimized ? "Expand"_i18n : "Minimize"_i18n, [this]() { ToggleMinimized(); }});
        }
    }

    if (state != State::Summary && state != State::Cancelled && state != State::Failed) {
        SetAction(Button::SELECT, Action{"Screen off"_i18n, [this]() {
            m_screensaver.Start();
        }});
    }
    m_actions_dirty = false;
}


void InstallSession::Update(Controller* controller, TouchInfo* touch) {
    if (m_state.load() == State::Installing && !HasKnownBatchTotals(m_origin)) {
        const bool transport_busy = (m_origin == TransportOrigin::Mtp)
            ? haze::HasActiveTransfer()
            : ftpsrv::HasActiveOrQueuedFiles();
        const bool should_grace = ShouldStartSummaryGracePeriod(
            m_origin, AllPackagesTerminal(), stream::BackgroundInstaller::IsInstalling(), transport_busy);

        if (should_grace) {
            if (!m_summary_grace_timestamp.has_value()) {
                m_summary_grace_timestamp.emplace();
                m_actions_dirty = true;
            } else if (m_summary_grace_timestamp->GetSecondsD() >= SUMMARY_GRACE_PERIOD_SEC) {
                TransitionToSummary();
                m_summary_grace_timestamp.reset();
            }
        } else {
            if (m_summary_grace_timestamp.has_value()) {
                m_summary_grace_timestamp.reset();
                m_actions_dirty = true;
            }
        }
    }

    if (m_actions_dirty) UpdateActions();

    if (m_state.load() != State::Installing) {
        m_screensaver.FlushPendingBrightness();
    }

    if (controller && controller->GotDown(Button::R3)) {
        ToggleMinimized();
        App::PlaySoundEffect(SoundEffect_Focus);
    }

    std::shared_ptr<PromptData> prompt{};
    if (mutexTryLock(&m_mutex)) {
        if (m_prompt_data && m_prompt_data->choice == -1) {
            prompt = m_prompt_data;
        }
        mutexUnlock(&m_mutex);
    }

    const double graph_elapsed = m_graph_timestamp.GetSecondsD();
    if (m_state.load() == State::Installing && graph_elapsed >= 0.5) {
        m_graph_timestamp.Update();
        const auto total_read = m_total_read.load();
        const auto total_write = m_total_write.load();
        m_read_history[m_history_index] = static_cast<s64>(std::max(0.0, (double)(total_read - m_graph_last_read) / graph_elapsed));
        m_write_history[m_history_index] = static_cast<s64>(std::max(0.0, (double)(total_write - m_graph_last_write) / graph_elapsed));
        // session peak, kept for the summary panel (the history window only
        // covers the last ~48 s).
        if (m_write_history[m_history_index] > m_peak_write_bps.load()) {
            m_peak_write_bps = m_write_history[m_history_index];
        }
        m_graph_last_read = total_read;
        m_graph_last_write = total_write;
        m_history_index = (m_history_index + 1) % SPEED_HISTORY;
        m_history_count = std::min(m_history_count + 1, SPEED_HISTORY);
    }

    // Minimized/background sessions still sample metrics, but Widget::Update
    // requires real input objects and must not receive null placeholders.
    if (!controller || !touch) {
        return;
    }

    const bool is_installing = (m_state.load() == State::Installing) && !WebShareIsRunning();
    const bool is_saver_active = m_screensaver.IsActive();
    const double now_sec = m_inactivity_timestamp.GetSecondsD();
    const long timeout_sec = App::GetBlankTimeout();

    TimeoutInput timeout_input{};
    if (controller) {
        timeout_input.kdown = controller->m_kdown;
        timeout_input.kheld = controller->m_kheld;
        timeout_input.stick_l_x = controller->m_stick_l.x;
        timeout_input.stick_l_y = controller->m_stick_l.y;
        timeout_input.stick_r_x = controller->m_stick_r.x;
        timeout_input.stick_r_y = controller->m_stick_r.y;
    }
    if (touch) {
        timeout_input.is_touching = touch->is_touching;
        timeout_input.is_clicked = touch->is_clicked;
    }

    if (m_inactivity_tracker.Update(is_installing, is_saver_active, timeout_sec, timeout_input, now_sec)) {
        m_screensaver.Start();
        m_inactivity_tracker.Reset(now_sec);
    }

    if (m_screensaver.IsActive()) {
        if (prompt) {
            m_screensaver.Stop();
            m_inactivity_tracker.Reset(now_sec);
        } else {
            m_screensaver.Update(controller, touch);
            if (m_screensaver.WantsWake(controller, touch)) {
                m_screensaver.Stop();
                m_inactivity_tracker.Reset(now_sec);
            }
            return;
        }
    }

    if (prompt) {
        int expected = -1;
        if (prompt->choice.compare_exchange_strong(expected, -2)) {
            std::string msg = prompt->title + "\n\n" + "Already installed. Reinstall?"_i18n;
            App::Push<OptionBox>(msg, "No"_i18n, "Yes"_i18n, 0, [prompt](std::optional<s64> choice) {
                if (choice && *choice == 1) {
                    prompt->choice = 1; // Yes
                } else {
                    prompt->choice = 0; // No
                }
            });
        }
    }

    std::optional<CompatibilityWarning> warn_popup{};
    {
        SCOPED_MUTEX(&m_mutex);
        if (!m_pending_warning_popups.empty() && (m_state == State::Summary || m_state == State::Cancelled || m_state == State::ReviewQueue)) {
            warn_popup = m_pending_warning_popups.front();
            m_pending_warning_popups.erase(m_pending_warning_popups.begin());
        }
    }
    if (warn_popup) {
        AddLog(FormatCompatibilityLog(*warn_popup), LogKind::Warning);
        App::Push<OptionBox>(FormatCompatibilityWarning(*warn_popup), "OK"_i18n);
    }

    MenuBase::Update(controller, touch);

    const auto state = m_state.load();
    if (m_show_errors) {
        SCOPED_MUTEX(&m_mutex);
        m_error_list->OnUpdate(controller, touch, m_error_index, m_errors.size(), [this](bool, s64 index) {
            m_error_index = index;
        }, this);
    } else if ((state == State::Installing || state == State::Summary || state == State::Cancelled) && !m_log.empty()) {
        SCOPED_MUTEX(&m_mutex);
        m_log_list->OnUpdate(controller, touch, m_log_index, m_log.size(), [this](bool, s64 index) {
            m_log_index = index;
        }, this);

        const s64 log_size = static_cast<s64>(m_log.size());
        if (log_size != m_log_last_seen_size) {
            const bool follow_tail = (m_log_last_seen_size == 0) || (m_log_index >= m_log_last_seen_size - 1);
            m_log_last_seen_size = log_size;
            if (follow_tail) {
                m_log_index = log_size - 1;
                const auto page = m_log_list->GetPage();
                const auto row = m_log_list->GetRow();
                const auto max_y = m_log_list->GetMaxY();
                float y_max = 0.f;
                if (log_size >= page) {
                    s64 rounded_count = log_size;
                    if (rounded_count % row) {
                        rounded_count = rounded_count + (row - rounded_count % row);
                    }
                    y_max = static_cast<float>(rounded_count - page) / row * max_y;
                }
                m_log_list->SetYoff(y_max);
            }
        }
    }
}


auto InstallSession::AvgWriteBps() const -> s64 {
    // caller holds m_mutex.
    if (!m_history_count) {
        return 0;
    }
    s64 sum = 0;
    for (size_t i = 0; i < m_history_count; i++) {
        const auto idx = (m_history_index + SPEED_HISTORY - m_history_count + i) % SPEED_HISTORY;
        sum += m_write_history[idx];
    }
    return sum / static_cast<s64>(m_history_count);
}

auto InstallSession::OverallDone() const -> s64 {
    // caller holds m_mutex. The bytes of the packages already off the queue,
    // plus what the one in flight has written so far.
    const s64 current_plan = m_current_package < m_queue.size() ? PlanSize(m_queue[m_current_package]) : 0;
    return m_plan_done_bytes + std::clamp<s64>(m_total_write.load() - m_package_write_start, 0, current_plan);
}

auto InstallSession::ComputeSaverInfo() -> SaverInfo {
    if (mutexTryLock(&m_mutex)) {
        ON_SCOPE_EXIT(mutexUnlock(&m_mutex));

        SaverInfo info{};
        const auto state = m_state.load();
        switch (state) {
            case State::WaitingForUsb:
            case State::WaitingForList: info.status = "Waiting for PC"_i18n; break;
            case State::Analysing:      info.status = "Analysing"_i18n; break;
            case State::ReviewQueue:    info.status = "Ready to install"_i18n; break;
            case State::Installing:
                info.status = m_origin == TransportOrigin::Mtp ? "MTP Install"_i18n
                    : m_origin == TransportOrigin::Ftp ? "FTP Install"_i18n
                    : m_origin == TransportOrigin::Web ? "Web Install"_i18n
                    : m_origin == TransportOrigin::Usb ? "USB Install"_i18n
                    : "Installing"_i18n;
                break;
            case State::Cancelled:      info.status = "Cancelled"_i18n; break;
            case State::Failed:         info.status = "Failed"_i18n; break;
            case State::Summary:
                info.status = m_session_failed ? "Finished with errors"_i18n : "Finished"_i18n;
                break;
        }

        info.file = m_current_title;
        if (info.file.empty() && m_current_package < m_queue.size()) {
            info.file = m_queue[m_current_package].file_name;
        }
        if (!m_current_transfer.empty() &&
            !path::EndsWithIC(m_current_transfer, ".nca") &&
            !path::EndsWithIC(m_current_transfer, ".ncz") &&
            m_current_transfer.find(".nca") == std::string::npos &&
            m_current_transfer.find(".ncz") == std::string::npos) {
            info.file += info.file.empty() ? m_current_transfer : " — " + m_current_transfer;
        }
        const bool known_batch = HasKnownBatchTotals(m_origin);
        info.package = known_batch ? std::min(m_current_package + 1, m_queue.size()) : 0;
        info.package_count = known_batch ? m_queue.size() : 0;
        info.installed = m_stats.installed;
        info.failed = m_stats.failed;
        info.is_complete = (state == State::Summary);
        info.is_failed = m_session_failed || (m_stats.failed > 0);

        const auto bps = AvgWriteBps();
        const auto done = OverallDone();
        info.ratio = known_batch && m_plan_total_bytes > 0
            ? std::clamp<double>((double)done / (double)m_plan_total_bytes, 0.0, 1.0)
            : m_progress_size > 0
                ? std::clamp<double>((double)m_progress_offset / (double)m_progress_size, 0.0, 1.0)
                : 0.0;
        info.speed_mib = static_cast<double>(bps) / (1024.0 * 1024.0);
        if (m_history_count >= 4) {
            const auto remaining = known_batch
                ? m_plan_total_bytes - done
                : std::max<s64>(0, m_progress_size - m_progress_offset);
            info.eta = FormatEta(remaining, bps);
        }
        info.elapsed_ns = m_stats.elapsed_ns ? m_stats.elapsed_ns : m_session_timestamp.GetNs();

        m_cached_saver_info = info;
    }

    m_cached_saver_info.has_graph = (m_history_count >= 2);
    m_cached_saver_info.history_count = m_history_count;
    m_cached_saver_info.history_index = m_history_index;
    m_cached_saver_info.read_history = m_read_history;
    m_cached_saver_info.write_history = m_write_history;

    return m_cached_saver_info;
}

} // namespace sphaira::ui::menu::dbi

#endif
