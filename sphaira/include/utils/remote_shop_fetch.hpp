#pragma once

#include "remote_shop.hpp"

#include <stop_token>
#include <string>
#include <vector>

namespace sphaira::shop {

struct Listing {
    std::vector<Item> items{};
    std::string motd{};
    std::string source_url{}; // the url that answered: an AeroFoil api or the legacy index itself
    bool aerofoil{};          // false: a legacy Tinfoil index
};

// reads a shop that is not an Ownfoil server: an AeroFoil one (/api/remote/sections, then /api/shop/sections),
// else a legacy Tinfoil index at the url itself, with the indexes of its "directories" too.
// only what yati installs is listed (nsp, nsz, xci, xcz). `error` says why when it returns false.
auto Fetch(const std::string& url, const std::string& user, const std::string& pass, std::stop_token token, Listing& out, std::string& error) -> bool;

} // namespace sphaira::shop
