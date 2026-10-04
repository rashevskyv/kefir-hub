#include "system_info.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "i18n.hpp"

#include <switch.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace sphaira::system_info {
namespace {

struct Report {
    std::string text;

    void Section(const char* title) {
        if (!text.empty()) {
            text += "\n";
        }
        const auto t = i18n::get(title);
        text += t + "\n" + std::string(t.size(), '=') + "\n";
    }
    void Line(const char* label, const std::string& value) {
        text += i18n::get(label) + ": " + value + "\n";
    }
};

auto Fmt(const char* fmt, auto... args) -> std::string {
    char buf[128];
    std::snprintf(buf, sizeof(buf), fmt, args...);
    return buf;
}

auto Mac(const u8* a) -> std::string {
    return Fmt("%02X:%02X:%02X:%02X:%02X:%02X", a[0], a[1], a[2], a[3], a[4], a[5]);
}

auto YesNo(bool v) -> std::string {
    return v ? "Yes"_i18n : "No"_i18n;
}

void Firmware(Report& r) {
    r.Section("Firmware");
    SetSysFirmwareVersion fw{};
    if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw))) {
        r.Line("Version", fw.display_version);
        r.Line("Name", fw.display_title);
        r.Line("Version hash", fw.version_hash);
    }
    u64 v{};
    if (R_SUCCEEDED(splGetConfig(SplConfigItem_HardwareType, &v))) {
        static constexpr const char* types[] = {"Icosa (Erista)", "Copper", "Hoag (Lite)", "Iowa (Mariko)", "Calcio", "Aula (OLED)"};
        r.Line("Hardware", v < std::size(types) ? types[v] : Fmt("%lu", v));
    }
    if (R_SUCCEEDED(splGetConfig(SplConfigItem_IsRetail, &v))) {
        r.Line("Unit", v ? "Retail"_i18n : "Development"_i18n);
    }
    if (R_SUCCEEDED(splGetConfig(SplConfigItem_DramId, &v))) {
        r.Line("DRAM id", Fmt("%lu", v));
    }
    if (R_SUCCEEDED(splGetConfig(SplConfigItem_DeviceId, &v))) {
        r.Line("Device id", Fmt("%016lX", v));
    }
    SetSysSerialNumber serial{};
    if (R_SUCCEEDED(setsysGetSerialNumber(&serial))) {
        r.Line("Serial number", serial.number);
    }
    SetSysDeviceNickName nick{};
    if (R_SUCCEEDED(setsysGetDeviceNickname(&nick))) {
        r.Line("Console nickname", nick.nickname);
    }
    u64 lang{};
    if (R_SUCCEEDED(setGetSystemLanguage(&lang))) {
        char code[9]{};
        std::memcpy(code, &lang, 8);
        r.Line("Language", code);
    }
    SetRegion region{};
    if (R_SUCCEEDED(setGetRegionCode(&region))) {
        static constexpr const char* regions[] = {"Japan", "USA", "Europe", "Australia", "Hong Kong / Taiwan / Korea", "China"};
        r.Line("Region", static_cast<size_t>(region) < std::size(regions) ? regions[region] : Fmt("%d", (int)region));
    }
    if (R_SUCCEEDED(pctlInitialize())) {
        bool restricted{};
        if (R_SUCCEEDED(pctlIsRestrictionEnabled(&restricted))) {
            r.Line("Parental controls", restricted ? "On"_i18n : "Off"_i18n);
        }
        pctlExit();
    }
}

void Atmosphere(Report& r) {
    r.Section("Atmosphere");
    u64 v{};
    if (R_SUCCEEDED(splGetConfig(static_cast<SplConfigItem>(65000), &v))) {
        r.Line("Version", Fmt("%lu.%lu.%lu", (v >> 56) & 0xFF, (v >> 48) & 0xFF, (v >> 40) & 0xFF));
        r.Line("Target firmware", Fmt("%lu.%lu.%lu", (v >> 24) & 0xFF, (v >> 16) & 0xFF, (v >> 8) & 0xFF));
        r.Line("Key generation", Fmt("%lu", (v >> 32) & 0xFF));
    }
    if (R_SUCCEEDED(splGetConfig(static_cast<SplConfigItem>(65003), &v))) {
        r.Line("Git commit", Fmt("%016lX", v));
    }
    if (R_SUCCEEDED(splGetConfig(static_cast<SplConfigItem>(65004), &v))) {
        r.Line("RCM bug patched", YesNo(v));
    }
    r.Line("emuMMC", YesNo(App::IsEmummc()));
    if (R_SUCCEEDED(splGetConfig(static_cast<SplConfigItem>(65010), &v))) {
        r.Line("USB 3.0 forced", YesNo(v));
    }
}

