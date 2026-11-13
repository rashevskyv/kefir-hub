// Host test for sphaira/include/nextendo_online.hpp

#include "nextendo_online.hpp"

#include <cassert>
#include <cstdio>
#include <string>

using namespace sphaira::nextendo;

int main() {
    // the real shape of /api/online-counts
    const auto real = ParseOnlineCounts(R"({"counts":{"0100000000010000":0,"0100152000022000":16,"01006a800016e000":21}})");
    assert(real && real->size() == 3);
    assert(real->at(0x0100000000010000) == 0); // supported with 0 players is still supported
    assert(real->at(0x0100152000022000) == 16 && real->at(0x01006A800016E000) == 21);
    assert(!real->count(0x0100000000010001));

    // whitespace, uppercase hex, an empty object
    const auto spaced = ParseOnlineCounts("{ \"counts\" : {\n \"01006A800016E000\" : 5 ,\n\"0100152000022000\":0 } }");
    assert(spaced && spaced->size() == 2 && spaced->at(0x01006A800016E000) == 5);
    const auto empty = ParseOnlineCounts(R"({"counts":{}})");
    assert(empty && empty->empty());

    // a key that is not a title id is skipped; the rest still counts
    const auto odd = ParseOnlineCounts(R"({"counts":{"total":7,"0100zz0000010000":1,"0100152000022000":2}})");
    assert(odd && odd->size() == 1 && odd->at(0x0100152000022000) == 2);

    // huge numbers are capped, not wrapped
    const auto big = ParseOnlineCounts(R"({"counts":{"0100152000022000":99999999999999999999}})");
    assert(big && big->at(0x0100152000022000) == 999999);

    // not that shape: no data, not an empty list
    for (const char* bad : {"", "<html>502 Bad Gateway</html>", R"({"error":"x"})", R"({"counts":[1,2]})",
                            R"({"counts":{"0100152000022000":-1}})", R"({"counts":{"0100152000022000":"3"}})",
                            R"({"counts":{"0100152000022000":3)", R"({"counts":{"0100152000022000" 3}})"}) {
        assert(!ParseOnlineCounts(bad));
    }
    assert(!ParseOnlineCounts(std::string(kOnlineCountsMaxBody + 1, ' ')));

    // asked once a minute; first time; a clock set back
    assert(OnlineCountsDue(1000, 0));
    assert(!OnlineCountsDue(1000 + kOnlineCountsEverySeconds - 1, 1000) && OnlineCountsDue(1000 + kOnlineCountsEverySeconds, 1000));
    assert(OnlineCountsDue(500, 1000));

    std::puts("test_nextendo_online: ok");
    return 0;
}
