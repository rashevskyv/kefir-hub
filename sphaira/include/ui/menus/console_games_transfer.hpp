#pragma once

#include <switch.h>
#include <vector>

namespace sphaira::ui::menu::games_transfer {

// Console Transfer → Send installed games: pick games (or take `preselected`
// as-is), then send them as NSPs by USB cable or over Wi-Fi. The other
// console uses Receive().
void Send(std::vector<u64> preselected = {});

// Console Transfer → Receive games: ask for the sending console's address,
// list what it offers, pick, install base + updates + DLC over HTTP.
void Receive();

} // namespace sphaira::ui::menu::games_transfer
