#include "ui/menus/kefir_menu.hpp"

#include "ui/error_box.hpp"
#include "ui/menus/ghdl.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"

#include "ams_su.h"
#include "app.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "hats_version.hpp"
#include "i18n.hpp"
#include "threaded_file_transfer.hpp"
#include "utils/utils.hpp"

#include <yyjson.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <string_view>
#include <utility>

namespace sphaira::ui::menu::kefir {
namespace {

constexpr const char* NXLINKS_URL = "https://raw.githubusercontent.com/rashevskyv/nx-links/master/nx-links.json";
constexpr const char* CACHE_DIR = "/config/kefir-updater";
constexpr const char* NXLINKS_CACHE = "/config/kefir-updater/nx-links.json";
constexpr const char* AMS_ZIP = "/config/kefir-updater/atmo.zip";
constexpr const char* FIRMWARE_ZIP = "/config/kefir-updater/firmware.zip";
constexpr const char* KEFIR_PATH = "/kefir";
constexpr const char* FIRMWARE_DEST = "/firmware";
constexpr const char* KEFIR_VERSION_PATH = "/switch/kefir-updater/version";
constexpr const char* COPY_FILES_TXT = "/config/kefir-updater/copy_files.txt";
constexpr const char* STAGED_COPY_FILES_TXT = "/kefir/config/kefir-updater/copy_files.txt";
constexpr size_t UPDATE_TASK_BUFFER_SIZE = 0x100000;

struct FirmwareValidation {
    AmsSuUpdateInformation info{};
    AmsSuUpdateValidationInfo validation{};
};

auto Trim(std::string value) -> std::string {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }

    size_t start{};
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        start++;
    }

    if (start) {
        value.erase(0, start);
    }
    return value;
}

auto ReadFirstLine(const char* path) -> std::string {
    FILE* file = std::fopen(path, "r");
    if (!file) {
        return "Not Found";
    }
    ON_SCOPE_EXIT(std::fclose(file));

    char line[128]{};
    if (!std::fgets(line, sizeof(line), file)) {
        return "Not Found";
    }

    auto value = Trim(line);
    return value.empty() ? "Not Found" : value;
}

auto FindDigitsAfter(const std::string& value, std::string_view marker) -> std::string {
    auto pos = value.find(marker);
    if (pos == std::string::npos) {
        return {};
    }

    pos += marker.size();
    const auto start = pos;
    while (pos < value.size() && std::isdigit(static_cast<unsigned char>(value[pos]))) {
        pos++;
    }

    if (pos == start) {
        return {};
    }
    return value.substr(start, pos - start);
}

auto ExtractKefirVersion(const std::string& name, const std::string& url) -> std::string {
    for (const auto* value : { &url, &name }) {
        if (auto version = FindDigitsAfter(*value, "/download/"); !version.empty()) {
            return version;
        }
        if (auto version = FindDigitsAfter(*value, "kefir_"); !version.empty()) {
            return version;
        }
    }

    for (const auto* value : { &url, &name }) {
        for (size_t i = 0; i < value->size(); i++) {
            if (!std::isdigit(static_cast<unsigned char>((*value)[i]))) {
                continue;
            }

            size_t end = i;
            while (end < value->size() && std::isdigit(static_cast<unsigned char>((*value)[end]))) {
                end++;
            }

            const auto len = end - i;
            if (len >= 3 && len <= 4) {
                return value->substr(i, len);
            }
            i = end;
        }
    }

    return {};
}

auto MakeKefirLatestLabel(const UpdaterEntry& entry) -> std::string {
    if (const auto version = ExtractKefirVersion(entry.name, entry.url); !version.empty()) {
        return version;
    }
    return entry.name;
}

auto TypeLabel(UpdaterEntryType type) -> const char* {
    switch (type) {
        case UpdaterEntryType::Section:
            return "";
        case UpdaterEntryType::Network:
            return "Network";
        case UpdaterEntryType::CustomLink:
            return "Other";
        case UpdaterEntryType::Kefir:
            return "Version";
        case UpdaterEntryType::Firmware:
            return "Firmware";
    }
    return "";
}

