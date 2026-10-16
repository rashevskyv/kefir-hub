// Host test for sphaira/include/ui/menus/kefir/kefir_changelog_range.hpp

#include "ui/menus/kefir/kefir_changelog_range.hpp"

#include <cassert>
#include <cstdio>

using sphaira::ui::menu::kefir::detail::ChangelogRange;

int main() {
    assert(ChangelogRange(924, 927) == std::make_pair(925, 927)); // an update: what changed since the installed one
    assert(ChangelogRange(927, 927) == std::make_pair(927, 927)); // reinstall
    assert(ChangelogRange(0, 927) == std::make_pair(927, 927));   // unknown installed version: not 1..927
    assert(ChangelogRange(930, 927) == std::make_pair(927, 927)); // a downgrade
    std::puts("test_kefir_changelog_range: ok");
}
