// Host test for sphaira/include/fw_store.hpp

#include "fw_store.hpp"

#include <cassert>
#include <cstdio>

using namespace sphaira::fwstore;

int main() {
    const auto id = ContentId("00e57b67b4dbd750a2df0574cddc123d.nca");
    assert(id && (*id)[0] == 0x00 && (*id)[1] == 0xE5 && (*id)[15] == 0x3D);
    assert(ContentId("00e57b67b4dbd750a2df0574cddc123d.cnmt.nca").has_value());
    assert(!ContentId("short.nca") && !ContentId("zz e57b67b4dbd750a2df0574cddc123d.nca"));

    assert(AssetUrl("https://x/{release}/{name}", "fw-22.0.0", "a.nca") == "https://x/fw-22.0.0/a.nca");

    // 22.1.0 → 22.5.0 on a console: 175 of 238 NCAs are shared; here three NCAs, one new, one a different size
    const std::vector<Nca> ncas{
        {"00000000000000000000000000000001.nca", 1000, "", "fw-22.0.0"},
        {"00000000000000000000000000000002.nca", 2000, "", "fw-22.5.0"},
        {"00000000000000000000000000000003.cnmt.nca", 3000, "", "fw-22.0.0"},
    };
    const auto plan = MakePlan(ncas, [](const Nca& n) -> std::optional<std::int64_t> {
        if (n.name.starts_with("00000000000000000000000000000001")) return 1000; // there, same size
        if (n.name.starts_with("00000000000000000000000000000003")) return 2999; // there, another size: fetch it again
        return std::nullopt;                                                      // not on the console
    });
    assert(plan.copy.size() == 1 && plan.copy[0] == 0);
    assert(plan.download.size() == 2 && plan.download[0] == 1 && plan.download[1] == 2);
    assert(plan.total_bytes == 6000 && plan.download_bytes == 5000);
    assert(NeededSdBytes(plan) > plan.total_bytes);

    std::puts("test_fw_store: ok");
}