auto EntryDescription(const UpdaterEntry& entry) -> const char* {
    if (entry.type == UpdaterEntryType::Network) {
        return "Open GitHub releases and direct links.";
    }
    if (entry.type == UpdaterEntryType::CustomLink) {
        return "Enter a ZIP URL and extract it to the SD card.";
    }
    return entry.url.c_str();
}

auto EntryDisplayName(const UpdaterEntry& entry) -> std::string {
    if (entry.type != UpdaterEntryType::Kefir) {
        return entry.name;
    }

    if (const auto version = ExtractKefirVersion(entry.name, entry.url); !version.empty()) {
        return "Version " + version;
    }

    auto name = entry.name;
    if (name.starts_with("Kefir")) {
        name.erase(0, std::strlen("Kefir"));
        name = Trim(name);
    }
    return name.empty() ? "Version" : "Version " + name;
}

auto IsSelectableEntry(const UpdaterEntry& entry) -> bool {
    return entry.type != UpdaterEntryType::Section;
}

auto ResolveSelectableIndex(const std::vector<UpdaterEntry>& entries, s64 index, s64 previous) -> s64 {
    if (entries.empty()) {
        return 0;
    }

    index = std::clamp<s64>(index, 0, static_cast<s64>(entries.size() - 1));
    if (IsSelectableEntry(entries[index])) {
        return index;
    }

    const auto direction = index >= previous ? 1 : -1;
    for (s64 i = index; i >= 0 && i < static_cast<s64>(entries.size()); i += direction) {
        if (IsSelectableEntry(entries[i])) {
            return i;
        }
    }

    for (s64 i = index; i >= 0 && i < static_cast<s64>(entries.size()); i -= direction) {
        if (IsSelectableEntry(entries[i])) {
            return i;
        }
    }

    return 0;
}

auto SelectableCount(const std::vector<UpdaterEntry>& entries) -> s64 {
    return std::count_if(entries.begin(), entries.end(), IsSelectableEntry);
}

auto SelectablePosition(const std::vector<UpdaterEntry>& entries, s64 index) -> s64 {
    s64 position{};
    for (s64 i = 0; i <= index && i < static_cast<s64>(entries.size()); i++) {
        if (IsSelectableEntry(entries[i])) {
            position++;
        }
    }
    return position;
}

void AddSectionEntry(std::vector<UpdaterEntry>& out, std::string name) {
    out.push_back({
        .type = UpdaterEntryType::Section,
        .name = std::move(name),
        .url = {},
        .pack = false,
    });
}

void AddNetworkEntry(std::vector<UpdaterEntry>& out) {
    out.push_back({
        .type = UpdaterEntryType::Network,
        .name = "Network Downloads",
        .url = {},
        .pack = false,
    });
}

void AddCustomLinkEntry(std::vector<UpdaterEntry>& out) {
    out.push_back({
        .type = UpdaterEntryType::CustomLink,
        .name = "Custom Link",
        .url = {},
        .pack = false,
    });
}

void AppendEntriesOfType(std::vector<UpdaterEntry>& out, const std::vector<UpdaterEntry>& entries, UpdaterEntryType type) {
    for (const auto& entry : entries) {
        if (entry.type == type) {
            out.push_back(entry);
        }
    }
}

void BuildSectionedEntries(std::vector<UpdaterEntry>& out, const std::vector<UpdaterEntry>& downloads) {
    out.clear();

    AddSectionEntry(out, "KEFIR");
    AppendEntriesOfType(out, downloads, UpdaterEntryType::Kefir);

    AddSectionEntry(out, "FIRMWARE");
    AppendEntriesOfType(out, downloads, UpdaterEntryType::Firmware);

    AddSectionEntry(out, "OTHER");
    AddNetworkEntry(out);
    AddCustomLinkEntry(out);
}

