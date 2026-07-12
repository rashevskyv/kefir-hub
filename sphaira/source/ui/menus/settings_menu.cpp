#include "ui/menus/settings_menu.hpp"
#include "ui/menus/settings/settings_fs_utils.hpp"
#include "ui/menus/settings/settings_translations.hpp"

#include "ui/menus/appstore.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/menus/themezer.hpp"
#include "ui/menus/uninstaller_menu.hpp"

#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"

#include "app.hpp"
#include "download.hpp"
#include "i18n.hpp"
#include "swkbd.hpp"
#include "threaded_file_transfer.hpp"
#include "utils/utils.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iterator>
#include <limits>
#include <minIni.h>
#include <switch/services/fan.h>
#include <switch/services/pm.h>
#include <switch/services/tc.h>
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


constexpr const char* ATMOSPHERE_CONFIG = "/atmosphere/config/system_settings.ini";
const auto FAN_PRESETS_CONFIG = paths::DATA_ROOT + "/fan_curve_presets.ini";
constexpr u64 SPHAIRA_FAN_PROGRAM_ID{0x00FF46554E43544CULL};
constexpr u64 SPHAIRA_OLD_FAN_PROGRAM_ID{0x00FF000053504846ULL};
constexpr const char* SPHAIRA_FAN_EXEFS_PATH = "/atmosphere/contents/00FF46554E43544C/exefs.nsp";
const auto DBI_TRANSLATIONS_PACKAGE = paths::PACKAGES + "/Software/DBI/Fan Translations/package.ini";


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

using namespace detail;


auto SettingsValueColour(Theme* theme, const std::string& value, bool selected) -> NVGcolor {
    if (value == "On"_i18n) {
        return nvgRGBA(78, 210, 112, 255);
    }
    if (value == "Off"_i18n) {
        return nvgRGBA(135, 138, 148, 255);
    }
    return theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT_INFO);
}





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
    return Trim(std::move(name));
}

auto FanBuiltinPresetLabels() -> PopupList::Items {
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
    value = Trim(std::move(value));
    if (StartsWith(value, "str!")) {
        value = Trim(value.substr(4));
    }
    return Trim(std::move(value));
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

    if (ParseFanCurveTable(ReadIniRawValue(ATMOSPHERE_CONFIG, "tc", primary_key), curve)) {
        return curve;
    }

    if (fallback_key && ParseFanCurveTable(ReadIniRawValue(ATMOSPHERE_CONFIG, "tc", fallback_key), curve)) {
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

    auto name = Trim(ReadIniRawValue(FAN_PRESETS_CONFIG, FanPresetSection(docked), FanCustomPresetNameKey(index)));
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

auto FanPresetLabels(bool docked) -> PopupList::Items {
    auto items = FanBuiltinPresetLabels();
    for (s64 i = 0; i < FAN_CUSTOM_PRESET_COUNT; i++) {
        items.emplace_back(FanCustomPresetLabel(i, docked, false));
    }
    return items;
}

auto FanCustomPresetLabels(bool docked) -> PopupList::Items {
    PopupList::Items items;
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
    return IniValueEquals(ATMOSPHERE_CONFIG, "tc", "use_configurations_on_fwdbg", "u8!0x1");
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

    R_TRY(SetIniValue(ATMOSPHERE_CONFIG, "tc", "use_configurations_on_fwdbg", "u8!0x1"));
    R_TRY(SetIniRawValue(ATMOSPHERE_CONFIG, "tc", "tskin_rate_table_handheld_on_fwdbg", handheld_value));
    R_TRY(SetIniRawValue(ATMOSPHERE_CONFIG, "tc", "tskin_rate_table_console_on_fwdbg", docked_value));
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
        float font_size = 22.f;
        if (m_message.length() < 60) {
            font_size = 28.f;
        } else if (m_message.length() < 120) {
            font_size = 25.f;
        } else if (m_message.length() > 200) {
            font_size = 20.f;
        }
        float line_height = 1.55f;

        nvgFontSize(vg, font_size);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
        nvgTextLineHeight(vg, line_height);

        float bounds[4]{};
        nvgTextBoxBounds(vg, m_pos.x + padding, m_pos.y, m_pos.w - padding * 2.f, m_message.c_str(), nullptr, bounds);
        float text_h = bounds[3] - bounds[1];
        float text_area_h = m_pos.h - 82.f;
        float text_y = m_pos.y + (text_area_h - text_h) / 2.f;

        gfx::drawTextBox(
            vg, m_pos.x + padding, text_y, font_size, m_pos.w - padding * 2.f,
            theme->GetColour(ThemeEntryID_TEXT), m_message.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP, nullptr, line_height
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
                        App::PushErrorBox(rc, "Failed to apply Kefir setting"_i18n);
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
                    App::PushErrorBox(rc, "Failed to run package action"_i18n);
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
        warning = "This action changes files on the SD card. Hold A to continue."_i18n;
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
        "Download DBI translations list"_i18n,
        "Update the DBI fan translations package list."_i18n,
        [](auto pbox) -> Result {
            R_TRY(DownloadFile(
                pbox,
                "Downloading list of translations..."_i18n,
                "https://github.com/rashevskyv/DBI_watcher/raw/main/output/package.ini",
                paths::DOWNLOADS + "/dbi.package.ini"
            ));
            R_TRY(MovePath(paths::DOWNLOADS + "/dbi.package.ini", DBI_TRANSLATIONS_PACKAGE));
            R_SUCCEED();
        },
    }));

    for (const auto& entry : ParseDbiTranslations(DBI_TRANSLATIONS_PACKAGE)) {
        items.emplace_back(MakePackageAction({
            entry.name,
            "Install DBI fan translation."_i18n,
            [entry](auto pbox) -> Result {
                return InstallDbiTranslation(pbox, entry);
            },
        }));
    }

    items.emplace_back(MakePackageAction({
        "Russian latest DBI"_i18n,
        "Download the latest Russian DBI build."_i18n,
        [](auto pbox) -> Result {
            R_TRY(DownloadFile(
                pbox,
                "Downloading Russian DBI..."_i18n,
                "https://github.com/rashevskyv/DBI/releases/latest/download/DBI.nro",
                "/switch/DBI/DBI_new.nro"
            ));
            R_TRY(MovePath("/switch/DBI/DBI_new.nro", "/switch/DBI/DBI.nro"));
            R_SUCCEED();
        },
    }));

    items.emplace_back(MakePackageAction({
        "Reset DBI config"_i18n,
        "Download a clean DBI config from Kefir."_i18n,
        [](auto pbox) -> Result {
            R_TRY(DownloadFile(
                pbox,
                "Resetting DBI Config..."_i18n,
                "https://github.com/rashevskyv/DBI/releases/latest/download/dbi.config",
                "/switch/DBI/dbi.config_new"
            ));
            R_TRY(MovePath("/switch/DBI/dbi.config_new", "/switch/DBI/dbi.config"));
            R_SUCCEED();
        },
        true,
        "This will replace your current DBI config with the default Kefir config."_i18n,
        0.5f,
    }));

    return items;
}

auto BuildSoftwareItems() -> std::vector<SettingsItem> {
    std::vector<SettingsItem> items;

    items.emplace_back(SettingsItem{
        "Homebrew App Store"_i18n,
        "Download and update homebrew apps."_i18n,
        [](){
            return std::string{};
        },
        [](){
            App::Push<ui::menu::appstore::Menu>(MenuFlag_None);
        },
        SettingsItemKind::Folder,
    });

    items.emplace_back(SettingsItem{
        "DBI"_i18n,
        "DBI installer and translations."_i18n,
        [](){
            return std::string{};
        },
        [](){
            App::Push<DbiMenu>();
        },
        SettingsItemKind::Folder,
    });

    items.emplace_back(MakePackageAction({
        "UAModDownloader"_i18n,
        "Ukrainian mods."_i18n,
        [](auto pbox) -> Result {
            R_TRY(DownloadFile(
                pbox,
                "Downloading UAModDownloader..."_i18n,
                "https://github.com/pro100luk/UAModDownloader/releases/latest/download/UAModDownloader.nro",
                "/switch/UAModDownloader/UAModDownloader_new.nro"
            ));
            R_TRY(MovePath("/switch/UAModDownloader/UAModDownloader_new.nro", "/switch/UAModDownloader/UAModDownloader.nro"));
            R_SUCCEED();
        },
    }));

    items.emplace_back(MakePackageAction({
        "ModCD"_i18n,
        "ECLIPS graphic mods."_i18n,
        [](auto pbox) -> Result {
            R_TRY(DownloadFile(
                pbox,
                "Downloading ModCD..."_i18n,
                "https://github.com/kawaii-flesh/ModCD/releases/latest/download/ModCD.nro",
                "/switch/ModCD/ModCD_new.nro"
            ));
            R_TRY(MovePath("/switch/ModCD/ModCD_new.nro", "/switch/ModCD/ModCD.nro"));
            R_SUCCEED();
        },
    }));

    items.emplace_back(MakePackageAction({
        "SimpleModDownloader"_i18n,
        "Game mods from GameBanana."_i18n,
        [](auto pbox) -> Result {
            R_TRY(DownloadFile(
                pbox,
                "Downloading SimpleModDownloader..."_i18n,
                "https://github.com/PoloNX/SimpleModDownloader/releases/latest/download/SimpleModDownloader.nro",
                "/switch/SimpleModDownloader/SimpleModDownloader_new.nro"
            ));
            R_TRY(MovePath("/switch/SimpleModDownloader/SimpleModDownloader_new.nro", "/switch/SimpleModDownloader/SimpleModDownloader.nro"));
            R_SUCCEED();
        },
    }));

    return items;
}

auto MakeFavoriteThemeItem(ui::menu::themezer::PackListEntry entry) -> SettingsItem {
    return {
        entry.details.name,
        entry.details.description.empty() ? "Install favorite theme." : entry.details.description,
        [](){
            return std::string{};
        },
        [entry](){
            App::Push<OptionBox>(
                "Download theme?"_i18n,
                "Back"_i18n, "Download"_i18n, 1, [entry](auto op_index){
                    if (op_index && *op_index) {
                        App::Push<ProgressBox>(0, "Downloading "_i18n, entry.details.name, [entry](auto pbox) -> Result {
                            return ui::menu::themezer::InstallTheme(pbox, entry);
                        }, [entry](Result rc){
                            App::PushErrorBox(rc, "Failed to download theme"_i18n);

                            if (R_SUCCEEDED(rc)) {
                                App::Notify("Downloaded "_i18n + entry.details.name);
                            }
                        });
                    }
                }
            );
        },
        SettingsItemKind::Favorite,
        entry.id
    };
}

