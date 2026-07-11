#if ENABLE_NETWORK_INSTALL

#include "ui/menus/dbi_menu.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "utils/utils.hpp"

#include <algorithm>
#include <cstdio>
#include <ranges>

namespace sphaira::ui::menu::dbi {
namespace {

constexpr u64 CONNECTION_TIMEOUT = UINT64_MAX;
constexpr u64 TRANSFER_TIMEOUT = UINT64_MAX;
constexpr u64 FINISHED_TIMEOUT = 1e+9 * 3;
constexpr size_t MAX_LOG_LINES = 128;

void thread_func(void* user) {
    static_cast<Menu*>(user)->ThreadFunction();
}

auto ResultText(Result rc) -> std::string {
    char out[32]{};
    std::snprintf(out, sizeof(out), "0x%08X", R_VALUE(rc));
    return out;
}

auto IsDbiSessionError(Result rc) -> bool {
    switch (rc) {
        case Result_TransferCancelled:
        case Result_UsbCancelled:
        case Result_UsbBadMagic:
        case Result_UsbBadVersion:
        case Result_UsbBadCount:
        case Result_UsbBadBufferAlign:
        case Result_UsbBadTransferSize:
        case Result_UsbEmptyTransferSize:
        case Result_UsbOverflowTransferSize:
        case Result_UsbDsBadDeviceSpeed:
        case KERNELRESULT(TimedOut):
            return true;
        default:
            return false;
    }
}

void AddSizeSaturated(s64& total, s64 value) {
    total = value > INT64_MAX - total ? INT64_MAX : total + value;
}

} // namespace

Menu::Menu(u32 flags) : MenuBase{"Install queue"_i18n, flags} {
    mutexInit(&m_mutex);
    ueventCreate(&m_cancel_event, false);

    const Vec4 row{70.f, GetY() + 80.f, 1140.f, 82.f};
    m_list = std::make_unique<List>(1, 6, m_pos, row);
    m_list->SetLayout(List::Layout::GRID);
    UpdateActions();

    m_was_mtp_enabled = App::GetMtpEnable();
    if (m_was_mtp_enabled) {
        App::Notify("Disable MTP for usb install"_i18n);
        App::SetMtpEnable(false);
    }

    m_usb_source = std::make_unique<yati::source::DbiUsb>(TRANSFER_TIMEOUT);
    if (R_FAILED(m_usb_source->GetOpenResult())) {
        m_state = State::Failed;
        m_actions_dirty = true;
        return;
    }

    const auto rc = threadCreate(&m_thread, thread_func, this, nullptr, 1024 * 64, PRIO_PREEMPTIVE, 1);
    if (R_SUCCEEDED(rc) && R_SUCCEEDED(threadStart(&m_thread))) {
        m_thread_created = true;
    } else {
        m_state = State::Failed;
        m_actions_dirty = true;
    }
}

Menu::~Menu() {
    m_cancel_requested = true;
    m_stop_source.request_stop();
    ueventSignal(&m_cancel_event);
    if (m_usb_source) {
        m_usb_source->SignalCancel();
    }
    if (m_thread_created) {
        threadWaitForExit(&m_thread);
        threadClose(&m_thread);
    }
    m_usb_source.reset();

    if (m_was_mtp_enabled) {
        App::Notify("Re-enabled MTP"_i18n);
        App::SetMtpEnable(true);
    }
}

void Menu::UpdateActions() {
    RemoveActions();
    const auto state = m_state.load();
    if (state == State::ReviewQueue) {
        SetActions(
            std::make_pair(Button::A, Action{"Select package"_i18n, [this]() {
                SCOPED_MUTEX(&m_mutex);
                if (m_index >= 0 && m_index < static_cast<s64>(m_queue.size()) && R_SUCCEEDED(m_queue[m_index].analysis_result)) {
                    m_queue[m_index].selected = !m_queue[m_index].selected;
                }
            }}),
            std::make_pair(Button::X, Action{"Select all / none"_i18n, [this]() {
                SCOPED_MUTEX(&m_mutex);
                const bool any_unselected = std::ranges::any_of(m_queue, [](const auto& entry) {
                    return R_SUCCEEDED(entry.analysis_result) && !entry.selected;
                });
                for (auto& entry : m_queue) {
                    if (R_SUCCEEDED(entry.analysis_result)) entry.selected = any_unselected;
                }
            }}),
            std::make_pair(Button::Y, Action{"Install target"_i18n, [this]() { CycleTarget(); }}),
            std::make_pair(Button::START, Action{"Install selected"_i18n, [this]() { StartInstall(); }}),
            std::make_pair(Button::B, Action{"Cancel session"_i18n, [this]() { CancelSession(); }})
        );
    } else if (state == State::Installing) {
        SetAction(Button::B, Action{"Cancel remaining"_i18n, [this]() { CancelSession(); }});
    } else if (state == State::Summary || state == State::Cancelled || state == State::Failed) {
        SetAction(Button::B, Action{"Back"_i18n, [this]() { SetPop(); }});
    } else {
        SetAction(Button::B, Action{"Cancel session"_i18n, [this]() { CancelSession(); }});
    }
    m_actions_dirty = false;
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    if (m_actions_dirty) UpdateActions();
    MenuBase::Update(controller, touch);

    const auto state = m_state.load();
    SCOPED_MUTEX(&m_mutex);
    if (state == State::ReviewQueue && !m_queue.empty()) {
        m_list->OnUpdate(controller, touch, m_index, m_queue.size(), [this](bool pressed, s64 index) {
            if (pressed && m_index == index && R_SUCCEEDED(m_queue[index].analysis_result)) {
                m_queue[index].selected = !m_queue[index].selected;
            } else {
                m_index = index;
            }
        }, this);
    } else if ((state == State::Installing || state == State::Summary || state == State::Cancelled) && !m_log.empty()) {
        m_list->OnUpdate(controller, touch, m_log_index, m_log.size(), [this](bool, s64 index) {
            m_log_index = index;
        }, this);
    }
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);
    const auto state = m_state.load();

