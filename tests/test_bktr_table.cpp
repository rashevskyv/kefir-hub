// LINK: sphaira/source/repack/bktr_table.cpp
#include "repack/bktr_table.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace sphaira::repack::bktr;

namespace {

template<typename V>
void Put(std::vector<uint8_t>& t, uint64_t at, V v) {
    std::memcpy(t.data() + at, &v, sizeof(v));
}

// Lays the entries out as the NCA stores them: L1 node, then one 0x4000 entry set per `per_set` entries.
std::vector<uint8_t> Table(uint64_t entry_size, size_t count, uint64_t end, auto write) {
    const auto per_set = (NODE_SIZE - 0x10) / entry_size;
    const auto sets = (count + per_set - 1) / per_set;
    std::vector<uint8_t> t(NODE_SIZE * (1 + sets));
    Put<uint32_t>(t, 4, sets);
    Put<uint64_t>(t, 8, end);
    for (size_t i = 0; i < count; i++) {
        const auto set = NODE_SIZE * (1 + i / per_set);
        Put<uint32_t>(t, set + 4, std::min<uint64_t>(per_set, count - i / per_set * per_set));
        write(t, set + 0x10 + (i % per_set) * entry_size, i);
    }
    return t;
}

std::vector<uint8_t> Indirect(size_t count, uint64_t step, uint32_t bad_storage_at = ~0u) {
    return Table(0x14, count, count * step, [&](auto& t, uint64_t at, size_t i) {
        Put<uint64_t>(t, at, i * step);
        Put<uint64_t>(t, at + 8, 0x1000000 + i);
        Put<uint32_t>(t, at + 0x10, i == bad_storage_at ? 2 : i % 2);
    });
}

void TestIndirect() {
    // 1000 entries need two entry sets (818 per set).
    const auto t = Indirect(1000, 0x100);
    std::vector<IndirectEntry> e;
    uint64_t end = 0;
    assert(ParseIndirect(t, 1000, e, end));
    assert(end == 1000 * 0x100);
    assert(e.size() == 1000);
    assert(e[818].offset == 818 * 0x100 && e[818].phys == 0x1000000 + 818 && e[818].storage == 0);
    assert(e[999].storage == 1);

    // A read that starts inside entry 2 and ends inside entry 4.
    using P = std::pair<uint64_t, uint64_t>;
    std::vector<P> pieces;
    assert(ForEachPiece(e, end, 0x2F0, 0x120, [&](const IndirectEntry& x, uint64_t off, uint64_t len) {
        pieces.emplace_back(x.offset, len);
        assert(off >= x.offset);
        return true;
    }));
    assert(pieces.size() == 3);
    assert((pieces[0] == P{0x200, 0x10}));
    assert((pieces[1] == P{0x300, 0x100}));
    assert((pieces[2] == P{0x400, 0x10}));

    // The last entry runs to `end`; past it is out of range.
    assert(ForEachPiece(e, end, end - 1, 1, [](auto&, auto, auto) { return true; }));
    assert(!ForEachPiece(e, end, end - 1, 2, [](auto&, auto, auto) { return true; }));
    assert(ForEachPiece(e, end, end, 0, [](auto&, auto, auto) { return true; }));

    // Malformed tables.
    assert(!ParseIndirect(t, 999, e, end));                          // count does not match the sets
    assert(!ParseIndirect(std::span(t).first(NODE_SIZE * 2), 1000, e, end)); // truncated
    assert(!ParseIndirect(Indirect(10, 0x100, 3), 10, e, end));      // storage index 2
    auto unsorted = Indirect(10, 0x100);
    Put<uint64_t>(unsorted, NODE_SIZE + 0x10 + 5 * 0x14, 0x50);
    assert(!ParseIndirect(unsorted, 10, e, end));
    assert(!ParseIndirect(t, 0, e, end));
}

void TestCtrEx() {
    const auto t = Table(0x10, 3, 0x30000, [](auto& t, uint64_t at, size_t i) {
        Put<uint64_t>(t, at, i * 0x10000);
        Put<uint32_t>(t, at + 8, 0x10000);
        Put<uint32_t>(t, at + 0xC, 7 + i);
    });
    std::vector<CtrExEntry> e;
    uint64_t end = 0;
    assert(ParseCtrEx(t, 3, e, end));
    assert(end == 0x30000 && e[2].offset == 0x20000 && e[2].generation == 9);

    std::vector<uint32_t> gens;
    assert(ForEachPiece(e, end, 0xFFF0, 0x10020, [&](const CtrExEntry& x, uint64_t, uint64_t) {
        gens.push_back(x.generation);
        return true;
    }));
    assert((gens == std::vector<uint32_t>{7, 8, 9}));

    static_assert(CtrUpper(0x1122334455667788ull, 0xA) == 0x112233440000000Aull);
}

} // namespace

int main() {
    TestIndirect();
    TestCtrEx();
    std::puts("test_bktr_table: ok");
}