auto BuildThemeItems() -> std::vector<SettingsItem> {
    std::vector<SettingsItem> items;

    items.emplace_back(SettingsItem{
        "Themezer",
        "Browse, download and install theme packs from themezer.net.",
        [](){
            return std::string{};
        },
        [](){
            App::Push<ui::menu::themezer::Menu>(MenuFlag_None);
        },
        SettingsItemKind::Folder,
    });

    items.emplace_back(MakePackageAction({
        "Mario BG Dark",
        "Download and extract Mario BG Modern theme.",
        [](auto pbox) -> Result {
            R_TRY(DownloadFile(
                pbox,
                "Downloading Mario BG Dark...",
                "https://github.com/rashevskyv/mario_bg_theme/releases/latest/download/Mario.BG.Modern.zip",
                paths::DOWNLOADS + "/theme.zip"
            ));
            R_TRY(UnzipFile(pbox, paths::DOWNLOADS + "/theme.zip", "/themes/"));
            R_TRY(DeletePath(paths::DOWNLOADS + "/theme.zip"));
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
                paths::DOWNLOADS + "/theme.zip"
            ));
            R_TRY(UnzipFile(pbox, paths::DOWNLOADS + "/theme.zip", "/themes/"));
            R_TRY(DeletePath(paths::DOWNLOADS + "/theme.zip"));
            R_SUCCEED();
        },
    }));

    for (const auto& entry : ui::menu::themezer::GetFavorites()) {
        items.emplace_back(MakeFavoriteThemeItem(entry));
    }

    return items;
}

auto BuildTranslateItems() -> std::vector<SettingsItem> {
    std::vector<SettingsItem> items;

    items.emplace_back(MakePackageAction({
        "Download language packs"_i18n,
        "Download the UltraHand language package list."_i18n,
        [](auto pbox) -> Result {
            R_TRY(DownloadFile(
                pbox,
                "Downloading language packs..."_i18n,
                "https://github.com/rashevskyv/switch-translations-mirrors/raw/main/lang_packs_ultra.zip",
                paths::DOWNLOADS + "/lang_packs.zip"
            ));
            R_TRY(MovePath(TRANSLATE_PACKAGE, TRANSLATE_PACKAGE_BACKUP));
            R_TRY(DeletePath(TRANSLATE_PACKAGE_DIR));
            fs::FsNativeSd fs;
            R_TRY(fs.CreateDirectoryRecursively(TRANSLATE_PACKAGE_DIR));
            if (FileExists(TRANSLATE_PACKAGE_BACKUP.c_str())) {
                R_TRY(CopyFileSimple(TRANSLATE_PACKAGE_BACKUP, std::string{TRANSLATE_PACKAGE_DIR} + "/package.ini.bkp"));
            }
            R_TRY(UnzipFile(pbox, paths::DOWNLOADS + "/lang_packs.zip", TRANSLATE_PACKAGE_DIR));
            R_TRY(DeletePath(paths::DOWNLOADS + "/lang_packs.zip"));
            R_SUCCEED();
        },
    }));

    items.emplace_back(MakePackageAction({
        "Remove installed translation"_i18n,
        "Delete installed interface translations and reboot."_i18n,
        [](auto pbox) -> Result {
            return RemoveInterfaceTranslation(pbox);
        },
        true,
        "This removes installed system interface translation files and reboots the console."_i18n,
        0.5f,
    }));

    for (const auto& entry : ParseInterfaceTranslations(TRANSLATE_PACKAGE)) {
        items.emplace_back(SettingsItem{
            entry.name,
            "Install interface translation."_i18n,
            [](){
                return std::string{};
            },
            [entry](){
                const auto options = ReadInterfaceReplacementOptions(entry);
                if (options.empty()) {
                    App::PushErrorBox(Result_FsEmpty, "No replacement languages found"_i18n);
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
                            "This will replace the selected system interface language and reboot the console."_i18n,
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
                                            App::PushErrorBox(rc, "Failed to install translation"_i18n);
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
        "Overclock status"_i18n,
        "Enable or disable Kefir overclock files."_i18n,
        [](){
            return FileExists("/atmosphere/kips/kefir.kip");
        },
        ApplyOverclock,
        "",
        0.5f,
    }));
    items.emplace_back(MakeKefirToggle({
        "40MB Memory"_i18n,
        "Toggle the 40MB applet memory patch."_i18n,
        [](){
            return IniValueEquals(ATMOSPHERE_CONFIG, "atmosphere", "force_40mb_applet", "u8!0x1");
        },
        Apply40Mb,
        "",
        0.5f,
    }));

    if (IsEmummcEnabled()) {
        items.emplace_back(MakeKefirToggle({
            "Redirect Emunand saves to SD"_i18n,
            "Experimental save redirection for emuMMC."_i18n,
            [](){
                return IniValueEquals(ATMOSPHERE_CONFIG, "atmosphere", "fsmitm_redirect_saves_to_sd", "u8!0x1");
            },
            ApplyRedirectSaves,
            "Experimental option.\n\nThis redirects emuMMC saves to the SD card. Use it only if you understand the risk; changing save paths can make saves appear missing until the setting is reverted."_i18n,
            0.5f,
        }));
    }

    items.emplace_back(MakeKefirToggle({
        "8GB DRAM status"_i18n,
        "Only for consoles with physically soldered 8GB RAM."_i18n,
        [](){
            return FileExists("/tegraexplorer/scripts/Remove_8GB-RAM_config.te");
        },
        Apply8GbDram,
        "Only for consoles with physically soldered 8GB RAM. Other consoles will not boot correctly.\n\nTo disable it if the console does not boot:\nhekate > payloads > TegraExplorer > Remove_8GB-RAM_config.te"_i18n,
        3.f,
    }));

    items.emplace_back(SettingsItem{
        "Fan curve"_i18n,
        "Edit Atmosphere tskin fan curves for handheld and docked modes."_i18n,
        [](){
            return "";
        },
        [](){
            App::Push<FanCurveMenu>();
        },
        SettingsItemKind::Folder,
    });

    items.emplace_back(SettingsItem{
        "Module Manager"_i18n,
        "Start, stop and configure installed sysmodules."_i18n,
        [](){
            return std::string{};
        },
        [](){
            App::Push<ui::menu::hats::UninstallerMenu>();
        },
        SettingsItemKind::Folder,
    });

    items.emplace_back(SettingsItem{
        "Translate Interface"_i18n,
        "Interface translation package tools."_i18n,
        [](){
            return std::string{};
        },
        [](){
            App::Push<ui::menu::settings::TranslateMenu>();
        },
        SettingsItemKind::Folder,
    });

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

auto EvaluateFanPercent(const std::vector<FanCurvePoint>& curve, float temp_c) -> float {
    if (curve.empty()) return 0.f;
    if (temp_c <= curve.front().temp_c) return static_cast<float>(curve.front().fan_percent);
    if (temp_c >= curve.back().temp_c) return static_cast<float>(curve.back().fan_percent);

    for (size_t i = 1; i < curve.size(); ++i) {
        const auto& prev = curve[i - 1];
        const auto& cur = curve[i];
        if (temp_c >= prev.temp_c && temp_c <= cur.temp_c) {
            const float temp_span = cur.temp_c - prev.temp_c;
            if (temp_span == 0.f) return static_cast<float>(prev.fan_percent);
            const float fan_span = cur.fan_percent - prev.fan_percent;
            const float temp_offset = temp_c - prev.temp_c;
            return prev.fan_percent + (fan_span * temp_offset) / temp_span;
        }
    }
    return static_cast<float>(curve.back().fan_percent);
}

} // namespace

struct FanCurveSensorSample {
    bool valid{};
    s32 temp_milli_c{};
    s32 fan_percent{-1};
};

struct FanCurveSensorReader final {
    FanCurveSensorReader() {
        m_ts_available = R_SUCCEEDED(tsInitialize());
    }

    ~FanCurveSensorReader() {
        if (m_ts_available) {
            tsExit();
        }
    }

#pragma pack(push, 1)
struct SphairaFanState {
    u32 magic;
    u32 version;
    s32 temp_milli_c;
    float fan_level;
    u64 timestamp_ns;
    u32 sysmodule_active;
};
#pragma pack(pop)

    void Update(const std::vector<FanCurvePoint>& active_curve) {
        const auto now = armTicksToNs(armGetSystemTick());
        const float dt = m_last_update_ns ? std::min(static_cast<float>(now - m_last_update_ns) / 1e9f, 0.1f) : 0.05f;
        m_last_update_ns = now;

        FanCurveSensorSample next{};
        SphairaFanState sys_state{};
        bool sys_ok = false;

        FILE* fp = fopen("/switch/sphaira/fan_status.bin", "rb");
        if (fp) {
            if (fread(&sys_state, sizeof(sys_state), 1, fp) == 1) {
                if (sys_state.magic == 0x46414E53 && sys_state.sysmodule_active == 1) {
                    if (now >= sys_state.timestamp_ns && (now - sys_state.timestamp_ns) < 3000000000ULL) {
                        sys_ok = true;
                    }
                }
            }
            fclose(fp);
        }

        if (sys_ok) {
            next.temp_milli_c = sys_state.temp_milli_c;
            next.valid = true;
        } else if (m_ts_available) {
            s32 temp_milli_c = 0;
            s32 temp_c = 0;
            TsSession session;
            if (R_SUCCEEDED(tsOpenSession(&session, TsDeviceCode_LocationExternal))) {
                float temp_f = 0.0f;
                Result rc = tsSessionGetTemperature(&session, &temp_f);
                tsSessionClose(&session);
                if (R_SUCCEEDED(rc) && temp_f > 0.0f) {
                    next.temp_milli_c = static_cast<s32>(temp_f * 1000.0f);
                    next.valid = true;
                }
            }
            if (!next.valid && R_SUCCEEDED(tsOpenSession(&session, TsDeviceCode_LocationInternal))) {
                float temp_f = 0.0f;
                Result rc = tsSessionGetTemperature(&session, &temp_f);
                tsSessionClose(&session);
                if (R_SUCCEEDED(rc) && temp_f > 0.0f) {
                    next.temp_milli_c = static_cast<s32>(temp_f * 1000.0f);
                    next.valid = true;
                }
            }
            if (!next.valid && R_SUCCEEDED(tsGetTemperature(TsLocation_External, &temp_c)) && temp_c > 0) {
                next.temp_milli_c = temp_c * 1000;
                next.valid = true;
            }
            if (!next.valid && R_SUCCEEDED(tsGetTemperature(TsLocation_Internal, &temp_c)) && temp_c > 0) {
                next.temp_milli_c = temp_c * 1000;
                next.valid = true;
            }
            if (!next.valid && R_SUCCEEDED(tsGetTemperatureMilliC(TsLocation_External, &temp_milli_c)) && temp_milli_c > 0) {
                next.temp_milli_c = temp_milli_c;
                next.valid = true;
            }
        }

        if (next.valid) {
            float target_fan = -1.0f;
            if (sys_ok) {
                target_fan = sys_state.fan_level * 100.0f;
            } else {
                const float temp_deg = static_cast<float>(next.temp_milli_c) / 1000.0f;
                target_fan = EvaluateFanPercent(active_curve, temp_deg);
            }

            if (m_displayed_fan_percent < 0.0f) {
                m_displayed_fan_percent = target_fan;
            } else {
                const float diff = target_fan - m_displayed_fan_percent;
                const float abs_diff = std::abs(diff);
                if (abs_diff < 0.2f) {
                    m_displayed_fan_percent = target_fan;
                } else {
                    const float speed_rate = (diff >= 0.0f) ? 25.0f : 18.0f; // 25% per sec up, 18% per sec down
                    const float max_step = std::min(abs_diff, speed_rate * dt);
                    m_displayed_fan_percent += (diff >= 0.0f ? max_step : -max_step);
                }
            }

            next.fan_percent = static_cast<s32>(m_displayed_fan_percent + 0.5f);
        }

        m_sample = next;
    }

    auto GetSample() const -> const FanCurveSensorSample* {
        return m_sample.valid ? &m_sample : nullptr;
    }

private:
    FanCurveSensorSample m_sample{};
    u64 m_last_update_ns{};
    float m_displayed_fan_percent{-1.0f};
    bool m_ts_available{};
};

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
    m_category_list->SetPageJump(false);
    m_category_list->SetFastScroll(false);

    m_item_list = std::make_unique<List>(1, 7, m_pos, Vec4{420.f, 132.f, 780.f, 66.f});
    m_item_list->SetLayout(List::Layout::GRID);
    m_item_list->SetPageJump(false);
    m_item_list->SetFastScroll(false);

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
    if (touch->is_clicked) {
        if (touch->in_range(m_category_list->GetPos())) {
            SetFocusPane(FocusPane::Categories);
        } else if (touch->in_range(m_item_list->GetPos())) {
            SetFocusPane(FocusPane::Items);
        }
    }

    if (m_focus_pane == FocusPane::Categories) {
        if (controller->GotDown(Button::RIGHT)) {
            SetFocusPane(FocusPane::Items);
            App::PlaySoundEffect(SoundEffect_Focus);
        }
    } else {
        if (controller->GotDown(Button::LEFT)) {
            SetFocusPane(FocusPane::Categories);
            App::PlaySoundEffect(SoundEffect_Focus);
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
        }, this);
    } else {
        const auto& category = m_categories[m_category_index];
        m_item_list->OnUpdate(controller, touch, m_item_index, category.items.size(), [this](bool touch, auto i) {
            if (touch && m_item_index == i) {
                FireAction(Button::A);
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                SetItemIndex(i);
            }
        }, this);
    }
}

