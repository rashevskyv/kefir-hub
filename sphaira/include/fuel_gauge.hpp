#pragma once

// MAX17050 fuel gauge registers, read over the i2c service. Read only: a read sends the
// register number, then receives two bytes. Scaling per the datasheet with the 10 mOhm
// sense resistor every Switch model uses.
// ponytail: fixed 10 mOhm Rsense; make it a knob only if a board ever differs.

#include <switch.h>
#include <optional>

namespace sphaira {

constexpr u8 MAX17050_Status = 0x00;
constexpr u8 MAX17050_RepCap = 0x05;
constexpr u8 MAX17050_RepSOC = 0x06;
constexpr u8 MAX17050_Age = 0x07;
constexpr u8 MAX17050_Temp = 0x08;
constexpr u8 MAX17050_VCell = 0x09;
constexpr u8 MAX17050_Current = 0x0A;
constexpr u8 MAX17050_AvgCurrent = 0x0B;
constexpr u8 MAX17050_FullCap = 0x10;
constexpr u8 MAX17050_TTE = 0x11;
constexpr u8 MAX17050_QRTable00 = 0x12;
constexpr u8 MAX17050_Cycles = 0x17;
constexpr u8 MAX17050_DesignCap = 0x18;
constexpr u8 MAX17050_QRTable10 = 0x22;
constexpr u8 MAX17050_FullCapNom = 0x23;
constexpr u8 MAX17050_MiscCFG = 0x2B;
constexpr u8 MAX17050_QRTable20 = 0x32;
constexpr u8 MAX17050_IAvgEmpty = 0x36;
constexpr u8 MAX17050_RCOMP0 = 0x38;
constexpr u8 MAX17050_TempCo = 0x39;
constexpr u8 MAX17050_QRTable30 = 0x42;
constexpr u8 MAX17050_VFOCV = 0xFB;
constexpr u8 MAX17050_VFSOC = 0xFF;

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
    FuelGauge(const FuelGauge&) = delete;
    FuelGauge& operator=(const FuelGauge&) = delete;

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

} // namespace sphaira
