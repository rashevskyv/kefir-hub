#include "repack/program_romfs.hpp"
#include "repack/bktr_table.hpp"

#include "defines.hpp"
#include "log.hpp"
#include "yati/nx/crypto.hpp"
#include "yati/nx/nca.hpp"
#include "yati/nx/ncm.hpp"

#include <cstring>
#include <mutex>
#include <vector>

namespace sphaira::repack {
namespace {

constexpr u32 BKTR_MAGIC = 0x52544B42;

struct OpenedNca : NcaFile {
    u32 romfs{}; // romfs section index.
};

Result OpenProgramNca(const keys::Keys& keys, NcmContentStorage* cs, const NcmContentId& id, OpenedNca& out) {
    R_TRY(OpenNca(keys, cs, id, out));
    for (u32 i = 0; i < out.header.GetSectionCount(); i++) {
        const auto& fs = out.header.fs_header[i];
        if (fs.fs_type != nca::FileSystemType_RomFS) {
            continue;
        }
        // ponytail: sparse base NCAs and compressed sections are refused; add their bucket
        // storages here when a game needs them.
        u32 sparse_magic;
        std::memcpy(&sparse_magic, fs.spares_info + 0x10, sizeof(sparse_magic));
        R_UNLESS(sparse_magic != BKTR_MAGIC && !fs.compression_info.table_size, Result_RepackUnsupportedNca);
        R_UNLESS(fs.hash_data.integrity_meta_info.magic == 0x43465649, Result_RepackUnsupportedNca);
        out.romfs = i;
        R_SUCCEED();
    }
    R_THROW(Result_RepackUnsupportedNca);
}

auto IsBktr(const nca::FsHeader& fs) -> bool {
    return fs.encryption_type == nca::EncryptionType_AesCtrEx || fs.encryption_type == nca::EncryptionType_AesCtrExSkipLayerHash;
}

struct BktrStorage final : yati::source::Base {
    OpenedNca base, patch;
    u64 base_section{}, patch_section{}, section_ctr{};
    std::vector<bktr::IndirectEntry> indirect;
    std::vector<bktr::CtrExEntry> ctr_ex;
    u64 end{}, ctr_ex_end{};
    std::vector<u8> buf = std::vector<u8>(1024 * 1024);
    std::mutex mutex;

    Result Init() {
        base_section = base.header.fs_table[base.romfs].GetOffset();
        patch_section = patch.header.fs_table[patch.romfs].GetOffset();
        const auto& fs = patch.header.fs_header[patch.romfs];
        section_ctr = fs.section_ctr;

        const auto& info = fs.patch_info;
        R_UNLESS(info.indirect_header.magic == BKTR_MAGIC && info.aes_ctr_header.magic == BKTR_MAGIC, Result_RepackBadBktr);

        // both tables are encrypted with the plain section counter, so the NcaReader decrypts them.
        std::vector<u8> table(info.indirect_size);
        R_TRY(patch.reader->Read2(table.data(), patch_section + info.indirect_offset, table.size()));
        R_UNLESS(bktr::ParseIndirect(table, info.indirect_header.count, indirect, end), Result_RepackBadBktr);

        table.resize(info.aes_ctr_size);
        R_TRY(patch.reader->Read2(table.data(), patch_section + info.aes_ctr_offset, table.size()));
        R_UNLESS(bktr::ParseCtrEx(table, info.aes_ctr_header.count, ctr_ex, ctr_ex_end), Result_RepackBadBktr);
        log_write("[repack] bktr: %zu indirect, %zu aes ctr ex entries, size %lu\n", indirect.size(), ctr_ex.size(), end);
        R_SUCCEED();
    }

