// DOCS_DEMO builds only: install screens frozen mid-session for docs screenshots (scenes in demo_scene.cpp).
// DemoSession is an InstallSession whose fields are filled once and whose Update does nothing: no worker
// thread, no source, nothing is written. The queue review is the real dbi::Menu over fixture paths with
// deferred analysis (no file is opened before the user confirms, and a scene never confirms).

#include "demo/demo_scene.hpp"
#include "app.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "ui/menus/dbi_menu.hpp"

#include <cmath>

namespace sphaira::demo {
namespace {

using namespace ui::menu::dbi;

constexpr s64 MiB = 1024ll * 1024;
constexpr s64 GiB = 1024 * MiB;

struct Package {
    const char* file;
    s64 size;      // on the source (0 = a stream that does not say)
    s64 install;   // written to storage
    bool failed;   // analysis failed
};

constexpr Package SD_QUEUE[] = {
    {"Stonks Tycoon [0100DE0000010000][v0].nsp", 3 * GiB, 3 * GiB, false},
    {"Borshch Royale [0100DE0000030000][v0].nsz", 4 * GiB + 300 * MiB, 7 * GiB, false},
    {"Galaxy Brain Academy [0100DE0000040000][v0].xci", 2 * GiB, 2 * GiB, false},
    {"99% Loading Simulator (broken copy).nsp", 950 * MiB, 0, true},
};

struct DemoSession final : InstallSession {
    DemoSession(const std::string& title, TransportOrigin origin) : InstallSession{title, 0, origin} {}

    // frozen: no sampling, no timeouts, no input.
    void Update(Controller*, TouchInfo*) override {}

    void AddPackage(const Package& p, bool sd = true) {
        QueueEntry e{};
        e.file_name = p.file;
        e.analysis.source_size = e.source_size = p.size;
        e.analysis.install_size = p.install;
        e.analysis_result = p.failed ? MAKERESULT(Module_Libnx, LibnxError_NotFound) : 0;
        e.selected = e.install_selected = !p.failed;
        e.planned_sd = e.install_sd = sd;
        m_queue.push_back(std::move(e));
    }

    // ~48 s of history: write around `write_mib` MiB/s, the source a little slower (compressed packages).
    void FillGraph(double write_mib, double read_mib) {
        for (size_t i = 0; i < SPEED_HISTORY; i++) {
            const double wobble = std::sin(i * 0.37) * 0.08 + std::sin(i * 1.9) * 0.04;
            m_write_history[i] = static_cast<s64>(write_mib * (1.0 + wobble) * MiB);
            m_read_history[i] = static_cast<s64>(read_mib * (1.0 - wobble * 0.5) * MiB);
        }
        m_history_count = SPEED_HISTORY;
        m_history_index = 0;
        m_peak_write_bps = static_cast<s64>(write_mib * 1.15 * MiB);
    }

    void StartScreensaver() { m_screensaver.Start(); }

    // the queue has ended: totals for the summary panel, then the summary footer.
    void Finish(size_t failed, u64 elapsed_s) {
        m_stats.failed = failed;
        TransitionToSummary();
        m_stats.elapsed_ns = elapsed_s * 1000000000ull;
        UpdateActions();
    }

    // a stream with no known total: only the bytes written so far.
    void SetWritten(s64 bytes) { m_total_write = bytes; m_total_read = bytes; }

