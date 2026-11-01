#include "ui/install_queue.hpp"
#include "ui/option_box.hpp"
#include "ui/sidebar.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "i18n.hpp"

#include <algorithm>
#include <cstdio>

namespace sphaira::ui {

auto InstallQueue::Get() -> InstallQueue& {
    static InstallQueue queue;
    return queue;
}

void InstallQueue::Add(const std::string& name, int image, const ProgressBoxCallback& callback, const std::function<void()>& finished) {
    if (m_shutdown) {
        return;
    }

    const auto busy = IsBusy() || App::HasActiveTransfer();
    auto& job = m_jobs.emplace_back(Job{name, image, callback, finished});
    job.id = m_next_id++;
    log_write("[QUEUE] added %s (id %zu), busy: %u\n", name.c_str(), static_cast<size_t>(job.id), busy);

    if (busy) {
        App::Notify("Queued "_i18n + name + " (" + std::to_string(GetPendingCount()) + ")");
        return;
    }

    Start(job);
}

void InstallQueue::Start(Job& job) {
    job.state = State::Running;
    m_cancel_pending = false;
    m_minimized = false;
    log_write("[QUEUE] starting %s (id %zu)\n", job.name.c_str(), static_cast<size_t>(job.id));

    const auto id = job.id;
    auto box = std::make_unique<ProgressBox>(job.image, "Installing "_i18n, job.name, job.callback, [this, id](Result rc) {
        // runs in the box's destructor, on the main thread.
        m_box = nullptr;

        auto job = Find(id);
        if (!job) {
            return;
        }

        if (R_SUCCEEDED(rc)) {
            job->state = State::Done;
        } else if (m_cancel_pending || rc == Result_TransferCancelled) {
            job->state = State::Cancelled;
        } else {
            job->state = State::Failed;
        }
        job->rc = rc;
        m_cancel_pending = false;
        log_write("[QUEUE] finished %s (id %zu) rc: 0x%X\n", job->name.c_str(), static_cast<size_t>(id), rc);

        if (m_shutdown) {
            return;
        }

        if (job->finished) {
            job->finished();
        }

        if (job->state == State::Done) {
            App::Notify("Installed "_i18n + job->name);
        } else if (job->state == State::Failed) {
            // a box being watched reports like a direct install, one in the badge
            // can't take over the screen from whatever the user is doing.
            if (!m_minimized) {
                App::PushErrorBox(rc, "Install failed!"_i18n);
            } else {
                char buf[32];
                std::snprintf(buf, sizeof(buf), " (0x%X)", rc);
                App::Notify("Install failed!"_i18n + " " + job->name + buf);
            }
        }
    });

    m_box = box.get();
    if (!App::PushTransfer(std::move(box))) {
        // refused: the box is destroyed here and its done callback ends the job as cancelled
        m_box = nullptr;
    }
}

void InstallQueue::Update() {
    if (m_shutdown) {
        return;
    }

    if (m_box) {
        m_minimized = m_box->IsMinimized();
        return;
    }

    if (App::HasActiveTransfer()) {
        return;
    }

    auto it = std::find_if(m_jobs.begin(), m_jobs.end(), [](const auto& e) {
        return e.state == State::Waiting;
    });
    if (it != m_jobs.end()) {
        Start(*it);
    }
}

void InstallQueue::CancelRunning(u64 id) {
    auto job = GetRunning();
    if (!job || job->id != id || !m_box) {
        return;
    }

    m_cancel_pending = true;
    m_box->RequestExit();
}

void InstallQueue::Shutdown() {
    m_shutdown = true;
    std::erase_if(m_jobs, [](const auto& e) {
        return e.state == State::Waiting;
    });
    // the running box is stopped and destroyed by App with the rest of the widgets.
}

auto InstallQueue::IsBusy() const -> bool {
    return m_box || GetPendingCount();
}

auto InstallQueue::GetPendingCount() const -> size_t {
    return std::count_if(m_jobs.begin(), m_jobs.end(), [](const auto& e) {
        return e.state == State::Waiting;
    });
}

auto InstallQueue::GetRunning() -> Job* {
    auto it = std::find_if(m_jobs.begin(), m_jobs.end(), [](const auto& e) {
        return e.state == State::Running;
    });
    return it != m_jobs.end() ? &*it : nullptr;
}

auto InstallQueue::Find(u64 id) -> Job* {
    auto it = std::find_if(m_jobs.begin(), m_jobs.end(), [id](const auto& e) {
        return e.id == id;
    });
    return it != m_jobs.end() ? &*it : nullptr;
}

auto InstallQueue::StateName(State state) -> std::string {
    switch (state) {
        case State::Waiting: return "Waiting"_i18n;
        case State::Running: return "Installing"_i18n;
        case State::Done: return "Installed"_i18n;
        case State::Failed: return "Failed"_i18n;
        case State::Cancelled: return "Cancelled"_i18n;
    }
    return {};
}

void InstallQueue::ShowMenu() {
    auto options = std::make_unique<Sidebar>("Install queue"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    if (m_jobs.empty()) {
        options->Add<SidebarEntryCallback>("Nothing queued"_i18n, [](){}, "Titles you install from Ownfoil appear here."_i18n);
        return;
    }

    for (const auto& job : m_jobs) {
        const auto id = job.id;
        const auto title = "[" + StateName(job.state) + "] " + job.name;

        switch (job.state) {
            case State::Running:
                options->Add<SidebarEntryCallback>(title, [this](){
                    if (m_box && m_box->IsMinimized()) {
                        m_box->ToggleMinimized();
                    }
                }, true, "Show its progress. R3 puts it back in the corner."_i18n);
                break;

            case State::Waiting:
                options->Add<SidebarEntryCallback>(title, [this, id](){
                    std::erase_if(m_jobs, [id](const auto& e) { return e.id == id && e.state == State::Waiting; });
                }, true, "Remove it from the queue."_i18n);
                break;

            case State::Failed: {
                char info[64];
                std::snprintf(info, sizeof(info), "%s 0x%X", "Error"_i18n.c_str(), job.rc);
                options->Add<SidebarEntryCallback>(title, [](){}, std::string{info});
            }   break;

            case State::Done:
            case State::Cancelled:
                options->Add<SidebarEntryCallback>(title, [](){}, "");
                break;
        }
    }

    if (m_box) {
        options->Add<SidebarEntryCallback>("Cancel current install"_i18n, [this](){
            auto job = GetRunning();
            if (!job) {
                return;
            }

            const auto id = job->id;
            App::Push<OptionBox>("Cancel the current install?"_i18n, "No"_i18n, "Yes"_i18n, 1, [this, id](auto op_index){
                if (op_index && *op_index) {
                    CancelRunning(id);
                }
            });
        }, true);
    }

    if (GetPendingCount()) {
        options->Add<SidebarEntryCallback>("Remove all waiting"_i18n, [this](){
            std::erase_if(m_jobs, [](const auto& e) { return e.state == State::Waiting; });
        }, true);
    }

    if (std::any_of(m_jobs.begin(), m_jobs.end(), [](const auto& e) { return e.state != State::Running && e.state != State::Waiting; })) {
        options->Add<SidebarEntryCallback>("Clear finished"_i18n, [this](){
            std::erase_if(m_jobs, [](const auto& e) { return e.state != State::Running && e.state != State::Waiting; });
        }, true);
    }
}

} // namespace sphaira::ui
