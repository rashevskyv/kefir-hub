#include "ui/menus/uninstaller_menu.hpp"

#include "ui/nvg_util.hpp"
#include "app.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <strings.h>
#include <string_view>
#include <yyjson.h>
#include <switch/services/pm.h>

namespace sphaira::ui::menu::hats {

namespace {

constexpr const char* ATMOSPHERE_CONTENTS_PATH = "/atmosphere/contents";
constexpr u64 TESLA_MENU_PROGRAM_ID = 0x420000000007E51AULL;

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

auto ToolboxPath(const FsDirectoryEntry& entry) -> fs::FsPath {
    fs::FsPath path;
    std::snprintf(path, sizeof(path), "%s/%s/toolbox.json", ATMOSPHERE_CONTENTS_PATH, entry.name);
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

auto StatusText(const ModuleItem& item) -> std::string {
    if (item.running && item.autostart) {
        return "On / Auto"_i18n;
    }
    if (item.running) {
        return "On / Manual"_i18n;
    }
    if (item.autostart) {
        return "Off / Auto"_i18n;
    }
    return "Off / Manual"_i18n;
}

} // namespace

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
        }})
    );

    const Vec4 v{75, GetY() + 1.f + 78.f, 1220.f - 150.f, 66.f};
    m_list = std::make_unique<List>(1, 7, m_pos, v);
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
    // inflate the clip by the selection outline pad so the highlight of edge
    // rows isn't clipped.
    const float p = gfx::SELECTION_OUTLINE_PAD;
    nvgScissor(vg, 75.f - p, GetY() + 78.f - p, 1220.f - 150.f + p * 2, SCREEN_HEIGHT - (GetY() + 78.f) + p * 2);

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
        gfx::drawRect(vg, x + 15.f, y + 20.f, 14.f, 14.f, marker_colour, 7.f);

        gfx::drawTextArgs(vg, x + 44.f, y + h / 2.f - 9.f, 18.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
            theme->GetColour(text_id),
            "%s", item.name.c_str());

        gfx::drawTextArgs(vg, x + 44.f, y + h / 2.f + 12.f, 13.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "%s%s", item.program_id_text.c_str(), item.requires_reboot ? " - reboot required"_i18n.c_str() : "");

        gfx::drawTextArgs(vg, x + w - 15.f, y + h / 2.f, 15.f,
            NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
            theme->GetColour(text_id),
            "%s", StatusText(item).c_str());
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
}

void UninstallerMenu::SetIndex(s64 index) {
    if (m_items.empty()) {
        m_index = 0;
        SetSubHeading("No sysmodules found"_i18n);
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
        const auto toolbox_path = ToolboxPath(entry);
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
        App::Notify("Use Autostart for reboot-required modules"_i18n);
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
        SetSubHeading("No sysmodules found"_i18n);
        return;
    }

    const auto& item = m_items[m_index];
    SetSubHeading(item.name + " - " + StatusText(item));
}

} // namespace sphaira::ui::menu::hats
