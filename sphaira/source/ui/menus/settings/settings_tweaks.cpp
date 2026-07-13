#include "ui/menus/settings/settings_tweaks.hpp"
#include "ui/menus/settings/settings_fs_utils.hpp"
#include "ui/menus/settings/settings_translations.hpp"
#include "utils/utils.hpp"
#include "i18n.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <switch/services/pm.h>

namespace sphaira::ui::menu::settings::detail {

auto IsEmummcEnabled() -> bool {
    return IniValueEquals("/emummc/emummc.ini", "emummc", "enabled", "1");
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
    R_TRY(SetIniValue("/atmosphere/config/system_settings.ini", "atmosphere", "force_40mb_applet", enabled ? "u8!0x1" : "u8!0x0"));
    RebootAfterSetting();
    R_SUCCEED();
}

auto ApplyRedirectSaves(bool enabled) -> Result {
    R_TRY(SetIniValue("/atmosphere/config/system_settings.ini", "atmosphere", "fsmitm_redirect_saves_to_sd", enabled ? "u8!0x1" : "u8!0x0"));
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

auto DefaultHandheldFanCurve() -> std::vector<FanCurvePoint> {
    return {
        {10, 0},
        {40, 20},
        {47, 20},
        {56, 60},
        {58, 100},
    };
}

auto DefaultDockedFanCurve() -> std::vector<FanCurvePoint> {
    return {
        {10, 0},
        {40, 20},
        {47, 20},
        {54, 60},
        {58, 100},
    };
}

auto QuietFanCurve(bool docked) -> std::vector<FanCurvePoint> {
    return docked ? std::vector<FanCurvePoint>{
        {10, 0},
        {42, 20},
        {50, 20},
        {58, 55},
        {63, 85},
        {68, 100},
    } : std::vector<FanCurvePoint>{
        {10, 0},
        {43, 20},
        {52, 20},
        {60, 55},
        {65, 85},
        {70, 100},
    };
}

auto BalancedFanCurve(bool docked) -> std::vector<FanCurvePoint> {
    return docked ? std::vector<FanCurvePoint>{
        {10, 0},
        {39, 20},
        {47, 30},
        {55, 65},
        {60, 100},
    } : std::vector<FanCurvePoint>{
        {10, 0},
        {40, 20},
        {49, 30},
        {57, 65},
        {62, 100},
    };
}

auto CoolFanCurve(bool docked) -> std::vector<FanCurvePoint> {
    return docked ? std::vector<FanCurvePoint>{
        {10, 0},
        {34, 25},
        {42, 45},
        {50, 75},
        {56, 100},
    } : std::vector<FanCurvePoint>{
        {10, 0},
        {35, 25},
        {44, 45},
        {52, 75},
        {58, 100},
    };
}

auto FullSpeedFanCurve() -> std::vector<FanCurvePoint> {
    return {
        {10, 100},
        {90, 100},
    };
}

auto FanOffCurve() -> std::vector<FanCurvePoint> {
    return {
        {10, 0},
        {90, 0},
    };
}

auto FanPresetSection(bool docked) -> const char* {
    return docked ? "docked" : "handheld";
}

auto FanCustomPresetKey(s64 index) -> std::string {
    return "custom_" + std::to_string(index + 1);
}

auto FanCustomPresetNameKey(s64 index) -> std::string {
    return FanCustomPresetKey(index) + "_name";
}

auto FanCustomPresetDefaultName(s64 index) -> std::string {
    return "Custom " + std::to_string(index + 1);
}

auto SanitizeFanPresetName(std::string name) -> std::string {
    for (auto& ch : name) {
        if (ch == '\r' || ch == '\n' || ch == ';' || ch == '#') {
            ch = ' ';
        }
    }
    return ::sphaira::utils::Trim(std::move(name));
}

auto FanBuiltinPresetLabels() -> std::vector<std::string> {
    return {
        "Cold console"_i18n,
        "Quiet"_i18n,
        "Balanced"_i18n,
        "Fan off"_i18n,
        "Fan 100%"_i18n,
    };
}

auto FanPresetCurve(s64 index, bool docked) -> std::vector<FanCurvePoint> {
    switch (index) {
        case 0: return CoolFanCurve(docked);
        case 1: return QuietFanCurve(docked);
        case 2: return BalancedFanCurve(docked);
        case 3: return FanOffCurve();
        case 4: return FullSpeedFanCurve();
        default:
            return BalancedFanCurve(docked);
    }
}

auto FanByteToPercent(s32 value) -> s32 {
    value = std::clamp(value, 0, 255);
    return (value * 100 + 127) / 255;
}

auto FanPercentToByte(s32 value) -> s32 {
    value = std::clamp(value, FAN_PERCENT_MIN, FAN_PERCENT_MAX);
    return (value * 255 + 50) / 100;
}

auto DecodeAtmosphereString(std::string value) -> std::string {
    value = ::sphaira::utils::Trim(std::move(value));
    if (StartsWith(value, "str!")) {
        value = ::sphaira::utils::Trim(value.substr(4));
    }
    return ::sphaira::utils::Trim(std::move(value));
}

auto ParseSignedIntegers(const std::string& value) -> std::vector<s32> {
    std::vector<s32> out;
    const char* ptr = value.c_str();

    while (*ptr) {
        if (*ptr == '-' || (*ptr >= '0' && *ptr <= '9')) {
            char* end{};
            const auto parsed = std::strtol(ptr, &end, 10);
            if (end != ptr) {
                out.push_back(static_cast<s32>(parsed));
                ptr = end;
                continue;
            }
        }
        ptr++;
    }

    return out;
}

void NormalizeFanCurve(std::vector<FanCurvePoint>& curve) {
    std::sort(curve.begin(), curve.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.temp_c < rhs.temp_c;
    });

    if (curve.size() > static_cast<size_t>(FAN_TEMP_MAX_C - FAN_TEMP_MIN_C + 1)) {
        curve.resize(FAN_TEMP_MAX_C - FAN_TEMP_MIN_C + 1);
    }

    for (size_t i = 0; i < curve.size(); i++) {
        auto& point = curve[i];
        const auto remaining = static_cast<s32>(curve.size() - i - 1);
        const auto min_temp = i ? curve[i - 1].temp_c + 1 : FAN_TEMP_MIN_C;
        const auto max_temp = FAN_TEMP_MAX_C - remaining;
        point.temp_c = std::clamp(point.temp_c, min_temp, max_temp);
        point.fan_percent = std::clamp(point.fan_percent, FAN_PERCENT_MIN, FAN_PERCENT_MAX);

        if (i) {
            const auto& prev = curve[i - 1];
            point.fan_percent = std::max(point.fan_percent, prev.fan_percent);
        }
    }

    for (size_t i = curve.size(); i-- > 0;) {
        auto& point = curve[i];
        if (i + 1 < curve.size()) {
            const auto& next = curve[i + 1];
            point.fan_percent = std::min(point.fan_percent, next.fan_percent);
        }
    }
}

auto ParseFanCurveTable(const std::string& value, std::vector<FanCurvePoint>& out) -> bool {
    const auto numbers = ParseSignedIntegers(DecodeAtmosphereString(value));
    if (numbers.size() < 8 || numbers.size() % 4) {
        return false;
    }

    std::vector<FanCurvePoint> curve;
    for (size_t i = 0; i < numbers.size(); i += 4) {
        const auto temp_max = numbers[i + 1];
        const auto fan_max = numbers[i + 3];

        if (temp_max >= 100000) {
            continue;
        }

        const auto temp_c = std::clamp((temp_max + 500) / 1000, FAN_TEMP_MIN_C, FAN_TEMP_MAX_C);
        const auto fan_percent = FanByteToPercent(fan_max);
        if (!curve.empty() && temp_c <= curve.back().temp_c) {
            continue;
        }

        curve.push_back({temp_c, fan_percent});
    }

    if (curve.size() < 2) {
        return false;
    }

    NormalizeFanCurve(curve);
    out = std::move(curve);
    return true;
}

auto ReadFanCurve(const char* primary_key, const char* fallback_key, std::vector<FanCurvePoint> defaults) -> std::vector<FanCurvePoint> {
    std::vector<FanCurvePoint> curve;

    if (ParseFanCurveTable(ReadIniRawValue("/atmosphere/config/system_settings.ini", "tc", primary_key), curve)) {
        return curve;
    }

    if (fallback_key && ParseFanCurveTable(ReadIniRawValue("/atmosphere/config/system_settings.ini", "tc", fallback_key), curve)) {
        return curve;
    }

    return defaults;
}

auto ReadCustomFanPreset(s64 index, bool docked, std::vector<FanCurvePoint>& out) -> bool {
    if (index < 0 || index >= FAN_CUSTOM_PRESET_COUNT) {
        return false;
    }

    return ParseFanCurveTable(
        ReadIniRawValue(FAN_PRESETS_CONFIG, FanPresetSection(docked), FanCustomPresetKey(index)),
        out
    );
}

auto ReadCustomFanPresetName(s64 index, bool docked) -> std::string {
    if (index < 0 || index >= FAN_CUSTOM_PRESET_COUNT) {
        return {};
    }

    auto name = ::sphaira::utils::Trim(ReadIniRawValue(FAN_PRESETS_CONFIG, FanPresetSection(docked), FanCustomPresetNameKey(index)));
    return name.empty() ? FanCustomPresetDefaultName(index) : name;
}

auto FanCustomPresetLabel(s64 index, bool docked, bool for_save) -> std::string {
    std::vector<FanCurvePoint> preset;
    auto label = ReadCustomFanPresetName(index, docked);
    const auto has_preset = ReadCustomFanPreset(index, docked, preset);
    if (!has_preset) {
        label += " (empty)";
    } else if (for_save) {
        label += " (overwrite)";
    }
    return label;
}

auto FanPresetLabels(bool docked) -> std::vector<std::string> {
    auto items = FanBuiltinPresetLabels();
    for (s64 i = 0; i < FAN_CUSTOM_PRESET_COUNT; i++) {
        items.emplace_back(FanCustomPresetLabel(i, docked, false));
    }
    return items;
}

auto FanCustomPresetLabels(bool docked) -> std::vector<std::string> {
    std::vector<std::string> items;
    for (s64 i = 0; i < FAN_CUSTOM_PRESET_COUNT; i++) {
        items.emplace_back(FanCustomPresetLabel(i, docked, true));
    }
    return items;
}

auto FormatFanCurveTable(std::vector<FanCurvePoint> curve) -> std::string {
    if (curve.size() < 2) {
        curve = DefaultHandheldFanCurve();
    }

    NormalizeFanCurve(curve);

    std::string out{"["};
    bool first = true;
    const auto append = [&](s32 temp_min, s32 temp_max, s32 fan_min, s32 fan_max) {
        if (!first) {
            out += ", ";
        }
        first = false;
        out += "[";
        out += std::to_string(temp_min);
        out += ", ";
        out += std::to_string(temp_max);
        out += ", ";
        out += std::to_string(fan_min);
        out += ", ";
        out += std::to_string(fan_max);
        out += "]";
    };

    append(
        FAN_TABLE_TEMP_MIN,
        curve.front().temp_c * 1000,
        FanPercentToByte(curve.front().fan_percent),
        FanPercentToByte(curve.front().fan_percent)
    );

    for (size_t i = 1; i < curve.size(); i++) {
        const auto& prev = curve[i - 1];
        const auto& cur = curve[i];
        append(
            prev.temp_c * 1000,
            cur.temp_c * 1000,
            FanPercentToByte(prev.fan_percent),
            FanPercentToByte(cur.fan_percent)
        );
    }

    append(
        curve.back().temp_c * 1000,
        FAN_TABLE_TEMP_MAX,
        FanPercentToByte(curve.back().fan_percent),
        FanPercentToByte(curve.back().fan_percent)
    );

    out += "]";
    return out;
}

auto FormatAtmosphereFanCurve(std::vector<FanCurvePoint> curve) -> std::string {
    return "str!\"" + FormatFanCurveTable(std::move(curve)) + "\"";
}

auto IsFanCurveEnabled() -> bool {
    return IniValueEquals("/atmosphere/config/system_settings.ini", "tc", "use_configurations_on_fwdbg", "u8!0x1");
}

auto IsSphairaFanSysmoduleInstalled() -> bool {
    return FileExists("/atmosphere/contents/00FF46554E43544C/exefs.nsp");
}

auto IsSphairaFanSysmoduleRunning() -> bool {
    if (!IsSphairaFanSysmoduleInstalled()) {
        return false;
    }

    Result rc = pmshellInitialize();
    if (R_FAILED(rc)) {
        return false;
    }

    u64 pid{};
    rc = pmshellGetProcessId(&pid, SPHAIRA_FAN_PROGRAM_ID);
    pmshellExit();
    return R_SUCCEEDED(rc);
}

auto EnsureSphairaFanSysmoduleInstalled() -> Result {
    R_UNLESS(IsSphairaFanSysmoduleInstalled(), FsError_PathNotFound);
    R_SUCCEED();
}

auto RestartSphairaFanSysmodule() -> Result {
    R_TRY(EnsureSphairaFanSysmoduleInstalled());

    if (FileExists("/atmosphere/contents/00FF46554E43544C/flags/boot2.flag")) {
        DeletePath("/atmosphere/contents/00FF46554E43544C/flags/boot2.flag");
    }

    Result rc = pmshellInitialize();
    R_TRY(rc);

    pmshellTerminateProgram(SPHAIRA_FAN_PROGRAM_ID);
    pmshellTerminateProgram(SPHAIRA_OLD_FAN_PROGRAM_ID);
    svcSleepThread(100000000);

    const NcmProgramLocation location{
        .program_id = SPHAIRA_FAN_PROGRAM_ID,
        .storageID = NcmStorageId_None,
    };
    u64 pid{};
    rc = pmshellLaunchProgram(PmLaunchFlag_None, &location, &pid);
    if (R_SUCCEEDED(rc)) {
        Result check_rc = rc;
        for (u32 i = 0; i < 5; i++) {
            svcSleepThread(100000000);
            check_rc = pmshellGetProcessId(&pid, SPHAIRA_FAN_PROGRAM_ID);
            if (R_SUCCEEDED(check_rc)) {
                break;
            }
        }
        rc = check_rc;
    }
    pmshellExit();
    R_TRY(rc);

    R_SUCCEED();
}

auto ApplyFanCurves(const std::vector<FanCurvePoint>& handheld, const std::vector<FanCurvePoint>& docked, FanCurveApplyMode mode) -> Result {
    const auto handheld_value = FormatAtmosphereFanCurve(handheld);
    const auto docked_value = FormatAtmosphereFanCurve(docked);

    R_TRY(SetIniValue("/atmosphere/config/system_settings.ini", "tc", "use_configurations_on_fwdbg", "u8!0x1"));
    R_TRY(SetIniRawValue("/atmosphere/config/system_settings.ini", "tc", "tskin_rate_table_handheld_on_fwdbg", handheld_value));
    R_TRY(SetIniRawValue("/atmosphere/config/system_settings.ini", "tc", "tskin_rate_table_console_on_fwdbg", docked_value));
    fsdevCommitDevice("sdmc");

    R_TRY(RestartSphairaFanSysmodule());

    R_SUCCEED();
}

auto SaveCustomFanPreset(s64 index, bool docked, const std::vector<FanCurvePoint>& curve, const std::string& name) -> Result {
    R_UNLESS(index >= 0 && index < FAN_CUSTOM_PRESET_COUNT, Result_FsUnknownStdioError);
    auto safe_name = SanitizeFanPresetName(name);
    if (safe_name.empty()) {
        safe_name = FanCustomPresetDefaultName(index);
    }

    R_TRY(SetIniRawValue(
        FAN_PRESETS_CONFIG,
        FanPresetSection(docked),
        FanCustomPresetKey(index),
        FormatFanCurveTable(curve)
    ));
    R_TRY(SetIniRawValue(
        FAN_PRESETS_CONFIG,
        FanPresetSection(docked),
        FanCustomPresetNameKey(index),
        safe_name
    ));
    fsdevCommitDevice("sdmc");
    R_SUCCEED();
}

} // namespace sphaira::ui::menu::settings::detail
