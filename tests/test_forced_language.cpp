// Host test for sphaira/include/forced_language.hpp (the pure parts).

#include "forced_language.hpp"

#include <cassert>
#include <cstdio>

int main() {
    using namespace sphaira::forced_language;

    // NACP order: bit i of supported_language_flag is LANGS[i].
    assert(IndexOf("en-US") == 0);
    assert(IndexOf("en-gb") == 1);
    assert(IndexOf("ja") == 2);
    assert(IndexOf("zh-Hans") == 14);
    assert(IndexOf("PT-BR") == 15);
    assert(IndexOf("uk") == -1);
    assert(IndexOf("") == -1);
    assert(IndexOf("en") == -1);

    assert(ParseTitleId("0100AC300919A000") == 0x0100AC300919A000ULL);
    assert(ParseTitleId("0100ac300919a000") == 0x0100AC300919A000ULL);
    assert(ParseTitleId("0100AC300919A00") == 0);
    assert(ParseTitleId("0100AC300919A00G") == 0);

    std::printf("ok  forced_language: all checks passed\n");
    return 0;
}
