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
void Menu::ThreadFunction() {
    if (m_local_fs) {
        LocalThreadFunction();
        return;
    }

    const auto finish_cancelled = [this]() {
        if (m_usb_source) {
            m_usb_source->SetPostReadHook(nullptr);
        }
        m_state = State::Cancelled;
        m_actions_dirty = true;
    };
    ON_SCOPE_EXIT({
        if (m_usb_source) {
            m_usb_source->SetPostReadHook(nullptr);
        }
    });

    for (;;) {
        m_session_failed = false;
        if (m_cancel_requested || GetToken().stop_requested()) {
            finish_cancelled();
            return;
        }
        std::vector<std::string> names;
        if (!m_handover_names.empty()) {
            // the usb probe already connected and listed: start from its answer.
            names = std::move(m_handover_names);
            m_handover_names.clear();
        } else {
            m_state = State::WaitingForUsb;
            const auto rc = m_usb_source->IsUsbConnected(CONNECTION_TIMEOUT);
            if (rc == Result_UsbCancelled || m_cancel_requested || GetToken().stop_requested()) {
                finish_cancelled();
                return;
            }
            if (R_FAILED(rc)) continue;

            if (m_cancel_requested || GetToken().stop_requested()) {
                finish_cancelled();
                return;
            }
            m_state = State::WaitingForList;
            const auto list_rc = m_usb_source->WaitForConnection(CONNECTION_TIMEOUT, names);
            if (list_rc == Result_UsbCancelled || m_cancel_requested || GetToken().stop_requested()) {
                finish_cancelled();
                return;
            }
            if (R_FAILED(list_rc)) continue;
        }

                AddLog(std::string{"Connected: "_i18n} + yati::source::GetUsbProtocolName(m_usb_source->GetProtocol()), LogKind::Event);
        m_last_acked_revision = 0;
        if (m_usb_source && m_usb_source->HasSelectionSync()) {
            const auto spaces = GetPolledData();
            m_usb_source->SendStorageInfo(spaces.nand_free, spaces.nand_total, spaces.sd_free, spaces.sd_total);
        }

        // ponytail: stream hosts are refused rather than served. The queue
        // analyses every package before it installs any of them, which needs
        // random access, and a stream host answers ranges in list order only.
        // Serving them would mean a second, unanalysed install path -- add it
        // if a host that sets the flag ever turns up (ns-usbloader does not).
        if (m_usb_source->IsStream()) {
            m_fail_reason = "This PC app is in stream mode, which the install queue cannot review. Turn stream mode off and try again."_i18n;
            AddLog(m_fail_reason, LogKind::Error);
            m_session_failed = true;
            m_state = State::Failed;
            m_actions_dirty = true;
            return;
        }

        m_state = State::Analysing;
        m_actions_dirty = true;
        for (const auto& name : names) {
            if (m_cancel_requested || GetToken().stop_requested()) break;
            QueueEntry entry{};
            entry.file_name = name;
            m_usb_source->SetFileNameForTranfser(name);
            entry.analysis_result = yati::AnalyzeSource(m_usb_source.get(), fs::FsPath{name}, entry.analysis);
            s64 pc_size = m_usb_source->GetFileSize(name);
            if (pc_size > 0) {
                entry.analysis.source_size = pc_size;
            }
            int pc_target = m_usb_source->GetFileTarget(name);
            if (pc_target == 1) {
                entry.target = InstallTarget::Sd;
            } else if (pc_target == 2) {
                entry.target = InstallTarget::Nand;
            } else {
                entry.target = InstallTarget::Auto;
            }
            entry.selected = R_SUCCEEDED(entry.analysis_result);
            if (R_FAILED(entry.analysis_result)) {
                AddError(name, "Analysis"_i18n, entry.analysis_result);
            }
            SCOPED_MUTEX(&m_mutex);
            entry.source_index = m_queue.size();
            m_queue.emplace_back(std::move(entry));
        }

        if (m_cancel_requested || GetToken().stop_requested()) {
            m_usb_source->Finished(FINISHED_TIMEOUT);
            m_state = State::Cancelled;
            m_actions_dirty = true;
            return;
        }

        // Inner loop for ReviewQueue -> Installing -> Summary/Cancelled cycle
        for (;;) {
            if (m_cancel_requested || GetToken().stop_requested()) {
                break;
            }
            {
                SCOPED_MUTEX(&m_mutex);
                RecomputePlan(true);
            }
            m_state = State::ReviewQueue;
            m_actions_dirty = true;
            m_install_requested = false; // Reset request flag

            const bool sync_supported = m_usb_source && m_usb_source->HasSelectionSync();
            TimeStamp last_poll{};

            while (!m_install_requested && !m_cancel_requested && !GetToken().stop_requested()) {
                if (sync_supported && last_poll.GetNs() >= 300'000'000) {
                    last_poll.Update();
                    // the console's changes go out before the PC's state comes in.
                    SendQueuePlanIfChanged();
                    std::vector<yati::source::Usb::LiveQueueItem> items;
                    u32 revision = 0;
                    if (R_SUCCEEDED(m_usb_source->FetchLiveQueue(items, revision))) {
                        ApplyLiveQueue(items, 0, false);
                        if (revision > 0 && revision != m_last_acked_revision) {
                            if (R_SUCCEEDED(m_usb_source->SendQueueAck(revision))) {
                                m_last_acked_revision = revision;
                            }
                        }
                    }
                }
                svcSleepThread(10'000'000);
            }
            if (m_cancel_requested || GetToken().stop_requested()) {
                break;
            }

            if (sync_supported) {
                std::vector<yati::source::Usb::LiveQueueItem> items;
                u32 revision = 0;
                if (R_SUCCEEDED(m_usb_source->FetchLiveQueue(items, revision))) {
                    if (ApplyLiveQueue(items, 0, false)) {
                        SCOPED_MUTEX(&m_mutex);
                        m_plan_total_bytes = 0;
                        for (auto& entry : m_queue) {
                            entry.install_selected = entry.selected && R_SUCCEEDED(entry.analysis_result);
                            entry.install_sd = entry.planned_sd;
                            if (entry.install_selected) {
                                AddSizeSaturated(m_plan_total_bytes, PlanSize(entry));
                            }
                        }
                    }
                    if (revision > 0 && revision != m_last_acked_revision) {
                        if (R_SUCCEEDED(m_usb_source->SendQueueAck(revision))) {
                            m_last_acked_revision = revision;
                        }
                    }
                }
            }

            m_state = State::Installing;
            m_actions_dirty = true;
            BeginSessionStats();
            bool session_failed{};
            for (size_t i = 0; i < m_queue.size(); i++) {
                if (sync_supported) {
                    std::vector<yati::source::Usb::LiveQueueItem> items;
                    u32 revision = 0;
                    if (R_SUCCEEDED(m_usb_source->FetchLiveQueue(items, revision))) {
                        ApplyLiveQueue(items, i, true);
                        if (revision > 0 && revision != m_last_acked_revision) {
                            if (R_SUCCEEDED(m_usb_source->SendQueueAck(revision))) {
                                m_last_acked_revision = revision;
                            }
                        }
                    }
                }
                bool selected{};
                yati::InstallAnalysis analysis{};
                bool plan_sd{};
                bool analysis_deferred{};
                std::string name{};
                {
                    SCOPED_MUTEX(&m_mutex);
                    selected = m_queue[i].install_selected;
                    analysis = m_queue[i].analysis;
                    plan_sd = m_queue[i].install_sd;
                    analysis_deferred = m_queue[i].analysis_deferred;
                    name = m_queue[i].file_name;
                    if (selected) {
                        m_skip_requested = false;
                        ueventClear(&m_cancel_event);
                        m_current_package = i;
                        m_current_title = name;
                        m_progress_offset = 0;
                        m_progress_size = 0;
                        m_package_write_start = m_total_write.load();
                        m_current_file_reinstall_choice = std::nullopt;
                        m_current_file_skipped = false;
                    }
                }
                if (!selected) continue;
                if (m_cancel_requested) break;

                if (analysis_deferred) {
                    m_usb_source->SetFileNameForTranfser(name);
                    const auto a_rc = yati::AnalyzeSource(m_usb_source.get(), fs::FsPath{name}, analysis);
                    {
                        SCOPED_MUTEX(&m_mutex);
                        m_queue[i].analysis = analysis;
                        m_queue[i].analysis_result = a_rc;
                        m_queue[i].analysis_deferred = false;
                        m_plan_total_bytes = m_plan_done_bytes;
                        for (size_t j = i; j < m_queue.size(); j++) {
                            if (m_queue[j].install_selected) {
                                AddSizeSaturated(m_plan_total_bytes, PlanSize(m_queue[j]));
                            }
                        }
                        m_actions_dirty = true;
                    }
                    if (R_FAILED(a_rc)) {
                        AddError(name, "Analysis"_i18n, a_rc);
                        RecordPackageResult(i, a_rc, false, false, plan_sd, 0, 0);
                        if (m_usb_source && m_usb_source->HasSelectionSync()) {
                            m_usb_source->SendPackageStatus(name, 3, a_rc);
                        }
                        continue;
                    }
                }

                // Pick Auto after deferred analysis supplies the actual install size.
                plan_sd = RefreshAutoInstallTarget(i);

                AddLog("Starting: "_i18n + name, LogKind::Event);
                yati::ConfigOverride override{};
                override.skip_if_already_installed = SkipMode();
                // pass the resolved target so yati does not ChooseInstallTarget again.
                override.sd_card_install = plan_sd;
                const auto read_before = m_total_read.load();
                const auto write_before = m_total_write.load();

                // The host re-enumerating us mid-package (cable nudged, hub or
                // port glitch) kills the transfer, and dbi backend restarts its
                // command loop on its own. Give the link a bounded chance to
                // come back and replay the package from the start -- yati has
                // already dropped the half-written placeholders -- instead of
                // losing the rest of the queue to a one second blip.
                Result install_rc{};
                TimeStamp hook_poll{};
                for (u32 attempt = 0; ; attempt++) {
                    m_usb_source->SetFileNameForTranfser(name);
                    if (sync_supported) {
                        m_usb_source->SetPostReadHook([this, i, &hook_poll]() {
                            if (hook_poll.GetNs() >= 500'000'000) {
                                hook_poll.Update();
                                std::vector<yati::source::Usb::LiveQueueItem> items;
                                u32 revision = 0;
                                if (R_SUCCEEDED(m_usb_source->FetchLiveQueue(items, revision))) {
                                    ApplyLiveQueue(items, i, true);
                                    if (revision > 0 && revision != m_last_acked_revision) {
                                        if (R_SUCCEEDED(m_usb_source->SendQueueAck(revision))) {
                                            m_last_acked_revision = revision;
                                        }
                                    }
                                }
                            }
                        });
                    }
                    install_rc = yati::InstallFromCollections(this, m_usb_source.get(), analysis.collections, override);
                    if (sync_supported) {
                        m_usb_source->SetPostReadHook(nullptr);
                    }
                    if ((!usb::IsLinkError(install_rc) && !IsDbiSessionError(install_rc)) || attempt >= MAX_LINK_RETRIES) {
                        break;
                    }
                    if (m_cancel_requested || m_skip_requested || GetToken().stop_requested()) {
                        break;
                    }
                    AddLog("USB connection lost; reconnecting..."_i18n, LogKind::Warning);
                    if (R_FAILED(ReestablishUsbLink())) {
                        break;
                    }
                    AddLog("USB connection restored; retrying: "_i18n + name, LogKind::Warning);
                }
                const bool user_skipped = m_skip_requested.load();
                const bool cancelled = m_cancel_requested || (!user_skipped && (install_rc == Result_TransferCancelled || install_rc == Result_UsbCancelled));
                const bool fatal_session_error = !user_skipped && R_FAILED(install_rc) && IsDbiSessionError(install_rc);
                RecordPackageResult(i, install_rc, cancelled, user_skipped, plan_sd,
                    m_total_read.load() - read_before, m_total_write.load() - write_before);
                if (user_skipped) {
                    AddLog("Skipped: "_i18n + name, LogKind::Success);
                } else if (R_SUCCEEDED(install_rc)) {
                    if (m_current_file_skipped) {
                        AddLog("Skipped: "_i18n + name + " — " + "already installed"_i18n, LogKind::Success);
                        AddLog("Change \"Skip if already installed\" in Settings to reinstall."_i18n, LogKind::Normal);
                    } else {
                        AddLog("Installed: "_i18n + name, LogKind::Success);
                    }
                }
                else if (cancelled) AddLog("Cancelled: "_i18n + name, LogKind::Warning);
                else {
                    AddLog("Failed: "_i18n + name + " (" + ResultText(install_rc) + ")", LogKind::Error);
                    AddError(name, "Install"_i18n, install_rc);
                }

                if (m_usb_source && m_usb_source->HasSelectionSync() && !cancelled) {
                    u32 pkg_status = 3; // Failed
                    if (user_skipped) {
                        pkg_status = 1; // User Skipped
                    } else if (R_SUCCEEDED(install_rc)) {
                        if (m_current_file_skipped) {
                            pkg_status = 2; // Already Installed
                        } else {
                            pkg_status = 0; // Installed
                        }
                    }
                    m_usb_source->SendPackageStatus(name, pkg_status, install_rc);
                    const auto spaces = GetPolledData();
                    m_usb_source->SendStorageInfo(spaces.nand_free, spaces.nand_total, spaces.sd_free, spaces.sd_total);
                }
                if (cancelled) {
                    m_cancel_requested = true;
                    break;
                }
                if (fatal_session_error) {
                    session_failed = true;
                    m_session_failed = true;
                    if (usb::IsLinkError(install_rc)) {
                        AddLog("The USB connection dropped and could not be restored. Check the cable and the port, then start the queue again."_i18n, LogKind::Error);
                    }
                    AddLog("USB session failed; remaining packages were skipped."_i18n, LogKind::Error);
                    break;
                }
            }

            std::vector<std::string> rejected_names;
            {
                SCOPED_MUTEX(&m_mutex);
                m_stats.elapsed_ns = m_session_timestamp.GetNs();
                for (const auto& entry : m_queue) {
                    if (entry.rejected_no_space) {
                        rejected_names.push_back(entry.file_name);
                    }
                }
            }
            for (const auto& name : rejected_names) {
                AddLog("Not installed: "_i18n + name + " — " + "not enough free space"_i18n, LogKind::Error);
            }
            if (m_cancel_requested) {
                AddLog("Session cancelled; completed installs were kept."_i18n, LogKind::Warning);
                m_state = State::Cancelled;
            } else if (session_failed) {
                m_state = State::Summary;
            } else {
                AddLog("Queue finished."_i18n, LogKind::Event);
                m_state = State::Summary;
            }
            m_actions_dirty = true;

            // Wait in Summary/Cancelled state
            while ((m_state == State::Summary || m_state == State::Cancelled) && !m_cancel_requested && !GetToken().stop_requested()) {
                svcSleepThread(1e+6);
            }

            if (m_cancel_requested || GetToken().stop_requested()) {
                break;
            }
        }

        if (!m_session_failed) {
            m_usb_source->Finished(FINISHED_TIMEOUT);
        }
        if (m_cancel_requested) {
            m_state = State::Cancelled;
        } else {
            m_state = State::Summary;
        }
        m_actions_dirty = true;
        return;
    }
}

Result Menu::ReestablishUsbLink() {
    // let the host finish re-enumerating before talking to it again.
    svcSleepThread(1e+9);
    R_TRY(m_usb_source->IsUsbConnected(RECONNECT_TIMEOUT));

    // replay the list handshake: one round trip that proves both sides are back
    // in lockstep before the next file range request goes out. Anything the
    // host left in the pipe before the drop trips the magic check here, which
    // ends the session, rather than silently shifting a package's bytes.
    //
    // Each call is one detection round of a couple of seconds, so keep asking
    // until the reconnect window is spent: the host may still be restarting
    // its own command loop when the first round goes out.
    std::vector<std::string> names;
    TimeStamp ts;
    Result rc;
    do {
        rc = m_usb_source->WaitForConnection(RECONNECT_TIMEOUT, names);
        if (rc == Result_UsbCancelled || m_cancel_requested || GetToken().stop_requested()) {
            break;
        }
    } while (R_FAILED(rc) && ts.GetNs() < RECONNECT_TIMEOUT);

    R_TRY(rc);
    R_SUCCEED();
}
} // namespace sphaira::ui::menu::dbi

#endif
