// host test for the shop index helpers (remote_shop.hpp). Run by tests/run.sh.
#include "remote_shop.hpp"
#include <cassert>
#include <cstdio>

using namespace sphaira::shop;

int main() {
    assert(UrlDecode("a%20b%5Bc%5D") == "a b[c]");
    assert(UrlDecode("100%") == "100%");        // a lone percent is kept
    assert(UrlDecode("%zz") == "%zz");

    std::string frag;
    assert(SplitFragment("http://h/a.nsp#Name%20X.nsp", frag) == "http://h/a.nsp" && frag == "Name%20X.nsp");
    assert(SplitFragment("http://h/a.nsp", frag) == "http://h/a.nsp" && frag.empty());

    // urls against the index they came from
    assert(ResolveUrl("http://h:8080/shop/index.json", "http://o/x.nsp") == "http://o/x.nsp");
    assert(ResolveUrl("http://h:8080/shop/index.json", "x.nsp") == "http://h:8080/shop/x.nsp");
    assert(ResolveUrl("http://h:8080/shop/", "sub/x.nsp") == "http://h:8080/shop/sub/x.nsp");
    assert(ResolveUrl("http://h:8080/shop/index.json?a=1#b", "/files/x.nsp") == "http://h:8080/files/x.nsp");
    assert(ResolveUrl("https://h", "x.nsp") == "https://h/x.nsp");
    assert(ResolveUrl("https://h/a/b", "//cdn/x.nsp") == "https://cdn/x.nsp");
    assert(ResolveUrl("https://h/", "").empty());

    // the name of a file
    assert(NameOf("http://h/a%20b.nsp", "", "Given.nsp") == "Given.nsp");
    assert(NameOf("http://h/get?id=1", "Game%20%5B1%5D.nsp", "") == "Game [1].nsp");
    assert(NameOf("http://h/dir/Game%20One.nsp?token=1", "", "") == "Game One.nsp");

    std::uint64_t id{};
    assert(ParseTitleId("Super Game [0100ABCDEF012000][v0].nsp", id) && id == 0x0100ABCDEF012000ull);
    assert(ParseTitleId("x [0100abcdef012800] y", id) && id == 0x0100ABCDEF012800ull);
    assert(!ParseTitleId("Game [0100ABCDEF01200].nsp", id));   // 15 digits
    assert(!ParseTitleId("Game [0100ABCDEF01200G].nsp", id));  // not hex
    assert(!ParseTitleId("Game.nsp", id));

    std::uint32_t version{};
    assert(ParseVersion("Game [0100ABCDEF012000][v65536].nsp", version) && version == 65536);
    assert(ParseVersion("Game [v0].nsp", version) && version == 0);
    assert(!ParseVersion("Game [v].nsp", version));
    assert(!ParseVersion("Game v5.nsp", version));

    assert(KindOf(0x0100ABCDEF012000ull) == Kind::Base);
    assert(KindOf(0x0100ABCDEF012800ull) == Kind::Update);
    assert(KindOf(0x0100ABCDEF013001ull) == Kind::Dlc);

    assert(Extension("a.NSP") == "nsp" && Extension("a.xcz") == "xcz" && Extension("a.nsz") == "nsz");
    assert(Extension("a.zip").empty() && Extension("noext").empty());

    // an entry as the index gave it
    const auto item = MakeItem("http://h/shop/", "files/g.nsp#Game%20%5B0100ABCDEF012000%5D%5Bv131072%5D.nsp", "", 1234);
    assert(item.url == "http://h/shop/files/g.nsp");
    assert(item.name == "Game [0100ABCDEF012000][v131072].nsp");
    assert(item.size == 1234 && item.title_id == 0x0100ABCDEF012000ull && item.has_version && item.version == 131072);

    std::puts("test_remote_shop: ok");
    return 0;
}
