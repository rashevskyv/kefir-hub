// Host test for sphaira/include/battery_gauge.hpp

#include "battery_gauge.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>

using namespace sphaira::battery;

namespace {

Params Sample() {
    // T26dc8 (v0, other vendor) factory values from the Battery Desync Fix tables
    return Params{{0x0053, 0x1C22, 0x2476, 0x2476, 0x1D2A, 0x5786, 0x3184, 0x1E00, 0x1602}};
}

// the older checks: the other MMC's copy is known and equal to the chip
Input Known(Input in) {
    if (!in.other) {
        in.other = in.chip;
    }
    return in;
}

void PacksLikeBattCal() {
    const auto cal = Pack(Sample(), 0xBEEF, 12345);
    unsigned char bytes[24];
    std::memcpy(bytes, &cal, sizeof(bytes));
    // little endian u16 x9, pad, u32 cycles
    assert(bytes[0] == 0x53 && bytes[1] == 0x00);
    assert(bytes[4] == 0x76 && bytes[5] == 0x24);
    assert(bytes[16] == 0x02 && bytes[17] == 0x16);
    assert(bytes[18] == 0xEF && bytes[19] == 0xBE);
    assert(bytes[20] == 0x39 && bytes[21] == 0x30 && bytes[22] == 0 && bytes[23] == 0);
}

void HealthyMmcHasNoSign() {
    Input in{Sample(), Sample(), 0x0000, 0x0000, 0x2476, 0x2300, 0, 0, 1, true, std::nullopt, -1, -1, 0};
    const auto r = Diagnose(Known(in));
    assert(r.size() == 1 && r[0].finding == Finding::NoSign);
}

void DifferentCopyIsNamed() {
    auto hos = Sample();
    hos.v[2] = 0x2000; // FullCap
    hos.v[8] = 0x1600; // QRTable30
    Input in{Sample(), hos, 0, 0, 0x2476, 0x2300, 0, 0, 1, true, std::nullopt, -1, -1, 0};
    const auto r = Diagnose(Known(in));
    assert(r.size() == 1 && r[0].finding == Finding::CopyDiffers);
    assert(r[0].detail == "FullCap, QRTable30");
}

void RestoreFlagAndPorComeFirst() {
    Input in{Sample(), Sample(), 0x8000 | 0x0810, 0x0002, 0x2476, 0x2300, 0, 0, 1, true, std::nullopt, -1, -1, 0};
    const auto r = Diagnose(Known(in));
    assert(r.size() == 2 && r[0].finding == Finding::RestorePending && r[1].finding == Finding::PowerOnReset);
}

void InformationalFindingsKeepNoSign() {
    // Lite: version 2, reliability off, worn battery; no desync sign
    Input in{Sample(), Sample(), 0, 0, 0x2476, 0x2300, 1, 2, 0, true, std::nullopt, -1, -1, 0};
    const auto r = Diagnose(Known(in));
    assert(r.size() == 3);
    assert(r[0].finding == Finding::NoSign);
    assert(r[1].finding == Finding::VersionMismatch);
    assert(r[2].finding == Finding::LiteNoCycles);
}

void UnknownVersionsAreNotCompared() {
    Input in{Sample(), Sample(), 0, 0, 0, 0, -1, 2, -1, true, std::nullopt, -1, -1, 0};
    const auto r = Diagnose(Known(in));
    assert(r.size() == 1 && r[0].finding == Finding::NoSign);
}

void UnreadCopyIsNotCompared() {
    auto hos = Sample();
    hos.v[0] = 0;
    Input in{Sample(), hos, 0, 0, 0x2476, 0x2300, 0, 0, 1, true, std::nullopt, -1, -1, 0};
    in.hos_read = false;
    const auto r = Diagnose(Known(in));
    assert(r.size() == 1 && r[0].finding == Finding::CopyUnknown); // no "no sign" without the copy
}

void CollapsedCapacityIsDesync() {
    // a console from the field: FullCap 36 mAh against 3600 mAh design, and this MMC's copy is the same
    auto p = Sample();
    p.v[2] = 0x0048;
    Input in{p, p, 0, 0, 0x1C20, 0x0048, 0, 0, 1, true, std::nullopt, -1, -1, 0};
    const auto r = Diagnose(Known(in));
    assert(r.size() == 1 && r[0].finding == Finding::BrokenModel && r[0].detail == "1 copy");
    in.hos.v[2] = 0x1C00; // the copy still has a sane FullCap: it differs and is not "copy"
    const auto r2 = Diagnose(Known(in));
    assert(r2[0].finding == Finding::BrokenModel && r2[0].detail == "1" && r2[1].finding == Finding::CopyDiffers);
}

void OtherMmcAndSocGap() {
    Input in{Sample(), Sample(), 0, 0, 0x2476, 0x2300, 0, 0, 1, true, std::nullopt, -1, -1, 0};
    in.rep_soc = 84;
    in.vf_soc = 73;
    in.avg_current_ma = 1070; // charging: the gap is only a hint
    auto r = Diagnose(in);
    assert(r[0].finding == Finding::NoSign);
    assert(r[1].finding == Finding::SocGapLoaded && r[1].detail == "84 73");
    assert(r[2].finding == Finding::OtherUnknown);

    in.avg_current_ma = -638; // on battery in normal use (.141): a sign
    auto other = Sample();
    other.v[2] = 0x2000;
    in.other = other;
    r = Diagnose(in);
    assert(r.size() == 2 && r[0].finding == Finding::OtherDiffers && r[0].detail == "FullCap");
    assert(r[1].finding == Finding::SocGap);

    in.other = Sample();
    in.vf_soc = 80;
    r = Diagnose(in);
    assert(r.size() == 1 && r[0].finding == Finding::NoSign); // everything checked, nothing found
}

void CopyFileLayout() {
    CopyFile f{Pack(Sample(), 0, 37196), 1760000000};
    assert(ParamsOf(f.cal) == Sample() && f.cal.cycles == 37196);
}

void NinetyPercentRule() {
    // .141: 3032 of 3600 mAh = 84 %: desync, the copy is as low
    auto p = Sample();
    p.v[2] = 0x17B0;
    Input in{p, p, 0, 0, 0x1C20, 0x17B0, 2, 2, 1, true, std::nullopt, -1, -1, 0};
    auto r = Diagnose(Known(in));
    assert(r[0].finding == Finding::BrokenModel && r[0].detail == "84 copy");
    assert(CapacityLow(0x194F, 0x1C20));   // 6479 of 7200: just under 90 %
    assert(!CapacityLow(0x1950, 0x1C20));  // 6480: exactly 90 %
}

void CyclesApartAcrossMmcs() {
    Input in{Sample(), Sample(), 0, 0, 0x2476, 0x2300, 2, 2, 1, true, Sample(), -1, -1, 0};
    in.hos_cycles = 37196;
    in.other_cycles = 29497;
    const auto r = Diagnose(in);
    assert(r.size() == 1 && r[0].finding == Finding::CyclesDiffer && r[0].detail == "371.96 294.97");
    in.other_cycles = 37150; // under one cycle apart: fine
    assert(Diagnose(in)[0].finding == Finding::NoSign);
}

} // namespace

