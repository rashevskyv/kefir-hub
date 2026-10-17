// Host test for sphaira/include/console_tables.hpp

#include "console_tables.hpp"

#include <cassert>
#include <cstdio>

using namespace sphaira::console;

int main() {
    assert(DramChip(0) == "Samsung K4F6E304HB-MGCH 4GB");   // the one name DBI's own table gave
    assert(DramChip(21) == "Samsung K4U6E3S4AB-MGCL 4GB");  // the user's console
    assert(DramChip(34) == "Micron MT53E512M32D1NP-046 WTB 4GB");
    assert(DramChip(16).empty() && DramChip(35).empty() && DramChip(1000).empty());

    assert(FuseFirmware(24) == "23.0.0");                   // the user's console: 24 fuses
    assert(FuseFirmware(1) == "1.0.0" && FuseFirmware(13) == "10.0.0" && FuseFirmware(16) == "13.2.1");
    assert(FuseFirmware(0).empty() && FuseFirmware(25).empty());

    assert(LanguageName("ru") == "Russian");
    assert(LanguageName("en-US") == "English (US)");
    assert(LanguageName("xx") == "xx");

    std::puts("test_console_tables: ok");
}
