// Host test for sphaira/include/demo/demo_cmd.hpp (DOCS_DEMO input commands from tools/docs/eden.ps1)
//
// g++ -std=c++20 -Wall -Wextra -Werror -I sphaira/include tests/test_demo_cmd.cpp -o /tmp/t && /tmp/t

#include "demo/demo_cmd.hpp"

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
    using namespace sphaira::demo;

    // every button name eden.ps1 knows, CRLF from PowerShell tolerated
    CHECK(ParseCmd("A").kind == Cmd::Button && ParseCmd("A").button == pad::A);
    CHECK(ParseCmd("Plus\r").button == pad::Plus);
    CHECK(ParseCmd("B\r\n").button == pad::B);  // a line from fgets keeps its newline
    CHECK(ParseCmd("ready\r\n").kind == Cmd::Ready);
    CHECK(ParseCmd("  Down ").button == pad::Down);
    CHECK(ParseCmd("ZR").button == pad::ZR);
    CHECK(ParseCmd("R3").button == pad::R3);
    CHECK(ParseCmd("R").button == pad::R);

    // names are case-sensitive and take no argument (eden.ps1 sends "A" n times, not "A 3")
    CHECK(ParseCmd("a").kind == Cmd::Invalid);
    CHECK(ParseCmd("A 3").kind == Cmd::Invalid);
    CHECK(ParseCmd("").kind == Cmd::Invalid);
    CHECK(ParseCmd("Start").kind == Cmd::Invalid);

    CHECK(ParseCmd("wait 1.5").kind == Cmd::Wait && ParseCmd("wait 1.5").seconds == 1.5);
    CHECK(ParseCmd("wait 2\r").seconds == 2.0);
    CHECK(ParseCmd("wait").kind == Cmd::Invalid);
    CHECK(ParseCmd("wait x").kind == Cmd::Invalid);
    CHECK(ParseCmd("wait -1").kind == Cmd::Invalid);

    CHECK(ParseCmd("lang uk").kind == Cmd::Lang && ParseCmd("lang uk").arg == "uk");
    CHECK(ParseCmd("lang  es419\r").arg == "es419");
    CHECK(ParseCmd("lang").kind == Cmd::Invalid);

    CHECK(ParseCmd("ready").kind == Cmd::Ready);
    CHECK(ParseCmd("ready now").kind == Cmd::Invalid);

    std::printf("test_demo_cmd: %d checks passed\n", g_checks);
    return 0;
}