int main() {
    PacksLikeBattCal();
    HealthyMmcHasNoSign();
    DifferentCopyIsNamed();
    RestoreFlagAndPorComeFirst();
    InformationalFindingsKeepNoSign();
    UnknownVersionsAreNotCompared();
    UnreadCopyIsNotCompared();
    CollapsedCapacityIsDesync();
    OtherMmcAndSocGap();
    CopyFileLayout();
    NinetyPercentRule();
    CyclesApartAcrossMmcs();
    // align the charge to the voltage (.141: Hekate 3193 mV, chip 32 %, the voltage says near empty)
    {
        const auto plan = PlanAlign({static_cast<std::uint16_t>(3 << 8), static_cast<std::uint16_t>(32 << 8), 0x1C20, 0x2C78, false});
        assert(plan.ok && plan.soc == (3 << 8));
        assert(plan.mix == (3 * 256 * 0x2C78 / 0x6400));
        assert(plan.rep == plan.mix * 0x1C20 / 0x2C78);
        assert(!PlanAlign({static_cast<std::uint16_t>(3 << 8), static_cast<std::uint16_t>(32 << 8), 0x1C20, 0x2C78, true}).ok);  // charger in
        assert(!PlanAlign({static_cast<std::uint16_t>(40 << 8), static_cast<std::uint16_t>(80 << 8), 0x1C20, 0x2C78, false}).ok); // the voltage says 40 %
        assert(!PlanAlign({static_cast<std::uint16_t>(8 << 8), static_cast<std::uint16_t>(15 << 8), 0x1C20, 0x2C78, false}).ok);  // only 7 points apart
        assert(!PlanAlign({0, 0, 0, 0, false}).ok);
    }

    std::puts("test_battery_gauge: ok");
}
