#include "repack/nca_build.hpp"
#include "repack/cnmt_patch.hpp"

#include "defines.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "ui/progress_box.hpp"
#include "utils/utils.hpp"
#include "yati/container/nsp.hpp"
#include "yati/nx/crypto.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

namespace sphaira::repack {
namespace {

constexpr u64 MEDIA = 0x200;
constexpr u64 IVFC_BLOCK = 0x4000;
constexpr u32 IVFC_BLOCK_LOG2 = 14;
constexpr u64 CHUNK = 4 * 1024 * 1024; // a multiple of IVFC_BLOCK.
constexpr u32 META_PFS0_BLOCK = 0x1000;

auto Align(u64 v, u64 a) -> u64 {
    return (v + a - 1) & ~(a - 1);
}

// bytes held in memory, as a source.
struct MemSource final : yati::source::Base {
    std::vector<u8> data;
    explicit MemSource(std::vector<u8>&& d) : data{std::move(d)} {}
    Result Read(void* buf, s64 off, s64 size, u64* bytes_read) override {
        size = std::min<s64>(size, off < (s64)data.size() ? data.size() - off : 0);
        std::memcpy(buf, data.data() + off, size);
        *bytes_read = size;
        R_SUCCEED();
    }
    Result GetSize(s64* out) override {
        *out = data.size();
        R_SUCCEED();
    }
};

// ranges of other sources laid end to end; a part without a source reads as zeros.
struct Concat final : yati::source::Base {
    struct Part {
        std::shared_ptr<yati::source::Base> src;
        u64 src_offset;
        u64 size;
        u64 offset{}; // in this source, set by Add().
    };
    std::vector<Part> parts;
    u64 size{};
    std::mutex mutex;

    void Add(std::shared_ptr<yati::source::Base> src, u64 src_offset, u64 n) {
        if (n) {
            parts.push_back({std::move(src), src_offset, n, size});
            size += n;
        }
    }

    Result Read(void* buf, s64 off, s64 len, u64* bytes_read) override {
        std::scoped_lock lock{mutex};
        len = std::min<s64>(len, off < (s64)size ? size - off : 0);
        auto out = static_cast<u8*>(buf);
        auto it = std::upper_bound(parts.begin(), parts.end(), (u64)off, [](u64 o, const Part& p) { return o < p.offset; });
        u64 pos = off, left = len;
        for (--it; left; ++it) {
            const auto rel = pos - it->offset;
            const auto n = std::min<u64>(left, it->size - rel);
            if (it->src) {
                R_TRY(it->src->Read2(out, it->src_offset + rel, n));
            } else {
                std::memset(out, 0, n);
            }
            out += n;
            pos += n;
            left -= n;
        }
        *bytes_read = len;
        R_SUCCEED();
    }