namespace {

auto SettingsItemTextX(const SettingsItem& item, float x) -> float {
    return item.kind == SettingsItemKind::Normal ? x + 18.f : x + 74.f;
}

void DrawSettingsItemKindIcon(NVGcontext* vg, Theme* theme, const SettingsItem& item, Vec4 v, bool selected) {
    if (item.kind == SettingsItemKind::Normal) {
        return;
    }

    const auto colour = theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT_INFO);
    const auto x = v.x + 18.f;
    const auto y = v.y + 15.f;

    nvgSave(vg);
    nvgStrokeColor(vg, colour);
    nvgStrokeWidth(vg, 3.f);
    nvgLineCap(vg, NVG_ROUND);
    nvgLineJoin(vg, NVG_ROUND);

    if (item.kind == SettingsItemKind::Folder) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y + 8.f, 42.f, 28.f, 5.f);
        nvgMoveTo(vg, x + 5.f, y + 9.f);
        nvgLineTo(vg, x + 5.f, y + 4.f);
        nvgLineTo(vg, x + 19.f, y + 4.f);
        nvgLineTo(vg, x + 24.f, y + 9.f);
        nvgLineTo(vg, x + 37.f, y + 9.f);
        nvgStroke(vg);
    } else if (item.kind == SettingsItemKind::Download) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, x + 21.f, y + 3.f);
        nvgLineTo(vg, x + 21.f, y + 25.f);
        nvgMoveTo(vg, x + 10.f, y + 16.f);
        nvgLineTo(vg, x + 21.f, y + 27.f);
        nvgLineTo(vg, x + 32.f, y + 16.f);
        nvgMoveTo(vg, x + 8.f, y + 36.f);
        nvgLineTo(vg, x + 34.f, y + 36.f);
        nvgStroke(vg);
    } else if (item.kind == SettingsItemKind::Favorite) {
        const float cx = x + 21.f;
        const float cy = y + 20.f;
        const float rOut = 15.f;
        const float rIn = 7.f;
        nvgBeginPath(vg);
        for (int i = 0; i < 10; ++i) {
            float r = (i % 2 == 0) ? rOut : rIn;
            float angle = -3.14159265f / 2.f + i * 3.14159265f / 5.f;
            float px = cx + r * std::cos(angle);
            float py = cy + r * std::sin(angle);
            if (i == 0) {
                nvgMoveTo(vg, px, py);
            } else {
                nvgLineTo(vg, px, py);
            }
        }
        nvgClosePath(vg);
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

        {
            const float text_x = v.x + 18.f;
            const float text_w = v.w - 36.f;
            nvgFontSize(vg, 20.f);
            nvgTextLineHeight(vg, 1.0f);
            float label_bounds[4];
            nvgTextBoxBounds(vg, text_x, 0, text_w, m_categories[i].label.c_str(), nullptr, label_bounds);
            const float label_h = label_bounds[3] - label_bounds[1];
            const float label_y = v.y + (v.h - label_h) / 2.f;
            gfx::drawTextBox(
                vg, text_x, label_y, 20.f, text_w,
                theme->GetColour(text_id), m_categories[i].label.c_str()
            );
        }
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

        if (item.kind == SettingsItemKind::Folder) {
            const float x1 = v.x + v.w - 24.f;
            const float y1 = v.y + v.h / 2.f;
            nvgBeginPath(vg);
            nvgMoveTo(vg, x1 - 8.f, y1 - 8.f);
            nvgLineTo(vg, x1, y1);
            nvgLineTo(vg, x1 - 8.f, y1 + 8.f);
            nvgStrokeColor(vg, theme->GetColour(focused ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT_INFO));
            nvgStrokeWidth(vg, 3.f);
            nvgLineCap(vg, NVG_ROUND);
            nvgLineJoin(vg, NVG_ROUND);
            nvgStroke(vg);
        }
    });
}

