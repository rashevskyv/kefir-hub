// Host test for sphaira/include/battery_hop.hpp

#include "battery_hop.hpp"

#include <cassert>
#include <cstdio>

using namespace sphaira::battery::hop;

int main() {
    // a Kefir-like hekate_ipl.ini: [config] is not counted; payload and caption lines are not CFW entries
    const std::string ini =
        "[config]\r\nautoboot=0\r\nautoboot_list=0\r\nbootwait=3\r\n\r\n"
        "[Kefir sysMMC]\r\nfss0=atmosphere/package3\r\nemummc_force_disable=1\r\nkip1patch=nosigchk\r\n\r\n"
        "{--- tools ---}\r\n"
        "[TegraExplorer]\r\npayload=bootloader/payloads/TegraExplorer.bin\r\n\r\n"
        "[Kefir emuMMC]\r\nfss0=atmosphere/package3\r\nemummcforce=1\r\n\r\n";
    assert(FindEntry(ini, false) == 1);
    assert(FindEntry(ini, true) == 3);   // the payload entry is number 2 and counts

    // no explicit emuMMC entry: the Atmosphère entry that does not force the sysMMC follows emummc.ini
    const std::string plain = "[config]\nautoboot=1\n[Atmosphere]\nfss0=atmosphere/package3\n[Stock]\nemummc_force_disable=1\nstock=1\n";
    assert(FindEntry(plain, true) == 1);
    assert(FindEntry(plain, false) == 0); // "Stock" is not a CFW entry (no fss0): nothing to hop to
    assert(FindEntry("", true) == 0 && FindEntry("[config]\nautoboot=0\n", false) == 0);

    // the boot request goes in at 0x94 and nothing else changes
    std::vector<std::uint8_t> hekate(0x4000, 0xAB);
    assert(PatchBootCfg(hekate, 3));
    assert(hekate[0x93] == 0xAB && hekate[0x94] == 1 && hekate[0x95] == 3 && hekate[0x96] == 0 && hekate[0x97] == 0);
    assert(hekate[0x98] == 0 && hekate[0x94 + 0x83] == 0 && hekate[0x94 + 0x84] == 0xAB);
    std::vector<std::uint8_t> tiny(100);
    assert(!PatchBootCfg(tiny, 1) && !PatchBootCfg(hekate, 0) && !PatchBootCfg(hekate, 300));

    std::puts("test_battery_hop: ok");
    return 0;
}
