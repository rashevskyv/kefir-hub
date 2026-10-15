#include "repack/romfs_walk.hpp"

#include <cstring>

namespace sphaira::repack {
namespace {

constexpr std::uint32_t NONE = 0xFFFFFFFF;
// dir entry: parent, sibling, child dir, child file, next hash, name length, name.
constexpr std::size_t DIR_SIZE = 0x18;
// file entry: parent, sibling, u64 data offset, u64 data size, next hash, name length, name.
constexpr std::size_t FILE_SIZE = 0x20;

std::uint32_t Rd32(std::span<const std::uint8_t> t, std::size_t at) {
    std::uint32_t v;
    std::memcpy(&v, t.data() + at, sizeof(v));
    return v;
}

std::uint64_t Rd64(std::span<const std::uint8_t> t, std::size_t at) {
    std::uint64_t v;
    std::memcpy(&v, t.data() + at, sizeof(v));
    return v;
}

bool Name(std::span<const std::uint8_t> t, std::size_t entry, std::size_t fixed, std::string& out) {
    if (entry > t.size() || t.size() - entry < fixed) {
        return false;
    }
    const auto len = Rd32(t, entry + fixed - 4);
    if (len > t.size() - entry - fixed) {
        return false;
    }
    out.assign(reinterpret_cast<const char*>(t.data() + entry + fixed), len);
    return true;
}

} // namespace

bool WalkRomfs(std::span<const std::uint8_t> dirs, std::span<const std::uint8_t> files, std::uint64_t data_offset, std::vector<RomfsFile>& out) {
    out.clear();
    // every entry is visited once (dirs twice: pushed, then walked); more steps means a loop.
    auto budget = 2 * (dirs.size() / DIR_SIZE) + files.size() / FILE_SIZE + 1;

    struct Pending { std::uint32_t dir; std::string prefix; };
    std::vector<Pending> stack{{0, ""}};
    std::string name;

    while (!stack.empty()) {
        auto [dir, prefix] = std::move(stack.back());
        stack.pop_back();
        if (!budget-- || !Name(dirs, dir, DIR_SIZE, name)) {
            return false;
        }

        for (auto f = Rd32(dirs, dir + 0xC); f != NONE; f = Rd32(files, f + 4)) {
            if (!budget-- || !Name(files, f, FILE_SIZE, name)) {
                return false;
            }
            out.push_back({prefix + name, data_offset + Rd64(files, f + 8), Rd64(files, f + 0x10)});
        }

        for (auto d = Rd32(dirs, dir + 8); d != NONE; d = Rd32(dirs, d + 4)) {
            if (!budget-- || !Name(dirs, d, DIR_SIZE, name)) {
                return false;
            }
            stack.push_back({d, prefix + name + "/"});
        }
    }
    return true;
}

} // namespace sphaira::repack