auto AddJsonEntries(yyjson_val* object, UpdaterEntryType type, std::vector<UpdaterEntry>& out, std::string& latest_kefir, bool& latest_from_pack) -> bool {
    if (!object || !yyjson_is_obj(object)) {
        return false;
    }

    bool found{};
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(object, &iter);
    yyjson_val* key;
    while ((key = yyjson_obj_iter_next(&iter))) {
        auto value = yyjson_obj_iter_get_val(key);
        const auto name = yyjson_get_str(key);
        const auto url = yyjson_get_str(value);
        if (!name || !url || !*url) {
            continue;
        }

        UpdaterEntry entry{
            .type = type,
            .name = name,
            .url = url,
            .pack = std::string_view{name}.find("[PACK]") != std::string_view::npos,
        };

        if (entry.type == UpdaterEntryType::Kefir && (latest_kefir.empty() || (entry.pack && !latest_from_pack))) {
            latest_kefir = MakeKefirLatestLabel(entry);
            latest_from_pack = entry.pack;
        }

        out.push_back(std::move(entry));
        found = true;
    }

    return found;
}

auto ParseUpdaterLinks(const fs::FsPath& path, std::vector<UpdaterEntry>& out, std::string& latest_kefir) -> bool {
    out.clear();
    latest_kefir.clear();

    auto doc = yyjson_read_file(path, YYJSON_READ_NOFLAG, nullptr, nullptr);
    if (!doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    auto root = yyjson_doc_get_root(doc);
    auto cfws = root ? yyjson_obj_get(root, "cfws") : nullptr;
    auto atmosphere = cfws ? yyjson_obj_get(cfws, "Atmosphere") : nullptr;
    auto firmwares = root ? yyjson_obj_get(root, "firmwares") : nullptr;

    bool latest_from_pack{};
    bool found{};
    found |= AddJsonEntries(atmosphere, UpdaterEntryType::Kefir, out, latest_kefir, latest_from_pack);
    found |= AddJsonEntries(firmwares, UpdaterEntryType::Firmware, out, latest_kefir, latest_from_pack);
    return found;
}

auto CopyIfExists(ProgressBox* pbox, fs::FsNativeSd& fs, const fs::FsPath& src, const fs::FsPath& dst) -> Result {
    if (!fs.FileExists(src)) {
        R_SUCCEED();
    }

    R_TRY(fs.CreateDirectoryRecursivelyWithPath(dst));
    pbox->NewTransfer("Copying " + dst.toString());
    return pbox->CopyFile(&fs, src, dst, true);
}

auto CopyListedFiles(ProgressBox* pbox, fs::FsNativeSd& fs, const char* list_path) -> Result {
    FILE* file = std::fopen(list_path, "r");
    if (!file) {
        R_SUCCEED();
    }
    ON_SCOPE_EXIT(std::fclose(file));

    char line[FS_MAX_PATH * 2]{};
    while (std::fgets(line, sizeof(line), file)) {
        std::string entry{line};
        while (!entry.empty() && (entry.back() == '\n' || entry.back() == '\r')) {
            entry.pop_back();
        }
        if (entry.empty()) {
            continue;
        }

        const auto sep = entry.find('|');
        if (sep == std::string::npos) {
            continue;
        }

        fs::FsPath src{entry.substr(0, sep)};
        fs::FsPath dst{entry.substr(sep + 1)};
        if (!fs.FileExists(src) && src.s[0] == '/') {
            src = std::string{KEFIR_PATH} + src.s;
        }
        R_TRY(CopyIfExists(pbox, fs, src, dst));
    }

    R_SUCCEED();
}

auto DownloadAndInstallKefir(ProgressBox* pbox, const UpdaterEntry& entry) -> Result {
    fs::FsNativeSd fs;
    R_TRY(fs.GetFsOpenResult());

    R_TRY(fs.CreateDirectoryRecursively(CACHE_DIR));

    if (fs.FileExists(AMS_ZIP)) {
        fs.DeleteFile(AMS_ZIP);
    }

    pbox->NewTransfer("Downloading " + entry.name);
    const auto result = curl::Api().ToFile(
        curl::Url{entry.url},
        curl::Path{AMS_ZIP},
        curl::OnProgress{pbox->OnDownloadProgressCallback()}
    );
    R_UNLESS(result.success, 0x1);

    if (fs.DirExists(KEFIR_PATH)) {
        R_TRY(fs.DeleteDirectoryRecursively(KEFIR_PATH));
    }
    R_TRY(fs.CreateDirectoryRecursively(KEFIR_PATH));

    pbox->NewTransfer("Extracting to /kefir...");
    R_TRY(thread::TransferUnzipAll(pbox, AMS_ZIP, &fs, KEFIR_PATH));
    R_TRY(fs.Commit());

    R_TRY(CopyIfExists(pbox, fs, "/kefir/bootloader/hekate_ipl.ini", "/bootloader/hekate_ipl.ini"));
    R_TRY(CopyIfExists(pbox, fs, "/kefir/config/kefir-updater/kefir_updater.ini", "/bootloader/ini/!kefir_updater.ini"));
    R_TRY(CopyIfExists(pbox, fs, "/kefir/bootloader/res/ku.bmp", "/bootloader/res/ku.bmp"));
    R_TRY(CopyListedFiles(pbox, fs, COPY_FILES_TXT));
    R_TRY(CopyListedFiles(pbox, fs, STAGED_COPY_FILES_TXT));

    if (fs.FileExists(AMS_ZIP)) {
        fs.DeleteFile(AMS_ZIP);
    }
    R_TRY(fs.Commit());

    R_SUCCEED();
}

auto FormatFirmwareVersion(u32 version) -> std::string {
    return std::to_string((version >> 26) & 0x1f) + "." +
           std::to_string((version >> 20) & 0x1f) + "." +
           std::to_string((version >> 16) & 0xf);
}

auto BuildFirmwareServicePath(const fs::FsPath& path) -> std::string {
    std::string service_path = path.s;
    if (service_path.empty()) {
        service_path = FIRMWARE_DEST;
    }
    if (service_path.back() != '/') {
        service_path += '/';
    }
    return service_path;
}

auto ValidateFirmware(FirmwareValidation* out, const fs::FsPath& path) -> Result {
    Result rc = amssuInitialize();
    if (R_FAILED(rc)) {
        return rc;
    }
    ON_SCOPE_EXIT(amssuExit());

    const auto service_path = BuildFirmwareServicePath(path);
    R_TRY(amssuGetUpdateInformation(&out->info, service_path.c_str()));
    R_TRY(amssuValidateUpdate(&out->validation, service_path.c_str()));
    return out->validation.result;
}

auto InstallValidatedFirmware(ProgressBox* pbox, bool use_exfat, const fs::FsPath& path) -> Result {
    Result rc = amssuInitialize();
    if (R_FAILED(rc)) {
        return rc;
    }
    ON_SCOPE_EXIT(amssuExit());

    const auto service_path = BuildFirmwareServicePath(path);
    pbox->NewTransfer("Setting up system update...");
    R_TRY(amssuSetupUpdate(nullptr, UPDATE_TASK_BUFFER_SIZE, service_path.c_str(), use_exfat));

    AsyncResult prepare{};
    R_TRY(amssuRequestPrepareUpdate(&prepare));
    ON_SCOPE_EXIT(asyncResultClose(&prepare));
    pbox->NewTransfer("Preparing system update...");

    while (true) {
        rc = asyncResultWait(&prepare, 0);
        if (R_FAILED(rc) && rc != 0xea01) {
            return rc;
        }
        if (R_SUCCEEDED(rc)) {
            R_TRY(asyncResultGet(&prepare));
        }

        bool prepared = false;
        R_TRY(amssuHasPreparedUpdate(&prepared));
        if (prepared) {
            break;
        }

        NsSystemUpdateProgress progress{};
        R_TRY(amssuGetPrepareUpdateProgress(&progress));
        pbox->UpdateTransfer(progress.current_size, progress.total_size);
        svcSleepThread(50'000'000);
    }

    pbox->NewTransfer("Applying system update...");
    return amssuApplyPreparedUpdate();
}

auto GetFirmwareTargetName() -> std::string {
    bool emummc = false;
    Result rc = splInitialize();
    if (R_SUCCEEDED(rc)) {
        u64 value{};
        if (R_SUCCEEDED(splGetConfig((SplConfigItem)65007, &value))) {
            emummc = value != 0;
        }
        splExit();
    }
    return emummc ? "emuMMC" : "sysMMC";
}

auto ParseVersion(const std::string& version) -> std::vector<int> {
    std::vector<int> parts;
    std::stringstream ss(version);
    std::string segment;

    while (std::getline(ss, segment, '.')) {
        if (segment.empty()) {
            continue;
        }

        char* end = nullptr;
        const auto part = std::strtol(segment.c_str(), &end, 10);
        if (end == segment.c_str()) {
            break;
        }
        parts.push_back(static_cast<int>(part));
    }

    return parts;
}

auto IsVersionLower(const std::string& target, const std::string& current) -> bool {
    const auto target_parts = ParseVersion(target);
    const auto current_parts = ParseVersion(current);
    const auto max_len = std::max(target_parts.size(), current_parts.size());

    for (size_t i = 0; i < max_len; i++) {
        const auto t = i < target_parts.size() ? target_parts[i] : 0;
        const auto c = i < current_parts.size() ? current_parts[i] : 0;
        if (t < c) {
            return true;
        }
        if (t > c) {
            return false;
        }
    }

    return false;
}

auto DownloadAndExtractFirmware(ProgressBox* pbox, const UpdaterEntry& entry) -> Result {
    fs::FsNativeSd fs;
    R_TRY(fs.GetFsOpenResult());

    R_TRY(fs.CreateDirectoryRecursively(CACHE_DIR));

    if (fs.FileExists(FIRMWARE_ZIP)) {
        fs.DeleteFile(FIRMWARE_ZIP);
    }

    pbox->NewTransfer("Downloading " + entry.name);
    const auto result = curl::Api().ToFile(
        curl::Url{entry.url},
        curl::Path{FIRMWARE_ZIP},
        curl::OnProgress{pbox->OnDownloadProgressCallback()}
    );
    R_UNLESS(result.success, 0x1);

    if (fs.DirExists(FIRMWARE_DEST)) {
        R_TRY(fs.DeleteDirectoryRecursively(FIRMWARE_DEST));
    }
    R_TRY(fs.CreateDirectoryRecursively(FIRMWARE_DEST));

    pbox->NewTransfer("Extracting to /firmware...");
    R_TRY(thread::TransferUnzipAll(pbox, FIRMWARE_ZIP, &fs, FIRMWARE_DEST));
    R_TRY(fs.Commit());

    if (fs.FileExists(FIRMWARE_ZIP)) {
        fs.DeleteFile(FIRMWARE_ZIP);
    }
    R_TRY(fs.Commit());

    R_SUCCEED();
}

} // namespace

