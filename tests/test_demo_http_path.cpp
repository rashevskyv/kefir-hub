// Host test for sphaira/include/demo/demo_http_path.hpp (DOCS_DEMO http fixture names)
//
// g++ -std=c++20 -Wall -Wextra -Werror -I sphaira/include tests/test_demo_http_path.cpp -o /tmp/t && /tmp/t

#include "demo/demo_http_path.hpp"

#include <cstdio>

static int g_checks = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(expr)) {                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);       \
            return 1;                                                         \
        }                                                                     \
    } while (0)

int main() {
    using sphaira::demo::HttpFixtureName;
    using sphaira::demo::Fnv1a32;

    CHECK(HttpFixtureName("https://api.github.com/repos/a/b/releases/latest", "") == "api.github.com/repos/a/b/releases/latest");
    CHECK(HttpFixtureName("http://10.0.0.2:8465/api/shop", "") == "10.0.0.2_8465/api/shop");
    CHECK(HttpFixtureName("http://10.0.0.2:8465", "") == "10.0.0.2_8465/index");
    CHECK(HttpFixtureName("http://user:pw@host/a:b", "") == "user_pw@host/a:b");
    CHECK(HttpFixtureName("https://host/dir/", "") == "host/dir/index");
    // query: kept characters stay, the rest become '_' (fixtures live in git on Windows: no '?', '&', ':')
    CHECK(HttpFixtureName("https://host/list?page=2&sort=new", "") == "host/list_page=2_sort=new");
    CHECK(HttpFixtureName("https://host/q?a=b%20c", "") == "host/q_a=b_20c");
    // POST: same path plus the body hash
    CHECK(Fnv1a32("") == 2166136261u);
    CHECK(Fnv1a32("a") == 0xe40c292cu);
    CHECK(HttpFixtureName("https://host/graphql", "a") == "host/graphql.e40c292c");
    CHECK(HttpFixtureName("no-scheme/path", "") == "no-scheme/path");
    CHECK(HttpFixtureName("https://host?x=1", "") == "host/_x=1");

    std::printf("test_demo_http_path: %d checks passed\n", g_checks);
    return 0;
}
