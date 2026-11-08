// NSO core: detection, ROM conversions, N64 cpio, XTX. Expected bytes come from the proven python converters
// (tests/data/nso/make_fixtures.py). Needs no zlib: compression is the libnx layer's job.
#include "nso_test_util.hpp"
#include "nso/n64.hpp"
#include "nso/rom.hpp"
#include "nso/systems.hpp"
#include "nso/xtx.hpp"

using namespace nso;

static std::vector<System> Det(const Bytes& b, const char* ext) {
    return DetectSystem(ByteSpan(b).first(std::min<size_t>(b.size(), 65536)), b.size(), ext);
}
static const std::vector<System> kAsk = {System::Genesis, System::Snes, System::Vb};

int main() {
    const auto ex = LoadExpect();

    // ---- tids
    CHECK_EQ(TidHex(Info(System::N64Mature).base_tid), std::string("0100E0601C632000"));
    CHECK_EQ(OverlayRomfs(System::Snes), std::string("/atmosphere/contents/01008D300C50C000/romfs"));
    CHECK_EQ(TidHex(UpdateTid(System::Nes)), std::string("0100D870045B6800"));
    CHECK_EQ(TidHex(UpdateTid(System::Gb)), std::string("0100C62011050800"));

    // ---- SNES
    Bytes snes = Gen(1, 32768);
    const u8 sig[4] = {0x34, 0x12, 0xCB, 0xED};
    for (int i = 0; i < 4; i++) snes[0x7FDC + size_t(i)] = sig[i];
    {
        const Normalized n = Normalize(System::Snes, snes);
        CHECK(n.ok && !n.passthrough && n.ext == "sfrom");
        CHECK(Matches(n.bytes, ex.at("sfrom")));
        CHECK(n.bytes.size() == snes.size() + kSnesCfgDefault.size() + 8);
        CHECK(Has(n.bytes, n.bytes.size() - 4, "Can1"));
        CHECK_EQ(Le32(n.bytes, n.bytes.size() - 8), u32(kSnesCfgDefault.size()));
        Bytes copier(512, 0);  // SMC copier header is stripped
        copier.insert(copier.end(), snes.begin(), snes.end());
        CHECK(Matches(Normalize(System::Snes, copier).bytes, ex.at("sfrom")));
        CHECK(!Normalize(System::Snes, Bytes(100, 0)).ok);
        // no extension, valid internal header -> SNES; header-less data -> ask
        CHECK(Det(snes, "bin") == std::vector<System>{System::Snes});
        CHECK(Det(snes, "SFC") == std::vector<System>{System::Snes});
        CHECK(Det(Gen(9, 32768), "bin") == kAsk);
        CHECK(Det(Gen(9, 32768), "") == kAsk);
        CHECK(Det(Gen(9, 4096), ".smc") == std::vector<System>{System::Snes});
    }

    // ---- NES
    {
        Bytes nes = Gen(2, 24592);
        const u8 h[16] = {'N', 'E', 'S', 0x1A, 1, 1, 1, 0, 'D', 'i', 's', 'k', 'D', 'u', 'd', 'e'};
        for (int i = 0; i < 16; i++) nes[size_t(i)] = h[i];
        const Normalized n = Normalize(System::Nes, nes);
        CHECK(n.ok && n.ext == "nes" && Matches(n.bytes, ex.at("nes")));
        for (size_t i = 8; i < 16; i++) CHECK_EQ(n.bytes[i], 0);
        CHECK_EQ(n.bytes[6], 1);  // flag 6 untouched
        CHECK(Det(nes, "bin") == std::vector<System>{System::Nes});
        CHECK(!Normalize(System::Nes, Gen(2, 100)).ok);
    }

    // ---- Genesis SMD
    {
        const Bytes smd = ReadBytes("smd.bin");
        CHECK(Det(smd, "bin") == std::vector<System>{System::Genesis});
        const Normalized n = Normalize(System::Genesis, smd);
        CHECK(n.ok && !n.passthrough && Matches(n.bytes, ex.at("smd_to_bin")));
        Bytes lin = Gen(3, 65536);
        for (int i = 0; i < 4; i++) lin[0x100 + size_t(i)] = u8("SEGA"[i]);
        CHECK(Det(lin, "bin") == std::vector<System>{System::Genesis});
        CHECK(Normalize(System::Genesis, lin).passthrough);
    }

    // ---- N64: byte order, header facts
    {
        Bytes z = Gen(4, 8192);
        const u8 head[4] = {0x80, 0x37, 0x12, 0x40};
        for (size_t i = 0; i < 4; i++) z[i] = head[i];
        const u8 crcs[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22, 0x33, 0x44};
        for (size_t i = 0; i < 8; i++) z[0x10 + i] = crcs[i];
        for (size_t i = 0; i < 4; i++) z[0x3B + i] = u8("NXYE"[i]);
        CHECK(Matches(z, ex.at("z64")));
        Bytes v64 = z, n64 = z;
        for (size_t i = 0; i < z.size(); i += 2) { v64[i] = z[i + 1]; v64[i + 1] = z[i]; }
        for (size_t i = 0; i < z.size(); i += 4) {
            n64[i] = z[i + 3]; n64[i + 1] = z[i + 2]; n64[i + 2] = z[i + 1]; n64[i + 3] = z[i];
        }
        CHECK(Matches(v64, ex.at("v64")));  // the python reference swaps give the same bytes
        CHECK(Matches(n64, ex.at("n64")));
        CHECK(N64ToZ64(v64) == z);
        CHECK(N64ToZ64(n64) == z);
        for (const Bytes* b : {&z, &v64, &n64}) {
            CHECK(Det(*b, "bin") == (std::vector<System>{System::N64, System::N64Mature}));
            const Normalized n = Normalize(System::N64Mature, *b);
            CHECK(n.ok && n.ext == "z64" && !n.n64_pal);
            CHECK_EQ(n.n64_crc1, 0xAABBCCDDu);
            CHECK_EQ(n.n64_crc2, 0x11223344u);
            CHECK_EQ(n.passthrough, b == &z);
            CHECK(n.passthrough || n.bytes == z);
        }
        Bytes zp = z;
        zp[0x3E] = 'P';
        CHECK(Matches(zp, ex.at("z64_pal")));
        CHECK(Normalize(System::N64, zp).n64_pal);
        CHECK(!Normalize(System::N64, Gen(4, 8192)).ok);  // no magic
    }

    // ---- Game Boy / GBA / VB
    {
        Bytes gb = Gen(5, 0x8000);
        const u8 logo[4] = {0xCE, 0xED, 0x66, 0x66};
        for (size_t i = 0; i < 4; i++) gb[0x104 + i] = logo[i];
        gb[0x143] = 0; gb[0x14E] = 0x43; gb[0x14F] = 0x21;
        CHECK(Det(gb, "bin") == std::vector<System>{System::Gb});
        CHECK_EQ(Normalize(System::Gb, gb).ext, std::string("gb"));
        CHECK_EQ(GbHeaderWord(gb), 0x4321u);
        gb[0x143] = 0xC0;
        CHECK_EQ(Normalize(System::Gb, gb).ext, std::string("gbc"));
        CHECK(Normalize(System::Gb, gb).passthrough);

        Bytes gba = Gen(7, 0x400);
        const u8 g[4] = {0x24, 0xFF, 0xAE, 0x51};
        for (size_t i = 0; i < 4; i++) gba[4 + i] = g[i];
        gba[0xB2] = 0x96;
        for (size_t i = 0; i < 4; i++) gba[0xAC + i] = u8("AB#D"[i]);
        CHECK(Det(gba, "bin") == std::vector<System>{System::Gba});
        CHECK_EQ(GbaGameCode(gba), std::string("AB0D"));
        Bytes lz4 = {0x04, 0x22, 0x4D, 0x18, 1, 2, 3};
        CHECK(Det(lz4, "gba") == std::vector<System>{System::Gba});
        CHECK(!Normalize(System::Gba, lz4).ok);

        CHECK(Det(Gen(8, 0x80000), "vb") == std::vector<System>{System::Vb});
        CHECK(Normalize(System::Vb, Gen(8, 0x80000)).passthrough);
        CHECK(Det(Gen(8, 100), "gen") == std::vector<System>{System::Genesis});
        CHECK(Det(Gen(8, 100), "fds").empty());
    }

    // ---- N64 .dtz payload: cpio + meta + cfg byte-exact vs the mod game N-4435_e
    {
        const std::string meta = ReadFile("n64_4435.meta");
        const std::string cfg = ReadFile("n64_4435.cfg");
        CHECK_EQ(N64MetaText(false), meta);
        const u32 idle[1] = {0x80001194};
        const N64Config c = {0, 0, "SRAM", "Controller Pak", "16M", 90, idle, 1};
        CHECK_EQ(N64CfgText(c), cfg);
        const Bytes cpio = BuildCpio("Hybrid Heaven (USA)", {{"00_XXXXXX.000.meta", meta, 1636711955u},
                                                              {"02_XXXXXX.000.cfg", cfg, 1636711992u}});
        CHECK(cpio == ReadBytes("n64_4435.cpio"));
        CHECK(N64MetaText(true).find("\"PAL\": 1") != std::string::npos);
        // generic default: just structure (UNPROVEN for boot)
        const Bytes d = BuildDtzCpio(false, nullptr, 5);
        CHECK(d.size() > 100 && d[0] == 0xC7 && d[1] == 0x71);
        // table lookup
        const N64Config table[] = {{0, 0, "NONE", "None", "", 0, nullptr, 0}, {1, 2, "FLASH", "None", "8M", 0, nullptr, 0}};
        CHECK(FindN64Config(table, 1, 2) == &table[1]);
        CHECK(FindN64Config(table, 0, 0) == nullptr);
        CHECK(FindN64Config(table, 1, 3) == nullptr);
        CHECK(FindN64Config(0xAABBCCDD, 0x11223344) == nullptr);  // shipped table is empty
    }

    // ---- XTX: whole container byte-exact vs the python reference encoder (which is byte-exact on 80/80 official files)
    for (auto [w, h] : {std::pair{48, 40}, std::pair{37, 23}}) {
        Bytes px;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                px.push_back(u8(x * 7 + y * 3));
                px.push_back(u8(x * y));
                px.push_back(u8(x ^ (y * 5)));
                px.push_back((x + y) % 5 ? 255 : u8(x * 13));
            }
        const Bytes x = EncodeXtx(px, w, h);
        CHECK(x == ReadBytes("xtx_" + std::to_string(w) + "x" + std::to_string(h) + ".bin"));
        CHECK(Has(x, 0, "DFvN") && x.size() % 0x400 != 1);
    }
    return Finish("test_nso_rom");
}
