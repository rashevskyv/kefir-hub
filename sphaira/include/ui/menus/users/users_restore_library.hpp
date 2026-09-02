#pragma once

#include "account/account_user.hpp"

#include <functional>
#include <optional>
#include <vector>

namespace sphaira::ui::menu::users {

void OpenRestoreLibrary(
    std::vector<account_user::Pack> packs,
    std::function<void(std::optional<std::vector<account_user::Pack>>)> cb,
    bool allow_delete = true);

} // namespace sphaira::ui::menu::users
