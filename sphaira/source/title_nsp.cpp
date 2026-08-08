#include "title_nsp.hpp"
#include "title_info.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "utils/utils.hpp"

#include "yati/nx/ncm.hpp"
#include "yati/nx/nca.hpp"
#include "yati/nx/es.hpp"
#include "yati/nx/keys.hpp"
#include "yati/container/nsp.hpp"

#include <algorithm>
#include <cstring>
#include <ranges>

namespace sphaira::title {
namespace {

auto isRightsIdValid(FsRightsId id) -> bool {
    FsRightsId empty_id{};
    return 0 != std::memcmp(std::addressof(id), std::addressof(empty_id), sizeof(id));
}

auto InRange(s64 off, s64 offset, s64 size) -> bool {
    return off < offset + size && off >= offset;
}

auto ClipSize(s64 off, s64 size, s64 file_size) -> s64 {
    return std::min(size, file_size - off);
}

auto BuildNspPath(const char* name, const NsApplicationContentMetaStatus& status, bool app_folder) -> fs::FsPath {
    fs::FsPath name_buf = name;
    utilsReplaceIllegalCharacters(name_buf, true);

    // a title whose control never loaded has no name, fall back to its id so
    // the nsp is still addressable.
    if (name_buf.empty()) {
        std::snprintf(name_buf, sizeof(name_buf), "%016lX", status.application_id);
    }

    char version[sizeof(NacpStruct::display_version) + 1]{};
    if (status.meta_type == NcmContentMetaType_Patch) {
        u64 program_id;
        fs::FsPath path;
        if (R_SUCCEEDED(GetControlPathFromStatus(status, &program_id, &path))) {
            char display_version[0x10];
            if (R_SUCCEEDED(nca::ParseControl(path, program_id, display_version, sizeof(display_version), nullptr, offsetof(NacpStruct, display_version)))) {
                std::snprintf(version, sizeof(version), "%s ", display_version);
            }
        }
    }

    fs::FsPath path;
    if (app_folder) {
        std::snprintf(path, sizeof(path), "%s/%s %s[%016lX][v%u][%s].nsp", name_buf.s, name_buf.s, version, status.application_id, status.version, ncm::GetMetaTypeShortStr(status.meta_type));
    } else {
        std::snprintf(path, sizeof(path), "%s %s[%016lX][v%u][%s].nsp", name_buf.s, version, status.application_id, status.version, ncm::GetMetaTypeShortStr(status.meta_type));
    }

    return path;
}

} // namespace

Result BuildContentEntry(const NsApplicationContentMetaStatus& status, ContentInfoEntry& out) {
    auto& cs = GetNcmCs(status.storageID);
    auto& db = GetNcmDb(status.storageID);
    const auto app_id = ncm::GetAppId(status.meta_type, status.application_id);

    auto id_min = status.application_id;
    auto id_max = status.application_id;
    // workaround N bug where they don't check the full range in the ID filter.
    // https://github.com/Atmosphere-NX/Atmosphere/blob/1d3f3c6e56b994b544fc8cd330c400205d166159/libraries/libstratosphere/source/ncm/ncm_on_memory_content_meta_database_impl.cpp#L22
    if (status.storageID == NcmStorageId_None || status.storageID == NcmStorageId_GameCard) {
        id_min -= 1;
        id_max += 1;
    }

    s32 meta_total;
    s32 meta_entries_written;
    NcmContentMetaKey key;
    R_TRY(ncmContentMetaDatabaseList(std::addressof(db), std::addressof(meta_total), std::addressof(meta_entries_written), std::addressof(key), 1, (NcmContentMetaType)status.meta_type, app_id, id_min, id_max, NcmContentInstallType_Full));
    log_write("ncmContentMetaDatabaseList(): AppId: %016lX Id: %016lX total: %d written: %d storageID: %u key.id %016lX\n", app_id, status.application_id, meta_total, meta_entries_written, status.storageID, key.id);
    R_UNLESS(meta_total == 1, Result_GameMultipleKeysFound);
    R_UNLESS(meta_entries_written == 1, Result_GameMultipleKeysFound);

    std::vector<NcmContentInfo> cnmt_infos;
    for (s32 i = 0; ; i++) {
        s32 entries_written;
        NcmContentInfo info_out;
        R_TRY(ncmContentMetaDatabaseListContentInfo(std::addressof(db), std::addressof(entries_written), std::addressof(info_out), 1, std::addressof(key), i));

        if (!entries_written) {
            break;
        }

        // check if we need to fetch tickets.
        NcmRightsId ncm_rights_id;
        R_TRY(ncmContentStorageGetRightsIdFromContentId(std::addressof(cs), std::addressof(ncm_rights_id), std::addressof(info_out.content_id), FsContentAttributes_All));

        if (isRightsIdValid(ncm_rights_id.rights_id)) {
            const auto it = std::ranges::find_if(out.ncm_rights_id, [&ncm_rights_id](auto& e){
                return !std::memcmp(&e, &ncm_rights_id, sizeof(ncm_rights_id));
            });

            if (it == out.ncm_rights_id.end()) {
                out.ncm_rights_id.emplace_back(ncm_rights_id);
            }
        }

        if (info_out.content_type == NcmContentType_Meta) {
            cnmt_infos.emplace_back(info_out);
        } else {
            out.content_infos.emplace_back(info_out);
        }
    }

    // append cnmt at the end of the list, following StandardNSP spec.
    out.content_infos.insert_range(out.content_infos.end(), cnmt_infos);
    out.status = status;
    R_SUCCEED();
}

namespace {

Result BuildNspEntry(const char* name, const ContentInfoEntry& info, const keys::Keys& keys, bool app_folder, NspEntry& out) {
    out.application_name = name;
    out.path = BuildNspPath(name, info.status, app_folder);
    s64 offset{};

    for (auto& e : info.content_infos) {
        char nca_name[0x200];
        std::snprintf(nca_name, sizeof(nca_name), "%s%s", utils::hexIdToStr(e.content_id).str, e.content_type == NcmContentType_Meta ? ".cnmt.nca" : ".nca");

        u64 size;
        ncmContentInfoSizeToU64(std::addressof(e), std::addressof(size));

        out.collections.emplace_back(nca_name, offset, size);
        offset += size;
    }

    for (auto& ncm_rights_id : info.ncm_rights_id) {
        const auto rights_id = ncm_rights_id.rights_id;
        const auto key_gen = ncm_rights_id.key_generation;

        TikEntry entry{rights_id, key_gen};
        log_write("rights id is valid, fetching common ticket and cert\n");

        u64 tik_size;
        u64 cert_size;
        R_TRY(es::GetCommonTicketAndCertificateSize(&tik_size, &cert_size, &rights_id));
        log_write("got tik_size: %zu cert_size: %zu\n", tik_size, cert_size);

        entry.tik_data.resize(tik_size);
        entry.cert_data.resize(cert_size);
        R_TRY(es::GetCommonTicketAndCertificateData(&tik_size, &cert_size, entry.tik_data.data(), entry.tik_data.size(), entry.cert_data.data(), entry.cert_data.size(), &rights_id));
        log_write("got tik_data: %zu cert_data: %zu\n", tik_size, cert_size);

        // patch fake ticket / convert personalised to common if needed.
        R_TRY(es::PatchTicket(entry.tik_data, entry.cert_data, key_gen, keys, App::GetApp()->m_dump_convert_to_common_ticket.Get()));

        char tik_name[0x200];
        std::snprintf(tik_name, sizeof(tik_name), "%s%s", utils::hexIdToStr(rights_id).str, ".tik");

        char cert_name[0x200];
        std::snprintf(cert_name, sizeof(cert_name), "%s%s", utils::hexIdToStr(rights_id).str, ".cert");

        out.collections.emplace_back(tik_name, offset, entry.tik_data.size());
        offset += entry.tik_data.size();

        out.collections.emplace_back(cert_name, offset, entry.cert_data.size());
        offset += entry.cert_data.size();

        out.tickets.emplace_back(entry);
    }

    out.nsp_data = yati::container::Nsp::Build(out.collections, out.nsp_size);
    out.cs = GetNcmCs(info.status.storageID);

    R_SUCCEED();
}

} // namespace

// todo: benchmark manual sdcard read and decryption vs ncm.
Result NspEntry::Read(void* buf, s64 off, s64 size, u64* bytes_read) {
    *bytes_read = 0;

    // past the end of the nsp, report eof rather than failing the transfer.
    if (off < 0 || off >= nsp_size) {
        R_SUCCEED();
    }

    if (off < (s64)nsp_data.size()) {
        *bytes_read = size = ClipSize(off, size, nsp_data.size());
        std::memcpy(buf, nsp_data.data() + off, size);
        R_SUCCEED();
    }

    // adjust offset.
    off -= nsp_data.size();

    for (const auto& collection : collections) {
        if (InRange(off, collection.offset, collection.size)) {
            // adjust offset relative to the collection.
            off -= collection.offset;
            *bytes_read = size = ClipSize(off, size, collection.size);

            if (collection.name.ends_with(".nca")) {
                const auto id = ncm::GetContentIdFromStr(collection.name.c_str());
                return ncmContentStorageReadContentIdFile(&cs, buf, size, &id, off);
            } else if (collection.name.ends_with(".tik") || collection.name.ends_with(".cert")) {
                FsRightsId id;
                keys::parse_hex_key(&id, collection.name.c_str());

                const auto it = std::ranges::find_if(tickets, [&id](auto& e){
                    return !std::memcmp(&id, &e.id, sizeof(id));
                });
                R_UNLESS(it != tickets.end(), Result_GameBadReadForDump);

                const auto& data = collection.name.ends_with(".tik") ? it->tik_data : it->cert_data;
                std::memcpy(buf, data.data() + off, size);
                R_SUCCEED();
            }
        }
    }

    log_write("did not find collection...\n");
    *bytes_read = 0;
    return 0x1;
}

Result BuildNspEntries(u64 app_id, const char* name, u32 flags, bool app_folder, std::vector<NspEntry>& out) {
    MetaEntries meta_entries;
    R_TRY(GetMetaEntries(app_id, meta_entries, flags));

    keys::Keys keys;
    R_TRY(keys::parse_keys(keys, true));

    for (const auto& status : meta_entries) {
        ContentInfoEntry info;
        R_TRY(BuildContentEntry(status, info));

        NspEntry nsp;
        R_TRY(BuildNspEntry(name, info, keys, app_folder, nsp));
        out.emplace_back(std::move(nsp));
    }

    R_UNLESS(!out.empty(), Result_GameNoNspEntriesBuilt);
    R_SUCCEED();
}

} // namespace sphaira::title
