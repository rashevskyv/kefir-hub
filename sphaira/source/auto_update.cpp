#include "auto_update.hpp"
#include "app.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "path_util.hpp"
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
bool g_notify_shown{};

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
            ApplyStaging(temp_path);
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
        0, "Updating"_i18n, version,
        [url, version, temp_path](ui::ProgressBox* pbox) -> Result {
            pbox->NewTransfer("Downloading "_i18n + version);
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

            pbox->NewTransfer("Installing "_i18n + version);
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
            App::Notify("Ready — restart"_i18n);
        }
    );

    if (!App::PushTransfer(std::move(pbox))) {
        log_write("[AutoUpdate] PushTransfer refused\n");
        SetJobState(JobState::Available);
    }
}

} // namespace

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
    if (fs.FileExists("/switch/kefir-hub.nro")) {
        return "/switch/kefir-hub.nro";
    }
    if (fs.FileExists("/switch/sphaira/sphaira.nro")) {
        return "/switch/sphaira/sphaira.nro";
    }
    if (fs.FileExists("/switch/kefir-hub/kefir-hub.nro")) {
        return "/switch/kefir-hub/kefir-hub.nro";
    }
    if (fs.FileExists("/switch/sphaira.nro")) {
        return "/switch/sphaira.nro";
    }

    // Default standard path
    return "/switch/sphaira/sphaira.nro";
}

bool InstallNroUpdate(const fs::FsPath& staging_path, const fs::FsPath& dest_path, bool replace_hbmenu) {
    fs::FsNativeSd fs;
    if (!fs.FileExists(staging_path)) {
        log_write("[AutoUpdate] Staging file does not exist: %s\n", staging_path.s);
        return false;
    }

    fs::File file;
    if (R_FAILED(fs.OpenFile(staging_path, FsOpenMode_Read, &file))) {
        log_write("[AutoUpdate] Failed to open staging file: %s\n", staging_path.s);
        return false;
    }

    s64 file_size = 0;
    const Result rc = file.GetSize(&file_size);
    file.Close();

    if (R_FAILED(rc) || file_size < 1024) {
        log_write("[AutoUpdate] Staging file too small: %ld bytes\n", file_size);
        return false;
    }

    fs.CreateDirectoryRecursivelyWithPath(dest_path);

    // Delete existing destination file before copy to ensure clean overwrite
    fs.DeleteFile(dest_path);

    if (R_FAILED(fs.copy_entire_file(dest_path, staging_path))) {
        log_write("[AutoUpdate] Failed to copy %s to %s\n", staging_path.s, dest_path.s);
        return false;
    }

    if (replace_hbmenu) {
        fs.DeleteFile("/hbmenu.nro");
        if (R_FAILED(fs.copy_entire_file("/hbmenu.nro", staging_path))) {
            log_write("[AutoUpdate] Warning: failed to copy update to /hbmenu.nro\n");
        }
    }

    return true;
}

} // namespace sphaira::auto_update

