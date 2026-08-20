#include "ui/menus/uninstaller_menu.hpp"

#include "ui/nvg_util.hpp"
#include "app.hpp"
#include "app_paths.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <strings.h>
#include <string_view>
#include <yyjson.h>
#include <switch/services/pm.h>

namespace sphaira::ui::menu::hats {

namespace {

constexpr const char* ATMOSPHERE_CONTENTS_PATH = "/atmosphere/contents";
constexpr u64 TESLA_MENU_PROGRAM_ID = 0x420000000007E51AULL;
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

auto IsRunning(u64 program_id) -> bool {
    Result rc = pmshellInitialize();
    if (R_FAILED(rc)) {
        return false;
    }

    u64 pid{};
    rc = pmshellGetProcessId(&pid, program_id);
    pmshellExit();
    return R_SUCCEEDED(rc);
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

UninstallerMenu::UninstallerMenu() : MenuBase{"Module Manager"_i18n, MenuFlag_None} {
    this->SetActions(
        std::make_pair(Button::A, Action{"Toggle"_i18n, [this](){
            ToggleSelectedModule();
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }}),
        std::make_pair(Button::Y, Action{"Autostart"_i18n, [this](){
            ToggleSelectedAutostart();
        }}),
        std::make_pair(Button::X, Action{"Refresh"_i18n, [this](){
            m_loaded = false;
            LoadModules();
            RequestCatalogUpdate(true);
        }})
    );

    const float list_x = 75.f;
    const float list_y = GetY() + 45.f;
    const float list_w = 1070.f;
    const float list_h = 574.f;
    const float row_h = 82.f;

    m_list = std::make_unique<List>(1, 7, Vec4{list_x, list_y, list_w, list_h}, Vec4{list_x, list_y, list_w, row_h});
    m_list->SetLayout(List::Layout::GRID);
}

UninstallerMenu::~UninstallerMenu() = default;

void UninstallerMenu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    if (!m_items.empty()) {
        m_list->OnUpdate(controller, touch, m_index, m_items.size(), [this](bool touch, auto i) {
            if (touch && m_index == i) {
                FireAction(Button::A);
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                SetIndex(i);
            }
        }, this);
    }
}

void UninstallerMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    gfx::drawTextArgs(vg, 80.f, GetY() + 10.f, 16.f,
        NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
        theme->GetColour(ThemeEntryID_TEXT_INFO),
        "A: Toggle running    Y: Toggle autostart    X: Refresh"_i18n.c_str());

    if (!m_error_message.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_ERROR),
            "%s", m_error_message.c_str());
        return;
    }

    if (m_items.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "No sysmodules found"_i18n.c_str());
        return;
    }

    nvgSave(vg);
    const float list_x = 75.f;
    const float list_y = GetY() + 45.f;
    const float list_w = 1070.f;
    const float list_h = 574.f;
    const float p = gfx::SELECTION_OUTLINE_PAD;
    nvgScissor(vg, list_x - p, list_y - p, list_w + p * 2, list_h + p * 2);

    m_list->Draw(vg, theme, m_items.size(), [this](auto* vg, auto* theme, Vec4 v, auto i) {
        const auto& [x, y, w, h] = v;
        const auto& item = m_items[i];
        const auto selected = m_index == i;

        const auto text_id = selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;
        if (selected) {
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else if (i != m_items.size() - 1) {
            gfx::drawRect(vg, x, y + h, w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
        }

        const auto marker_colour = item.running ? nvgRGBA(76, 190, 120, 255) :
            item.autostart ? nvgRGBA(216, 174, 80, 255) : theme->GetColour(ThemeEntryID_TEXT_INFO);
        gfx::drawRect(vg, x + 15.f, y + h / 2.f - 7.f, 14.f, 14.f, marker_colour, 7.f);

        gfx::drawTextArgs(vg, x + 44.f, y + h / 2.f - 11.f, 18.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
            theme->GetColour(text_id),
            "%s", item.name.c_str());

        gfx::drawTextArgs(vg, x + 44.f, y + h / 2.f + 14.f, 13.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "%s%s", item.program_id_text.c_str(), item.requires_reboot ? " - Applies after reboot"_i18n.c_str() : "");

        const auto now_text = item.running ? "Now: On"_i18n : "Now: Off"_i18n;
        const auto now_colour = item.running ? nvgRGBA(76, 190, 120, 255) : theme->GetColour(ThemeEntryID_TEXT_INFO);
        gfx::drawTextArgs(vg, x + w - 15.f, y + h / 2.f - 11.f, 14.f,
            NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
            now_colour,
            "%s", now_text.c_str());

        const auto reboot_text = item.autostart ? "After reboot: Enabled"_i18n : "After reboot: Disabled"_i18n;
        const auto reboot_colour = item.autostart ? nvgRGBA(216, 174, 80, 255) : theme->GetColour(ThemeEntryID_TEXT_INFO);
        gfx::drawTextArgs(vg, x + w - 15.f, y + h / 2.f + 14.f, 14.f,
            NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
            reboot_colour,
            "%s", reboot_text.c_str());
    });

    nvgRestore(vg);
}

void UninstallerMenu::OnFocusGained() {
    MenuBase::OnFocusGained();

    if (!m_loaded) {
        LoadModules();
    } else {
        RefreshStatuses();
    }
    RequestCatalogUpdate();
}

void UninstallerMenu::SetIndex(s64 index) {
    if (m_items.empty()) {
        m_index = 0;
        SetTitleSubHeading("No sysmodules found"_i18n, true);
        SetSubHeading("");
        return;
    }

    m_index = std::clamp<s64>(index, 0, static_cast<s64>(m_items.size() - 1));
    if (!m_index) {
        m_list->SetYoff(0);
    }
    UpdateSubheading();
}

void UninstallerMenu::LoadModules() {
    m_items.clear();
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

        if (const auto it = catalog.find(item.program_id_text); it != catalog.end() && item.name == item.program_id_text) {
            item.name = it->second.name;
        }
        item.description = ModuleDescription(item.program_id);

        item.autostart = fs.FileExists(Boot2FlagPath(item.program_id));
        item.running = IsRunning(item.program_id);
        m_items.push_back(std::move(item));
    }

    std::sort(m_items.begin(), m_items.end(), [](const ModuleItem& a, const ModuleItem& b) {
        if (a.requires_reboot != b.requires_reboot) {
            return !a.requires_reboot;
        }
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    });

    m_loaded = true;
    SetIndex(std::min<s64>(m_index, static_cast<s64>(m_items.size()) - 1));
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
        item.running = IsRunning(item.program_id);
    }
    UpdateSubheading();
}

void UninstallerMenu::ToggleSelectedModule() {
    if (m_items.empty()) {
        return;
    }

    auto& item = m_items[m_index];

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
        App::PushErrorBox(rc, "Failed to toggle module"_i18n);
    }
    RefreshStatuses();
}

void UninstallerMenu::ToggleSelectedAutostart() {
    if (m_items.empty()) {
        return;
    }

    auto& item = m_items[m_index];
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

void UninstallerMenu::UpdateSubheading() {
    if (m_items.empty()) {
        SetTitleSubHeading("No sysmodules found"_i18n, true);
        SetSubHeading("");
        return;
    }

    const auto& item = m_items[m_index];
    SetTitleSubHeading(item.description, true);
    SetSubHeading("");
}

} // namespace sphaira::ui::menu::hats
