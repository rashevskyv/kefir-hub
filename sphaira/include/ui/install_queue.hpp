#pragma once

#include "ui/progress_box.hpp"

#include <deque>
#include <functional>
#include <string>

namespace sphaira::ui {

// installs run one after the other. the running one is the app's transfer box (App::PushTransfer):
// R3 puts it in the corner badge, and the next titles are queued from wherever the user goes.
//
// everything here is touched from the main thread only: the box's worker thread never reaches the
// queue, and the box's done callback runs in its destructor, which App destroys on the main thread.
class InstallQueue final {
public:
    enum class State { Waiting, Running, Done, Failed, Cancelled };

    struct Job {
        std::string name{};
        int image{};
        ProgressBoxCallback callback{};
        // when the job ends, whatever the result: pages that show what is installed ask the console again
        std::function<void()> finished{};
        State state{State::Waiting};
        Result rc{};
        u64 id{};
    };

    static auto Get() -> InstallQueue&;

    // queues a job, starting it right away when nothing runs.
    void Add(const std::string& name, int image, const ProgressBoxCallback& callback, const std::function<void()>& finished = {});

    // called once per frame by the app: starts the next waiting job when the transfer slot is free.
    void Update();
    // stops the running install and drops what waits, on exit.
    void Shutdown();

    // true while an install runs or waits.
    auto IsBusy() const -> bool;
    auto GetPendingCount() const -> size_t;

    // the queue screen.
    void ShowMenu();

private:
    void Start(Job& job);
    void CancelRunning(u64 id);
    auto GetRunning() -> Job*;
    auto Find(u64 id) -> Job*;
    static auto StateName(State state) -> std::string;

private:
    std::deque<Job> m_jobs{};
    // the running job's box. App owns it; null once its done callback ran.
    ProgressBox* m_box{};
    u64 m_next_id{1};
    bool m_minimized{};      // the running box is in the corner badge (read each frame, the callback needs it)
    bool m_cancel_pending{}; // the running job was cancelled by the user
    bool m_shutdown{};
};

} // namespace sphaira::ui
