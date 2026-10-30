// host test for the BIOS download plan (bios_plan.hpp): destinations per emulator,
// the core filter and the URL encoding. Run by tests/run.sh.
#include "bios_plan.hpp"
#include <cassert>
#include <cstdio>

using namespace sphaira::bios;

static Manifest Sample() {
    Manifest m;
    m.source = "https://raw.githubusercontent.com/Abdess/retrobios/main/";
    m.systems.push_back({"sony-playstation", "Sony - PlayStation", "psx", {
        {"scph5501.bin", "bios/Sony/PlayStation/scph5501.bin", "0555c6fae8906f3f09baf5988f00e55f88e9f30b", 524288},
    }});
    m.systems.push_back({"nintendo-ds", "Nintendo - DS", "nds", {
        {"bios7.bin", "bios/Nintendo/DS/bios7.bin", "24f67bdea115a2c847c8813a262502ee1607b7df", 16384},
        {"firmware.bin", "bios/Nintendo/DS/firmware.bin", "a1b2", 262144},
    }});
    m.systems.push_back({"arcade", "Arcade", "", {
        {"fbneo/neogeo.zip", "bios/Arcade/FinalBurn Neo/neogeo.zip", "c0de", 1000},
    }});
    m.cores = {
        {"pcsx_rearmed", {"sony-playstation"}},
        {"melonds", {"nintendo-ds", "nintendo-dsi"}},
        {"fbneo", {"arcade", "nintendo-ds"}},
    };
    return m;
}

int main() {
    const auto m = Sample();

    // core file names
    assert(CoreName("pcsx_rearmed_libretro_libnx.nro") == "pcsx_rearmed");
    assert(CoreName("FBNeo_libretro_libnx.nro") == "fbneo");
    assert(CoreName("retroarch_switch.nro").empty());
    assert(CoreName("_libretro_libnx.nro").empty());

    // RetroArch: everything, destination verbatim (folders kept)
    auto all = Plan(m, Target::RetroArch, {});
    assert(all.size() == 4);
    assert(all[3].dest == "/retroarch/cores/system/fbneo/neogeo.zip");
    assert(all[3].url == "https://raw.githubusercontent.com/Abdess/retrobios/main/bios/Arcade/FinalBurn%20Neo/neogeo.zip");
    assert(TotalSize(all) == 524288 + 16384 + 262144 + 1000);

    // RetroArch, only the installed cores
    const auto ids = SystemsOfCores(m, {"pcsx_rearmed", "unknown_core"});
    assert(ids.size() == 1 && ids[0] == "sony-playstation");
    auto some = Plan(m, Target::RetroArch, ids);
    assert(some.size() == 1 && some[0].dest == "/retroarch/cores/system/scph5501.bin");
    // a core of two systems lists each once
    assert(SystemsOfCores(m, {"fbneo", "melonds"}).size() == 3);

    // Tico: only systems with a folder, flat file name under it
    auto tico = Plan(m, Target::Tico, {});
    assert(tico.size() == 3);
    assert(tico[0].dest == "/tico/system/psx/scph5501.bin");
    assert(tico[1].dest == "/tico/system/nds/bios7.bin");

    // standalone emulators take their own systems only
    auto duck = Plan(m, Target::DuckStation, {});
    assert(duck.size() == 1 && duck[0].dest == "/switch/duckstation/bios/scph5501.bin");
    auto drastic = Plan(m, Target::DraStic, {});
    assert(drastic.size() == 2);
    assert(drastic[0].dest == "/switch/drastic/system/nds_bios_arm7.bin");
    assert(drastic[1].dest == "/switch/drastic/system/nds_firmware.bin");
    assert(Plan(m, Target::Flycast, {}).empty());

    // the target table is complete and in enum order
    assert(Targets().size() == static_cast<size_t>(Target::Count));
    for (size_t i = 0; i < Targets().size(); i++) {
        assert(static_cast<size_t>(Targets()[i].target) == i);
        assert(std::string_view{Targets()[i].bios_root}.ends_with('/'));
    }

    std::puts("test_bios_plan: ok");
    return 0;
}
