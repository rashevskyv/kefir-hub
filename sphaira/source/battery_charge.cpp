#include "battery_charge.hpp"
#include "fuel_gauge.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "ui/progress_box.hpp"

#include <atomic>
#include <cstdio>
#include <ctime>
#include <mutex>
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

// ---- the charger chip bq24193 (I2C 0x6B): can its input be cut while the charger stays in? --------------------------------------
// REG00 bit 7 EN_HIZ: the input is disconnected and the system lives on the battery. The chip's watchdog (REG05 bits 5-4) takes the
// settings back to the default when nobody writes for a while: a console that switched off charges again by itself.

namespace {

constexpr const char* HIZ_RUN = "/config/kefir/battery/hiz_test.run";
constexpr const char* HIZ_OUT = "/config/kefir/battery/hiz_test.txt";

struct Bq {
    I2cSession s{};
    bool open{};
    Bq() { open = R_SUCCEEDED(i2cInitialize()) && R_SUCCEEDED(i2cOpenSession(&s, I2cDevice_Bq24193)); }
    ~Bq() {
        if (open) {
            i2csessionClose(&s);
        }
        i2cExit();
    }
    Bq(const Bq&) = delete;
    Bq& operator=(const Bq&) = delete;
    auto Read(u8 reg) -> std::optional<u8> {
        if (!open) {
            return std::nullopt;
        }
        const u8 cmd[] = {0x40, 0x01, reg, 0xC1, 0x01}; // the same command list the gauge uses, one byte back
        u8 v{};
        for (int i = 0; i < 5; i++) {
            if (R_SUCCEEDED(i2csessionExecuteCommandList(&s, &v, sizeof(v), cmd, sizeof(cmd)))) {
                return v;
            }
            svcSleepThread(1'000'000);
        }
        return std::nullopt;
    }
    auto Write(u8 reg, u8 v) -> bool {
        const u8 buf[2] = {reg, v};
        return open && R_SUCCEEDED(i2csessionSendAuto(&s, buf, sizeof(buf), I2cTransactionOption_All));
    }
};

} // namespace

