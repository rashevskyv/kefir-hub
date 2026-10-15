#pragma once

#include "yati/source/base.hpp"
#include "yati/nx/keys.hpp"
#include "yati/nx/nca.hpp"
#include <memory>
#include <switch.h>

namespace sphaira::repack {

// An NCA in ncm, opened for reading.
struct NcaFile {
    nca::Header header{};                          // decrypted.
    keys::KeyEntry key{};                          // title key or decrypted key area key.
    std::shared_ptr<yati::source::Base> raw{};     // as stored (encrypted).
    std::shared_ptr<yati::source::Base> reader{};  // decrypts each section with its fs header counter.
    s64 size{};
};

Result OpenNca(const keys::Keys& keys, NcmContentStorage* cs, const NcmContentId& id, NcaFile& out);

// The romfs of an installed program as the game sees it: the base program NCA, with the update's
// BKTR patch applied when an update is given. Reads are plaintext.
struct ProgramRomfs {
    std::shared_ptr<yati::source::Base> storage{};
    u64 offset{}; // romfs image start inside storage.
    u64 size{};
};

// base / patch: program NCAs in ncm. patch_cs / patch may be null (no update).
Result OpenProgramRomfs(const keys::Keys& keys, NcmContentStorage* base_cs, const NcmContentId& base, NcmContentStorage* patch_cs, const NcmContentId* patch, ProgramRomfs& out);

} // namespace sphaira::repack