void Menu::BuildCategories() {
    auto* app = App::GetApp();

    m_categories = {
        {
            "General"_i18n,
            "Language, timing and application flow."_i18n,
            {
                { "Language"_i18n, "Select the active interface language."_i18n, LanguageValue, [](){
                    PopupList::Items items;
                    for (const auto& lang : LANGUAGE_ITEMS) {
                        items.push_back(i18n::get(lang));
                    }
                    App::Push<PopupList>("Language"_i18n, std::move(items), [](std::optional<s64> op_index){
                        if (op_index) {
                            App::SetLanguage(*op_index);
                        }
                    }, App::GetLanguage());
                }},
                { "Text scroll speed"_i18n, "Select how fast long labels scroll."_i18n, TextScrollSpeedValue, [](){
                    PopupList::Items items;
                    for (const auto& speed : TEXT_SCROLL_SPEED_ITEMS) {
                        items.push_back(i18n::get(speed));
                    }
                    App::Push<PopupList>("Text scroll speed"_i18n, std::move(items), [](std::optional<s64> op_index){
                        if (op_index) {
                            App::SetTextScrollSpeed(*op_index);
                        }
                    }, App::GetTextScrollSpeed());
                }},
                MakeBoolItem("12 Hour Time"_i18n, "Use 12 hour clock format."_i18n, App::Get12HourTimeEnable, App::Set12HourTimeEnable),
                { "Restart Kefir Hub"_i18n, "Close and reopen the application."_i18n, [](){ return std::string{}; }, [](){
                    App::ExitRestart();
                }},
                { "Exit"_i18n, "Close Kefir Hub."_i18n, [](){ return std::string{}; }, [](){
                    App::Exit();
                }},
            }
        },
        {
            "Appearance"_i18n,
            "Theme and audio options."_i18n,
            {
                { "Theme"_i18n, "Select the active Kefir Hub theme."_i18n, ThemeValue, [](){
                    const auto themes = App::GetThemeMetaList();
                    if (!themes.empty()) {
                        PopupList::Items items;
                        for (const auto& theme : themes) {
                            items.push_back(theme.name);
                        }
                        App::Push<PopupList>("Theme"_i18n, std::move(items), [](std::optional<s64> op_index){
                            if (op_index) {
                                App::SetTheme(*op_index);
                            }
                        }, App::GetThemeIndex());
                    }
                }},
                MakeBoolItem("Music"_i18n, "Enable background music from the current theme."_i18n, App::GetThemeMusicEnable, App::SetThemeMusicEnable),
                MakeBoolItem("Animated waves"_i18n, "Enable animated background waves in the bottom bar."_i18n, App::GetAnimatedWavesEnable, App::SetAnimatedWavesEnable),
                { "Kefir Hub theme options"_i18n, "Select the Kefir Hub interface theme and music options."_i18n, [](){ return std::string{}; }, [](){
                    App::DisplayThemeOptions(false);
                }, SettingsItemKind::Folder },
            }
        },
        {
            "Network"_i18n,
            "Background services and network downloads."_i18n,
            {
                MakeBoolItem("FTP"_i18n, "Run the FTP server in the background."_i18n, App::GetFtpEnable, App::SetFtpEnable),
                MakeBoolItem("MTP"_i18n, "Run the MTP server in the background."_i18n, App::GetMtpEnable, App::SetMtpEnable),
                MakeBoolItem("Nxlink"_i18n, "Receive .nro files from a PC."_i18n, App::GetNxlinkEnable, App::SetNxlinkEnable),
                MakeBoolItem("HDD"_i18n, "Mount connected USB/HDD devices."_i18n, App::GetHddEnable, App::SetHddEnable),
                MakeBoolItem("HDD write protect"_i18n, "Make connected HDD storage read-only."_i18n, App::GetWriteProtect, App::SetWriteProtect),
                { "WebDAV URL"_i18n, "Use webdav:// for HTTPS and automatic remote folder creation. Leave empty to disable."_i18n, App::GetWebdavUrl, [](){
                    auto value = App::GetWebdavUrl();
                    const auto guide = "WebDAV URL"_i18n;
                    if (R_SUCCEEDED(swkbd::ShowText(value, guide.c_str(), value.c_str()))) {
                        App::SetWebdavUrl(std::move(value));
                    }
                }},
                { "WebDAV User"_i18n, "Username for the WebDAV server."_i18n, App::GetWebdavUser, [](){
                    auto value = App::GetWebdavUser();
                    const auto guide = "WebDAV User"_i18n;
                    if (R_SUCCEEDED(swkbd::ShowText(value, guide.c_str(), value.c_str()))) {
                        App::SetWebdavUser(std::move(value));
                    }
                }},
                { "WebDAV Password"_i18n, "Password for the WebDAV server."_i18n, [](){
                    return App::GetWebdavPass().empty() ? std::string{} : std::string(8, '*');
                }, [](){
                    std::string value;
                    const auto guide = "WebDAV Password"_i18n;
                    if (R_SUCCEEDED(swkbd::ShowText(value, guide.c_str()))) {
                        App::SetWebdavPass(std::move(value));
                    }
                }},
            }
        },
        {
            "Homebrew"_i18n,
            "Shortcuts for core Kefir Hub tools."_i18n,
            {
                { "Homebrew App Store"_i18n, "Download and update homebrew apps."_i18n, [](){ return std::string{}; }, [](){
                    App::Push<ui::menu::appstore::Menu>(MenuFlag_None);
                }},
                { "File Browser"_i18n, "Browse and manage files on the SD card."_i18n, [](){ return std::string{}; }, [](){
                    App::Push<ui::menu::filebrowser::Menu>(MenuFlag_None);
                }},
            }
        },

        {
            "Install"_i18n,
            "Install behavior and safety switches."_i18n,
            {
                MakeInstallToggle("Enable sysMMC"_i18n, "Allow installing while running sysMMC."_i18n, app->m_install_sysmmc),
                MakeInstallToggle("Enable emuMMC"_i18n, "Allow installing while running emuMMC."_i18n, app->m_install_emummc),
                { "Install location"_i18n, "Choose system memory or microSD card."_i18n, [](){
                    const auto loc = App::GetInstallLocation();
                    if (loc >= 0 && loc < 5) {
                        static constexpr const char* labels[] = {
                            "microSD card only",
                            "System memory only",
                            "System first, then SD",
                            "SD first, then system",
                            "Automatic"
                        };
                        return i18n::get(labels[loc]);
                    }
                    return std::string{};
                }, [](){
                    PopupList::Items items;
                    items.push_back("microSD card only"_i18n);
                    items.push_back("System memory only"_i18n);
                    items.push_back("System first, then SD"_i18n);
                    items.push_back("SD first, then system"_i18n);
                    items.push_back("Automatic"_i18n);

                    App::Push<PopupList>("Install location"_i18n, std::move(items), [](std::optional<s64> op_index){
                        if (op_index) {
                            App::SetInstallLocation(*op_index);
                        }
                    }, App::GetInstallLocation());
                }},
                MakeOptionItem("Allow downgrade"_i18n, "Allow lower title updates to be installed."_i18n, app->m_allow_downgrade),
                MakeOptionItem("Skip if already installed"_i18n, "Skip titles or NCAs that are already installed."_i18n, app->m_skip_if_already_installed),
                MakeOptionItem("Ticket only"_i18n, "Install tickets without title contents."_i18n, app->m_ticket_only),
                MakeOptionItem("Skip base"_i18n, "Skip installing base applications."_i18n, app->m_skip_base),
                MakeOptionItem("Skip patch"_i18n, "Skip installing title updates."_i18n, app->m_skip_patch),
                MakeOptionItem("Skip DLC"_i18n, "Skip installing DLC content."_i18n, app->m_skip_addon),
                MakeOptionItem("Skip data patch"_i18n, "Skip installing DLC updates."_i18n, app->m_skip_data_patch),
                MakeOptionItem("Skip ticket"_i18n, "Skip installing tickets."_i18n, app->m_skip_ticket),
            }
        },
        {
            "Dump"_i18n,
            "Game dump naming and transfer options."_i18n,
            {
                MakeOptionItem("Created nested folder"_i18n, "Create a nested folder for each game dump."_i18n, app->m_dump_app_folder),
                MakeOptionItem("Append folder with .xci"_i18n, "Append .xci to XCI dump folders."_i18n, app->m_dump_append_folder_with_xci),
                MakeOptionItem("Trim XCI"_i18n, "Remove unused data from XCI dumps."_i18n, app->m_dump_trim_xci),
                MakeOptionItem("Label trimmed XCI"_i18n, "Mark trimmed XCI output names."_i18n, app->m_dump_label_trim_xci),
                MakeOptionItem("USB transfer stream"_i18n, "Stream dump output over USB."_i18n, app->m_dump_usb_transfer_stream),
                MakeOptionItem("Convert to common ticket"_i18n, "Convert personalized tickets during dump."_i18n, app->m_dump_convert_to_common_ticket),
            }
        },
        {
            "Advanced"_i18n,
            "Power-user options and verification controls."_i18n,
            {
                MakeBoolItem("Logging"_i18n, "Write logs to /config/kefir/log.txt."_i18n, App::GetLogEnable, App::SetLogEnable),
                MakeBoolItem("Replace hbmenu on exit"_i18n, "Replace /hbmenu.nro with Kefir Hub on exit."_i18n, App::GetReplaceHbmenuEnable, App::SetReplaceHbmenuEnable),
                MakeOptionItem("Boost CPU during transfer"_i18n, "Enable CPU boost during transfers."_i18n, app->m_progress_boost_mode),
                MakeOptionItem("Skip NCA hash verify"_i18n, "Skip SHA-256 verification over NCA content."_i18n, app->m_skip_nca_hash_verify),
                MakeOptionItem("Skip RSA header verify"_i18n, "Skip RSA NCA fixed-key header verification."_i18n, app->m_skip_rsa_header_fixed_key_verify),
                MakeOptionItem("Skip RSA NPDM verify"_i18n, "Skip RSA NPDM fixed-key verification."_i18n, app->m_skip_rsa_npdm_fixed_key_verify),
                MakeOptionItem("Ignore distribution bit"_i18n, "Ignore the NCA distribution bit."_i18n, app->m_ignore_distribution_bit),
                MakeOptionItem("Convert to common ticket"_i18n, "Convert personalized tickets to common tickets."_i18n, app->m_convert_to_common_ticket),
                MakeOptionItem("Convert to standard crypto"_i18n, "Convert titlekey to standard crypto."_i18n, app->m_convert_to_standard_crypto),
                MakeOptionItem("Lower master key"_i18n, "Encrypt key area keys with master key 0."_i18n, app->m_lower_master_key),
                MakeOptionItem("Lower system version"_i18n, "Lower the system firmware field in metadata."_i18n, app->m_lower_system_version),
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

auto FanCurveProfileLabel(bool docked) -> const char* {
    return docked ? "Docked" : "Handheld";
}

auto FanCurveGraphRect() -> Vec4 {
    return {360.f, 106.f, 860.f, 520.f};
}

auto FanCurvePlotRect() -> Vec4 {
    const auto graph = FanCurveGraphRect();
    return {graph.x + 58.f, graph.y + 66.f, graph.w - 90.f, graph.h - 132.f};
}

auto FanCurveListRect() -> Vec4 {
    return {58.f, 134.f, 270.f, 468.f};
}

auto FanCurveListItemRect() -> Vec4 {
    return {66.f, 184.f, 248.f, 46.f};
}

auto FanCurveXForTempValue(const Vec4& plot, float temp_c) -> float {
    const auto span = static_cast<float>(FAN_TEMP_MAX_C - FAN_TEMP_MIN_C);
    const auto value = std::clamp(temp_c, static_cast<float>(FAN_TEMP_MIN_C), static_cast<float>(FAN_TEMP_MAX_C));
    return plot.x + plot.w * ((value - static_cast<float>(FAN_TEMP_MIN_C)) / span);
}

auto FanCurveXForTemp(const Vec4& plot, s32 temp_c) -> float {
    return FanCurveXForTempValue(plot, static_cast<float>(temp_c));
}

auto FanCurveYForFan(const Vec4& plot, s32 fan_percent) -> float {
    return plot.y + plot.h - plot.h * (static_cast<float>(fan_percent) / 100.f);
}

auto FanCurveTempForX(const Vec4& plot, float x) -> s32 {
    const auto ratio = std::clamp((x - plot.x) / plot.w, 0.f, 1.f);
    return FAN_TEMP_MIN_C + static_cast<s32>(ratio * static_cast<float>(FAN_TEMP_MAX_C - FAN_TEMP_MIN_C) + 0.5f);
}

auto FanCurveFanForY(const Vec4& plot, float y) -> s32 {
    const auto ratio = std::clamp((plot.y + plot.h - y) / plot.h, 0.f, 1.f);
    return static_cast<s32>(ratio * 100.f + 0.5f);
}

auto ExpandRect(Vec4 rect, float amount) -> Vec4 {
    rect.x -= amount;
    rect.y -= amount;
    rect.w += amount * 2.f;
    rect.h += amount * 2.f;
    return rect;
}

auto WithAlpha(NVGcolor colour, float alpha) -> NVGcolor {
    colour.a = alpha;
    return colour;
}

auto FormatMilliC(s32 milli_c) -> std::string {
    const auto negative = milli_c < 0;
    const auto abs_milli = negative ? -milli_c : milli_c;
    const auto tenths = (abs_milli + 50) / 100;
    return std::string{negative ? "-" : ""} + std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + "C";
}

void DrawHorizontalDashes(NVGcontext* vg, float x0, float x1, float y, const NVGcolor& colour) {
    if (x1 < x0) {
        std::swap(x0, x1);
    }

    nvgStrokeWidth(vg, 1.5f);
    nvgStrokeColor(vg, colour);
    for (float x = x0; x < x1; x += 13.f) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, x, y);
        nvgLineTo(vg, std::min(x + 7.f, x1), y);
        nvgStroke(vg);
    }
}

void DrawVerticalDashes(NVGcontext* vg, float x, float y0, float y1, const NVGcolor& colour) {
    if (y1 < y0) {
        std::swap(y0, y1);
    }

    nvgStrokeWidth(vg, 1.5f);
    nvgStrokeColor(vg, colour);
    for (float y = y0; y < y1; y += 13.f) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, x, y);
        nvgLineTo(vg, x, std::min(y + 7.f, y1));
        nvgStroke(vg);
    }
}



void DrawFanCurveSensorMarker(NVGcontext* vg, Theme* theme, const Vec4& plot, const std::vector<FanCurvePoint>& curve, const FanCurveSensorSample* sensor) {
    if (!sensor) {
        return;
    }

    const auto accent = theme->GetColour(ThemeEntryID_TEXT_SELECTED);
    const auto temp_c = static_cast<float>(sensor->temp_milli_c) / 1000.f;
    const auto x = FanCurveXForTempValue(plot, temp_c);
    const float fan_percent = (sensor->fan_percent >= 0) ? static_cast<float>(sensor->fan_percent) : EvaluateFanPercent(curve, temp_c);
    const auto y = FanCurveYForFan(plot, fan_percent);
    const auto guide_colour = WithAlpha(accent, 0.42f);

    DrawHorizontalDashes(vg, plot.x, x, y, guide_colour);
    DrawVerticalDashes(vg, x, y, plot.y + plot.h, guide_colour);

    nvgBeginPath(vg);
    nvgCircle(vg, x, y, 19.f);
    nvgFillColor(vg, WithAlpha(accent, 0.20f));
    nvgFill(vg);

    nvgStrokeWidth(vg, 2.5f);
    nvgStrokeColor(vg, WithAlpha(accent, 0.95f));
    nvgBeginPath(vg);
    nvgCircle(vg, x, y, 19.f);
    nvgStroke(vg);

    nvgBeginPath(vg);
    nvgCircle(vg, x, y, 4.5f);
    nvgFillColor(vg, WithAlpha(accent, 0.95f));
    nvgFill(vg);

    const auto place_left = x > plot.x + plot.w - 145.f;
    const auto label_x = place_left ? x - 24.f : x + 24.f;
    const auto label_y = std::clamp(y - 22.f, plot.y + 12.f, plot.y + plot.h - 10.f);
    const auto align = (place_left ? NVG_ALIGN_RIGHT : NVG_ALIGN_LEFT) | NVG_ALIGN_MIDDLE;
    const auto label = FormatMilliC(sensor->temp_milli_c);
    gfx::drawTextArgs(
        vg, label_x, label_y, 14.f, align,
        theme->GetColour(ThemeEntryID_TEXT), "%s  %d%%", label.c_str(), static_cast<s32>(fan_percent + 0.5f)
    );
}

