#pragma once

#include "ui/install_progress.hpp"
#include "ui/list.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/screensaver.hpp"
#include "ui/screensaver_timeout.hpp"
#include "ui/menus/dbi/install_queue_state.hpp"
#include "yati/source/usb.hpp"
#include "yati/yati.hpp"
#include <array>
#include <optional>

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
    std::string batch_id{};
    std::string file_name{};
    yati::InstallAnalysis analysis{};
    Result analysis_result{};
    std::optional<Result> install_result{};
    bool selected{true};
    bool installed{};
    InstallTarget target{InstallTarget::Auto};
    bool install_selected{};
    bool analysis_deferred{};
    // where the queue plan puts this package. planned_sd is recomputed while the
    // queue is being reviewed; install_sd is copied at confirm, and Auto entries
    // are re-picked from live usable space at each package start.
    bool planned_sd{};
    bool install_sd{};
    bool rejected_no_space{};
    s64 source_size{0};
    size_t source_index{0};
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

} // namespace sphaira::ui::menu::dbi

namespace sphaira::ui::menu::stream {
struct Stream;
}

namespace sphaira::ui::menu::dbi {

struct InstallSession : MenuBase, InstallProgress {
    InstallSession(const std::string& title, u32 flags, TransportOrigin origin = TransportOrigin::Dbi);
    virtual ~InstallSession();

    auto GetShortTitle() const -> const char* override { return "Install"; }
    auto WantsChrome() const -> bool override { return !m_screensaver.OwnsScreen() && !m_minimized; }
    auto BlocksDrawUnder() const -> bool override { return !m_minimized; }
    auto IsMinimized() const -> bool override { return m_minimized; }
    void ToggleMinimized() override;
    void SetMinimized(bool min) override;

    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;

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
    void OnCompatibilityWarning(const CompatibilityWarning& warning) override;
    void SetInstallTarget(bool to_sd) override;

    void EnqueueFile(const std::string& name, s64 size = 0, bool to_sd = true, const std::string& batch_id = "");
    bool HasQueuedFile(const std::string& name) const;
    bool HasQueuedItem(const std::string& batch_id, const std::string& name) const;
    const std::vector<QueueEntry>& GetQueue() const { return m_queue; }
    void SetCurrentPackageIndex(size_t index);
    void SetCurrentPackageByName(const std::string& name);
    void SetCurrentPackage(const std::string& batch_id, const std::string& name, s64 size = 0, bool to_sd = true);
    void SetCurrentPackageTarget(bool to_sd);
    void RecordCurrentPackageResult(Result rc, bool cancelled = false, bool user_skipped = false);
    void MarkPackageComplete(size_t index, Result rc, bool user_skipped = false);
    void TransitionToSummary(bool failed = false);
    bool AllPackagesTerminal() const;

    auto GetTransportOrigin() const -> TransportOrigin { return m_origin; }
    auto GetOrigin() const -> TransportOrigin { return m_origin; }
    size_t GetQueueSize() const { return m_queue.size(); }
    size_t GetCurrentPackageIndex() const { return m_current_package; }
    auto GetState() const -> State { return m_state.load(); }
    void SetState(State state);

    bool IsCancelRequested() const { return m_cancel_requested.load(); }
    bool IsSkipRequested() const { return m_skip_requested.load(); }
    void ResetSkipRequest() { m_skip_requested = false; }
    bool ShouldExit() const { return m_should_exit.load(); }
    void RequestExit() { m_should_exit = true; }

    void SetStream(std::shared_ptr<stream::Stream> s) { m_stream = s; }
    auto GetStream() const { return m_stream; }

    virtual void CancelSession();
    virtual void SkipCurrentPackage(size_t expected_package);
    virtual void UpdateActions();

    void AddLog(const std::string& text, LogKind kind = LogKind::Normal);
    void AddError(const std::string& name, const std::string& stage, Result rc);
    void BeginSessionStats();
    void ToggleErrorView();
    void RecordPackageResult(size_t index, Result rc, bool cancelled, bool user_skipped, bool to_sd, s64 read_delta, s64 write_delta);
    void RecordPackageResultLocked(size_t index, Result rc, bool cancelled, bool user_skipped, bool to_sd, s64 read_delta, s64 write_delta);

    auto AvgWriteBps() const -> s64;
    auto OverallDone() const -> s64;
    auto ComputeSaverInfo() -> SaverInfo;

    void DrawSummaryPanel(NVGcontext* vg, Theme* theme, const Vec4& area);
    void DrawBottomList(NVGcontext* vg, Theme* theme);
    void DrawInstalling(NVGcontext* vg, Theme* theme);
    void DrawMiniBadge(NVGcontext* vg, Theme* theme);

