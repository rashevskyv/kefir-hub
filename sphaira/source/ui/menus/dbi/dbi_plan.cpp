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
void Menu::StartInstall() {
    s64 sd_required{}, nand_required{};
    size_t count{};
    {
        SCOPED_MUTEX(&m_mutex);
        for (const auto& entry : m_queue) {
            if (!entry.selected || R_FAILED(entry.analysis_result)) continue;
            count++;
        }
        if (!count && m_index >= 0 && m_index < static_cast<s64>(m_queue.size()) && R_SUCCEEDED(m_queue[m_index].analysis_result)) {
            m_queue[m_index].selected = true;
            count = 1;
        }
        RecomputePlan();
        for (const auto& entry : m_queue) {
            if (!entry.selected || R_FAILED(entry.analysis_result)) continue;
            if (!entry.analysis_deferred) {
                const u64 title_id = GetQueueEntryTitleId(entry);
                if (!IsTitleAlreadyInstalled(title_id)) {
                    AddSizeSaturated(entry.planned_sd ? sd_required : nand_required, entry.analysis.install_size);
                }
            }
        }
    }
    if (!count) {
        App::Notify("Select at least one package"_i18n);
        return;
    }
    const auto spaces = GetPolledData(true);
    const bool global = App::GetSaveSettingsGlobally();
    const auto reserve_nand = static_cast<s64>(global ? App::GetInstallReserveMb() : m_session_reserve_mb) * 1024 * 1024;
    const auto reserve_sd = static_cast<s64>(global ? App::GetInstallReserveSdMb() : m_session_reserve_sd_mb) * 1024 * 1024;
    if ((sd_required && spaces.sd_free - sd_required < reserve_sd) ||
        (nand_required && spaces.nand_free - nand_required < reserve_nand)) {
        App::Push<OptionBox>("Selected packages may not fit after the configured reserve. Continue?"_i18n,
            "Cancel"_i18n, "Install selected"_i18n, 0, [this](auto choice) {
                if (choice && *choice == 1) {
                    ConfirmInstallPlan();
                }
            });
        return;
    }
    ConfirmInstallPlan();
}

void Menu::RecomputePlan(bool force_refresh) {
    const auto spaces = GetPolledData(force_refresh);
    const bool global = App::GetSaveSettingsGlobally();
    const auto reserve_nand = static_cast<s64>(global ? App::GetInstallReserveMb() : m_session_reserve_mb) * 1024 * 1024;
    const auto reserve_sd = static_cast<s64>(global ? App::GetInstallReserveSdMb() : m_session_reserve_sd_mb) * 1024 * 1024;
    const long loc = global ? App::GetInstallLocation() : m_session_install_location;

    s64 free_nand = std::max<s64>(0, spaces.nand_free - reserve_nand);
    s64 free_sd = std::max<s64>(0, spaces.sd_free - reserve_sd);

    auto plannable = [](const QueueEntry& e) {
        return e.selected && R_SUCCEEDED(e.analysis_result)
            && !IsTitleAlreadyInstalled(GetQueueEntryTitleId(e));
    };

    // packages pinned to a target claim their space first, fit or not; the Auto
    // ones then pack into whatever budget is left.
    for (auto& e : m_queue) {
        if (!plannable(e) || e.target == InstallTarget::Auto) continue;
        e.planned_sd = e.target == InstallTarget::Sd;
        PlanTake(e.planned_sd ? free_sd : free_nand, PlanSize(e));
    }

    // ponytail: greedy in queue order, not best-fit -- packing order follows the
    // list the user is looking at; swap in a decreasing-size pass if it matters.
    for (auto& e : m_queue) {
        if (!plannable(e) || e.target != InstallTarget::Auto) continue;
        const auto size = PlanSize(e);
        e.planned_sd = PlanPickSd(loc, size, free_sd, free_nand);
        PlanTake(e.planned_sd ? free_sd : free_nand, size);
    }
}