Menu::Menu() : MenuBase{"Updater", MenuFlag_None} {
    RefreshSystemInfo();

    this->SetActions(
        std::make_pair(Button::A, Action{"Open"_i18n, [this](){
            if (!m_entries.empty() && !m_loading) {
                OpenSelected();
            }
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }}),
        std::make_pair(Button::X, Action{"Refresh"_i18n, [this](){
            m_loaded = false;
            RefreshSystemInfo();
            FetchLinks();
        }})
    );

    const Vec4 v{75.f, GetY() + 1.f + 66.f, 1220.f - 150.f, 50.f};
    m_list = std::make_unique<List>(1, 9, m_pos, v);
    m_list->SetLayout(List::Layout::GRID);
}

Menu::~Menu() = default;

void Menu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    if (!m_entries.empty()) {
        m_list->OnUpdate(controller, touch, m_index, m_entries.size(), [this](bool touch, auto i) {
            if (touch && m_index == i) {
                FireAction(Button::A);
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                SetIndex(i);
            }
        });
    }
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    const auto info_colour = theme->GetColour(ThemeEntryID_TEXT_INFO);
    const auto info_y = GetY() + 11.f;
    gfx::drawTextArgs(vg, 80.f, info_y, 17.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
        info_colour, "Current Kefir: %s", m_current_kefir.c_str());
    gfx::drawTextArgs(vg, 650.f, info_y, 17.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
        info_colour, "Latest Kefir: %s", m_latest_kefir.c_str());
    gfx::drawTextArgs(vg, 80.f, info_y + 25.f, 17.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
        info_colour, "Current Firmware: %s", m_current_firmware.c_str());
    gfx::drawTextArgs(vg, 650.f, info_y + 25.f, 17.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
        info_colour, "Console: %s", m_console_revision.c_str());

    if (m_loading) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO),
            "Loading updater links...");
        return;
    }

    if (!m_error_message.empty()) {
        gfx::drawTextArgs(vg, 80.f, GetY() + 71.f, 17.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_ERROR),
            "%s", m_error_message.c_str());
    }

    if (m_entries.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO),
            "No updater entries found");
        return;
    }

    m_list->Draw(vg, theme, m_entries.size(), [this](auto* vg, auto* theme, Vec4 v, auto i) {
        const auto& entry = m_entries[i];
        if (entry.type == UpdaterEntryType::Section) {
            const auto top_pad = 16.f;
            const Vec4 band{v.x, v.y + top_pad, v.w, 26.f};
            gfx::drawRect(vg, band.x, band.y, band.w, band.h, theme->GetColour(ThemeEntryID_SELECTED_BACKGROUND), 4.f);
            gfx::drawRect(vg, v.x + 15.f, band.y + 7.f, 4.f, band.h - 14.f, theme->GetColour(ThemeEntryID_TEXT_SELECTED), 2.f);
            gfx::drawTextArgs(vg, v.x + 30.f, band.y + band.h / 2.f, 15.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_SELECTED),
                "%s", entry.name.c_str());
            return;
        }

        const auto selected = m_index == i;
        const auto downgrade = entry.type == UpdaterEntryType::Firmware && IsDowngrade(entry.name);
        const auto text_id = selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;
        const auto name_id = downgrade ? ThemeEntryID_ERROR : text_id;

        if (selected) {
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else if (i != m_entries.size() - 1) {
            gfx::drawRect(vg, v.x, v.y + v.h, v.w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
        }

        std::string name = EntryDisplayName(entry);
        if (downgrade) {
            name += " [DOWNGRADE]";
        }

        gfx::drawTextBox(vg, v.x + 15.f, v.y + 10.f, 20.f, v.w - 190.f,
            theme->GetColour(name_id), name.c_str());

        if (entry.type != UpdaterEntryType::Kefir) {
            gfx::drawTextArgs(vg, v.x + v.w - 15.f, v.y + 14.f, 14.f,
                NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT_INFO),
                "%s", TypeLabel(entry.type));
        }

        gfx::drawTextBox(vg, v.x + 15.f, v.y + 34.f, 14.f, v.w - 30.f,
            theme->GetColour(ThemeEntryID_TEXT_INFO), EntryDescription(entry));
    });
}

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();
    RefreshSystemInfo();

    if (!m_loaded && !m_loading) {
        FetchLinks();
    }
}