    static auto TargetName(InstallTarget target) -> std::string;
    static auto FormatDuration(u64 ns) -> std::string;

protected:
    TransportOrigin m_origin{TransportOrigin::Dbi};
    std::atomic<State> m_state{State::Installing};
    std::atomic_bool m_cancel_requested{false};
    std::atomic_bool m_skip_requested{false};
    std::atomic_bool m_actions_dirty{true};
    std::atomic_bool m_should_exit{false};
    bool m_minimized{false};

    Mutex m_mutex{};
    UEvent m_cancel_event{};

    SessionStats m_stats{};
    std::vector<SessionError> m_errors{};
    std::unique_ptr<List> m_log_list{};
    std::unique_ptr<List> m_error_list{};
    bool m_show_errors{false};
    s64 m_error_index{0};
    s64 m_log_index{0};
    s64 m_log_last_seen_size{0};
    TimeStamp m_session_timestamp{};
    std::atomic<s64> m_peak_write_bps{0};

    std::vector<QueueEntry> m_queue{};
    std::vector<LogEntry> m_log{};
    bool m_session_failed{false};
    std::string m_fail_reason{};
    std::string m_current_title{};
    std::string m_current_transfer{};
    s64 m_progress_offset{0};
    s64 m_progress_size{0};
    s64 m_progress_last_offset{0};
    s64 m_progress_speed{0};
    std::array<s64, 8> m_progress_speed_samples{};
    size_t m_progress_speed_sample_count{0};
    size_t m_progress_speed_sample_index{0};
    TimeStamp m_progress_timestamp{};
    size_t m_current_package{0};

    static constexpr size_t SPEED_HISTORY = 96;
    std::atomic<s64> m_total_read{0};
    std::atomic<s64> m_total_write{0};
    s64 m_last_file_read{0};
    s64 m_last_file_write{0};
    s64 m_graph_last_read{0};
    s64 m_graph_last_write{0};
    std::array<s64, SPEED_HISTORY> m_read_history{};
    std::array<s64, SPEED_HISTORY> m_write_history{};
    size_t m_history_index{0};
    size_t m_history_count{0};
    TimeStamp m_graph_timestamp{};

    struct PromptData {
        std::string title;
        std::atomic<int> choice{-1};
    };
    std::shared_ptr<PromptData> m_prompt_data{};
    std::optional<bool> m_current_file_reinstall_choice{};
    bool m_current_file_skipped{false};

    s64 m_plan_total_bytes{0};
    s64 m_plan_done_bytes{0};
    s64 m_package_write_start{0};

    Screensaver m_screensaver{};
    SaverInfo m_cached_saver_info{};
    InactivityTracker m_inactivity_tracker{};
    TimeStamp m_inactivity_timestamp{};

    std::vector<CompatibilityWarning> m_compat_warnings{};
    std::vector<CompatibilityWarning> m_pending_warning_popups{};

    std::shared_ptr<stream::Stream> m_stream{};
};

struct Menu final : InstallSession {
    Menu(u32 flags);
    Menu(u32 flags, fs::Fs* fs, std::vector<fs::FsPath> paths, std::vector<s64> source_sizes = {}, bool defer_analysis = false);
    ~Menu();

    auto GetShortTitle() const -> const char* override { return "DBI"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void ThreadFunction();
    void LocalThreadFunction();
    Result ReestablishUsbLink();

    void CancelSession() override;
    void SkipCurrentPackage(size_t expected_package) override;
    void UpdateActions() override;

private:
    void StartInstall();
    void ConfirmInstallPlan();
    void RecomputePlan();
    bool RefreshAutoInstallTarget(size_t index);
    bool ApplyLiveSelection(const std::unordered_map<std::string, bool>& selections);
    void SetIndex(s64 index);
    void CycleSelectedTarget();
    void DisplayQueueOptions(bool left_side = false);
    void SortQueue();

    std::unique_ptr<yati::source::Usb> m_usb_source{};
    fs::Fs* m_local_fs{};
    std::vector<fs::FsPath> m_local_paths{};
    std::vector<s64> m_local_source_sizes{};
    bool m_defer_local_analysis{};
    std::unique_ptr<List> m_list{};
    bool m_was_mtp_enabled{};
    s64 m_index{};

    Thread m_thread{};
    bool m_thread_created{};
    std::atomic_bool m_install_requested{};

    TimeStamp m_usb_poll_ts{};
    char m_usb_link_buf[128]{};

    long m_session_skip_if_already_installed{1};
    long m_session_install_location{4};
    long m_session_reserve_mb{500};
    long m_session_reserve_sd_mb{500};
    long m_session_sort_type{0};
    long m_session_sort_order{0};
};

} // namespace sphaira::ui::menu::dbi
