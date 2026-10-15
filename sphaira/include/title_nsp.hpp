#pragma once

#include "fs.hpp"
#include "yati/container/base.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <switch.h>

namespace sphaira::title {

// the nca / ticket list of one installed component, straight out of ncm.
struct ContentInfoEntry {
    NsApplicationContentMetaStatus status{};
    std::vector<NcmContentInfo> content_infos{};
    std::vector<NcmRightsId> ncm_rights_id{};
};

// a ticket / cert pair belonging to an nsp, fetched from es.
struct TikEntry {
    FsRightsId id{};
    u8 key_gen{};
    std::vector<u8> tik_data{};
    std::vector<u8> cert_data{};
};

// one installed content component (base / update / dlc) presented as an nsp.
// the header, file table and ticket data are built up front, the nca payloads
// are read straight out of NcmContentStorage at the requested offset, so the
// nsp only ever exists as a stream - nothing is written to disk.
struct NspEntry {
    struct NcaStorage {
        NcmContentId content_id{};
        u8 storage_id{};
    };

    // an NCA that is not in ncm: a repack builds it on the fly.
    struct Generated {
        NcmContentId content_id{};
        std::shared_ptr<yati::source::Base> data{};
    };

    // application name.
    std::string application_name{};
    // name of the nsp (name [id][v0][BASE].nsp).
    fs::FsPath path{};
    // tickets and cert data, will be empty if title key crypto isn't used.
    std::vector<TikEntry> tickets{};
    // all the collections for this nsp, such as nca's and tickets.
    std::vector<yati::container::CollectionEntry> collections{};
    // raw nsp data (header, file table and string table).
    std::vector<u8> nsp_data{};
    // size of the entier nsp.
    s64 nsp_size{};
    // mapping from each NCA content ID to its original NCM storage ID.
    std::vector<NcaStorage> nca_storage{};
    // read instead of ncm for these content ids.
    std::vector<Generated> generated{};
    // copy of the icon, if invalid, it will use the default icon.
    int icon{};

    // reads from anywhere within the nsp. short reads happen at collection
    // boundaries, callers must loop on *bytes_read.
    Result Read(void* buf, s64 off, s64 size, u64* bytes_read);
};

// lists the ncas (and any rights ids) of one installed component. also used on
// its own to size a component and count its contents.
Result BuildContentEntry(const NsApplicationContentMetaStatus& status, ContentInfoEntry& out);

// builds one NspEntry per installed component of app_id matching flags
// (ContentFlag_*). name is the game's display name, used to name the nsp;
// app_folder puts each nsp inside its own "<name>/" folder (dump layout).
// entries are appended, so several titles can share one list.
Result BuildNspEntries(u64 app_id, const char* name, u32 flags, bool app_folder, std::vector<NspEntry>& out);

// builds one streamed NSP containing BASE, latest UPD, and all DLC for app_id.
Result BuildMergedNspEntry(u64 app_id, const char* name, NspEntry& out);

// edits the components of a merged NSP before it is laid out: a repack swaps an NCA for a rebuilt
// one (new content id and size in the content info) and adds its data to `generated`.
using ContentEdit = std::function<Result(std::vector<ContentInfoEntry>& entries, std::vector<NspEntry::Generated>& generated)>;

// builds one streamed NSP containing selected installed components (BASE, latest UPD, DLC) for app_id.
Result BuildMergedNspEntry(u64 app_id, const char* name, u32 flags, NspEntry& out, const ContentEdit& edit = {});

} // namespace sphaira::title
