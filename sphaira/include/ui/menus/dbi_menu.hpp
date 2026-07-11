#pragma once

#include "ui/install_progress.hpp"
#include "ui/list.hpp"
#include "ui/menus/menu_base.hpp"
#include "yati/source/usb_dbi.hpp"
#include "yati/yati.hpp"

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
};

struct Menu final : MenuBase, InstallProgress {
    Menu(u32 flags);
    ~Menu();

    auto GetShortTitle() const -> const char* override { return "DBI"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void ThreadFunction();

    Result CheckCancelled() override;
    UEvent* GetInstallCancelEvent() override { return &m_cancel_event; }
    void SetInstallTitle(const std::string& title) override;
    void SetInstallImage(std::vector<u8>&) override {}
    void SetInstallTransfer(const std::string& transfer) override;
    void UpdateInstallTransfer(s64 offset, s64 size) override;
    void InstallYield() override;

private:
    void UpdateActions();
    void CancelSession();
    void StartInstall();
    void CycleSelectedTarget();
    void AddLog(const std::string& text);
    static auto TargetName(InstallTarget target) -> std::string;

    std::unique_ptr<yati::source::DbiUsb> m_usb_source{};
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
    std::string m_current_title{};
    std::string m_current_transfer{};
    s64 m_progress_offset{};
    s64 m_progress_size{};
    s64 m_progress_last_offset{};
    s64 m_progress_speed{};
    TimeStamp m_progress_timestamp{};
    size_t m_current_package{};
    size_t m_success_count{};
    size_t m_failure_count{};
};

} // namespace sphaira::ui::menu::dbi
