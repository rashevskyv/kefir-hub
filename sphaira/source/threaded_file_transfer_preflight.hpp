#pragma once

#include "threaded_file_transfer.hpp"
#include <minizip/unzip.h>
#include <string>
#include <vector>

namespace sphaira::thread {

struct ResolvedDestinationEntry {
    fs::FsPath path;
    bool is_directory{false};
    bool keep{false};
    bool is_dbi_root_marker{false};
};

Result ResolveArchiveEntryName(const unz_file_info64& info, const char* name_buf, bool save_dbi_compat, fs::FsPath& out_name, bool* out_is_dbi_root_marker = nullptr);

Result ResolveArchiveDestinationEntry(
    const unz_file_info64& info,
    const char* name_buf,
    const fs::FsPath& base_path,
    UnzipAllFilter filter,
    bool save_dbi_compat,
    ResolvedDestinationEntry& out);

std::vector<std::string> GetParentDirectories(const std::string& path);

} // namespace sphaira::thread
