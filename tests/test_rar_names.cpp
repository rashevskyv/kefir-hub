// rar_reader.hpp pure helpers: which sets go to UnRAR, name and time conversion.
#include "archive_volumes.hpp"
#include "rar_reader.hpp"

#include <cassert>
#include <cstdio>

using namespace sphaira;

int main() {
    // backend choice: by the first volume of the set.
    assert(rar::IsRarName("a.rar"));
    assert(rar::IsRarName("/x/Game.PART1.RAR"));
    assert(!rar::IsRarName("a.7z"));
    assert(!rar::IsRarName("a.r00"));
    assert(!rar::IsRarName("rar"));
    assert(!rar::IsRarName(""));
    const std::vector<std::string> sib{"g.rar", "g.r00", "g.r01", "h.part1.rar", "h.part2.rar"};
    assert(rar::IsRarName(archive::Volumes("g.r01", sib).front()));
    assert(archive::Volumes("h.part2.rar", sib).front() == "h.part1.rar");
    assert(!rar::IsRarName(archive::Volumes("lone.r05", {"lone.r05"}).front())); // no .rar: libarchive

    // UTF-32 -> UTF-8, all lengths; surrogates and out-of-range become '?'.
    assert(rar::WideToUtf8(L"abc") == "abc");
    assert(rar::WideToUtf8(L"тест") == "\xD1\x82\xD0\xB5\xD1\x81\xD1\x82");
    assert(rar::WideToUtf8(L"€") == "\xE2\x82\xAC");
    assert(rar::WideToUtf8(L"\U0001F600") == "\xF0\x9F\x98\x80");
    const wchar_t bad[] = {static_cast<wchar_t>(0xD800), static_cast<wchar_t>(0x110000), L'x', 0};
    assert(rar::WideToUtf8(bad) == "??x");
    assert(rar::WideToUtf8(nullptr).empty());

    // FILETIME -> unix.
    assert(rar::FileTimeToUnix(0) == 0);
    assert(rar::FileTimeToUnix(116444736000000000ULL) == 0);                 // 1970-01-01
    assert(rar::FileTimeToUnix(116444736000000000ULL + 10000000ULL) == 1);
    assert(rar::FileTimeToUnix(133485408000000000ULL) == 1704067200);        // 2024-01-01
    assert(rar::FileTimeToUnix(1000) == 0);                                   // before 1970

    std::puts("test_rar_names: ok");
    return 0;
}
