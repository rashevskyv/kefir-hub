#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace sphaira::rom {

struct FolderStat {
    size_t entries{};       // files and folders at the top of /roms/<folder>/
    std::uint64_t bytes{};  // the files among them
};

// what lies in /roms/<folder>/ now
auto Stat(const std::string& folder) -> FolderStat;

// the files the Hub downloaded into /roms/<folder>/: a filter row, A or START open the context menu (delete),
// X/Y mark several. `refreshed` is called after something was deleted, so the page behind can show new counts.
void OpenFiles(const std::string& folder, std::function<void()> refreshed);

} // namespace sphaira::rom
