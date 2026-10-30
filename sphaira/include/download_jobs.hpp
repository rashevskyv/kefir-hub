#pragma once

// A list of files to fetch to the SD card, one after another, in a ProgressBox: a file already
// in place with the right size is skipped; a downloaded one is hash-checked beside the target
// (<dest>.part) and moved into place. Shared by the BIOS and the ROM downloaders.

#include "hasher.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sphaira::jobs {

struct Job {
    std::string url;
    std::string dest;  // full SD path
    std::string hash;  // hex, empty = no check
    hash::Type hash_type{hash::Type::Sha1};
    std::uint64_t size{};
};

inline std::uint64_t TotalSize(const std::vector<Job>& jobs) {
    std::uint64_t n = 0;
    for (const auto& j : jobs) n += j.size;
    return n;
}

// "12 files, 1.38 GB"
auto Describe(const std::vector<Job>& jobs) -> std::string;

// the ProgressBox for the list; `title` names what is fetched ("Download BIOS for RetroArch").
// At the end a toast with the counts, or a box naming the last failed file; then `on_done`
// (also after a cancel), so a page can redraw what is on the card now.
void Run(const std::string& title, std::vector<Job> jobs, std::function<void()> on_done = {});

} // namespace sphaira::jobs
