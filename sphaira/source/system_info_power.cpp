// Tools → System information: the Power and Battery groups. psm for what the
// charger negotiates, the MAX17050 fuel gauge over i2c for what the cell itself
// reports (capacities, cycles, current).
#include "system_info_internal.hpp"
#include "defines.hpp"
#include "i18n.hpp"

#include <switch.h>
#include <optional>

namespace sphaira::system_info {
namespace {

auto ChargerName(PsmChargerType t) -> std::string {
    static constexpr const char* names[] = {"Not connected", "Official charger", "Low power charger", "Unsupported charger"};
    return static_cast<size_t>(t) < std::size(names) ? i18n::get(names[t]) : Fmt("%d", (int)t);
}

auto UsbChargerName(u32 t) -> std::string {
    static constexpr const char* names[] = {"None", "USB Power Delivery", "USB-C 1.5 A", "USB-C 3 A", "Dedicated charging port", "Charging downstream port", "Standard USB port", "Apple 0.5 A", "Apple 1 A", "Apple 2 A"};
    return t < std::size(names) ? i18n::get(names[t]) : Fmt("%u", t);
}

auto PowerRoleName(u32 r) -> std::string {
    switch (r) {
        case 1: return "Sink (console is charged)"_i18n;
        case 2: return "Source (console powers the accessory)"_i18n;
        default: return Fmt("%u", r);
    }
}

auto Vdd50Name(PsmVdd50State s) -> std::string {
    static constexpr const char* names[] = {"Unknown", "A off, B off", "A on, B off", "A off, B on"};
    return static_cast<size_t>(s) < std::size(names) ? i18n::get(names[s]) : Fmt("%d", (int)s);
}

// MAX17050 fuel gauge registers, read over the i2c service. Scaling per the
// datasheet with the 10 mOhm sense resistor every Switch model uses.
// ponytail: fixed 10 mOhm Rsense; make it a knob only if a board ever differs.
struct FuelGauge {
    I2cSession session{};
    bool open{};

    FuelGauge() {
        open = R_SUCCEEDED(i2cInitialize()) && R_SUCCEEDED(i2cOpenSession(&session, I2cDevice_Max17050));
    }
    ~FuelGauge() {
        if (open) {
            i2csessionClose(&session);
        }
        i2cExit();
    }

    auto Read(u8 reg) -> std::optional<u16> {
        if (!open) {
            return std::nullopt;
        }
        u16 value{};
        if (R_FAILED(i2csessionSendAuto(&session, &reg, sizeof(reg), I2cTransactionOption_Start)) ||
            R_FAILED(i2csessionReceiveAuto(&session, &value, sizeof(value), I2cTransactionOption_All))) {
            return std::nullopt;
        }
        return value;
    }