    Result GetSize(s64* out) override {
        *out = size;
        R_SUCCEED();
    }
};

auto Mem(std::vector<u8>&& d) -> std::shared_ptr<yati::source::Base> {
    return std::make_shared<MemSource>(std::move(d));
}

// one hash per block of `src` (the last block zero padded), padded to a whole IVFC block.
auto HashLevel(std::span<const u8> src) -> std::vector<u8> {
    std::vector<u8> out;
    out.reserve(Align(src.size() / IVFC_BLOCK * 0x20 + 0x20, IVFC_BLOCK));
    std::vector<u8> block(IVFC_BLOCK);
    for (u64 i = 0; i < src.size(); i += IVFC_BLOCK) {
        const auto n = std::min<u64>(IVFC_BLOCK, src.size() - i);
        u8 hash[0x20];
        if (n == IVFC_BLOCK) {
            sha256CalculateHash(hash, src.data() + i, n);
        } else {
            std::memcpy(block.data(), src.data() + i, n);
            std::memset(block.data() + n, 0, IVFC_BLOCK - n);
            sha256CalculateHash(hash, block.data(), IVFC_BLOCK);
        }
        out.insert(out.end(), hash, hash + 0x20);
    }
    out.resize(Align(out.size(), IVFC_BLOCK));
    return out;
}

// level 4 = the hashes of the romfs blocks; read in CHUNK pieces so a multi-GB romfs never sits in memory.
Result HashRomfs(ui::ProgressBox* pbox, yati::source::Base& romfs, u64 size, std::vector<u8>& level4) {
    std::vector<u8> buf(CHUNK);
    level4.clear();
    level4.reserve(Align(size, IVFC_BLOCK) / IVFC_BLOCK * 0x20);
    for (u64 off = 0; off < size; off += CHUNK) {
        R_TRY(pbox->ShouldExitResult());
        const auto n = std::min<u64>(CHUNK, size - off);
        R_TRY(romfs.Read2(buf.data(), off, n));
        std::memset(buf.data() + n, 0, Align(n, IVFC_BLOCK) - n);
        for (u64 i = 0; i < n; i += IVFC_BLOCK) {
            u8 hash[0x20];
            sha256CalculateHash(hash, buf.data() + i, IVFC_BLOCK);
            level4.insert(level4.end(), hash, hash + 0x20);
        }
        pbox->UpdateTransfer(off + n, size);
    }
    level4.resize(Align(level4.size(), IVFC_BLOCK));
    R_SUCCEED();
}

// the header as the console reads it: rights id cleared (sections are plaintext), section
// table, fs header hashes and size from the new layout, then encrypted with the header key.
auto SealHeader(const keys::Keys& keys, nca::Header h, u64 size) -> std::vector<u8> {
    std::memset(&h.rights_id, 0, sizeof(h.rights_id));
    for (u32 i = 0; i < NCA_SECTION_TOTAL; i++) {
        if (h.fs_header[i].IsValid() && h.fs_table[i].IsValid()) {
            sha256CalculateHash(&h.fs_header_hash[i], &h.fs_header[i], sizeof(h.fs_header[i]));
        }
    }
    h.size = size;
    std::vector<u8> out(sizeof(h));
    crypto::cryptoAes128Xts(&h, out.data(), keys.header_key, 0, MEDIA, sizeof(h), true);
    return out;
}

void SetSection(nca::Header& h, u32 i, u64 start, u64 end) {
    h.fs_table[i] = {};
    h.fs_table[i].media_start_offset = start / MEDIA;
    h.fs_table[i].media_end_offset = end / MEDIA;
    h.fs_table[i]._0x8[0] = 1; // as Nintendo's tools write it.
}

// the whole NCA's sha256, read back through its own source.
Result HashNca(ui::ProgressBox* pbox, BuiltNca& nca) {
    std::vector<u8> buf(CHUNK);
    Sha256Context ctx;
    sha256ContextCreate(&ctx);
    for (u64 off = 0; off < nca.size; off += CHUNK) {
        if (pbox) {
            R_TRY(pbox->ShouldExitResult());
        }
        const auto n = std::min<u64>(CHUNK, nca.size - off);
        R_TRY(nca.data->Read2(buf.data(), off, n));
        sha256ContextUpdate(&ctx, buf.data(), n);
        if (pbox) {
            pbox->UpdateTransfer(off + n, nca.size);
        }
    }
    sha256ContextGetHash(&ctx, nca.hash.data());
    std::memcpy(&nca.id, nca.hash.data(), sizeof(nca.id));
    R_SUCCEED();
}

} // namespace

Result BuildProgramNca(ui::ProgressBox* pbox, const keys::Keys& keys, const NcaFile& orig, const std::shared_ptr<yati::source::Base>& romfs, u64 romfs_size, BuiltNca& out) {
    out = {};
    auto h = orig.header;

    pbox->NewTransfer("Hashing the new romfs"_i18n);
    std::vector<u8> levels[5];
    R_TRY(HashRomfs(pbox, *romfs, romfs_size, levels[4]));
    for (int i = 3; i >= 0; i--) {
        levels[i] = HashLevel(levels[i + 1]);
    }

    auto nca = std::make_shared<Concat>();
    nca->Add(nullptr, 0, sizeof(h)); // header, replaced below.

    bool has_romfs = false;
    for (u32 i = 0; i < h.GetSectionCount(); i++) {
        auto& fs = h.fs_header[i];
        const auto start = nca->size;
        if (fs.fs_type == nca::FileSystemType_RomFS) {
            R_UNLESS(!has_romfs, Result_RepackUnsupportedNca);
            has_romfs = true;
            fs = {};
            fs.version = 2;
            fs.fs_type = nca::FileSystemType_RomFS;
            fs.hash_type = nca::HashType_HierarchicalIntegrity;
            fs.encryption_type = nca::EncryptionType_None;
            auto& ivfc = fs.hash_data.integrity_meta_info;
            ivfc.magic = 0x43465649;
            ivfc.version = 0x20000;
            ivfc.master_hash_size = 0x20;
            ivfc.info_level_hash.max_layers = 7;
            u64 off = 0;
            for (int l = 0; l < 6; l++) {
                auto& level = ivfc.info_level_hash.levels[l];
                level.logical_offset = off;
                level.hash_data_size = l < 5 ? levels[l].size() : romfs_size;
                level.block_size = IVFC_BLOCK_LOG2;
                off += level.hash_data_size;
            }
            sha256CalculateHash(ivfc.master_hash, levels[0].data(), levels[0].size());

            for (auto& l : levels) {
                const auto n = l.size();
                nca->Add(Mem(std::move(l)), 0, n);
            }
            nca->Add(romfs, 0, romfs_size);
            // level 5 is read in whole IVFC blocks, as the spike image that the console accepted.
            nca->Add(nullptr, 0, Align(romfs_size, IVFC_BLOCK) - romfs_size);
        } else {
            // exefs / logo: the original bytes, decrypted; their hash data stays valid.
            R_UNLESS(fs.encryption_type != nca::EncryptionType_AesCtrEx && fs.encryption_type != nca::EncryptionType_AesCtrExSkipLayerHash, Result_RepackUnsupportedNca);
            nca->Add(orig.reader, h.fs_table[i].GetOffset(), h.fs_table[i].GetSize());
            fs.encryption_type = nca::EncryptionType_None;
            fs.section_ctr = 0;
        }
        nca->Add(nullptr, 0, Align(nca->size, MEDIA) - nca->size);
        SetSection(h, i, start, nca->size);
    }
    R_UNLESS(has_romfs, Result_RepackUnsupportedNca);

    nca->parts.front().src = Mem(SealHeader(keys, h, nca->size));
    out.size = nca->size;
    out.data = nca;

    pbox->NewTransfer("Hashing the new program NCA"_i18n);
    R_TRY(HashNca(pbox, out));
    log_write("[repack] program nca %s, %lu bytes\n", utils::hexIdToStr(out.id).str, out.size);
    R_SUCCEED();
}

Result BuildMetaNca(const keys::Keys& keys, const NcaFile& orig, const BuiltNca& program, BuiltNca& out) {
    out = {};
    auto h = orig.header;
    R_UNLESS(h.GetSectionCount() == 1 && h.fs_header[0].fs_type == nca::FileSystemType_PFS0, Result_RepackUnsupportedNca);

    // the .cnmt file out of the PFS0 in section 0.
    const auto& sha = h.fs_header[0].hash_data.hierarchical_sha256_data;
    yati::container::Collections files;
    R_TRY(yati::container::Nsp{orig.reader.get()}.GetCollections(files, h.fs_table[0].GetOffset() + sha.pfs0_layer.offset));
    const auto it = std::ranges::find_if(files, [](const auto& f) { return f.name.ends_with(".cnmt"); });
    R_UNLESS(it != files.end() && it->size <= 1024 * 1024, Result_RepackUnsupportedNca);
    std::vector<u8> cnmt(it->size);
    R_TRY(orig.reader->Read2(cnmt.data(), it->offset, cnmt.size()));
    R_UNLESS(PatchCnmtContent(cnmt, CNMT_CONTENT_PROGRAM, program.hash, program.size), Result_RepackUnsupportedNca);

    // new PFS0 with that one file, behind a hash table of META_PFS0_BLOCK blocks.
    s64 pfs0_size;
    auto pfs0 = yati::container::Nsp::Build(std::array{yati::container::CollectionEntry{it->name, 0, (s64)cnmt.size()}}, pfs0_size);
    pfs0.insert(pfs0.end(), cnmt.begin(), cnmt.end());
    std::vector<u8> table;
    for (u64 i = 0; i < pfs0.size(); i += META_PFS0_BLOCK) {
        u8 hash[0x20];
        sha256CalculateHash(hash, pfs0.data() + i, std::min<u64>(META_PFS0_BLOCK, pfs0.size() - i));
        table.insert(table.end(), hash, hash + 0x20);
    }

    auto& fs = h.fs_header[0];
    fs = {};
    fs.version = 2;
    fs.fs_type = nca::FileSystemType_PFS0;
    fs.hash_type = nca::HashType_HierarchicalSha256;
    fs.encryption_type = nca::EncryptionType_None;
    auto& hs = fs.hash_data.hierarchical_sha256_data;
    sha256CalculateHash(hs.master_hash, table.data(), table.size());
    hs.block_size = META_PFS0_BLOCK;
    hs.layer_count = 2;
    hs.hash_layer = {0, table.size()};
    hs.pfs0_layer = {Align(table.size(), MEDIA), pfs0.size()};

    std::vector<u8> body(sizeof(h));
    table.resize(Align(table.size(), MEDIA));
    body.insert(body.end(), table.begin(), table.end());
    body.insert(body.end(), pfs0.begin(), pfs0.end());
    body.resize(Align(body.size(), MEDIA));
    for (u32 i = 1; i < NCA_SECTION_TOTAL; i++) {
        h.fs_table[i] = {};
        h.fs_header[i] = {};
        std::memset(&h.fs_header_hash[i], 0, sizeof(h.fs_header_hash[i]));
    }
    SetSection(h, 0, sizeof(h), body.size());

    const auto sealed = SealHeader(keys, h, body.size());
    std::memcpy(body.data(), sealed.data(), sealed.size());
    out.size = body.size();
    out.data = Mem(std::move(body));
    R_TRY(HashNca(nullptr, out));
    log_write("[repack] meta nca %s, %lu bytes\n", utils::hexIdToStr(out.id).str, out.size);
    R_SUCCEED();
}

} // namespace sphaira::repack
