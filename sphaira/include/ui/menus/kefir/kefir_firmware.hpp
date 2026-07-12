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

namespace detail {



auto Trim(std::string value) -> std::string;
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
auto TrimAsciiWhitespace(std::string value) -> std::string;
auto IsVersionHeaderLine(const std::string& line) -> bool;
auto BuildFirmwareServicePath(const fs::FsPath& path) -> std::string;
auto FormatFirmwareVersion(u32 version) -> std::string;
auto ValidateFirmware(FirmwareValidation* out, const fs::FsPath& path) -> Result;
auto InstallValidatedFirmware(ProgressBox* pbox, bool use_exfat, const fs::FsPath& path, bool apply_downgrade_fix) -> Result;
auto DownloadAndExtractFirmware(ProgressBox* pbox, const UpdaterEntry& entry) -> Result;
auto SelectableCount(const std::vector<UpdaterEntry>& entries) -> s64;
auto SelectablePosition(const std::vector<UpdaterEntry>& entries, s64 index) -> s64;
auto TypeLabel(UpdaterEntryType type) -> const char*;

} // namespace detail


} // namespace sphaira::ui::menu::kefir
