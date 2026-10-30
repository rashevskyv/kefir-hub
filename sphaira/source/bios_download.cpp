#include "bios_download.hpp"
#include "bios_plan.hpp"
#include "download_jobs.hpp"
#include "rom_download.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "ui/option_box.hpp"
#include "ui/menus/tools_menu.hpp"
#include "ui/menus/settings/settings_internal.hpp"

#include <yyjson.h>
#include <algorithm>
#include <functional>
#include <memory>

namespace sphaira::bios {
namespace {

constexpr const char* MANIFEST_PATH = "romfs:/bios/bios.json";

auto Str(yyjson_val* obj, const char* key) -> std::string {
    const auto v = yyjson_obj_get(obj, key);
    const auto s = v ? yyjson_get_str(v) : nullptr;
    return s ? s : "";
}

auto LoadManifest(Manifest& out) -> bool {
    // romfs is mounted only for the read (as the theme loader does): the Hub keeps it unmounted
    // so the running nro can be replaced by an update
    std::vector<u8> data;
    if (R_SUCCEEDED(romfsInit())) {
        ON_SCOPE_EXIT(romfsExit());
        fs::FsStdio().read_entire_file(MANIFEST_PATH, data);
    }
    if (data.empty()) {
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

auto ToJobs(const std::vector<Job>& plan) -> std::vector<jobs::Job> {
    std::vector<jobs::Job> out;
    out.reserve(plan.size());
    for (const auto& j : plan) {
        out.push_back({j.url, j.dest, j.sha1, hash::Type::Sha1, j.size});
    }
    return out;
}

// the question for one emulator: all its BIOS files, or only the systems it has content for
void Ask(std::shared_ptr<Manifest> m, Target target) {
    fs::FsNativeSd fs;
    auto all = ToJobs(Plan(*m, target, {}));
    const auto installed = InstalledSystems(fs, *m, target);
    const std::string name = Info(target).name;
    const std::string title = "Download BIOS for "_i18n + name;
    const std::string where = "\n" + "Folder: "_i18n + Info(target).bios_root;

    if (installed.empty()) {
        App::Push<ui::OptionBox>(
            "Download all BIOS files for "_i18n + name + " (" + jobs::Describe(all) + ")?" + where,
            "Back"_i18n, "Download"_i18n, 1, [all = std::move(all), title](auto op) {
                if (op && *op == 1) jobs::Run(title, all);
            });
        return;
    }

    auto some = ToJobs(Plan(*m, target, installed));
    const std::string what = target == Target::RetroArch ? "Only for the installed cores"_i18n : "Only for the systems it has"_i18n;
    App::Push<ui::OptionBox>(
        "Download BIOS files for "_i18n + name + "?" + where,
        "Back"_i18n, what + " (" + jobs::Describe(some) + ")", "All"_i18n + " (" + jobs::Describe(all) + ")", 1,
        [all = std::move(all), some = std::move(some), title](auto op) {
            if (op && *op == 1) jobs::Run(title, some);
            if (op && *op == 2) jobs::Run(title, all);
        });
}

void AddBiosRows(std::vector<ui::menu::settings::SettingsItem>& rows) {
    using ui::menu::settings::SettingsItem;
    auto m = std::make_shared<Manifest>();
    rows.push_back(ui::menu::settings::MakeHeader("BIOS"_i18n));

    if (!LoadManifest(*m)) {
        SettingsItem row{"The BIOS list is missing from this build"_i18n, ""};
        row.disabled = true;
        rows.push_back(std::move(row));
        return;
    }

    fs::FsNativeSd fs;
    std::string known;
    bool any = false;
    for (const auto& info : Targets()) {
        known += known.empty() ? "" : ", ";
        known += info.name;
        if (!fs.DirExists(info.detect)) {
            continue;
        }
        any = true;
        const auto plan = ToJobs(Plan(*m, info.target, {}));
        SettingsItem row{"Download BIOS for "_i18n + info.name, jobs::Describe(plan) + " → " + info.bios_root};
        row.action = [m, target = info.target]() { Ask(m, target); };
        rows.push_back(std::move(row));
    }
    if (!any) {
        SettingsItem row{"No emulator found on the SD card"_i18n, "Known: "_i18n + known};
        row.disabled = true;
        rows.push_back(std::move(row));
    }
    rows.push_back({"github.com/Abdess/retrobios", "The files are downloaded one by one on your request; the Hub ships none of them."_i18n});
}

} // namespace

void OpenEmulatorsPage() {
    // the page is rebuilt after a collection is added or removed: the old one leaves, a new one is pushed
    auto self = std::make_shared<ui::menu::tools::SystemToolsMenu*>(nullptr);
    const std::function<void()> reopen = [self]() {
        if (*self) {
            (*self)->SetPop();
        }
        OpenEmulatorsPage();
    };
    std::vector<ui::menu::settings::SettingsItem> rows;
    AddBiosRows(rows);
    rom::AddRows(rows, reopen);
    auto menu = std::make_unique<ui::menu::tools::SystemToolsMenu>("Emulators"_i18n, std::move(rows));
    *self = menu.get();
    App::Push(std::move(menu));
}

} // namespace sphaira::bios
