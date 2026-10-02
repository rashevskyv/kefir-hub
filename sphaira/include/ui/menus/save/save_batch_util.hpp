#pragma once

// Pure helpers for the batch-restore prompts. Host test: tests/test_save_batch_util.cpp.

#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sphaira::ui::menu::save {

// Keeps, for every key, only the item with the highest rank. The survivor stays at the
// position of the first item that had that key, so the order of distinct keys is stable.
template <typename T, typename KeyFn, typename RankFn>
void KeepNewestPerKey(std::vector<T>& items, KeyFn key_of, RankFn rank_of) {
    std::vector<T> out;
    std::unordered_map<std::string, std::size_t> index;
    for (auto& item : items) {
        const auto [it, inserted] = index.try_emplace(key_of(item), out.size());
        if (inserted) {
            out.emplace_back(std::move(item));
        } else if (rank_of(item) > rank_of(out[it->second])) {
            out[it->second] = std::move(item);
        }
    }
    items = std::move(out);
}

// Appends " (tag)" to every label that occurs more than once, so two entries with the
// same text (e.g. two accounts with one nickname) stay distinguishable.
inline void DisambiguateLabels(std::vector<std::string>& labels, const std::vector<std::string>& tags) {
    std::unordered_map<std::string, int> count;
    for (const auto& label : labels) {
        count[label]++;
    }
    for (std::size_t i = 0; i < labels.size() && i < tags.size(); i++) {
        if (count[labels[i]] > 1) {
            labels[i] += " (" + tags[i] + ")";
        }
    }
}

} // namespace sphaira::ui::menu::save
