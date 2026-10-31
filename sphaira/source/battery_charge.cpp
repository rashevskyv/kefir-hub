#include "battery_charge.hpp"
#include "fuel_gauge.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "ui/progress_box.hpp"

#include <atomic>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

namespace sphaira::battery::charge {

namespace {

constexpr const char* FLAG = "/config/kefir/battery/charging_off.flag";
constexpr const char* RUN = "/config/kefir/battery/charge_test.run";
constexpr const char* OUT = "/config/kefir/battery/charge_test.txt";
constexpr int TEST_CORES = 3;
constexpr int TEST_PRIO = 59;

std::atomic<bool> g_burn{false};
std::atomic<bool> g_burn_stop{false};

// the test's own load: the drain page's threads belong to that page
void Burn(void*) {
    volatile double x = 1.0001;
    while (!g_burn_stop) {
        if (!g_burn) {
            svcSleepThread(50'000'000ULL);
            continue;
        }
        for (int i = 0; i < 200000; i++) {
            x = std::sqrt(x * 1.0000001 + 0.5);
        }
    }
}

// one line: what the charger and the battery do now (the sign of the battery current: minus = the battery gives)
auto Sample(const char* phase, int t) -> std::string {
    FuelGauge g;
    const auto v = g.open ? g.Read(MAX17050_VCell) : std::nullopt;
    const auto now = g.open ? g.Current(MAX17050_Current) : std::nullopt;
    const auto avg = g.open ? g.Current(MAX17050_AvgCurrent) : std::nullopt;
    PsmChargerType charger{};
    psmGetChargerType(&charger);
    u32 pct{};
    psmGetBatteryChargePercentage(&pct);
    char b[200];
    std::snprintf(b, sizeof(b), "%-22s t=%3d charger=%d charging_on=%d vcell=%d mV current=%d mA avg=%d mA psm=%u%%\n", phase, t,
                  static_cast<int>(charger), IsOn().value_or(true) ? 1 : 0, v ? static_cast<int>((*v >> 3) * 0.625) : 0,
                  now ? static_cast<int>(*now) : 0, avg ? static_cast<int>(*avg) : 0, pct);
    return b;
}

} // namespace

auto IsOn() -> std::optional<bool> {
    bool on{};
    if (R_FAILED(psmIsBatteryChargingEnabled(&on))) {
        return std::nullopt;
    }
    return on;
}

auto Set(bool on) -> Result {
    const Result rc = on ? psmEnableBatteryCharging() : psmDisableBatteryCharging();
    log_write("[Charge] charging %s: rc 0x%X\n", on ? "on" : "off", rc);
    if (R_SUCCEEDED(rc)) {
        fs::FsNativeSd sd;
        if (on) {
            sd.DeleteFile(FLAG);
        } else {
            sd.CreateDirectoryRecursively("/config/kefir/battery");
            const char note[] = "charging is off: the Hub switches it on again at its start\n";
            sd.write_entire_file(FLAG, std::vector<u8>(note, note + sizeof(note) - 1));
        }
    }
    return rc;
}

void RestoreIfLeftOff() {
    if (fs::FsNativeSd().FileExists(FLAG)) {
        log_write("[Charge] charging was left off: switching it on\n");
        Set(true);
    }
}

void RunTestIfAsked() {
    fs::FsNativeSd sd;
    if (!sd.FileExists(RUN)) {
        return;
    }
    sd.DeleteFile(RUN);
    App::Push<ui::ProgressBox>(0, "Charging test", "Battery", [](auto pbox) -> Result {
        std::string out = "Charging test: the battery current with the charger in. minus = the battery gives current.\n";
        Thread threads[TEST_CORES]{};
        bool started[TEST_CORES]{};
        // whatever happens, charging goes on again and the load stops
        ON_SCOPE_EXIT(
            g_burn = false;
            g_burn_stop = true;
            for (int i = 0; i < TEST_CORES; i++) {
                if (started[i]) {
                    threadWaitForExit(&threads[i]);
                    threadClose(&threads[i]);
                }
            }
            Set(true);
        );
        const auto samples = [&](const char* phase, int secs) {
            pbox->NewTransfer(phase);
            for (int t = 0; t < secs; t++) {
                out += Sample(phase, t);
                svcSleepThread(1'000'000'000ULL);
            }
        };
        samples("baseline", 10);
        const Result off = Set(false);
        char b[96];
        std::snprintf(b, sizeof(b), "psmDisableBatteryCharging rc 0x%X, the service says charging_on=%d\n", off, IsOn().value_or(true) ? 1 : 0);
        out += b;
        samples("charging off", 20);
        g_burn_stop = false;
        for (int i = 0; i < TEST_CORES; i++) {
            started[i] = R_SUCCEEDED(threadCreate(&threads[i], Burn, nullptr, nullptr, 0x4000, TEST_PRIO, i)) && R_SUCCEEDED(threadStart(&threads[i]));
        }
        g_burn = true;
        samples("charging off + load", 40);
        g_burn = false;
        const Result on = Set(true);
        std::snprintf(b, sizeof(b), "psmEnableBatteryCharging rc 0x%X, the service says charging_on=%d\n", on, IsOn().value_or(false) ? 1 : 0);
        out += b;
        samples("charging on again", 15);
        fs::FsNativeSd().write_entire_file(OUT, std::vector<u8>(out.begin(), out.end()));
        log_write("[Charge] the test is written to %s\n", OUT);
        R_SUCCEED();
    });
}

} // namespace sphaira::battery::charge