void Menu::FetchLinks() {
    m_loading = true;
    m_error_message.clear();
    m_entries.clear();
    m_latest_kefir = "Unknown";
    BuildSectionedEntries(m_entries, {});
    SetIndex(0);

    fs::FsNativeSd().CreateDirectoryRecursively(CACHE_DIR);

    curl::Api().ToFileAsync(
        curl::Url{NXLINKS_URL},
        curl::Path{NXLINKS_CACHE},
        curl::Flags{curl::Flag_Cache},
        curl::StopToken{this->GetToken()},
        curl::OnComplete{[this](auto& result) {
            m_loading = false;
            m_loaded = true;

            std::vector<UpdaterEntry> entries;
            std::string latest_kefir;
            if (!result.success || !ParseUpdaterLinks(result.path, entries, latest_kefir)) {
                m_error_message = "Failed to load updater lists.";
                SetIndex(0);
                return false;
            }

            BuildSectionedEntries(m_entries, entries);
            m_latest_kefir = latest_kefir.empty() ? "Unknown" : latest_kefir;

            if (entries.empty()) {
                m_error_message = "No Kefir or firmware downloads found.";
            }

            SetIndex(0);
            return true;
        }}
    );
}

void Menu::SetIndex(s64 index) {
    m_index = ResolveSelectableIndex(m_entries, index, m_index);
    if (m_index <= 1) {
        m_list->SetYoff(0);
    }
    UpdateSubheading();
}