void DrawFanCurveGraph(NVGcontext* vg, Theme* theme, const std::vector<FanCurvePoint>& curve, const std::vector<FanCurvePoint>& control_points, bool easy_curve_mode, s64 selected, bool docked, bool dirty, bool editing, const FanCurveSensorSample* sensor) {
    const auto graph = FanCurveGraphRect();
    const auto plot = FanCurvePlotRect();

    gfx::drawRect(vg, graph, theme->GetColour(ThemeEntryID_SIDEBAR), 5.f);

    gfx::drawTextArgs(
        vg, graph.x + 24.f, graph.y + 20.f, 22.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
        theme->GetColour(ThemeEntryID_TEXT), "%s curve", FanCurveProfileLabel(docked)
    );
    if (easy_curve_mode) {
        if (!control_points.empty()) {
            const auto safe_index = std::clamp<s64>(selected, 0, static_cast<s64>(control_points.size() - 1));
            const auto& point = control_points[safe_index];
            const char* name = (safe_index == 0) ? "Min" : ((safe_index == 1) ? "Mid" : "Max");
            gfx::drawTextArgs(
                vg, graph.x + graph.w - 24.f, graph.y + 20.f, 18.f,
                NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT),
                "%s %s   %dC   %d%%", editing ? "Editing point" : "Point", name, point.temp_c, point.fan_percent
            );
        }
    } else {
        if (!curve.empty()) {
            const auto safe_index = std::clamp<s64>(selected, 0, static_cast<s64>(curve.size() - 1));
            const auto& point = curve[safe_index];
            gfx::drawTextArgs(
                vg, graph.x + graph.w - 24.f, graph.y + 20.f, 18.f,
                NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT),
                "%s %d   %dC   %d%%", editing ? "Editing point" : "Point", static_cast<int>(safe_index + 1), point.temp_c, point.fan_percent
            );
        }
    }
    if (dirty) {
        gfx::drawText(
            vg, graph.x + 24.f, graph.y + 44.f, 15.f,
            theme->GetColour(ThemeEntryID_TEXT_SELECTED), "Unsaved changes", NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE
        );
    }
    if (easy_curve_mode) {
        gfx::drawText(
            vg, graph.x + 24.f, graph.y + (dirty ? 64.f : 44.f), 15.f,
            theme->GetColour(ThemeEntryID_TEXT_INFO), "Bezier", NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE
        );
    }

    const auto line_colour = theme->GetColour(ThemeEntryID_LINE_SEPARATOR);
    const auto info_colour = theme->GetColour(ThemeEntryID_TEXT_INFO);
    const auto text_colour = theme->GetColour(ThemeEntryID_TEXT);
    const auto accent_colour = theme->GetColour(ThemeEntryID_TEXT_SELECTED);

    nvgSave(vg);
    nvgStrokeWidth(vg, 1.f);
    nvgStrokeColor(vg, line_colour);

    for (s32 fan = 0; fan <= 100; fan += 25) {
        const auto y = FanCurveYForFan(plot, fan);
        nvgBeginPath(vg);
        nvgMoveTo(vg, plot.x, y);
        nvgLineTo(vg, plot.x + plot.w, y);
        nvgStroke(vg);
        gfx::drawTextArgs(vg, plot.x - 10.f, y, 14.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, info_colour, "%d%%", fan);
    }

    for (s32 temp = 0; temp <= FAN_TEMP_MAX_C; temp += 15) {
        const auto x = FanCurveXForTemp(plot, temp);
        nvgBeginPath(vg);
        nvgMoveTo(vg, x, plot.y);
        nvgLineTo(vg, x, plot.y + plot.h);
        nvgStroke(vg);
        gfx::drawTextArgs(vg, x, plot.y + plot.h + 14.f, 14.f, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, info_colour, "%dC", temp);
    }

    nvgStrokeWidth(vg, 2.f);
    nvgStrokeColor(vg, text_colour);
    nvgBeginPath(vg);
    nvgMoveTo(vg, plot.x, plot.y);
    nvgLineTo(vg, plot.x, plot.y + plot.h);
    nvgLineTo(vg, plot.x + plot.w, plot.y + plot.h);
    nvgStroke(vg);

    if (!curve.empty()) {
        if (easy_curve_mode) {
            // 1. Draw original curve (thin line + small dots)
            nvgStrokeWidth(vg, 2.5f);
            nvgStrokeColor(vg, WithAlpha(text_colour, 0.4f));
            nvgBeginPath(vg);
            for (size_t i = 0; i < curve.size(); i++) {
                const auto x = FanCurveXForTemp(plot, curve[i].temp_c);
                const auto y = FanCurveYForFan(plot, curve[i].fan_percent);
                if (i) {
                    nvgLineTo(vg, x, y);
                } else {
                    nvgMoveTo(vg, x, y);
                }
            }
            nvgStroke(vg);

            for (size_t i = 0; i < curve.size(); i++) {
                const auto x = FanCurveXForTemp(plot, curve[i].temp_c);
                const auto y = FanCurveYForFan(plot, curve[i].fan_percent);
                nvgBeginPath(vg);
                nvgCircle(vg, x, y, 4.f);
                nvgFillColor(vg, WithAlpha(text_colour, 0.6f));
                nvgFill(vg);
            }

            // 2. Draw smooth Bezier green helper curve
            const auto green_colour = nvgRGBA(46, 204, 113, 255); // emerald green
            nvgStrokeWidth(vg, 4.5f);
            nvgStrokeColor(vg, green_colour);
            nvgBeginPath(vg);
            
            const double bx0 = control_points[0].temp_c;
            const double bx2 = control_points[2].temp_c;
            const s32 steps = 30;
            for (s32 s = 0; s <= steps; s++) {
                const s32 temp = static_cast<s32>(bx0 + (bx2 - bx0) * s / steps + 0.5);
                const auto eval_bezier = [&](s32 temp_val) -> float {
                    const double X0 = control_points[0].temp_c;
                    const double Y0 = control_points[0].fan_percent;
                    const double X1 = control_points[1].temp_c;
                    const double Y1 = control_points[1].fan_percent;
                    const double X2 = control_points[2].temp_c;
                    const double Y2 = control_points[2].fan_percent;

                    if (temp_val <= X0) return Y0;
                    if (temp_val >= X2) return Y2;

                    const double a = X0 - 2.0 * X1 + X2;
                    const double b = 2.0 * (X1 - X0);
                    const double c = X0 - temp_val;

                    double t = 0.0;
                    if (std::abs(a) < 1e-5) {
                        if (std::abs(b) > 1e-5) t = -c / b;
                    } else {
                        const double disc = b * b - 4.0 * a * c;
                        if (disc >= 0.0) {
                            const double r1 = (-b + std::sqrt(disc)) / (2.0 * a);
                            const double r2 = (-b - std::sqrt(disc)) / (2.0 * a);
                            t = (r1 >= -0.01 && r1 <= 1.01) ? std::clamp(r1, 0.0, 1.0) : std::clamp(r2, 0.0, 1.0);
                        }
                    }
                    const double fan = (1.0 - t) * (1.0 - t) * Y0 + 2.0 * (1.0 - t) * t * Y1 + t * t * Y2;
                    return std::clamp(fan, static_cast<double>(FAN_PERCENT_MIN), static_cast<double>(FAN_PERCENT_MAX));
                };

                const auto x = FanCurveXForTemp(plot, temp);
                const auto y = FanCurveYForFan(plot, eval_bezier(temp));
                if (s) {
                    nvgLineTo(vg, x, y);
                } else {
                    nvgMoveTo(vg, x, y);
                }
            }
            nvgStroke(vg);

            // 3. Draw the 3 green control points
            for (size_t i = 0; i < control_points.size(); i++) {
                const auto point_selected = static_cast<s64>(i) == selected;
                const auto x = FanCurveXForTemp(plot, control_points[i].temp_c);
                const auto y = FanCurveYForFan(plot, control_points[i].fan_percent);
                nvgBeginPath(vg);
                nvgCircle(vg, x, y, point_selected ? 10.f : 7.f);
                nvgFillColor(vg, point_selected ? text_colour : green_colour);
                nvgFill(vg);
                if (point_selected && editing) {
                    nvgStrokeWidth(vg, 2.5f);
                    nvgStrokeColor(vg, WithAlpha(green_colour, 0.95f));
                    nvgBeginPath(vg);
                    nvgCircle(vg, x, y, 15.f);
                    nvgStroke(vg);
                }
            }

        } else {
            // Draw normal curve
            nvgStrokeWidth(vg, 4.f);
            nvgStrokeColor(vg, accent_colour);
            nvgBeginPath(vg);
            for (size_t i = 0; i < curve.size(); i++) {
                const auto x = FanCurveXForTemp(plot, curve[i].temp_c);
                const auto y = FanCurveYForFan(plot, curve[i].fan_percent);
                if (i) {
                    nvgLineTo(vg, x, y);
                } else {
                    nvgMoveTo(vg, x, y);
                }
            }
            nvgStroke(vg);

            for (size_t i = 0; i < curve.size(); i++) {
                const auto point_selected = static_cast<s64>(i) == selected;
                const auto x = FanCurveXForTemp(plot, curve[i].temp_c);
                const auto y = FanCurveYForFan(plot, curve[i].fan_percent);
                nvgBeginPath(vg);
                nvgCircle(vg, x, y, point_selected ? 10.f : 7.f);
                nvgFillColor(vg, point_selected ? text_colour : accent_colour);
                nvgFill(vg);
                if (point_selected && editing) {
                    nvgStrokeWidth(vg, 2.5f);
                    nvgStrokeColor(vg, WithAlpha(accent_colour, 0.95f));
                    nvgBeginPath(vg);
                    nvgCircle(vg, x, y, 15.f);
                    nvgStroke(vg);
                }
            }
        }

        DrawFanCurveSensorMarker(vg, theme, plot, curve, sensor);
    }
    nvgRestore(vg);
}

