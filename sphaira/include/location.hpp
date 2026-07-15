#pragma once

#include <string>
#include <vector>
#include <switch.h>
#include "ui/menus/filebrowser.hpp"

namespace sphaira::location {

using FsEntryFlag = ui::menu::filebrowser::FsEntryFlag;

struct Entry {
    std::string name{};
    std::string url{};
    std::string user{};
    std::string pass{};
    std::string bearer{};
    std::string pub_key{};
    std::string priv_key{};
    u16 port{};

    bool IsSmb() const {
        return url.starts_with("smb://");
    }
};
using Entries = std::vector<Entry>;

auto Load() -> Entries;
void Add(const Entry& e);

// helper for hdd devices.
// this doesn't really belong in this header, however
// locations likely will be renamed to something more generic soon.
struct StdioEntry {
    // mount point (ums0:)
    std::string mount{};
    // ums0: (USB Flash Disk)
    std::string name{};
    // FsEntryFlag
    u32 flags{};
    // optional dump path inside the mount point.
    std::string dump_path{};
    // set to hide for filebrowser.
    bool fs_hidden{};
    // set to hide in dump list.
    bool dump_hidden{};

    bool write_protect() const {
        return flags & ui::menu::filebrowser::FsEntryFlag_ReadOnly;
    }
};

using StdioEntries = std::vector<StdioEntry>;

// set write=true to filter out write protected devices.
auto GetStdio(bool write) -> StdioEntries;

} // namespace sphaira::location
