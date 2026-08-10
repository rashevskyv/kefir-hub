#pragma once

#include "ui/install_progress.hpp"
#include "ui/list.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/screensaver.hpp"
#include "ui/screensaver_timeout.hpp"
#include "yati/source/usb.hpp"
#include "yati/yati.hpp"
#include <array>

namespace sphaira::ui::menu::dbi {

enum class State {
    WaitingForUsb,
    WaitingForList,
    Analysing,
    ReviewQueue,
    Installing,
    Summary,
    Cancelled,
    Failed,
};

enum class InstallTarget {
    Auto,
    Sd,
    Nand,
};

struct QueueEntry {
    std::string file_name{};
    yati::InstallAnalysis analysis{};
    Result analysis_result{};
    Result install_result{};
    bool selected{true};
    bool installed{};
    InstallTarget target{InstallTarget::Auto};
    bool install_selected{};
    bool analysis_deferred{};
    // where the queue plan puts this package. planned_sd is recomputed while the
    // queue is being reviewed; install_sd is the frozen copy the install thread
    // obeys, so the run lands exactly where the review screen promised.
    bool planned_sd{};
    bool install_sd{};
};

// how a session-log line is drawn: events are bold, results are coloured.
enum class LogKind {
    Normal,   // plain informational line
    Event,    // start / finish / requested -- drawn bold
    Success,  // installed or skipped-already-present -- green
    Warning,  // cancelled -- amber
    Error,    // failed -- red
};

struct LogEntry {
    std::string text{};
    LogKind kind{LogKind::Normal};
};

// a failure kept for the summary screen. Recorded independently of the rolling
// session log (which is capped and drops old lines) and of whether file logging
// is enabled at all, so the list is always complete when the queue ends.
struct SessionError {
    std::string name{};      // package the failure belongs to
    std::string stage{};     // what was being done, e.g. "Install" / "Analysis"
    Result rc{};
    std::string code_name{}; // symbolic name, empty when the code is unknown
    std::string detail{};    // translated explanation, empty when there is none
};

// totals for the run, shown on the summary panel once the queue ends.
struct SessionStats {
    size_t installed{};
    size_t skipped{};
    size_t failed{};
    s64 read_bytes{};   // pulled from the source (over usb / off disk)
    s64 write_bytes{};  // committed to storage after decompression
    s64 sd_bytes{};
    s64 nand_bytes{};
    u64 elapsed_ns{};
};

struct Menu final : MenuBase, InstallProgress {
    Menu(u32 flags);
    Menu(u32 flags, fs::Fs* fs, std::vector<fs::FsPath> paths, std::vector<s64> source_sizes = {}, bool defer_analysis = false);
    ~Menu();

    auto GetShortTitle() const -> const char* override { return "DBI"; }
    // the screensaver page owns the whole panel, header and hint row included.
    auto WantsChrome() const -> bool override { return !m_screensaver.OwnsScreen(); }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void ThreadFunction();
    void LocalThreadFunction();
    // brings the usb session back after the host re-enumerated the device,
    // so the package that was interrupted can be replayed.
    Result ReestablishUsbLink();

    Result CheckCancelled() override;
    UEvent* GetInstallCancelEvent() override { return &m_cancel_event; }
    void SetInstallTitle(const std::string& title) override;
    void SetInstallImage(std::vector<u8>&) override {}
    void SetInstallTransfer(const std::string& transfer) override;
    void UpdateInstallTransfer(s64 offset, s64 size) override;
    void UpdateInstallReadWrite(s64 read_offset, s64 write_offset) override;
    void InstallYield() override;
    bool PromptReinstall(const std::string& title_name) override;
    void OnInstallSkipped() override;

private:
    void UpdateActions();
    void CancelSession();
    void StartInstall();
    void ConfirmInstallPlan();
    // assigns every selected package to NAND or SD up front, honouring the
    // per-target reserve and the install-location priority. Call with the mutex.
    void RecomputePlan();
    void CycleSelectedTarget();
    void DisplayQueueOptions(bool left_side = false);
    void AddLog(const std::string& text, LogKind kind = LogKind::Normal);
    // records a failure for the summary screen and writes it to the always-on
    // error log. Call instead of AddLog() for anything that failed.
    void AddError(const std::string& name, const std::string& stage, Result rc);
    void BeginSessionStats();
    void ToggleErrorView();
    // folds one finished package into the queue entry and the session totals.
    void RecordPackageResult(size_t index, Result rc, bool cancelled, bool to_sd, s64 read_delta, s64 write_delta);
    void DrawSummaryPanel(NVGcontext* vg, Theme* theme, const Vec4& area);
    // the scrolling list under the header: the session log, or the error list
    // when the summary has it toggled on.
    void DrawBottomList(NVGcontext* vg, Theme* theme);
    static auto TargetName(InstallTarget target) -> std::string;
    static auto FormatDuration(u64 ns) -> std::string;