void DrawFanCurveListItem(NVGcontext* vg, Theme* theme, Vec4 v, const FanCurvePoint& point, s64 index, bool selected) {
    const auto label_id = selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;
    const auto value_id = selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT_INFO;

    if (selected) {
        gfx::drawRect(vg, v, theme->GetColour(ThemeEntryID_SELECTED_BACKGROUND), 5.f);
        gfx::drawRectOutline(vg, theme, 4.f, v);
    } else {
        gfx::drawRect(vg, v.x, v.y + v.h, v.w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
    }

    gfx::drawTextArgs(
        vg, v.x + 14.f, v.y + v.h / 2.f, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
        theme->GetColour(label_id), "%d", static_cast<int>(index + 1)
    );
    gfx::drawTextArgs(
        vg, v.x + 142.f, v.y + v.h / 2.f, 18.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
        theme->GetColour(value_id), "%dC", point.temp_c
    );
    gfx::drawTextArgs(
        vg, v.x + v.w - 14.f, v.y + v.h / 2.f, 18.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
        theme->GetColour(value_id), "%d%%", point.fan_percent
    );
}

void DrawFanCurveListHeader(NVGcontext* vg, Theme* theme) {
    const auto v = FanCurveListRect();
    const auto title_colour = theme->GetColour(ThemeEntryID_TEXT);
    const auto column_colour = theme->GetColour(ThemeEntryID_TEXT_INFO);
    gfx::drawText(vg, v.x + 22.f, v.y, 23.f, title_colour, "Points:", NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    gfx::drawText(vg, v.x + 22.f, v.y + 34.f, 14.f, column_colour, "#", NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    gfx::drawText(vg, v.x + 150.f, v.y + 34.f, 14.f, column_colour, "Temp", NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
    gfx::drawText(vg, v.x + v.w - 22.f, v.y + 34.f, 14.f, column_colour, "Fan", NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
}

void DrawActionListItem(NVGcontext* vg, Theme* theme, Vec4 v, const SettingsItem& item, bool selected) {
    const auto label_id = selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;
    const auto value = item.value ? item.value() : std::string{};
    const auto value_width = value.empty() ? 0.f : 224.f;

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
        vg, text_x, v.y + 10.f, 20.f, v.w - 18.f - value_width - text_offset,
        theme->GetColour(label_id), item.label.c_str()
    );
    if (!item.description.empty()) {
        gfx::drawTextBox(
            vg, text_x, v.y + 39.f, 14.f, v.w - 18.f - value_width - text_offset,
            theme->GetColour(ThemeEntryID_TEXT_INFO), item.description.c_str()
        );
    }

    if (!value.empty()) {
        gfx::drawText(
            vg, v.x + v.w - 20.f, v.y + 21.f, 18.f,
            SettingsValueColour(theme, value, selected),
            value.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP
        );
    }

    if (item.kind == SettingsItemKind::Folder) {
        const float x1 = v.x + v.w - 24.f;
        const float y1 = v.y + v.h / 2.f;
        nvgBeginPath(vg);
        nvgMoveTo(vg, x1 - 8.f, y1 - 8.f);
        nvgLineTo(vg, x1, y1);
        nvgLineTo(vg, x1 - 8.f, y1 + 8.f);
        nvgStrokeColor(vg, theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT_INFO));
        nvgStrokeWidth(vg, 3.f);
        nvgLineCap(vg, NVG_ROUND);
        nvgLineJoin(vg, NVG_ROUND);
        nvgStroke(vg);
    }
}

} // namespace

FanCurveMenu::FanCurveMenu() : MenuBase{"Fan curve", MenuFlag_None} {
    if (FileExists("/atmosphere/contents/00FF46554E43544C/flags/boot2.flag")) {
        DeletePath("/atmosphere/contents/00FF46554E43544C/flags/boot2.flag");
    }

    if (!IsSphairaFanSysmoduleRunning() && IsSphairaFanSysmoduleInstalled()) {
        RestartSphairaFanSysmodule();
    }

    m_handheld_curve = ReadFanCurve(
        "tskin_rate_table_handheld_on_fwdbg",
        "tskin_rate_table_handheld",
        DefaultHandheldFanCurve()
    );
    m_docked_curve = ReadFanCurve(
        "tskin_rate_table_console_on_fwdbg",
        "tskin_rate_table_console",
        DefaultDockedFanCurve()
    );
    m_applied_handheld_curve = m_handheld_curve;
    m_applied_docked_curve = m_docked_curve;
    
    // Initialize Bezier control points from loaded curves
    m_docked = false;
    InitializeControlPointsFromCurve();
    m_docked = true;
    InitializeControlPointsFromCurve();
    m_docked = false;

    m_sysmodule_enabled = IsSphairaFanSysmoduleRunning();
    m_sensor_reader = std::make_unique<FanCurveSensorReader>();
    RefreshActions();

    m_list = std::make_unique<List>(1, 9, FanCurveListRect(), FanCurveListItemRect());
    m_list->SetLayout(List::Layout::GRID);
    m_list->SetPageJump(false);
    SetIndex(0);
}

FanCurveMenu::~FanCurveMenu() = default;

auto FanCurveMenu::ActiveCurve() -> std::vector<FanCurvePoint>& {
    return m_docked ? m_docked_curve : m_handheld_curve;
}

auto FanCurveMenu::ActiveCurve() const -> const std::vector<FanCurvePoint>& {
    return m_docked ? m_docked_curve : m_handheld_curve;
}

auto FanCurveMenu::ActiveControlPoints() -> std::vector<FanCurvePoint>& {
    return m_docked ? m_docked_control_points : m_handheld_control_points;
}

auto FanCurveMenu::ActiveControlPoints() const -> const std::vector<FanCurvePoint>& {
    return m_docked ? m_docked_control_points : m_handheld_control_points;
}

auto FanCurveMenu::ActiveOriginalTemps() -> std::vector<s32>& {
    return m_docked ? m_docked_original_temps : m_handheld_original_temps;
}

auto FanCurveMenu::ActiveOriginalTemps() const -> const std::vector<s32>& {
    return m_docked ? m_docked_original_temps : m_handheld_original_temps;
}

void FanCurveMenu::InitializeControlPointsFromCurve() {
    auto& curve = ActiveCurve();
    auto& controls = ActiveControlPoints();
    auto& orig_temps = ActiveOriginalTemps();
    controls.clear();
    orig_temps.clear();

    if (curve.empty()) {
        controls.push_back({40, 20});
        controls.push_back({60, 50});
        controls.push_back({80, 100});
        orig_temps = {40, 48, 56, 64, 72, 80};
        return;
    }

    // Save original temperatures
    for (const auto& pt : curve) {
        orig_temps.push_back(pt.temp_c);
    }

    controls.push_back(curve.front());

    s32 t_min = curve.front().temp_c;
    s32 t_max = curve.back().temp_c;
    s32 t_mid = (t_min + t_max) / 2;
    s32 f_mid = static_cast<s32>(EvaluateFanPercent(curve, static_cast<float>(t_mid)) + 0.5f);
    
    controls.push_back({t_mid, f_mid});
    controls.push_back(curve.back());
}

auto FanCurveMenu::EvaluateBezierFanPercent(const std::vector<FanCurvePoint>& controls, s32 temp_c) const -> s32 {
    if (controls.size() != 3) {
        return 0;
    }
    const double X0 = controls[0].temp_c;
    const double Y0 = controls[0].fan_percent;
    const double X1 = controls[1].temp_c;
    const double Y1 = controls[1].fan_percent;
    const double X2 = controls[2].temp_c;
    const double Y2 = controls[2].fan_percent;

    if (temp_c <= X0) return static_cast<s32>(Y0);
    if (temp_c >= X2) return static_cast<s32>(Y2);

    const double a = X0 - 2.0 * X1 + X2;
    const double b = 2.0 * (X1 - X0);
    const double c = X0 - temp_c;

    double t = 0.0;
    if (std::abs(a) < 1e-5) {
        if (std::abs(b) > 1e-5) {
            t = -c / b;
        }
    } else {
        const double disc = b * b - 4.0 * a * c;
        if (disc >= 0.0) {
            const double r1 = (-b + std::sqrt(disc)) / (2.0 * a);
            const double r2 = (-b - std::sqrt(disc)) / (2.0 * a);
            if (r1 >= -0.01 && r1 <= 1.01) {
                t = std::clamp(r1, 0.0, 1.0);
            } else {
                t = std::clamp(r2, 0.0, 1.0);
            }
        }
    }
    const double fan = (1.0 - t) * (1.0 - t) * Y0 + 2.0 * (1.0 - t) * t * Y1 + t * t * Y2;
    return std::clamp(static_cast<s32>(fan + 0.5), FAN_PERCENT_MIN, FAN_PERCENT_MAX);
}

void FanCurveMenu::RegenerateCurveFromControls() {
    auto& controls = ActiveControlPoints();
    auto& curve = ActiveCurve();
    const auto& orig_temps = ActiveOriginalTemps();
    if (controls.size() != 3 || curve.empty() || orig_temps.size() != curve.size()) {
        return;
    }

    // Clamp Mid temperature strictly between Min and Max
    controls[1].temp_c = std::clamp(controls[1].temp_c, controls[0].temp_c + 1, controls[2].temp_c - 1);

    const double new_t_min = controls[0].temp_c;
    const double new_t_max = controls[2].temp_c;
    const double old_t_min = orig_temps.front();
    const double old_t_max = orig_temps.back();

    const double old_range = old_t_max - old_t_min;
    const double new_range = new_t_max - new_t_min;

    // 1. Update original points' temperatures proportionally
    for (size_t i = 0; i < curve.size(); i++) {
        if (i == 0) {
            curve[i].temp_c = controls[0].temp_c;
        } else if (i == curve.size() - 1) {
            curve[i].temp_c = controls[2].temp_c;
        } else {
            if (old_range > 0.0) {
                const double pct = (orig_temps[i] - old_t_min) / old_range;
                curve[i].temp_c = static_cast<s32>(new_t_min + pct * new_range + 0.5);
            } else {
                curve[i].temp_c = controls[0].temp_c;
            }
        }
    }

    // Ensure the temperatures of the curve are strictly sorted/monotonic and separated by at least 1 degree
    for (size_t i = 1; i < curve.size(); i++) {
        if (curve[i].temp_c <= curve[i - 1].temp_c) {
            curve[i].temp_c = curve[i - 1].temp_c + 1;
        }
    }
    // Make sure we didn't overshoot max temp
    if (curve.back().temp_c != controls[2].temp_c) {
        curve.back().temp_c = controls[2].temp_c;
        for (size_t i = curve.size() - 2; i > 0; i--) {
            if (curve[i].temp_c >= curve[i + 1].temp_c) {
                curve[i].temp_c = curve[i + 1].temp_c - 1;
            }
        }
    }

    // 2. Update each original point's fan speed based on the green Bezier curve
    for (size_t i = 0; i < curve.size(); i++) {
        curve[i].fan_percent = EvaluateBezierFanPercent(controls, curve[i].temp_c);
    }
}

void FanCurveMenu::RefreshActions() {
    RemoveActions();
    SetUiButtonSort(true);
    SetAction(Button::SELECT, Action{App::Exit});

    if (m_editing) {
        SetActions(
            std::make_pair(Button::A, Action{"Done"_i18n, [this](){
                SetEditing(false);
            }}),
            std::make_pair(Button::B, Action{"Done"_i18n, [this](){
                SetEditing(false);
            }})
        );
        return;
    }

    const auto apply_label = "Apply"_i18n;
    const auto apply_mode = FanCurveApplyMode::Live;

    SetActions(
        std::make_pair(Button::A, Action{"Edit"_i18n, [this](){
            SetEditing(true);
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            OnBack();
        }}),
        std::make_pair(Button::X, Action{"Mode"_i18n, [this](){
            SwitchProfile();
        }}),
        std::make_pair(Button::Y, Action{m_helper_curve_mode ? "Manual Mode"_i18n : "Bezier"_i18n, [this](){
            SetEditing(false);
            m_helper_curve_mode = !m_helper_curve_mode;
            if (m_helper_curve_mode) {
                InitializeControlPointsFromCurve();
            }
            SetIndex(0);
            RefreshActions();
        }}),
        std::make_pair(Button::L2, Action{"Load Preset"_i18n, [this](){
            DisplayPresets();
        }}),
        std::make_pair(Button::R2, Action{"Save Preset"_i18n, [this](){
            DisplaySavePreset();
        }}),
        std::make_pair(Button::START, Action{apply_label, [this, apply_mode](){
            ApplyCurves(apply_mode);
        }})
    );

    SetAction(Button::L, Action{"Add Point"_i18n, [this](){
        AddPoint();
    }});
    SetAction(Button::R, Action{"Remove Point"_i18n, [this](){
        RemovePoint();
    }});
}

void FanCurveMenu::RefreshSubHeading() {
    SetSubHeading("");
}

void FanCurveMenu::SetIndex(s64 index) {
    const auto& points = m_helper_curve_mode ? ActiveControlPoints() : ActiveCurve();
    if (points.empty()) {
        m_index = 0;
        RefreshSubHeading();
        return;
    }

    m_index = std::clamp<s64>(index, 0, static_cast<s64>(points.size() - 1));
    if (!m_index) {
        m_list->SetYoff(0);
    }
    RefreshSubHeading();
}

void FanCurveMenu::SetEditing(bool editing) {
    if (m_editing == editing) {
        return;
    }
    if (editing && ActiveCurve().empty()) {
        return;
    }

    m_editing = editing;
    RefreshActions();
    RefreshSubHeading();
}

void FanCurveMenu::SwitchProfile() {
    SetEditing(false);
    m_docked = !m_docked;
    SetIndex(m_index);
}

void FanCurveMenu::DisplayPresets() {
    App::Push<PopupList>(
        "Fan preset"_i18n,
        FanPresetLabels(m_docked),
        [this](auto index){
            if (index) {
                ApplyPreset(*index);
            }
        },
        0
    );
}

void FanCurveMenu::DisplaySavePreset() {
    App::Push<PopupList>(
        "Save fan preset"_i18n,
        FanCustomPresetLabels(m_docked),
        [this](auto index){
            if (index) {
                SavePreset(*index);
            }
        },
        0
    );
}

void FanCurveMenu::DisplayApplyMenu() {
    const bool live_apply_available = IsSphairaFanSysmoduleInstalled();
    PopupList::Items items;
    if (live_apply_available) {
        items.emplace_back("Apply"_i18n);
    } else {
        items.emplace_back("Save and Reboot"_i18n);
    }

    App::Push<PopupList>(
        "Apply fan curve"_i18n,
        std::move(items),
        [this, live_apply_available](auto index){
            if (!index) {
                return;
            }
            if (live_apply_available && *index == 0) {
                ApplyCurves(FanCurveApplyMode::Live);
            } else {
                ApplyCurves(FanCurveApplyMode::Reboot);
            }
        },
        0
    );
}

void FanCurveMenu::ApplyPreset(s64 index) {
    SetEditing(false);
    std::vector<FanCurvePoint> curve;
    if (index < FAN_BUILTIN_PRESET_COUNT) {
        curve = FanPresetCurve(index, m_docked);
    } else if (!ReadCustomFanPreset(index - FAN_BUILTIN_PRESET_COUNT, m_docked, curve)) {
        App::Notify("Fan preset is empty"_i18n);
        return;
    }

    ActiveCurve() = std::move(curve);
    if (m_helper_curve_mode) {
        InitializeControlPointsFromCurve();
    }
    m_dirty = true;
    SetIndex(m_index);
}

void FanCurveMenu::SavePreset(s64 index) {
    auto name = ReadCustomFanPresetName(index, m_docked);
    if (R_FAILED(swkbd::ShowText(name, "Preset name", name.c_str(), 0, 48))) {
        return;
    }
    name = SanitizeFanPresetName(std::move(name));
    if (name.empty()) {
        name = FanCustomPresetDefaultName(index);
    }

    const auto rc = SaveCustomFanPreset(index, m_docked, ActiveCurve(), name);
    if (R_FAILED(rc)) {
        App::PushErrorBox(rc, "Failed to save fan preset"_i18n);
        return;
    }

    App::Notify("Saved fan preset: "_i18n + name);
}

void FanCurveMenu::AddPoint() {
    auto& curve = ActiveCurve();
    if (curve.empty()) {
        curve.push_back({40, 20});
        m_dirty = true;
        SetIndex(0);
        return;
    }

    if (curve.size() >= static_cast<size_t>(FAN_TEMP_MAX_C - FAN_TEMP_MIN_C + 1)) {
        App::Notify("No room for another fan point"_i18n);
        return;
    }

    NormalizeFanCurve(curve);

    const auto index = std::clamp<s64>(m_index, 0, static_cast<s64>(curve.size() - 1));
    s64 insert_index = index + 1;
    FanCurvePoint point{};
    bool found{};

    if (index + 1 < static_cast<s64>(curve.size()) && curve[index + 1].temp_c - curve[index].temp_c > 1) {
        const auto& left = curve[index];
        const auto& right = curve[index + 1];
        point.temp_c = (left.temp_c + right.temp_c) / 2;
        point.fan_percent = (left.fan_percent + right.fan_percent) / 2;
        found = true;
    } else if (index > 0 && curve[index].temp_c - curve[index - 1].temp_c > 1) {
        const auto& left = curve[index - 1];
        const auto& right = curve[index];
        insert_index = index;
        point.temp_c = (left.temp_c + right.temp_c) / 2;
        point.fan_percent = (left.fan_percent + right.fan_percent) / 2;
        found = true;
    } else if (curve[index].temp_c < FAN_TEMP_MAX_C) {
        const auto& base = curve[index];
        point.temp_c = std::max(base.temp_c + 1, (base.temp_c + FAN_TEMP_MAX_C) / 2);
        point.fan_percent = base.fan_percent;
        found = true;
    } else if (curve[index].temp_c > FAN_TEMP_MIN_C) {
        const auto& base = curve[index];
        insert_index = index;
        point.temp_c = std::min(base.temp_c - 1, (FAN_TEMP_MIN_C + base.temp_c) / 2);
        point.fan_percent = base.fan_percent;
        found = true;
    }

    if (!found) {
        App::Notify("No room for another fan point"_i18n);
        return;
    }

    curve.insert(curve.begin() + insert_index, point);
    NormalizeFanCurve(curve);
    m_dirty = true;
    if (m_helper_curve_mode) {
        InitializeControlPointsFromCurve();
    }
    SetIndex(insert_index);
}

void FanCurveMenu::RemovePoint() {
    auto& curve = ActiveCurve();
    if (curve.size() <= 2) {
        App::Notify("Fan curve needs at least two points"_i18n);
        return;
    }

    const auto index = std::clamp<s64>(m_index, 0, static_cast<s64>(curve.size() - 1));
    curve.erase(curve.begin() + index);
    NormalizeFanCurve(curve);
    m_dirty = true;
    if (m_helper_curve_mode) {
        InitializeControlPointsFromCurve();
    }
    SetEditing(false);
    SetIndex(std::min<s64>(index, static_cast<s64>(curve.size() - 1)));
}

void FanCurveMenu::AdjustSelectedFan(s32 delta) {
    const auto& points = m_helper_curve_mode ? ActiveControlPoints() : ActiveCurve();
    if (points.empty()) {
        return;
    }

    const auto index = std::clamp<s64>(m_index, 0, static_cast<s64>(points.size() - 1));
    const auto& point = points[index];
    SetSelectedPoint(index, point.temp_c, point.fan_percent + delta);
}

void FanCurveMenu::AdjustSelectedTemp(s32 delta) {
    const auto& points = m_helper_curve_mode ? ActiveControlPoints() : ActiveCurve();
    if (points.empty()) {
        return;
    }

    const auto index = std::clamp<s64>(m_index, 0, static_cast<s64>(points.size() - 1));
    const auto& point = points[index];
    SetSelectedPoint(index, point.temp_c + delta, point.fan_percent);
}

void FanCurveMenu::SetSelectedPoint(s64 index, s32 temp_c, s32 fan_percent) {
    if (m_helper_curve_mode) {
        auto& controls = ActiveControlPoints();
        if (controls.size() != 3) {
            return;
        }
        index = std::clamp<s64>(index, 0, 2);
        
        s32 min_temp = FAN_TEMP_MIN_C;
        s32 max_temp = FAN_TEMP_MAX_C;
        if (index == 0) {
            min_temp = FAN_TEMP_MIN_C;
            max_temp = controls[1].temp_c - 1;
        } else if (index == 1) {
            min_temp = controls[0].temp_c + 1;
            max_temp = controls[2].temp_c - 1;
        } else if (index == 2) {
            min_temp = controls[1].temp_c + 1;
            max_temp = FAN_TEMP_MAX_C;
        }

        s32 min_fan = FAN_PERCENT_MIN;
        s32 max_fan = FAN_PERCENT_MAX;

        auto& point = controls[index];
        const auto next_temp = std::clamp<s32>(temp_c, min_temp, max_temp);
        const auto next_fan = std::clamp<s32>(fan_percent, min_fan, max_fan);

        m_index = index;
        if (point.temp_c != next_temp || point.fan_percent != next_fan) {
            point.temp_c = next_temp;
            point.fan_percent = next_fan;
            RegenerateCurveFromControls();
            m_dirty = true;
        }
    } else {
        auto& curve = ActiveCurve();
        if (curve.empty()) {
            return;
        }

        index = std::clamp<s64>(index, 0, static_cast<s64>(curve.size() - 1));
        const auto min_value = index ? curve[index - 1].temp_c + 1 : FAN_TEMP_MIN_C;
        const auto max_value = index + 1 < static_cast<s64>(curve.size()) ? curve[index + 1].temp_c - 1 : FAN_TEMP_MAX_C;
        const auto min_fan = index ? curve[index - 1].fan_percent : FAN_PERCENT_MIN;
        const auto max_fan = index + 1 < static_cast<s64>(curve.size()) ? curve[index + 1].fan_percent : FAN_PERCENT_MAX;
        auto& point = curve[index];
        const auto next_temp = std::clamp<s32>(temp_c, min_value, max_value);
        const auto next_fan = std::clamp<s32>(fan_percent, min_fan, max_fan);

        m_index = index;
        if (point.temp_c != next_temp || point.fan_percent != next_fan) {
            point.temp_c = next_temp;
            point.fan_percent = next_fan;
            m_dirty = true;
        }
    }
    RefreshSubHeading();
}

auto FanCurveMenu::HandleGraphTouch(TouchInfo* touch) -> bool {
    const auto& points = m_helper_curve_mode ? ActiveControlPoints() : ActiveCurve();
    if (points.empty()) {
        m_touch_dragging = false;
        return false;
    }

    const auto graph_touch = ExpandRect(FanCurveGraphRect(), 18.f);
    const auto plot = FanCurvePlotRect();
    const auto pick_point = [&](float x, float y, s64& out_index) {
        constexpr float PICK_RADIUS = 34.f;
        constexpr float PICK_DISTANCE = PICK_RADIUS * PICK_RADIUS;
        auto best_distance = std::numeric_limits<float>::max();
        s64 best_index{};

        for (size_t i = 0; i < points.size(); i++) {
            const auto point_x = FanCurveXForTemp(plot, points[i].temp_c);
            const auto point_y = FanCurveYForFan(plot, points[i].fan_percent);
            const auto dx = point_x - x;
            const auto dy = point_y - y;
            const auto distance = dx * dx + dy * dy;
            if (distance < best_distance) {
                best_distance = distance;
                best_index = static_cast<s64>(i);
            }
        }

        if (best_distance > PICK_DISTANCE) {
            return false;
        }

        out_index = best_index;
        return true;
    };

    if (touch->is_clicked && touch->in_range(graph_touch)) {
        m_touch_dragging = false;
        s64 best_index{};
        if (pick_point(static_cast<float>(touch->cur.x), static_cast<float>(touch->cur.y), best_index)) {
            SetIndex(best_index);
            return true;
        }
        return false;
    }

    if (touch->is_touching && (m_touch_dragging || touch->in_range(graph_touch))) {
        if (!m_touch_dragging) {
            s64 best_index{};
            if (touch->is_scroll || !pick_point(static_cast<float>(touch->cur.x), static_cast<float>(touch->cur.y), best_index)) {
                return false;
            }
            m_index = best_index;
            m_touch_dragging = true;
        }

        SetSelectedPoint(m_index, FanCurveTempForX(plot, touch->cur.x), FanCurveFanForY(plot, touch->cur.y));
        return true;
    }

    if (touch->is_end) {
        m_touch_dragging = false;
    }

    return false;
}

void FanCurveMenu::ApplyCurves(FanCurveApplyMode mode) {
    const auto handheld = m_handheld_curve;
    const auto docked = m_docked_curve;
    const bool live_apply = mode == FanCurveApplyMode::Live;

    App::Push<ProgressBox>(
        0,
        "Applying"_i18n,
        "Fan curve",
        [handheld, docked, mode, live_apply](auto pbox) -> Result {
            pbox->NewTransfer("Writing Atmosphere fan curve and restarting fan module...");
            return ApplyFanCurves(handheld, docked, mode);
        },
        [this, live_apply, handheld, docked](Result rc){
            if (R_FAILED(rc)) {
                App::Push<HoldConfirmBox>(
                    "Failed to activate fan module.\n\nChanges will apply on next reboot.\n\nHold A to reboot now and apply changes."_i18n,
                    3.f,
                    [](bool confirmed){
                        if (confirmed) {
                            RebootAfterSetting();
                        }
                    }
                );
                m_dirty = false;
                return;
            }
            m_applied_handheld_curve = handheld;
            m_applied_docked_curve = docked;
            m_dirty = false;
            App::Notify("Fan curve applied"_i18n);
        }
    );
}

void FanCurveMenu::OnBack() {
    if (m_editing) {
        SetEditing(false);
        return;
    }

    if (!m_dirty) {
        SetPop();
        return;
    }

    App::Push<OptionBox>(
        "Discard unsaved fan curve changes?"_i18n,
        "Cancel"_i18n,
        "Discard"_i18n,
        0,
        [this](auto op_index){
            if (op_index && *op_index) {
                SetPop();
            }
        }
    );
}

void FanCurveMenu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    if (m_sensor_reader) {
        const auto& applied_curve = m_docked ? m_applied_docked_curve : m_applied_handheld_curve;
        m_sensor_reader->Update(applied_curve);
    }

    if (HandleGraphTouch(touch)) {
        return;
    }

    if (m_editing) {
        s32 temp_delta{};
        s32 fan_delta{};
        if (controller->GotDown(Button::LEFT)) {
            temp_delta--;
        }
        if (controller->GotDown(Button::RIGHT)) {
            temp_delta++;
        }
        if (controller->GotDown(Button::UP)) {
            fan_delta++;
        }
        if (controller->GotDown(Button::DOWN)) {
            fan_delta--;
        }

        if (temp_delta || fan_delta) {
            const auto& points = m_helper_curve_mode ? ActiveControlPoints() : ActiveCurve();
            if (!points.empty()) {
                const auto index = std::clamp<s64>(m_index, 0, static_cast<s64>(points.size() - 1));
                const auto point = points[index];
                SetSelectedPoint(index, point.temp_c + temp_delta, point.fan_percent + fan_delta);
            }
        }
        return;
    }

    const auto& points = m_helper_curve_mode ? ActiveControlPoints() : ActiveCurve();
    m_list->OnUpdate(controller, touch, m_index, points.size(), [this](bool touch, auto i) {
        if (!touch || m_index != i) {
            App::PlaySoundEffect(SoundEffect_Focus);
            SetIndex(i);
        }
    }, this);
}

void FanCurveMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    const auto& curve = ActiveCurve();
    const auto& controls = ActiveControlPoints();
    DrawFanCurveListHeader(vg, theme);

    if (m_helper_curve_mode) {
        m_list->Draw(vg, theme, controls.size(), [this, &controls](auto* vg, auto* theme, Vec4 v, auto i) {
            DrawFanCurveListItem(vg, theme, v, controls[i], i, m_index == i);
        });
    } else {
        m_list->Draw(vg, theme, curve.size(), [this, &curve](auto* vg, auto* theme, Vec4 v, auto i) {
            DrawFanCurveListItem(vg, theme, v, curve[i], i, m_index == i);
        });
    }

    DrawFanCurveGraph(vg, theme, curve, controls, m_helper_curve_mode, m_index, m_docked, m_dirty, m_editing, m_sensor_reader ? m_sensor_reader->GetSample() : nullptr);
}

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
    m_list->SetPageJump(false);
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
    }, this);
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
    m_list->SetPageJump(false);
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
    }, this);
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

