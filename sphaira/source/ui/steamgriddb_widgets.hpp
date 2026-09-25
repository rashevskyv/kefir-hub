#pragma once

#include "ui/steamgriddb_icon.hpp"
#include <string>

namespace sphaira::ui::steamgriddb {

void StartSearch(const std::string& api_key, const std::string& title, const IconCallback& callback);

} // namespace sphaira::ui::steamgriddb
