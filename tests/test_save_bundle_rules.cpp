// Host test for sphaira/include/save_bundle_rules.hpp

#include "save_bundle_rules.hpp"

#include <cassert>
#include <cstdio>

int main() {
    using namespace sphaira::save_bundle;

    assert(KindOf("Just Dance 2025 - Cache Data 0100BEE017FC0000.kefir-cache.tar") == Kind::Cache);
    assert(KindOf("Just Dance 2025 - Offline Gamesave Data 0100BEE017FC0000.kefir-save.tar") == Kind::Account);
    assert(KindOf("Pikmin 4 - Russian.kefir-sd.tar") == Kind::None);
    assert(KindOf(".kefir-save.tar") == Kind::None);

    assert(TitleOf("Just Dance 2025 - Cache Data 0100BEE017FC0000.kefir-cache.tar") == 0x0100BEE017FC0000ULL);
    assert(TitleOf("JD - Save 0100bee017fc0000.kefir-save.tar") == 0x0100BEE017FC0000ULL);
    assert(TitleOf("JD - Save x0100BEE017FC0000.kefir-save.tar") == 0);   // no space before the id
    assert(TitleOf("JD - Save 0100BEE017FC000G.kefir-save.tar") == 0);
    assert(TitleOf("0100BEE017FC0000.kefir-save.tar") == 0);
    // the queue's "already installed" check must not see a title id in the name
    assert(sphaira::sd_bundle::IsBundleName("a.kefir-sd.tar") && !sphaira::sd_bundle::IsBundleName("a 0100BEE017FC0000.kefir-cache.tar"));

    const auto sizes = ParseSizes("0\t1548288\r\n8\t1470000000\nbad\n70000\t5\n3\t-1\n4\t12x\n");
    assert(sizes.size() == 2);
    assert(sizes.at(0) == 1548288 && sizes.at(8) == 1470000000);

    const auto e = ParseCacheEntry("0003/77010b6f-5f36/json.cache");
    assert(e && e->index == 3 && e->path == "77010b6f-5f36/json.cache");
    assert(ParseCacheEntry("./8\\a\\b.bin")->path == "a/b.bin");
    assert(!ParseCacheEntry("0003/"));
    assert(!ParseCacheEntry("0003"));
    assert(!ParseCacheEntry("x/a"));
    assert(!ParseCacheEntry("1/../../evil"));
    assert(!ParseCacheEntry("99999/a"));

    assert(StorageSize(0) == 16 * 1024 * 1024);
    assert(StorageSize(2300LL * 1024 * 1024) % BLOCK == 0);
    assert(StorageSize(2300LL * 1024 * 1024) > 2300LL * 1024 * 1024);
    assert(StorageSize(INT64_MAX) == 0);
    assert(CommitBudget(MIN_JOURNAL) == MIN_JOURNAL / 2);
    assert(CommitBudget(0) == BLOCK);

    // the queue never waits for the user's answer in the first pass
    assert(NextPickStep(-1, false, false) == PickStep::Defer);
    assert(NextPickStep(-2, false, false) == PickStep::Defer);
    assert(NextPickStep(-2, true, false) == PickStep::Wait);
    assert(NextPickStep(1, false, false) == PickStep::Use);
    assert(NextPickStep(0, true, true) == PickStep::Use);
    assert(NextPickStep(-3, true, false) == PickStep::Skip);
    assert(NextPickStep(-2, true, true) == PickStep::Skip);

    std::puts("test_save_bundle_rules: ok");
    return 0;
}
