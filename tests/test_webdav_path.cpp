// host test for the WebDAV path matching (webdav_path.hpp). Run by tests/run.sh.
#include "webdav_path.hpp"
#include <cassert>
#include <cstdio>

using sphaira::webdav::NormalizeUrlPath;

int main() {
    assert(NormalizeUrlPath("/").empty());
    assert(NormalizeUrlPath("").empty());
    assert(NormalizeUrlPath("//").empty());
    assert(NormalizeUrlPath("/base/") == "/base");
    assert(NormalizeUrlPath("/base") == "/base");
    assert(NormalizeUrlPath("//base//dir///") == "/base/dir");
    assert(NormalizeUrlPath("https://example.com/base/dir/") == "/base/dir");
    assert(NormalizeUrlPath("https://example.com").empty());
    assert(NormalizeUrlPath("https://example.com/") .empty());

    // the mount https://host/base: the folder itself comes back as /base/, a child as /base/file
    const auto requested = NormalizeUrlPath("/base/");
    assert(NormalizeUrlPath("/base/") == requested);
    assert(NormalizeUrlPath("https://host/base") == requested);
    assert(NormalizeUrlPath("/base/file") != requested);
    assert(NormalizeUrlPath("/base/base/") != requested);

    std::puts("test_webdav_path: ok");
    return 0;
}
