#pragma once

#include <algorithm>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace sphaira::yati {

class DeferredPatchLaunchResets {
public:
    void Add(std::uint64_t application_id) {
        if (std::find(application_ids.begin(), application_ids.end(), application_id) == application_ids.end()) {
            application_ids.push_back(application_id);
        }
    }

    template <typename Reset>
    auto Apply(Reset&& reset) const -> std::invoke_result_t<Reset, std::uint64_t> {
        using Result = std::invoke_result_t<Reset, std::uint64_t>;
        Result first_error{};
        for (const auto application_id : application_ids) {
            const auto result = reset(application_id);
            if (first_error == Result{} && result != Result{}) {
                first_error = result;
            }
        }
        return first_error;
    }

private:
    std::vector<std::uint64_t> application_ids{};
};

} // namespace sphaira::yati
