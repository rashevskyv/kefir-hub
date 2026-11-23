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

static void TestTitles() {
    std::vector<std::uint8_t> nacp(NACP_SIZE, 0);
    // names in English (0) and Japanese (2) only.
    std::memcpy(&nacp[0], "Old", 3);
    std::memcpy(&nacp[2 * LANG_ENTRY_SIZE], "Furui", 5);
    std::memcpy(&nacp[OFF_DISPLAY_VERSION], "1.0.0", 5);
    auto s = ReadState(nacp);
    assert(s.titles_editable && s.version == "1.0.0");

    Patch p;
    p.name = "New name";
    p.author = "Me";
    p.version = std::string(40, 'v');
    assert(p.EditsTitle());
    assert(Apply(nacp, p));
    assert(ReadText(&nacp[0], NAME_SIZE) == "New name" && ReadText(&nacp[NAME_SIZE], AUTHOR_SIZE) == "Me");
    assert(ReadText(&nacp[2 * LANG_ENTRY_SIZE], NAME_SIZE) == "New name");
    assert(nacp[1 * LANG_ENTRY_SIZE] == 0); // a language without a name stays empty
    assert(ReadState(nacp).version == std::string(15, 'v'));
    assert(!Apply(nacp, p));

    // no name at all: the first entry gets it.
    std::vector<std::uint8_t> empty(NACP_SIZE, 0);
    Patch n;
    n.name = "X";
    assert(Apply(empty, n) && empty[0] == 'X' && empty[LANG_ENTRY_SIZE] == 0);

    // a deflated title table (21.0.0+) is left alone.
    std::vector<std::uint8_t> v2(NACP_SIZE, 0x5A);
    v2[OFF_TITLES_DATA_FORMAT] = 1;
    assert(!ReadState(v2).titles_editable);
    assert(!Apply(v2, n));

    // a cut never splits a UTF-8 character: 0x1FF bytes of room, "я" is 2 bytes.
    std::string long_name(0x1FE, 'a');
    long_name += "я";
    std::vector<std::uint8_t> u(NACP_SIZE, 0);
    Patch ln;
    ln.name = long_name;
    Apply(u, ln);
    assert(ReadText(&u[0], NAME_SIZE) == std::string(0x1FE, 'a'));
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

    // the icon slot reaches the next file, not just its own size.
    auto files = ListRomfsFiles(r);
    assert(files.size() == 2 && IsIconFile(files[0].name) && !IsIconFile(files[1].name));
    auto icon = files[0];
    assert(icon.data_off == 0x140 && icon.size == 0x20);
    assert(files[1].capacity == 0x4000); // never less than the file itself
    // icon at 0x140 with nacp at 0x100: nothing after it but the image end.
    assert(icon.capacity == 0x200 - 0x140);

    std::vector<std::uint8_t> small(0x10, 0xAB);
    r[0x140 + 0x18] = 0xEE; // old bytes past the new end are zeroed
    assert(ReplaceRomfsFile(r, icon, small));
    assert(FindRomfsFile(r, "icon_AmericanEnglish.dat")->second == 0x10);
    assert(r[0x140] == 0xAB && r[0x150] == 0 && r[0x158] == 0);
    // written once smaller, the slot is still as big as before.
    auto again = ListRomfsFiles(r)[0];
    assert(again.size == 0x10 && again.capacity == icon.capacity);
    std::vector<std::uint8_t> big(icon.capacity + 1, 1);
    assert(!ReplaceRomfsFile(r, again, big));
    std::vector<std::uint8_t> full(icon.capacity, 2);
    assert(ReplaceRomfsFile(r, again, full) && FindRomfsFile(r, "icon_AmericanEnglish.dat")->second == icon.capacity);
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
    TestTitles();
    TestRomfs();
    TestIvfc();
    std::printf("ok  nacp_patch: all checks passed\n");
    return 0;
}
