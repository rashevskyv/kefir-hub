#pragma once

#include "ams_su.h"
#include "fs.hpp"
#include "ui/menus/kefir_menu.hpp"
#include <string>
#include <vector>
#include <string_view>

namespace sphaira::ui {
    class ProgressBox;
}

namespace sphaira::ui::menu::kefir {
using ProgressBox = sphaira::ui::ProgressBox;


struct FirmwareValidation {
    AmsSuUpdateInformation info{};
    AmsSuUpdateValidationInfo validation{};
};

// outcome of the downgrade fix. reported on its own so a fix that
// fails can never be mistaken for a firmware update that failed.
struct DowngradeFixResult {
    Result rc{};       // result of the operation.
    bool attempted{};  // the fix was requested.
    bool staged{};     // staged startup.te and downgrade_nand.
    bool deleted{};    // the save existed and was removed.
};

namespace detail {

constexpr const char* MANUAL_FIRMWARE_DEST = "/config/kefir-updater/firmware_manual";

auto ReadLineNumber(const char* path, size_t line_index) -> std::string;
auto ReadFirstLine(const char* path) -> std::string;
auto ReadSecondLine(const char* path) -> std::string;
auto IsKnownVersion(const std::string& version) -> bool;
auto FirmwareUnsupportedReason(const std::string& target, const std::string& supported) -> std::string;
auto UnsupportedFirmwareLabel(const std::string& supported) -> std::string;
auto ReadCurrentKefirSupportedFirmware() -> std::string;
auto FindDigitsAfter(const std::string& value, std::string_view marker) -> std::string;
auto ExtractKefirVersion(const std::string& name, const std::string& url) -> std::string;
auto MakeKefirLatestLabel(const UpdaterEntry& entry) -> std::string;
auto ParseVersion(const std::string& version) -> std::vector<int>;
auto IsVersionLower(const std::string& target, const std::string& current) -> bool;
auto GetFirmwareTargetName() -> std::string;

auto IsVersionHeaderLine(const std::string& line) -> bool;
auto BuildFirmwareServicePath(const fs::FsPath& path) -> std::string;
auto FormatFirmwareVersion(u32 version) -> std::string;
auto ValidateFirmware(FirmwareValidation* out, const fs::FsPath& path) -> Result;
auto InstallValidatedFirmware(ProgressBox* pbox, bool use_exfat, const fs::FsPath& path, bool apply_downgrade_fix, DowngradeFixResult* out_fix = nullptr) -> Result;
void CleanupFirmwareFiles(ProgressBox* pbox, const fs::FsPath& path);
auto ExtractManualFirmwareZip(ProgressBox* pbox, const fs::FsPath& zip_path) -> Result;
void CleanupManualFirmwareStaging();
// returns true now that the fix is automated via TegraExplorer after reboot.
auto IsDowngradeFixAvailable() -> bool;
// stages downgrade_fix.te to /startup.te and writes target flag.
auto StageDowngradeFix(bool is_emummc, DowngradeFixResult* out = nullptr) -> bool;
// stages the fix and reboots immediately to TegraExplorer.
auto StageAndLaunchDowngradeFix(bool is_emummc) -> bool;
// stages the downgrade fix to startup.te.
void ApplyDowngradeFix(DowngradeFixResult* out);
// cleans custom themes and translations from SD to avoid crash 2162-0002 on reboot.
void CleanThemesAndTranslations(fs::FsNativeSd& sd);
void CleanThemesAndTranslations();
// one sentence describing what the fix actually did, empty if not attempted.
auto DescribeDowngradeFix(const DowngradeFixResult& fix) -> std::string;
auto DownloadAndExtractFirmware(ProgressBox* pbox, const UpdaterEntry& entry) -> Result;
auto SelectableCount(const std::vector<UpdaterEntry>& entries) -> s64;
auto SelectablePosition(const std::vector<UpdaterEntry>& entries, s64 index) -> s64;
auto TypeLabel(UpdaterEntryType type) -> const char*;

} // namespace detail


} // namespace sphaira::ui::menu::kefir
