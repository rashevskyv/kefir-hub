#include "thumb_cache.hpp"
#include <cstdio>
#include <cstdlib>

using namespace sphaira::thumb_cache;

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)

int main() {
    // under the cap: nothing goes
    CHECK(PlanTrim({{"a", 100, 1}, {"b", 100, 2}}, 200).empty());
    CHECK(PlanTrim({}, 10).empty());

    // over the cap: the oldest go first, down to 90% of the cap
    {
        std::vector<Entry> f{{"new", 500, 30}, {"old", 500, 10}, {"mid", 500, 20}}; // 1500 > 1000
        const auto del = PlanTrim(f, 1000);
        CHECK(del.size() == 2);                 // 1500 -> 1000 -> 500 (<= 900)
        CHECK(del[0] == "old" && del[1] == "mid");
    }
    // stops as soon as the total is <= 90%
    {
        std::vector<Entry> f{{"a", 300, 1}, {"b", 300, 2}, {"c", 300, 3}, {"d", 300, 4}}; // 1200, cap 1000
        const auto del = PlanTrim(f, 1000);
        CHECK(del.size() == 1 && del[0] == "a");  // 900 <= 900
    }
    // equal times: ordered by path, deterministic
    {
        std::vector<Entry> f{{"z", 600, 5}, {"y", 600, 5}};
        const auto del = PlanTrim(f, 1000);
        CHECK(del.size() == 1 && del[0] == "y");
    }
    // the kept file survives even when it is the oldest
    {
        std::vector<Entry> f{{"keep", 600, 1}, {"b", 600, 2}};
        const auto del = PlanTrim(f, 1000, "keep");
        CHECK(del.size() == 1 && del[0] == "b");
    }
    // a huge cap does not overflow
    CHECK(PlanTrim({{"a", 5, 1}}, CAP_BYTES).empty());
    CHECK(TotalSize({{"a", 5, 1}, {"b", 7, 2}}) == 12);

    // the index folder is not a picture
    CHECK(IsIndexPath("index"));
    CHECK(IsIndexPath("index/nes.txt"));
    CHECK(!IsIndexPath("indexed/x.png"));
    CHECK(!IsIndexPath("nes/index/x.png"));

    std::puts("test_thumb_cache: ok");
    return 0;
}
