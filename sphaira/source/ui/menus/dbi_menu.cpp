#if ENABLE_NETWORK_INSTALL

#include "ui/menus/dbi_menu.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "utils/devoptab_curl_thread.hpp"
#include "utils/utils.hpp"
#include "yati/source/file.hpp"

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

void DrawCheckbox(NVGcontext* vg, Theme* theme, const Vec4& row, bool selected) {
    constexpr float box_size = 20.f;
    const float box_x = row.x + 12.f;
    const float box_y = row.y + (row.h - box_size) / 2.f;

    nvgBeginPath(vg);
    nvgRect(vg, box_x, box_y, box_size, box_size);
    nvgFillColor(vg, theme->GetColour(ThemeEntryID_BACKGROUND));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRect(vg, box_x, box_y, box_size, box_size);
    nvgStrokeColor(vg, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
    nvgStrokeWidth(vg, 2.f);
    nvgStroke(vg);

    if (selected) {
        const float check_x = box_x + box_size / 2.f;
        const float check_y = row.y + (row.h - 18.f) / 2.f;
        const auto outline = nvgRGBA(0, 0, 0, 255);
        gfx::drawText(vg, check_x - 1.f, check_y, 18.f, "\uE14B", nullptr, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, outline);
        gfx::drawText(vg, check_x + 1.f, check_y, 18.f, "\uE14B", nullptr, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, outline);
        gfx::drawText(vg, check_x, check_y - 1.f, 18.f, "\uE14B", nullptr, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, outline);
        gfx::drawText(vg, check_x, check_y + 1.f, 18.f, "\uE14B", nullptr, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, outline);
        gfx::drawText(vg, check_x, check_y, 18.f, "\uE14B", nullptr, NVG_ALIGN_CENTER | NVG_ALIGN_TOP,
            theme->GetColour(ThemeEntryID_TEXT_SELECTED));
    }
}

} // namespace