    if (state == State::WaitingForList || state == State::Analysing) {
        const auto text = state == State::WaitingForList
            ? "Waiting for DBI package list..."_i18n
            : "Analysing packages (nothing is being installed)..."_i18n;
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 30.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), text.c_str());
        return;
    }
    if (state == State::Failed) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 28.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_ERROR), "DBI session failed"_i18n.c_str());
        return;
    }

    SCOPED_MUTEX(&m_mutex);
    if (state == State::ReviewQueue) {
        s64 selected_size{};
        size_t selected_count{};
        for (const auto& entry : m_queue) {
            if (entry.selected && R_SUCCEEDED(entry.analysis_result)) {
                AddSizeSaturated(selected_size, entry.analysis.install_size);
                selected_count++;
            }
        }
        const auto spaces = GetPolledData();
        const auto reserve = App::GetInstallReserveMb() * 1024ULL * 1024ULL;
        gfx::drawTextArgs(vg, 70.f, GetY() + 10.f, 17.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
            theme->GetColour(ThemeEntryID_TEXT_INFO), "%s: %s    %s: %zu / %zu    %s: %s",
            "Target"_i18n.c_str(), TargetName().c_str(), "Selected"_i18n.c_str(), selected_count, m_queue.size(),
            "Required"_i18n.c_str(), utils::formatSizeStorage(selected_size).c_str());
        gfx::drawTextArgs(vg, 70.f, GetY() + 36.f, 15.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
            theme->GetColour(ThemeEntryID_TEXT_INFO), "%s: %s    %s: %s    %s: %s",
            "microSD free"_i18n.c_str(), utils::formatSizeStorage(std::max<s64>(0, spaces.sd_free - reserve)).c_str(),
            "System memory free"_i18n.c_str(), utils::formatSizeStorage(std::max<s64>(0, spaces.nand_free - reserve)).c_str(),
            "Reserve"_i18n.c_str(), utils::formatSizeStorage(reserve).c_str());

        m_list->Draw(vg, theme, m_queue.size(), [this](NVGcontext* vg, Theme* theme, Vec4 v, s64 index) {
            const auto& entry = m_queue[index];
            if (index == m_index) gfx::drawRectOutline(vg, theme, 4.f, v);
            const auto colour = R_FAILED(entry.analysis_result) ? theme->GetColour(ThemeEntryID_ERROR) : theme->GetColour(ThemeEntryID_TEXT);
            const char* mark = !R_SUCCEEDED(entry.analysis_result) ? "!" : (entry.selected ? "[x]" : "[ ]");
            gfx::drawTextArgs(vg, v.x + 12.f, v.y + 8.f, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, colour,
                "%s %s", mark, entry.file_name.c_str());
            if (R_FAILED(entry.analysis_result)) {
                gfx::drawTextArgs(vg, v.x + 42.f, v.y + 40.f, 14.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, colour,
                    "%s: %s", "Analysis failed"_i18n.c_str(), ResultText(entry.analysis_result).c_str());
            } else {
                const auto kind = entry.analysis.size_kind == yati::AnalysisSizeKind::Exact ? "Exact"_i18n : "Estimate"_i18n;
                const auto target = entry.analysis.suggested_sd ? "microSD"_i18n : "System memory"_i18n;
                gfx::drawTextArgs(vg, v.x + 42.f, v.y + 40.f, 14.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
                    theme->GetColour(ThemeEntryID_TEXT_INFO), "%s: %s    %s: %s (%s)    Auto: %s",
                    "Package size"_i18n.c_str(), utils::formatSizeStorage(entry.analysis.source_size).c_str(),
                    "Install size"_i18n.c_str(), utils::formatSizeStorage(entry.analysis.install_size).c_str(), kind.c_str(), target.c_str());
            }
        });
        return;
    }

    gfx::drawTextArgs(vg, 70.f, GetY() + 10.f, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
        theme->GetColour(ThemeEntryID_TEXT_INFO), "%s %zu/%zu    %s: %zu    %s: %zu",
        "Package"_i18n.c_str(), std::min(m_current_package + 1, m_queue.size()), m_queue.size(),
        "Installed"_i18n.c_str(), m_success_count, "Failed"_i18n.c_str(), m_failure_count);
    if (!m_current_title.empty()) {
        gfx::drawTextArgs(vg, 70.f, GetY() + 38.f, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
            theme->GetColour(ThemeEntryID_TEXT), "%s", m_current_title.c_str());
    }
    if (m_progress_size > 0) {
        const Vec4 bar{70.f, GetY() + 67.f, 1140.f, 12.f};
        gfx::drawRect(vg, bar, theme->GetColour(ThemeEntryID_PROGRESSBAR_BACKGROUND), 3.f);
        gfx::drawRect(vg, bar.x, bar.y, bar.w * std::clamp<double>((double)m_progress_offset / m_progress_size, 0.0, 1.0), bar.h,
            theme->GetColour(ThemeEntryID_PROGRESSBAR), 3.f);
    }
    m_list->Draw(vg, theme, m_log.size(), [this](NVGcontext* vg, Theme* theme, Vec4 v, s64 index) {
        if (index == m_log_index) gfx::drawRectOutline(vg, theme, 2.f, v);
        gfx::drawTextArgs(vg, v.x + 10.f, v.y + 10.f, 15.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
            theme->GetColour(ThemeEntryID_TEXT), "%s", m_log[index].c_str());
    });
}

