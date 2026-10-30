#include "bios_download.hpp"
#include "bios_plan.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "hasher.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/menus/tools_menu.hpp"
#include "ui/menus/grid_menu_base.hpp"
#include "ui/menus/settings/settings_internal.hpp"

#include <yyjson.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <strings.h>

namespace sphaira::bios {
namespace {

constexpr const char* MANIFEST_PATH = "romfs:/bios/bios.json";

auto Str(yyjson_val* obj, const char* key) -> std::string {
    const auto v = yyjson_obj_get(obj, key);
    const auto s = v ? yyjson_get_str(v) : nullptr;
    return s ? s : "";
}

auto LoadManifest(Manifest& out) -> bool {
    std::vector<u8> data;
    if (R_FAILED(fs::FsStdio().read_entire_file(MANIFEST_PATH, data)) || data.empty()) {
        log_write("[bios] no manifest at %s\n", MANIFEST_PATH);
        return false;
    }
    auto doc = yyjson_read(reinterpret_cast<const char*>(data.data()), data.size(), 0);
    if (!doc) {
        log_write("[bios] manifest is not json\n");
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));
    const auto root = yyjson_doc_get_root(doc);
    out.source = Str(root, "source");

    size_t idx, max;
    yyjson_val* v;
    const auto systems = yyjson_obj_get(root, "systems");
    yyjson_arr_foreach(systems, idx, max, v) {
        System sys{Str(v, "id"), Str(v, "name"), Str(v, "tico"), {}};
        size_t fidx, fmax;
        yyjson_val* f;
        yyjson_arr_foreach(yyjson_obj_get(v, "files"), fidx, fmax, f) {
            const auto size = yyjson_obj_get(f, "size");
            sys.files.push_back({Str(f, "dest"), Str(f, "path"), Str(f, "sha1"), size ? yyjson_get_uint(size) : 0});
        }
        out.systems.push_back(std::move(sys));
    }

    const auto cores = yyjson_obj_get(root, "cores");
    yyjson_val* key;
    yyjson_obj_foreach(cores, idx, max, key, v) {
        std::vector<std::string> ids;
        size_t sidx, smax;
        yyjson_val* s;
        yyjson_arr_foreach(v, sidx, smax, s) {
            if (const auto str = yyjson_get_str(s)) {
                ids.emplace_back(str);
            }
        }
        out.cores.emplace_back(yyjson_get_str(key), std::move(ids));
    }
    return !out.systems.empty() && !out.source.empty();
}

// the names of the folders (or files) in `dir`, no dot entries
auto ListNames(fs::Fs& fs, const fs::FsPath& dir, u32 mode) -> std::vector<std::string> {
    std::vector<std::string> out;
    fs::Dir d;
    if (R_FAILED(fs.OpenDirectory(dir, mode, &d))) {
        return out;
    }
    std::vector<FsDirectoryEntry> entries;
    if (R_FAILED(d.ReadAll(entries))) {
        return out;
    }
    for (const auto& e : entries) {
        if (e.name[0] != '.') {
            out.emplace_back(e.name);
        }
    }
    return out;
}

// the system ids this emulator has content for right now: RetroArch by its cores,
// Tico by the system folders it made. Empty = no narrowing is known.
auto InstalledSystems(fs::Fs& fs, const Manifest& m, Target target) -> std::vector<std::string> {
    std::vector<std::string> out;
    if (target == Target::RetroArch) {
        std::vector<std::string> cores;
        for (const auto& name : ListNames(fs, "/retroarch/cores", FsDirOpenMode_ReadFiles)) {
            if (auto core = CoreName(name); !core.empty()) {
                cores.push_back(std::move(core));
            }
        }
        out = SystemsOfCores(m, cores);
        log_write("[bios] retroarch: %zu cores -> %zu systems\n", cores.size(), out.size());
    } else if (target == Target::Tico) {
        auto folders = ListNames(fs, "/tico/system", FsDirOpenMode_ReadDirs);
        for (const auto& f : ListNames(fs, "/tico/roms", FsDirOpenMode_ReadDirs)) {
            folders.push_back(f);
        }
        for (const auto& sys : m.systems) {
            if (!sys.tico.empty() && std::find(folders.begin(), folders.end(), sys.tico) != folders.end()) {
                out.push_back(sys.id);
            }
        }
        log_write("[bios] tico: %zu folders -> %zu systems\n", folders.size(), out.size());
    }
    return out;
}

struct Outcome {
    int downloaded{};
    int skipped{};
    int failed{};
    std::string last_error;
};

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

// one file: skip when it is already there with the right size, else download beside it,
// check the sha1 and move it into place.
auto Fetch(ui::ProgressBox* pbox, fs::Fs& fs, const Job& job, Outcome& out) -> Result {
    const fs::FsPath dest{job.dest};
    if (FileSizeOnSd(fs, dest) == static_cast<s64>(job.size)) {
        out.skipped++;
        R_SUCCEED();
    }

    const fs::FsPath part{job.dest + ".part"};
    fs.CreateDirectoryRecursivelyWithPath(dest);
    fs.DeleteFile(part);
    pbox->NewTransfer(FileName(job.dest));
    const auto result = curl::Api().ToFile(
        curl::Url{job.url},
        curl::Path{part},
        curl::OnProgress{pbox->OnDownloadProgressCallback()}
    );
    R_TRY(pbox->ShouldExitResult());
    if (!result.success || FileSizeOnSd(fs, part) != static_cast<s64>(job.size)) {
        log_write("[bios] %s: http %ld, size %lld (want %llu)\n", job.dest.c_str(), result.code,
            static_cast<long long>(FileSizeOnSd(fs, part)), static_cast<unsigned long long>(job.size));
        fs.DeleteFile(part);
        out.failed++;
        out.last_error = FileName(job.dest);
        R_SUCCEED();
    }

    std::string sha1;
    R_TRY(hash::Hash(pbox, hash::Type::Sha1, &fs, part, sha1));
    if (strncasecmp(sha1.c_str(), job.sha1.c_str(), 40) != 0) {
        log_write("[bios] %s: sha1 %s, want %s\n", job.dest.c_str(), sha1.c_str(), job.sha1.c_str());
        fs.DeleteFile(part);
        out.failed++;
        out.last_error = FileName(job.dest);
        R_SUCCEED();
    }

    fs.DeleteFile(dest);
    R_TRY(fs.RenameFile(part, dest));
    out.downloaded++;
    R_SUCCEED();
}

void Run(Target target, std::vector<Job> jobs) {
    auto out = std::make_shared<Outcome>();
    const std::string name = Info(target).name;
    App::Push<ui::ProgressBox>(0, "Download BIOS for "_i18n + name, "", [jobs = std::move(jobs), out](auto pbox) -> Result {
        fs::FsNativeSd fs;
        R_TRY(fs.GetFsOpenResult());
        for (size_t i = 0; i < jobs.size(); i++) {
            R_TRY(pbox->ShouldExitResult());
            pbox->SetTitle(std::to_string(i + 1) + " / " + std::to_string(jobs.size()));
            R_TRY(Fetch(pbox, fs, jobs[i], *out));
        }
        R_SUCCEED();
    }, [out, name](Result rc) {
        log_write("[bios] %s: %d downloaded, %d skipped, %d failed, rc 0x%X\n", name.c_str(),
            out->downloaded, out->skipped, out->failed, rc);
        if (rc == Result_TransferCancelled) {
            return;
        }
        App::PushErrorBox(rc, "BIOS download failed"_i18n);
        if (R_FAILED(rc)) {
            return;
        }
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s: %d %s, %d %s, %d %s",
            name.c_str(), out->downloaded, "downloaded"_i18n.c_str(), out->skipped, "already there"_i18n.c_str(),
            out->failed, "failed"_i18n.c_str());
        if (out->failed) {
            App::Push<ui::OptionBox>(std::string{buf} + "\n" + "Last failed file: "_i18n + out->last_error, "OK"_i18n);
        } else {
            App::Notify(buf);
        }
    });
}

