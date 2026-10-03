// DOCS_DEMO builds only: libnx calls wrapped with -Wl,--wrap=<fn> (sphaira/CMakeLists.txt).
// List calls return the real result plus the demo entries, so Eden's own content stays; calls for one id answer
// for a demo id from titles.json and pass every other id to the real function.

#include "demo/demo_data.hpp"
#include "defines.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using sphaira::demo::FindTitle;
using sphaira::demo::Title;

constexpr u32 DEMO_CONTENT_MAGIC = 0x4F4D4544; // "DEMO": NcmContentId bytes 8..11 of a demo content

// what ns reports as installed for a demo game: base, its updates, its add-ons.
auto MetaStatus(const Title& t) -> std::vector<NsApplicationContentMetaStatus> {
    std::vector<NsApplicationContentMetaStatus> out;
    out.push_back({NcmContentMetaType_Application, t.storage, 0, 0, 0, t.id});
    for (const auto& u : t.updates) {
        out.push_back({NcmContentMetaType_Patch, t.storage, 0, 0, u.version, t.id + 0x800});
    }
    for (int i = 1; i <= t.dlc; i++) {
        out.push_back({NcmContentMetaType_AddOnContent, t.storage, 0, 0, 0, t.id + 0x1000 + u64(i)});
    }
    return out;
}

auto ContentSize(const Title& t, u64 id) -> s64 {
    if (id == t.id) return t.size;
    if (id == t.id + 0x800) return t.updates.empty() ? 0 : t.updates.back().size;
    return sphaira::demo::DLC_SIZE;
}

auto IsDemoContent(const NcmContentId* id) -> bool {
    u32 magic;
    std::memcpy(&magic, id->c + 8, sizeof(magic));
    return magic == DEMO_CONTENT_MAGIC;
}

} // namespace

