#pragma once

#include "account/account_user.hpp"

namespace sphaira::account_user {

auto LoadProfile(const AccountUid& uid, AccountProfileBase& base, AccountUserData& data) -> Result;

} // namespace sphaira::account_user