void Menu::ThreadFunction() {
    for (;;) {
        if (GetToken().stop_requested()) return;
        const auto rc = m_usb_source->IsUsbConnected(CONNECTION_TIMEOUT);
        if (rc == Result_UsbCancelled) return;
        if (R_FAILED(rc)) continue;

        std::vector<std::string> names;
        const auto list_rc = m_usb_source->WaitForConnection(CONNECTION_TIMEOUT, names);
        if (R_FAILED(list_rc)) continue;

        m_state = State::Analysing;
        m_actions_dirty = true;
        for (const auto& name : names) {
            if (m_cancel_requested || GetToken().stop_requested()) break;
            QueueEntry entry{};
            entry.file_name = name;
            m_usb_source->SetFileNameForTranfser(name);
            entry.analysis_result = yati::AnalyzeSource(m_usb_source.get(), fs::FsPath{name}, entry.analysis);
            entry.selected = R_SUCCEEDED(entry.analysis_result);
            SCOPED_MUTEX(&m_mutex);
            m_queue.emplace_back(std::move(entry));
        }

        if (m_cancel_requested || GetToken().stop_requested()) {
            m_usb_source->Finished(FINISHED_TIMEOUT);
            m_state = State::Cancelled;
            m_actions_dirty = true;
            return;
        }

        m_state = State::ReviewQueue;
        m_actions_dirty = true;
        while (!m_install_requested && !m_cancel_requested && !GetToken().stop_requested()) svcSleepThread(1e+6);
        if (m_cancel_requested || GetToken().stop_requested()) {
            m_usb_source->Finished(FINISHED_TIMEOUT);
            m_state = State::Cancelled;
            m_actions_dirty = true;
            return;
        }

        m_state = State::Installing;
        m_actions_dirty = true;
        InstallTarget install_target{};
        {
            SCOPED_MUTEX(&m_mutex);
            install_target = m_install_target;
        }
        bool session_failed{};
        for (size_t i = 0; i < m_queue.size(); i++) {
            bool selected{};
            yati::InstallAnalysis analysis{};
            std::string name{};
            {
                SCOPED_MUTEX(&m_mutex);
                selected = m_queue[i].selected && R_SUCCEEDED(m_queue[i].analysis_result);
                analysis = m_queue[i].analysis;
                name = m_queue[i].file_name;
                m_current_package = i;
                m_current_title = name;
                m_progress_offset = 0;
                m_progress_size = 0;
            }
            if (!selected) continue;
            if (m_cancel_requested) break;

            AddLog("Starting: "_i18n + name);
            m_usb_source->SetFileNameForTranfser(name);
            yati::ConfigOverride override{};
            override.sd_card_install = install_target == InstallTarget::Sd ? true
                : install_target == InstallTarget::Nand ? false : analysis.suggested_sd;
            const auto install_rc = yati::InstallFromCollections(this, m_usb_source.get(), analysis.collections, override);
            const bool cancelled = m_cancel_requested || install_rc == Result_TransferCancelled || install_rc == Result_UsbCancelled;
            const bool fatal_session_error = R_FAILED(install_rc) && IsDbiSessionError(install_rc);
            {
                SCOPED_MUTEX(&m_mutex);
                m_queue[i].install_result = install_rc;
                m_queue[i].installed = R_SUCCEEDED(install_rc);
                if (R_SUCCEEDED(install_rc)) m_success_count++;
                else if (!cancelled) m_failure_count++;
            }
            if (R_SUCCEEDED(install_rc)) AddLog("Installed: "_i18n + name);
            else if (cancelled) AddLog("Cancelled: "_i18n + name);
            else AddLog("Failed: "_i18n + name + " (" + ResultText(install_rc) + ")");
            if (cancelled) {
                m_cancel_requested = true;
                break;
            }
            if (fatal_session_error) {
                session_failed = true;
                AddLog("DBI session failed; remaining packages were skipped."_i18n);
                break;
            }
        }

        if (!session_failed) m_usb_source->Finished(FINISHED_TIMEOUT);
        if (m_cancel_requested) {
            AddLog("Session cancelled; completed installs were kept."_i18n);
            m_state = State::Cancelled;
        } else if (session_failed) {
            // Keep the completed-package history visible even though the USB
            // session itself cannot safely continue.
            m_state = State::Summary;
        } else {
            AddLog("Queue finished."_i18n);
            m_state = State::Summary;
        }
        m_actions_dirty = true;
        return;
    }
}

