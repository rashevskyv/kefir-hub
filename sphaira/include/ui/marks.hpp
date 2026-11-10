#pragma once
// X / Y marks of a list: one rule for every list with X/Y select. X flips the mark of the row under the cursor and
// the cursor steps to the next row that takes a mark; Y inverts, R2 + Y marks all or none; an action works on the
// marked rows, or on the row under the cursor when none is marked. A list keeps the mark in its own rows: `mark(i)` returns it as a reference
// (bool& or u8&), `can(i)` says whether row i takes a mark (a folder, a caption or a parent entry may not).
// Drawing a marked row is gfx::drawMarkedRow.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sphaira::ui::marks {

inline constexpr auto AnyRow = [](std::size_t) { return true; };

// the mark of a list whose rows keep it in a `selected` field
template<typename Rows>
auto Selected(Rows& rows) {
    return [&rows](std::size_t i) -> auto& { return rows[i].selected; };
}

template<typename Mark>
auto Count(std::size_t rows, Mark mark) -> std::size_t {
    std::size_t n = 0;
    for (std::size_t i = 0; i < rows; i++) {
        n += mark(i) ? 1 : 0;
    }
    return n;
}

// X: flips the mark of `index` when that row takes one. Returns the row the cursor goes to: the next row below that
// takes a mark, or `index` when there is none.
template<typename Mark, typename Can = decltype(AnyRow)>
auto Toggle(std::size_t rows, std::int64_t index, Mark mark, Can can = AnyRow) -> std::int64_t {
    if (index < 0 || static_cast<std::size_t>(index) >= rows) {
        return index;
    }
    const auto at = static_cast<std::size_t>(index);
    if (can(at)) {
        mark(at) = !mark(at);
    }
    for (auto i = at + 1; i < rows; i++) {
        if (can(i)) return static_cast<std::int64_t>(i);
    }
    return index;
}

// every row that takes a mark gets `value`; a row that takes none has none. Returns the number of marks.
template<typename Mark, typename Can = decltype(AnyRow)>
auto SetAll(std::size_t rows, bool value, Mark mark, Can can = AnyRow) -> std::size_t {
    for (std::size_t i = 0; i < rows; i++) {
        mark(i) = value && can(i);
    }
    return Count(rows, mark);
}

// Y: every row that takes a mark gets the opposite one; a row that takes none has none. Returns the number of marks.
template<typename Mark, typename Can = decltype(AnyRow)>
auto Invert(std::size_t rows, Mark mark, Can can = AnyRow) -> std::size_t {
    for (std::size_t i = 0; i < rows; i++) {
        mark(i) = can(i) && !mark(i);
    }
    return Count(rows, mark);
}

// R2 + Y: every row that takes a mark gets one, or none when every such row has one already. Returns the number of
// marks.
template<typename Mark, typename Can = decltype(AnyRow)>
auto All(std::size_t rows, Mark mark, Can can = AnyRow) -> std::size_t {
    std::size_t marked = 0, markable = 0;
    for (std::size_t i = 0; i < rows; i++) {
        if (can(i)) {
            markable++;
            marked += mark(i) ? 1 : 0;
        }
    }
    return SetAll(rows, marked != markable, mark, can);
}

// true while R2 is held (app.cpp); a list never reads it itself, OnY does
auto WholeList() -> bool;

// Y of every list: R2 + Y marks all or none (All), Y alone inverts (Invert). Returns the number of marks.
template<typename Mark, typename Can = decltype(AnyRow)>
auto OnY(std::size_t rows, Mark mark, Can can = AnyRow) -> std::size_t {
    return WholeList() ? All(rows, mark, can) : Invert(rows, mark, can);
}

// the marked rows only
template<typename Mark, typename Can = decltype(AnyRow)>
auto Marked(std::size_t rows, Mark mark, Can can = AnyRow) -> std::vector<std::size_t> {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < rows; i++) {
        if (mark(i) && can(i)) out.push_back(i);
    }
    return out;
}

// what an action works on: the marked rows, or the row under the cursor when none is marked and it takes a mark
template<typename Mark, typename Can = decltype(AnyRow)>
auto Targets(std::size_t rows, std::int64_t index, Mark mark, Can can = AnyRow) -> std::vector<std::size_t> {
    auto out = Marked(rows, mark, can);
    if (out.empty() && index >= 0 && static_cast<std::size_t>(index) < rows && can(static_cast<std::size_t>(index))) {
        out.push_back(static_cast<std::size_t>(index));
    }
    return out;
}

} // namespace sphaira::ui::marks
