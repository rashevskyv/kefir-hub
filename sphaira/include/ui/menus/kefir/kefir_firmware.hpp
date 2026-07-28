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

// outcome of the experimental downgrade fix. reported on its own so a fix that
// fails can never be mistaken for a firmware update that failed.
struct DowngradeFixResult {
    Result rc{};       // result of the delete attempt.
    bool attempted{};  // the fix was requested.
    bool deleted{};    // the save existed and was removed.
};

namespace detail {



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
// false while the fix has no working implementation: the system save cannot be
// deleted from a running console (FsError_TargetLocked).
auto IsDowngradeFixAvailable() -> bool;
// experimental downgrade fix. currently a no-op stub; never fails the caller,
// the outcome is reported through out.
void ApplyDowngradeFix(DowngradeFixResult* out);
// one sentence describing what the fix actually did, empty if not attempted.
auto DescribeDowngradeFix(const DowngradeFixResult& fix) -> std::string;
auto DownloadAndExtractFirmware(ProgressBox* pbox, const UpdaterEntry& entry) -> Result;
auto SelectableCount(const std::vector<UpdaterEntry>& entries) -> s64;
auto SelectablePosition(const std::vector<UpdaterEntry>& entries, s64 index) -> s64;
auto TypeLabel(UpdaterEntryType type) -> const char*;

} // namespace detail


} // namespace sphaira::ui::menu::kefir
