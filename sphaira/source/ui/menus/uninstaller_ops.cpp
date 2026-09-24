#include "ui/menus/uninstaller_menu.hpp"
#include "meminfo.hpp"

#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/sidebar.hpp"
#include "app.hpp"
#include "app_paths.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "path_util.hpp"
#include "utils/utils.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <cstring>
#include <strings.h>
#include <string_view>
#include <yyjson.h>
#include <switch.h>
#include <switch/services/pm.h>

namespace sphaira::ui::menu::hats {

namespace {

constexpr const char* ATMOSPHERE_CONTENTS_PATH = "/atmosphere/contents";
constexpr u64 TESLA_MENU_PROGRAM_ID = 0x420000000007E51AULL;
constexpr u64 SYS_PATCH_PROGRAM_ID = 0x420000000000000BULL;
constexpr u64 FUNCONTROL_PROGRAM_ID = 0x00FF46554E43544CULL;
constexpr u64 FUNCONTROL_OLD_PROGRAM_ID = 0x00FF000053504846ULL;
constexpr const char* MODULE_CATALOG_ROMFS_PATH = "romfs:/modules/homebrew_sysmodules.json";
constexpr const char* MODULE_INDEX_URL = "https://gist.githubusercontent.com/ndeadly/a4b8c01bb453028cd0008f282098f696/raw/homebrew_sysmodules.txt";

struct ModuleCatalogEntry {
    std::string name;
    std::string repository;
};

using ModuleCatalog = std::unordered_map<std::string, ModuleCatalogEntry>;

auto FormatProgramId(u64 program_id) -> std::string {
    char out[17]{};
    std::snprintf(out, sizeof(out), "%016" PRIX64, program_id);
    return out;
}

auto ModuleFolder(u64 program_id) -> fs::FsPath {
    fs::FsPath path;
    std::snprintf(path, sizeof(path), "%s/%s", ATMOSPHERE_CONTENTS_PATH, FormatProgramId(program_id).c_str());
    return path;
}

auto ToolboxPath(const char* folder_name) -> fs::FsPath {
    fs::FsPath path;
    std::snprintf(path, sizeof(path), "%s/%s/toolbox.json", ATMOSPHERE_CONTENTS_PATH, folder_name);
    return path;
}

auto Boot2FlagFolder(u64 program_id) -> fs::FsPath {
    fs::FsPath path;
    std::snprintf(path, sizeof(path), "%s/flags", ModuleFolder(program_id).s);
    return path;
}

auto Boot2FlagPath(u64 program_id) -> fs::FsPath {
    fs::FsPath path;
    std::snprintf(path, sizeof(path), "%s/boot2.flag", Boot2FlagFolder(program_id).s);
    return path;
}

auto JsonString(yyjson_val* object, const char* key) -> std::string {
    auto* val = object ? yyjson_obj_get(object, key) : nullptr;
    if (!val || !yyjson_is_str(val)) {
        return {};
    }

    return yyjson_get_str(val);
}

auto JsonBool(yyjson_val* object, const char* key) -> bool {
    auto* val = object ? yyjson_obj_get(object, key) : nullptr;
    return val && yyjson_is_bool(val) && yyjson_get_bool(val);
}

auto ParseProgramId(std::string_view text, u64& out) -> bool {
    if (text.empty()) {
        return false;
    }

    const std::string s{text};
    char* end{};
    out = std::strtoull(s.c_str(), &end, 16);
    return end && *end == '\0' && out != 0;
}

auto ParseModuleCatalog(const std::vector<u8>& data, ModuleCatalog& out) -> bool {
    auto* doc = yyjson_read(reinterpret_cast<const char*>(data.data()), data.size(), YYJSON_READ_NOFLAG);
    if (!doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    auto* root = yyjson_doc_get_root(doc);
    auto* modules = root && yyjson_is_obj(root) ? yyjson_obj_get(root, "modules") : nullptr;
    if (!modules || !yyjson_is_obj(modules)) {
        return false;
    }

    ModuleCatalog parsed;
    yyjson_val* key;
    yyjson_val* value;
    size_t index, count;
    yyjson_obj_foreach(modules, index, count, key, value) {
        const char* tid = yyjson_get_str(key);
        u64 program_id{};
        if (!tid || !yyjson_is_obj(value) || !ParseProgramId(tid, program_id)) {
            return false;
        }

        const auto normalized_tid = FormatProgramId(program_id);
        if (normalized_tid != tid) {
            return false;
        }

        ModuleCatalogEntry entry{
            .name = JsonString(value, "name"),
            .repository = JsonString(value, "repository"),
        };
        if (entry.name.empty() || entry.repository.empty()) {
            return false;
        }
        parsed.emplace(normalized_tid, std::move(entry));
    }

    if (parsed.empty()) {
        return false;
    }
    out = std::move(parsed);
    return true;
}

auto LoadModuleCatalog(fs::Fs& fs, const fs::FsPath& path, ModuleCatalog& out) -> bool {
    std::vector<u8> data;
    return R_SUCCEEDED(fs.read_entire_file(path, data)) && ParseModuleCatalog(data, out);
}

auto ParseModuleIndex(const std::vector<u8>& data, std::unordered_map<std::string, std::string>& out) -> bool {
    std::istringstream input{std::string{reinterpret_cast<const char*>(data.data()), data.size()}};
    std::unordered_map<std::string, std::string> parsed;
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream fields{line};
        std::string tid;
        std::string name;
        if (!(fields >> tid >> name)) {
            continue;
        }
        if (tid.starts_with("/*") || tid.starts_with("#")) {
            continue;
        }

        u64 program_id{};
        if (!ParseProgramId(tid, program_id) || FormatProgramId(program_id) != tid || name.empty()) {
            return false;
        }
        parsed.insert_or_assign(tid, name);
    }

    // Reject empty or obviously truncated responses before replacing a good cache.
    if (parsed.size() < 50) {
        return false;
    }
    out = std::move(parsed);
    return true;
}

auto LoadModuleIndex(fs::Fs& fs, const fs::FsPath& path, std::unordered_map<std::string, std::string>& out) -> bool {
    std::vector<u8> data;
    return R_SUCCEEDED(fs.read_entire_file(path, data)) && ParseModuleIndex(data, out);
}

auto LoadPreferredModuleCatalog() -> ModuleCatalog {
    ModuleCatalog catalog;
    fs::FsStdio stdio;
    if (!LoadModuleCatalog(stdio, MODULE_CATALOG_ROMFS_PATH, catalog)) {
        log_write("[MODULES] failed to load built-in catalog\n");
    }

    std::unordered_map<std::string, std::string> online_index;
    fs::FsNativeSd sd;
    if (LoadModuleIndex(sd, paths::MODULE_INDEX, online_index)) {
        for (auto& [tid, name] : online_index) {
            catalog[tid].name = std::move(name);
        }
    }
    return catalog;
}

auto ModuleDescription(u64 program_id) -> std::string {
    const auto key = "module." + FormatProgramId(program_id) + ".description";
    auto description = i18n::get(key);
    if (description == key) {
        description = "No description provided"_i18n;
    }
    return description;
}

auto ParseToolbox(const std::vector<u8>& data, ModuleItem& out) -> bool {
    auto* doc = yyjson_read(reinterpret_cast<const char*>(data.data()), data.size(), YYJSON_READ_NOFLAG);
    if (!doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    auto* root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        return false;
    }

    const auto tid = JsonString(root, "tid");
    if (!ParseProgramId(tid, out.program_id)) {
        return false;
    }

    out.program_id_text = FormatProgramId(out.program_id);
    out.name = JsonString(root, "name");
    if (out.name.empty()) {
        out.name = out.program_id_text;
    }
    out.requires_reboot = JsonBool(root, "requires_reboot");
    return true;
}

void QueryRuntime(ModuleItem& item) {
    item.running = false;
    item.memory_bytes = 0;

    Result rc = pmshellInitialize();
    if (R_FAILED(rc)) {
        return;
    }
    ON_SCOPE_EXIT(pmshellExit());

    u64 pid{};
    if (R_FAILED(pmshellGetProcessId(&pid, item.program_id))) {
        return;
    }
    item.running = true;
    item.memory_bytes = meminfo::MeasurePid(pid);
}

auto NameStartsWith(const std::string& name, const char* prefix) -> bool {
    const auto n = std::strlen(prefix);
    return name.size() >= n && strncasecmp(name.c_str(), prefix, n) == 0;
}

enum class TogglePolicy {
    Normal,
    BootOnly,
    AppManaged,
};

auto GetTogglePolicy(const ModuleItem& item) -> TogglePolicy {
    if (item.program_id == SYS_PATCH_PROGRAM_ID || NameStartsWith(item.name, "sys-patch")) {
        return TogglePolicy::BootOnly;
    }
    if (item.program_id == FUNCONTROL_PROGRAM_ID ||
        item.program_id == FUNCONTROL_OLD_PROGRAM_ID ||
        NameStartsWith(item.name, "FunControl")) {
        return TogglePolicy::AppManaged;
    }
    return TogglePolicy::Normal;
}

auto LaunchModule(u64 program_id) -> Result {
    Result rc = pmshellInitialize();
    R_TRY(rc);

    const NcmProgramLocation location{
        .program_id = program_id,
        .storageID = NcmStorageId_None,
    };
    u64 pid{};
    rc = pmshellLaunchProgram(PmLaunchFlag_None, &location, &pid);
    pmshellExit();
    return rc;
}

auto TerminateModule(u64 program_id) -> Result {
    Result rc = pmshellInitialize();
    R_TRY(rc);

    rc = pmshellTerminateProgram(program_id);
    pmshellExit();
    return rc;
}

auto SetAutostart(fs::FsNativeSd& fs, u64 program_id, bool enabled) -> Result {
    const auto flag_path = Boot2FlagPath(program_id);
    if (enabled) {
        R_TRY(fs.CreateDirectoryRecursively(Boot2FlagFolder(program_id)));
        if (!fs.FileExists(flag_path)) {
            R_TRY(fs.CreateFile(flag_path, 0, 0));
        }
    } else if (fs.FileExists(flag_path)) {
        R_TRY(fs.DeleteFile(flag_path));
    }

    R_SUCCEED();
}

} // namespace

auto GetModuleName(u64 program_id) -> std::string {
    // ponytail: catalog read once per launch, the Module Manager's own refresh
    // loads its own copy. Reload here too if stale names ever matter.
    static const auto catalog = LoadPreferredModuleCatalog();
    const auto program_id_text = FormatProgramId(program_id);

    fs::FsNativeSd fs;
    std::vector<u8> data;
    ModuleItem item;
    if (R_SUCCEEDED(fs.read_entire_file(ToolboxPath(program_id_text.c_str()), data)) &&
        ParseToolbox(data, item) && item.name != item.program_id_text) {
        return item.name;
    }

    if (const auto it = catalog.find(program_id_text); it != catalog.end()) {
        return it->second.name;
    }

    return {};
}


void UninstallerMenu::LoadModules() {
    m_items.clear();
    m_view.clear();
    m_error_message.clear();

    fs::FsNativeSd fs;
    const auto catalog = LoadPreferredModuleCatalog();

    fs::Dir dir;
    Result rc = fs.OpenDirectory(ATMOSPHERE_CONTENTS_PATH, FsDirOpenMode_ReadDirs | FsDirOpenMode_NoFileSize, &dir);
    if (R_FAILED(rc)) {
        m_error_message = "No Atmosphere contents folder found"_i18n;
        m_loaded = true;
        log_write("[MODULES] failed to open %s: 0x%X\n", ATMOSPHERE_CONTENTS_PATH, rc);
        return;
    }

    std::vector<FsDirectoryEntry> entries;
    rc = dir.ReadAll(entries);
    if (R_FAILED(rc)) {
        m_error_message = "Failed to scan sysmodules"_i18n;
        m_loaded = true;
        log_write("[MODULES] failed to read %s: 0x%X\n", ATMOSPHERE_CONTENTS_PATH, rc);
        return;
    }

    for (const auto& entry : entries) {
        const auto toolbox_path = ToolboxPath(entry.name);
        if (!fs.FileExists(toolbox_path)) {
            continue;
        }

        std::vector<u8> data;
        rc = fs.read_entire_file(toolbox_path, data);
        if (R_FAILED(rc)) {
            log_write("[MODULES] failed to read %s: 0x%X\n", toolbox_path.s, rc);
            continue;
        }

        ModuleItem item;
        if (!ParseToolbox(data, item)) {
            log_write("[MODULES] failed to parse %s\n", toolbox_path.s);
            continue;
        }

        if (item.program_id == TESLA_MENU_PROGRAM_ID) {
            continue;
        }

        if (const auto it = catalog.find(item.program_id_text); it != catalog.end()) {
            if (item.name == item.program_id_text) {
                item.name = it->second.name;
            }
            item.repository = it->second.repository;
        }
        item.description = ModuleDescription(item.program_id);

        item.autostart = fs.FileExists(Boot2FlagPath(item.program_id));
        QueryRuntime(item);
        m_items.push_back(std::move(item));
    }

    m_system = meminfo::QuerySystem();
    m_loaded = true;
    SortItems();
    log_write("[MODULES] loaded %zu toolbox sysmodules\n", m_items.size());
}

void UninstallerMenu::RequestCatalogUpdate(bool force) {
    if (m_catalog_update_pending || (m_catalog_update_attempted && !force)) {
        return;
    }

    m_catalog_update_attempted = true;
    m_catalog_update_pending = true;
    const auto queued = curl::Api().ToFileAsync(
        curl::Url{MODULE_INDEX_URL},
        curl::Path{paths::MODULE_INDEX_DOWNLOAD},
        curl::StopToken{this->GetToken()},
        curl::OnComplete{[this](auto& result) {
            m_catalog_update_pending = false;

            fs::FsNativeSd fs;
            if (!result.success) {
                fs.DeleteFile(paths::MODULE_INDEX_DOWNLOAD);
                log_write("[MODULES] online catalog unavailable; using cached or built-in catalog\n");
                return;
            }

            std::unordered_map<std::string, std::string> downloaded;
            if (!LoadModuleIndex(fs, paths::MODULE_INDEX_DOWNLOAD, downloaded)) {
                fs.DeleteFile(paths::MODULE_INDEX_DOWNLOAD);
                log_write("[MODULES] rejected invalid online module index\n");
                return;
            }

            fs.DeleteFile(paths::MODULE_INDEX);
            if (R_FAILED(fs.RenameFile(paths::MODULE_INDEX_DOWNLOAD, paths::MODULE_INDEX))) {
                fs.DeleteFile(paths::MODULE_INDEX_DOWNLOAD);
                log_write("[MODULES] failed to replace cached module index\n");
                return;
            }

            log_write("[MODULES] updated online module index (%zu entries)\n", downloaded.size());
            LoadModules();
        }}
    );

    if (!queued) {
        m_catalog_update_pending = false;
    }
}

void UninstallerMenu::RefreshStatuses() {
    fs::FsNativeSd fs;

    for (auto& item : m_items) {
        item.autostart = fs.FileExists(Boot2FlagPath(item.program_id));
        QueryRuntime(item);
    }
    m_system = meminfo::QuerySystem();
    const auto keep = HasCurrent() ? Current().program_id : 0;
    if (m_sort == ModuleSort::Running || m_sort == ModuleSort::Autostart) {
        SortItems(keep);
    } else {
        RebuildView(keep);
    }
}

void UninstallerMenu::ToggleSelectedModule() {
    if (!HasCurrent()) {
        return;
    }

    auto& item = Current();
    switch (GetTogglePolicy(item)) {
        case TogglePolicy::BootOnly:
            App::Push<OptionBox>(
                "This module cannot be turned on here. It starts when the console boots and does not need to be started again."_i18n,
                "OK"_i18n);
            return;
        case TogglePolicy::AppManaged:
            App::Push<OptionBox>(
                "FunControl is started by Kefir Hub when you use the fan curve, then stopped automatically. You do not need to turn it on here."_i18n,
                "OK"_i18n);
            return;
        case TogglePolicy::Normal:
            break;
    }

    Result rc{};
    if (item.requires_reboot) {
        App::Notify("This module applies after reboot. Use autostart."_i18n);
        RefreshStatuses();
        return;
    } else if (item.running) {
        rc = TerminateModule(item.program_id);
        if (R_SUCCEEDED(rc)) {
            App::Notify("Module stopped"_i18n);
        }
    } else {
        rc = LaunchModule(item.program_id);
        if (R_SUCCEEDED(rc)) {
            App::Notify("Module started"_i18n);
        }
    }

    if (R_FAILED(rc)) {
        App::Push<OptionBox>(item.running
            ? "Could not stop this module."_i18n
            : "Could not start this module. Some modules only load when the console boots — use autostart."_i18n,
            "OK"_i18n);
    }
    RefreshStatuses();
}

void UninstallerMenu::ToggleSelectedAutostart() {
    if (!HasCurrent()) {
        return;
    }

    auto& item = Current();
    if (GetTogglePolicy(item) == TogglePolicy::AppManaged) {
        App::Push<OptionBox>(
            "FunControl is started by Kefir Hub when you use the fan curve, then stopped automatically. You do not need autostart."_i18n,
            "OK"_i18n);
        return;
    }
    fs::FsNativeSd fs;

    const auto enable = !item.autostart;
    const auto rc = SetAutostart(fs, item.program_id, enable);
    if (R_FAILED(rc)) {
        App::PushErrorBox(rc, "Failed to toggle module autostart"_i18n);
        return;
    }

    App::Notify(enable ? "Module autostart enabled"_i18n : "Module autostart disabled"_i18n);
    RefreshStatuses();
}


} // namespace sphaira::ui::menu::hats
