#pragma once

#include "account/nand_transfer.hpp"

#include <functional>
#include <string>
#include <string_view>

namespace sphaira::ui::menu::users {

inline auto GetPackDisplayName(const nand_transfer::PackInfo& pack) -> std::string {
    std::string stem = pack.name;
    if (pack.is_archive) {
        constexpr std::string_view kKefirNandExt = ".kefir-nand.zip";
        constexpr std::string_view kZipExt = ".zip";
        if (stem.size() >= kKefirNandExt.size() && stem.ends_with(kKefirNandExt)) {
            stem.resize(stem.size() - kKefirNandExt.size());
        } else if (stem.size() >= kZipExt.size() && stem.ends_with(kZipExt)) {
            stem.resize(stem.size() - kZipExt.size());
        }
    }
    if (stem.size() == 15 && !pack.created_label.empty()) {
        return pack.created_label;
    }
    return stem;
}

using RestoreCb = std::function<void(const std::string& dir, bool restore_play_hours)>;

void OpenNandPackDetail(const nand_transfer::PackInfo& pack, RestoreCb on_restore);

} // namespace sphaira::ui::menu::users
