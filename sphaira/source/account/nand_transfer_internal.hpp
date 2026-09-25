#pragma once

#include "account/nand_transfer.hpp"
#include "fs.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <minizip/unzip.h>
#include <minizip/zip.h>

namespace sphaira::nand_transfer {

auto Join(const std::string& dir, const char* name) -> std::string;
auto BaseName(const std::string& path) -> std::string;
auto ParentDir(const std::string& path) -> std::string;
auto StemName(std::string_view name) -> std::string;
auto PackLooksRight(fs::Fs& sd, const std::string& dir) -> bool;

auto ReadZipFileEntry(unzFile zf, const char* filename, std::vector<u8>& out, u64 max_size = 1024 * 1024) -> bool;
auto ValidatePackArchiveFile(const std::string& path) -> bool;
auto CollectArchivePackUsers(const std::string& pack_path) -> std::vector<PackUser>;
auto MakeArchivePackInfo(const std::string& path) -> PackInfo;
auto MakePackInfo(fs::Fs& sd, const std::string& dir) -> PackInfo;
auto CollectPackUsers(fs::Fs& sd, const std::string& pack_dir) -> std::vector<PackUser>;

} // namespace sphaira::nand_transfer
