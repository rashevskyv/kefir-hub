#include "ui/menus/kefir/kefir_firmware.hpp"
#include "version_compare.hpp"
#include "ui/menus/kefir/kefir_changelog.hpp"
#include "hats_version.hpp"
#include "app.hpp"
#include "download.hpp"
#include "threaded_file_transfer.hpp"
#include "utils/utils.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include <yyjson.h>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>


namespace sphaira::ui::menu::kefir {
namespace detail {

constexpr const char* KEFIR_VERSION_PATH = "/switch/kefir-updater/version";
constexpr const char* FIRMWARE_DEST = "/firmware";
constexpr const char* CACHE_DIR = "/config/kefir-updater";
constexpr const char* FIRMWARE_ZIP = "/config/kefir-updater/firmware.zip";

auto ReadLineNumber(const char* path, size_t line_index) -> std::string {
    FILE* file = std::fopen(path, "r");
    if (!file) {
        return "Not Found";
    }
    ON_SCOPE_EXIT(std::fclose(file));

    char line[128]{};
    for (size_t i = 0; i <= line_index; i++) {
        if (!std::fgets(line, sizeof(line), file)) {
            return "Not Found";
        }
    }

    auto value = ::sphaira::utils::TrimAsciiWhitespace(line);
    return value.empty() ? "Not Found" : value;
}

auto ReadFirstLine(const char* path) -> std::string {
    return ReadLineNumber(path, 0);
}

auto ReadSecondLine(const char* path) -> std::string {
    return ReadLineNumber(path, 1);
}

auto IsKnownVersion(const std::string& version) -> bool {
    if (version.empty() || version == "Not Found" || version == "Unknown") {
        return false;
    }

    return std::any_of(version.begin(), version.end(), [](unsigned char c) {
        return std::isdigit(c);
    });
}

auto FirmwareUnsupportedReason(const std::string& target, const std::string& supported) -> std::string {
    std::string message = "Firmware " + target + " is not supported by the current Kefir.";
    if (IsKnownVersion(supported)) {
        message += "\n\nCurrent Kefir supports system firmware up to " + supported + ".";
    }
    message += "\n\nUpdate Kefir first?";
    return message;
}

auto UnsupportedFirmwareLabel(const std::string& supported) -> std::string {
    if (IsKnownVersion(supported)) {
        return "Unsupported > " + supported;
    }
    return "Unsupported";
}

auto ReadCurrentKefirSupportedFirmware() -> std::string {
    const auto value = ReadSecondLine(KEFIR_VERSION_PATH);
    if (!IsKnownVersion(value)) {
        return "Not Found";
    }
    return value;
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

auto ParseVersion(const std::string& version) -> std::vector<int> {
    return version::Parse(version);
}

auto IsVersionLower(const std::string& target, const std::string& current) -> bool {
    return version::IsLower(target, current);
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

auto IsVersionHeaderLine(const std::string& line) -> bool {
    const auto trimmed = ::sphaira::utils::TrimAsciiWhitespace(line);
    if (!IsFullBoldLine(trimmed)) {
        return false;
    }

    const auto content = trimmed.substr(2, trimmed.size() - 4);
    return !content.empty() && std::all_of(content.begin(), content.end(), [](unsigned char c) {
        return std::isdigit(c);
    });
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

auto FormatFirmwareVersion(u32 version) -> std::string {
    return version::FormatPacked(version);
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

namespace {

auto ReadRomfsTe(const char* romfs_path, std::string& out, Result* err = nullptr) -> bool {
    const auto read = [&]() {
        std::vector<u8> bytes;
        if (R_FAILED(fs::read_entire_file(romfs_path, bytes)) || bytes.empty() || bytes.size() > 1024 * 1024) {
            return false;
        }
        out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return true;
    };

    if (read()) {
        return true;
    }
    const Result rc = romfsInit();
    if (R_FAILED(rc)) {
        if (err) {
            *err = rc;
        }
        return false;
    }
    ON_SCOPE_EXIT(romfsExit());
    if (!read()) {
        if (err) {
            *err = FsError_PathNotFound;
        }
        return false;
    }
    return true;
}

constexpr const char* FIRMWARE_CLEANUP_PATHS[]{
    // Custom Themes (qlaunch, user page, settings/controllers, uLoader sysmodule)
    "/atmosphere/contents/0100000000001000",
    "/atmosphere/contents/0100000000001013",
    "/atmosphere/contents/0100000000001007",
    "/atmosphere/contents/00FF007468656D65",
    // System Translations
    "/atmosphere/contents/0100000000000803",
    "/atmosphere/contents/010000000000080B",
    "/atmosphere/contents/010000000000080C",
    "/atmosphere/contents/0100000000000811",
    "/atmosphere/contents/0100000000001001",
    "/atmosphere/contents/0100000000001002",
    "/atmosphere/contents/0100000000001003",
    "/atmosphere/contents/0100000000001004",
    "/atmosphere/contents/0100000000001005",
    "/atmosphere/contents/0100000000001006",
    "/atmosphere/contents/0100000000001008",
    "/atmosphere/contents/0100000000001009",
    "/atmosphere/contents/010000000000100D",
    "/atmosphere/contents/0100000000001012",
    "/atmosphere/contents/0100000000001015",
};

auto IsDowngradeStartupScript(fs::FsNativeSd& sd, const char* path = "/startup.te") -> bool {
    if (!sd.FileExists(path)) {
        return false;
    }
    std::vector<u8> bytes;
    if (R_FAILED(sd.read_entire_file(path, bytes)) || bytes.empty()) {
        return false;
    }
    std::string_view content(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return content.find("downgrade_fix.te") != std::string_view::npos ||
           content.find("8000000000000073") != std::string_view::npos;
}

} // namespace

auto CleanThemesAndTranslations(fs::FsNativeSd& sd) -> bool {
    bool ok = true;
    for (const auto* path : FIRMWARE_CLEANUP_PATHS) {
        const fs::FsPath p{path};
        if (sd.DirExists(p)) {
            const auto rc = sd.DeleteDirectoryRecursively(p);
            if (R_FAILED(rc) && rc != FsError_PathNotFound && rc != FsError_PathNotFoundFsDev) {
                ok = false;
            }
        }
        if (sd.FileExists(p)) {
            const auto rc = sd.DeleteFile(p);
            if (R_FAILED(rc) && rc != FsError_PathNotFound && rc != FsError_PathNotFoundFsDev) {
                ok = false;
            }
        }
        if (sd.DirExists(p) || sd.FileExists(p)) {
            ok = false;
        }
    }
    return ok;
}

auto CleanThemesAndTranslations() -> bool {
    fs::FsNativeSd sd;
    if (R_FAILED(sd.GetFsOpenResult())) {
        return false;
    }
    const bool ok = CleanThemesAndTranslations(sd);
    fsdevCommitDevice("sdmc");
    if (R_FAILED(sd.Commit())) {
        return false;
    }
    return ok;
}

auto IsDowngradeFixAvailable() -> bool {
    return true;
}

auto StageDowngradeFix(bool is_emummc, DowngradeFixResult* out, bool arm_startup) -> bool {
    if (out) {
        *out = {};
        out->attempted = true;
    }

    std::string script;
    Result romfs_err = 0;
    if (!ReadRomfsTe("romfs:/tegra/downgrade_fix.te", script, &romfs_err)) {
        if (R_FAILED(romfs_err) && romfs_err != FsError_PathNotFound) {
            log_write("StageDowngradeFix: romfsInit failed (0x%x)\n", romfs_err);
        } else {
            log_write("StageDowngradeFix: failed to read romfs:/tegra/downgrade_fix.te\n");
        }
        if (out) {
            out->rc = R_FAILED(romfs_err) ? romfs_err : static_cast<Result>(FsError_PathNotFound);
        }
        return false;
    }

    const std::string target = is_emummc ? "emu" : "sys";

    fs::FsNativeSd sd;
    if (const auto rc = sd.GetFsOpenResult(); R_FAILED(rc)) {
        if (out) {
            out->rc = rc;
        }
        return false;
    }

    // Any startup script is an already-pending one-shot workflow. Do not replace it.
    if (sd.FileExists("/startup.te")) {
        log_write("StageDowngradeFix: /startup.te already exists; refusing to overwrite\n");
        if (out) {
            out->rc = FsError_PathAlreadyExists;
        }
        return false;
    }

    // Validate the payload before creating any staged state.
    fs::FsPath te_bin;
    if (!utils::ensureTegraExplorerPayload(te_bin)) {
        log_write("StageDowngradeFix: ensureTegraExplorerPayload failed\n");
        if (out) {
            out->rc = FsError_FileNotFound;
        }
        return false;
    }

    // 1. Stage to /TegraExplorer/scripts/downgrade_fix.te
    std::vector<u8> script_bytes(script.begin(), script.end());
    if (const auto rc = sd.CreateDirectoryRecursively("/TegraExplorer/scripts"); R_FAILED(rc)) {
        log_write("StageDowngradeFix: failed to create /TegraExplorer/scripts (0x%x)\n", rc);
        if (out) {
            out->rc = rc;
        }
        return false;
    }
    if (const auto rc = sd.write_entire_file("/TegraExplorer/scripts/downgrade_fix.te", script_bytes); R_FAILED(rc)) {
        log_write("StageDowngradeFix: failed to write /TegraExplorer/scripts/downgrade_fix.te (0x%x)\n", rc);
        DisarmDowngradeFix();
        if (out) {
            out->rc = rc;
        }
        return false;
    }

    // 2. Write target flag /config/kefir/downgrade_nand
    if (const auto rc = sd.CreateDirectoryRecursively("/config/kefir"); R_FAILED(rc)) {
        log_write("StageDowngradeFix: failed to create /config/kefir (0x%x)\n", rc);
        DisarmDowngradeFix();
        if (out) {
            out->rc = rc;
        }
        return false;
    }
    std::vector<u8> target_bytes(target.begin(), target.end());
    if (const auto rc = sd.write_entire_file("/config/kefir/downgrade_nand", target_bytes); R_FAILED(rc)) {
        log_write("StageDowngradeFix: failed to write /config/kefir/downgrade_nand (0x%x)\n", rc);
        DisarmDowngradeFix();
        if (out) {
            out->rc = rc;
        }
        return false;
    }

    // 3. Arm /startup.te only if requested (e.g. standalone apply flow)
    if (arm_startup) {
        if (const auto rc = sd.write_entire_file("/startup.te", script_bytes); R_FAILED(rc)) {
            log_write("StageDowngradeFix: failed to write /startup.te (0x%x)\n", rc);
            DisarmDowngradeFix();
            if (out) {
                out->rc = rc;
            }
            return false;
        }
    }

    fsdevCommitDevice("sdmc");
    if (const auto rc = sd.Commit(); R_FAILED(rc)) {
        log_write("StageDowngradeFix: sd.Commit failed (0x%x)\n", rc);
        DisarmDowngradeFix();
        if (out) {
            out->rc = rc;
        }
        return false;
    }

    log_write("StageDowngradeFix: successfully %s for %s\n",
        arm_startup ? "staged and armed /startup.te" : "preflight-staged downgrade fix",
        target.c_str());
    if (out) {
        out->staged = arm_startup;
        out->rc = 0;
    }
    return true;
}

auto ArmDowngradeFix(DowngradeFixResult* out) -> bool {
    if (out) {
        out->staged = false;
    }

    fs::FsNativeSd sd;
    if (const auto rc = sd.GetFsOpenResult(); R_FAILED(rc)) {
        log_write("ArmDowngradeFix: failed to open SD (0x%x)\n", rc);
        if (out) {
            out->rc = rc;
        }
        return false;
    }

    // A script appearing between preflight and activation belongs to another workflow.
    if (sd.FileExists("/startup.te")) {
        log_write("ArmDowngradeFix: /startup.te appeared after preflight; refusing to overwrite\n");
        if (out) {
            out->rc = FsError_PathAlreadyExists;
        }
        return false;
    }

    std::vector<u8> script_bytes;
    Result rc = sd.read_entire_file("/TegraExplorer/scripts/downgrade_fix.te", script_bytes);
    if (R_FAILED(rc) || script_bytes.empty()) {
        log_write("ArmDowngradeFix: staged script is missing or unreadable (0x%x)\n", rc);
        if (out) {
            out->rc = R_FAILED(rc) ? rc : static_cast<Result>(FsError_PathNotFound);
        }
        DisarmDowngradeFix();
        return false;
    }

    rc = sd.write_entire_file("/startup.te", script_bytes);
    if (R_FAILED(rc)) {
        log_write("ArmDowngradeFix: failed to write /startup.te (0x%x)\n", rc);
        DisarmDowngradeFix();
        if (out) {
            out->rc = rc;
            out->staged = false;
        }
        return false;
    }

    fsdevCommitDevice("sdmc");
    rc = sd.Commit();
    if (R_FAILED(rc)) {
        log_write("ArmDowngradeFix: sd.Commit failed (0x%x)\n", rc);
        DisarmDowngradeFix();
        if (out) {
            out->rc = rc;
            out->staged = false;
        }
        return false;
    }

    log_write("ArmDowngradeFix: successfully armed /startup.te\n");
    if (out) {
        out->staged = true;
        out->rc = 0;
    }
    return true;
}

auto DisarmDowngradeFix() -> bool {
    fs::FsNativeSd sd;
    if (R_FAILED(sd.GetFsOpenResult())) {
        return false;
    }
    bool ok = true;
    const auto remove = [&](const char* path) {
        if (sd.FileExists(path)) {
            const auto rc = sd.DeleteFile(path);
            if (R_FAILED(rc) && rc != FsError_PathNotFound && rc != FsError_PathNotFoundFsDev) {
                ok = false;
            }
        }
    };
    if (IsDowngradeStartupScript(sd)) {
        remove("/startup.te");
    }
    remove("/config/kefir/downgrade_nand");
    remove("/TegraExplorer/scripts/downgrade_fix.te");
    fsdevCommitDevice("sdmc");
    return R_SUCCEEDED(sd.Commit()) && ok;
}

auto StageAndLaunchDowngradeFix(bool is_emummc) -> bool {
    DowngradeFixResult fix{};
    if (!StageDowngradeFix(is_emummc, &fix, /*arm_startup=*/true)) {
        return false;
    }
    fs::FsPath te_bin;
    if (!utils::findTegraExplorerPayload(te_bin)) {
        log_write("StageAndLaunchDowngradeFix: findTegraExplorerPayload failed\n");
        DisarmDowngradeFix();
        return false;
    }
    if (!utils::rebootToPayload(static_cast<const char*>(te_bin))) {
        log_write("StageAndLaunchDowngradeFix: rebootToPayload failed\n");
        DisarmDowngradeFix();
        return false;
    }
    return true;
}

void ApplyDowngradeFix(DowngradeFixResult* out) {
    StageDowngradeFix(App::IsEmummc(), out, /*arm_startup=*/true);
}

auto DescribeDowngradeFix(const DowngradeFixResult& fix) -> std::string {
    if (!fix.attempted) {
        return {};
    }

    if (fix.staged) {
        return "Downgrade fix staged: console will reboot to TegraExplorer to delete system save 8000000000000073, themes, and translations."_i18n;
    }

    if (fix.deleted) {
        return "Downgrade fix applied: system save 8000000000000073, themes, and translations deleted."_i18n;
    }

    if (R_FAILED(fix.rc)) {
        char rc_str[32];
        std::snprintf(rc_str, sizeof(rc_str), "0x%08X", R_VALUE(fix.rc));
        std::string out = "WARNING: Downgrade fix staging or activation failed ("_i18n;
        out += rc_str;
        out += "). Automatic recovery is NOT armed. System save 8000000000000073 must be removed manually via TegraExplorer or Maintenance Mode."_i18n;
        return out;
    }

    return "Downgrade fix could not be staged. Automatic recovery is NOT armed."_i18n;
}

void CleanupFirmwareFiles(ProgressBox* pbox, const fs::FsPath& path) {
    fs::FsNativeSd fs;
    if (R_FAILED(fs.GetFsOpenResult())) {
        return;
    }

    fs::FsPath firmware_path = path;
    if (firmware_path.s[0] == '\0') {
        firmware_path = FIRMWARE_DEST;
    }

    // Only clean app-owned staging locations. Never remove user-selected folders.
    const bool is_download_dest = (std::strcmp(firmware_path.s, FIRMWARE_DEST) == 0);
    const bool is_manual_staging = (std::strcmp(firmware_path.s, MANUAL_FIRMWARE_DEST) == 0);

    if (!is_download_dest && !is_manual_staging) {
        return;
    }

    if (pbox) {
        pbox->NewTransfer("Removing firmware files...");
    }

    if (fs.DirExists(firmware_path)) {
        fs.DeleteDirectoryRecursively(firmware_path);
    }
    if (is_download_dest && fs.FileExists(FIRMWARE_ZIP)) {
        fs.DeleteFile(FIRMWARE_ZIP);
    }
    fs.Commit();
}

void CleanupManualFirmwareStaging() {
    fs::FsNativeSd fs;
    if (R_FAILED(fs.GetFsOpenResult())) {
        return;
    }

    if (fs.DirExists(MANUAL_FIRMWARE_DEST)) {
        fs.DeleteDirectoryRecursively(MANUAL_FIRMWARE_DEST);
        fs.Commit();
    }
}

auto InstallValidatedFirmware(ProgressBox* pbox, bool use_exfat, const fs::FsPath& path, bool apply_downgrade_fix, DowngradeFixResult* out_fix) -> Result {
    Result rc = amssuInitialize();
    if (R_FAILED(rc)) {
        return rc;
    }
    ON_SCOPE_EXIT(amssuExit());

    constexpr size_t UPDATE_TASK_BUFFER_SIZE = 0x100000;
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

    if (apply_downgrade_fix) {
        pbox->NewTransfer("Staging downgrade fix...");
        DowngradeFixResult fix{};
        if (!StageDowngradeFix(App::IsEmummc(), &fix, /*arm_startup=*/false)) {
            if (out_fix) {
                *out_fix = fix;
            }
            R_THROW(R_FAILED(fix.rc) ? fix.rc : static_cast<Result>(FsError_PathNotFound));
        }
        if (out_fix) {
            *out_fix = fix;
        }
    }

    pbox->NewTransfer("Applying system update...");
    rc = amssuApplyPreparedUpdate();
    if (R_FAILED(rc)) {
        if (apply_downgrade_fix) {
            DisarmDowngradeFix();
        }
        return rc;
    }

    if (apply_downgrade_fix) {
        pbox->NewTransfer("Activating downgrade fix...");
        if (!ArmDowngradeFix(out_fix)) {
            log_write("InstallValidatedFirmware: failed to arm downgrade fix\n");
            DisarmDowngradeFix();
        }
    }

    // Clean themes and translations unconditionally on ANY firmware installation (both update and downgrade)
    // to prevent Atmosphere fatal crash 2162-0002 and mismatched qlaunch components on reboot.
    pbox->NewTransfer("Removing themes and translations...");
    const bool cleanup_ok = CleanThemesAndTranslations();
    if (out_fix) {
        out_fix->cleanup_failed = !cleanup_ok;
    }

    CleanupFirmwareFiles(pbox, path);

    R_SUCCEED();
}

auto IsSelectableEntry(const UpdaterEntry& entry) -> bool {
    return entry.type != UpdaterEntryType::Section;
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

auto TypeLabel(UpdaterEntryType type) -> const char* {
    switch (type) {
        case UpdaterEntryType::Section:
            return "";
        case UpdaterEntryType::Network:
            return "Network";
        case UpdaterEntryType::CustomLink:
            return "Other";
        case UpdaterEntryType::Kefir:
            return "Kefir";
        case UpdaterEntryType::Firmware:
            return "Firmware";
        case UpdaterEntryType::FirmwareManual:
            return "Firmware";
    }
    return "";
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
    if (!result.success) {
        if (pbox->ShouldExit()) {
            R_THROW(Result_TransferCancelled);
        }
        R_THROW(Result_AppstoreFailedZipDownload);
    }

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

auto ExtractManualFirmwareZip(ProgressBox* pbox, const fs::FsPath& zip_path) -> Result {
    fs::FsNativeSd fs;
    R_TRY(fs.GetFsOpenResult());

    R_TRY(fs.CreateDirectoryRecursively(CACHE_DIR));

    if (fs.DirExists(MANUAL_FIRMWARE_DEST)) {
        R_TRY(fs.DeleteDirectoryRecursively(MANUAL_FIRMWARE_DEST));
    }
    R_TRY(fs.CreateDirectoryRecursively(MANUAL_FIRMWARE_DEST));

    pbox->NewTransfer("Extracting firmware...");
    const Result rc = thread::TransferUnzipAll(pbox, zip_path, &fs, MANUAL_FIRMWARE_DEST);
    if (R_FAILED(rc)) {
        if (fs.DirExists(MANUAL_FIRMWARE_DEST)) {
            fs.DeleteDirectoryRecursively(MANUAL_FIRMWARE_DEST);
            fs.Commit();
        }
        return rc;
    }
    R_TRY(fs.Commit());

    R_SUCCEED();
}

} // namespace detail


} // namespace sphaira::ui::menu::kefir