    // capacity registers: 5 uVh / Rsense = 0.5 mAh per lsb.
    auto Capacity(u8 reg) -> std::optional<double> {
        const auto v = Read(reg);
        return v ? std::optional<double>{*v * 0.5} : std::nullopt;
    }
    // current registers: 1.5625 uV / Rsense = 0.15625 mA per lsb, signed.
    auto Current(u8 reg) -> std::optional<double> {
        const auto v = Read(reg);
        return v ? std::optional<double>{static_cast<s16>(*v) * 0.15625} : std::nullopt;
    }
};

constexpr u8 MAX17050_RepCap = 0x05;
constexpr u8 MAX17050_Age = 0x07;
constexpr u8 MAX17050_Temp = 0x08;
constexpr u8 MAX17050_VCell = 0x09;
constexpr u8 MAX17050_Current = 0x0A;
constexpr u8 MAX17050_AvgCurrent = 0x0B;
constexpr u8 MAX17050_FullCap = 0x10;
constexpr u8 MAX17050_TTE = 0x11;
constexpr u8 MAX17050_Cycles = 0x17;
constexpr u8 MAX17050_DesignCap = 0x18;

auto HoursMinutes(u64 seconds) -> std::string {
    return Fmt("%lu h %02lu min", seconds / 3600, (seconds / 60) % 60);
}

} // namespace

auto CollectPower() -> Group {
    GroupBuilder g{"Power"};
    if (R_FAILED(psmInitialize())) {
        return g.group;
    }
    ON_SCOPE_EXIT(psmExit());

    PsmChargerType charger{};
    if (R_SUCCEEDED(psmGetChargerType(&charger))) {
        g.Line("Charger", ChargerName(charger));
    }
    bool b{};
    if (R_SUCCEEDED(psmIsBatteryChargingEnabled(&b))) {
        g.Line("Charging allowed", YesNo(b));
    }
    if (R_SUCCEEDED(psmIsEnoughPowerSupplied(&b))) {
        g.Line("Enough power supplied", YesNo(b));
    }

    if (!hosversionAtLeast(17, 0, 0)) {
        return g.group;
    }
    PsmBatteryChargeInfoFields f{};
    if (R_FAILED(psmGetBatteryChargeInfoFields(&f))) {
        return g.group;
    }
    g.Line("Charging now", YesNo(f.battery_charging));
    g.Line("Fast charging", YesNo(f.fast_battery_charging));
    g.Line("USB charger type", UsbChargerName(f.usb_charger_type));
    g.Line("USB power role", PowerRoleName(f.usb_power_role));
    g.Line("Power source voltage limit", Fmt("%u mV", f.charger_input_voltage_limit));
    g.Line("Power source current limit", Fmt("%u mA", f.charger_input_current_limit));
    g.Line("Input current limit", Fmt("%u mA", f.input_current_limit));
    g.Line("Fast charge current limit", Fmt("%u mA", f.fast_charge_current_limit));
    g.Line("Charge voltage limit", Fmt("%u mV", f.charge_voltage_limit));
    g.Line("Accessory (OTG) current limit", Fmt("%u mA", f.boost_mode_current_limit));
    g.Line("HiZ mode", f.hi_z_mode ? "On"_i18n : "Off"_i18n);
    g.Line("Controller power supply", YesNo(f.controller_power_supply));
    g.Line("OTG requested", YesNo(f.otg_request));
    g.Line("Power delivery state", Vdd50Name(f.vdd50_state));
    return g.group;
}

auto CollectBattery() -> Group {
    GroupBuilder g{"Battery"};
    if (R_SUCCEEDED(psmInitialize())) {
        ON_SCOPE_EXIT(psmExit());
        u32 percent{};
        if (R_SUCCEEDED(psmGetBatteryChargePercentage(&percent))) {
            g.Line("Charge", Fmt("%u%%", percent));
        }
        double d{};
        if (R_SUCCEEDED(psmGetRawBatteryChargePercentage(&d))) {
            g.Line("Charge (raw)", Fmt("%.2f%%", d));
        }
        if (R_SUCCEEDED(psmGetBatteryAgePercentage(&d))) {
            g.Line("Battery health", Fmt("%.1f%%", d));
        }
        if (hosversionAtLeast(17, 0, 0)) {
            PsmBatteryChargeInfoFields f{};
            if (R_SUCCEEDED(psmGetBatteryChargeInfoFields(&f))) {
                g.Line("Temperature", Fmt("%.1f °C", f.temperature_celcius / 1000.0));
                g.Line("Voltage", Fmt("%u mV", f.battery_charge_milli_voltage));
            }
        }
    }

    FuelGauge gauge;
    if (!gauge.open) {
        return g.group;
    }
    if (const auto v = gauge.Capacity(MAX17050_DesignCap)) {
        g.Line("Design capacity", Fmt("%.0f mAh", *v));
    }
    if (const auto v = gauge.Capacity(MAX17050_FullCap)) {
        g.Line("Full capacity now", Fmt("%.0f mAh", *v));
    }
    if (const auto v = gauge.Capacity(MAX17050_RepCap)) {
        g.Line("Remaining capacity", Fmt("%.0f mAh", *v));
    }
    if (const auto v = gauge.Read(MAX17050_Cycles)) {
        g.Line("Charge cycles", Fmt("%.2f", *v / 100.0));
    }
    if (const auto v = gauge.Read(MAX17050_Age)) {
        g.Line("Age (fuel gauge)", Fmt("%.1f%%", *v / 256.0));
    }
    if (const auto v = gauge.Current(MAX17050_Current)) {
        g.Line("Current", Fmt("%.0f mA", *v));
    }
    if (const auto v = gauge.Current(MAX17050_AvgCurrent)) {
        g.Line("Average current", Fmt("%.0f mA", *v));
    }
    if (const auto v = gauge.Read(MAX17050_VCell)) {
        g.Line("Cell voltage", Fmt("%.0f mV", (*v >> 3) * 0.625));
    }
    if (const auto v = gauge.Read(MAX17050_Temp)) {
        g.Line("Cell temperature", Fmt("%.1f °C", static_cast<s16>(*v) / 256.0));
    }
    if (const auto v = gauge.Read(MAX17050_TTE); v && *v != 0xFFFF) {
        g.Line("Time to empty", HoursMinutes(static_cast<u64>(*v * 5.625)));
    }
    return g.group;
}

} // namespace sphaira::system_info
