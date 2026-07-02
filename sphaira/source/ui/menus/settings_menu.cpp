#include "ui/menus/settings_menu.hpp"

#include "ui/menus/appstore.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/menus/uninstaller_menu.hpp"

#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"

#include "app.hpp"
#include "download.hpp"
#include "i18n.hpp"
#include "threaded_file_transfer.hpp"
#include "utils/utils.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <minIni.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace sphaira::ui::menu::settings {
namespace {

constexpr std::array LANGUAGE_ITEMS{
    "Auto",
    "English",
    "Japanese",
    "French",
    "German",
    "Italian",
    "Spanish",
    "Chinese",
    "Korean",
    "Dutch",
    "Portuguese",
    "Russian",
    "Swedish",
    "Vietnamese",
    "Ukrainian",
};

constexpr std::array TEXT_SCROLL_SPEED_ITEMS{
    "Slow",
    "Normal",
    "Fast",
};

constexpr std::array TRANSLATION_PATHS{
    "/atmosphere/contents/010000000000080B/",
    "/atmosphere/contents/010000000000080C/",
    "/atmosphere/contents/010000000000100D/",
    "/atmosphere/contents/0100000000000803/",
    "/atmosphere/contents/0100000000000811/",
    "/atmosphere/contents/0100000000001000/romfs/message/",
    "/atmosphere/contents/0100000000001001/",
    "/atmosphere/contents/0100000000001002/",
    "/atmosphere/contents/0100000000001003/",
    "/atmosphere/contents/0100000000001004/",
    "/atmosphere/contents/0100000000001005/",
    "/atmosphere/contents/0100000000001006/",
    "/atmosphere/contents/0100000000001007/",
    "/atmosphere/contents/0100000000001008/",
    "/atmosphere/contents/0100000000001009/",
    "/atmosphere/contents/0100000000001012/",
    "/atmosphere/contents/0100000000001013/",
    "/atmosphere/contents/0100000000001015/",
};

constexpr const char* ATMOSPHERE_CONFIG = "/atmosphere/config/system_settings.ini";
constexpr const char* SPHAIRA_DOWNLOADS = "/config/sphaira/downloads";
constexpr const char* DBI_TRANSLATIONS_PACKAGE = "/config/sphaira/packages/Software/DBI/Fan Translations/package.ini";
constexpr const char* TRANSLATE_PACKAGE_DIR = "/config/sphaira/packages/Translate Interface";
constexpr const char* TRANSLATE_PACKAGE = "/config/sphaira/packages/Translate Interface/package.ini";
constexpr const char* TRANSLATE_PACKAGE_BACKUP = "/config/sphaira/packages/translate_interface.package.ini.bkp";

struct KefirSetting {
    std::string label;
    std::string description;
    std::function<bool()> get;
    std::function<Result(bool)> set;
    std::string warning_on;
    float hold_seconds{0.5f};
};

struct PackageAction {
    std::string label;
    std::string description;
    std::function<Result(ProgressBox*)> run;
    bool hold{};
    std::string warning;
    float hold_seconds{0.5f};
};

struct DbiTranslationEntry {
    std::string name;
    std::string translation_url;
};

struct InterfaceTranslationEntry {
    std::string name;
    std::string json_path;
    std::string zip_url;
};

auto ClampIndex(long index, long count) -> long {
    if (count <= 0) {
        return 0;
    }

    index %= count;
    if (index < 0) {
        index += count;
    }
    return index;
}

auto OnOff(bool enabled) -> std::string {
    return enabled ? "On"_i18n : "Off"_i18n;
}

auto Trim(std::string str) -> std::string {
    const auto first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = str.find_last_not_of(" \t\r\n");
    str = str.substr(first, last - first + 1);
    if (str.size() >= 2 && ((str.front() == '\'' && str.back() == '\'') || (str.front() == '"' && str.back() == '"'))) {
        str = str.substr(1, str.size() - 2);
    }
    return str;
}

auto ReadTextFile(const std::string& path) -> std::string {
    std::ifstream file{path};
    if (!file) {
        return {};
    }
    return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

auto ReadLines(const std::string& path) -> std::vector<std::string> {
    std::vector<std::string> lines;
    std::ifstream file{path};
    for (std::string line; std::getline(file, line);) {
        lines.emplace_back(std::move(line));
    }
    return lines;
}

auto ExtractBracketName(const std::string& line) -> std::string {
    const auto trimmed = Trim(line);
    if (trimmed.size() < 3 || trimmed.front() != '[' || trimmed.back() != ']') {
        return {};
    }

    auto name = trimmed.substr(1, trimmed.size() - 2);
    if (!name.empty() && name.front() == '*') {
        name.erase(name.begin());
    }
    return Trim(name);
}

auto StartsWith(const std::string& str, const char* prefix) -> bool {
    return str.rfind(prefix, 0) == 0;
}

auto SplitCommand(const std::string& line) -> std::vector<std::string> {
    std::vector<std::string> out;
    std::string current;
    char quote{};

    for (const auto ch : line) {
        if (quote) {
            if (ch == quote) {
                quote = 0;
            } else {
                current += ch;
            }
            continue;
        }

        if (ch == '\'' || ch == '"') {
            quote = ch;
        } else if (ch == ' ' || ch == '\t') {
            if (!current.empty()) {
                out.emplace_back(std::move(current));
                current.clear();
            }
        } else {
            current += ch;
        }
    }

    if (!current.empty()) {
        out.emplace_back(std::move(current));
    }
    return out;
}

auto ExtractJsonStringField(const std::string& json, const std::string& field) -> std::string {
    const auto key = "\"" + field + "\"";
    const auto key_pos = json.find(key);
    if (key_pos == std::string::npos) {
        return {};
    }

    const auto colon = json.find(':', key_pos + key.size());
    const auto first_quote = json.find('"', colon == std::string::npos ? key_pos : colon);
    if (first_quote == std::string::npos) {
        return {};
    }

    const auto second_quote = json.find('"', first_quote + 1);
    if (second_quote == std::string::npos) {
        return {};
    }

    return json.substr(first_quote + 1, second_quote - first_quote - 1);
}

auto SettingsValueColour(Theme* theme, const std::string& value, bool selected) -> NVGcolor {
    if (value == "On"_i18n) {
        return nvgRGBA(78, 210, 112, 255);
    }
    if (value == "Off"_i18n) {
        return nvgRGBA(135, 138, 148, 255);
    }
    return theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT_INFO);
}

auto FileExists(const char* path) -> bool {
    struct stat st {};
    return stat(path, &st) == 0;
}

auto DirectoryExists(const char* path) -> bool {
    struct stat st {};
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

auto ParentPath(const std::string& path) -> std::string {
    const auto pos = path.find_last_of('/');
    if (pos == std::string::npos || pos == 0) {
        return "/";
    }
    return path.substr(0, pos);
}

auto EnsureParentDirectory(const std::string& path) -> Result {
    fs::FsNativeSd fs;
    R_TRY(fs.GetFsOpenResult());
    R_TRY(fs.CreateDirectoryRecursively(ParentPath(path)));
    R_SUCCEED();
}

auto CopyFileSimple(const std::string& src, const std::string& dst) -> Result {
    R_TRY(EnsureParentDirectory(dst));

    FILE* in = std::fopen(src.c_str(), "rb");
    if (!in) {
        R_THROW(fsdevGetLastResult());
    }

    FILE* out = std::fopen(dst.c_str(), "wb");
    if (!out) {
        std::fclose(in);
        R_THROW(fsdevGetLastResult());
    }

    std::array<char, 64 * 1024> buf{};
    while (const auto read = std::fread(buf.data(), 1, buf.size(), in)) {
        if (std::fwrite(buf.data(), 1, read, out) != read) {
            std::fclose(out);
            std::fclose(in);
            R_THROW(fsdevGetLastResult());
        }
    }

    std::fclose(out);
    std::fclose(in);
    R_SUCCEED();
}

auto DeletePath(const std::string& path) -> Result {
    if (!FileExists(path.c_str())) {
        R_SUCCEED();
    }

    if (DirectoryExists(path.c_str())) {
        DIR* dir = opendir(path.c_str());
        if (!dir) {
            R_THROW(fsdevGetLastResult());
        }

        while (auto* ent = readdir(dir)) {
            if (!std::strcmp(ent->d_name, ".") || !std::strcmp(ent->d_name, "..")) {
                continue;
            }
            R_TRY(DeletePath(path + "/" + ent->d_name));
        }
        closedir(dir);

        if (rmdir(path.c_str()) != 0) {
            R_THROW(fsdevGetLastResult());
        }
    } else if (std::remove(path.c_str()) != 0) {
        R_THROW(fsdevGetLastResult());
    }

    R_SUCCEED();
}

auto CopyDirectoryContents(const std::string& src, const std::string& dst) -> Result {
    DIR* dir = opendir(src.c_str());
    if (!dir) {
        R_THROW(fsdevGetLastResult());
    }

    while (auto* ent = readdir(dir)) {
        if (!std::strcmp(ent->d_name, ".") || !std::strcmp(ent->d_name, "..")) {
            continue;
        }

        const auto src_path = src + "/" + ent->d_name;
        const auto dst_path = dst == "/" ? "/" + std::string{ent->d_name} : dst + "/" + ent->d_name;
        if (DirectoryExists(src_path.c_str())) {
            fs::FsNativeSd fs;
            R_TRY(fs.CreateDirectoryRecursively(dst_path));
            R_TRY(CopyDirectoryContents(src_path, dst_path));
        } else {
            R_TRY(CopyFileSimple(src_path, dst_path));
        }
    }
    closedir(dir);
    R_SUCCEED();
}

auto MovePath(const std::string& src, const std::string& dst) -> Result {
    if (!FileExists(src.c_str())) {
        R_SUCCEED();
    }

    R_TRY(EnsureParentDirectory(dst));
    R_TRY(DeletePath(dst));
    if (std::rename(src.c_str(), dst.c_str()) == 0) {
        R_SUCCEED();
    }

    if (DirectoryExists(src.c_str())) {
        fs::FsNativeSd fs;
        R_TRY(fs.CreateDirectoryRecursively(dst));
        R_TRY(CopyDirectoryContents(src, dst));
    } else {
        R_TRY(CopyFileSimple(src, dst));
    }
    R_TRY(DeletePath(src));
    R_SUCCEED();
}

auto IniValueEquals(const char* path, const char* section, const char* key, const char* value) -> bool {
    char buf[64]{};
    return ini_gets(section, key, "", buf, sizeof(buf), path) && !std::strcmp(buf, value);
}

auto SetIniValue(const char* path, const char* section, const char* key, const char* value) -> Result {
    R_TRY(EnsureParentDirectory(path));
    if (!ini_puts(section, key, value, path)) {
        R_THROW(fsdevGetLastResult());
    }
    R_SUCCEED();
}

auto DownloadFile(ProgressBox* pbox, const std::string& label, const std::string& url, const fs::FsPath& dst) -> Result {
    R_TRY(EnsureParentDirectory(dst.toString()));
    pbox->NewTransfer(label);

    const auto result = curl::Api().ToFile(
        curl::Url{url},
        curl::Path{dst},
        curl::OnProgress{pbox->OnDownloadProgressCallback()}
    );
    R_UNLESS(result.success, Result_CurlFailedEasyInit);
    R_SUCCEED();
}

auto UnzipFile(ProgressBox* pbox, const fs::FsPath& zip, const fs::FsPath& dst) -> Result {
    fs::FsNativeSd fs;
    R_TRY(fs.GetFsOpenResult());
    R_TRY(fs.CreateDirectoryRecursively(dst));
    pbox->NewTransfer("Extracting " + dst.toString());
    R_TRY(thread::TransferUnzipAll(pbox, zip, &fs, dst));
    R_SUCCEED();
}

void RebootAfterSetting();

auto ParseDbiTranslations(const std::string& path) -> std::vector<DbiTranslationEntry> {
    std::vector<DbiTranslationEntry> entries;
    auto lines = ReadLines(path);

    std::string name;
    std::string translation_url;

    const auto flush = [&]() {
        if (!name.empty() && name != "Update list of translations" && !translation_url.empty()) {
            entries.push_back({name, translation_url});
        }
        name.clear();
        translation_url.clear();
    };

    for (const auto& line : lines) {
        if (const auto section = ExtractBracketName(line); !section.empty()) {
            flush();
            name = section;
            continue;
        }

        const auto cmd = SplitCommand(line);
        if (cmd.size() >= 3 && cmd[0] == "download" && cmd[2].find("translation_new.bin") != std::string::npos) {
            translation_url = cmd[1];
        }
    }
    flush();

    return entries;
}

auto ParseInterfaceTranslations(const std::string& path) -> std::vector<InterfaceTranslationEntry> {
    std::vector<InterfaceTranslationEntry> entries;
    auto lines = ReadLines(path);

    InterfaceTranslationEntry current;
    bool in_language{};

    const auto flush = [&]() {
        if (in_language && !current.name.empty() && !current.zip_url.empty() && !current.json_path.empty()) {
            entries.push_back(current);
        }
        current = {};
        in_language = false;
    };

    for (const auto& line : lines) {
        if (const auto section = ExtractBracketName(line); !section.empty()) {
            flush();
            in_language = line.find("[*") != std::string::npos;
            if (in_language) {
                current.name = section;
                current.json_path = std::string{TRANSLATE_PACKAGE_DIR} + "/langs/" + current.name + ".json";
            }
            continue;
        }

        if (!in_language) {
            continue;
        }

        const auto cmd = SplitCommand(Trim(line));
        if (cmd.size() >= 3 && cmd[0] == "download") {
            current.zip_url = cmd[1];
        }
    }
    flush();

    return entries;
}

auto ReadInterfaceReplacementOptions(const InterfaceTranslationEntry& entry) -> std::vector<std::pair<std::string, std::string>> {
    std::vector<std::pair<std::string, std::string>> out;
    const auto json = ReadTextFile(entry.json_path);
    size_t pos{};
    while (true) {
        const auto object_start = json.find('{', pos);
        if (object_start == std::string::npos) {
            break;
        }
        const auto object_end = json.find('}', object_start);
        if (object_end == std::string::npos) {
            break;
        }

        const auto object = json.substr(object_start, object_end - object_start + 1);
        const auto label = ExtractJsonStringField(object, "lang");
        const auto dir = ExtractJsonStringField(object, "dir");
        if (!label.empty() && !dir.empty()) {
            out.emplace_back(label, dir);
        }
        pos = object_end + 1;
    }
    return out;
}

auto FileNameFromUrl(const std::string& url) -> std::string {
    const auto query = url.find_first_of("?#");
    auto clean = query == std::string::npos ? url : url.substr(0, query);
    const auto slash = clean.find_last_of('/');
    if (slash != std::string::npos) {
        clean = clean.substr(slash + 1);
    }
    return clean.empty() ? "translation.zip" : clean;
}

auto TranslationExtractFolder(const std::string& zip_name) -> std::string {
    auto folder = zip_name;
    if (const auto dot = folder.find_last_of('.'); dot != std::string::npos) {
        folder = folder.substr(0, dot);
    }
    if (StartsWith(folder, "NX-")) {
        folder = "Nx-" + folder.substr(3);
    }
    return folder;
}

auto InstallDbiTranslation(ProgressBox* pbox, const DbiTranslationEntry& entry) -> Result {
    R_TRY(DownloadFile(
        pbox,
        "Downloading DBI...",
        "https://github.com/rashevskyv/DBIPatcher/releases/latest/download/DBI.nro",
        "/switch/DBI/DBI_new.nro"
    ));
    R_TRY(DownloadFile(
        pbox,
        "Downloading " + entry.name + " translation...",
        entry.translation_url,
        "/switch/DBI/translation_new.bin"
    ));
    R_TRY(MovePath("/switch/DBI/DBI_new.nro", "/switch/DBI/DBI.nro"));
    R_TRY(MovePath("/switch/DBI/translation_new.bin", "/switch/DBI/translation.bin"));
    R_SUCCEED();
}

auto InstallInterfaceTranslation(ProgressBox* pbox, InterfaceTranslationEntry entry, std::string replacement_dir) -> Result {
    const auto zip_name = FileNameFromUrl(entry.zip_url);
    const auto extract_dir = std::string{SPHAIRA_DOWNLOADS} + "/translations";
    const auto zip_path = extract_dir + "/" + zip_name;

    R_TRY(DeletePath(extract_dir));
    R_TRY(DownloadFile(pbox, "Downloading " + entry.name + "...", entry.zip_url, zip_path));

    for (const auto path : TRANSLATION_PATHS) {
        R_TRY(DeletePath(path));
    }

    R_TRY(UnzipFile(pbox, zip_path, extract_dir));

    const auto source = extract_dir + "/" + TranslationExtractFolder(zip_name) + "/" + replacement_dir + "/contents";
    R_TRY(CopyDirectoryContents(source, "/atmosphere/contents"));
    R_TRY(DeletePath(source));
    R_TRY(DeletePath(extract_dir));
    R_TRY(DeletePath(TRANSLATE_PACKAGE));
    R_TRY(DeletePath(std::string{TRANSLATE_PACKAGE_DIR} + "/langs"));
    R_TRY(MovePath(TRANSLATE_PACKAGE_BACKUP, TRANSLATE_PACKAGE));
    RebootAfterSetting();
    R_SUCCEED();
}

auto IsEmummcEnabled() -> bool {
    return IniValueEquals("/emummc/emummc.ini", "emummc", "enabled", "1");
}

void RebootAfterSetting() {
    fsdevCommitDevice("sdmc");
    utils::requestForcedReboot();
}

auto ApplyOverclock(bool enabled) -> Result {
    if (enabled) {
        R_TRY(CopyDirectoryContents("/config/oc", "/"));
        R_TRY(MovePath("/config/oc_bkp/config/sys-clk/config.ini", "/config/sys-clk/config.ini"));
    } else {
        R_TRY(DeletePath("/atmosphere/contents/00FF0000636C6BFF"));
        R_TRY(DeletePath("/atmosphere/kips/kefir.kip"));
        R_TRY(DeletePath("/bootloader/loader.kip"));
        R_TRY(DeletePath("/switch/.overlays/sys-clk-overlay.ovl"));
        R_TRY(MovePath("/config/sys-clk/config.ini", "/config/oc_bkp/config/sys-clk/config.ini"));
    }
    RebootAfterSetting();
    R_SUCCEED();
}

auto Apply40Mb(bool enabled) -> Result {
    R_TRY(SetIniValue(ATMOSPHERE_CONFIG, "atmosphere", "force_40mb_applet", enabled ? "u8!0x1" : "u8!0x0"));
    RebootAfterSetting();
    R_SUCCEED();
}

auto ApplyRedirectSaves(bool enabled) -> Result {
    R_TRY(SetIniValue(ATMOSPHERE_CONFIG, "atmosphere", "fsmitm_redirect_saves_to_sd", enabled ? "u8!0x1" : "u8!0x0"));
    if (!enabled) {
        R_TRY(DeletePath("/config/redirect.bin"));
    }
    RebootAfterSetting();
    R_SUCCEED();
}

auto Apply8GbDram(bool enabled) -> Result {
    if (enabled) {
        R_TRY(CopyFileSimple("/config/8gb/install.te", "/startup.te"));
    } else {
        R_TRY(CopyFileSimple("/tegraexplorer/scripts/Remove_8GB-RAM_config.te", "/startup.te"));
    }
    fsdevCommitDevice("sdmc");
    if (!utils::rebootToPayload("/bootloader/payloads/TegraExplorer.bin")) {
        R_THROW(Result_FsUnknownStdioError);
    }
    R_SUCCEED();
}

class HoldConfirmBox final : public Widget {
public:
    using Callback = std::function<void(bool)>;

    HoldConfirmBox(std::string message, float hold_seconds, Callback callback)
    : m_message{std::move(message)}
    , m_hold_seconds{std::max(0.5f, hold_seconds)}
    , m_callback{std::move(callback)} {
        m_pos = Vec4{255.f, 168.f, 770.f, 384.f};
        SetActions(
            std::make_pair(Button::B, Action{"Cancel"_i18n, [this](){
                m_callback(false);
                SetPop();
            }})
        );
    }

    void Update(Controller* controller, TouchInfo* touch) override {
        Widget::Update(controller, touch);

        if (controller->GotHeld(Button::A)) {
            if (!m_holding) {
                m_holding = true;
                m_hold_start = armTicksToNs(armGetSystemTick());
            }

            const auto now = armTicksToNs(armGetSystemTick());
            m_progress = std::min(1.f, static_cast<float>(now - m_hold_start) / (m_hold_seconds * 1000000000.f));
            if (m_progress >= 1.f) {
                m_callback(true);
                SetPop();
            }
        } else {
            m_holding = false;
            m_progress = 0.f;
        }
    }

    void Draw(NVGcontext* vg, Theme* theme) override {
        gfx::dimBackground(vg);
        gfx::drawRect(vg, m_pos, theme->GetColour(ThemeEntryID_POPUP), 5.f);

        constexpr float padding = 34.f;
        nvgSave(vg);
        nvgTextLineHeight(vg, 1.35f);
        gfx::drawTextBox(
            vg, m_pos.x + padding, m_pos.y + 38.f, 18.f, m_pos.w - padding * 2.f,
            theme->GetColour(ThemeEntryID_TEXT), m_message.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP
        );
        nvgRestore(vg);

        const Vec4 button{m_pos.x, m_pos.y + m_pos.h - 82.f, m_pos.w, 82.f};
        gfx::drawRect(vg, button.x, button.y - 2.f, button.w, 2.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
        gfx::drawRectOutline(vg, theme, 4.f, Vec4{button.x + 160.f, button.y + 10.f, button.w - 320.f, button.h - 20.f});

        const Vec4 bar{button.x + 180.f, button.y + button.h - 22.f, button.w - 360.f, 6.f};
        gfx::drawRect(vg, bar, theme->GetColour(ThemeEntryID_LINE_SEPARATOR), 3.f);
        gfx::drawRect(vg, bar.x, bar.y, bar.w * m_progress, bar.h, theme->GetColour(ThemeEntryID_TEXT_SELECTED), 3.f);

        const auto hold_text = "Hold A to continue"_i18n;
        gfx::drawText(
            vg, button.x + button.w / 2.f, button.y + 35.f, 24.f,
            theme->GetColour(ThemeEntryID_TEXT_SELECTED),
            hold_text.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE
        );
    }

private:
    std::string m_message;
    float m_hold_seconds{};
    Callback m_callback;
    bool m_holding{};
    u64 m_hold_start{};
    float m_progress{};
};

auto MakeBoolItem(std::string label, std::string description, std::function<bool()> get, std::function<void(bool)> set) -> SettingsItem {
    return {
        std::move(label),
        std::move(description),
        [get](){
            return OnOff(get());
        },
        [get, set](){
            set(!get());
        }
    };
}

auto MakeOptionItem(std::string label, std::string description, option::OptionBool& option) -> SettingsItem {
    return MakeBoolItem(
        std::move(label),
        std::move(description),
        [&option](){
            return option.Get();
        },
        [&option](bool enabled){
            option.Set(enabled);
        }
    );
}

void ToggleInstallOption(option::OptionBool& option) {
    if (option.Get()) {
        option.Set(false);
        return;
    }

    App::Push<OptionBox>(
        "WARNING: Installing apps will lead to a ban!"_i18n,
        "Back"_i18n,
        "Enable"_i18n,
        0,
        [&option](auto op_index){
            if (op_index && *op_index) {
                option.Set(true);
                App::Notify("Installing enabled!"_i18n);
            }
        }
    );
}

auto MakeInstallToggle(std::string label, std::string description, option::OptionBool& option) -> SettingsItem {
    return {
        std::move(label),
        std::move(description),
        [&option](){
            return OnOff(option.Get());
        },
        [&option](){
            ToggleInstallOption(option);
        }
    };
}

void ToggleKefirSetting(const KefirSetting& setting) {
    const auto enabled = !setting.get();
    auto warning = setting.warning_on;

    if (warning.empty()) {
        warning = enabled
            ? "This setting changes Kefir or Atmosphere files and will reboot the console."
            : "This setting changes Kefir or Atmosphere files and will reboot the console.";
    }

    App::Push<HoldConfirmBox>(
        warning,
        setting.hold_seconds,
        [setting, enabled](bool confirmed){
            if (!confirmed) {
                return;
            }

            App::Push<ProgressBox>(
                0,
                enabled ? "Enabling"_i18n : "Disabling"_i18n,
                setting.label,
                [setting, enabled](auto pbox) -> Result {
                    pbox->NewTransfer(setting.description);
                    return setting.set(enabled);
                },
                [](Result rc){
                    if (R_FAILED(rc)) {
                        App::PushErrorBox(rc, "Failed to apply Kefir setting");
                    }
                }
            );
        }
    );
}

auto MakeKefirToggle(KefirSetting setting) -> SettingsItem {
    return {
        setting.label,
        setting.description,
        [setting](){
            return OnOff(setting.get());
        },
        [setting](){
            ToggleKefirSetting(setting);
        }
    };
}

void RunPackageAction(const PackageAction& action) {
    const auto start = [action](){
        App::Push<ProgressBox>(
            0,
            "Running"_i18n,
            action.label,
            [action](auto pbox) -> Result {
                return action.run(pbox);
            },
            [](Result rc){
                if (R_FAILED(rc)) {
                    App::PushErrorBox(rc, "Failed to run package action");
                } else {
                    App::Notify("Done"_i18n);
                }
            }
        );
    };

    if (!action.hold) {
        start();
        return;
    }

    auto warning = action.warning;
    if (warning.empty()) {
        warning = "This action changes files on the SD card. Hold A to continue.";
    }

    App::Push<HoldConfirmBox>(
        warning,
        action.hold_seconds,
        [start](bool confirmed){
            if (confirmed) {
                start();
            }
        }
    );
}

auto MakePackageAction(PackageAction action) -> SettingsItem {
    return {
        action.label,
        action.description,
        [](){
            return std::string{};
        },
        [action](){
            RunPackageAction(action);
        },
        SettingsItemKind::Download,
    };
}

auto BuildDbiItems() -> std::vector<SettingsItem> {
    std::vector<SettingsItem> items;

    items.emplace_back(MakePackageAction({
        "Download DBI translations list",
        "Update the DBI fan translations package list.",
        [](auto pbox) -> Result {
            R_TRY(DownloadFile(
                pbox,
                "Downloading list of translations...",
                "https://github.com/rashevskyv/DBI_watcher/raw/main/output/package.ini",
                "/config/sphaira/downloads/dbi.package.ini"
            ));
            R_TRY(MovePath("/config/sphaira/downloads/dbi.package.ini", DBI_TRANSLATIONS_PACKAGE));
            R_SUCCEED();
        },
    }));

    for (const auto& entry : ParseDbiTranslations(DBI_TRANSLATIONS_PACKAGE)) {
        items.emplace_back(MakePackageAction({
            entry.name,
            "Install DBI fan translation.",
            [entry](auto pbox) -> Result {
                return InstallDbiTranslation(pbox, entry);
            },
        }));
    }

    items.emplace_back(MakePackageAction({
        "Russian latest DBI",
        "Download the latest Russian DBI build.",
        [](auto pbox) -> Result {
            R_TRY(DownloadFile(
                pbox,
                "Downloading Russian DBI...",
                "https://github.com/rashevskyv/DBI/releases/latest/download/DBI.nro",
                "/switch/DBI/DBI_new.nro"
            ));
            R_TRY(MovePath("/switch/DBI/DBI_new.nro", "/switch/DBI/DBI.nro"));
            R_SUCCEED();
        },
    }));

    items.emplace_back(MakePackageAction({
        "Reset DBI config",
        "Download a clean DBI config from Kefir.",
        [](auto pbox) -> Result {
            R_TRY(DownloadFile(
                pbox,
                "Resetting DBI Config...",
                "https://github.com/rashevskyv/DBI/releases/latest/download/dbi.config",
                "/switch/DBI/dbi.config_new"
            ));
            R_TRY(MovePath("/switch/DBI/dbi.config_new", "/switch/DBI/dbi.config"));
            R_SUCCEED();
        },
        true,
        "This will replace your current DBI config with the default Kefir config.",
        0.5f,
    }));

    return items;
}

auto BuildSoftwareItems() -> std::vector<SettingsItem> {
    std::vector<SettingsItem> items;

    items.emplace_back(SettingsItem{
        "DBI",
        "DBI installer and translations.",
        [](){
            return std::string{};
        },
        [](){
            App::Push<DbiMenu>();
        },
        SettingsItemKind::Folder,
    });

    items.emplace_back(MakePackageAction({
        "UAModDownloader",
        "Ukrainian mods.",
        [](auto pbox) -> Result {
            R_TRY(DownloadFile(
                pbox,
                "Downloading UAModDownloader...",
                "https://github.com/pro100luk/UAModDownloader/releases/latest/download/UAModDownloader.nro",
                "/switch/UAModDownloader/UAModDownloader_new.nro"
            ));
            R_TRY(MovePath("/switch/UAModDownloader/UAModDownloader_new.nro", "/switch/UAModDownloader/UAModDownloader.nro"));
            R_SUCCEED();
        },
    }));

    items.emplace_back(MakePackageAction({
        "ModCD",
        "ECLIPS graphic mods.",
        [](auto pbox) -> Result {
            R_TRY(DownloadFile(
                pbox,
                "Downloading ModCD...",
                "https://github.com/kawaii-flesh/ModCD/releases/latest/download/ModCD.nro",
                "/switch/ModCD/ModCD_new.nro"
            ));
            R_TRY(MovePath("/switch/ModCD/ModCD_new.nro", "/switch/ModCD/ModCD.nro"));
            R_SUCCEED();
        },
    }));

    items.emplace_back(MakePackageAction({
        "SimpleModDownloader",
        "Game mods from GameBanana.",
        [](auto pbox) -> Result {
            R_TRY(DownloadFile(
                pbox,
                "Downloading SimpleModDownloader...",
                "https://github.com/PoloNX/SimpleModDownloader/releases/latest/download/SimpleModDownloader.nro",
                "/switch/SimpleModDownloader/SimpleModDownloader_new.nro"
            ));
            R_TRY(MovePath("/switch/SimpleModDownloader/SimpleModDownloader_new.nro", "/switch/SimpleModDownloader/SimpleModDownloader.nro"));
            R_SUCCEED();
        },
    }));

    return items;
}

auto BuildThemeItems() -> std::vector<SettingsItem> {
    std::vector<SettingsItem> items;

    items.emplace_back(MakePackageAction({
        "Mario BG Dark",
        "Download and extract Mario BG Modern theme.",
        [](auto pbox) -> Result {
            R_TRY(DownloadFile(
                pbox,
                "Downloading Mario BG Dark...",
                "https://github.com/rashevskyv/mario_bg_theme/releases/latest/download/Mario.BG.Modern.zip",
                "/config/sphaira/downloads/theme.zip"
            ));
            R_TRY(UnzipFile(pbox, "/config/sphaira/downloads/theme.zip", "/themes/"));
            R_TRY(DeletePath("/config/sphaira/downloads/theme.zip"));
            R_SUCCEED();
        },
    }));

    items.emplace_back(MakePackageAction({
        "Switch 2 Theme by alexwak",
        "Download and extract Switch 2 theme.",
        [](auto pbox) -> Result {
            R_TRY(DownloadFile(
                pbox,
                "Downloading Switch 2 Theme...",
                "https://github.com/alexwak/Switch-2-Switch-Theme/releases/latest/download/Switch-2-Switch-Banned.zip",
                "/config/sphaira/downloads/theme.zip"
            ));
            R_TRY(UnzipFile(pbox, "/config/sphaira/downloads/theme.zip", "/themes/"));
            R_TRY(DeletePath("/config/sphaira/downloads/theme.zip"));
            R_SUCCEED();
        },
    }));

    return items;
}

auto BuildTranslateItems() -> std::vector<SettingsItem> {
    std::vector<SettingsItem> items;

    items.emplace_back(MakePackageAction({
        "Download language packs",
        "Download the UltraHand language package list.",
        [](auto pbox) -> Result {
            R_TRY(DownloadFile(
                pbox,
                "Downloading language packs...",
                "https://github.com/rashevskyv/switch-translations-mirrors/raw/main/lang_packs_ultra.zip",
                "/config/sphaira/downloads/lang_packs.zip"
            ));
            R_TRY(MovePath(TRANSLATE_PACKAGE, TRANSLATE_PACKAGE_BACKUP));
            R_TRY(DeletePath(TRANSLATE_PACKAGE_DIR));
            fs::FsNativeSd fs;
            R_TRY(fs.CreateDirectoryRecursively(TRANSLATE_PACKAGE_DIR));
            if (FileExists(TRANSLATE_PACKAGE_BACKUP)) {
                R_TRY(CopyFileSimple(TRANSLATE_PACKAGE_BACKUP, std::string{TRANSLATE_PACKAGE_DIR} + "/package.ini.bkp"));
            }
            R_TRY(UnzipFile(pbox, "/config/sphaira/downloads/lang_packs.zip", TRANSLATE_PACKAGE_DIR));
            R_TRY(DeletePath("/config/sphaira/downloads/lang_packs.zip"));
            R_SUCCEED();
        },
    }));

    items.emplace_back(MakePackageAction({
        "Remove installed translation",
        "Delete installed interface translations and reboot.",
        [](auto pbox) -> Result {
            pbox->NewTransfer("Removing translations...");
            for (const auto path : TRANSLATION_PATHS) {
                R_TRY(DeletePath(path));
            }
            RebootAfterSetting();
            R_SUCCEED();
        },
        true,
        "This removes installed system interface translation files and reboots the console.",
        0.5f,
    }));

    for (const auto& entry : ParseInterfaceTranslations(TRANSLATE_PACKAGE)) {
        items.emplace_back(SettingsItem{
            entry.name,
            "Install interface translation.",
            [](){
                return std::string{};
            },
            [entry](){
                const auto options = ReadInterfaceReplacementOptions(entry);
                if (options.empty()) {
                    App::PushErrorBox(Result_FsEmpty, "No replacement languages found");
                    return;
                }

                PopupList::Items labels;
                labels.reserve(options.size());
                for (const auto& [label, dir] : options) {
                    labels.push_back(label);
                }

                App::Push<PopupList>(
                    "Replace language"_i18n,
                    labels,
                    [entry, options](auto index){
                        if (!index) {
                            return;
                        }

                        const auto dir = options[*index].second;
                        App::Push<HoldConfirmBox>(
                            "This will replace the selected system interface language and reboot the console.",
                            0.5f,
                            [entry, dir](bool confirmed){
                                if (!confirmed) {
                                    return;
                                }

                                App::Push<ProgressBox>(
                                    0,
                                    "Installing"_i18n,
                                    entry.name,
                                    [entry, dir](auto pbox) -> Result {
                                        return InstallInterfaceTranslation(pbox, entry, dir);
                                    },
                                    [](Result rc){
                                        if (R_FAILED(rc)) {
                                            App::PushErrorBox(rc, "Failed to install translation");
                                        }
                                    }
                                );
                            }
                        );
                    }
                );
            },
            SettingsItemKind::Folder,
        });
    }

    return items;
}

auto BuildKefirItems() -> std::vector<SettingsItem> {
    std::vector<SettingsItem> items;
    items.emplace_back(MakeKefirToggle({
        "Overclock status",
        "Enable or disable Kefir overclock files.",
        [](){
            return FileExists("/atmosphere/kips/kefir.kip");
        },
        ApplyOverclock,
        "",
        0.5f,
    }));
    items.emplace_back(MakeKefirToggle({
        "40MB Memory",
        "Toggle the 40MB applet memory patch.",
        [](){
            return IniValueEquals(ATMOSPHERE_CONFIG, "atmosphere", "force_40mb_applet", "u8!0x1");
        },
        Apply40Mb,
        "",
        0.5f,
    }));

    if (IsEmummcEnabled()) {
        items.emplace_back(MakeKefirToggle({
            "Redirect Emunand saves to SD",
            "Experimental save redirection for emuMMC.",
            [](){
                return IniValueEquals(ATMOSPHERE_CONFIG, "atmosphere", "fsmitm_redirect_saves_to_sd", "u8!0x1");
            },
            ApplyRedirectSaves,
            "Experimental option.\n\nThis redirects emuMMC saves to the SD card. Use it only if you understand the risk; changing save paths can make saves appear missing until the setting is reverted.",
            0.5f,
        }));
    }

    items.emplace_back(MakeKefirToggle({
        "8GB DRAM status",
        "Only for consoles with physically soldered 8GB RAM.",
        [](){
            return FileExists("/tegraexplorer/scripts/Remove_8GB-RAM_config.te");
        },
        Apply8GbDram,
        "Only for consoles with physically soldered 8GB RAM. Other consoles will not boot correctly.\n\nTo disable it if the console does not boot:\nhekate > payloads > TegraExplorer > Remove_8GB-RAM_config.te",
        3.f,
    }));

    return items;
}

auto LanguageValue() -> std::string {
    const auto index = ClampIndex(App::GetLanguage(), static_cast<long>(LANGUAGE_ITEMS.size()));
    return i18n::get(LANGUAGE_ITEMS[index]);
}

auto TextScrollSpeedValue() -> std::string {
    const auto index = ClampIndex(App::GetTextScrollSpeed(), static_cast<long>(TEXT_SCROLL_SPEED_ITEMS.size()));
    return i18n::get(TEXT_SCROLL_SPEED_ITEMS[index]);
}

auto ThemeValue() -> std::string {
    const auto themes = App::GetThemeMetaList();
    if (themes.empty()) {
        return "None";
    }

    const auto index = std::clamp<s64>(App::GetThemeIndex(), 0, static_cast<s64>(themes.size() - 1));
    return themes[index].name;
}

} // namespace

Menu::Menu() : MenuBase{"Settings"_i18n, MenuFlag_None} {
    BuildCategories();

    this->SetActions(
        std::make_pair(Button::A, Action{"Select"_i18n, [this](){
            OnSelect();
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            OnBack();
        }})
    );

    m_category_list = std::make_unique<List>(1, 8, m_pos, Vec4{76.f, 138.f, 300.f, 56.f});
    m_category_list->SetLayout(List::Layout::GRID);

    m_item_list = std::make_unique<List>(1, 7, m_pos, Vec4{420.f, 132.f, 780.f, 66.f});
    m_item_list->SetLayout(List::Layout::GRID);

    SetCategoryIndex(0);
}

Menu::~Menu() = default;

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();

    std::string category_label;
    if (!m_categories.empty()) {
        category_label = m_categories[m_category_index].label;
    }

    BuildCategories();
    auto it = std::find_if(m_categories.cbegin(), m_categories.cend(), [&](const auto& category) {
        return category.label == category_label;
    });
    SetCategoryIndex(it == m_categories.cend() ? m_category_index : std::distance(m_categories.cbegin(), it));
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    const auto item_count = m_categories.empty() ? 0 : static_cast<s64>(m_categories[m_category_index].items.size());
    const auto items_can_page = item_count > m_item_list->GetPage();
    const auto categories_can_page = static_cast<s64>(m_categories.size()) > m_category_list->GetPage();

    if (controller->GotDown(Button::RIGHT) && m_focus_pane == FocusPane::Categories && !categories_can_page) {
        SetFocusPane(FocusPane::Items);
        App::PlaySoundEffect(SoundEffect_Focus);
        return;
    }

    if (controller->GotDown(Button::LEFT) && m_focus_pane == FocusPane::Items && !items_can_page) {
        SetFocusPane(FocusPane::Categories);
        App::PlaySoundEffect(SoundEffect_Focus);
        return;
    }

    if (touch->is_clicked) {
        if (touch->in_range(m_category_list->GetPos())) {
            SetFocusPane(FocusPane::Categories);
        } else if (touch->in_range(m_item_list->GetPos())) {
            SetFocusPane(FocusPane::Items);
        }
    }

    MenuBase::Update(controller, touch);

    if (m_focus_pane == FocusPane::Categories) {
        m_category_list->OnUpdate(controller, touch, m_category_index, m_categories.size(), [this](bool touch, auto i) {
            if (touch && m_category_index == i) {
                FireAction(Button::A);
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                SetCategoryIndex(i);
            }
        });
    } else {
        const auto& category = m_categories[m_category_index];
        m_item_list->OnUpdate(controller, touch, m_item_index, category.items.size(), [this](bool touch, auto i) {
            if (touch && m_item_index == i) {
                FireAction(Button::A);
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                SetItemIndex(i);
            }
        });
    }
}

namespace {

auto SettingsItemTextX(const SettingsItem& item, float x) -> float {
    return item.kind == SettingsItemKind::Normal ? x + 18.f : x + 58.f;
}

void DrawSettingsItemKindIcon(NVGcontext* vg, Theme* theme, const SettingsItem& item, Vec4 v, bool selected) {
    if (item.kind == SettingsItemKind::Normal) {
        return;
    }

    const auto colour = theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT_INFO);
    const auto x = v.x + 18.f;
    const auto y = v.y + 21.f;

