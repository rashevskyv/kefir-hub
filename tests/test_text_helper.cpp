#include "text_helper.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

int main() {
    using namespace text_helper;

    // case-insensitive known text names
    assert(IsTextFile("test.txt"));
    assert(IsTextFile("config.INI"));
    assert(IsTextFile("settings.JSON"));
    assert(IsTextFile("script.Py"));
    assert(IsTextFile("README"));
    assert(IsTextFile("readme.txt"));
    assert(IsTextFile("CMakeLists.txt"));
    assert(IsTextFile("Makefile"));
    assert(IsTextFile(".gitignore"));
    assert(IsTextFile("/sdmc/switch/config.toml"));

    // rejection of representative binary names
    assert(!IsTextFile("app.nro"));
    assert(!IsTextFile("game.nsp"));
    assert(!IsTextFile("rom.xci"));
    assert(!IsTextFile("image.png"));
    assert(!IsTextFile("archive.zip"));
    assert(!IsTextFile("program.exe"));
    assert(!IsTextFile("data.bin"));

    // INI boolean toggle tests:
    // true -> false
    auto r1 = ToggleIniBoolean("setting = true");
    assert(r1.toggled && r1.new_line == "setting = false");

    // false -> true
    auto r2 = ToggleIniBoolean("setting = false");
    assert(r2.toggled && r2.new_line == "setting = true");

    // uppercase preservation
    auto r3 = ToggleIniBoolean("  setting  =   TRUE   ; inline comment");
    assert(r3.toggled && r3.new_line == "  setting  =   FALSE   ; inline comment");

    // titlecase preservation
    auto r4 = ToggleIniBoolean("flag = False # hash comment");
    assert(r4.toggled && r4.new_line == "flag = True # hash comment");

    // comment/section lines rejected
    auto r5 = ToggleIniBoolean("; setting = true");
    assert(!r5.toggled);

    auto r6 = ToggleIniBoolean("# setting = true");
    assert(!r6.toggled);

    auto r7 = ToggleIniBoolean("[section_name]");
    assert(!r7.toggled);

    // rejection of truthful/falsehood/quoted
    auto r8 = ToggleIniBoolean("setting = truthful");
    assert(!r8.toggled);

    auto r9 = ToggleIniBoolean("setting = falsehood");
    assert(!r9.toggled);

    auto r10 = ToggleIniBoolean("setting = \"true\"");
    assert(!r10.toggled);

    // Atmosphère typed flag toggles: u8!0x0 <-> u8!0x1
    auto r11 = ToggleIniBoolean("dmnt_cheats_enabled = u8!0x0");
    assert(r11.toggled && r11.new_line == "dmnt_cheats_enabled = u8!0x1");

    auto r12 = ToggleIniBoolean("dmnt_cheats_enabled = u8!0x1 ; comment");
    assert(r12.toggled && r12.new_line == "dmnt_cheats_enabled = u8!0x0 ; comment");

    auto r13 = ToggleIniBoolean("flag = U8!0X0");
    assert(r13.toggled && r13.new_line == "flag = U8!0X1");

    // Plain 0 / 1 toggling
    auto r14 = ToggleIniBoolean("autostart = 0");
    assert(r14.toggled && r14.new_line == "autostart = 1");

    auto r15 = ToggleIniBoolean("autostart = 1");
    assert(r15.toggled && r15.new_line == "autostart = 0");

    auto r16 = ToggleIniBoolean("  enabled = 0   ; trailing comment");
    assert(r16.toggled && r16.new_line == "  enabled = 1   ; trailing comment");

    auto r17 = ToggleIniBoolean("flag = 1 # hash comment");
    assert(r17.toggled && r17.new_line == "flag = 0 # hash comment");

    auto r18 = ToggleIniBoolean("debugmode=0");
    assert(r18.toggled && r18.new_line == "debugmode=1");

    auto r19 = ToggleIniBoolean("enable=1");
    assert(r19.toggled && r19.new_line == "enable=0");

    // Typed flag rejection cases & multi-digit numbers:
    assert(!ToggleIniBoolean("flag = u8!0x2").toggled);
    assert(!ToggleIniBoolean("flag = u32!0x0").toggled);
    assert(!ToggleIniBoolean("flag = u8!0x01").toggled);
    assert(!ToggleIniBoolean("flag = \"u8!0x0\"").toggled);
    assert(!ToggleIniBoolean("; flag = u8!0x0").toggled);
    assert(!ToggleIniBoolean("flag = my_u8!0x0_value").toggled);
    assert(!ToggleIniBoolean("count = 10").toggled);
    assert(!ToggleIniBoolean("count = 01").toggled);
    assert(!ToggleIniBoolean("count = 0x0").toggled);
    assert(!ToggleIniBoolean("count = 0x1").toggled);
    assert(!ToggleIniBoolean("count = 2").toggled);
    assert(!ToggleIniBoolean("; count = 0").toggled);

    // INI comment / uncomment tests:
    // 1. Commenting with indentation preservation
    assert(CommentIniLine("flag = 1") == ";flag = 1");
    assert(CommentIniLine("  setting = value") == "  ;setting = value");
    assert(CommentIniLine("\tautostart = 1") == "\t;autostart = 1");
    assert(CommentIniLine("  [section]") == "  ;[section]");

    // 2. Commenting blank and already-commented lines (intact)
    assert(CommentIniLine("") == "");
    assert(CommentIniLine("   ") == "   ");
    assert(CommentIniLine("\t\t") == "\t\t");
    assert(CommentIniLine("; already commented") == "; already commented");
    assert(CommentIniLine("  ; already commented") == "  ; already commented");
    assert(CommentIniLine("  # hash comment") == "  # hash comment");

    // 3. Uncommenting with indentation preservation
    assert(UncommentIniLine(";flag = 1") == "flag = 1");
    assert(UncommentIniLine("  ;setting = value") == "  setting = value");
    assert(UncommentIniLine("  ; setting = value") == "   setting = value");
    assert(UncommentIniLine("  #autostart = 1") == "  autostart = 1");
    assert(UncommentIniLine("\t#\tkey = val") == "\t\tkey = val");

    // 4. Uncommenting blank and non-comment negative cases (intact)
    assert(UncommentIniLine("") == "");
    assert(UncommentIniLine("   ") == "   ");
    assert(UncommentIniLine("setting = value") == "setting = value");
    assert(UncommentIniLine("  setting = value") == "  setting = value");
    assert(UncommentIniLine("[section]") == "[section]");
    assert(UncommentIniLine("  [section]") == "  [section]");

    // ReadPage pager unit tests:
    // 1. Multi-line LF paging
    std::string sample_lf = "line 1\nline 2\nline 3\nline 4\nline 5\n";
    auto read_mem = [&](int64_t off, char* buf, int64_t sz) -> int64_t {
        if (off >= static_cast<int64_t>(sample_lf.size())) return 0;
        int64_t n = std::min<int64_t>(sz, sample_lf.size() - off);
        std::memcpy(buf, sample_lf.data() + off, n);
        return n;
    };
    auto p1 = ReadPage(read_mem, sample_lf.size(), 0, 1, 2);
    assert(p1.lines.size() == 2);
    assert(p1.lines[0] == "line 1");
    assert(p1.lines[1] == "line 2");
    assert(!p1.is_eof);
    assert(p1.start_line == 1);
    assert(p1.end_offset == 14);

    auto p2 = ReadPage(read_mem, sample_lf.size(), p1.end_offset, 3, 2);
    assert(p2.lines.size() == 2);
    assert(p2.lines[0] == "line 3");
    assert(p2.lines[1] == "line 4");
    assert(!p2.is_eof);
    assert(p2.start_line == 3);

    auto p3 = ReadPage(read_mem, sample_lf.size(), p2.end_offset, 5, 2);
    assert(p3.lines.size() == 1);
    assert(p3.lines[0] == "line 5");
    assert(p3.is_eof);

    // 2. CRLF line endings
    std::string sample_crlf = "alpha\r\nbeta\r\ngamma\r\n";
    auto read_crlf = [&](int64_t off, char* buf, int64_t sz) -> int64_t {
        if (off >= static_cast<int64_t>(sample_crlf.size())) return 0;
        int64_t n = std::min<int64_t>(sz, sample_crlf.size() - off);
        std::memcpy(buf, sample_crlf.data() + off, n);
        return n;
    };
    auto pc1 = ReadPage(read_crlf, sample_crlf.size(), 0, 1, 10);
    assert(pc1.lines.size() == 3);
    assert(pc1.lines[0] == "alpha");
    assert(pc1.lines[1] == "beta");
    assert(pc1.lines[2] == "gamma");
    assert(pc1.is_eof);

    // 3. Empty file
    auto read_empty = [](int64_t, char*, int64_t) -> int64_t { return 0; };
    auto pe = ReadPage(read_empty, 0, 0, 1, 10);
    assert(pe.lines.size() == 1);
    assert(pe.lines[0] == "");
    assert(pe.is_eof);

    // 4. Long line capping
    std::string long_line(500, 'x');
    auto read_long = [&](int64_t off, char* buf, int64_t sz) -> int64_t {
        if (off >= static_cast<int64_t>(long_line.size())) return 0;
        int64_t n = std::min<int64_t>(sz, long_line.size() - off);
        std::memcpy(buf, long_line.data() + off, n);
        return n;
    };
    auto pl = ReadPage(read_long, long_line.size(), 0, 1, 10, 0, 100);
    assert(pl.lines.size() == 5);
    assert(pl.lines[0].size() == 100);
    assert(pl.lines[0] == std::string(100, 'x'));

    // 5. Premature/error reader returning 0 before EOF
    auto read_fail = [](int64_t, char*, int64_t) -> int64_t { return 0; };
    auto pf = ReadPage(read_fail, 1000, 0, 1, 10);
    assert(!pf.is_eof);
    assert(pf.is_error);
    assert(pf.end_offset == 0);

    // 6. Premature short read terminating before page rows filled
    int call_count = 0;
    auto read_short = [&](int64_t, char* buf, int64_t) -> int64_t {
        if (call_count++ == 0) {
            std::string line = "hello\n";
            std::memcpy(buf, line.data(), line.size());
            return line.size();
        }
        return 0; // premature failure on second chunk
    };
    auto ps = ReadPage(read_short, 1000, 0, 1, 10);
    assert(!ps.is_eof);
    assert(ps.is_error);
    assert(ps.lines.size() == 1);
    assert(ps.lines[0] == "hello");
    assert(ps.end_offset == 6);

    // 7. Buffered page with one-viewport logical offset discovery
    std::string sample_stream = "line 1\nline 2\nline 3\nline 4\nline 5\nline 6\nline 7\n";
    auto read_stream = [&](int64_t off, char* buf, int64_t sz) -> int64_t {
        if (off >= static_cast<int64_t>(sample_stream.size())) return 0;
        int64_t n = std::min<int64_t>(sz, sample_stream.size() - off);
        std::memcpy(buf, sample_stream.data() + off, n);
        return n;
    };
    // viewport = 2 rows, buffer = 4 rows
    auto sp0 = ReadPage(read_stream, sample_stream.size(), 0, 1, 4, 2);
    assert(sp0.lines.size() == 4);
    assert(sp0.lines[0] == "line 1");
    assert(sp0.lines[1] == "line 2");
    assert(sp0.lines[2] == "line 3");
    assert(sp0.lines[3] == "line 4");
    assert(sp0.start_offset == 0);
    assert(sp0.start_line == 1);
    assert(sp0.logical_end_offset == 14); // after "line 1\nline 2\n"
    assert(sp0.logical_end_line == 3);
    assert(!sp0.is_eof);

    auto sp1 = ReadPage(read_stream, sample_stream.size(), sp0.logical_end_offset, sp0.logical_end_line, 4, 2);
    assert(sp1.lines.size() == 4);
    assert(sp1.lines[0] == "line 3");
    assert(sp1.lines[1] == "line 4");
    assert(sp1.lines[2] == "line 5");
    assert(sp1.lines[3] == "line 6");
    assert(sp1.start_offset == 14);
    assert(sp1.start_line == 3);
    assert(sp1.logical_end_offset == 28); // after "line 3\nline 4\n"
    assert(sp1.logical_end_line == 5);
    assert(!sp1.is_eof);

    auto sp2 = ReadPage(read_stream, sample_stream.size(), sp1.logical_end_offset, sp1.logical_end_line, 4, 2);
    assert(sp2.lines.size() == 3);
    assert(sp2.lines[0] == "line 5");
    assert(sp2.lines[1] == "line 6");
    assert(sp2.lines[2] == "line 7");
    assert(sp2.is_eof);

    std::cout << "ok  text_helper: all checks passed\n";
    return 0;
}