auto Describe(const std::vector<Job>& jobs) -> std::string {
    return std::to_string(jobs.size()) + " " + "files"_i18n + ", " + ui::menu::grid::FormatBytes(TotalSize(jobs));
}

// the question for one emulator: all its BIOS files, or only the systems it has content for
void Ask(std::shared_ptr<Manifest> m, Target target) {
    fs::FsNativeSd fs;
    auto all = Plan(*m, target, {});
    const auto installed = InstalledSystems(fs, *m, target);
    const std::string name = Info(target).name;
    const std::string where = "\n" + "Folder: "_i18n + Info(target).bios_root;

    if (installed.empty()) {
        App::Push<ui::OptionBox>(
            "Download all BIOS files for "_i18n + name + " (" + Describe(all) + ")?" + where,
            "Back"_i18n, "Download"_i18n, 1, [all = std::move(all), target](auto op) {
                if (op && *op == 1) Run(target, all);
            });
        return;
    }

    auto some = Plan(*m, target, installed);
    const std::string what = target == Target::RetroArch ? "Only for the installed cores"_i18n : "Only for the systems it has"_i18n;
    App::Push<ui::OptionBox>(
        "Download BIOS files for "_i18n + name + "?" + where,
        "Back"_i18n, what + " (" + Describe(some) + ")", "All"_i18n + " (" + Describe(all) + ")", 1,
        [all = std::move(all), some = std::move(some), target](auto op) {
            if (op && *op == 1) Run(target, some);
            if (op && *op == 2) Run(target, all);
        });
}

} // namespace

void OpenEmulatorsPage() {
    using ui::menu::settings::SettingsItem;
    auto m = std::make_shared<Manifest>();
    std::vector<SettingsItem> rows;
    rows.push_back(ui::menu::settings::MakeHeader("BIOS"_i18n));

    if (!LoadManifest(*m)) {
        SettingsItem row{"The BIOS list is missing from this build"_i18n, ""};
        row.disabled = true;
        rows.push_back(std::move(row));
    } else {
        fs::FsNativeSd fs;
        std::string known;
        for (const auto& info : Targets()) {
            known += known.empty() ? "" : ", ";
            known += info.name;
            if (!fs.DirExists(info.detect)) {
                continue;
            }
            const auto jobs = Plan(*m, info.target, {});
            SettingsItem row{"Download BIOS for "_i18n + info.name, Describe(jobs) + " → " + info.bios_root};
            row.action = [m, target = info.target]() { Ask(m, target); };
            rows.push_back(std::move(row));
        }
        if (rows.size() == 1) {
            SettingsItem row{"No emulator found on the SD card"_i18n, "Known: "_i18n + known};
            row.disabled = true;
            rows.push_back(std::move(row));
        }
    }

    rows.push_back(ui::menu::settings::MakeHeader("Source"_i18n));
    rows.push_back({"github.com/Abdess/retrobios", "The files are downloaded one by one on your request; the Hub ships none of them."_i18n});
    App::Push<ui::menu::tools::SystemToolsMenu>("Emulators"_i18n, std::move(rows));
}

} // namespace sphaira::bios
