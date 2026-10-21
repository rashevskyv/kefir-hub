// Host test for sphaira/include/web_favorites.hpp

#include "web_favorites.hpp"

#include <cassert>
#include <cstdio>

using namespace sphaira::web;

int main() {
    // the browser is offered on firmware 19.x and before, and 22.5.1 and after (23.0.1 included), not between
    assert(BrowserWorks(19, 0, 1) && BrowserWorks(18, 1, 0) && BrowserWorks(23, 0, 1) && BrowserWorks(22, 5, 1) && BrowserWorks(22, 6, 0));
    assert(!BrowserWorks(20, 0, 0) && !BrowserWorks(21, 2, 0) && !BrowserWorks(22, 0, 0) && !BrowserWorks(22, 5, 0));

    // addresses: a missing scheme is added, spaces and the bare keyboard text are refused
    assert(NormalizeUrl("example.com/page") == "https://example.com/page");
    assert(NormalizeUrl("  http://example.com \n") == "http://example.com");
    assert(NormalizeUrl("https://").empty() && NormalizeUrl("").empty() && NormalizeUrl("a b.com").empty());
    assert(HostOf("https://www.example.com/a/b?q=1") == "www.example.com" && HostOf("example.com:8080/x") == "example.com:8080");

    // a list: add, no twice, remove
    std::vector<Favourite> list;
    assert(Add(list, "example.com") && list.size() == 1 && list[0].title == "example.com");
    assert(!Add(list, "https://example.com") && list.size() == 1);           // the same address
    assert(Add(list, "https://news.site/x", "News\tSite") && list[1].title == "News Site"); // a tab in a title becomes a space
    assert(!Add(list, "  ") && !Add(list, "https://"));
    assert(Remove(list, 0) && list.size() == 1 && list[0].url == "https://news.site/x");
    assert(!Remove(list, 5));

    // the file: round trip, and a hand-edited one (CRLF, no title, a broken line) still loads
    const auto text = Format(list);
    const auto back = Parse(text);
    assert(back.size() == 1 && back[0].url == list[0].url && back[0].title == list[0].title);
    const auto hand = Parse("example.org\r\nhttps://a.b/c\tMy page\r\n\r\nbad line with spaces\r\n");
    assert(hand.size() == 2 && hand[0].url == "https://example.org" && hand[0].title == "example.org");
    assert(hand[1].title == "My page");

    std::puts("test_web_favorites: ok");
    return 0;
}
