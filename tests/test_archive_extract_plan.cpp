// Host test for sphaira/include/archive_extract_plan.hpp

#include "archive_extract_plan.hpp"

#include <cassert>
#include <cstdio>

int main() {
    using sphaira::archive::OutputPath;

    // normal entries keep their relative path; windows separators and a leading slash are normalised.
    assert(OutputPath("dir/file.txt", false, "/a.rar") == "dir/file.txt");
    assert(OutputPath("dir\\sub\\file.bin", false, "/a.7z") == "dir/sub/file.bin");
    assert(OutputPath("/abs/file", false, "/a.tar") == "abs/file");
    assert(OutputPath("folder/", false, "/a.tar") == "folder");

    // nothing may climb out of the target folder.
    assert(!OutputPath("../evil", false, "/a.rar"));
    assert(!OutputPath("dir/../../evil", false, "/a.rar"));
    assert(!OutputPath("c:/evil", false, "/a.rar"));
    assert(!OutputPath("", false, "/a.rar"));

    // a bare compressed stream is named after the archive.
    assert(OutputPath("data", true, "/switch/game.log.gz") == "game.log");
    assert(OutputPath("data", true, "/backup.img.xz") == "backup.img");
    assert(OutputPath("data", true, "/noext") == "noext");
    assert(!OutputPath("data", true, "/.gz"));

    std::printf("ok  archive_extract_plan: all checks passed\n");
    return 0;
}