    nvgSave(vg);
    nvgStrokeColor(vg, colour);
    nvgStrokeWidth(vg, 2.f);

    if (item.kind == SettingsItemKind::Folder) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y + 3.f, 28.f, 19.f, 3.f);
        nvgRect(vg, x + 3.f, y, 11.f, 5.f);
        nvgStroke(vg);
    } else if (item.kind == SettingsItemKind::Download) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, x + 14.f, y);
        nvgLineTo(vg, x + 14.f, y + 17.f);
        nvgMoveTo(vg, x + 7.f, y + 10.f);
        nvgLineTo(vg, x + 14.f, y + 17.f);
        nvgLineTo(vg, x + 21.f, y + 10.f);
        nvgMoveTo(vg, x + 5.f, y + 23.f);
        nvgLineTo(vg, x + 23.f, y + 23.f);
        nvgStroke(vg);
    }

    nvgRestore(vg);
}

} // namespace

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    gfx::drawRect(vg, 392.f, 118.f, 1.f, 504.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));

    m_category_list->Draw(vg, theme, m_categories.size(), [this](auto* vg, auto* theme, Vec4 v, auto i) {
        const auto selected = m_category_index == i;
        const auto focused = selected && m_focus_pane == FocusPane::Categories;
        const auto text_id = selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;

        if (selected) {
            gfx::drawRect(vg, v, theme->GetColour(ThemeEntryID_SELECTED_BACKGROUND), 5.f);
        }
        if (focused) {
            gfx::drawRectOutline(vg, theme, 4.f, v);
        }

        gfx::drawTextBox(
            vg, v.x + 18.f, v.y + 16.f, 20.f, v.w - 36.f,
            theme->GetColour(text_id), m_categories[i].label.c_str()
        );
    });

    if (m_categories.empty()) {
        return;
    }

    const auto& category = m_categories[m_category_index];
    m_item_list->Draw(vg, theme, category.items.size(), [this, &category](auto* vg, auto* theme, Vec4 v, auto i) {
        const auto& item = category.items[i];
        const auto selected = m_item_index == i;
        const auto focused = selected && m_focus_pane == FocusPane::Items;
        const auto label_id = selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;

        if (selected) {
            gfx::drawRect(vg, v, theme->GetColour(ThemeEntryID_SELECTED_BACKGROUND), 5.f);
        } else {
            gfx::drawRect(vg, v.x, v.y + v.h, v.w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
        }
        if (focused) {
            gfx::drawRectOutline(vg, theme, 4.f, v);
        }

        DrawSettingsItemKindIcon(vg, theme, item, v, selected);
        const auto text_x = SettingsItemTextX(item, v.x);
        const auto text_offset = text_x - v.x;

        gfx::drawTextBox(
            vg, text_x, v.y + 10.f, 20.f, v.w - 242.f - text_offset,
            theme->GetColour(label_id), item.label.c_str()
        );
        if (!item.description.empty()) {
            gfx::drawTextBox(
                vg, text_x, v.y + 37.f, 14.f, v.w - 212.f - text_offset,
                theme->GetColour(ThemeEntryID_TEXT_INFO), item.description.c_str()
            );
        }

        if (item.value) {
            const auto value = item.value();
            gfx::drawText(
                vg, v.x + v.w - 20.f, v.y + 21.f, 18.f,
                SettingsValueColour(theme, value, selected),
                value.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP
            );
        }
    });
}

