#pragma once

// Edits a packaged content meta (.cnmt, the file inside a meta NCA), libnx-free.

#include <cstdint>
#include <span>

namespace sphaira::repack {

inline constexpr std::uint8_t CNMT_CONTENT_PROGRAM = 1;

// Points the content of `type` (id offset 0) at a rebuilt NCA: its sha256, the id (first 16 bytes
// of the hash, as Nintendo names contents) and the size. Returns false when the cnmt is malformed
// or has no such content.
bool PatchCnmtContent(std::span<std::uint8_t> cnmt, std::uint8_t type, std::span<const std::uint8_t, 0x20> hash, std::uint64_t size);

} // namespace sphaira::repack