KefirSettingsMenu::KefirSettingsMenu() : MenuBase{"Kefir Settings", MenuFlag_None} {
    m_items = BuildKefirItems();
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
    m_list->SetPageJump(false);
    SetIndex(0);
}

KefirSettingsMenu::~KefirSettingsMenu() = default;

void KefirSettingsMenu::OnFocusGained() {
    MenuBase::OnFocusGained();
    std::string item_label;
    if (!m_items.empty()) {
        item_label = m_items[m_index].label;
    }
    m_items = BuildKefirItems();
    auto it = std::find_if(m_items.cbegin(), m_items.cend(), [&](const auto& item) {
        return item.label == item_label;
    });
    SetIndex(it == m_items.cend() ? m_index : std::distance(m_items.cbegin(), it));
}

void KefirSettingsMenu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);
    m_list->OnUpdate(controller, touch, m_index, m_items.size(), [this](bool touch, auto i) {
        if (touch && m_index == i) {
            FireAction(Button::A);
        } else {
            App::PlaySoundEffect(SoundEffect_Focus);
            SetIndex(i);
        }
    }, this);
}

void KefirSettingsMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);
    m_list->Draw(vg, theme, m_items.size(), [this](auto* vg, auto* theme, Vec4 v, auto i) {
        DrawActionListItem(vg, theme, v, m_items[i], m_index == i);
    });
}