void Menu::BuildCategories() {
    auto* app = App::GetApp();

    m_categories = {
        {
            "General",
            "Language, timing and application flow.",
            {
                { "Language", "Cycle the active interface language.", LanguageValue, [](){
                    App::SetLanguage(ClampIndex(App::GetLanguage() + 1, static_cast<long>(LANGUAGE_ITEMS.size())));
                }},
                { "Text scroll speed", "Change how fast long labels scroll.", TextScrollSpeedValue, [](){
                    App::SetTextScrollSpeed(ClampIndex(App::GetTextScrollSpeed() + 1, static_cast<long>(TEXT_SCROLL_SPEED_ITEMS.size())));
                }},
                MakeBoolItem("12 Hour Time", "Use 12 hour clock format.", App::Get12HourTimeEnable, App::Set12HourTimeEnable),
                { "Restart Sphaira", "Close and reopen the application.", [](){ return std::string{}; }, [](){
                    App::ExitRestart();
                }},
                { "Exit", "Close Sphaira.", [](){ return std::string{}; }, [](){
                    App::Exit();
                }},
            }
        },
        {
            "Appearance",
            "Theme and audio options.",
            {
                { "Theme", "Cycle installed Sphaira themes.", ThemeValue, [](){
                    const auto themes = App::GetThemeMetaList();
                    if (!themes.empty()) {
                        App::SetTheme(ClampIndex(App::GetThemeIndex() + 1, static_cast<long>(themes.size())));
                    }
                }},
                MakeBoolItem("Music", "Enable background music from the current theme.", App::GetThemeMusicEnable, App::SetThemeMusicEnable),
            }
        },
        {
            "Network",
            "Background services and network downloads.",
            {
                MakeBoolItem("FTP", "Run the FTP server in the background.", App::GetFtpEnable, App::SetFtpEnable),
                MakeBoolItem("MTP", "Run the MTP server in the background.", App::GetMtpEnable, App::SetMtpEnable),
                MakeBoolItem("Nxlink", "Receive .nro files from a PC.", App::GetNxlinkEnable, App::SetNxlinkEnable),
                MakeBoolItem("HDD", "Mount connected USB/HDD devices.", App::GetHddEnable, App::SetHddEnable),
                MakeBoolItem("HDD write protect", "Make connected HDD storage read-only.", App::GetWriteProtect, App::SetWriteProtect),
            }
        },
        {
            "Homebrew",
            "Shortcuts for core Sphaira tools.",
            {
                { "Homebrew App Store", "Download and update homebrew apps.", [](){ return std::string{}; }, [](){
                    App::Push<ui::menu::appstore::Menu>(MenuFlag_None);
                }},
                { "File Browser", "Browse and manage files on the SD card.", [](){ return std::string{}; }, [](){
                    App::Push<ui::menu::filebrowser::Menu>(MenuFlag_None);
                }},
                { "Component Manager", "Manage installed packages and modules.", [](){ return std::string{}; }, [](){
                    App::Push<ui::menu::hats::UninstallerMenu>();
                }},
            }
        },
        {
            "Kefir",
            "Kefir patches and console-specific switches.",
            BuildKefirItems(),
        },
        {
            "Translate Interface",
            "Interface translation package tools.",
            BuildTranslateItems(),
        },
        {
            "Install",
            "Install behavior and safety switches.",
            {
                MakeInstallToggle("Enable sysMMC", "Allow installing while running sysMMC.", app->m_install_sysmmc),
                MakeInstallToggle("Enable emuMMC", "Allow installing while running emuMMC.", app->m_install_emummc),
                { "Install location", "Choose system memory or microSD card.", [](){
                    return App::GetInstallSdEnable() ? "microSD card"_i18n : "System memory"_i18n;
                }, [](){
                    App::SetInstallSdEnable(!App::GetInstallSdEnable());
                }},
                MakeOptionItem("Allow downgrade", "Allow lower title updates to be installed.", app->m_allow_downgrade),
                MakeOptionItem("Skip if already installed", "Skip titles or NCAs that are already installed.", app->m_skip_if_already_installed),
                MakeOptionItem("Ticket only", "Install tickets without title contents.", app->m_ticket_only),
                MakeOptionItem("Skip base", "Skip installing base applications.", app->m_skip_base),
                MakeOptionItem("Skip patch", "Skip installing title updates.", app->m_skip_patch),
                MakeOptionItem("Skip DLC", "Skip installing DLC content.", app->m_skip_addon),
                MakeOptionItem("Skip data patch", "Skip installing DLC updates.", app->m_skip_data_patch),
                MakeOptionItem("Skip ticket", "Skip installing tickets.", app->m_skip_ticket),
            }
        },
        {
            "Dump",
            "Game dump naming and transfer options.",
            {
                MakeOptionItem("Created nested folder", "Create a nested folder for each game dump.", app->m_dump_app_folder),
                MakeOptionItem("Append folder with .xci", "Append .xci to XCI dump folders.", app->m_dump_append_folder_with_xci),
                MakeOptionItem("Trim XCI", "Remove unused data from XCI dumps.", app->m_dump_trim_xci),
                MakeOptionItem("Label trimmed XCI", "Mark trimmed XCI output names.", app->m_dump_label_trim_xci),
                MakeOptionItem("USB transfer stream", "Stream dump output over USB.", app->m_dump_usb_transfer_stream),
                MakeOptionItem("Convert to common ticket", "Convert personalized tickets during dump.", app->m_dump_convert_to_common_ticket),
            }
        },
        {
            "Advanced",
            "Power-user options and verification controls.",
            {
                MakeBoolItem("Logging", "Write logs to /config/sphaira/log.txt.", App::GetLogEnable, App::SetLogEnable),
                MakeBoolItem("Replace hbmenu on exit", "Replace /hbmenu.nro with Sphaira on exit.", App::GetReplaceHbmenuEnable, App::SetReplaceHbmenuEnable),
                MakeOptionItem("Boost CPU during transfer", "Enable CPU boost during transfers.", app->m_progress_boost_mode),
                MakeOptionItem("Skip NCA hash verify", "Skip SHA-256 verification over NCA content.", app->m_skip_nca_hash_verify),
                MakeOptionItem("Skip RSA header verify", "Skip RSA NCA fixed-key header verification.", app->m_skip_rsa_header_fixed_key_verify),
                MakeOptionItem("Skip RSA NPDM verify", "Skip RSA NPDM fixed-key verification.", app->m_skip_rsa_npdm_fixed_key_verify),
                MakeOptionItem("Ignore distribution bit", "Ignore the NCA distribution bit.", app->m_ignore_distribution_bit),
                MakeOptionItem("Convert to common ticket", "Convert personalized tickets to common tickets.", app->m_convert_to_common_ticket),
                MakeOptionItem("Convert to standard crypto", "Convert titlekey to standard crypto.", app->m_convert_to_standard_crypto),
                MakeOptionItem("Lower master key", "Encrypt key area keys with master key 0.", app->m_lower_master_key),
                MakeOptionItem("Lower system version", "Lower the system firmware field in metadata.", app->m_lower_system_version),
            }
        },
    };
}