void Menu::OpenSelected() {
    if (m_entries.empty() || m_index >= static_cast<s64>(m_entries.size())) {
        return;
    }

    const auto entry = m_entries[m_index];
    switch (entry.type) {
        case UpdaterEntryType::Network:
            App::Push<ui::menu::gh::Menu>(MenuFlag_None);
            break;
        case UpdaterEntryType::CustomLink:
            ui::menu::gh::DownloadDirectLink();
            break;
        case UpdaterEntryType::Kefir:
            InstallKefir(entry);
            break;
        case UpdaterEntryType::Firmware:
            DownloadFirmware(entry);
            break;
        case UpdaterEntryType::Section:
            break;
    }
}

void Menu::InstallKefir(const UpdaterEntry& entry) {
    std::string message = "Download and install " + entry.name + "?\n\n";
    message += "The archive will be staged like Kefir Updater:\n";
    message += AMS_ZIP;
    message += "\n\nThen it will be extracted to /kefir and Kefir bootloader files will be copied into place.";

    App::Push<OptionBox>(message, "Cancel"_i18n, "Install"_i18n, 1,
        [entry](auto op_index) {
            if (!op_index || *op_index != 1) {
                return;
            }

            App::Push<ProgressBox>(0, "Installing"_i18n, entry.name,
                [entry](auto pbox) -> Result {
                    return DownloadAndInstallKefir(pbox, entry);
                },
                [entry](Result rc) {
                    if (R_FAILED(rc)) {
                        App::Push<ErrorBox>(rc, "Failed to install " + entry.name);
                        return;
                    }

                    App::Push<OptionBox>(
                        "Kefir package installed.\n\nReboot now?",
                        "Later"_i18n, "Reboot"_i18n, 1,
                        [](auto op_index) {
                            if (op_index && *op_index == 1) {
                                utils::requestForcedReboot();
                            }
                        });
                });
        });
}

