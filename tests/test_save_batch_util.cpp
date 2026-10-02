// Host test for the batch-restore helpers in
// sphaira/include/ui/menus/save/save_batch_util.hpp (plan H6).
//
//     g++ -std=c++20 -Wall -Wextra -Werror -I sphaira/include tests/test_save_batch_util.cpp -o /tmp/t && /tmp/t

#include "ui/menus/save/save_batch_util.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace sphaira::ui::menu::save;

static int g_checks = 0;

#define CHECK(expr)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(expr)) {                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);       \
            return 1;                                                         \
        }                                                                     \
    } while (0)

struct Seed {
    std::string slot;   // BackupGroupKey: same value = same live save slot
    std::string source; // Kefir Hub / DBI / ...
    unsigned long long ts;
};

static void Collapse(std::vector<Seed>& seeds) {
    KeepNewestPerKey(seeds, [](const Seed& s) { return s.slot; }, [](const Seed& s) { return s.ts; });
}

// B5: one game backed up by two tools for the same account must be one restore item.
static int test_same_slot_two_sources() {
    std::vector<Seed> seeds{
        {"minecraft:uidA", "Kefir Hub", 20261001},
        {"minecraft:uidA", "DBI", 20260915},
        {"boxing:uidA", "DBI", 20260101},
    };
    Collapse(seeds);
    CHECK(seeds.size() == 2);
    CHECK(seeds[0].slot == "minecraft:uidA" && seeds[0].source == "Kefir Hub");
    CHECK(seeds[1].slot == "boxing:uidA");
    return 0;
}

// The newest wins wherever it sits, and takes the place of the first occurrence.
static int test_newest_wins_keeps_position() {
    std::vector<Seed> seeds{
        {"a", "old", 1},
        {"b", "only", 5},
        {"a", "new", 9},
        {"a", "mid", 4},
    };
    Collapse(seeds);
    CHECK(seeds.size() == 2);
    CHECK(seeds[0].slot == "a" && seeds[0].source == "new");
    CHECK(seeds[1].slot == "b");
    return 0;
}

// Equal timestamps: the first one stays (stable).
static int test_tie_keeps_first() {
    std::vector<Seed> seeds{{"a", "first", 7}, {"a", "second", 7}};
    Collapse(seeds);
    CHECK(seeds.size() == 1);
    CHECK(seeds[0].source == "first");
    return 0;
}

// Different accounts are different slots and both survive.
static int test_distinct_slots_untouched() {
    std::vector<Seed> seeds{{"g:uidA", "DBI", 1}, {"g:uidB", "DBI", 2}};
    Collapse(seeds);
    CHECK(seeds.size() == 2);
    std::vector<Seed> empty;
    Collapse(empty);
    CHECK(empty.empty());
    return 0;
}

// B5: two accounts named "nin10do" must not look identical in the picker.
static int test_duplicate_labels_get_tags() {
    std::vector<std::string> labels{"nin10do", "guest", "nin10do"};
    DisambiguateLabels(labels, {"B486", "0001", "9BBD"});
    CHECK(labels[0] == "nin10do (B486)");
    CHECK(labels[1] == "guest");
    CHECK(labels[2] == "nin10do (9BBD)");
    return 0;
}

static int test_unique_labels_unchanged() {
    std::vector<std::string> labels{"a", "b"};
    DisambiguateLabels(labels, {"1", "2"});
    CHECK(labels[0] == "a" && labels[1] == "b");
    // fewer tags than labels: the untagged tail is left alone, no out-of-range access.
    std::vector<std::string> same{"x", "x", "x"};
    DisambiguateLabels(same, {"1"});
    CHECK(same[0] == "x (1)" && same[1] == "x" && same[2] == "x");
    return 0;
}

int main() {
    if (test_same_slot_two_sources()) return 1;
    if (test_newest_wins_keeps_position()) return 1;
    if (test_tie_keeps_first()) return 1;
    if (test_distinct_slots_untouched()) return 1;
    if (test_duplicate_labels_get_tags()) return 1;
    if (test_unique_labels_unchanged()) return 1;
    std::printf("ok  save_batch_util: %d checks passed\n", g_checks);
    return 0;
}