bool Menu::RefreshAutoInstallTarget(size_t index) {
    InstallTarget target{InstallTarget::Auto};
    s64 size{};
    bool current_sd{};
    bool selected{};
    {
        SCOPED_MUTEX(&m_mutex);
        if (index >= m_queue.size()) return false;
        const auto& e = m_queue[index];
        selected = e.install_selected;
        target = e.target;
        size = PlanSize(e);
        current_sd = e.install_sd;
    }
    if (!selected || target != InstallTarget::Auto) {
        return current_sd;
    }

    const auto spaces = GetPolledData(true);
    const bool global = App::GetSaveSettingsGlobally();
    const auto reserve_nand = static_cast<s64>(global ? App::GetInstallReserveMb() : m_session_reserve_mb) * 1024 * 1024;
    const auto reserve_sd = static_cast<s64>(global ? App::GetInstallReserveSdMb() : m_session_reserve_sd_mb) * 1024 * 1024;
    const long loc = global ? App::GetInstallLocation() : m_session_install_location;
    const s64 usable_nand = std::max<s64>(0, spaces.nand_free - reserve_nand);
    const s64 usable_sd = std::max<s64>(0, spaces.sd_free - reserve_sd);

    const auto cand = PlanEvaluateCandidate(loc, size, usable_sd, usable_nand);
    const bool pick = cand.fits ? cand.is_sd : PlanPickSd(loc, size, usable_sd, usable_nand);

    {
        SCOPED_MUTEX(&m_mutex);
        if (index >= m_queue.size()) return pick;
        m_queue[index].install_sd = pick;
        m_queue[index].planned_sd = pick;
    }
    return pick;
}

bool Menu::ApplyLiveSelection(const std::unordered_map<std::string, bool>& selections, const std::unordered_map<std::string, int>& targets) {
    bool changed = false;
    {
        SCOPED_MUTEX(&m_mutex);
        for (auto& entry : m_queue) {
            if (entry.selected) {
                auto it = selections.find(entry.file_name);
                if (it != selections.end() && !it->second) {
                    entry.selected = false;
                    changed = true;
                }
            }
            auto tit = targets.find(entry.file_name);
            if (tit != targets.end()) {
                InstallTarget new_target = InstallTarget::Auto;
                if (tit->second == 1) new_target = InstallTarget::Sd;
                else if (tit->second == 2) new_target = InstallTarget::Nand;
                if (entry.target != new_target) {
                    entry.target = new_target;
                    changed = true;
                }
            }
        }
    }

    std::vector<std::string> new_names;
    {
        SCOPED_MUTEX(&m_mutex);
        for (const auto& [name, selected] : selections) {
            if (!selected) continue;
            bool exists = false;
            for (const auto& entry : m_queue) {
                if (entry.file_name == name) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                new_names.push_back(name);
            }
        }
    }

    for (const auto& name : new_names) {
        QueueEntry entry{};
        entry.file_name = name;
        if (m_usb_source) {
            m_usb_source->SetFileNameForTranfser(name);
            entry.analysis_result = yati::AnalyzeSource(m_usb_source.get(), fs::FsPath{name}, entry.analysis);
            s64 pc_size = m_usb_source->GetFileSize(name);
            if (pc_size > 0) {
                entry.analysis.source_size = pc_size;
            }
        }
        int pc_target = 0;
        auto tit = targets.find(name);
        if (tit != targets.end()) {
            pc_target = tit->second;
        } else if (m_usb_source) {
            pc_target = m_usb_source->GetFileTarget(name);
        }
        if (pc_target == 1) entry.target = InstallTarget::Sd;
        else if (pc_target == 2) entry.target = InstallTarget::Nand;
        else entry.target = InstallTarget::Auto;

        entry.selected = R_SUCCEEDED(entry.analysis_result);
        if (R_FAILED(entry.analysis_result)) {
            AddError(name, "Analysis"_i18n, entry.analysis_result);
        }
        {
            SCOPED_MUTEX(&m_mutex);
            bool exists = false;
            for (const auto& existing : m_queue) {
                if (existing.file_name == name) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                entry.source_index = m_queue.size();
                m_queue.emplace_back(std::move(entry));
                changed = true;
            }
        }
    }

    if (changed) {
        SCOPED_MUTEX(&m_mutex);
        RecomputePlan();
        m_actions_dirty = true;
    }
    return changed;
}

void Menu::ConfirmInstallPlan() {
    SCOPED_MUTEX(&m_mutex);
    if (m_install_requested) return;
    RecomputePlan();
    m_plan_total_bytes = 0;
    m_plan_done_bytes = 0;
    m_package_write_start = m_total_write.load();
    for (auto& entry : m_queue) {
        entry.install_selected = entry.selected && R_SUCCEEDED(entry.analysis_result);
        entry.install_sd = entry.planned_sd;
        if (entry.install_selected) {
            AddSizeSaturated(m_plan_total_bytes, PlanSize(entry));
        }
    }
    m_skip_requested = false;
    m_install_requested = true;
}
} // namespace sphaira::ui::menu::dbi

#endif