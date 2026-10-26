#include "yati/deferred_patch_launch_resets.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

int main() {
    using sphaira::yati::DeferredPatchLaunchResets;
    constexpr std::uint64_t first_app_id = 0x0100000000001000;
    constexpr std::uint64_t second_app_id = 0x0100000000002000;

    DeferredPatchLaunchResets empty{};
    int reset_calls = 0;
    assert(empty.Apply([&](std::uint64_t) { ++reset_calls; return 0; }) == 0);
    assert(reset_calls == 0);

    DeferredPatchLaunchResets queued{};
    queued.Add(first_app_id);
    queued.Add(second_app_id);
    queued.Add(first_app_id);
    std::vector<std::uint64_t> reset_ids{};
    assert(queued.Apply([&](std::uint64_t id) { reset_ids.push_back(id); return 0; }) == 0);
    assert((reset_ids == std::vector<std::uint64_t>{first_app_id, second_app_id}));

    DeferredPatchLaunchResets failed{};
    failed.Add(first_app_id);
    failed.Add(second_app_id);
    failed.Add(second_app_id + 1);
    int reset_attempts = 0;
    const auto reset_result = failed.Apply([&](std::uint64_t) {
        ++reset_attempts;
        return reset_attempts == 1 ? 7 : reset_attempts == 2 ? 9 : 0;
    });
    assert(reset_result == 7);
    assert(reset_attempts == 3);
}
