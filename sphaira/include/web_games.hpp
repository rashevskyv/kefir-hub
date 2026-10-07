#pragma once

// Console Transfer → Send installed games. The sending console's web server
// offers chosen installed games as streamed NSPs (base, updates, DLC), built
// the same way the MTP Games drive builds them; nothing is written to the SD.
// The receiving console reads /games, then installs each file over HTTP.

#include "web_http.hpp"
#include <switch.h>
#include <string>
#include <vector>

namespace sphaira {

// the games the server offers. Holds title::Init() while non-empty.
void WebGamesSetShared(std::vector<u64> app_ids);
void WebGamesClear();
auto WebGamesIsSharing() -> bool;

// GET /games: {"games":[{"id":"<16 hex>","name":"...","files":[{"n":0,"name":"...nsp","size":N}]}]}
void HandleGamesList(web::detail::Socket sock);
// GET /games/file?id=<16 hex>&n=<index>: the nsp bytes; honours Range (206).
void SendGameFile(web::detail::Socket sock, const std::string& req, const std::string& query);

} // namespace sphaira
