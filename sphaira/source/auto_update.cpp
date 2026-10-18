#include "auto_update.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "nro.hpp"
#include "path_util.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <mutex>
#include <string_view>

namespace sphaira::auto_update {
namespace {

std::mutex g_job_mutex;
Job g_job{};
bool g_notify_shown{}; // guarded by g_job_mutex
bool g_ready_prompt{}; // a silent update finished and the user was not told yet; guarded by g_job_mutex

} // namespace

auto GetJob() -> Job {
    std::lock_guard lock(g_job_mutex);
    return g_job;
}

void SetJobState(JobState state) {
    std::lock_guard lock(g_job_mutex);
    g_job.state = state;
    if (state == JobState::Downloading) {
        g_job.progress = 0.f;
    }
    if (state == JobState::Installing) {
        g_job.progress = 1.f;
    }
    if (state == JobState::Idle || state == JobState::Failed) {
        g_job.progress = 0.f;
    }
}

void SetJobProgress(float progress) {
    std::lock_guard lock(g_job_mutex);
    g_job.progress = std::clamp(progress, 0.f, 1.f);
}

void SetAvailable(std::string version, std::string url) {
    std::lock_guard lock(g_job_mutex);
    g_job.state = JobState::Available;
    g_job.progress = 0.f;
    g_job.version = std::move(version);
    g_job.url = std::move(url);
    g_notify_shown = false;
}

auto ConsumeNotifyPrompt() -> bool {
    std::lock_guard lock(g_job_mutex);
    if (g_notify_shown || g_job.state != JobState::Available) {
        return false;
    }
    g_notify_shown = true;
    return true;
}

namespace {

bool ApplyStaging(const fs::FsPath& temp_path) {
    SetJobState(JobState::Installing);
    const auto dest = ResolveInstallDestination(App::GetExePath());
    const bool replace_hbmenu = App::GetReplaceHbmenuEnable();
    log_write("[AutoUpdate] installing to %s (replace_hbmenu=%d)\n", dest.s, replace_hbmenu);
    const bool ok = InstallNroUpdate(temp_path, dest, replace_hbmenu);
    fs::FsNativeSd().DeleteFile(temp_path);
    if (ok) {
        SetJobState(JobState::Ready);
        App::SetAutoUpdateSkip("");
        log_write("[AutoUpdate] ready — next launch uses the new build\n");
    } else {
        SetJobState(JobState::Failed);
    }
    return ok;
}

void StartSilentDownload(const std::string& url, const fs::FsPath& temp_path) {
    curl::Api().ToFileAsync(
        curl::Url{url},
        curl::Path{temp_path},
        curl::StopToken{},
        curl::OnProgress{[](s64 dltotal, s64 dlnow, s64, s64) -> bool {
            if (dltotal > 0) {
                SetJobProgress(static_cast<float>(dlnow) / static_cast<float>(dltotal));
            }
            return true;
        }},
        curl::OnComplete{[temp_path](auto& result) {
            if (!result.success) {
                log_write("[AutoUpdate] download failed (code: %ld)\n", result.code);
                fs::FsNativeSd().DeleteFile(temp_path);
                SetJobState(JobState::Failed);
                return false;
            }
            if (ApplyStaging(temp_path)) {
                std::lock_guard lock(g_job_mutex);
                g_ready_prompt = true;
            }
            return true;
        }}
    );
}

void StartTransferDownload(const std::string& url, const std::string& version, const fs::FsPath& temp_path) {
    if (App::HasActiveTransfer() || App::GetProgressActive()) {
        log_write("[AutoUpdate] transfer already active, not starting\n");
        SetJobState(JobState::Available);
        App::Notify("Another transfer is in progress."_i18n);
        return;
    }

    auto pbox = std::make_unique<ui::ProgressBox>(
        0, "Updating"_i18n, std::string{APP_VERSION} + " -> " + version, // from which version to which
        [url, version, temp_path](ui::ProgressBox* pbox) -> Result {
            pbox->NewTransfer("Downloading"_i18n); // the version is already the box title
            const auto result = curl::Api().ToFile(
                curl::Url{url},
                curl::Path{temp_path},
                curl::OnProgress{pbox->OnDownloadProgressCallback()}
            );

            if (pbox->ShouldExit()) {
                fs::FsNativeSd().DeleteFile(temp_path);
                SetJobState(JobState::Available);
                R_THROW(Result_TransferCancelled);
            }

            if (!result.success) {
                log_write("[AutoUpdate] download failed (code: %ld)\n", result.code);
                fs::FsNativeSd().DeleteFile(temp_path);
                SetJobState(JobState::Failed);
                R_THROW(Result_CurlFailedEasyInit);
            }

            pbox->NewTransfer("Installing"_i18n);
            if (!ApplyStaging(temp_path)) {
                R_THROW(Result_CurlFailedEasyInit);
            }
            R_SUCCEED();
        },
        [](Result rc) {
            if (rc == Result_TransferCancelled) {
                return;
            }
            if (R_FAILED(rc)) {
                App::Notify("Failed"_i18n);
                return;
            }
            // the user accepted this update on screen: the new version starts now
            RestartIntoNew();
        }
    );

    if (!App::PushTransfer(std::move(pbox))) {
        log_write("[AutoUpdate] PushTransfer refused\n");
        SetJobState(JobState::Available);
    }
}

} // namespace

void RestartIntoNew() {
    nro_launch(ResolveInstallDestination(App::GetExePath()));
    App::Exit();
}

void Poll() {
    std::string version;
    {
        std::lock_guard lock(g_job_mutex);
        if (!g_ready_prompt || g_job.state != JobState::Ready || App::GetProgressActive()) {
            return;
        }
        g_ready_prompt = false;
        version = g_job.version;
    }
    App::Push<ui::OptionBox>(
        "Kefir Hub was updated to"_i18n + " " + version + ".\n\n" + "The new version runs after a restart. Restart now?"_i18n,
        "Later"_i18n, "Restart Kefir Hub"_i18n, 1, [](auto op) {
            if (op && *op == 1) {
                RestartIntoNew();
            }
        });
}

void StartDownload() {
    Job job;
    {
        std::lock_guard lock(g_job_mutex);
        job = g_job;
        if (job.url.empty() || (job.state != JobState::Available && job.state != JobState::Failed)) {
            return;
        }
        g_job.state = JobState::Downloading;
        g_job.progress = 0.f;
    }

    log_write("[AutoUpdate] downloading %s\n", job.version.c_str());
    fs::FsNativeSd().CreateDirectoryRecursively("/switch/sphaira/cache");

    constexpr fs::FsPath temp_path{"/switch/sphaira/cache/sphaira_update.temp"};
    const auto mode = static_cast<Mode>(App::GetAutoUpdateMode());
    if (mode == Mode::Silent) {
        StartSilentDownload(job.url, temp_path);
    } else {
        StartTransferDownload(job.url, job.version, temp_path);
    }
}

fs::FsPath ResolveInstallDestination(const fs::FsPath& running_exe_path) {
    if (!running_exe_path.empty() && strcasecmp(running_exe_path.s, "/hbmenu.nro") != 0) {
        return running_exe_path;
    }

    fs::FsNativeSd fs;
    if (fs.FileExists("/switch/kefir-hub/kefir-hub.nro")) {
        return "/switch/kefir-hub/kefir-hub.nro";
    }
    if (fs.FileExists("/switch/kefir-hub.nro")) {
        return "/switch/kefir-hub.nro";
    }
    if (fs.FileExists("/switch/sphaira/sphaira.nro")) {
        return "/switch/sphaira/sphaira.nro";
    }
    if (fs.FileExists("/switch/sphaira.nro")) {
        return "/switch/sphaira.nro";
    }

    // Running as /hbmenu.nro with no copy among the apps (the owner deleted it): update
    // /hbmenu.nro itself and do not bring the second copy back.
    if (!running_exe_path.empty()) {
        return "/hbmenu.nro";
    }

    // Default standard path
    return "/switch/kefir-hub/kefir-hub.nro";
}

namespace {

// a download cut short or an error page saved as the update must never replace a working Hub:
// the file has to start with an NRO header and be at least as long as that header says.
bool IsCompleteNro(fs::FsNativeSd& fs, const fs::FsPath& path, s64* out_size) {
    fs::File file;
    if (R_FAILED(fs.OpenFile(path, FsOpenMode_Read, &file))) {
        return false;
    }
    if (R_FAILED(file.GetSize(out_size))) {
        return false;
    }
    struct {
        NroStart start;
        NroHeader header;
    } head{};
    u64 bytes_read{};
    if (R_FAILED(file.Read(0, &head, sizeof(head), FsReadOption_None, &bytes_read)) || bytes_read != sizeof(head)) {
        return false;
    }
    if (R_FAILED(nro_verify(std::span{reinterpret_cast<const u8*>(&head), sizeof(head)}))) {
        return false;
    }
    return head.header.size >= sizeof(head) && static_cast<s64>(head.header.size) <= *out_size;
}

// copy next to the destination first, check the size, then swap: a full card or a power cut
// during the copy leaves the old file in place instead of half a new one.
bool ReplaceWithCopy(fs::FsNativeSd& fs, const fs::FsPath& dest, const fs::FsPath& src, s64 size) {
    const fs::FsPath next{std::string{dest.s} + ".new"};
    fs.DeleteFile(next);
    s64 copied{-1};
    if (R_FAILED(fs.copy_entire_file(next, src)) || !IsCompleteNro(fs, next, &copied) || copied != size) {
        log_write("[AutoUpdate] Failed to copy %s to %s (%lld of %lld bytes)\n", src.s, next.s, (long long)copied, (long long)size);
        fs.DeleteFile(next);
        return false;
    }
    fs.DeleteFile(dest);
    if (R_FAILED(fs.RenameFile(next, dest))) {
        log_write("[AutoUpdate] Failed to rename %s to %s\n", next.s, dest.s);
        return false;
    }
    return true;
}

} // namespace

bool InstallNroUpdate(const fs::FsPath& staging_path, const fs::FsPath& dest_path, bool replace_hbmenu) {
    fs::FsNativeSd fs;
    if (!fs.FileExists(staging_path)) {
        log_write("[AutoUpdate] Staging file does not exist: %s\n", staging_path.s);
        return false;
    }

    s64 file_size = 0;
    if (!IsCompleteNro(fs, staging_path, &file_size)) {
        log_write("[AutoUpdate] Staging file is not a complete NRO: %s (%lld bytes)\n", staging_path.s, (long long)file_size);
        return false;
    }

    fs.CreateDirectoryRecursivelyWithPath(dest_path);

    if (!ReplaceWithCopy(fs, dest_path, staging_path, file_size)) {
        return false;
    }

    if (replace_hbmenu && !ReplaceWithCopy(fs, "/hbmenu.nro", staging_path, file_size)) {
        log_write("[AutoUpdate] Warning: failed to copy update to /hbmenu.nro\n");
    }

    return true;
}

} // namespace sphaira::auto_update

