// Host test for sphaira/include/nextendo_refresh.hpp

#include "nextendo_refresh.hpp"

#include <cassert>
#include <cstdio>

using namespace sphaira::nextendo;

int main() {
    assert(IsSafeTag("v4.0.11") && IsSafeTag("v4.0.12-rc1") && IsSafeTag("release_2"));
    assert(!IsSafeTag("") && !IsSafeTag("v4/../x") && !IsSafeTag("a b") && !IsSafeTag("x?y=1") && !IsSafeTag(std::string(41, 'a')));
    assert(ConfigUrl("v4.0.11") == "https://raw.githubusercontent.com/NextendoNetwork/Prelude-Nextendo/v4.0.11/source/nextendo_config.h");

    // public: the real Nextendo servers
    assert(IsPublicIpv4("51.178.29.194") && IsPublicIpv4("164.132.111.120") && IsPublicIpv4("3.135.232.168"));
    // never the user's own network or special ranges
    for (const char* bad : {"10.0.0.1", "127.0.0.1", "0.0.0.0", "172.16.0.1", "172.31.255.255", "192.168.1.5", "169.254.1.1",
                            "100.64.0.1", "224.0.0.1", "255.255.255.255", "not-an-ip", "1.2.3"}) {
        assert(!IsPublicIpv4(bad));
    }
    assert(IsPublicIpv4("172.15.0.1") && IsPublicIpv4("172.32.0.1") && IsPublicIpv4("192.169.0.1") && IsPublicIpv4("100.63.0.1"));
    assert(ArePublic({"51.178.29.194", "164.132.111.120"}) && !ArePublic({"51.178.29.194", "10.1.1.1"}));

    // due once a day; first run; a clock set back
    const std::int64_t day = kRefreshEverySeconds;
    assert(RefreshDue(1000000, 0));
    assert(!RefreshDue(1000000 + day - 1, 1000000) && RefreshDue(1000000 + day, 1000000));
    assert(RefreshDue(500, 1000000));

    std::puts("test_nextendo_refresh: ok");
    return 0;
}
