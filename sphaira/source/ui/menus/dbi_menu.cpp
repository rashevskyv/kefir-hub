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
Menu::Menu(u32 flags) : MenuBase{"Install queue"_i18n, flags} {
    mutexInit(&m_mutex);
    ueventCreate(&m_cancel_event, false);

    m_session_skip_if_already_installed = App::GetApp()->m_skip_if_already_installed.Get();
    m_session_install_location = App::GetInstallLocation();
    m_session_reserve_mb = App::GetInstallReserveMb();
    m_session_reserve_sd_mb = App::GetInstallReserveSdMb();

    const Vec4 queue_pos{70.f, GetY() + 63.f, 1140.f, 470.f};
    const Vec4 row{queue_pos.x, queue_pos.y, queue_pos.w, 78.f};
    m_list = std::make_unique<List>(1, 6, queue_pos, row);
    m_list->SetLayout(List::Layout::GRID);
    const Vec4 log_pos{70.f, GetY() + 235.f, 1140.f, 310.f};
    const Vec4 log_row{log_pos.x, log_pos.y, log_pos.w, 30.f};
    m_log_list = std::make_unique<List>(1, 10, log_pos, log_row);
    m_log_list->SetLayout(List::Layout::GRID);
    m_error_list = std::make_unique<List>(1, 5, log_pos, Vec4{log_pos.x, log_pos.y, log_pos.w, 55.f});
    m_error_list->SetLayout(List::Layout::GRID);
    UpdateActions();

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
    : MenuBase{"Install queue"_i18n, flags}, m_local_fs{fs}, m_local_paths{std::move(paths)},
      m_local_source_sizes{std::move(source_sizes)}, m_defer_local_analysis{defer_analysis} {
    mutexInit(&m_mutex);
    ueventCreate(&m_cancel_event, false);

    m_session_skip_if_already_installed = App::GetApp()->m_skip_if_already_installed.Get();
    m_session_install_location = App::GetInstallLocation();
    m_session_reserve_mb = App::GetInstallReserveMb();
    m_session_reserve_sd_mb = App::GetInstallReserveSdMb();

    const Vec4 queue_pos{70.f, GetY() + 63.f, 1140.f, 470.f};
    m_list = std::make_unique<List>(1, 6, queue_pos, Vec4{queue_pos.x, queue_pos.y, queue_pos.w, 78.f});
    m_list->SetLayout(List::Layout::GRID);
    const Vec4 log_pos{70.f, GetY() + 235.f, 1140.f, 310.f};
    m_log_list = std::make_unique<List>(1, 10, log_pos, Vec4{log_pos.x, log_pos.y, log_pos.w, 30.f});
    m_log_list->SetLayout(List::Layout::GRID);
    m_error_list = std::make_unique<List>(1, 5, log_pos, Vec4{log_pos.x, log_pos.y, log_pos.w, 55.f});
    m_error_list->SetLayout(List::Layout::GRID);
    m_state = State::Analysing;
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

void Menu::UpdateActions() {
    RemoveActions();
    const auto state = m_state.load();
    if (state == State::ReviewQueue) {
        SetActions(
            std::make_pair(Button::X, Action{"Select"_i18n, [this]() {
                SCOPED_MUTEX(&m_mutex);
                if (m_install_requested) return;
                if (m_index >= 0 && m_index < static_cast<s64>(m_queue.size())) {
                    if (R_SUCCEEDED(m_queue[m_index].analysis_result)) {
                        m_queue[m_index].selected = !m_queue[m_index].selected;
                    }
                    if (m_index + 1 < static_cast<s64>(m_queue.size())) {
                        SetIndex(m_index + 1);
                    }
                }
            }}),
            std::make_pair(Button::Y, Action{"Invert"_i18n, [this]() {
                SCOPED_MUTEX(&m_mutex);
                if (m_install_requested) return;
                for (auto& entry : m_queue) {
                    if (R_SUCCEEDED(entry.analysis_result)) entry.selected = !entry.selected;
                }
            }}),
            std::make_pair(Button::A, Action{"Install selected"_i18n, [this]() { StartInstall(); }}),
            std::make_pair(Button::R3, Action{"Package target"_i18n, [this]() { CycleSelectedTarget(); }}),
            std::make_pair(Button::START, Action{"Options"_i18n, [this]() { DisplayQueueOptions(); }}),
            std::make_pair(Button::B, Action{"Cancel session"_i18n, [this]() { CancelSession(); }})
        );
    } else if (state == State::Installing) {
        SetActions(
            std::make_pair(Button::X, Action{"Cancel queue"_i18n, [this]() {
                App::Push<OptionBox>("Cancel installation queue?"_i18n, "No"_i18n, "Yes"_i18n, 0, [this](auto choice) {
                    if (choice && *choice == 1) {
                        CancelSession();
                    }
                });
            }}),
            std::make_pair(Button::B, Action{"Skip package"_i18n, [this]() {
                size_t active_pkg{};
                {
                    SCOPED_MUTEX(&m_mutex);
                    active_pkg = m_current_package;
                }
                App::Push<OptionBox>("Skip this package?"_i18n, "No"_i18n, "Yes"_i18n, 1, [this, active_pkg](auto choice) {
                    if (choice && *choice == 1) {
                        SkipCurrentPackage(active_pkg);
                    }
                });
            }}),
            std::make_pair(Button::START, Action{"Options"_i18n, [this]() { DisplayQueueOptions(); }})
        );
    } else if (state == State::Summary || state == State::Cancelled) {
        if (state == State::Summary && !m_session_failed) {
            SetAction(Button::B, Action{"Back"_i18n, [this]() {
                m_state = State::ReviewQueue;
                m_actions_dirty = true;
            }});
        } else {
            SetAction(Button::B, Action{"Back"_i18n, [this]() { SetPop(); }});
        }
        // m_errors is filled by the worker thread; take the lock rather than
        // racing it for the count.
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
        SetAction(Button::B, Action{"Back"_i18n, [this]() { SetPop(); }});
    } else {
        SetAction(Button::B, Action{"Cancel session"_i18n, [this]() { CancelSession(); }});
    }

    // only while the queue still has something to do: once it has ended there
    // is nothing left to walk away from.
    if (state != State::Summary && state != State::Cancelled && state != State::Failed) {
        SetAction(Button::SELECT, Action{"Screen off"_i18n, [this]() {
            m_screensaver.Start();
        }});
    }
    m_actions_dirty = false;
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    if (m_actions_dirty) UpdateActions();

    if (m_state.load() != State::Installing) {
        m_screensaver.FlushPendingBrightness();
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

    const bool is_installing = (m_state.load() == State::Installing);
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
        // a question needs an answer, so it wins over a blanked panel: the
        // option box would otherwise be raised behind a screen nobody can read.
        if (prompt) {
            m_screensaver.Stop();
            m_inactivity_tracker.Reset(now_sec);
        } else {
            m_screensaver.Update(controller, touch);
            // the press that wakes the panel is spent on waking it -- waking
            // with B must not also cancel the queue behind it.
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
    bool activate{};
    if (state == State::ReviewQueue && !m_queue.empty()) {
        {
            SCOPED_MUTEX(&m_mutex);
            m_list->OnUpdate(controller, touch, m_index, m_queue.size(), [this, &activate](bool pressed, s64 index) {
                if (pressed && m_index == index) activate = true;
                else SetIndex(index);
            }, this);
        }
        if (activate) FireAction(Button::A);
    } else if (m_show_errors) {
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

auto Menu::AvgWriteBps() const -> s64 {
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

auto Menu::OverallDone() const -> s64 {
    // caller holds m_mutex. The bytes of the packages already off the queue,
    // plus what the one in flight has written so far.
    const s64 current_plan = m_current_package < m_queue.size() ? PlanSize(m_queue[m_current_package]) : 0;
    return m_plan_done_bytes + std::clamp<s64>(m_total_write.load() - m_package_write_start, 0, current_plan);
}

auto Menu::ComputeSaverInfo() -> SaverInfo {
    if (mutexTryLock(&m_mutex)) {
        ON_SCOPE_EXIT(mutexUnlock(&m_mutex));

        SaverInfo info{};
        const auto state = m_state.load();
        switch (state) {
            case State::WaitingForUsb:
            case State::WaitingForList: info.status = "Waiting for PC"_i18n; break;
            case State::Analysing:      info.status = "Analysing"_i18n; break;
            case State::ReviewQueue:    info.status = "Ready to install"_i18n; break;
            case State::Installing:     info.status = "Installing"_i18n; break;
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
        info.package = std::min(m_current_package + 1, m_queue.size());
        info.package_count = m_queue.size();
        info.installed = m_stats.installed;
        info.failed = m_stats.failed;
        info.is_complete = (state == State::Summary);
        info.is_failed = m_session_failed || (m_stats.failed > 0);

        const auto bps = AvgWriteBps();
        const auto done = OverallDone();
        info.ratio = m_plan_total_bytes > 0
            ? std::clamp<double>((double)done / (double)m_plan_total_bytes, 0.0, 1.0) : 0.0;
        info.speed_mib = static_cast<double>(bps) / (1024.0 * 1024.0);
        if (m_history_count >= 4) {
            info.eta = FormatEta(m_plan_total_bytes - done, bps);
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

void Menu::SetIndex(s64 index) {
    m_index = index;
    if (!m_index) {
        m_list->SetYoff();
    } else if (!m_queue.empty()) {
        // keep one row of context past the cursor visible, so scrolling starts
        // at the second-to-last row rather than when the cursor falls off the
        // edge. also covers the callers that move the index themselves (X
        // toggling selection), which otherwise never touched the scroll offset.
        const s64 count = m_queue.size();
        m_list->EnsureVisible(m_index + 1, count);
        m_list->EnsureVisible(m_index - 1, count);
    }
}

void Menu::CycleSelectedTarget() {
    SCOPED_MUTEX(&m_mutex);
    if (m_install_requested) return;
    if (m_index < 0 || m_index >= static_cast<s64>(m_queue.size()) || R_FAILED(m_queue[m_index].analysis_result)) return;
    auto& target = m_queue[m_index].target;
    target = target == InstallTarget::Auto ? InstallTarget::Sd
        : target == InstallTarget::Sd ? InstallTarget::Nand : InstallTarget::Auto;
}

void Menu::DisplayQueueOptions(bool left_side) {
    auto options = std::make_unique<Sidebar>("Install Options"_i18n, left_side ? Sidebar::Side::LEFT : Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    const bool global = App::GetSaveSettingsGlobally();

    SidebarEntryArray::Items skip_installed_items;
    skip_installed_items.push_back("Reinstall"_i18n);
    skip_installed_items.push_back("Skip"_i18n);
    skip_installed_items.push_back("Prompt"_i18n);

    s64 current_skip = global ? App::GetApp()->m_skip_if_already_installed.Get() : m_session_skip_if_already_installed;
    options->Add<SidebarEntryArray>("Skip if already installed"_i18n, skip_installed_items, [this, global](s64& index_out){
        if (global) {
            App::GetApp()->m_skip_if_already_installed.Set(index_out);
        } else {
            m_session_skip_if_already_installed = index_out;
        }
    }, current_skip, "For titles / ncas already installed: reinstall, skip, or prompt each time."_i18n);

    SidebarEntryArray::Items install_items;
    install_items.push_back("microSD card only"_i18n);
    install_items.push_back("System memory only"_i18n);
    install_items.push_back("System first, then SD"_i18n);
    install_items.push_back("SD first, then system"_i18n);
    install_items.push_back("Automatic"_i18n);

    s64 current_loc = global ? App::GetInstallLocation() : m_session_install_location;
    options->Add<SidebarEntryArray>("Install location"_i18n, install_items, [this, global](s64& index_out){
        if (global) {
            App::SetInstallLocation(index_out);
        } else {
            m_session_install_location = index_out;
        }
    }, current_loc);

    // one entry per target: the two media fill at very different rates and want
    // very different headroom.
    auto add_reserve = [&](const std::string& title, const std::string& prompt, const std::string& help,
                           long (*get)(), void (*set)(long), long* session) {
        auto entry_ptr = std::make_unique<SidebarEntryTextBase>(title,
            std::to_string(global ? get() : *session) + " MB", nullptr, help);
        auto* entry = entry_ptr.get();
        entry->SetCallback([this, global, entry, prompt, get, set, session]() {
            s64 out = global ? get() : *session;
            if (R_SUCCEEDED(swkbd::ShowNumPad(out, prompt.c_str(), std::to_string(out).c_str(), 1, 5))) {
                if (out >= 0 && out <= 32768) {
                    if (global) {
                        set(out);
                    } else {
                        *session = out;
                    }
                    entry->SetValue(std::to_string(out) + " MB");
                }
            }
        });
        options->Add(std::move(entry_ptr));
    };

    add_reserve("Reserve free space (system)"_i18n, "Enter System Reserve Free Space (MB)"_i18n,
        "Free space to keep on system memory when planning installs (MB)."_i18n,
        App::GetInstallReserveMb, App::SetInstallReserveMb, &m_session_reserve_mb);
    add_reserve("Reserve free space (microSD)"_i18n, "Enter microSD Reserve Free Space (MB)"_i18n,
        "Free space to keep on the microSD card when planning installs (MB)."_i18n,
        App::GetInstallReserveSdMb, App::SetInstallReserveSdMb, &m_session_reserve_sd_mb);

    auto screen_off_entry = options->Add<SidebarEntryCallback>("Screen off (Minus)"_i18n, [left_side]() {
        auto sub = std::make_unique<Sidebar>("Screen off (Minus)"_i18n, left_side ? Sidebar::Side::LEFT : Sidebar::Side::RIGHT);
        ON_SCOPE_EXIT(App::Push(std::move(sub)));

        static constexpr const char* MODE_LABELS[] = {
            "Lower brightness",
            "Turn off backlight",
            "Screensaver",
        };
        static constexpr long BRIGHTNESS_STEPS[] = { 1, 5, 10, 20, 30, 50 };
        static constexpr const char* TIMEOUT_LABELS[] = {
            "Off",
            "30 s",
            "1 min",
            "2 min",
            "5 min",
            "10 min",
        };
        static constexpr long TIMEOUT_STEPS[] = { 0, 30, 60, 120, 300, 600 };

        SidebarEntryArray::Items mode_items;
        for (const auto& label : MODE_LABELS) {
            mode_items.push_back(i18n::get(label));
        }
        s64 current_mode = std::clamp<s64>(App::GetBlankMode(), 0, std::size(MODE_LABELS) - 1);
        sub->Add<SidebarEntryArray>("Minus button"_i18n, mode_items, [](s64& index_out) {
            App::SetBlankMode(index_out);
        }, current_mode, "What pressing Minus does while the install queue is running."_i18n);

        SidebarEntryArray::Items timeout_items;
        s64 current_timeout_idx = 0;
        const long current_timeout = App::GetBlankTimeout();
        for (size_t i = 0; i < std::size(TIMEOUT_STEPS); i++) {
            timeout_items.push_back(i18n::get(TIMEOUT_LABELS[i]));
            if (TIMEOUT_STEPS[i] == current_timeout) {
                current_timeout_idx = static_cast<s64>(i);
            }
        }
        sub->Add<SidebarEntryArray>("Inactivity timeout"_i18n, timeout_items, [](s64& index_out) {
            if (index_out >= 0 && index_out < static_cast<s64>(std::size(TIMEOUT_STEPS))) {
                App::SetBlankTimeout(TIMEOUT_STEPS[index_out]);
            }
        }, current_timeout_idx, "Automatically start the screen off mode after a period of inactivity during installation."_i18n);

        SidebarEntryArray::Items brightness_items;
        s64 current_brightness_idx = 0;
        const long current_brightness = App::GetBlankBrightness();
        for (size_t i = 0; i < std::size(BRIGHTNESS_STEPS); i++) {
            brightness_items.push_back(std::to_string(BRIGHTNESS_STEPS[i]) + "%");
            if (BRIGHTNESS_STEPS[i] == current_brightness) {
                current_brightness_idx = static_cast<s64>(i);
            }
        }
        sub->Add<SidebarEntryArray>("Brightness"_i18n, brightness_items, [](s64& index_out) {
            if (index_out >= 0 && index_out < static_cast<s64>(std::size(BRIGHTNESS_STEPS))) {
                App::SetBlankBrightness(BRIGHTNESS_STEPS[index_out]);
            }
        }, current_brightness_idx, "Panel brightness while the screen is lowered. Ignored when the backlight is turned off."_i18n);

        sub->Add<SidebarEntryBool>("OLED mode"_i18n, App::GetSaverOled(), [](bool& val) {
            App::SetSaverOled(val);
        }, "Light only the pixels that carry information: the empty part of the progress bar is left black."_i18n);

        sub->Add<SidebarEntryHeader>("Show on screensaver"_i18n);

        const auto add_field = [&sub](SaverField bit, const std::string& label, const std::string& description) {
            const bool enabled = (App::GetSaverFields() & bit) != 0;
            sub->Add<SidebarEntryBool>(label, enabled, [bit](bool& val) {
                App::SetSaverField(bit, val);
            }, description);
        };

        add_field(SaverField_Clock, "Clock"_i18n, "Show the current time."_i18n);
        add_field(SaverField_Status, "Status"_i18n, "Show what the queue is doing."_i18n);
        add_field(SaverField_Counter, "Package counter"_i18n, "Show which package of how many is being installed."_i18n);
        add_field(SaverField_File, "Current file"_i18n, "Show the package and file being written."_i18n);
        add_field(SaverField_Bar, "Progress bar"_i18n, "Show the whole-queue progress bar and percentage."_i18n);
        add_field(SaverField_Speed, "Average speed"_i18n, "Show the average write speed."_i18n);
        add_field(SaverField_Eta, "Time remaining"_i18n, "Show the estimated time left for the whole queue."_i18n);
        add_field(SaverField_Elapsed, "Elapsed time"_i18n, "Show how long the queue has been running."_i18n);
        add_field(SaverField_Battery, "Battery"_i18n, "Show the battery level and whether it is charging."_i18n);
        add_field(SaverField_Errors, "Errors"_i18n, "Show the failure count, once anything has failed."_i18n);
        add_field(SaverField_Graph, "Speed graph"_i18n, "Show the live installation read/write speed graph."_i18n);
    }, "Blank or dim the panel while a long queue runs, and choose what the screensaver shows."_i18n);
    screen_off_entry->SetHasSubmenu(true);
}
} // namespace sphaira::ui::menu::dbi

#endif