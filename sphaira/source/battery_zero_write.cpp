// Battery calibration, cycle 1 (battery_zero.hpp): the zero is the cut-off voltage in V_empty and the counters at 1 %; the full battery (the charger ended
// the charge) gets the factory capacity of this console's battery and the counters at 100 %. Nothing is computed from QH.
#include "battery_page.hpp"
#include "battery_internal.hpp"
#include "battery_init.hpp"
#include "battery_zero.hpp"
#include "battery_calib_internal.hpp"
#include "fuel_gauge.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "i18n.hpp"

#include <string>

namespace sphaira::battery::zero_write {

namespace {

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

constexpr unsigned NEAR_EMPTY_PCT = 10;

} // namespace

// backs the chip up, writes VE and (when the chip is not near empty) the counters at 1 %, verifies by reading. `failed` says why when it did not.
auto WriteZero(int mv, std::string& failed) -> Result {
    if (!zero::InRange(mv)) {
        failed = "The cut-off voltage is outside 3.00 - 3.40 V: not a zero."_i18n;
        R_THROW(Result_ArchiveRead);
    }
    auto s = detail::Read();
    R_UNLESS(s.gauge, Result_ArchiveRead);
    std::string name;
    R_TRY(detail::Backup(s, name));

    FuelGauge g;
    R_UNLESS(g.open, Result_ArchiveRead);
    const auto old_ve = g.Read(MAX17050_VEmpty);
    R_UNLESS(old_ve, Result_ArchiveRead);
    const auto ve = zero::VEmptyWith(*old_ve, mv);
    log_write("[Zero] cut-off %d mV: V_empty %04X -> %04X\n", mv, *old_ve, ve);
    if (!g.WriteVerify(MAX17050_VEmpty, ve)) {
        failed = "The gauge did not take the empty voltage."_i18n;
        R_THROW(Result_ArchiveRead);
    }
    // the cut-off is the empty battery: a chip far from it gets the counters at 1 % (not 0: HOS switches off at 0 %), else it
    // keeps the % it had (91 % at a 3.18 V cut-off, 2026-11-06) and the charge seems to start near full. A chip already near
    // empty (5 % after the sleep, owner 2026-11-06) keeps its own %: the charge goes on from it.
    if (const auto soc = g.Read(MAX17050_RepSOC); soc && static_cast<unsigned>(*soc >> 8) <= NEAR_EMPTY_PCT) {
        log_write("[Zero] the chip says %u %%: near empty, the counters stay\n", static_cast<unsigned>(*soc >> 8));
        R_SUCCEED();
    }
    const auto full = g.Read(MAX17050_FullCap);
    const auto nom = g.Read(MAX17050_FullCapNom);
    R_UNLESS(full && nom && *full && *nom, Result_ArchiveRead);
    return WriteCounters(g, zero::Counters{true, static_cast<std::uint16_t>(*nom / 100), static_cast<std::uint16_t>(*full / 100), 0x0100}, failed);
}

// The charger ended the charge: FullCap and FullCapNom = the factory capacity of the battery in PRODINFO (the table the reset
// wrote), the counters 100 %, and this system's copy. A backup is made first. `cap` gets the written FullCap (0.5 mAh units).
auto WriteFactoryFull(std::string& failed, std::uint16_t& cap) -> Result {
    auto s = detail::Read();
    R_UNLESS(s.gauge, Result_ArchiveRead);
    std::string name;
    R_TRY(detail::Backup(s, name));
    FuelGauge g;
    R_UNLESS(g.open, Result_ArchiveRead);
    const auto factory = ChooseTable(s.cal0_version, s.cal0_vendor)[2];
    const auto old_full = g.Read(MAX17050_FullCap);
    const auto old_nom = g.Read(MAX17050_FullCapNom);
    log_write("[Cap] full by the charger: FullCap %u FullCapNom %u -> factory %u (CAL0 version %d, vendor %c)\n", old_full.value_or(0),
              old_nom.value_or(0), factory, s.cal0_version, s.cal0_vendor ? s.cal0_vendor : '?');
    if (!g.WriteVerify(MAX17050_FullCap, factory) || !g.WriteVerify(MAX17050_FullCapNom, factory)) {
        failed = "The gauge did not take the factory capacity."_i18n;
        R_THROW(Result_ArchiveRead);
    }
    const auto c = zero::PlanCounters({factory, factory, 0, 0, true});
    R_UNLESS(c.ok, Result_ArchiveRead);
    R_TRY(WriteCounters(g, c, failed));
    R_TRY(detail::SaveCopyOf(g)); // HOS keeps its own copy of the learned values: it must not bring the old ones back
    cap = factory;
    R_SUCCEED();
}

} // namespace sphaira::battery::zero_write