void KefirSettingsMenu::SetIndex(s64 index) {
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

void KefirSettingsMenu::OnSelect() {
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
    m_list->SetPageJump(false);
    SetIndex(0);
}

ThemesMenu::~ThemesMenu() = default;

void ThemesMenu::OnFocusGained() {
    MenuBase::OnFocusGained();
    m_items = BuildThemeItems();
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
    }, this);
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
        RemoveAction(Button::R3);
        return;
    }
    m_index = std::clamp<s64>(index, 0, static_cast<s64>(m_items.size() - 1));
    if (!m_index) {
        m_list->SetYoff(0);
    }
    SetSubHeading(m_items[m_index].description);

    const auto& item = m_items[m_index];
    if (item.kind == SettingsItemKind::Favorite) {
        SetAction(Button::R3, Action{"Unstar"_i18n, [this, id = item.id](){
            ini_puts("themezer_favorites", id.c_str(), nullptr, App::CONFIG_PATH);
            ini_puts("themezer_favorites", (id + "_name").c_str(), nullptr, App::CONFIG_PATH);
            ini_puts("themezer_favorites", (id + "_creator").c_str(), nullptr, App::CONFIG_PATH);
            ini_puts("themezer_favorites", (id + "_themes").c_str(), nullptr, App::CONFIG_PATH);
            App::Notify("Removed from Favorites"_i18n);

            m_items = BuildThemeItems();
            SetIndex(m_index);
        }});
    } else {
        RemoveAction(Button::R3);
    }
}

void ThemesMenu::OnSelect() {
    if (!m_items.empty() && m_items[m_index].action) {
        m_items[m_index].action();
    }
}

TranslateMenu::TranslateMenu() : MenuBase{"Translate Interface"_i18n, MenuFlag_None} {
    m_items = BuildTranslateItems();
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
    m_list->SetPageJump(false);
    SetIndex(0);
}

TranslateMenu::~TranslateMenu() = default;

void TranslateMenu::OnFocusGained() {
    MenuBase::OnFocusGained();
    SetIndex(m_index);
}

void TranslateMenu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);
    m_list->OnUpdate(controller, touch, m_index, m_items.size(), [this](bool touch, auto i) {
        if (touch && m_index == i) {
            FireAction(Button::A);
        } else {
            App::PlaySoundEffect(SoundEffect_Focus);
            SetIndex(i);
        }
    }, this);
}

void TranslateMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);
    m_list->Draw(vg, theme, m_items.size(), [this](auto* vg, auto* theme, Vec4 v, auto i) {
        DrawActionListItem(vg, theme, v, m_items[i], m_index == i);
    });
}

void TranslateMenu::SetIndex(s64 index) {
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

void TranslateMenu::OnSelect() {
    if (!m_items.empty() && m_items[m_index].action) {
        m_items[m_index].action();
    }
}

} // namespace sphaira::ui::menu::settings
