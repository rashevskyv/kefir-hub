#include "ui/menus/kefir/kefir_firmware.hpp"
#include "ui/menus/kefir/kefir_changelog.hpp"
#include "hats_version.hpp"
#include "app.hpp"
#include "download.hpp"
#include "threaded_file_transfer.hpp"
#include "utils/utils.hpp"
#include <yyjson.h>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <cstdio>
#include <cstdlib>


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
    return std::to_string((version >> 26) & 0x1f) + "." +
           std::to_string((version >> 20) & 0x1f) + "." +
           std::to_string((version >> 16) & 0xf);
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

auto IsDowngradeFixAvailable() -> bool {
    // deleting /save/8000000000000073 from the BIS System partition fails with
    // FsError_TargetLocked: nim keeps that save mounted for as long as HOS is
    // running, so it can never be removed from inside the running system.
    // stubbed out until it is replaced by a script run after the install.
    return false;
}

void ApplyDowngradeFix(DowngradeFixResult* out) {
    *out = {};
    out->attempted = true;
    // no-op, see IsDowngradeFixAvailable().
}

auto DescribeDowngradeFix(const DowngradeFixResult& fix) -> std::string {
    if (!fix.attempted) {
        return {};
    }

    if (fix.deleted) {
        return "Downgrade fix applied: system save 8000000000000073 deleted.";
    }

    if (R_FAILED(fix.rc)) {
        char rc_str[32];
        std::snprintf(rc_str, sizeof(rc_str), "0x%08X", R_VALUE(fix.rc));
        std::string out = "Downgrade fix FAILED (";
        out += rc_str;
        out += ").";
        return out;
    }

    return "Downgrade fix is not available yet.\n\nThe system save 8000000000000073 is locked by the system while the console is running and cannot be deleted from here.\n\nUse hekate > Payloads > TegraExplorer > DowngradeFix.te instead.";
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
    pbox->NewTransfer("Removing firmware files...");

    if (fs.DirExists(firmware_path)) {
        fs.DeleteDirectoryRecursively(firmware_path);
    }
    if (fs.FileExists(FIRMWARE_ZIP)) {
        fs.DeleteFile(FIRMWARE_ZIP);
    }
    fs.Commit();
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

    pbox->NewTransfer("Applying system update...");
    R_TRY(amssuApplyPreparedUpdate());

    // the update itself is already applied at this point, so the fix must never
    // be able to report the whole install as failed.
    if (apply_downgrade_fix) {
        DowngradeFixResult fix{};
        ApplyDowngradeFix(&fix);
        if (out_fix) {
            *out_fix = fix;
        }
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

} // namespace detail


} // namespace sphaira::ui::menu::kefir