void Menu::SetFocusPane(FocusPane pane) {
    m_focus_pane = pane;
}

void Menu::SetCategoryIndex(s64 index) {
    if (m_categories.empty()) {
        m_category_index = 0;
        m_item_index = 0;
        return;
    }

    m_category_index = std::clamp<s64>(index, 0, static_cast<s64>(m_categories.size() - 1));
    m_item_index = 0;
    m_item_list->SetYoff(0);
    if (!m_category_index) {
        m_category_list->SetYoff(0);
    }

    SetSubHeading(m_categories[m_category_index].description);
}

void Menu::SetItemIndex(s64 index) {
    const auto& items = m_categories[m_category_index].items;
    if (items.empty()) {
        m_item_index = 0;
        return;
    }

    m_item_index = std::clamp<s64>(index, 0, static_cast<s64>(items.size() - 1));
    if (!m_item_index) {
        m_item_list->SetYoff(0);
    }
}

void Menu::OnSelect() {
    if (m_categories.empty()) {
        return;
    }

    if (m_focus_pane == FocusPane::Categories) {
        SetFocusPane(FocusPane::Items);
        return;
    }

    const auto& item = m_categories[m_category_index].items[m_item_index];
    if (item.action) {
        item.action();
    }
}

void Menu::OnBack() {
    if (m_focus_pane == FocusPane::Items) {
        SetFocusPane(FocusPane::Categories);
        return;
    }

    SetPop();
}

