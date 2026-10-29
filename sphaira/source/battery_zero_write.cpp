// Battery calibration, the zero of cycle 1 (battery_zero.hpp): after the drain cut the console off, the last voltage of the
// drain log is written into the gauge as the empty point: V_empty's VE, and the charge counters at "what came in since the
// switch-on". The QH counter is read once at the Hub's start for that.
#include "battery_page.hpp"
#include "battery_internal.hpp"
#include "battery_zero.hpp"
#include "battery_calib_internal.hpp"
#include "fuel_gauge.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "i18n.hpp"

#include <optional>
#include <string>
#include <vector>

namespace sphaira::battery::zero_write {

namespace {

std::optional<u16> g_qh_at_start; // 0x4D when the Hub started this boot

} // namespace

void ReadQhAtStart() {
    if (g_qh_at_start) {
        return;
    }
    FuelGauge g;
    if (g.open) {
        g_qh_at_start = g.Read(MAX17050_QH);
    }
}

// backs the chip up, writes VE and the counters, verifies by reading. `failed` says why when it did not.
auto WriteZero(int mv, std::string& failed) -> Result {
    if (!zero::InRange(mv)) {
        failed = "The cut-off voltage is outside 3.00 - 3.50 V: not a zero."_i18n;
        R_THROW(Result_ArchiveRead);
    }
    auto s = detail::Read();
    R_UNLESS(s.gauge, Result_ArchiveRead);
    std::string name;
    R_TRY(detail::Backup(s, name));

    FuelGauge g;
    R_UNLESS(g.open, Result_ArchiveRead);
    const auto old_ve = g.Read(MAX17050_VEmpty);
    const auto full = g.Read(MAX17050_FullCap);
    const auto nom = g.Read(MAX17050_FullCapNom);
    const auto qh = g.Read(MAX17050_QH);
    R_UNLESS(old_ve && full && nom && qh, Result_ArchiveRead);

    const auto ve = zero::VEmptyWith(*old_ve, mv);
    const auto c = zero::PlanCounters({*full, *nom, g_qh_at_start.value_or(*qh), *qh});
    R_UNLESS(c.ok, Result_ArchiveRead);
    log_write("[Zero] cut-off %d mV: V_empty %04X -> %04X; QH at start %04X now %04X -> MIX %04X REP %04X SOC %u/256 %%\n",
              mv, *old_ve, ve, g_qh_at_start.value_or(*qh), *qh, c.mix, c.rep, c.soc);

    if (!g.WriteVerify(MAX17050_VEmpty, ve)) {
        failed = "The gauge did not take the empty voltage."_i18n;
        R_THROW(Result_ArchiveRead);
    }
    // the counters, in the order the drivers use (Linux max17042, hekate): MIX, then REP and RepSOC
    const bool ok_mix = g.WriteVerify(MAX17050_MixCap, c.mix);
    svcSleepThread(350'000'000);
    const bool ok_rep = g.Write(MAX17050_RepCap, c.rep);
    const bool ok_soc = g.Write(MAX17050_RepSOC, c.soc);
    const auto after = g.Read(MAX17050_RepSOC);
    log_write("[Zero] written: MIX %d REP %d SOC %d; chip now %u/256 %%\n", ok_mix, ok_rep, ok_soc, after ? *after : 0);
    if (!ok_mix || !ok_rep || !ok_soc) {
        failed = "The gauge did not take the charge counters."_i18n;
        R_THROW(Result_ArchiveRead);
    }
    R_SUCCEED();
}

} // namespace sphaira::battery::zero_write
