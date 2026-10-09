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
#include <unordered_map>
#include <unordered_set>

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
                if (!TakesNoSpace(entry)) {
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

    auto plannable = [this](const QueueEntry& e) {
        return e.selected && R_SUCCEEDED(e.analysis_result) && !TakesNoSpace(e);
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

long Menu::SkipMode() const {
    const auto pc = PcSkipMode(m_usb_source ? m_usb_source->GetPcSkipField() : 0);
    if (pc >= 0) return pc;
    return App::GetSaveSettingsGlobally() ? App::GetApp()->m_skip_if_already_installed.Get() : m_session_skip_if_already_installed;
}

bool Menu::TakesNoSpace(const QueueEntry& entry) const {
    return SkipMode() != 0 && IsEntryAlreadyInstalled(entry);
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

bool Menu::ApplyLiveQueue(const std::vector<yati::source::Usb::LiveQueueItem>& items, size_t active_index, bool is_installing) {
    bool changed = false;

    if (!is_installing) {
        // ReviewQueue mode: entire queue can be reordered, items added, removed, target/selected updated
        std::unordered_map<std::string, QueueEntry> existing_map;
        {
            SCOPED_MUTEX(&m_mutex);
            for (const auto& entry : m_queue) {
                existing_map.emplace(entry.file_name, entry);
            }
        }

        std::vector<QueueEntry> new_queue;
        new_queue.reserve(items.size());

        for (const auto& item : items) {
            auto it = existing_map.find(item.name);
            if (it != existing_map.end()) {
                QueueEntry entry = std::move(it->second);
                existing_map.erase(it);

                // a tick or target changed on the console keeps its value until the PC
                // has been told (SendQueuePlanIfChanged); otherwise this poll would
                // put the tick straight back.
                if (!entry.sync_dirty) {
                    bool new_sel = item.selected && R_SUCCEEDED(entry.analysis_result);
                    if (entry.selected != new_sel) {
                        entry.selected = new_sel;
                        changed = true;
                    }

                    InstallTarget new_target = (item.target == 1) ? InstallTarget::Sd : ((item.target == 2) ? InstallTarget::Nand : InstallTarget::Auto);
                    if (entry.target != new_target) {
                        entry.target = new_target;
                        changed = true;
                    }
                }
                new_queue.emplace_back(std::move(entry));
            } else {
                // New item added from host
                QueueEntry entry{};
                entry.file_name = item.name;
                if (m_usb_source) {
                    m_usb_source->SetFileNameForTranfser(item.name);
                    entry.analysis_result = yati::AnalyzeSource(m_usb_source.get(), fs::FsPath{item.name}, entry.analysis);
                    s64 pc_size = m_usb_source->GetFileSize(item.name);
                    if (pc_size > 0) {
                        entry.analysis.source_size = pc_size;
                    }
                }
                entry.target = (item.target == 1) ? InstallTarget::Sd : ((item.target == 2) ? InstallTarget::Nand : InstallTarget::Auto);
                entry.selected = item.selected && R_SUCCEEDED(entry.analysis_result);
                if (R_FAILED(entry.analysis_result)) {
                    AddError(item.name, "Analysis"_i18n, entry.analysis_result);
                }
                new_queue.emplace_back(std::move(entry));
                changed = true;
            }
        }

        if (!existing_map.empty()) {
            changed = true;
        }

        {
            SCOPED_MUTEX(&m_mutex);
            m_queue = std::move(new_queue);
            for (size_t i = 0; i < m_queue.size(); i++) {
                m_queue[i].source_index = i;
            }
            RecomputePlan();
            m_actions_dirty = true;
        }
        // the wire order is the PC's; a sort the user chose on the console is
        // re-applied on top of it, or every poll would undo it.
        if (m_session_sort_type != 0) {
            SortQueue();
        }
        return changed;
    }

    // Installing mode: indices 0..active_index are frozen (completed or active package).
    // Future packages (after active_index) follow wire order, can be added or removed.
    std::unordered_set<std::string> frozen_names;
    std::unordered_map<std::string, QueueEntry> future_map;
    {
        SCOPED_MUTEX(&m_mutex);
        if (active_index >= m_queue.size()) {
            return false;
        }
        for (size_t k = 0; k <= active_index; k++) {
            frozen_names.insert(m_queue[k].file_name);
        }
        for (size_t k = active_index + 1; k < m_queue.size(); k++) {
            future_map.emplace(m_queue[k].file_name, m_queue[k]);
        }
    }

    std::vector<QueueEntry> new_future;
    for (const auto& item : items) {
        if (frozen_names.contains(item.name)) {
            continue; // Frozen prefix stays intact
        }
        auto it = future_map.find(item.name);
        if (it != future_map.end()) {
            QueueEntry entry = std::move(it->second);
            future_map.erase(it);

            if (entry.selected != item.selected) {
                entry.selected = item.selected;
                if (!item.selected) {
                    entry.install_selected = false;
                }
                changed = true;
            }

            InstallTarget new_target = (item.target == 1) ? InstallTarget::Sd : ((item.target == 2) ? InstallTarget::Nand : InstallTarget::Auto);
            if (entry.target != new_target) {
                entry.target = new_target;
                entry.planned_sd = (new_target == InstallTarget::Sd);
                entry.install_sd = entry.planned_sd;
                changed = true;
            }
            new_future.emplace_back(std::move(entry));
        } else {
            QueueEntry entry{};
            entry.file_name = item.name;
            entry.analysis_deferred = true;
            entry.analysis_result = 0;
            s64 sz = item.size;
            if (sz <= 0 && m_usb_source) {
                sz = m_usb_source->GetFileSize(item.name);
            }
            entry.source_size = sz;
            entry.analysis.source_size = sz;
            entry.target = (item.target == 1) ? InstallTarget::Sd : ((item.target == 2) ? InstallTarget::Nand : InstallTarget::Auto);
            entry.selected = item.selected;
            entry.install_selected = item.selected;
            entry.planned_sd = (entry.target == InstallTarget::Sd);
            entry.install_sd = entry.planned_sd;
            new_future.emplace_back(std::move(entry));
            changed = true;
        }
    }

    if (!future_map.empty()) {
        changed = true;
    }

    {
        SCOPED_MUTEX(&m_mutex);
        if (active_index >= m_queue.size()) {
            return false;
        }
        m_queue.resize(active_index + 1);
        for (auto& entry : new_future) {
            entry.source_index = m_queue.size();
            m_queue.emplace_back(std::move(entry));
        }

        const auto spaces = GetPolledData();
        const bool global = App::GetSaveSettingsGlobally();
        const auto reserve_nand = static_cast<s64>(global ? App::GetInstallReserveMb() : m_session_reserve_mb) * 1024 * 1024;
        const auto reserve_sd = static_cast<s64>(global ? App::GetInstallReserveSdMb() : m_session_reserve_sd_mb) * 1024 * 1024;
        const long loc = global ? App::GetInstallLocation() : m_session_install_location;

        s64 avail_nand = std::max<s64>(0, spaces.nand_free - reserve_nand);
        s64 avail_sd = std::max<s64>(0, spaces.sd_free - reserve_sd);

        if (m_queue[active_index].install_selected && !TakesNoSpace(m_queue[active_index])) {
            const auto asize = PlanSize(m_queue[active_index]);
            if (m_queue[active_index].install_sd) {
                PlanTake(avail_sd, asize);
            } else {
                PlanTake(avail_nand, asize);
            }
        }

        for (size_t j = active_index + 1; j < m_queue.size(); j++) {
            auto& entry = m_queue[j];
            if (!entry.selected || R_FAILED(entry.analysis_result)) {
                entry.install_selected = false;
                continue;
            }
            if (entry.target == InstallTarget::Sd) {
                entry.planned_sd = true;
                entry.install_sd = true;
                entry.install_selected = true;
                PlanTake(avail_sd, PlanSize(entry));
            } else if (entry.target == InstallTarget::Nand) {
                entry.planned_sd = false;
                entry.install_sd = false;
                entry.install_selected = true;
                PlanTake(avail_nand, PlanSize(entry));
            } else if (TakesNoSpace(entry)) {
                entry.planned_sd = PlanPickSd(loc, PlanSize(entry), avail_sd, avail_nand);
                entry.install_sd = entry.planned_sd;
                entry.install_selected = true;
            } else {
                const auto size = PlanSize(entry);
                const auto cand = PlanEvaluateCandidate(loc, size, avail_sd, avail_nand);
                if (cand.fits) {
                    entry.planned_sd = cand.is_sd;
                    entry.install_sd = cand.is_sd;
                    entry.install_selected = true;
                    PlanTake(cand.is_sd ? avail_sd : avail_nand, size);
                } else {
                    entry.planned_sd = cand.is_sd;
                    entry.install_sd = cand.is_sd;
                    entry.install_selected = false;
                    entry.selected = false;
                    entry.rejected_no_space = true;
                }
            }
        }

        m_plan_total_bytes = m_plan_done_bytes;
        for (size_t j = active_index; j < m_queue.size(); j++) {
            if (m_queue[j].install_selected) {
                AddSizeSaturated(m_plan_total_bytes, PlanSize(m_queue[j]));
            }
        }
        m_actions_dirty = true;
    }

    return changed;
}

bool Menu::ApplyLiveSelection(const std::unordered_map<std::string, bool>& selections, const std::unordered_map<std::string, int>& targets) {
    std::vector<yati::source::Usb::LiveQueueItem> items;
    items.reserve(selections.size());
    for (const auto& [name, sel] : selections) {
        int tgt = 0;
        auto tit = targets.find(name);
        if (tit != targets.end()) tgt = tit->second;
        items.push_back({name, 0, sel, tgt});
    }
    return ApplyLiveQueue(items, 0, false);
}

void Menu::SendQueuePlanIfChanged() {
    if (!m_usb_source || !m_usb_source->HasSelectionSync()) {
        return;
    }

    std::vector<yati::source::Usb::QueuePlanItem> items;
    std::string digest;
    bool dirty = false;
    {
        SCOPED_MUTEX(&m_mutex);
        items.reserve(m_queue.size());
        for (const auto& e : m_queue) {
            const bool ok = R_SUCCEEDED(e.analysis_result);
            yati::source::Usb::QueuePlanItem it{
                .name = e.file_name,
                .selected = e.selected,
                .target = e.target == InstallTarget::Sd ? 1 : e.target == InstallTarget::Nand ? 2 : 0,
                .planned_sd = e.target == InstallTarget::Auto ? e.planned_sd : e.target == InstallTarget::Sd,
                .analysis_ok = ok,
                .already_installed = ok && IsEntryAlreadyInstalled(e),
                .no_space = ok && TakesNoSpace(e),
                .no_base = ok && IsBaseMissing(e),
                .install_size = static_cast<u64>(std::max<s64>(0, PlanSize(e))),
            };
            digest += it.name + '|' + std::to_string(it.selected) + std::to_string(it.target) + std::to_string(it.planned_sd)
                + std::to_string(it.analysis_ok) + std::to_string(it.already_installed) + std::to_string(it.no_space) + std::to_string(it.no_base) + '|' + std::to_string(it.install_size) + '\n';
            dirty |= e.sync_dirty;
            items.push_back(std::move(it));
        }
    }
    digest += std::to_string(m_last_acked_revision);
    if (!dirty && digest == m_sent_plan_digest) {
        return;
    }

    if (R_FAILED(m_usb_source->SendQueuePlan(items, m_last_acked_revision))) {
        return;
    }
    m_sent_plan_digest = digest;
    SCOPED_MUTEX(&m_mutex);
    for (auto& e : m_queue) {
        e.sync_dirty = false;
    }
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