void Battery(Report& r) {
    if (R_FAILED(psmInitialize())) {
        return;
    }
    ON_SCOPE_EXIT(psmExit());
    r.Section("Battery and power");
    u32 percent{};
    if (R_SUCCEEDED(psmGetBatteryChargePercentage(&percent))) {
        r.Line("Charge", Fmt("%u%%", percent));
    }
    double d{};
    if (R_SUCCEEDED(psmGetRawBatteryChargePercentage(&d))) {
        r.Line("Charge (raw)", Fmt("%.2f%%", d));
    }
    if (R_SUCCEEDED(psmGetBatteryAgePercentage(&d))) {
        r.Line("Battery health", Fmt("%.1f%%", d));
    }
    PsmChargerType charger{};
    if (R_SUCCEEDED(psmGetChargerType(&charger))) {
        static constexpr const char* chargers[] = {"Not connected", "Official charger", "Low power charger", "Unsupported charger"};
        r.Line("Charger", static_cast<size_t>(charger) < std::size(chargers) ? i18n::get(chargers[charger]) : Fmt("%d", (int)charger));
    }
    if (hosversionAtLeast(17, 0, 0)) {
        PsmBatteryChargeInfoFields f{};
        if (R_SUCCEEDED(psmGetBatteryChargeInfoFields(&f))) {
            r.Line("Charging", YesNo(f.battery_charging));
            r.Line("Battery temperature", Fmt("%.1f °C", f.temperature_celcius / 1000.0));
            r.Line("Battery voltage", Fmt("%u mV", f.battery_charge_milli_voltage));
            r.Line("Input current limit", Fmt("%u mA", f.input_current_limit));
            r.Line("Fast charge current limit", Fmt("%u mA", f.fast_charge_current_limit));
            r.Line("Charge voltage limit", Fmt("%u mV", f.charge_voltage_limit));
            r.Line("Power source voltage", Fmt("%u mV", f.charger_input_voltage_limit));
            r.Line("Power source current", Fmt("%u mA", f.charger_input_current_limit));
        }
    }
}

void Hardware(Report& r) {
    if (R_FAILED(setcalInitialize())) {
        return;
    }
    ON_SCOPE_EXIT(setcalExit());
    r.Section("Hardware");
    SetCalBdAddress bt{};
    if (R_SUCCEEDED(setcalGetBdAddress(&bt))) {
        r.Line("Bluetooth MAC", Mac(bt.bd_addr));
    }
    SetCalMacAddress wlan{};
    if (R_SUCCEEDED(setcalGetWirelessLanMacAddress(&wlan))) {
        r.Line("Wi-Fi MAC", Mac(wlan.addr));
    }
    SetCalConfigurationId1 cfg{};
    if (R_SUCCEEDED(setcalGetConfigurationId1(&cfg))) {
        r.Line("Configuration id", std::string(reinterpret_cast<const char*>(cfg.cfg), strnlen(reinterpret_cast<const char*>(cfg.cfg), sizeof(cfg.cfg))));
    }
    SetBatteryLot lot{};
    if (R_SUCCEEDED(setcalGetBatteryLot(&lot))) {
        r.Line("Battery lot", std::string(lot.lot, strnlen(lot.lot, sizeof(lot.lot))));
    }
}

} // namespace

auto BuildReport() -> std::string {
    Report r;
    splInitialize();
    ON_SCOPE_EXIT(splExit());
    Firmware(r);
    Atmosphere(r);
    Battery(r);
    Hardware(r);
    return r.text;
}

} // namespace sphaira::system_info
