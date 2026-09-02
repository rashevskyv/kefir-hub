#pragma once

#include <functional>
#include <string>

namespace sphaira::ui::menu::users {

void OpenNandPackLibrary(std::function<void(const std::string& dir, bool restore_play_hours)> on_restore);

} // namespace sphaira::ui::menu::users