namespace {

void DrawActionListItem(NVGcontext* vg, Theme* theme, Vec4 v, const SettingsItem& item, bool selected) {
    const auto label_id = selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;

    if (selected) {
        gfx::drawRect(vg, v, theme->GetColour(ThemeEntryID_SELECTED_BACKGROUND), 5.f);
        gfx::drawRectOutline(vg, theme, 4.f, v);
    } else {
        gfx::drawRect(vg, v.x, v.y + v.h, v.w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
    }

    DrawSettingsItemKindIcon(vg, theme, item, v, selected);
    const auto text_x = SettingsItemTextX(item, v.x);
    const auto text_offset = text_x - v.x;

    gfx::drawTextBox(
        vg, text_x, v.y + 10.f, 20.f, v.w - 18.f - text_offset,
        theme->GetColour(label_id), item.label.c_str()
    );
    if (!item.description.empty()) {
        gfx::drawTextBox(
            vg, text_x, v.y + 39.f, 14.f, v.w - 18.f - text_offset,
            theme->GetColour(ThemeEntryID_TEXT_INFO), item.description.c_str()
        );
    }
}

} // namespace

SoftwareMenu::SoftwareMenu() : MenuBase{"Software", MenuFlag_None} {
    m_items = BuildSoftwareItems();
    this->SetActions(
        std::make_pair(Button::A, Action{"Open"_i18n, [this](){
            OnSelect();
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }})
    );

    m_list = std::make_unique<List>(1, 7, m_pos, Vec4{75.f, 132.f, 1130.f, 66.f});
    m_list->SetLayout(List::Layout::GRID);
    SetIndex(0);
}

SoftwareMenu::~SoftwareMenu() = default;

void SoftwareMenu::OnFocusGained() {
    MenuBase::OnFocusGained();
    std::string item_label;
    if (!m_items.empty()) {
        item_label = m_items[m_index].label;
    }
    m_items = BuildSoftwareItems();
    auto it = std::find_if(m_items.cbegin(), m_items.cend(), [&](const auto& item) {
        return item.label == item_label;
    });
    SetIndex(it == m_items.cend() ? m_index : std::distance(m_items.cbegin(), it));
}

void SoftwareMenu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);
    m_list->OnUpdate(controller, touch, m_index, m_items.size(), [this](bool touch, auto i) {
        if (touch && m_index == i) {
            FireAction(Button::A);
        } else {
            App::PlaySoundEffect(SoundEffect_Focus);
            SetIndex(i);
        }
    });
}

void SoftwareMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);
    m_list->Draw(vg, theme, m_items.size(), [this](auto* vg, auto* theme, Vec4 v, auto i) {
        DrawActionListItem(vg, theme, v, m_items[i], m_index == i);
    });
}

void SoftwareMenu::SetIndex(s64 index) {
    if (m_items.empty()) {
        m_index = 0;
        return;
    }
    m_index = std::clamp<s64>(index, 0, static_cast<s64>(m_items.size() - 1));
    if (!m_index) {
        m_list->SetYoff(0);
    }
    SetSubHeading(m_items[m_index].description);
}

void SoftwareMenu::OnSelect() {
    if (!m_items.empty() && m_items[m_index].action) {
        m_items[m_index].action();
    }
}

DbiMenu::DbiMenu() : MenuBase{"DBI", MenuFlag_None} {
    m_items = BuildDbiItems();
    this->SetActions(
        std::make_pair(Button::A, Action{"Open"_i18n, [this](){
            OnSelect();
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }})
    );

    m_list = std::make_unique<List>(1, 7, m_pos, Vec4{75.f, 132.f, 1130.f, 66.f});
    m_list->SetLayout(List::Layout::GRID);
    SetIndex(0);
}

DbiMenu::~DbiMenu() = default;

void DbiMenu::OnFocusGained() {
    MenuBase::OnFocusGained();
    std::string item_label;
    if (!m_items.empty()) {
        item_label = m_items[m_index].label;
    }
    m_items = BuildDbiItems();
    auto it = std::find_if(m_items.cbegin(), m_items.cend(), [&](const auto& item) {
        return item.label == item_label;
    });
    SetIndex(it == m_items.cend() ? m_index : std::distance(m_items.cbegin(), it));
}