void Menu::StartInstall() {
    s64 sd_required{}, nand_required{};
    size_t count{};
    {
        SCOPED_MUTEX(&m_mutex);
        for (const auto& entry : m_queue) {
            if (!entry.selected || R_FAILED(entry.analysis_result)) continue;
            count++;
            const bool sd = m_target == InstallTarget::Sd || (m_target == InstallTarget::Auto && entry.analysis.suggested_sd);
            AddSizeSaturated(sd ? sd_required : nand_required, entry.analysis.install_size);
        }
    }
    if (!count) {
        App::Notify("Select at least one package"_i18n);
        return;
    }
    const auto spaces = GetPolledData(true);
    const auto reserve = App::GetInstallReserveMb() * 1024ULL * 1024ULL;
    if ((sd_required && spaces.sd_free - sd_required < static_cast<s64>(reserve)) ||
        (nand_required && spaces.nand_free - nand_required < static_cast<s64>(reserve))) {
        App::Push<OptionBox>("Selected packages may not fit after the configured reserve. Continue?"_i18n,
            "Cancel"_i18n, "Install selected"_i18n, 0, [this](auto choice) {
                if (choice && *choice == 1) {
                    SCOPED_MUTEX(&m_mutex);
                    m_install_target = m_target;
                    m_install_requested = true;
                }
            });
        return;
    }
    {
        SCOPED_MUTEX(&m_mutex);
        m_install_target = m_target;
        m_install_requested = true;
    }
}

void Menu::CancelSession() {
    m_cancel_requested = true;
    ueventSignal(&m_cancel_event);
    const auto state = m_state.load();
    if (state == State::WaitingForList || state == State::Analysing || state == State::Installing) {
        m_usb_source->SignalCancel();
    }
    if (state == State::WaitingForList) {
        m_state = State::Cancelled;
        m_actions_dirty = true;
    }
    AddLog("Cancellation requested."_i18n);
}

void Menu::CycleTarget() {
    SCOPED_MUTEX(&m_mutex);
    m_target = m_target == InstallTarget::Auto ? InstallTarget::Sd
        : m_target == InstallTarget::Sd ? InstallTarget::Nand : InstallTarget::Auto;
}

auto Menu::TargetName() const -> std::string {
    if (m_target == InstallTarget::Sd) return "microSD"_i18n;
    if (m_target == InstallTarget::Nand) return "System memory"_i18n;
    return "Auto"_i18n;
}

void Menu::AddLog(const std::string& text) {
    SCOPED_MUTEX(&m_mutex);
    if (m_log.size() == MAX_LOG_LINES) m_log.erase(m_log.begin());
    m_log.emplace_back(text);
    m_log_index = m_log.size() - 1;
}

Result Menu::CheckCancelled() {
    R_UNLESS(!m_cancel_requested && !GetToken().stop_requested(), Result_TransferCancelled);
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
}

void Menu::UpdateInstallTransfer(s64 offset, s64 size) {
    SCOPED_MUTEX(&m_mutex);
    m_progress_offset = offset;
    m_progress_size = size;
}

void Menu::InstallYield() {
    svcSleepThread(1e+6);
}

} // namespace sphaira::ui::menu::dbi

#endif
