#include "ui/menus/save/save_paths.hpp"
#include "save_internal.hpp"
#include "defines.hpp"
#include <cstring>
#include <algorithm>
#include <limits>

namespace sphaira::ui::menu::save {

inline auto ReadU8(const u8* p) -> u8 {
    return p[0];
}
inline auto ReadU16LE(const u8* p) -> u16 {
    return static_cast<u16>(p[0]) |
          (static_cast<u16>(p[1]) << 8);
}
inline auto ReadU32LE(const u8* p) -> u32 {
    return static_cast<u32>(p[0]) |
          (static_cast<u32>(p[1]) << 8) |
          (static_cast<u32>(p[2]) << 16) |
          (static_cast<u32>(p[3]) << 24);
}
inline auto ReadU64LE(const u8* p) -> u64 {
    return static_cast<u64>(p[0]) |
          (static_cast<u64>(p[1]) << 8) |
          (static_cast<u64>(p[2]) << 16) |
          (static_cast<u64>(p[3]) << 24) |
          (static_cast<u64>(p[4]) << 32) |
          (static_cast<u64>(p[5]) << 40) |
          (static_cast<u64>(p[6]) << 48) |
          (static_cast<u64>(p[7]) << 56);
}
inline auto ReadS64LE(const u8* p) -> s64 {
    return static_cast<s64>(ReadU64LE(p));
}

auto ValidateDecodedSaveMeta(const DecodedSaveMetaInternal& m, bool is_86_layout) -> bool {
    // type 0..6
    if (m.save_data_type > FsSaveDataType_SystemBcat) {
        return false;
    }
    // rank 0..1
    if (m.save_data_rank > FsSaveDataRank_Secondary) {
        return false;
    }
    // data/journal signed s64 >= 0
    if (m.data_size < 0 || m.journal_size < 0) {
        return false;
    }

    if (m.save_data_type == FsSaveDataType_Account) {
        // Account: nonzero app, systemID 0, full 128-bit UID nonzero
        if (m.application_id == 0) {
            return false;
        }
        if (m.system_save_data_id != 0) {
            return false;
        }
        if (m.uid.uid[0] == 0 && m.uid.uid[1] == 0) {
            return false;
        }
    } else if (m.save_data_type == FsSaveDataType_System || m.save_data_type == FsSaveDataType_SystemBcat) {
        // System/SystemBcat: nonzero systemID
        if (m.system_save_data_id == 0) {
            return false;
        }
    } else {
        // Other types: nonzero app
        if (m.application_id == 0) {
            return false;
        }
    }

    // source space 86 allow 0,1,2,3,4,100,101; reject 255/All/unknown
    if (is_86_layout) {
        if (!m.source_space.has_value()) {
            return false;
        }
        const auto sp = *m.source_space;
        const bool valid_space = (sp == FsSaveDataSpaceId_System ||
                                  sp == FsSaveDataSpaceId_User ||
                                  sp == FsSaveDataSpaceId_SdSystem ||
                                  sp == FsSaveDataSpaceId_Temporary ||
                                  sp == FsSaveDataSpaceId_SdUser ||
                                  sp == FsSaveDataSpaceId_ProperSystem ||
                                  sp == FsSaveDataSpaceId_SafeMode);
        if (!valid_space) {
            return false;
        }
    }

    return true;
}

auto DecodeJksv85(const u8* p, DecodedSaveMetaInternal& out) -> bool {
    const u32 magic = ReadU32LE(p + 0);
    if (magic != JKSV_SAVE_META_MAGIC) {
        return false;
    }
    const u8 revision = ReadU8(p + 4);
    if (revision != JKSV_SAVE_META_REVISION) {
        return false;
    }
    out = DecodedSaveMetaInternal{};
    out.application_id = ReadU64LE(p + 5);
    out.uid.uid[0] = ReadU64LE(p + 13);
    out.uid.uid[1] = ReadU64LE(p + 21);
    out.system_save_data_id = ReadU64LE(p + 29);
    out.save_data_type = ReadU8(p + 37);
    out.save_data_rank = ReadU8(p + 38);
    out.save_data_index = ReadU16LE(p + 39);
    out.owner_id = ReadU64LE(p + 41);
    out.timestamp = ReadU64LE(p + 49);
    out.flags = ReadU32LE(p + 57);
    out.data_size = ReadS64LE(p + 61);
    out.journal_size = ReadS64LE(p + 69);
    out.commit_id = ReadU64LE(p + 77);
    out.source_space = std::nullopt;
    out.raw_size = 0;
    out.unk_x54 = 0;

    return ValidateDecodedSaveMeta(out, false);
}

auto DecodeJksvTail86(const u8* p, DecodedSaveMetaInternal& out) -> bool {
    if (!DecodeJksv85(p, out)) {
        return false;
    }
    out.source_space = ReadU8(p + 85);
    return ValidateDecodedSaveMeta(out, true);
}

auto DecodeJksvMiddle86(const u8* p, DecodedSaveMetaInternal& out) -> bool {
    const u32 magic = ReadU32LE(p + 0);
    if (magic != JKSV_SAVE_META_MAGIC) {
        return false;
    }
    const u8 revision = ReadU8(p + 4);
    if (revision != JKSV_SAVE_META_REVISION) {
        return false;
    }
    out = DecodedSaveMetaInternal{};
    out.application_id = ReadU64LE(p + 5);
    out.uid.uid[0] = ReadU64LE(p + 13);
    out.uid.uid[1] = ReadU64LE(p + 21);
    out.system_save_data_id = ReadU64LE(p + 29);
    out.save_data_type = ReadU8(p + 37);
    out.save_data_rank = ReadU8(p + 38);
    out.save_data_index = ReadU16LE(p + 39);
    out.source_space = ReadU8(p + 41);
    out.owner_id = ReadU64LE(p + 42);
    out.timestamp = ReadU64LE(p + 50);
    out.flags = ReadU32LE(p + 58);
    out.data_size = ReadS64LE(p + 62);
    out.journal_size = ReadS64LE(p + 70);
    out.commit_id = ReadU64LE(p + 78);
    out.raw_size = 0;
    out.unk_x54 = 0;

    return ValidateDecodedSaveMeta(out, true);
}

auto CompareCommonSourceFields(const DecodedSaveMetaInternal& a, const DecodedSaveMetaInternal& b) -> bool {
    return (a.application_id == b.application_id) &&
           (a.uid.uid[0] == b.uid.uid[0] && a.uid.uid[1] == b.uid.uid[1]) &&
           (a.system_save_data_id == b.system_save_data_id) &&
           (a.save_data_type == b.save_data_type) &&
           (a.save_data_rank == b.save_data_rank) &&
           (a.save_data_index == b.save_data_index) &&
           (a.owner_id == b.owner_id) &&
           (a.timestamp == b.timestamp) &&
           (a.flags == b.flags) &&
           (a.data_size == b.data_size) &&
           (a.journal_size == b.journal_size) &&
           (a.commit_id == b.commit_id);
}

auto DecodeJksv86WithAmbiguityCheck(const u8* p, DecodedSaveMetaInternal& out) -> bool {
    DecodedSaveMetaInternal tail{};
    DecodedSaveMetaInternal mid{};
    const bool tail_valid = DecodeJksvTail86(p, tail);
    const bool mid_valid = DecodeJksvMiddle86(p, mid);

    if (!tail_valid && !mid_valid) {
        return false;
    }
    if (tail_valid && !mid_valid) {
        out = tail;
        return true;
    }
    if (!tail_valid && mid_valid) {
        out = mid;
        return true;
    }

    // Both valid: accept only identical decoded source semantics including space
    if (CompareCommonSourceFields(tail, mid) && tail.source_space == mid.source_space) {
        out = tail;
        return true;
    }
    // Different valid interpretations -> fail closed
    return false;
}

auto DecodeSphaira128(const u8* p, DecodedSaveMetaInternal& out) -> bool {
    const u32 magic = ReadU32LE(p + 0);
    if (magic != NX_SAVE_META_MAGIC) {
        return false;
    }
    const u32 version = ReadU32LE(p + 4);
    if (version != NX_SAVE_META_VERSION) {
        return false;
    }
    out = DecodedSaveMetaInternal{};
    out.application_id = ReadU64LE(p + 8);
    out.uid.uid[0] = ReadU64LE(p + 16);
    out.uid.uid[1] = ReadU64LE(p + 24);
    out.system_save_data_id = ReadU64LE(p + 32);
    out.save_data_type = ReadU8(p + 40);
    out.save_data_rank = ReadU8(p + 41);
    out.save_data_index = ReadU16LE(p + 42);
    // bytes 44..47 pad, 48..71 unk ignored (no blanket zero requirement)
    out.owner_id = ReadU64LE(p + 72);
    out.timestamp = ReadU64LE(p + 80);
    out.flags = ReadU32LE(p + 88);
    out.unk_x54 = ReadU32LE(p + 92);
    out.data_size = ReadS64LE(p + 96);
    out.journal_size = ReadS64LE(p + 104);
    out.commit_id = ReadU64LE(p + 112);
    out.raw_size = ReadU64LE(p + 120);
    out.source_space = std::nullopt;

    return ValidateDecodedSaveMeta(out, false);
}

auto DecodeDbiRaw512(const u8* p, DecodedSaveMetaInternal& out) -> bool {
    out = DecodedSaveMetaInternal{};
    out.application_id = ReadU64LE(p + 0);
    out.uid.uid[0] = ReadU64LE(p + 8);
    out.uid.uid[1] = ReadU64LE(p + 16);
    out.system_save_data_id = ReadU64LE(p + 24);
    out.save_data_type = ReadU8(p + 32);
    out.save_data_rank = ReadU8(p + 33);
    out.save_data_index = ReadU16LE(p + 34);
    // 36..39 pad, 40..63 unk ignored
    out.owner_id = ReadU64LE(p + 64);
    out.timestamp = ReadU64LE(p + 72);
    out.flags = ReadU32LE(p + 80);
    out.unk_x54 = ReadU32LE(p + 84);
    out.data_size = ReadS64LE(p + 88);
    out.journal_size = ReadS64LE(p + 96);
    out.commit_id = ReadU64LE(p + 104);
    // 112..511 unused ignored
    out.raw_size = 0;
    out.source_space = std::nullopt;

    return ValidateDecodedSaveMeta(out, false);
}

auto DecodeNxSaveMeta(const u8* p, size_t size, DecodedSaveMetaInternal& out) -> bool {
    if (size == 85) {
        return DecodeJksv85(p, out);
    } else if (size == 86) {
        return DecodeJksv86WithAmbiguityCheck(p, out);
    } else if (size == 128) {
        return DecodeSphaira128(p, out);
    }
    return false;
}

auto ToNXSaveMeta(const DecodedSaveMetaInternal& d) -> NXSaveMeta {
    NXSaveMeta m{};
    m.magic = NX_SAVE_META_MAGIC;
    m.version = NX_SAVE_META_VERSION;
    m.attr.application_id = d.application_id;
    m.attr.uid = d.uid;
    m.attr.system_save_data_id = d.system_save_data_id;
    m.attr.save_data_type = d.save_data_type;
    m.attr.save_data_rank = d.save_data_rank;
    m.attr.save_data_index = d.save_data_index;
    m.owner_id = d.owner_id;
    m.timestamp = d.timestamp;
    m.flags = d.flags;
    m.unk_x54 = d.unk_x54;
    m.data_size = d.data_size;
    m.journal_size = d.journal_size;
    m.commit_id = d.commit_id;
    m.raw_size = d.raw_size;
    return m;
}

} // namespace sphaira::ui::menu::save