void Menu::DownloadFirmware(const UpdaterEntry& entry) {
    const auto downgrade = IsDowngrade(entry.name);

    std::string message = "Download firmware " + entry.name + "?\n\n";
    message += "It will be staged at ";
    message += FIRMWARE_ZIP;
    message += " and extracted to /firmware.";

    if (downgrade) {
        message = "Firmware downgrade warning\n\n";
        message += "Current: " + m_current_firmware + "\n";
        message += "Target: " + entry.name + "\n\n";
        message += "Downgrading system firmware may prevent the console from booting until a factory reset is performed.\n\n";
        message += "Fix path: hekate > Payloads > TegraExplorer > DowngradeFix.te\n";
        message += "Guide: https://bit.ly/fw_downgrade\n\n";
        message += "Continue?";
    }

    App::Push<OptionBox>(message, "Cancel"_i18n, "Download"_i18n, 1,
        [this, entry](auto op_index) {
            if (!op_index || *op_index != 1) {
                return;
            }

            App::Push<ProgressBox>(0, "Downloading"_i18n, entry.name,
                [entry](auto pbox) -> Result {
                    return DownloadAndExtractFirmware(pbox, entry);
                },
                [this, entry](Result rc) {
                    if (R_FAILED(rc)) {
                        App::Push<ErrorBox>(rc, "Failed to download " + entry.name);
                        return;
                    }

                    PromptInstallFirmware(entry.name);
                });
        });
}

