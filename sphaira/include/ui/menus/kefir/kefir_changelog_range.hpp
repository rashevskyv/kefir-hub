#pragma once

// Which Kefir versions the changelog box lists before an install. Pure; host test tests/test_kefir_changelog_range.cpp.

#include <utility>

namespace sphaira::ui::menu::kefir::detail {

// current = 0: the installed version is unknown (no /switch/kefir-updater/version). Listing 1..target then put the
// whole history of Kefir on screen and crashed the Hub (0x767, a console with plain Atmosphère, 2026-10-16), so an
// unknown current version shows only the target's changes.
inline auto ChangelogRange(int current, int target) -> std::pair<int, int> {
    if (current <= 0 || current >= target) {
        return {target, target};
    }
    return {current + 1, target};
}

} // namespace sphaira::ui::menu::kefir::detail
