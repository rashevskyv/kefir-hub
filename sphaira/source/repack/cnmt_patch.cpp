#include "repack/cnmt_patch.hpp"

#include <cstring>

namespace sphaira::repack {
namespace {

constexpr std::size_t HEADER_SIZE = 0x20;
// packaged content info: sha256, content id, u40 size, attributes, type, id offset.
constexpr std::size_t INFO_SIZE = 0x38;

} // namespace

bool PatchCnmtContent(std::span<std::uint8_t> cnmt, std::uint8_t type, std::span<const std::uint8_t, 0x20> hash, std::uint64_t size) {
    if (cnmt.size() < HEADER_SIZE || size >> 40) {
        return false;
    }
    std::uint16_t ext_size, count;
    std::memcpy(&ext_size, cnmt.data() + 0xE, sizeof(ext_size));
    std::memcpy(&count, cnmt.data() + 0x10, sizeof(count));
    if (cnmt.size() < HEADER_SIZE + ext_size + std::size_t{count} * INFO_SIZE) {
        return false;
    }

    int patched = 0;
    for (std::size_t i = 0; i < count; i++) {
        auto* e = cnmt.data() + HEADER_SIZE + ext_size + i * INFO_SIZE;
        if (e[0x36] != type || e[0x37] != 0) {
            continue;
        }
        std::memcpy(e, hash.data(), 0x20);
        std::memcpy(e + 0x20, hash.data(), 0x10);
        for (int b = 0; b < 5; b++) {
            e[0x30 + b] = static_cast<std::uint8_t>(size >> (8 * b));
        }
        patched++;
    }
    return patched == 1;
}

} // namespace sphaira::repack