Menu::Menu(u32 flags) : MenuBase{"Install queue"_i18n, flags} {
    mutexInit(&m_mutex);
    ueventCreate(&m_cancel_event, false);

    const Vec4 queue_pos{70.f, GetY() + 80.f, 1140.f, 500.f};
    const Vec4 row{queue_pos.x, queue_pos.y, queue_pos.w, 82.f};
    m_list = std::make_unique<List>(1, 6, queue_pos, row);
    m_list->SetLayout(List::Layout::GRID);
    const Vec4 log_pos{70.f, GetY() + 100.f, 1140.f, 470.f};
    const Vec4 log_row{log_pos.x, log_pos.y, log_pos.w, 30.f};
    m_log_list = std::make_unique<List>(1, 15, log_pos, log_row);
    m_log_list->SetLayout(List::Layout::GRID);
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

    const Vec4 queue_pos{70.f, GetY() + 80.f, 1140.f, 500.f};
    m_list = std::make_unique<List>(1, 6, queue_pos, Vec4{queue_pos.x, queue_pos.y, queue_pos.w, 82.f});
    m_list->SetLayout(List::Layout::GRID);
    const Vec4 log_pos{70.f, GetY() + 100.f, 1140.f, 470.f};
    m_log_list = std::make_unique<List>(1, 15, log_pos, Vec4{log_pos.x, log_pos.y, log_pos.w, 30.f});
    m_log_list->SetLayout(List::Layout::GRID);
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
            std::make_pair(Button::X, Action{"Select"_i18n, [this]() {
                SCOPED_MUTEX(&m_mutex);
                if (m_install_requested) return;
                if (m_index >= 0 && m_index < static_cast<s64>(m_queue.size()) && R_SUCCEEDED(m_queue[m_index].analysis_result)) {
                    m_queue[m_index].selected = !m_queue[m_index].selected;
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
            std::make_pair(Button::B, Action{"Cancel session"_i18n, [this]() { CancelSession(); }})
        );
    } else if (state == State::Installing) {
        SetAction(Button::B, Action{"Cancel remaining"_i18n, [this]() { CancelSession(); }});
    } else if (state == State::Summary) {
        if (m_session_failed) {
            SetAction(Button::B, Action{"Back"_i18n, [this]() { SetPop(); }});
        } else {
            SetAction(Button::B, Action{"Back"_i18n, [this]() {
                m_state = State::ReviewQueue;
                m_actions_dirty = true;
            }});
        }
    } else if (state == State::Cancelled || state == State::Failed) {
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
    bool activate{};
    if (state == State::ReviewQueue && !m_queue.empty()) {
        {
            SCOPED_MUTEX(&m_mutex);
            m_list->OnUpdate(controller, touch, m_index, m_queue.size(), [this, &activate](bool pressed, s64 index) {
                if (pressed && m_index == index) activate = true;
                else m_index = index;
            }, this);
        }
        if (activate) FireAction(Button::A);
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

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);
    const auto state = m_state.load();

    if (state == State::WaitingForUsb || state == State::WaitingForList || state == State::Analysing) {
        const auto text = state == State::WaitingForUsb
            ? "Waiting for PC connection. Connect the USB cable and make sure the console is detected."_i18n
            : state == State::WaitingForList
                ? "PC connected. Select packages and press Start in DBI Backend."_i18n
                : "Analysing packages (nothing is being installed)..."_i18n;
        gfx::drawTextBox(vg, (SCREEN_WIDTH - 1000.f) / 2.f, SCREEN_HEIGHT / 2.f - 30.f, 30.f, 1000.f,
            theme->GetColour(ThemeEntryID_TEXT_INFO), text.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP, nullptr, 1.3f);
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
            "Targets"_i18n.c_str(), "Per package"_i18n.c_str(), "Selected"_i18n.c_str(), selected_count, m_queue.size(),
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
            if (R_SUCCEEDED(entry.analysis_result)) DrawCheckbox(vg, theme, v, entry.selected);
            gfx::drawTextArgs(vg, v.x + (R_SUCCEEDED(entry.analysis_result) ? 42.f : 12.f), v.y + 8.f, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, colour,
                R_FAILED(entry.analysis_result) ? "! %s" : "%s", entry.file_name.c_str());
            if (R_FAILED(entry.analysis_result)) {
                gfx::drawTextArgs(vg, v.x + 42.f, v.y + 40.f, 14.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, colour,
                    "%s: %s", "Analysis failed"_i18n.c_str(), ResultText(entry.analysis_result).c_str());
            } else if (entry.analysis_deferred) {
                const auto source_size = entry.analysis.source_size > 0
                    ? utils::formatSizeStorage(entry.analysis.source_size) : "Unknown"_i18n;
                gfx::drawTextArgs(vg, v.x + 42.f, v.y + 40.f, 14.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
                    theme->GetColour(ThemeEntryID_TEXT_INFO), "%s: %s    %s: %s    %s: %s",
                    "Package size"_i18n.c_str(), source_size.c_str(),
                    "Install size"_i18n.c_str(), "Calculated during install"_i18n.c_str(),
                    "Target"_i18n.c_str(), TargetName(entry.target).c_str());
            } else {
                const auto kind = entry.analysis.size_kind == yati::AnalysisSizeKind::Exact ? "Exact"_i18n : "Estimate"_i18n;
                const auto target = entry.target == InstallTarget::Auto
                    ? TargetName(entry.target) + " → " + (entry.analysis.suggested_sd ? "microSD"_i18n : "System memory"_i18n)
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

    const double elapsed = m_progress_timestamp.GetSecondsD();
    if (elapsed >= 0.5) {
        m_progress_timestamp.Update();
        const auto bytes = std::max<s64>(0, m_progress_offset - m_progress_last_offset);
        m_progress_last_offset = m_progress_offset;
        const auto current_speed = static_cast<s64>(static_cast<double>(bytes) / elapsed);
        m_progress_speed_samples[m_progress_speed_sample_index] = current_speed;
        m_progress_speed_sample_index = (m_progress_speed_sample_index + 1) % m_progress_speed_samples.size();
        m_progress_speed_sample_count = std::min(m_progress_speed_sample_count + 1, m_progress_speed_samples.size());
        s64 speed_sum{};
        for (size_t i = 0; i < m_progress_speed_sample_count; i++) {
            speed_sum += m_progress_speed_samples[i];
        }
        m_progress_speed = speed_sum / static_cast<s64>(m_progress_speed_sample_count);
    }
    const double speed_mib = static_cast<double>(m_progress_speed) / (1024.0 * 1024.0);
    std::string eta{};
    if (m_progress_speed_sample_count >= 3 && m_progress_speed > 0 && m_progress_size > m_progress_offset) {
        const auto seconds_left = static_cast<size_t>((m_progress_size - m_progress_offset) / m_progress_speed);
        char eta_buf[64]{};
        if (seconds_left >= 3600) {
            std::snprintf(eta_buf, sizeof(eta_buf), "%zuh %zum", seconds_left / 3600, seconds_left % 3600 / 60);
        } else {
            std::snprintf(eta_buf, sizeof(eta_buf), "%zum %zus", seconds_left / 60, seconds_left % 60);
        }
        eta = std::string{"    "} + "Remaining"_i18n + ": " + eta_buf;
    }
    gfx::drawTextArgs(vg, 70.f, GetY() + 10.f, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
        theme->GetColour(ThemeEntryID_TEXT_INFO), "%s %zu/%zu    %s: %zu    %s: %zu    %s: %.2f MiB/s%s",
        "Package"_i18n.c_str(), std::min(m_current_package + 1, m_queue.size()), m_queue.size(),
        "Installed"_i18n.c_str(), m_success_count, "Failed"_i18n.c_str(), m_failure_count,
        "Speed"_i18n.c_str(), speed_mib, eta.c_str());
    if (!m_current_title.empty()) {
        const auto title = m_current_transfer.empty()
            ? m_current_title : m_current_title + " — " + m_current_transfer;
        nvgSave(vg);
        nvgIntersectScissor(vg, 70.f, GetY() + 38.f, 1140.f, 25.f);
        gfx::drawTextArgs(vg, 70.f, GetY() + 38.f, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
            theme->GetColour(ThemeEntryID_TEXT), "%s", title.c_str());
        nvgRestore(vg);
    }
    if (m_progress_size > 0) {
        const Vec4 bar{70.f, GetY() + 67.f, 1140.f, 12.f};
        gfx::drawRect(vg, bar, theme->GetColour(ThemeEntryID_PROGRESSBAR_BACKGROUND), 3.f);
        gfx::drawRect(vg, bar.x, bar.y, bar.w * std::clamp<double>((double)m_progress_offset / m_progress_size, 0.0, 1.0), bar.h,
            theme->GetColour(ThemeEntryID_PROGRESSBAR), 3.f);
    }
    m_log_list->Draw(vg, theme, m_log.size(), [this](NVGcontext* vg, Theme* theme, Vec4 v, s64 index) {
        gfx::drawTextArgs(vg, v.x + 4.f, v.y + 5.f, 15.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
            theme->GetColour(ThemeEntryID_TEXT), "%s", m_log[index].c_str());
    });
}

void Menu::ThreadFunction() {
    if (m_local_fs) {
        LocalThreadFunction();
        return;
    }

    const auto finish_cancelled = [this]() {
        m_state = State::Cancelled;
        m_actions_dirty = true;
    };

    for (;;) {
        m_session_failed = false;
        if (m_cancel_requested || GetToken().stop_requested()) {
            finish_cancelled();
            return;
        }
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
        std::vector<std::string> names;
        const auto list_rc = m_usb_source->WaitForConnection(CONNECTION_TIMEOUT, names);
        if (list_rc == Result_UsbCancelled || m_cancel_requested || GetToken().stop_requested()) {
            finish_cancelled();
            return;
        }
        if (R_FAILED(list_rc)) continue;

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

        // Inner loop for ReviewQueue -> Installing -> Summary/Cancelled cycle
        for (;;) {
            if (m_cancel_requested || GetToken().stop_requested()) {
                break;
            }
            m_state = State::ReviewQueue;
            m_actions_dirty = true;
            m_install_requested = false; // Reset request flag

            while (!m_install_requested && !m_cancel_requested && !GetToken().stop_requested()) svcSleepThread(1e+6);
            if (m_cancel_requested || GetToken().stop_requested()) {
                break;
            }

            m_state = State::Installing;
            m_actions_dirty = true;
            {
                SCOPED_MUTEX(&m_mutex);
                m_success_count = 0;
                m_failure_count = 0;
            }
            bool session_failed{};
            for (size_t i = 0; i < m_queue.size(); i++) {
                bool selected{};
                yati::InstallAnalysis analysis{};
                InstallTarget target{};
                std::string name{};
                {
                    SCOPED_MUTEX(&m_mutex);
                    selected = m_queue[i].install_selected;
                    analysis = m_queue[i].analysis;
                    target = m_queue[i].install_target;
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
                override.sd_card_install = target == InstallTarget::Sd ? true
                    : target == InstallTarget::Nand ? false : analysis.suggested_sd;
                const auto install_rc = yati::InstallFromCollections(this, m_usb_source.get(), analysis.collections, override);
                const bool cancelled = m_cancel_requested || install_rc == Result_TransferCancelled || install_rc == Result_UsbCancelled;
                const bool fatal_session_error = R_FAILED(install_rc) && IsDbiSessionError(install_rc);
                {
                    SCOPED_MUTEX(&m_mutex);
                    m_queue[i].install_result = install_rc;
                    m_queue[i].installed = R_SUCCEEDED(install_rc);
                    if (R_SUCCEEDED(install_rc)) {
                        m_queue[i].selected = false; // Uncheck successfully installed package
                        m_success_count++;
                    }
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
                    m_session_failed = true;
                    AddLog("DBI session failed; remaining packages were skipped."_i18n);
                    break;
                }
            }

            if (m_cancel_requested) {
                AddLog("Session cancelled; completed installs were kept."_i18n);
                m_state = State::Cancelled;
            } else if (session_failed) {
                m_state = State::Summary;
            } else {
                AddLog("Queue finished."_i18n);
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
        if (m_defer_local_analysis) {
            entry.analysis_deferred = true;
            entry.analysis_result = 0;
            if (path_index < m_local_source_sizes.size()) {
                entry.analysis.source_size = std::max<s64>(0, m_local_source_sizes[path_index]);
            }
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
        SCOPED_MUTEX(&m_mutex);
        m_queue.emplace_back(std::move(entry));
    }

    for (;;) {
        if (m_cancel_requested || GetToken().stop_requested()) {
            m_state = State::Cancelled;
            m_actions_dirty = true;
            return;
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
        {
            SCOPED_MUTEX(&m_mutex);
            m_success_count = 0;
            m_failure_count = 0;
        }

        for (size_t i = 0; i < m_queue.size(); i++) {
            bool selected{};
            yati::InstallAnalysis analysis{};
            InstallTarget target{};
            bool analysis_deferred{};
            std::string name{};
            {
                SCOPED_MUTEX(&m_mutex);
                selected = m_queue[i].install_selected;
                analysis = m_queue[i].analysis;
                target = m_queue[i].install_target;
                analysis_deferred = m_queue[i].analysis_deferred;
                name = m_queue[i].file_name;
                m_current_package = i;
                m_current_title = name;
                m_progress_offset = 0;
                m_progress_size = 0;
            }
            if (!selected) continue;
            if (m_cancel_requested) break;

            AddLog("Starting: "_i18n + name);
            yati::ConfigOverride override{};
            if (target == InstallTarget::Sd) {
                override.sd_card_install = true;
            } else if (target == InstallTarget::Nand) {
                override.sd_card_install = false;
            } else if (!analysis_deferred) {
                override.sd_card_install = analysis.suggested_sd;
            }

            Result result{};
            if (analysis_deferred) {
                result = yati::InstallFromFile(this, m_local_fs, m_local_paths[i], override);
            } else {
                yati::source::File source{m_local_fs, m_local_paths[i]};
                const auto open_rc = source.GetOpenResult();
                result = R_SUCCEEDED(open_rc)
                    ? yati::InstallFromCollections(this, &source, analysis.collections, override)
                    : open_rc;
            }
            const bool cancelled = m_cancel_requested || result == Result_TransferCancelled;
            {
                SCOPED_MUTEX(&m_mutex);
                m_queue[i].install_result = result;
                m_queue[i].installed = R_SUCCEEDED(result);
                if (R_SUCCEEDED(result)) {
                    m_queue[i].selected = false;
                    m_success_count++;
                } else if (!cancelled) {
                    m_failure_count++;
                }
            }
            if (R_SUCCEEDED(result)) AddLog("Installed: "_i18n + name);
            else if (cancelled) AddLog("Cancelled: "_i18n + name);
            else AddLog("Failed: "_i18n + name + " (" + ResultText(result) + ")");
            if (cancelled) break;
        }

        if (m_cancel_requested) {
            AddLog("Session cancelled; completed installs were kept."_i18n);
            m_state = State::Cancelled;
        } else {
            AddLog("Queue finished."_i18n);
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

void Menu::StartInstall() {
    s64 sd_required{}, nand_required{};
    size_t count{};
    {
        SCOPED_MUTEX(&m_mutex);
        for (const auto& entry : m_queue) {
            if (!entry.selected || R_FAILED(entry.analysis_result)) continue;
            count++;
            const bool sd = entry.target == InstallTarget::Sd || (entry.target == InstallTarget::Auto && entry.analysis.suggested_sd);
            if (!entry.analysis_deferred) {
                AddSizeSaturated(sd ? sd_required : nand_required, entry.analysis.install_size);
            }
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
                    ConfirmInstallPlan();
                }
            });
        return;
    }
    ConfirmInstallPlan();
}

void Menu::ConfirmInstallPlan() {
    SCOPED_MUTEX(&m_mutex);
    if (m_install_requested) return;
    for (auto& entry : m_queue) {
        entry.install_selected = entry.selected && R_SUCCEEDED(entry.analysis_result);
        entry.install_target = entry.target;
    }
    m_install_requested = true;
}

void Menu::CancelSession() {
    if (m_cancel_requested) {
        SetPop();
        return;
    }
    m_cancel_requested = true;
    ueventSignal(&m_cancel_event);
    if (m_local_fs && !m_local_fs->IsNative()) {
        devoptab::common::CancelActiveCurlTransfers();
    }
    const auto state = m_state.load();
    if (state == State::WaitingForUsb || state == State::WaitingForList || state == State::Analysing || state == State::Installing) {
        if (m_usb_source) m_usb_source->SignalCancel();
    }
    if (state == State::WaitingForUsb || state == State::WaitingForList || state == State::ReviewQueue) {
        SetPop();
    } else {
        m_state = State::Cancelled;
        m_actions_dirty = true;
    }
    AddLog("Cancellation requested."_i18n);
}

void Menu::CycleSelectedTarget() {
    SCOPED_MUTEX(&m_mutex);
    if (m_install_requested) return;
    if (m_index < 0 || m_index >= static_cast<s64>(m_queue.size()) || R_FAILED(m_queue[m_index].analysis_result)) return;
    auto& target = m_queue[m_index].target;
    target = target == InstallTarget::Auto ? InstallTarget::Sd
        : target == InstallTarget::Sd ? InstallTarget::Nand : InstallTarget::Auto;
}

auto Menu::TargetName(InstallTarget target) -> std::string {
    if (target == InstallTarget::Sd) return "microSD"_i18n;
    if (target == InstallTarget::Nand) return "System memory"_i18n;
    return "Auto"_i18n;
}

void Menu::AddLog(const std::string& text) {
    SCOPED_MUTEX(&m_mutex);
    const bool follow_tail = m_log.empty() || m_log_index >= static_cast<s64>(m_log.size()) - 1;
    if (m_log.size() == MAX_LOG_LINES) {
        m_log.erase(m_log.begin());
        if (!follow_tail && m_log_index > 0) m_log_index--;
    }
    m_log.emplace_back(text);
    if (follow_tail) m_log_index = m_log.size() - 1;
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

void Menu::InstallYield() {
    svcSleepThread(1e+6);
}

} // namespace sphaira::ui::menu::dbi

#endif
