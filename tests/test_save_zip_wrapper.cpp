// host test for the wrapper folder of a save zip (save_zip_wrapper.hpp). Run by tests/run.sh.
#include "save_zip_wrapper.hpp"
#include <cassert>
#include <cstdio>

using namespace sphaira::savezip;

namespace {
const std::vector<std::string_view> ROOT{".nx_save_meta.bin", ".dbi_save_extra", ".dbi_save_info.ini"};
auto Detect(const std::vector<std::string>& n) { return DetectWrapperPrefix(n, ".nx_save_meta.bin", ROOT); }
}

int main() {
    // wrapped: the meta is inside the folder and so is everything else
    assert(Detect({"Backup/", "Backup/.nx_save_meta.bin", "Backup/file0", "Backup/sub/", "Backup/sub/file1"}) == "Backup/");
    assert(Detect({"Backup/.NX_SAVE_META.BIN", "Backup/a"}) == "Backup/");
    // macOS leftovers do not break it
    assert(Detect({"Backup/.nx_save_meta.bin", "Backup/a", "__MACOSX/Backup/._a", ".DS_Store"}) == "Backup/");

    // not wrapped: the meta is at the root
    assert(Detect({".nx_save_meta.bin", "file0", "Backup/file1"}).empty());
    assert(Detect({".dbi_save_extra", "Backup/.nx_save_meta.bin", "Backup/a"}).empty());
    // a save that really holds one folder, no meta inside it
    assert(Detect({"Folder/file0", "Folder/file1"}).empty());
    // the meta is two folders deep
    assert(Detect({"A/B/.nx_save_meta.bin", "A/B/x"}).empty());
    // something outside the folder
    assert(Detect({"Backup/.nx_save_meta.bin", "Backup/a", "other"}).empty());
    // two folders with a meta each
    assert(Detect({"A/.nx_save_meta.bin", "B/.nx_save_meta.bin"}).empty());
    assert(Detect({}).empty());

    // the name inside the wrapper
    assert(StripWrapper("Backup/file0", "Backup/").value() == "file0");
    assert(StripWrapper("Backup/sub/", "Backup/").value() == "sub/");
    assert(!StripWrapper("Backup/", "Backup/").has_value());
    assert(!StripWrapper("__MACOSX/x", "Backup/").has_value());
    assert(StripWrapper("anything", "").value() == "anything");

    std::puts("test_save_zip_wrapper: ok");
    return 0;
}
