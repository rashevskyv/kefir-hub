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
    bool save_0010{};
    bool save_00F0{};
    u32 accounts{};
};

struct PackUser {
    std::string uid;
    std::string nickname;
    std::string avatar_path;
};

auto IsPack(const std::string& dir) -> bool;

auto ListPacks() -> std::vector<PackInfo>;
auto ListPackUsers(const std::string& pack_dir) -> std::vector<PackUser>;

// Decrypt system saves 0010/0011/00F0/0041 onto SD (source Horizon keys).
auto Export(ui::ProgressBox* pbox, Report& out) -> Result;

// Legacy Horizon FS write+Commit path. Hub restore uses TegraExplorer auto
// script instead (Import hits the same wall as ApplyLink on 0010/00F0).
auto Import(ui::ProgressBox* pbox, const std::string& dir, Report& out) -> Result;

} // namespace sphaira::nand_transfer
