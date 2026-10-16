// Cheat files as Atmosphere's dmnt accepts them: one bad part drops the whole file.
//   g++ -std=c++20 -I../sphaira/include test_cheat_text.cpp -o t && ./t
#include "ui/menus/cheats/cheat_text.hpp"

#include <cassert>
#include <cstdio>
#include <string>

using sphaira::ui::menu::hats::detail::SanitizeCheatText;
using sphaira::ui::menu::hats::detail::DMNT_MAX_CHEATS;
using sphaira::ui::menu::hats::detail::DMNT_MAX_OPCODES;
using sphaira::ui::menu::hats::detail::MergeCheatText;
using sphaira::ui::menu::hats::detail::ParsePackCheatPath;

namespace {

auto Count(const std::string& s, const std::string& what) -> size_t {
    size_t n = 0;
    for (auto pos = s.find(what); pos != std::string::npos; pos = s.find(what, pos + 1)) {
        n++;
    }
    return n;
}

void CleansNotesAndComments() {
    const auto out = SanitizeCheatText(
        "\xEF\xBB\xBF" "04000000 00000000 00000063\r\n"  // code before any header: dmnt fails
        "[Infinite HP [by X]]\r\n"
        "(note)\r\n"
        "// comment\r\n"
        "04000000 0a1b2c3d 0000ffff // inline\r\n"
        "\r\n"
        "[Empty]\r\n"
        "[Bad words]\r\n"
        "0400000 123\r\n");
    assert(out.text == "[Infinite HP by X]\n04000000 0A1B2C3D 0000FFFF\n\n");
    assert(out.cheats == 1 && out.dropped == 0);
}

void KeepsOneMaster() {
    const auto out = SanitizeCheatText(
        "{Master}\n80000000 00000001\n"
        "[A]\n04000000 00000000 00000001\n"
        "{Master}\n80000000 00000002\n");
    assert(Count(out.text, "{Master}") == 1);
    assert(out.text.find("00000002") == std::string::npos);
    assert(out.cheats == 1 && out.dropped == 1);
}

void CapsCheatCount() {
    std::string in;
    for (size_t i = 0; i < DMNT_MAX_CHEATS + 3; i++) {
        in += "[C" + std::to_string(i) + "]\n04000000 00000000 00000001\n";
    }
    in += "{M}\n80000000 00000001\n";  // the master has its own slot
    const auto out = SanitizeCheatText(in);
    assert(out.cheats == DMNT_MAX_CHEATS && out.dropped == 3);
    assert(Count(out.text, "[C") == DMNT_MAX_CHEATS && Count(out.text, "{M}") == 1);
}

void DropsOversizedCheat() {
    std::string big = "[Big]\n";
    for (size_t i = 0; i < DMNT_MAX_OPCODES / 4 + 1; i++) {
        big += "11111111 22222222 33333333 44444444\n";
    }
    std::string fits = "[Fits]\n";
    for (size_t i = 0; i < DMNT_MAX_OPCODES / 4; i++) {
        fits += "11111111 22222222 33333333 44444444\n";
    }
    const auto out = SanitizeCheatText(big + fits);
    assert(out.text.find("[Big]") == std::string::npos);
    assert(out.text.find("[Fits]") != std::string::npos);
    assert(out.cheats == 1 && out.dropped == 1);
}

void RejectsUtf16() {
    const std::string utf16("\xFF\xFE[\0A\0]\0\n\0", 10);
    assert(SanitizeCheatText(utf16).text.empty());
}

void MergeKeepsUserCheats() {
    const std::string user = "{Master}\n04000000 00000000 00000001\n\n[Infinite HP]\n04000000 00000010 00000063\n";
    const std::string pack = "{Pack master}\n04000000 00000000 00000002\n\n[Infinite HP]\n04000000 00000010 00000000\n\n"
                             "[60 FPS]\n04000000 00000020 0000003C\n";
    const auto out = MergeCheatText(user, pack);
    assert(out.added == 1);
    assert(out.text.find("00000063") != std::string::npos);  // the user's version of a same-name cheat stays
    assert(out.text.find("[60 FPS]") != std::string::npos);
    assert(out.text.find("{Pack master}") == std::string::npos && out.dropped == 1); // one master only
    assert(MergeCheatText(out.text, pack).added == 0);         // a second install adds nothing
    assert(MergeCheatText("", pack).added == 2);                // no file yet: the pack as it is
}

void ParsesPackPaths() {
    const auto p = ParsePackCheatPath("contents/01009790186FE000/cheats/8915848314513cbd.txt");
    assert(p && p->title_id == 0x01009790186FE000ULL && p->build_id == "8915848314513CBD");
    assert(ParsePackCheatPath("titles/01009790186FE000/cheats/8915848314513CBD.txt"));
    assert(!ParsePackCheatPath("contents/01009790186FE000/Smurfs Kart.txt"));
    assert(!ParsePackCheatPath("contents/01009790186FE000/cheats/short.txt"));
    assert(!ParsePackCheatPath("contents/0100979018/cheats/8915848314513CBD.txt"));
}

} // namespace

int main() {
    CleansNotesAndComments();
    KeepsOneMaster();
    CapsCheatCount();
    DropsOversizedCheat();
    RejectsUtf16();
    MergeKeepsUserCheats();
    ParsesPackPaths();
    std::puts("test_cheat_text: ok");
}
