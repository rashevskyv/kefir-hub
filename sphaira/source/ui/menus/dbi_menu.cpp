#if ENABLE_NETWORK_INSTALL

#include "ui/menus/dbi_menu.hpp"
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
#include "utils/devoptab_curl_thread.hpp"
#include "utils/utils.hpp"
#include "yati/source/file.hpp"
#include <usbhsfs.h>

#include "title_info.hpp"

#include <algorithm>
#include <cstdio>
#include <ranges>

namespace sphaira::ui::menu::dbi {
namespace {

constexpr u64 CONNECTION_TIMEOUT = UINT64_MAX;
constexpr u64 TRANSFER_TIMEOUT = UINT64_MAX;
constexpr u64 FINISHED_TIMEOUT = 1e+9 * 3;
constexpr size_t MAX_LOG_LINES = 128;
// how long to wait for the host to re-enumerate us and answer the replayed
// handshake after the link dropped. Bounded, unlike the session timeouts
// above, so an unplugged cable ends the queue instead of hanging on it.
constexpr u64 RECONNECT_TIMEOUT = 1e+9 * 15;
// a blip usually recovers on the first attempt; the cap is there so a link
// that keeps flapping ends the session rather than looping forever.
constexpr u32 MAX_LINK_RETRIES = 3;

void thread_func(void* user) {
    static_cast<Menu*>(user)->ThreadFunction();
}

auto ResultText(Result rc) -> std::string {
    char out[32]{};
    std::snprintf(out, sizeof(out), "0x%08X", R_VALUE(rc));
    return out;
}

auto IsDbiSessionError(Result rc) -> bool {
    // a dropped link poisons the whole session: every following package would
    // fail instantly with the same result, so it has to end the queue too.
    if (usb::IsLinkError(rc)) {
        return true;
    }

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
            return true;
        default:
            return false;
    }
}

void AddSizeSaturated(s64& total, s64 value) {
    total = value > INT64_MAX - total ? INT64_MAX : total + value;
}

u64 GetQueueEntryTitleId(const QueueEntry& entry) {
    for (const auto& col : entry.analysis.collections) {
        if (path::EndsWithIC(col.name, ".cnmt.nca") || path::EndsWithIC(col.name, ".cnmt.ncz")) {
            size_t pos = col.name.find(".cnmt.");
            if (pos != std::string::npos && pos >= 16) {
                std::string hex_str = col.name.substr(pos - 16, 16);
                u64 val = 0;
                bool ok = true;
                for (char c : hex_str) {
                    val <<= 4;
                    if (c >= '0' && c <= '9') val |= (c - '0');
                    else if (c >= 'a' && c <= 'f') val |= (c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F') val |= (c - 'A' + 10);
                    else { ok = false; break; }
                }
                if (ok) return val;
            }
        }
    }
    return 0;
}

bool IsTitleAlreadyInstalled(u64 title_id) {
    if (!title_id) return false;
    // Check BuiltInUser (NAND)
    {
        auto& db = title::GetNcmDb(NcmStorageId_BuiltInUser);
        NcmContentMetaKey key{};
        if (R_SUCCEEDED(ncmContentMetaDatabaseGetLatestContentMetaKey(&db, &key, title_id))) {
            return true;
        }
    }
    // Check SdCard
    {
        auto& db = title::GetNcmDb(NcmStorageId_SdCard);
        NcmContentMetaKey key{};
        if (R_SUCCEEDED(ncmContentMetaDatabaseGetLatestContentMetaKey(&db, &key, title_id))) {
            return true;
        }
    }
    return false;
}

// one "label: value" cell of a stats row.
struct StatItem {
    std::string label{};
    std::string value{};
    // value colour; the label is always drawn in the info colour.
    std::optional<NVGcolor> colour{};
};

// draws stat cells left to right, with the label faked bold (over-drawn -- no
// bold font face is loaded) so the labels stand out from the numbers.
void DrawStatRow(NVGcontext* vg, NVGcolor info_col, float x0, float y, float size, const std::vector<StatItem>& items) {
    float x = x0;
    nvgFontSize(vg, size);
    float b[4];
    for (const auto& item : items) {
        const auto lab = item.label + ": ";
        gfx::drawTextArgs(vg, x, y, size, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, info_col, "%s", lab.c_str());
        gfx::drawTextArgs(vg, x + 0.7f, y, size, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, info_col, "%s", lab.c_str());
        gfx::textBounds(vg, 0, 0, b, lab.c_str());
        x += b[2] - b[0];
        const auto value_col = item.colour.value_or(info_col);
        gfx::drawTextArgs(vg, x, y, size, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, value_col, "%s", item.value.c_str());
        gfx::textBounds(vg, 0, 0, b, item.value.c_str());
        x += (b[2] - b[0]) + 28.f;
    }
}

void DrawCheckbox(NVGcontext* vg, Theme* theme, const Vec4& row, bool selected) {
    gfx::drawCheckbox(vg, theme, row.x + 12.f, row.y + (row.h - gfx::CHECKBOX_SIZE) / 2.f, gfx::CHECKBOX_SIZE, selected);
}

} // namespace

Menu::Menu(u32 flags) : MenuBase{"Install queue"_i18n, flags} {
    mutexInit(&m_mutex);
    ueventCreate(&m_cancel_event, false);

    m_session_skip_if_already_installed = App::GetApp()->m_skip_if_already_installed.Get();
    m_session_install_location = App::GetInstallLocation();
    m_session_reserve_mb = App::GetInstallReserveMb();

    const Vec4 queue_pos{70.f, GetY() + 80.f, 1140.f, 500.f};
    const Vec4 row{queue_pos.x, queue_pos.y, queue_pos.w, 82.f};
    m_list = std::make_unique<List>(1, 6, queue_pos, row);
    m_list->SetLayout(List::Layout::GRID);
    const Vec4 log_pos{70.f, GetY() + 235.f, 1140.f, 330.f};
    const Vec4 log_row{log_pos.x, log_pos.y, log_pos.w, 30.f};
    m_log_list = std::make_unique<List>(1, 11, log_pos, log_row);
    m_log_list->SetLayout(List::Layout::GRID);
    m_error_list = std::make_unique<List>(1, 6, log_pos, Vec4{log_pos.x, log_pos.y, log_pos.w, 55.f});
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

    m_session_skip_if_already_installed = App::GetApp()->m_skip_if_already_installed.Get();
    m_session_install_location = App::GetInstallLocation();
    m_session_reserve_mb = App::GetInstallReserveMb();

    const Vec4 queue_pos{70.f, GetY() + 80.f, 1140.f, 500.f};
    m_list = std::make_unique<List>(1, 6, queue_pos, Vec4{queue_pos.x, queue_pos.y, queue_pos.w, 82.f});
    m_list->SetLayout(List::Layout::GRID);
    const Vec4 log_pos{70.f, GetY() + 235.f, 1140.f, 330.f};
    m_log_list = std::make_unique<List>(1, 11, log_pos, Vec4{log_pos.x, log_pos.y, log_pos.w, 30.f});
    m_log_list->SetLayout(List::Layout::GRID);
    m_error_list = std::make_unique<List>(1, 6, log_pos, Vec4{log_pos.x, log_pos.y, log_pos.w, 55.f});
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
            std::make_pair(Button::START, Action{"Options"_i18n, [this]() { DisplayQueueOptions(); }}),
            std::make_pair(Button::B, Action{"Cancel session"_i18n, [this]() { CancelSession(); }})
        );
    } else if (state == State::Installing) {
        SetAction(Button::B, Action{"Cancel queue"_i18n, [this]() { CancelSession(); }});
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
    m_actions_dirty = false;
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    if (m_actions_dirty) UpdateActions();

    std::shared_ptr<PromptData> prompt{};
    {
        SCOPED_MUTEX(&m_mutex);
        if (m_prompt_data && m_prompt_data->choice == -1) {
            prompt = m_prompt_data;
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
        s64 sd_required{}, nand_required{};
        for (const auto& entry : m_queue) {
            if (entry.selected && R_SUCCEEDED(entry.analysis_result)) {
                // deferred entries only know the (compressed) file size: use
                // it as a lower bound so the projection is never empty.
                const auto size = entry.analysis_deferred
                    ? entry.analysis.source_size : entry.analysis.install_size;
                
                const u64 title_id = GetQueueEntryTitleId(entry);
                if (!IsTitleAlreadyInstalled(title_id)) {
                    AddSizeSaturated(selected_size, std::max<s64>(0, size));
                    const bool sd = entry.target == InstallTarget::Sd
                        || (entry.target == InstallTarget::Auto && entry.analysis.suggested_sd);
                    AddSizeSaturated(sd ? sd_required : nand_required, std::max<s64>(0, size));
                }
                selected_count++;
            }
        }
        // preview the required space on the NAND/SD bars in the status area.
        SetStorageProjection(nand_required, sd_required);
        const auto spaces = GetPolledData();
        const auto reserve = App::GetInstallReserveMb() * 1024ULL * 1024ULL;
        const auto info_col = theme->GetColour(ThemeEntryID_TEXT_INFO);
        DrawStatRow(vg, info_col, 70.f, GetY() + 10.f, 17.f, {
            {"Targets"_i18n, "Per package"_i18n},
            {"Selected"_i18n, std::to_string(selected_count) + " / " + std::to_string(m_queue.size())},
            {"Required"_i18n, utils::formatSizeStorage(selected_size)},
        });
        DrawStatRow(vg, info_col, 70.f, GetY() + 36.f, 15.f, {
            {"microSD free"_i18n, utils::formatSizeStorage(std::max<s64>(0, spaces.sd_free - reserve))},
            {"NAND free"_i18n, utils::formatSizeStorage(std::max<s64>(0, spaces.nand_free - reserve))},
            {"Reserve"_i18n, utils::formatSizeStorage(reserve)},
        });

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

    if (state == State::Installing) {
        // keep projecting the space still needed by the remaining packages.
        s64 sd_required{}, nand_required{};
        for (const auto& entry : m_queue) {
            if (!entry.install_selected || entry.installed || R_FAILED(entry.analysis_result)) continue;
            const auto size = entry.analysis_deferred
                ? entry.analysis.source_size : entry.analysis.install_size;
            
            const u64 title_id = GetQueueEntryTitleId(entry);
            if (!IsTitleAlreadyInstalled(title_id)) {
                const bool sd = entry.install_target == InstallTarget::Sd
                    || (entry.install_target == InstallTarget::Auto && entry.analysis.suggested_sd);
                AddSizeSaturated(sd ? sd_required : nand_required, std::max<s64>(0, size));
            }
        }
        SetStorageProjection(nand_required, sd_required);
    } else {
        ClearStorageHighlight();
    }

    // once the queue has ended the live header, progress bar and graph have
    // nothing left to say, so that space goes to the session summary instead.
    if (state == State::Summary || state == State::Cancelled) {
        DrawSummaryPanel(vg, theme, Vec4{70.f, GetY() + 8.f, 1140.f, 214.f});
        DrawBottomList(vg, theme);
        return;
    }

    // Header speed and ETA use the average write rate over the whole graph
    // history window (~48 s, near a minute), not the instantaneous rate. The
    // moment-to-moment R/W speeds are shown per line on the graph below, so the
    // header stays a single stable "how fast is this going overall" number.
    s64 avg_write_bps = 0;
    if (m_history_count) {
        s64 sum = 0;
        for (size_t i = 0; i < m_history_count; i++) {
            const auto idx = (m_history_index + SPEED_HISTORY - m_history_count + i) % SPEED_HISTORY;
            sum += m_write_history[idx];
        }
        avg_write_bps = sum / static_cast<s64>(m_history_count);
    }
    const double speed_mib = static_cast<double>(avg_write_bps) / (1024.0 * 1024.0);
    std::string eta{};
    if (m_history_count >= 4 && avg_write_bps > 0 && m_progress_size > m_progress_offset) {
        const auto seconds_left = static_cast<size_t>((m_progress_size - m_progress_offset) / avg_write_bps);
        char eta_buf[64]{};
        if (seconds_left >= 3600) {
            std::snprintf(eta_buf, sizeof(eta_buf), "%zuh %zum", seconds_left / 3600, seconds_left % 3600 / 60);
        } else {
            std::snprintf(eta_buf, sizeof(eta_buf), "%zum %zus", seconds_left / 60, seconds_left % 60);
        }
        eta = eta_buf;
    }
    char avg_buf[32]{};
    std::snprintf(avg_buf, sizeof(avg_buf), "%.2f MiB/s", speed_mib);
    std::vector<StatItem> header{
        {"Package"_i18n, std::to_string(std::min(m_current_package + 1, m_queue.size())) + "/" + std::to_string(m_queue.size())},
        {"Installed"_i18n, std::to_string(m_stats.installed)},
        {"Failed"_i18n, std::to_string(m_stats.failed), m_stats.failed ? std::optional{theme->GetColour(ThemeEntryID_ERROR)} : std::nullopt},
        // the R/W readout beside the graph is the momentary rate and is where the
        // eye lands; this is the whole-window average, accented so it is not lost.
        {"Average speed"_i18n, avg_buf, theme->GetColour(ThemeEntryID_TEXT_SELECTED)},
    };
    if (!eta.empty()) {
        header.push_back({"Remaining"_i18n, eta});
    }
    DrawStatRow(vg, theme->GetColour(ThemeEntryID_TEXT_INFO), 70.f, GetY() + 10.f, 18.f, header);
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

    // R/W speed graph: red = source read, blue = storage write.
    {
        const double graph_elapsed = m_graph_timestamp.GetSecondsD();
        if (state == State::Installing && graph_elapsed >= 0.5) {
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

        const auto red = nvgRGBA(231, 76, 60, 255);
        const auto blue = nvgRGBA(52, 152, 219, 255);
        const Vec4 plot{110.f, GetY() + 95.f, 930.f, 125.f};
        const float pad = 4.f;

        gfx::drawRect(vg, plot, theme->GetColour(ThemeEntryID_PROGRESSBAR_BACKGROUND), 3.f);

        // labels to the left of the plot.
        gfx::drawTextArgs(vg, plot.x - 14.f, plot.y + plot.h * 0.30f, 20.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, red, "R");
        gfx::drawTextArgs(vg, plot.x - 14.f, plot.y + plot.h * 0.70f, 20.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, blue, "W");

        // readout is averaged over the last few samples so a single idle window
        // (read waiting on decompress/write) does not make it flicker to 0.
        const auto avg_mib = [&](const std::array<s64, SPEED_HISTORY>& history) -> double {
            const size_t n = std::min<size_t>(m_history_count, 4);
            if (!n) return 0.0;
            s64 sum = 0;
            for (size_t i = 0; i < n; i++) {
                const auto idx = (m_history_index + SPEED_HISTORY - 1 - i) % SPEED_HISTORY;
                sum += history[idx];
            }
            return (double)sum / (double)n / (1024.0 * 1024.0);
        };
        // captioned "now" so it reads as the momentary rate, distinct from the
        // averaged figure in the header line above.
        gfx::drawTextArgs(vg, plot.x + plot.w + 14.f, plot.y + 2.f, 13.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
            theme->GetColour(ThemeEntryID_TEXT_INFO), "%s", "Now"_i18n.c_str());
        const auto draw_readout = [&](float ry, NVGcolor colour, double mib) {
            char buf[32]{};
            std::snprintf(buf, sizeof(buf), "%.1f MiB/s", mib);
            gfx::drawTextBold(vg, plot.x + plot.w + 14.f, ry, 18.f, colour, buf, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        };
        draw_readout(plot.y + plot.h * 0.30f, red, avg_mib(m_read_history));
        draw_readout(plot.y + plot.h * 0.70f, blue, avg_mib(m_write_history));

        if (m_history_count >= 2) {
            s64 peak = 1;
            for (size_t i = 0; i < m_history_count; i++) {
                const auto idx = (m_history_index + SPEED_HISTORY - m_history_count + i) % SPEED_HISTORY;
                peak = std::max({peak, m_read_history[idx], m_write_history[idx]});
            }
            const double peak_mib = (double)peak / (1024.0 * 1024.0);

            // round the gridline step to a "nice" 1/2/5 x 10^n MiB/s, then pick
            // the top of scale as a whole number of steps that clears the peak.
            // A 1 MiB/s floor keeps slow transfers from filling the whole plot.
            const auto nice_step = [](double range) -> double {
                double s = 1.0;
                while (true) {
                    if (range <= s) return s;
                    if (range <= s * 2.0) return s * 2.0;
                    if (range <= s * 5.0) return s * 5.0;
                    s *= 10.0;
                }
            };
            const double step_mib = nice_step(std::max(peak_mib, 1.0) / 4.0);
            int steps = 1;
            while (step_mib * steps < peak_mib) steps++;
            const double top_mib = step_mib * steps;
            const double top = top_mib * 1024.0 * 1024.0;

            // clip lines and grid to the plot so nothing bleeds past its edges.
            nvgSave(vg);
            nvgIntersectScissor(vg, plot.x, plot.y, plot.w, plot.h);

            auto grid_col = theme->GetColour(ThemeEntryID_TEXT);
            grid_col.a = 0.12f;
            const auto label_col = theme->GetColour(ThemeEntryID_TEXT_INFO);
            const float inner_h = plot.h - pad * 2.f;
            for (int k = 0; k <= steps; k++) {
                const float gy = plot.y + plot.h - pad - inner_h * (float)k / (float)steps;
                nvgBeginPath(vg);
                nvgMoveTo(vg, plot.x + pad, gy);
                nvgLineTo(vg, plot.x + plot.w - pad, gy);
                nvgStrokeColor(vg, grid_col);
                nvgStrokeWidth(vg, 1.f);
                nvgStroke(vg);
                if (k > 0) {
                    const double val = step_mib * k;
                    if (k == steps) {
                        gfx::drawTextArgs(vg, plot.x + pad + 4.f, gy + 2.f, 12.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, label_col, "%g MiB/s", val);
                    } else {
                        gfx::drawTextArgs(vg, plot.x + pad + 4.f, gy + 2.f, 12.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, label_col, "%g", val);
                    }
                }
            }

            const auto draw_line = [&](const std::array<s64, SPEED_HISTORY>& history, NVGcolor colour) {
                nvgBeginPath(vg);
                for (size_t i = 0; i < m_history_count; i++) {
                    const auto idx = (m_history_index + SPEED_HISTORY - m_history_count + i) % SPEED_HISTORY;
                    // newest sample is pinned to the right edge.
                    const auto slot = SPEED_HISTORY - m_history_count + i;
                    const float x = plot.x + pad + (plot.w - pad * 2.f) * slot / (SPEED_HISTORY - 1);
                    const double frac = std::clamp((double)history[idx] / top, 0.0, 1.0);
                    const float y = plot.y + plot.h - pad - inner_h * (float)frac;
                    if (i == 0) nvgMoveTo(vg, x, y);
                    else nvgLineTo(vg, x, y);
                }
                nvgStrokeColor(vg, colour);
                nvgStrokeWidth(vg, 2.f);
                nvgStroke(vg);
            };
            // additive blend so where the red (R) and blue (W) lines overlap
            // they sum into a bright mixed colour, making crossings obvious
            // instead of one line simply hiding the other. Restored with the
            // enclosing nvgRestore (composite op is part of the saved state).
            nvgGlobalCompositeOperation(vg, NVG_LIGHTER);
            draw_line(m_read_history, red);
            draw_line(m_write_history, blue);

            nvgRestore(vg);
        }
    }

    DrawBottomList(vg, theme);
}

void Menu::DrawBottomList(NVGcontext* vg, Theme* theme) {
    // caller holds m_mutex.
    if (m_show_errors) {
        const auto error_col = theme->GetColour(ThemeEntryID_ERROR);
        const auto info_col = theme->GetColour(ThemeEntryID_TEXT_INFO);
        m_error_list->Draw(vg, theme, m_errors.size(), [this, error_col, info_col](NVGcontext* vg, Theme* theme, Vec4 v, s64 index) {
            const auto& error = m_errors[index];
            if (index == m_error_index) {
                gfx::drawRectOutline(vg, theme, 2.f, v);
            }
            gfx::drawTextArgs(vg, v.x + 10.f, v.y + 5.f, 16.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, error_col,
                "%ld. %s — %s", index + 1, error.stage.c_str(), error.name.c_str());

            // second line: the raw code, its symbolic name, and the plain
            // language explanation when sphaira has one for it.
            auto text = ResultText(error.rc);
            if (!error.code_name.empty()) {
                text += "  " + error.code_name;
            }
            if (!error.detail.empty()) {
                text += "  —  " + error.detail;
            }
            gfx::drawTextArgs(vg, v.x + 26.f, v.y + 28.f, 14.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, info_col, "%s", text.c_str());
        });
        return;
    }

    m_log_list->Draw(vg, theme, m_log.size(), [this](NVGcontext* vg, Theme* theme, Vec4 v, s64 index) {
        const auto& entry = m_log[index];
        NVGcolor colour;
        switch (entry.kind) {
            case LogKind::Success: colour = nvgRGB(80, 200, 120); break;
            case LogKind::Warning: colour = nvgRGB(230, 170, 50); break;
            case LogKind::Error:   colour = theme->GetColour(ThemeEntryID_ERROR); break;
            default:               colour = theme->GetColour(ThemeEntryID_TEXT); break;
        }
        gfx::drawTextArgs(vg, v.x + 4.f, v.y + 5.f, 15.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, colour, "%s", entry.text.c_str());
        // no bold font face is loaded, so fake bold for events by over-drawing
        // with a sub-pixel x offset to thicken the strokes.
        if (entry.kind == LogKind::Event) {
            gfx::drawTextArgs(vg, v.x + 4.7f, v.y + 5.f, 15.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, colour, "%s", entry.text.c_str());
        }
    });
}

void Menu::DrawSummaryPanel(NVGcontext* vg, Theme* theme, const Vec4& area) {
    // caller holds m_mutex.
    gfx::drawRect(vg, area, theme->GetColour(ThemeEntryID_PROGRESSBAR_BACKGROUND), 3.f);

    const auto info_col = theme->GetColour(ThemeEntryID_TEXT_INFO);
    const auto text_col = theme->GetColour(ThemeEntryID_TEXT);
    const auto error_col = theme->GetColour(ThemeEntryID_ERROR);
    const auto good_col = nvgRGB(80, 200, 120);
    const auto warn_col = nvgRGB(230, 170, 50);

    const float x = area.x + 20.f;
    float y = area.y + 10.f;

    // headline: what the run ended as.
    const auto outcome = m_state == State::Cancelled ? "Session cancelled"_i18n
        : m_session_failed ? "Session failed"_i18n : "Queue finished"_i18n;
    const auto outcome_col = m_state == State::Cancelled ? warn_col
        : m_session_failed ? error_col : good_col;
    gfx::drawTextArgs(vg, x, y, 20.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, text_col, "%s", "Session summary"_i18n.c_str());
    gfx::drawTextArgs(vg, x + 0.7f, y, 20.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, text_col, "%s", "Session summary"_i18n.c_str());
    gfx::drawTextArgs(vg, area.x + area.w - 20.f, y, 20.f, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, outcome_col, "%s", outcome.c_str());
    y += 32.f;

    const auto seconds = m_stats.elapsed_ns / 1000000000ULL;
    const double avg_mib = seconds
        ? (double)m_stats.write_bytes / (double)seconds / (1024.0 * 1024.0) : 0.0;
    const double peak_mib = (double)m_peak_write_bps.load() / (1024.0 * 1024.0);
    const auto fmt_speed = [](double mib) {
        char buf[32]{};
        std::snprintf(buf, sizeof(buf), "%.2f MiB/s", mib);
        return std::string{buf};
    };

    DrawStatRow(vg, info_col, x, y, 17.f, {
        {"Installed"_i18n, std::to_string(m_stats.installed), m_stats.installed ? std::optional{good_col} : std::nullopt},
        {"Skipped"_i18n, std::to_string(m_stats.skipped)},
        {"Failed"_i18n, std::to_string(m_stats.failed), m_stats.failed ? std::optional{error_col} : std::nullopt},
        {"Packages"_i18n, std::to_string(m_queue.size())},
    });
    y += 28.f;

    DrawStatRow(vg, info_col, x, y, 17.f, {
        {"Duration"_i18n, FormatDuration(m_stats.elapsed_ns)},
        {"Average speed"_i18n, fmt_speed(avg_mib)},
        {"Peak speed"_i18n, fmt_speed(peak_mib)},
    });
    y += 28.f;

    // read is what came off the source (compressed, over usb); written is what
    // actually landed in storage after decompression, so the two differ for nsz.
    DrawStatRow(vg, info_col, x, y, 17.f, {
        {"Received"_i18n, utils::formatSizeStorage(std::max<s64>(0, m_stats.read_bytes))},
        {"Written"_i18n, utils::formatSizeStorage(std::max<s64>(0, m_stats.write_bytes))},
    });
    y += 28.f;

    DrawStatRow(vg, info_col, x, y, 17.f, {
        {"microSD"_i18n, utils::formatSizeStorage(std::max<s64>(0, m_stats.sd_bytes))},
        {"System memory"_i18n, utils::formatSizeStorage(std::max<s64>(0, m_stats.nand_bytes))},
    });
    y += 30.f;

    if (!m_errors.empty()) {
        gfx::drawTextArgs(vg, x, y, 15.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, error_col, "%s — %s",
            (std::to_string(m_errors.size()) + " " + "error(s) recorded"_i18n).c_str(),
            "press Y to review them, they are also saved to errors.txt"_i18n.c_str());
    } else {
        gfx::drawTextArgs(vg, x, y, 15.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, good_col, "%s",
            "No errors recorded."_i18n.c_str());
    }
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
            if (R_FAILED(entry.analysis_result)) {
                AddError(name, "Analysis"_i18n, entry.analysis_result);
            }
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
            BeginSessionStats();
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
                    m_current_file_reinstall_choice = std::nullopt;
                    m_current_file_skipped = false;
                }
                if (!selected) continue;
                if (m_cancel_requested) break;

                AddLog("Starting: "_i18n + name, LogKind::Event);
                yati::ConfigOverride override{};
                override.skip_if_already_installed = App::GetSaveSettingsGlobally()
                    ? App::GetApp()->m_skip_if_already_installed.Get()
                    : m_session_skip_if_already_installed;
                if (target == InstallTarget::Sd) {
                    override.sd_card_install = true;
                } else if (target == InstallTarget::Nand) {
                    override.sd_card_install = false;
                } else {
                    long loc = App::GetSaveSettingsGlobally() ? App::GetInstallLocation() : m_session_install_location;
                    if (loc == 0) override.sd_card_install = true;
                    else if (loc == 1 || loc == 2) override.sd_card_install = false;
                    else if (loc == 3) override.sd_card_install = true;
                    else override.sd_card_install = analysis.suggested_sd;
                }
                const auto read_before = m_total_read.load();
                const auto write_before = m_total_write.load();

                // The host re-enumerating us mid-package (cable nudged, hub or
                // port glitch) kills the transfer, and dbi backend restarts its
                // command loop on its own. Give the link a bounded chance to
                // come back and replay the package from the start -- yati has
                // already dropped the half-written placeholders -- instead of
                // losing the rest of the queue to a one second blip.
                Result install_rc{};
                for (u32 attempt = 0; ; attempt++) {
                    m_usb_source->SetFileNameForTranfser(name);
                    install_rc = yati::InstallFromCollections(this, m_usb_source.get(), analysis.collections, override);
                    if (!usb::IsLinkError(install_rc) || attempt >= MAX_LINK_RETRIES) {
                        break;
                    }
                    if (m_cancel_requested || GetToken().stop_requested()) {
                        break;
                    }
                    AddLog("USB connection lost; reconnecting..."_i18n, LogKind::Warning);
                    if (R_FAILED(ReestablishUsbLink())) {
                        break;
                    }
                    AddLog("USB connection restored; retrying: "_i18n + name, LogKind::Warning);
                }
                const bool cancelled = m_cancel_requested || install_rc == Result_TransferCancelled || install_rc == Result_UsbCancelled;
                const bool fatal_session_error = R_FAILED(install_rc) && IsDbiSessionError(install_rc);
                RecordPackageResult(i, install_rc, cancelled, override.sd_card_install.value_or(analysis.suggested_sd),
                    m_total_read.load() - read_before, m_total_write.load() - write_before);
                if (R_SUCCEEDED(install_rc)) {
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
                    AddLog("DBI session failed; remaining packages were skipped."_i18n, LogKind::Error);
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
    std::vector<std::string> names;
    R_TRY(m_usb_source->WaitForConnection(RECONNECT_TIMEOUT, names));
    R_SUCCEED();
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
                m_current_file_reinstall_choice = std::nullopt;
                m_current_file_skipped = false;
            }
            if (!selected) continue;
            if (m_cancel_requested) break;

            AddLog("Starting: "_i18n + name, LogKind::Event);
            yati::ConfigOverride override{};
            override.skip_if_already_installed = App::GetSaveSettingsGlobally()
                ? App::GetApp()->m_skip_if_already_installed.Get()
                : m_session_skip_if_already_installed;
            if (target == InstallTarget::Sd) {
                override.sd_card_install = true;
            } else if (target == InstallTarget::Nand) {
                override.sd_card_install = false;
            } else {
                long loc = App::GetSaveSettingsGlobally() ? App::GetInstallLocation() : m_session_install_location;
                if (loc == 0) override.sd_card_install = true;
                else if (loc == 1 || loc == 2) override.sd_card_install = false;
                else if (loc == 3) override.sd_card_install = true;
                else if (!analysis_deferred) override.sd_card_install = analysis.suggested_sd;
            }

            const auto read_before = m_total_read.load();
            const auto write_before = m_total_write.load();

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
            RecordPackageResult(i, result, cancelled, override.sd_card_install.value_or(analysis.suggested_sd),
                m_total_read.load() - read_before, m_total_write.load() - write_before);
            if (R_SUCCEEDED(result)) {
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
            if (cancelled) break;
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
        for (const auto& entry : m_queue) {
            if (!entry.selected || R_FAILED(entry.analysis_result)) continue;
            const bool sd = entry.target == InstallTarget::Sd || (entry.target == InstallTarget::Auto && entry.analysis.suggested_sd);
            if (!entry.analysis_deferred) {
                const u64 title_id = GetQueueEntryTitleId(entry);
                if (!IsTitleAlreadyInstalled(title_id)) {
                    AddSizeSaturated(sd ? sd_required : nand_required, entry.analysis.install_size);
                }
            }
        }
    }
    if (!count) {
        App::Notify("Select at least one package"_i18n);
        return;
    }
    const auto spaces = GetPolledData(true);
    const auto reserve_mb = App::GetSaveSettingsGlobally() ? App::GetInstallReserveMb() : m_session_reserve_mb;
    const auto reserve = reserve_mb * 1024ULL * 1024ULL;
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

    s64 current_reserve = global ? App::GetInstallReserveMb() : m_session_reserve_mb;
    auto reserve_entry_ptr = std::make_unique<SidebarEntryTextBase>("Reserve free space"_i18n,
        std::to_string(current_reserve) + " MB",
        nullptr,
        "Set the threshold of free space to reserve on installation target (MB)."_i18n
    );
    auto* reserve_entry = reserve_entry_ptr.get();
    reserve_entry->SetCallback([this, global, reserve_entry]() {
        s64 out = global ? App::GetInstallReserveMb() : m_session_reserve_mb;
        if (R_SUCCEEDED(swkbd::ShowNumPad(out, "Enter Reserve Free Space (MB)"_i18n.c_str(), std::to_string(out).c_str(), 1, 5))) {
            if (out >= 0 && out <= 32768) {
                if (global) {
                    App::SetInstallReserveMb(out);
                } else {
                    m_session_reserve_mb = out;
                }
                reserve_entry->SetValue(std::to_string(out) + " MB");
            }
        }
    });
    options->Add(std::move(reserve_entry_ptr));
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
    m_session_timestamp.Update();
}

void Menu::RecordPackageResult(size_t index, Result rc, bool cancelled, bool to_sd, s64 read_delta, s64 write_delta) {
    SCOPED_MUTEX(&m_mutex);
    m_queue[index].install_result = rc;
    m_queue[index].installed = R_SUCCEEDED(rc);
    m_stats.read_bytes += std::max<s64>(0, read_delta);
    m_stats.write_bytes += std::max<s64>(0, write_delta);

    if (R_SUCCEEDED(rc)) {
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

    while (data->choice == -1 && !m_cancel_requested && !GetToken().stop_requested()) {
        svcSleepThread(10'000'000ULL); // 10ms
    }

    if (m_cancel_requested || GetToken().stop_requested()) {
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

} // namespace sphaira::ui::menu::dbi

#endif
