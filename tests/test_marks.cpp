// X / Y marks of a list (ui/marks.hpp): toggle and step, invert, set all, targets.
//   g++ -std=c++20 -I../sphaira/include test_marks.cpp -o t && ./t
#include "ui/marks.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace sphaira::ui::marks;

int main() {
    // rows 0 and 3 take no mark (a caption, a folder)
    std::vector<std::uint8_t> m(5, 0);
    const auto mark = [&](std::size_t i) -> std::uint8_t& { return m[i]; };
    const auto can = [](std::size_t i) { return i != 0 && i != 3; };

    // X marks the row and steps over the row that takes no mark
    assert(Toggle(m.size(), 2, mark, can) == 4);
    assert(m[2] == 1);
    // X on the last row: marked, the cursor stays
    assert(Toggle(m.size(), 4, mark, can) == 4);
    assert(m[4] == 1);
    // X on a row that takes no mark: no mark, the cursor still steps on
    assert(Toggle(m.size(), 0, mark, can) == 1);
    assert(m[0] == 0);
    // X again: the mark goes
    Toggle(m.size(), 2, mark, can);
    assert(m[2] == 0);
    // out of range: nothing changes
    assert(Toggle(m.size(), 7, mark, can) == 7);
    assert(Count(m.size(), mark) == 1);

    // Y: the markable rows flip, the others stay clear even if a stale mark was there
    m = {1, 0, 0, 1, 1};
    Invert(m.size(), mark, can);
    assert((m == std::vector<std::uint8_t>{0, 1, 1, 0, 0}));

    assert(Invert(m.size(), mark, can) == 1);
    assert((m == std::vector<std::uint8_t>{0, 0, 0, 0, 1}));

    assert(SetAll(m.size(), true, mark, can) == 3);
    assert((m == std::vector<std::uint8_t>{0, 1, 1, 0, 1}));
    assert((Marked(m.size(), mark, can) == std::vector<std::size_t>{1, 2, 4}));
    assert(SetAll(m.size(), false, mark, can) == 0);
    assert(Count(m.size(), mark) == 0);

    // R2 + Y: some marked -> all; all marked -> none; a row that takes no mark stays clear
    m = {1, 1, 0, 0, 0};
    assert(All(m.size(), mark, can) == 3);
    assert((m == std::vector<std::uint8_t>{0, 1, 1, 0, 1}));
    assert(All(m.size(), mark, can) == 0);
    assert(Count(m.size(), mark) == 0);

    // targets: the cursor row when nothing is marked; none on a row that takes no mark; else the marks
    assert((Targets(m.size(), 2, mark, can) == std::vector<std::size_t>{2}));
    assert(Targets(m.size(), 3, mark, can).empty());
    m[1] = m[4] = 1;
    assert((Targets(m.size(), 2, mark, can) == std::vector<std::size_t>{1, 4}));

    // the default: every row takes a mark; a bool mark works too
    bool c[3]{};
    const auto cm = [&](std::size_t i) -> bool& { return c[i]; };
    assert(Toggle(3, 0, cm) == 1);
    Invert(3, cm);
    assert(Count(3, cm) == 2);

    std::puts("test_marks: ok");
    return 0;
}
