// Host test for sphaira/include/net_address.hpp

#include "net_address.hpp"

#include <cassert>
#include <cstdio>

using sphaira::net::SplitHostPort;

int main() {
    auto r = SplitHostPort("192.168.1.5:2121");
    assert(r.host == "192.168.1.5" && r.port == 2121);

    r = SplitHostPort("  nas.local:21/ ");
    assert(r.host == "nas.local" && r.port == 21);
    r = SplitHostPort("ftp://10.0.0.2:990/");
    assert(r.host == "10.0.0.2" && r.port == 990);
    r = SplitHostPort("[fe80::1]:2121");
    assert(r.host == "[fe80::1]" && r.port == 2121);

    // nothing to split: the text comes back as it was typed
    r = SplitHostPort("192.168.1.5");
    assert(r.host == "192.168.1.5" && !r.port);
    r = SplitHostPort("fe80::1");                       // a bare IPv6 address has colons but no port
    assert(r.host == "fe80::1" && !r.port);
    r = SplitHostPort("host:0");
    assert(r.host == "host:0" && !r.port);
    r = SplitHostPort("host:70000");
    assert(r.host == "host:70000" && !r.port);
    r = SplitHostPort("host:21x");
    assert(r.host == "host:21x" && !r.port);
    r = SplitHostPort("host:");
    assert(r.host == "host:" && !r.port);
    r = SplitHostPort(":21");
    assert(r.host == ":21" && !r.port);
    r = SplitHostPort("host:21/dir");                   // a path after the host: leave it to the caller
    assert(r.host == "host:21/dir" && !r.port);
    r = SplitHostPort("[fe80::1");
    assert(r.host == "[fe80::1" && !r.port);
    r = SplitHostPort("");
    assert(r.host.empty() && !r.port);

    std::puts("test_net_address: ok");
    return 0;
}
