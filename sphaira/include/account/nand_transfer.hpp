#pragma once

#include <string>
#include <vector>
#include <switch.h>

namespace sphaira::ui { struct ProgressBox; }

namespace sphaira::nand_transfer {

struct Report {
    std::string dir;
    bool save_0010{};
    bool save_0011{};
    bool save_00F0{};
    bool save_0041{};
    bool complete{};
};

struct PackInfo {
    std::string dir;
    std::string name;
    std::string created_label;
    bool save_0010{};
    bool save_00F0{};
    u32 accounts{};
    bool is_archive{};
};

struct PackUser {
    std::string uid;
    std::string nickname;
    std::string avatar_path;
};

auto IsPack(const std::string& dir) -> bool;
auto IsPackArchive(const std::string& path) -> bool;

auto ListPacks() -> std::vector<PackInfo>;
auto ListPackUsers(const std::string& pack_dir) -> std::vector<PackUser>;
auto ReadPackUserAvatar(const PackInfo& pack, const PackUser& user, std::vector<u8>& out_jpeg) -> bool;

// Creates a single .kefir-nand.zip archive from an unpacked pack directory.
// Uses an atomic .part rename, validates pack structure and manifest.
// On success, deletes staging_dir and sets out_archive_path.
auto FinalizePackArchive(const std::string& staging_dir, std::string& out_archive_path, ui::ProgressBox* pbox = nullptr) -> Result;

// Extracts an archive pack into out_staging_dir under /config/kefir/nand_transfer/.
// Validates safe paths and structure before returning success.
auto StagePackArchiveForRestore(const std::string& archive_path, std::string& out_staging_dir, ui::ProgressBox* pbox = nullptr) -> Result;

// Decrypt system saves 0010/0011/00F0/0041 onto SD (source Horizon keys).
auto Export(ui::ProgressBox* pbox, Report& out) -> Result;

// Legacy Horizon FS write+Commit path. Hub restore uses TegraExplorer auto
// script instead (Import hits the same wall as ApplyLink on 0010/00F0).
auto Import(ui::ProgressBox* pbox, const std::string& dir, Report& out) -> Result;

} // namespace sphaira::nand_transfer
