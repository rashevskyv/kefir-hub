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

// the counters, in the order the drivers use (Linux max17042, hekate): MIX, then REP and RepSOC
auto WriteCounters(FuelGauge& g, const zero::Counters& c, std::string& failed) -> Result {
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
// `qh_last`: the QH counter + 1 the drain page noted at its last tick on battery (Marker::qh_last), 0 = none. The charge that came
// in since then is what the counters get; without a note the QH of the Hub's start stands in for it. A battery that is full by
// its voltage and current (zero::FullNow) is written as 100 %, whatever the difference says.
auto WriteZero(int mv, std::string& failed, int qh_last) -> Result {
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

    const auto vcell = g.Read(MAX17050_VCell);
    const auto avg = g.Current(MAX17050_AvgCurrent);
    const int now_mv = vcell ? static_cast<int>((*vcell >> 3) * 0.625) : 0;
    const int now_ma = avg ? static_cast<int>(*avg) : 0;
    const bool top = vcell && avg && zero::FullNow(now_mv, now_ma);
    const u16 base = qh_last > 0 ? static_cast<u16>(qh_last - 1) : g_qh_at_start.value_or(*qh);

    const auto ve = zero::VEmptyWith(*old_ve, mv);
    const auto c = zero::PlanCounters({*full, *nom, base, *qh, top});
    R_UNLESS(c.ok, Result_ArchiveRead);
    log_write("[Zero] cut-off %d mV: V_empty %04X -> %04X; now %d mV %d mA (%s); QH from %s %04X now %04X -> MIX %04X REP %04X SOC %u/256 %%\n",
              mv, *old_ve, ve, now_mv, now_ma, top ? "full" : "not full", qh_last > 0 ? "the last drain tick" : "the Hub's start", base, *qh,
              c.mix, c.rep, c.soc);

    if (!g.WriteVerify(MAX17050_VEmpty, ve)) {
        failed = "The gauge did not take the empty voltage."_i18n;
        R_THROW(Result_ArchiveRead);
    }
    R_TRY(WriteCounters(g, c, failed));
    R_SUCCEED();
}

// The charge page found the battery full (voltage and current) while the chip shows far less, as after a zero written from a
// wrong baseline: the counters say 100 %, V_empty stays. A backup is made first.
auto WriteFull(std::string& failed) -> Result {
    auto s = detail::Read();
    R_UNLESS(s.gauge, Result_ArchiveRead);
    std::string name;
    R_TRY(detail::Backup(s, name));
    FuelGauge g;
    R_UNLESS(g.open, Result_ArchiveRead);
    const auto full = g.Read(MAX17050_FullCap);
    const auto nom = g.Read(MAX17050_FullCapNom);
    R_UNLESS(full && nom, Result_ArchiveRead);
    const auto c = zero::PlanCounters({*full, *nom, 0, 0, true});
    R_UNLESS(c.ok, Result_ArchiveRead);
    log_write("[Zero] the battery is full but the chip shows less: counters to 100 %% (MIX %04X REP %04X)\n", c.mix, c.rep);
    return WriteCounters(g, c, failed);
}

// The charge page found a full battery (the constant-voltage tail). The charge that came in since the cut-off is the capacity: it
// goes into FullCap and FullCapNom, the counters to 100 %, and into this system's copy. `qh_cut`: Marker::qh_last (QH + 1 at the
// cut-off). A backup is made first. `cap` gets the new FullCap (0.5 mAh units).
auto WriteCapacity(int qh_cut, std::string& failed, std::uint16_t& cap) -> Result {
    R_UNLESS(qh_cut > 0, Result_ArchiveRead);
    auto s = detail::Read();
    R_UNLESS(s.gauge, Result_ArchiveRead);
    std::string name;
    R_TRY(detail::Backup(s, name));
    FuelGauge g;
    R_UNLESS(g.open, Result_ArchiveRead);
    const auto qh = g.Read(MAX17050_QH);
    const auto design = g.Read(MAX17050_DesignCap);
    const auto old_full = g.Read(MAX17050_FullCap);
    const auto old_nom = g.Read(MAX17050_FullCapNom);
    R_UNLESS(qh && design && old_full && old_nom, Result_ArchiveRead);
    const auto status = g.Read(MAX17050_Status);
    if (!status || (*status & zero::STATUS_POR)) {
        failed = "The gauge restarted since the cut-off: the capacity could not be measured."_i18n;
        R_THROW(Result_ArchiveRead);
    }
    const auto base = static_cast<u16>(qh_cut - 1);
    const auto m = zero::MeasureCapacity(base, *qh, *design);
    log_write("[Cap] QH from %04X to %04X = %d (0.5 mAh), design %u, chip's FullCap %u FullCapNom %u: %s\n", base, *qh,
              static_cast<u16>(*qh - base), *design, *old_full, *old_nom, m.ok ? "taken" : "outside 50-125 % of the design, not taken");
    if (!m.ok) {
        failed = "The charge measured since the cut-off does not fit this battery: the gauge keeps its own capacity."_i18n;
        R_THROW(Result_ArchiveRead);
    }
    if (!g.WriteVerify(MAX17050_FullCap, m.raw) || !g.WriteVerify(MAX17050_FullCapNom, m.raw)) {
        failed = "The gauge did not take the measured capacity."_i18n;
        R_THROW(Result_ArchiveRead);
    }
    const auto c = zero::PlanCounters({m.raw, m.raw, 0, 0, true});
    R_UNLESS(c.ok, Result_ArchiveRead);
    R_TRY(WriteCounters(g, c, failed));
    R_TRY(detail::SaveCopyOf(g)); // HOS keeps its own copy of the learned values: it must not bring the old ones back
    cap = m.raw;
    R_SUCCEED();
}

} // namespace sphaira::battery::zero_write
