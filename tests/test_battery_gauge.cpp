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
    Input in{Sample(), Sample(), 0, 0, 0x2476, 0x1600, 1, 2, 0, true, std::nullopt, -1, -1, 0};
    const auto r = Diagnose(Known(in));
    assert(r.size() == 4);
    assert(r[0].finding == Finding::NoSign);
    assert(r[1].finding == Finding::VersionMismatch);
    assert(r[2].finding == Finding::LiteNoCycles);
    assert(r[3].finding == Finding::LowCapacity && r[3].detail == "60");
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
    std::puts("test_battery_gauge: ok");
}
