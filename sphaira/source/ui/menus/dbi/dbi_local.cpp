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
void Menu::LocalThreadFunction() {
    m_state = State::Analysing;
    m_actions_dirty = true;

    for (const auto& path : m_local_paths) {
        if (m_cancel_requested || GetToken().stop_requested()) {
            m_state = State::Cancelled;
            m_actions_dirty = true;
            return;
        }

        QueueEntry entry{};
        if (const auto slash = std::strrchr(path.s, '/')) {
            entry.file_name = slash + 1;
        } else {
            entry.file_name = path.s;
        }
        const auto path_index = static_cast<size_t>(&path - m_local_paths.data());
        entry.source_index = path_index;
        const auto listed_size = path_index < m_local_source_sizes.size()
            ? std::max<s64>(0, m_local_source_sizes[path_index]) : 0;
        if (m_defer_local_analysis) {
            // network source: network analysis is bypassed to prevent potential hangs
            // during ranged read/seek operations inside curl/devoptab. We immediately
            // fall back to deferred analysis which uses the listed size.
            log_write("[DBI] LocalThreadFunction: network source, bypassing network analysis (deferred) for %s\n", path.s);
            entry.analysis = {};
            entry.analysis_deferred = true;
            entry.analysis_result = 0;
            entry.analysis.source_size = listed_size;
        } else {
            yati::source::File source{m_local_fs, path};
            entry.analysis_result = source.GetOpenResult();
            if (R_SUCCEEDED(entry.analysis_result)) {
                entry.analysis_result = yati::AnalyzeSource(&source, path, entry.analysis);
            }
            s64 source_size{};
            if (R_SUCCEEDED(source.GetOpenResult()) && R_SUCCEEDED(source.GetSize(&source_size))) {
                entry.analysis.source_size = source_size;
            }
        }
        entry.selected = R_SUCCEEDED(entry.analysis_result);
        if (R_FAILED(entry.analysis_result)) {
            AddError(entry.file_name, "Analysis"_i18n, entry.analysis_result);
        }
        SCOPED_MUTEX(&m_mutex);
        m_queue.emplace_back(std::move(entry));
    }

    for (;;) {
        if (m_cancel_requested || GetToken().stop_requested()) {
            m_state = State::Cancelled;
            m_actions_dirty = true;
            return;
        }

        {
            SCOPED_MUTEX(&m_mutex);
            RecomputePlan(true);
        }
        m_state = State::ReviewQueue;
        m_actions_dirty = true;
        m_install_requested = false;
        while (!m_install_requested && !m_cancel_requested && !GetToken().stop_requested()) {
            svcSleepThread(1e+6);
        }
        if (m_cancel_requested || GetToken().stop_requested()) {
            m_state = State::Cancelled;
            m_actions_dirty = true;
            return;
        }

        m_state = State::Installing;
        m_actions_dirty = true;
        BeginSessionStats();

        for (size_t i = 0; i < m_queue.size(); i++) {
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

            // Auto re-picks from live usable space; pinned Sd/Nand stay frozen.
            plan_sd = RefreshAutoInstallTarget(i);

            AddLog("Starting: "_i18n + name, LogKind::Event);
            yati::ConfigOverride override{};
            override.skip_if_already_installed = App::GetSaveSettingsGlobally()
                ? App::GetApp()->m_skip_if_already_installed.Get()
                : m_session_skip_if_already_installed;
            // pass the resolved target so yati does not ChooseInstallTarget again.
            override.sd_card_install = plan_sd;

            const auto read_before = m_total_read.load();
            const auto write_before = m_total_write.load();

            const auto src_idx = m_queue[i].source_index;
            Result result{};
            if (src_idx >= m_local_paths.size()) {
                result = FsError_PathNotFound;
            } else if (analysis_deferred) {
                result = yati::InstallFromFile(this, m_local_fs, m_local_paths[src_idx], override);
            } else {
                yati::source::File source{m_local_fs, m_local_paths[src_idx]};
                const auto open_rc = source.GetOpenResult();
                result = R_SUCCEEDED(open_rc)
                    ? yati::InstallFromCollections(this, &source, analysis.collections, override)
                    : open_rc;
            }
            const bool user_skipped = m_skip_requested.load();
            const bool cancelled = m_cancel_requested || (!user_skipped && (result == Result_TransferCancelled || result == Result_UsbCancelled));
            RecordPackageResult(i, result, cancelled, user_skipped, plan_sd,
                m_total_read.load() - read_before, m_total_write.load() - write_before);
            if (user_skipped) {
                AddLog("Skipped: "_i18n + name, LogKind::Success);
            } else if (R_SUCCEEDED(result)) {
                if (m_current_file_skipped) {
                    AddLog("Skipped: "_i18n + name + " — " + "already installed"_i18n, LogKind::Success);
                    AddLog("Change \"Skip if already installed\" in Settings to reinstall."_i18n, LogKind::Normal);
                } else {
                    AddLog("Installed: "_i18n + name, LogKind::Success);
                }
            }
            else if (cancelled) AddLog("Cancelled: "_i18n + name, LogKind::Warning);
            else {
                AddLog("Failed: "_i18n + name + " (" + ResultText(result) + ")", LogKind::Error);
                AddError(name, "Install"_i18n, result);
            }
            if (cancelled) {
                m_cancel_requested = true;
                break;
            }
        }

        {
            SCOPED_MUTEX(&m_mutex);
            m_stats.elapsed_ns = m_session_timestamp.GetNs();
        }
        if (m_cancel_requested) {
            AddLog("Session cancelled; completed installs were kept."_i18n, LogKind::Warning);
            m_state = State::Cancelled;
        } else {
            AddLog("Queue finished."_i18n, LogKind::Event);
            m_state = State::Summary;
        }
        m_actions_dirty = true;

        while ((m_state == State::Summary || m_state == State::Cancelled) &&
               !m_cancel_requested && !GetToken().stop_requested()) {
            svcSleepThread(1e+6);
        }
        if (m_cancel_requested || GetToken().stop_requested()) return;
    }
}
} // namespace sphaira::ui::menu::dbi

#endif