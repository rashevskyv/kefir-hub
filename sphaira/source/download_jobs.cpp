#include "download_jobs.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/menus/grid_menu_base.hpp"

#include <cstring>
#include <memory>
#include <strings.h>

namespace sphaira::jobs {
namespace {

struct Outcome {
    int downloaded{};
    int skipped{};
    int failed{};
    std::string last_error;
};

auto BaseName(const std::string& path) -> std::string {
    const auto slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

auto FileSizeOnSd(fs::Fs& fs, const fs::FsPath& path) -> s64 {
    fs::File f;
    if (R_FAILED(fs.OpenFile(path, FsOpenMode_Read, &f))) {
        return -1;
    }
    s64 size{};
    if (R_FAILED(f.GetSize(&size))) {
        return -1;
    }
    return size;
}

// `done` of `total` bytes of the whole list were fetched before this job: the second bar of the box follows the whole list
auto Fetch(ui::ProgressBox* pbox, fs::Fs& fs, const Job& job, Outcome& out, std::uint64_t done, std::uint64_t total) -> Result {
    const fs::FsPath dest{job.dest};
    if (job.size && FileSizeOnSd(fs, dest) == static_cast<s64>(job.size)) {
        out.skipped++;
        R_SUCCEED();
    }

    const fs::FsPath part{job.dest + ".part"};
    fs.CreateDirectoryRecursivelyWithPath(dest);
    fs.DeleteFile(part);
    pbox->NewTransfer(BaseName(job.dest));
    const auto result = curl::Api().ToFile(
        curl::Url{job.url},
        curl::Path{part},
        curl::OnProgress{[pbox, done, total](s64 dltotal, s64 dlnow, s64, s64) {
            if (pbox->ShouldExit()) {
                return false;
            }
            pbox->UpdateTransfer(dlnow, dltotal);
            if (total) {
                pbox->SetOverall(static_cast<s64>(done) + dlnow, static_cast<s64>(total));
            }
            return true;
        }}
    );
    R_TRY(pbox->ShouldExitResult());
    const auto got = FileSizeOnSd(fs, part);
    if (!result.success || got < 0 || (job.size && got != static_cast<s64>(job.size))) {
        log_write("[jobs] %s: http %ld, size %lld (want %llu)\n", job.dest.c_str(), result.code,
            static_cast<long long>(got), static_cast<unsigned long long>(job.size));
        fs.DeleteFile(part);
        out.failed++;
        out.last_error = BaseName(job.dest);
        R_SUCCEED();
    }

    if (!job.hash.empty()) {
        std::string hex;
        R_TRY(hash::Hash(pbox, job.hash_type, &fs, part, hex));
        if (strncasecmp(hex.c_str(), job.hash.c_str(), job.hash.size()) != 0) {
            log_write("[jobs] %s: hash %s, want %s\n", job.dest.c_str(), hex.c_str(), job.hash.c_str());
            fs.DeleteFile(part);
            out.failed++;
            out.last_error = BaseName(job.dest);
            R_SUCCEED();
        }
    }

    fs.DeleteFile(dest);
    R_TRY(fs.RenameFile(part, dest));
    out.downloaded++;
    R_SUCCEED();
}

} // namespace

auto Describe(const std::vector<Job>& jobs) -> std::string {
    return std::to_string(jobs.size()) + " " + "files"_i18n + ", " + ui::menu::grid::FormatBytes(TotalSize(jobs));
}

void Run(const std::string& title, std::vector<Job> jobs, std::function<void()> on_done) {
    auto out = std::make_shared<Outcome>();
    App::Push<ui::ProgressBox>(0, title, "", [jobs = std::move(jobs), out](auto pbox) -> Result {
        fs::FsNativeSd fs;
        R_TRY(fs.GetFsOpenResult());
        // the second bar is for a list of several files: by bytes when the sizes are known, else by the file count
        const auto total = TotalSize(jobs);
        std::uint64_t done = 0;
        for (size_t i = 0; i < jobs.size(); i++) {
            R_TRY(pbox->ShouldExitResult());
            pbox->SetTitle(std::to_string(i + 1) + " / " + std::to_string(jobs.size()));
            if (jobs.size() > 1) {
                pbox->SetOverall(total ? static_cast<s64>(done) : static_cast<s64>(i), total ? static_cast<s64>(total) : static_cast<s64>(jobs.size()));
            }
            R_TRY(Fetch(pbox, fs, jobs[i], *out, done, jobs.size() > 1 ? total : 0));
            done += jobs[i].size;
        }
        R_SUCCEED();
    }, [out, title, on_done](Result rc) {
        log_write("[jobs] %s: %d downloaded, %d skipped, %d failed, rc 0x%X\n", title.c_str(),
            out->downloaded, out->skipped, out->failed, rc);
        ON_SCOPE_EXIT(if (on_done) on_done());
        if (rc == Result_TransferCancelled) {
            return;
        }
        App::PushErrorBox(rc, "Download failed"_i18n);
        if (R_FAILED(rc)) {
            return;
        }
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%d %s, %d %s, %d %s",
            out->downloaded, "downloaded"_i18n.c_str(), out->skipped, "already there"_i18n.c_str(),
            out->failed, "failed"_i18n.c_str());
        if (out->failed) {
            App::Push<ui::OptionBox>(std::string{buf} + "\n" + "Last failed file: "_i18n + out->last_error, "OK"_i18n);
        } else {
            App::Notify(buf);
        }
    });
}

} // namespace sphaira::jobs