    // patch data: AES-CTR with the generation of each AesCtrEx range in the counter.
    Result ReadPatch(u8* out, u64 off, u64 len) {
        Result rc = 0;
        const auto ok = bktr::ForEachPiece(ctr_ex, ctr_ex_end, off, len, [&](const bktr::CtrExEntry& e, u64 pos, u64 n) {
            while (n && R_SUCCEEDED(rc)) {
                const auto abs = patch_section + pos;
                const auto aligned = abs & ~u64{0xF};
                const auto head = abs - aligned;
                const auto chunk = std::min<u64>(n, buf.size() - 0x10);
                const auto span = (head + chunk + 0xF) & ~u64{0xF};
                if (R_FAILED(rc = patch.raw->Read2(buf.data(), aligned, span))) {
                    break;
                }

                u8 ctr[0x10];
                crypto::SetCtr(ctr, bktr::CtrUpper(section_ctr, e.generation), aligned);
                Aes128CtrContext ctx;
                aes128CtrContextCreate(&ctx, patch.key.key, ctr);
                aes128CtrCrypt(&ctx, buf.data(), buf.data(), span);

                std::memcpy(out, buf.data() + head, chunk);
                out += chunk;
                pos += chunk;
                n -= chunk;
            }
            return R_SUCCEEDED(rc);
        });
        R_TRY(rc);
        R_UNLESS(ok, Result_RepackBadBktr);
        R_SUCCEED();
    }

    Result Read(void* _buf, s64 off, s64 size, u64* bytes_read) override {
        std::scoped_lock lock{mutex};
        *bytes_read = 0;
        size = std::min<s64>(size, off < (s64)end ? end - off : 0);
        auto out = static_cast<u8*>(_buf);

        Result rc = 0;
        const auto ok = bktr::ForEachPiece(indirect, end, off, size, [&](const bktr::IndirectEntry& e, u64 pos, u64 n) {
            const auto phys = e.phys + (pos - e.offset);
            if (e.storage == bktr::STORAGE_BASE) {
                rc = base.reader->Read2(out, base_section + phys, n);
            } else {
                rc = ReadPatch(out, phys, n);
            }
            out += n;
            return R_SUCCEEDED(rc);
        });
        R_TRY(rc);
        R_UNLESS(ok, Result_RepackBadBktr);
        *bytes_read = size;
        R_SUCCEED();
    }

    Result GetSize(s64* out) override {
        *out = end;
        R_SUCCEED();
    }
};

void SetRomfsRange(const nca::FsHeader& fs, u64 section, ProgramRomfs& out) {
    const auto& level = fs.hash_data.integrity_meta_info.info_level_hash.levels[5];
    out.offset = section + level.logical_offset;
    out.size = level.hash_data_size;
}

} // namespace

Result OpenNca(const keys::Keys& keys, NcmContentStorage* cs, const NcmContentId& id, NcaFile& out) {
    auto src = std::make_shared<ncm::NcmSource>(cs, &id);
    R_TRY(src->GetSize(&out.size));
    out.raw = src;
    R_TRY(src->Read2(&out.header, 0, sizeof(out.header)));
    R_TRY(nca::DecryptHeader(&out.header, keys, out.header));
    R_TRY(nca::GetDecryptedTitleKey(out.header, keys, out.key));
    out.reader = std::make_shared<nca::NcaReader>(out.header, out.key.key, out.size, src);
    R_SUCCEED();
}

Result OpenProgramRomfs(const keys::Keys& keys, NcmContentStorage* base_cs, const NcmContentId& base, NcmContentStorage* patch_cs, const NcmContentId* patch, ProgramRomfs& out) {
    out = {};
    OpenedNca b;
    R_TRY(OpenProgramNca(keys, base_cs, base, b));
    R_UNLESS(!IsBktr(b.header.fs_header[b.romfs]), Result_RepackUnsupportedNca);

    if (!patch) {
        out.storage = b.reader;
        SetRomfsRange(b.header.fs_header[b.romfs], b.header.fs_table[b.romfs].GetOffset(), out);
        R_SUCCEED();
    }

    OpenedNca p;
    R_TRY(OpenProgramNca(keys, patch_cs, *patch, p));
    const auto& pfs = p.header.fs_header[p.romfs];
    if (!IsBktr(pfs)) {
        // an update that ships its whole romfs.
        out.storage = p.reader;
        SetRomfsRange(pfs, p.header.fs_table[p.romfs].GetOffset(), out);
        R_SUCCEED();
    }

    auto storage = std::make_shared<BktrStorage>();
    storage->base = std::move(b);
    storage->patch = std::move(p);
    R_TRY(storage->Init());
    // the indirect storage spans the patched section, so romfs offsets are section-relative.
    SetRomfsRange(pfs, 0, out);
    R_UNLESS(out.offset + out.size <= storage->end, Result_RepackBadBktr);
    out.storage = std::move(storage);
    R_SUCCEED();
}

} // namespace sphaira::repack