void Menu::PromptInstallFirmware(const std::string& display_name, const fs::FsPath& path) {
    auto validation = std::make_shared<FirmwareValidation>();
    App::Push<ProgressBox>(0, "Validating"_i18n, display_name,
        [validation, path](auto pbox) -> Result {
            pbox->NewTransfer("Validating firmware contents...");
            return ValidateFirmware(validation.get(), path);
        },
        [this, display_name, path, validation](Result rc) {
            if (R_FAILED(rc)) {
                App::Push<ErrorBox>(rc, "Firmware validation failed");
                return;
            }

            const auto version = FormatFirmwareVersion(validation->info.version);
            const bool use_exfat = validation->info.exfat_supported &&
                                   R_SUCCEEDED(validation->validation.exfat_result);
            std::string message = "Install firmware " + version + " on " + GetFirmwareTargetName() + "?\n\n";
            message += use_exfat ? "FAT32 + exFAT support\n" : "FAT32 support only\n";
            message += "Do not power off the console during installation.";

            App::Push<OptionBox>(message, "Cancel"_i18n, "Install"_i18n, 1,
                [this, display_name, path](auto op_index) {
                    if (op_index && *op_index == 1) {
                        InstallFirmware(display_name, path);
                    }
                });
        });
}

void Menu::InstallFirmware(const std::string& display_name, const fs::FsPath& path) {
    App::Push<ProgressBox>(0, "Updating Firmware"_i18n, display_name,
        [path](auto pbox) -> Result {
            FirmwareValidation validation{};
            R_TRY(ValidateFirmware(&validation, path));
            const bool use_exfat = validation.info.exfat_supported &&
                                   R_SUCCEEDED(validation.validation.exfat_result);
            return InstallValidatedFirmware(pbox, use_exfat, path);
        },
        [](Result rc) {
            if (R_FAILED(rc)) {
                App::Push<ErrorBox>(rc, "Firmware update failed");
                return;
            }

            App::Push<OptionBox>(
                "Firmware update applied successfully.\n\nReboot now?",
                "Later"_i18n, "Reboot"_i18n, 1,
                [](auto op_index) {
                    if (op_index && *op_index == 1) {
                        utils::requestForcedReboot();
                    }
                });
        }, false);
}

void Menu::UpdateSubheading() {
    const auto count = SelectableCount(m_entries);
    if (m_entries.empty() || !count) {
        this->SetSubHeading("0 / 0");
        return;
    }

    const auto& entry = m_entries[m_index];
    this->SetSubHeading(std::to_string(SelectablePosition(m_entries, m_index)) + " / " + std::to_string(count) + " - " + TypeLabel(entry.type));
}

void Menu::RefreshSystemInfo() {
    m_current_kefir = ReadFirstLine(KEFIR_VERSION_PATH);
    m_current_firmware = hats::getSystemFirmware();
    m_console_revision = hats::isErista() ? "Erista (v1)" : "Mariko (v2)";
    if (m_latest_kefir.empty()) {
        m_latest_kefir = "Unknown";
    }
}

bool Menu::IsDowngrade(const std::string& target_version) const {
    return IsVersionLower(target_version, m_current_firmware);
}

} // namespace sphaira::ui::menu::kefir
