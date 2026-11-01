#pragma once

#include "utils/remote_shop_fetch.hpp"

#include <memory>
#include <string>

namespace sphaira::ui::menu::shop {

// the files of a shop that is not an Ownfoil server (an AeroFoil one or a legacy Tinfoil index): a filter on the
// first row, A installs one file, X/Y mark several, START installs the marked ones or every file shown.
// Every install goes through the install queue.
void OpenShop(const std::string& name, std::shared_ptr<const sphaira::shop::Listing> listing, const std::string& user, const std::string& pass);

} // namespace sphaira::ui::menu::shop