    void Installing(size_t current, s64 package_written, const std::string& transfer) {
        m_state = State::Installing;
        m_current_package = current;
        for (size_t i = 0; i < m_queue.size(); i++) {
            if (i < current && m_queue[i].install_selected) {
                m_queue[i].installed = true;
                m_queue[i].install_result = 0;
                m_plan_done_bytes += m_queue[i].analysis.install_size;
                m_stats.installed++;
                m_stats.write_bytes += m_queue[i].analysis.install_size;
                m_stats.sd_bytes += m_queue[i].analysis.install_size;
                m_stats.read_bytes += m_queue[i].analysis.source_size;
            }
            if (m_queue[i].install_selected) {
                m_plan_total_bytes += m_queue[i].analysis.install_size;
            }
        }
        m_package_write_start = m_plan_done_bytes;
        m_total_write = m_plan_done_bytes + package_written;
        m_total_read = m_total_write.load() * 6 / 10;
        if (current < m_queue.size()) {
            m_current_title = m_queue[current].file_name;
            m_progress_size = m_queue[current].analysis.install_size;
        }
        m_progress_offset = package_written;
        m_current_transfer = transfer;
    }
};

void AddSdLog(DemoSession& s, bool finished) {
    s.AddLog("Queue confirmed: 3 packages, 12.00 GB to microSD", LogKind::Event);
    s.AddLog("99% Loading Simulator (broken copy).nsp: analysis failed, skipped", LogKind::Error);
    s.AddLog("Installing Stonks Tycoon [0100DE0000010000][v0].nsp", LogKind::Event);
    s.AddLog("Stonks Tycoon [0100DE0000010000][v0].nsp: installed", LogKind::Success);
    s.AddLog("Installing Borshch Royale [0100DE0000030000][v0].nsz", LogKind::Event);
    s.AddLog("Decompressing NCZ, writing to microSD");
    if (finished) {
        s.AddLog("Borshch Royale [0100DE0000030000][v0].nsz: installed", LogKind::Success);
        s.AddLog("Installing Galaxy Brain Academy [0100DE0000040000][v0].xci", LogKind::Event);
        s.AddLog("Galaxy Brain Academy [0100DE0000040000][v0].xci: installed", LogKind::Success);
        s.AddLog("Queue finished", LogKind::Event);
    }
}

// the SD card queue is the dbi::Menu widget itself (title "Install queue"), so the demo is pushed the same way.
auto SdSession() -> std::unique_ptr<DemoSession> {
    auto s = std::make_unique<DemoSession>("Install queue"_i18n, TransportOrigin::Dbi);
    for (const auto& p : SD_QUEUE) {
        s->AddPackage(p);
    }
    s->FillGraph(86.0, 61.0);
    s->Installing(1, 2 * GiB + 900 * MiB, "Program NCA");
    AddSdLog(*s, false);
    s->UpdateActions();
    return s;
}


} // namespace

bool IsBrokenPackage(const std::string& file_name) {
    return file_name.find("(broken copy)") != std::string::npos;
}

void SceneInstallReview() {
    static fs::FsNativeSd sd;
    std::vector<fs::FsPath> paths;
    std::vector<s64> sizes;
    for (const auto& p : SD_QUEUE) {
        paths.emplace_back(fs::FsPath{"/games/"} + p.file);
        sizes.push_back(p.size);
    }
    App::Push<Menu>(ui::menu::MenuFlag_None, &sd, std::move(paths), std::move(sizes), true);
}

// PC Install (USB): the same queue as the PC app sends it (title and file names as from DBI Backend Qt).
void SceneInstallUsbQueue() {
    static fs::FsNativeSd sd;
    const char* files[] = {
        "Goose Delivery Service [0100DE0000110000][v0].nsp",
        "Goose Delivery Service [0100DE0000110800][v131072].nsp",
        "Bread Knight [0100DE0000120000][v0].nsz",
    };
    const s64 sizes[] = {2 * GiB, 512 * MiB, 3 * GiB + 200 * MiB};
    std::vector<fs::FsPath> paths;
    std::vector<s64> list;
    for (size_t i = 0; i < std::size(files); i++) {
        paths.emplace_back(fs::FsPath{"/games/"} + files[i]);
        list.push_back(sizes[i]);
    }
    auto menu = std::make_unique<Menu>(ui::menu::MenuFlag_None, &sd, std::move(paths), std::move(list), true);
    menu->SetTitle("PC Install (USB)"_i18n);
    App::Push(std::move(menu));
}

void SceneInstallProgress() {
    App::Push(SdSession());
}

// minimized = a background session (as when R3 is pressed): the badge stays on top, the menus under it take input.
void SceneInstallMinimized() {
    std::shared_ptr<DemoSession> s = SdSession();
    s->SetMinimized(true);
    App::PushInstallSession(std::move(s));
}

void SceneInstallScreensaver() {
    auto s = SdSession();
    s->StartScreensaver();
    App::Push(std::move(s));
}

void SceneInstallSummary() {
    auto s = std::make_unique<DemoSession>("Install queue"_i18n, TransportOrigin::Dbi);
    for (const auto& p : SD_QUEUE) {
        s->AddPackage(p);
    }
    s->FillGraph(86.0, 61.0);
    s->Installing(3, 0, "");
    s->AddError("99% Loading Simulator (broken copy).nsp", "Analysis", MAKERESULT(Module_Libnx, LibnxError_NotFound));
    AddSdLog(*s, true);
    s->Finish(1, 151);
    App::Push(std::move(s));
}

void SceneInstallStream(bool mtp) {
    auto s = std::make_shared<DemoSession>(mtp ? "MTP Install"_i18n : "FTP Install"_i18n, mtp ? TransportOrigin::Mtp : TransportOrigin::Ftp);
    s->AddPackage({mtp ? "Much Wow Racing [0100DE0000050000][v0].nsz" : "This Is Fine Office Edition [0100DE0000020000][v0].nsp", 0, 0, false});
    s->FillGraph(mtp ? 38.0 : 21.0, mtp ? 38.0 : 21.0);
    s->Installing(0, 0, "Program NCA");
    s->SetWritten(mtp ? 1 * GiB + 640 * MiB : 870 * MiB);
    s->AddLog(mtp ? "MTP: copy of Much Wow Racing [0100DE0000050000][v0].nsz started" : "FTP: upload of This Is Fine Office Edition [0100DE0000020000][v0].nsp started", LogKind::Event);
    s->AddLog("Installing to microSD");
    s->UpdateActions();
    App::PushInstallSession(std::move(s));
}

} // namespace sphaira::demo
