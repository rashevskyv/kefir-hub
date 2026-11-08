// NSO core: entry builders (byte-exact vs the python converters), code allocation, PlanTarget / BuildAdd / BuildRemove.
#include "nso_test_util.hpp"
#include "nso/plan.hpp"
#include "nso/xtx.hpp"

using namespace nso;

static const char* kTitle = "Test \"Game\" \\ \t\xC2\xA9 \xE3\x83\xBC";

static EntryArgs Args(const std::string& code, bool cgb = false) {
    EntryArgs a;
    a.code = code; a.title = kTitle; a.publisher = "Pub Co"; a.release_date = "1994-05-06";
    a.lcla6_date = "2031-02-03"; a.players = 2; a.simultaneous = true; a.cgb = cgb;
    return a;
}

static void EntryCase(System s, const std::string& code, const std::string& fixture, std::string_view ext, bool cgb = false) {
    const Paths p = MakePaths(s, code, ext);
    CHECK_EQ(BuildEntry(s, Args(code, cgb), p), ReadFile(fixture));
}

static Bytes WithMagic(Bytes b, size_t off, std::initializer_list<int> m) {
    for (int v : m) b[off++] = u8(v);
    return b;
}

int main() {
    // ---- entries == python reference output, byte for byte
    EntryCase(System::Snes, "S-3123_e", "entry_snes.json", "sfrom");
    EntryCase(System::Nes, "CLV-P-NABCE", "entry_nes.json", "nes");
    EntryCase(System::N64, "N-4435_e", "entry_n64.json", "z64");
    EntryCase(System::N64Mature, "N-4435_e", "entry_n64.json", "z64");
    EntryCase(System::Genesis, "M-1234_e", "entry_genesis.json", "gen");
    EntryCase(System::Gb, "D-4321_e", "entry_gb_dmg.json", "gb");
    EntryCase(System::Gb, "C-4321_e", "entry_gb_cgb.json", "gbc", true);
    EntryCase(System::Gba, "A-ABCD_e", "entry_gba.json", "gba");
    EntryCase(System::Vb, "V-1234_e", "entry_vb.json", "vb");  // LIKELY: no sample mod

    // ---- paths
    {
        const Paths p = MakePaths(System::Snes, "S-3123_e", "sfrom");
        CHECK_EQ(p.dir, std::string("titles/S-3123_e"));
        CHECK_EQ(p.rom_file, std::string("3123_e.sfrom"));
        CHECK_EQ(p.sig_file, std::string("3123_e.sfromsig"));
        CHECK_EQ(p.cover_file, std::string("S-3123_e.xtx.z"));
        CHECK_EQ(p.details_db, std::string("/titles/S-3123_e/S-3123_e-details.xtx.z"));
        const Paths n = MakePaths(System::Nes, "CLV-P-NABCE", "nes");
        CHECK_EQ(n.details_file, std::string("CLV-P-NABCE00.xtx.z"));
        const Paths q = MakePaths(System::N64, "N-1234_e", "z64");
        CHECK_EQ(q.rom_file, std::string("N-1234_e.bnz"));
        CHECK_EQ(q.rom_db, std::string("/titles/N-1234_e/N-1234_e.bin"));
        CHECK_EQ(MakePaths(System::N64, "N-1234_e", "z64", true).rom_file, std::string("N-1234_e.bin"));
        CHECK_EQ(q.dtz_file, std::string("N-1234_e.dtz"));
    }

    // ---- allocation: deterministic, probes past taken codes, wraps
    {
        IdSeed id;
        id.crc = 12345;
        CHECK_EQ(AllocateCode(System::Genesis, id, {}), std::string("M-2345_e"));
        CHECK_EQ(AllocateCode(System::Genesis, id, {"M-2345_e", "M-2346_e"}), std::string("M-2347_e"));
        bool taken = false;
        AllocateCode(System::Genesis, id, {"M-2345_e"}, &taken);
        CHECK(taken);
        id.crc = 9999;
        CHECK_EQ(AllocateCode(System::Genesis, id, {"M-9999_e"}), std::string("M-0000_e"));
        id.crc = 7;
        CHECK_EQ(AllocateCode(System::Snes, id, {}), std::string("S-3007_e"));
        CHECK_EQ(AllocateCode(System::N64, id, {}), std::string("N-1007_e"));
        id.pal = true;
        CHECK_EQ(AllocateCode(System::N64, id, {}), std::string("N-1007_p"));
        CHECK_EQ(AllocateCode(System::Vb, id, {}), std::string("V-1007_e"));
        id.crc = 0;
        CHECK_EQ(AllocateCode(System::Nes, id, {}), std::string("CLV-P-NAAAE"));
        CHECK_EQ(AllocateCode(System::Nes, id, {"CLV-P-NAAAE"}), std::string("CLV-P-NAABE"));
        id.gb_word = 0x4321;
        CHECK_EQ(AllocateCode(System::Gb, id, {}), std::string("D-4321_e"));
        id.cgb = true;
        CHECK_EQ(AllocateCode(System::Gb, id, {"C-4321_e"}), std::string("C-4322_e"));
        id.gba_code = "AZCE";
        CHECK_EQ(AllocateCode(System::Gba, id, {}), std::string("A-AZCE_e"));
        CHECK_EQ(AllocateCode(System::Gba, id, {"A-AZCE_e"}), std::string("A-AZCF_e"));
        id.gba_code = "ZZZZ";
        CHECK_EQ(AllocateCode(System::Gba, id, {"A-ZZZZ_e"}), std::string("A-0000_e"));
    }

    // ---- title from file name
    CHECK_EQ(TitleFromFileName("/roms/Super Game (USA) [!].sfc"), std::string("Super Game"));
    CHECK_EQ(TitleFromFileName("a_b_c.nes"), std::string("a b c"));
    CHECK_EQ(TitleFromFileName("(demo).nes"), std::string("(demo)"));

    CHECK(DetectSystem(Gen(1, 100), 100, "fds").empty());
    CHECK(!PlanTarget("a.fds", Gen(1, 100), 100, Installed{}).error.empty());
    CHECK_EQ(MarkerRemove("A\tx\nB\ty\n", "A"), std::string("B\ty\n"));

    // ---- PlanTarget
    {
        Installed inst{};
        const Bytes unknown = Gen(9, 32768);
        TargetPlan t = PlanTarget("game.bin", unknown, unknown.size(), inst);
        CHECK(t.status == TargetPlan::Status::Error && !t.error.empty());
        CHECK((t.detected == std::vector<System>{System::Genesis, System::Snes, System::Vb}));
        inst[size_t(System::Snes)] = true;
        t = PlanTarget("game.bin", unknown, unknown.size(), inst);
        CHECK(t.status == TargetPlan::Status::Chosen && t.chosen == System::Snes);
        inst[size_t(System::Vb)] = true;
        t = PlanTarget("game.bin", unknown, unknown.size(), inst);
        CHECK(t.status == TargetPlan::Status::NeedChoice);
        CHECK((t.candidates == std::vector<System>{System::Snes, System::Vb}));
        const Bytes n64 = WithMagic(Gen(4, 8192), 0, {0x80, 0x37, 0x12, 0x40});
        inst = {};
        inst[size_t(System::N64Mature)] = true;
        t = PlanTarget("x.z64", n64, n64.size(), inst);
        CHECK(t.status == TargetPlan::Status::Chosen && t.chosen == System::N64Mature);
        inst[size_t(System::N64)] = true;
        t = PlanTarget("x.z64", n64, n64.size(), inst);
        CHECK(t.status == TargetPlan::Status::NeedChoice);
    }

    // ---- BuildAdd / BuildRemove end to end
    {
        const std::string snes_db = ReadFile("db_snes_v0.json");
        Bytes snes = Gen(1, 32768);
        snes = WithMagic(snes, 0x7FDC, {0x34, 0x12, 0xCB, 0xED});
        AddRequest rq;
        rq.system = System::Snes; rq.rom = snes; rq.file_name = "My Hack (U).smc"; rq.db_text = snes_db;
        AddResult r = BuildAdd(rq);
        CHECK(r.ok);
        CHECK_EQ(r.overlay_romfs, std::string("/atmosphere/contents/01008D300C50C000/romfs"));
        CHECK(r.code.rfind("S-", 0) == 0);
        const std::string num = r.code.substr(2);
        CHECK_EQ(r.files.size(), size_t(2));
        CHECK_EQ(r.files[0].path, "titles/" + r.code + "/" + num + ".sfrom");
        CHECK(r.files[0].kind == OutFile::Kind::Bytes && !r.files[0].compress);
        CHECK(r.files[1].kind == OutFile::Kind::DonorSfromsig);
        CHECK_EQ(r.files[1].path, "titles/" + r.code + "/" + num + ".sfromsig");
        CHECK_EQ(r.covers.size(), size_t(2));
        CHECK(r.covers[0].kind == CoverKind::XtxZ && !r.covers[0].is_details && r.covers[0].w == 512);
        CHECK(r.covers[1].is_details && r.covers[1].w == 400 && r.covers[1].h == 300);
        CHECK(!r.experimental);
        CHECK(r.entry_json.find("\"title\":\"My Hack\"") != std::string::npos);
        auto codes = titlesdb::ListCodes(r.db_text);
        CHECK(codes && codes->size() == titlesdb::ListCodes(snes_db)->size() + 1 && codes->back() == r.code);
        CHECK(MarkerHas(r.marker_text, r.code));
        CHECK(r.marker_text.rfind(r.code + "\t", 0) == 0 && MarkerNote(r.marker_text, r.code).size() == 8);
        // same ROM into the new db -> refused (it is ours already)
        rq.db_text = r.db_text; rq.marker_text = r.marker_text;
        const AddResult dup = BuildAdd(rq);
        CHECK(!dup.ok && dup.error.find("already") != std::string::npos);
        // a different ROM that collides with one of ours (other crc in the marker) -> next free code
        rq.marker_text = r.code + "\tdeadbeef\n";
        CHECK(BuildAdd(rq).ok);
        // same ROM but the code belongs to an official entry (not in marker) -> next free code
        rq.marker_text.clear();
        const AddResult second = BuildAdd(rq);
        CHECK(second.ok && second.code != r.code);
        // the first candidate is taken by an unmarked entry: the ROM is still recognised by its crc
        rq.db_text = second.db_text; rq.marker_text = second.marker_text;
        CHECK(!BuildAdd(rq).ok);
        // a db with a duplicated key loses every copy
        const RemoveResult dupkey = BuildRemove("{\"titles\":{\"X-1\":{},\"X-1\":{}}}", "X-1\n", "X-1");
        CHECK(dupkey.ok && titlesdb::ListCodes(dupkey.db_text) && titlesdb::ListCodes(dupkey.db_text)->empty());
        // the encoder refuses a short buffer
        CHECK(EncodeXtx(Bytes(4), 8, 8).empty());

        // remove: only our own
        const RemoveResult rm = BuildRemove(r.db_text, r.marker_text, r.code);
        CHECK(rm.ok);
        CHECK_EQ(rm.db_text, snes_db);  // byte-exact round trip
        CHECK_EQ(rm.delete_dir, "titles/" + r.code);
        CHECK(rm.marker_empty);
        const std::string official = titlesdb::ListCodes(snes_db)->front();
        const RemoveResult nope = BuildRemove(r.db_text, r.marker_text, official);
        CHECK(!nope.ok);
        CHECK(!BuildRemove(r.db_text, r.marker_text, "../etc").ok);
        CHECK(!BuildRemove(r.db_text, "", r.code).ok);
        CHECK(!BuildRemove("garbage", r.marker_text, r.code).ok);
        CHECK((ListAdded(r.db_text, r.marker_text) == std::vector<std::string>{r.code}));
        CHECK(ListAdded(snes_db, r.marker_text).empty());
        CHECK_EQ(MarkerRemove(MarkerAdd(MarkerAdd("", "A"), "B"), "A"), std::string("B\n"));
        CHECK_EQ(MarkerAdd("A\tnote\r\n", "A"), std::string("A\tnote\r\n"));

        // NES into the v0 list db
        const Bytes nes = WithMagic(Gen(2, 24592), 0, {'N', 'E', 'S', 0x1A});
        AddRequest nq;
        nq.system = System::Nes; nq.rom = nes; nq.file_name = "game.nes"; nq.db_text = ReadFile("db_nes_v0.json");
        const AddResult nr = BuildAdd(nq);
        CHECK(nr.ok && nr.code.rfind("CLV-P-N", 0) == 0 && nr.code.back() == 'E');
        CHECK(titlesdb::Scan(nr.db_text).is_list);
        CHECK_EQ(nr.files[0].path, "titles/" + nr.code + "/" + nr.code + ".nes");
        CHECK_EQ(nr.covers[1].path, "titles/" + nr.code + "/" + nr.code + "00.xtx.z");

        // Genesis from SMD: converted bytes; GB: pass-through; GBA; VB
        const Bytes smd = ReadBytes("smd.bin");
        AddRequest gq;
        gq.system = System::Genesis; gq.rom = smd; gq.file_name = "g.smd"; gq.db_text = ReadFile("db_n64_small.json");
        const AddResult gr = BuildAdd(gq);
        CHECK(gr.ok && gr.files[0].kind == OutFile::Kind::Bytes && gr.files[0].data.size() == 65536);
        CHECK(gr.covers[0].kind == CoverKind::Png);
        Bytes gb = WithMagic(Gen(5, 0x8000), 0x104, {0xCE, 0xED, 0x66, 0x66});
        gb[0x143] = 0x80; gb[0x14E] = 0x43; gb[0x14F] = 0x21;
        AddRequest bq;
        bq.system = System::Gb; bq.rom = gb; bq.file_name = "g.gbc"; bq.db_text = "{\"titles\":{}}";
        const AddResult br = BuildAdd(bq);
        CHECK(br.ok && br.code == "C-4321_e" && br.files[0].kind == OutFile::Kind::Source);
        CHECK_EQ(br.files[0].path, std::string("titles/C-4321_e/C-4321_e.gbc"));
        CHECK_EQ(br.db_text.find("{\"titles\":{\"C-4321_e\":{"), size_t(0));
        const Bytes tiny(10);
        bq.rom = tiny;  // too small
        CHECK(!BuildAdd(bq).ok);
        const Bytes vbrom = Gen(8, 0x80000);
        AddRequest vq;
        vq.system = System::Vb; vq.rom = vbrom; vq.file_name = "v.vb"; vq.db_text = "{\"titles\":{}}";
        const AddResult vr = BuildAdd(vq);
        CHECK(vr.ok && vr.covers[1].w == 768 && vr.covers[1].h == 224);
        vq.db_text = "oops";
        CHECK(!BuildAdd(vq).ok);

        // N64: v64 input -> .bnz (compress) of z64 + .dtz; unknown game = experimental
        Bytes z = WithMagic(Gen(4, 8192), 0, {0x80, 0x37, 0x12, 0x40});
        z = WithMagic(z, 0x10, {0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22, 0x33, 0x44});
        Bytes v64 = z;
        for (size_t i = 0; i < z.size(); i += 2) { v64[i] = z[i + 1]; v64[i + 1] = z[i]; }
        AddRequest zq;
        zq.system = System::N64Mature; zq.rom = v64; zq.file_name = "z.v64"; zq.db_text = ReadFile("db_n64_small.json"); zq.mtime = 42;
        AddResult zr = BuildAdd(zq);
        CHECK(zr.ok && zr.experimental && !zr.experimental_reason.empty());
        CHECK_EQ(zr.overlay_romfs, std::string("/atmosphere/contents/0100E0601C632000/romfs"));
        CHECK_EQ(zr.files.size(), size_t(2));
        CHECK(zr.files[0].compress && zr.files[0].kind == OutFile::Kind::Bytes && zr.files[0].data == z);
        CHECK_EQ(zr.files[0].path, "titles/" + zr.code + "/" + zr.code + ".bnz");
        CHECK(zr.files[1].compress && zr.files[1].path == "titles/" + zr.code + "/" + zr.code + ".dtz");
        CHECK(zr.files[1].data == BuildDtzCpio(false, nullptr, 42));
        CHECK(zr.entry_json.find("\"rom\":\"/titles/" + zr.code + "/" + zr.code + ".bin\"") != std::string::npos);
        // table hit -> not experimental, cfg from the table
        const u32 idle[1] = {0x80001194};
        const N64Config tab[] = {{0xAABBCCDD, 0x11223344, "FLASH", "None", "8M", 0, idle, 1}};
        zq.n64_table = tab;
        zr = BuildAdd(zq);
        CHECK(zr.ok && !zr.experimental);
        CHECK(zr.files[1].data == BuildDtzCpio(false, &tab[0], 42));
        // z64 input with raw .bin: pass-through, not compressed
        zq.rom = z; zq.n64_raw_bin = true;
        zr = BuildAdd(zq);
        CHECK(zr.ok && zr.files[0].kind == OutFile::Kind::Source && !zr.files[0].compress);
        CHECK_EQ(zr.files[0].path, "titles/" + zr.code + "/" + zr.code + ".bin");
    }
    return Finish("test_nso_plan");
}
