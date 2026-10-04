// Host test for sphaira/include/nacp_patch.hpp

#include "nacp_patch.hpp"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace sphaira::nacp_patch;

// toy 32-byte hash, enough to see that the right bytes feed the right slots.
static void ToyHash(const std::uint8_t* d, std::size_t n, std::uint8_t out[32]) {
    std::uint32_t h[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    for (std::size_t i = 0; i < n; i++) {
        h[i % 8] = h[i % 8] * 31 + d[i] + static_cast<std::uint32_t>(i);
    }
    std::memcpy(out, h, 32);
}

static void TestNacp() {
    std::vector<std::uint8_t> nacp(NACP_SIZE, 0);
    nacp[OFF_STARTUP_USER_ACCOUNT] = 2;
    nacp[OFF_NETWORK_LICENSE_ON_LAUNCH] = 1;
    nacp[OFF_SCREENSHOT] = 1;
    nacp[OFF_VIDEO_CAPTURE] = 0;
    auto s = ReadState(nacp);
    assert(s.linked_account_required && !s.screenshots_allowed && !s.video_allowed);

    Patch p;
    p.linked_account_required = false;
    p.video_allowed = true;
    assert(Apply(nacp, p));
    s = ReadState(nacp);
    assert(!s.linked_account_required && s.screenshots_allowed && s.video_allowed);
    assert(nacp[OFF_STARTUP_USER_ACCOUNT] == 1 && nacp[OFF_VIDEO_CAPTURE] == 2);
    assert(!Apply(nacp, p)); // already so

    // a game without a user at launch keeps 0.
    std::vector<std::uint8_t> no_user(NACP_SIZE, 0);
    Patch off;
    off.linked_account_required = false;
    Apply(no_user, off);
    assert(no_user[OFF_STARTUP_USER_ACCOUNT] == 0);
}

static void TestRomfs() {
    // header 0x50, file table at 0x60 with two entries, data at 0x100.
    std::vector<std::uint8_t> r(0x200, 0);
    auto put64 = [&](std::size_t o, std::uint64_t v) { std::memcpy(&r[o], &v, 8); };
    auto put32 = [&](std::size_t o, std::uint32_t v) { std::memcpy(&r[o], &v, 4); };
    put64(0x38, 0x60);
    put64(0x48, 0x100);
    std::size_t e = 0x60;
    const std::string a = "icon_AmericanEnglish.dat", b = "control.nacp";
    put32(e + 0x00, 0); put64(e + 0x08, 0x40); put64(e + 0x10, 0x20); put32(e + 0x1C, a.size());
    std::memcpy(&r[e + 0x20], a.data(), a.size());
    e += (0x20 + a.size() + 3) & ~3u;
    put32(e + 0x00, 0); put64(e + 0x08, 0x0); put64(e + 0x10, 0x4000); put32(e + 0x1C, b.size());
    std::memcpy(&r[e + 0x20], b.data(), b.size());
    put64(0x40, e + 0x20 + b.size() - 0x60);

    const auto f = FindRomfsFile(r, "control.nacp");
    assert(f && f->first == 0x100 && f->second == 0x4000);
    assert(FindRomfsFile(r, "icon_AmericanEnglish.dat")->first == 0x140);
    assert(!FindRomfsFile(r, "missing"));
}

static void TestIvfc() {
    // 3 levels: L0 (hash of L1), L1 (hash of data), data 5 blocks of 0x40 (last one partial).
    const std::uint32_t log2 = 6; // 0x40 blocks
    std::vector<std::uint8_t> sec(0x1000, 0);
    // hash levels use bigger blocks, like real NCAs (0x4000), so L0 fits in one block.
    std::vector<IvfcLevel> lv = {
        {0x000, 2 * 32, 7},             // L0: hashes of L1's two 0x80 blocks
        {0x100, 5 * 32, 7},             // L1: hashes of the five data blocks
        {0x400, 4 * 0x40 + 0x10, log2}, // data, last block partial
    };
    for (std::size_t i = 0; i < lv[2].size; i++) {
        sec[lv[2].offset + i] = static_cast<std::uint8_t>(i * 7);
    }
    std::uint8_t master[32]{};
    bool exact = false;
    // build the whole tree from scratch, then it must verify.
    assert(Rehash(sec, lv, master, 0, lv[2].size, ToyHash, false, exact));
    assert(Rehash(sec, lv, master, 0, lv[2].size, ToyHash, true, exact));
    assert(!exact);

    // change one byte in the last (partial) block: verify fails until it is rehashed.
    sec[lv[2].offset + 4 * 0x40 + 3] ^= 0xFF;
    assert(!Rehash(sec, lv, master, 4 * 0x40 + 3, 1, ToyHash, true, exact));
    assert(Rehash(sec, lv, master, 4 * 0x40 + 3, 1, ToyHash, false, exact));
    assert(Rehash(sec, lv, master, 0, lv[2].size, ToyHash, true, exact));

    // a master hash over level 0 exactly (unpadded) is recognised and kept that way.
    std::uint8_t exact_master[32];
    ToyHash(sec.data() + lv[0].offset, lv[0].size, exact_master);
    std::memcpy(master, exact_master, 32);
    assert(Rehash(sec, lv, master, 0, 1, ToyHash, true, exact) && exact);
    sec[lv[2].offset] ^= 1;
    assert(Rehash(sec, lv, master, 0, 1, ToyHash, false, exact));
    ToyHash(sec.data() + lv[0].offset, lv[0].size, exact_master);
    assert(!std::memcmp(master, exact_master, 32));
}

int main() {
    TestNacp();
    TestRomfs();
    TestIvfc();
    std::printf("ok  nacp_patch: all checks passed\n");
    return 0;
}
