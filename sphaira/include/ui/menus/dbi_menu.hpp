#pragma once

#include "ui/install_progress.hpp"
#include "ui/list.hpp"
#include "ui/menus/menu_base.hpp"
#include "yati/source/usb_dbi.hpp"
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
    InstallTarget install_target{InstallTarget::Auto};
    bool analysis_deferred{};
};

struct Menu final : MenuBase, InstallProgress {
    Menu(u32 flags);
    Menu(u32 flags, fs::Fs* fs, std::vector<fs::FsPath> paths, std::vector<s64> source_sizes = {}, bool defer_analysis = false);
    ~Menu();

    auto GetShortTitle() const -> const char* override { return "DBI"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void ThreadFunction();
    void LocalThreadFunction();

    Result CheckCancelled() override;
    UEvent* GetInstallCancelEvent() override { return &m_cancel_event; }
    void SetInstallTitle(const std::string& title) override;
    void SetInstallImage(std::vector<u8>&) override {}
    void SetInstallTransfer(const std::string& transfer) override;
    void UpdateInstallTransfer(s64 offset, s64 size) override;
    void UpdateInstallReadWrite(s64 read_offset, s64 write_offset) override;
    void InstallYield() override;

private:
    void UpdateActions();
    void CancelSession();
    void StartInstall();
    void ConfirmInstallPlan();
    void CycleSelectedTarget();
    void AddLog(const std::string& text);
    static auto TargetName(InstallTarget target) -> std::string;

    std::unique_ptr<yati::source::DbiUsb> m_usb_source{};
    fs::Fs* m_local_fs{};
    std::vector<fs::FsPath> m_local_paths{};
    std::vector<s64> m_local_source_sizes{};
    bool m_defer_local_analysis{};
    std::unique_ptr<List> m_list{};
    std::unique_ptr<List> m_log_list{};
    bool m_was_mtp_enabled{};

    Thread m_thread{};
    bool m_thread_created{};
    Mutex m_mutex{};
    UEvent m_cancel_event{};
    std::atomic<State> m_state{State::WaitingForUsb};
    std::atomic_bool m_install_requested{};
    std::atomic_bool m_cancel_requested{};
    std::atomic_bool m_actions_dirty{true};

    std::vector<QueueEntry> m_queue{};
    std::vector<std::string> m_log{};
    s64 m_index{};
    s64 m_log_index{};
    s64 m_log_last_seen_size{};
    bool m_session_failed{};
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
    size_t m_success_count{};
    size_t m_failure_count{};

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
};

} // namespace sphaira::ui::menu::dbi