void RunHizTestIfAsked() {
    fs::FsNativeSd sd;
    if (!sd.FileExists(HIZ_RUN)) {
        return;
    }
    sd.DeleteFile(HIZ_RUN);
    App::Push<ui::ProgressBox>(0, "Charger input test", "Battery", [](auto pbox) -> Result {
        std::string out = "bq24193 input test: the battery current with the charger in. minus = the battery gives current.\n";
        Bq bq;
        Thread threads[TEST_CORES]{};
        bool started[TEST_CORES]{};
        std::optional<u8> reg00_before;
        // whatever happens: the input back as it was, the load stopped
        ON_SCOPE_EXIT(
            if (bq.open && reg00_before) {
                bq.Write(0x00, *reg00_before);
            }
            g_burn = false;
            g_burn_stop = true;
            for (int i = 0; i < TEST_CORES; i++) {
                if (started[i]) {
                    threadWaitForExit(&threads[i]);
                    threadClose(&threads[i]);
                }
            }
        );
        if (!bq.open) {
            out += "the bq24193 session did not open\n";
            fs::FsNativeSd().write_entire_file(HIZ_OUT, std::vector<u8>(out.begin(), out.end()));
            R_SUCCEED();
        }
        char b[160];
        out += "registers 00..0A:";
        for (u8 r = 0; r <= 0x0A; r++) {
            const auto v = bq.Read(r);
            std::snprintf(b, sizeof(b), v ? " %02X=%02X" : " %02X=??", r, v ? *v : 0);
            out += b;
        }
        out += "\n";
        reg00_before = bq.Read(0x00);
        const auto line = [&](const char* phase, int t) {
            const auto r00 = bq.Read(0x00);
            const auto r08 = bq.Read(0x08);
            std::snprintf(b, sizeof(b), " reg00=%02X reg08=%02X\n", r00 ? *r00 : 0xFF, r08 ? *r08 : 0xFF);
            auto s = Sample(phase, t);
            s.pop_back();
            out += s + b;
        };
        const auto samples = [&](const char* phase, int secs, bool keep_hiz) {
            pbox->NewTransfer(phase);
            for (int t = 0; t < secs; t++) {
                if (keep_hiz && reg00_before) {
                    bq.Write(0x00, static_cast<u8>(*reg00_before | 0x80)); // written again every second: nothing takes it back unseen
                }
                line(phase, t);
                svcSleepThread(1'000'000'000ULL);
            }
        };
        if (!reg00_before) {
            out += "REG00 was not readable: nothing is written\n";
        } else {
            samples("baseline", 8, false);
            g_burn_stop = false;
            for (int i = 0; i < TEST_CORES; i++) {
                started[i] = R_SUCCEEDED(threadCreate(&threads[i], Burn, nullptr, nullptr, 0x4000, TEST_PRIO, i)) && R_SUCCEEDED(threadStart(&threads[i]));
            }
            g_burn = true;
            samples("input cut + load", 30, true);
            g_burn = false;
            const bool back = bq.Write(0x00, *reg00_before);
            std::snprintf(b, sizeof(b), "REG00 back to %02X: %s\n", *reg00_before, back ? "written" : "NOT written");
            out += b;
            samples("input back", 12, false);
        }
        fs::FsNativeSd().write_entire_file(HIZ_OUT, std::vector<u8>(out.begin(), out.end()));
        log_write("[Charge] the input test is written to %s\n", HIZ_OUT);
        R_SUCCEED();
    });
}

// ---- the cut-off test -----------------------------------------------------------------------------------------------------

namespace {

constexpr const char* HIZ_FLAG = "/config/kefir/battery/hiz_on.flag";
constexpr const char* HIZ_CUTOFF_RUN = "/config/kefir/battery/hiz_cutoff.run";
constexpr const char* HIZ_CUTOFF_LOG = "sdmc:/config/kefir/battery/hiz_cutoff.txt";

std::atomic<bool> g_hiz{false};
std::mutex g_hiz_mu;                // the page's tick, the watchdog and the release come from different threads
std::atomic<u64> g_hiz_tick{0};     // the system tick of the last HizTick: the cut is a lease, it runs out when nobody renews it
bool g_hiz_dog_started = false;
constexpr u64 HIZ_LEASE_NS = 3'000'000'000ULL;
bool g_hiz_test = false; // the cut-off test writes hiz_cutoff.txt; the calibration's cut only logs what it must
std::FILE* g_hiz_log = nullptr;
int g_hiz_rewrites = 0;
u64 g_hiz_last_line = 0;

void HizLine(const char* what) {
    if (!g_hiz_log) {
        g_hiz_log = std::fopen(HIZ_CUTOFF_LOG, "ab");
    }
    if (g_hiz_log) {
        std::fputs(what, g_hiz_log);
        std::fflush(g_hiz_log);
        fsdevCommitDevice("sdmc"); // the last seconds before the switch-off must reach the card
    }
}

void HizReleaseLocked() {
    if (g_hiz_log) {
        std::fclose(g_hiz_log);
        g_hiz_log = nullptr;
    }
    g_hiz = false;
    g_hiz_test = false;
    Bq bq;
    if (const auto r00 = bq.Read(0x00); r00 && (*r00 & 0x80)) {
        const bool ok = bq.Write(0x00, static_cast<u8>(*r00 & 0x7F));
        log_write("[Charge] the charger input is back: REG00 %02X -> %02X, %s\n", *r00, *r00 & 0x7F, ok ? "written" : "NOT written");
    }
    fs::FsNativeSd().DeleteFile(HIZ_FLAG);
}

// The cut is only for the open drain page (owner, 2026-11-01): the page renews it every second. When the page is closed, covered by
// another one, or the Hub goes to the background, nobody renews it and this thread gives the input back within seconds.
void HizWatchdog(void*) {
    for (;;) {
        svcSleepThread(500'000'000ULL);
        std::scoped_lock lock{g_hiz_mu};
        if (g_hiz && armTicksToNs(armGetSystemTick() - g_hiz_tick) > HIZ_LEASE_NS) {
            log_write("[Charge] nobody keeps the cut (the drain page is closed or covered): the charger input is given back\n");
            HizReleaseLocked();
        }
    }
}

void EnsureWatchdog() {
    static Thread dog{};
    if (g_hiz_dog_started) {
        return;
    }
    if (R_SUCCEEDED(threadCreate(&dog, HizWatchdog, nullptr, nullptr, 0x8000, 0x2C, -2)) && R_SUCCEEDED(threadStart(&dog))) {
        g_hiz_dog_started = true;
    } else {
        log_write("[Charge] the watchdog thread did not start: the cut ends with the page only\n");
    }
}

} // namespace

auto HizActive() -> bool {
    return g_hiz;
}

auto StartCutoffTestIfAsked() -> bool {
    fs::FsNativeSd sd;
    if (!sd.FileExists(HIZ_CUTOFF_RUN)) {
        return false;
    }
    sd.DeleteFile(HIZ_CUTOFF_RUN);
    const char note[] = "the charger input is cut for the cut-off test: the Hub puts it back at its start\n";
    sd.write_entire_file(HIZ_FLAG, std::vector<u8>(note, note + sizeof(note) - 1));
    std::scoped_lock lock{g_hiz_mu};
    g_hiz = true;
    g_hiz_tick = armGetSystemTick();
    EnsureWatchdog();
    g_hiz_test = true;
    g_hiz_rewrites = 0;
    g_hiz_last_line = 0;
    char b[160];
    std::snprintf(b, sizeof(b), "cut-off test started, unix %lld: the charger input is cut (REG00 bit 7), full load until the console switches off\n",
                  static_cast<long long>(std::time(nullptr)));
    HizLine(b);
    log_write("[Charge] the cut-off test starts\n");
    return true;
}

void HizSet(bool on) {
    std::scoped_lock lock{g_hiz_mu};
    if (on == g_hiz) {
        return;
    }
    if (!on) {
        log_write("[Charge] the charger input is given back (the charger was unplugged or the drain ended)\n");
        HizReleaseLocked();
        return;
    }
    fs::FsNativeSd sd;
    sd.CreateDirectoryRecursively("/config/kefir/battery");
    const char note[] = "the charger input is cut for a drain: the Hub puts it back at its start\n";
    sd.write_entire_file(HIZ_FLAG, std::vector<u8>(note, note + sizeof(note) - 1));
    g_hiz = true;
    g_hiz_tick = armGetSystemTick();
    EnsureWatchdog();
    g_hiz_rewrites = 0;
    log_write("[Charge] the charger is in: its input is cut, the console drains on the battery\n");
}

void HizTick(int mv, int avg_ma, int pct) {
    std::scoped_lock lock{g_hiz_mu};
    if (!g_hiz) {
        return;
    }
    g_hiz_tick = armGetSystemTick(); // the lease is renewed
    Bq bq;
    const auto r00 = bq.Read(0x00);
    const auto r08 = bq.Read(0x08);
    bool took = false;
    // something took the cut back (a charger plugged in again resets the chip), or the battery is charged although the bit
    // looks set: the cut is written again, and counted
    const bool charging = avg_ma > 100;
    if (r00 && (!(*r00 & 0x80) || charging)) {
        bq.Write(0x00, static_cast<u8>(*r00 | 0x80));
        g_hiz_rewrites++;
        took = true;
        log_write("[Charge] the cut was taken back (REG00 %02X, battery %d mA): set again, %d times\n", *r00, avg_ma, g_hiz_rewrites);
    }
    const u64 now = static_cast<u64>(std::time(nullptr));
    if (g_hiz_test && (took || now >= g_hiz_last_line + 5)) {
        g_hiz_last_line = now;
        char b[200];
        std::snprintf(b, sizeof(b), "%llu vcell=%d mV avg=%d mA psm=%d%% reg00=%02X reg08=%02X rewrites=%d%s\n", static_cast<unsigned long long>(now), mv,
                      avg_ma, pct, r00 ? *r00 : 0xFF, r08 ? *r08 : 0xFF, g_hiz_rewrites, took ? " (set again)" : "");
        HizLine(b);
    }
}

void HizRelease() {
    std::scoped_lock lock{g_hiz_mu};
    HizReleaseLocked();
}

void HizStartCheck() {
    fs::FsNativeSd sd;
    if (!sd.FileExists(HIZ_FLAG)) {
        return;
    }
    // the cut-off test ran before: this is the first start after it. What the chip holds now says whether the cut survived.
    Bq bq;
    const auto r00 = bq.Read(0x00);
    const auto r08 = bq.Read(0x08);
    const auto r05 = bq.Read(0x05);
    char b[200];
    std::snprintf(b, sizeof(b), "%lld the Hub started again: REG00=%02X REG05=%02X REG08=%02X (REG00 bit 7 set = the cut survived the switch-off)\n",
                  static_cast<long long>(std::time(nullptr)), r00 ? *r00 : 0xFF, r05 ? *r05 : 0xFF, r08 ? *r08 : 0xFF);
    HizLine(b);
    log_write("[Charge] %s", b);
    HizRelease();
}

} // namespace sphaira::battery::charge