    // the two running totals the header line and the screensaver both read off.
    // Call with the mutex.
    auto AvgWriteBps() const -> s64;
    auto OverallDone() const -> s64;
    // takes the mutex itself: the screensaver draws instead of the queue, not
    // inside it.
    auto ComputeSaverInfo() -> SaverInfo;

    std::unique_ptr<yati::source::Usb> m_usb_source{};
    fs::Fs* m_local_fs{};
    std::vector<fs::FsPath> m_local_paths{};
    std::vector<s64> m_local_source_sizes{};
    bool m_defer_local_analysis{};
    std::unique_ptr<List> m_list{};
    std::unique_ptr<List> m_log_list{};
    bool m_was_mtp_enabled{};

    SessionStats m_stats{};
    std::vector<SessionError> m_errors{};
    // summary screen swaps the bottom list between the session log and the
    // error list; the errors survive the log's line cap. The error list gets
    // its own List because its rows are two lines tall.
    bool m_show_errors{};
    std::unique_ptr<List> m_error_list{};
    s64 m_error_index{};
    TimeStamp m_session_timestamp{};
    // peak write rate seen across the whole run, sampled by the graph in Draw().
    std::atomic<s64> m_peak_write_bps{};

    Thread m_thread{};
    bool m_thread_created{};
    Mutex m_mutex{};
    UEvent m_cancel_event{};
    std::atomic<State> m_state{State::WaitingForUsb};
    std::atomic_bool m_install_requested{};
    std::atomic_bool m_cancel_requested{};
    std::atomic_bool m_actions_dirty{true};

    std::vector<QueueEntry> m_queue{};
    std::vector<LogEntry> m_log{};
    s64 m_index{};
    s64 m_log_index{};
    s64 m_log_last_seen_size{};
    bool m_session_failed{};
    // why the session ended on the Failed screen, drawn under its title. Empty
    // when the failure has no explanation worth showing.
    std::string m_fail_reason{};
    std::string m_current_title{};
    std::string m_current_transfer{};
    s64 m_progress_offset{};
    s64 m_progress_size{};
    s64 m_progress_last_offset{};
    s64 m_progress_speed{};
    std::array<s64, 8> m_progress_speed_samples{};
    size_t m_progress_speed_sample_count{};
    size_t m_progress_speed_sample_index{};
    TimeStamp m_progress_timestamp{};
    size_t m_current_package{};

    // R/W speed graph. Offsets are cumulative within the session: yati
    // reports per-file offsets, UpdateInstallReadWrite() folds them into
    // monotonic totals so per-file resets don't produce negative deltas.
    static constexpr size_t SPEED_HISTORY = 96;
    std::atomic<s64> m_total_read{};
    std::atomic<s64> m_total_write{};
    s64 m_last_file_read{};
    s64 m_last_file_write{};
    s64 m_graph_last_read{};
    s64 m_graph_last_write{};
    std::array<s64, SPEED_HISTORY> m_read_history{};
    std::array<s64, SPEED_HISTORY> m_write_history{};
    size_t m_history_index{};
    size_t m_history_count{};
    TimeStamp m_graph_timestamp{};
    
    struct PromptData {
        std::string title;
        std::atomic<int> choice{-1};
    };
    std::shared_ptr<PromptData> m_prompt_data{};
    std::optional<bool> m_current_file_reinstall_choice{};
    // set by OnInstallSkipped() while a title is being installed: the queue
    // logs "skipped (already installed)" instead of "installed" for it.
    bool m_current_file_skipped{};

    // whole-run progress: the plan's total write size, how much of it is behind
    // us, and the session write count when the current package started.
    s64 m_plan_total_bytes{};
    s64 m_plan_done_bytes{};
    s64 m_package_write_start{};

    // Minus blanks the panel while a long queue runs; see ui/screensaver.hpp.
    Screensaver m_screensaver{};
    SaverInfo m_cached_saver_info{};
    InactivityTracker m_inactivity_tracker{};
    TimeStamp m_inactivity_timestamp{};

    TimeStamp m_usb_poll_ts{};
    char m_usb_link_buf[128]{};

    long m_session_skip_if_already_installed{1};
    long m_session_install_location{4};
    long m_session_reserve_mb{500};
    long m_session_reserve_sd_mb{500};
};

} // namespace sphaira::ui::menu::dbi
