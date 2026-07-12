#pragma once

#include <string>
#include <vector>
#include <switch.h>
#include "ui/progress_box.hpp"
#include "app_paths.hpp"

namespace sphaira::ui::menu::settings {

struct FanCurvePoint {
    s32 temp_c{};
    s32 fan_percent{};
};

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

enum class FanCurveApplyMode {
    Live,
    Reboot,
};

namespace detail {

inline const auto FAN_PRESETS_CONFIG = paths::DATA_ROOT + "/fan_curve_presets.ini";
constexpr u64 SPHAIRA_FAN_PROGRAM_ID{0x00FF46554E43544CULL};
constexpr u64 SPHAIRA_OLD_FAN_PROGRAM_ID{0x00FF000053504846ULL};
constexpr const char* SPHAIRA_FAN_EXEFS_PATH = "/atmosphere/contents/00FF46554E43544C/exefs.nsp";

constexpr s32 FAN_TABLE_TEMP_MIN{-1000000};
constexpr s32 FAN_TABLE_TEMP_MAX{1000000};
constexpr s32 FAN_TEMP_MIN_C{0};
constexpr s32 FAN_TEMP_MAX_C{90};
constexpr s32 FAN_PERCENT_MIN{0};
constexpr s32 FAN_PERCENT_MAX{100};
constexpr u32 FAN_DEVICE_CODE_PWM{0x3D000001};
constexpr u64 FAN_SENSOR_REFRESH_NS{500000000};
constexpr s64 FAN_BUILTIN_PRESET_COUNT{5};
constexpr s64 FAN_CUSTOM_PRESET_COUNT{3};

auto IsEmummcEnabled() -> bool;
auto ApplyOverclock(bool enabled) -> Result;
auto Apply40Mb(bool enabled) -> Result;
auto ApplyRedirectSaves(bool enabled) -> Result;
auto Apply8GbDram(bool enabled) -> Result;

auto DefaultHandheldFanCurve() -> std::vector<FanCurvePoint>;
auto DefaultDockedFanCurve() -> std::vector<FanCurvePoint>;
auto QuietFanCurve(bool docked) -> std::vector<FanCurvePoint>;
auto BalancedFanCurve(bool docked) -> std::vector<FanCurvePoint>;
auto CoolFanCurve(bool docked) -> std::vector<FanCurvePoint>;
auto FullSpeedFanCurve() -> std::vector<FanCurvePoint>;
auto FanOffCurve() -> std::vector<FanCurvePoint>;

auto FanPresetSection(bool docked) -> const char*;
auto FanCustomPresetKey(s64 index) -> std::string;
auto FanCustomPresetNameKey(s64 index) -> std::string;
auto FanCustomPresetDefaultName(s64 index) -> std::string;
auto SanitizeFanPresetName(std::string name) -> std::string;
auto FanBuiltinPresetLabels() -> std::vector<std::string>;
auto FanPresetCurve(s64 index, bool docked) -> std::vector<FanCurvePoint>;
auto FanByteToPercent(s32 value) -> s32;
auto FanPercentToByte(s32 value) -> s32;
auto DecodeAtmosphereString(std::string value) -> std::string;
auto ParseSignedIntegers(const std::string& value) -> std::vector<s32>;
void NormalizeFanCurve(std::vector<FanCurvePoint>& curve);
auto ParseFanCurveTable(const std::string& value, std::vector<FanCurvePoint>& out) -> bool;
auto ReadFanCurve(const char* primary_key, const char* fallback_key, std::vector<FanCurvePoint> defaults) -> std::vector<FanCurvePoint>;
auto ReadCustomFanPreset(s64 index, bool docked, std::vector<FanCurvePoint>& out) -> bool;
auto ReadCustomFanPresetName(s64 index, bool docked) -> std::string;
auto FanCustomPresetLabel(s64 index, bool docked, bool for_save) -> std::string;
auto FanPresetLabels(bool docked) -> std::vector<std::string>;
auto FanCustomPresetLabels(bool docked) -> std::vector<std::string>;
auto FormatFanCurveTable(std::vector<FanCurvePoint> curve) -> std::string;
auto FormatAtmosphereFanCurve(std::vector<FanCurvePoint> curve) -> std::string;
auto IsFanCurveEnabled() -> bool;
auto IsSphairaFanSysmoduleInstalled() -> bool;
auto IsSphairaFanSysmoduleRunning() -> bool;
auto EnsureSphairaFanSysmoduleInstalled() -> Result;
auto RestartSphairaFanSysmodule() -> Result;
auto ApplyFanCurves(const std::vector<FanCurvePoint>& handheld, const std::vector<FanCurvePoint>& docked, FanCurveApplyMode mode) -> Result;
auto SaveCustomFanPreset(s64 index, bool docked, const std::vector<FanCurvePoint>& curve, const std::string& name) -> Result;

} // namespace detail
} // namespace sphaira::ui::menu::settings