extern "C" {

Result __real_nsListApplicationRecord(NsApplicationRecord* records, s32 count, s32 entry_offset, s32* out_entrycount);
Result __real_nsGetApplicationControlData(NsApplicationControlSource source, u64 application_id, NsApplicationControlData* buffer, size_t size, u64* actual_size);
Result __real_nsListApplicationContentMetaStatus(u64 application_id, s32 index, NsApplicationContentMetaStatus* list, s32 count, s32* out_entrycount);
Result __real_ncmContentMetaDatabaseList(NcmContentMetaDatabase* db, s32* out_entries_total, s32* out_entries_written, NcmContentMetaKey* out_keys, s32 count, NcmContentMetaType meta_type, u64 id, u64 id_min, u64 id_max, NcmContentInstallType install_type);
Result __real_ncmContentMetaDatabaseListContentInfo(NcmContentMetaDatabase* db, s32* out_entries_written, NcmContentInfo* out_info, s32 count, const NcmContentMetaKey* key, s32 start_index);
Result __real_ncmContentMetaDatabaseGet(NcmContentMetaDatabase* db, const NcmContentMetaKey* key, u64* out_size, void* out_data, u64 out_data_size);
Result __real_ncmContentStorageGetRightsIdFromContentId(NcmContentStorage* cs, NcmRightsId* out_rights_id, const NcmContentId* content_id, FsContentAttributes attr);

// real records first, demo ids after them: offsets past the real total index the demo list.
Result __wrap_nsListApplicationRecord(NsApplicationRecord* records, s32 count, s32 entry_offset, s32* out_entrycount) {
    s32 real{};
    R_TRY(__real_nsListApplicationRecord(records, count, entry_offset, &real));
    *out_entrycount = real;
    if (real == count) {
        R_SUCCEED();
    }

    // a short read means the real list ends here; with nothing read, count it.
    s32 real_total = entry_offset + real;
    if (!real && entry_offset) {
        NsApplicationRecord tmp[64];
        real_total = 0;
        for (s32 n; R_SUCCEEDED(__real_nsListApplicationRecord(tmp, 64, real_total, &n)) && n > 0; ) {
            real_total += n;
        }
    }

    const auto titles = sphaira::demo::Titles();
    const s32 first = std::max(0, entry_offset - real_total);
    for (s32 i = first; i < (s32)titles.size() && *out_entrycount < count; i++) {
        records[(*out_entrycount)++] = NsApplicationRecord{.application_id = titles[i].id};
    }

    R_SUCCEED();
}

// NACP with the name in the current UI language in every language slot, so the docs language decides the name.
Result __wrap_nsGetApplicationControlData(NsApplicationControlSource source, u64 application_id, NsApplicationControlData* buffer, size_t size, u64* actual_size) {
    const auto t = FindTitle(application_id);
    if (!t || t->id != application_id) {
        return __real_nsGetApplicationControlData(source, application_id, buffer, size, actual_size);
    }
    R_UNLESS(size >= sizeof(NacpStruct), 0x1);

    std::memset(buffer, 0, size);
    auto& nacp = buffer->nacp;
    const auto name = sphaira::demo::Name(*t);
    for (auto& lang : nacp.lang_data.lang) {
        std::snprintf(lang.name, sizeof(lang.name), "%s", name.c_str());
        std::snprintf(lang.author, sizeof(lang.author), "%s", t->publisher.c_str());
    }
    nacp.supported_language_flag = 0xFFFF;
    std::snprintf(nacp.display_version, sizeof(nacp.display_version), "%s", t->version.c_str());

    const auto icon = sphaira::demo::Icon(*t);
    const auto icon_size = std::min(icon.size(), size - sizeof(NacpStruct));
    std::memcpy(buffer->icon, icon.data(), icon_size);
    *actual_size = sizeof(NacpStruct) + icon_size;
    R_SUCCEED();
}

Result __wrap_nsListApplicationContentMetaStatus(u64 application_id, s32 index, NsApplicationContentMetaStatus* list, s32 count, s32* out_entrycount) {
    const auto t = FindTitle(application_id);
    if (!t || t->id != application_id) {
        return __real_nsListApplicationContentMetaStatus(application_id, index, list, count, out_entrycount);
    }

    const auto status = MetaStatus(*t);
    *out_entrycount = 0;
    for (s32 i = index; i < (s32)status.size() && *out_entrycount < count; i++) {
        list[(*out_entrycount)++] = status[i];
    }
    R_SUCCEED();
}

// title_nsp.cpp BuildContentEntry: the meta key of one installed demo content (base, update or add-on).
Result __wrap_ncmContentMetaDatabaseList(NcmContentMetaDatabase* db, s32* out_entries_total, s32* out_entries_written, NcmContentMetaKey* out_keys, s32 count, NcmContentMetaType meta_type, u64 id, u64 id_min, u64 id_max, NcmContentInstallType install_type) {
    const auto t = FindTitle(id);
    if (!t || t->id != id) {
        return __real_ncmContentMetaDatabaseList(db, out_entries_total, out_entries_written, out_keys, count, meta_type, id, id_min, id_max, install_type);
    }

    *out_entries_total = *out_entries_written = 0;
    for (const auto& s : MetaStatus(*t)) {
        if (s.meta_type == meta_type && s.application_id >= id_min && s.application_id <= id_max && count > 0) {
            out_keys[0] = NcmContentMetaKey{.id = s.application_id, .version = s.version, .type = s.meta_type, .install_type = NcmContentInstallType_Full};
            *out_entries_total = *out_entries_written = 1;
            break;
        }
    }
    R_SUCCEED();
}

// two contents per demo key: the program (all of the size) and the cnmt.
Result __wrap_ncmContentMetaDatabaseListContentInfo(NcmContentMetaDatabase* db, s32* out_entries_written, NcmContentInfo* out_info, s32 count, const NcmContentMetaKey* key, s32 start_index) {
    const auto t = FindTitle(key->id);
    if (!t) {
        return __real_ncmContentMetaDatabaseListContentInfo(db, out_entries_written, out_info, count, key, start_index);
    }

    *out_entries_written = 0;
    for (s32 i = start_index; i < 2 && *out_entries_written < count; i++) {
        auto& info = out_info[(*out_entries_written)++];
        info = {};
        std::memcpy(info.content_id.c, &key->id, sizeof(key->id));
        std::memcpy(info.content_id.c + 8, &DEMO_CONTENT_MAGIC, 4);
        info.content_id.c[12] = u8(i);
        info.content_type = i ? NcmContentType_Meta : NcmContentType_Program;
        ncmU64ToContentInfoSize(i ? 0x1000 : ContentSize(*t, key->id), &info);
    }
    R_SUCCEED();
}

// content meta header of a demo key (ncm::GetContentMeta, used by the move plan): just the content count.
Result __wrap_ncmContentMetaDatabaseGet(NcmContentMetaDatabase* db, const NcmContentMetaKey* key, u64* out_size, void* out_data, u64 out_data_size) {
    if (!FindTitle(key->id)) {
        return __real_ncmContentMetaDatabaseGet(db, key, out_size, out_data, out_data_size);
    }
    R_UNLESS(out_data_size >= sizeof(NcmContentMetaHeader), 0x1);
    std::memset(out_data, 0, out_data_size);
    NcmContentMetaHeader header{};
    header.content_count = 2;
    std::memcpy(out_data, &header, sizeof(header));
    *out_size = sizeof(header);
    R_SUCCEED();
}

// demo contents have no ticket.
Result __wrap_ncmContentStorageGetRightsIdFromContentId(NcmContentStorage* cs, NcmRightsId* out_rights_id, const NcmContentId* content_id, FsContentAttributes attr) {
    if (!IsDemoContent(content_id)) {
        return __real_ncmContentStorageGetRightsIdFromContentId(cs, out_rights_id, content_id, attr);
    }
    *out_rights_id = {};
    R_SUCCEED();
}

} // extern "C"