void DbiMenu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);
    m_list->OnUpdate(controller, touch, m_index, m_items.size(), [this](bool touch, auto i) {
        if (touch && m_index == i) {
            FireAction(Button::A);
        } else {
            App::PlaySoundEffect(SoundEffect_Focus);
            SetIndex(i);
        }
    });
}

void DbiMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);
    m_list->Draw(vg, theme, m_items.size(), [this](auto* vg, auto* theme, Vec4 v, auto i) {
        DrawActionListItem(vg, theme, v, m_items[i], m_index == i);
    });
}

void DbiMenu::SetIndex(s64 index) {
    if (m_items.empty()) {
        m_index = 0;
        return;
    }
    m_index = std::clamp<s64>(index, 0, static_cast<s64>(m_items.size() - 1));
    if (!m_index) {
        m_list->SetYoff(0);
    }
    SetSubHeading(m_items[m_index].description);
}

void DbiMenu::OnSelect() {
    if (!m_items.empty() && m_items[m_index].action) {
        m_items[m_index].action();
    }
}

ThemesMenu::ThemesMenu() : MenuBase{"Themes", MenuFlag_None} {
    m_items = BuildThemeItems();
    this->SetActions(
        std::make_pair(Button::A, Action{"Open"_i18n, [this](){
            OnSelect();
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }})
    );

    m_list = std::make_unique<List>(1, 7, m_pos, Vec4{75.f, 132.f, 1130.f, 66.f});
    m_list->SetLayout(List::Layout::GRID);
    SetIndex(0);
}

ThemesMenu::~ThemesMenu() = default;

void ThemesMenu::OnFocusGained() {
    MenuBase::OnFocusGained();
    SetIndex(m_index);
}

void ThemesMenu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);
    m_list->OnUpdate(controller, touch, m_index, m_items.size(), [this](bool touch, auto i) {
        if (touch && m_index == i) {
            FireAction(Button::A);
        } else {
            App::PlaySoundEffect(SoundEffect_Focus);
            SetIndex(i);
        }
    });
}

void ThemesMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);
    m_list->Draw(vg, theme, m_items.size(), [this](auto* vg, auto* theme, Vec4 v, auto i) {
        DrawActionListItem(vg, theme, v, m_items[i], m_index == i);
    });
}

void ThemesMenu::SetIndex(s64 index) {
    if (m_items.empty()) {
        m_index = 0;
        return;
    }
    m_index = std::clamp<s64>(index, 0, static_cast<s64>(m_items.size() - 1));
    if (!m_index) {
        m_list->SetYoff(0);
    }
    SetSubHeading(m_items[m_index].description);
}

void ThemesMenu::OnSelect() {
    if (!m_items.empty() && m_items[m_index].action) {
        m_items[m_index].action();
    }
}

} // namespace sphaira::ui::menu::settings
