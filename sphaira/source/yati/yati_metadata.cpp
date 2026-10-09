#include "yati_internal.hpp"
#include "orphan_content.hpp"
#include "path_util.hpp"
#include "yati/nx/nca.hpp"
#include "yati/nx/ncm.hpp"
#include "yati/nx/ns.hpp"
#include "yati/nx/es.hpp"
#include "yati/nx/keys.hpp"
#include "ui/progress_box.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "hats_version.hpp"
#include "version_compare.hpp"
#include "utils/utils.hpp"

#include <switch.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

namespace sphaira::yati::detail {
namespace {

// nca/tik/cert filenames inside a container may use either case for the
// hex id and extension (e.g. "1A2B....NCA"), so all name matching below
// must be case-insensitive.
auto FindIC(std::string_view haystack, std::string_view needle) -> bool {
    if (needle.size() > haystack.size()) {
        return false;
    }
    return !std::ranges::search(haystack, needle, [](unsigned char a, unsigned char b){
        return std::tolower(a) == std::tolower(b);
    }).empty();
}

// stdio-like wrapper for std::vector
struct BufHelper {
    BufHelper() = default;
    BufHelper(std::span<const u8> data) {
        write(data);
    }

    void write(const void* data, u64 size) {
        if (offset + size >= buf.size()) {
            buf.resize(offset + size);
        }
        std::memcpy(buf.data() + offset, data, size);
        offset += size;
    }

    void write(std::span<const u8> data) {
        write(data.data(), data.size());
    }

    void seek(u64 where_to) {
        offset = where_to;
    }

    [[nodiscard]]
    auto tell() const {
        return offset;
    }

    std::vector<u8> buf{};
    u64 offset{};
};

} // namespace

Result Yati::InstallCnmtNca(std::span<TikCollection> tickets, CnmtCollection& cnmt, const container::Collections& collections) {
    R_TRY(InstallNca(tickets, cnmt));

    fs::FsPath path;
    if (cnmt.skipped) {
        R_TRY(ncmContentStorageGetPath(std::addressof(cs), path, sizeof(path), std::addressof(cnmt.content_id)));
    } else {
        R_TRY(ncmContentStorageFlushPlaceHolder(std::addressof(cs)));
        R_TRY(ncmContentStorageGetPlaceHolderPath(std::addressof(cs), path, sizeof(path), std::addressof(cnmt.placeholder_id)));
    }

    ncm::PackagedContentMeta header;
    std::vector<NcmPackagedContentInfo> infos;
    R_TRY(nca::ParseCnmt(path, cnmt.header.program_id, header, cnmt.extended_header, infos));

    for (const auto& packed_info : infos) {
        const auto& info = packed_info.info;
        if (info.content_type == NcmContentType_DeltaFragment) {
            continue;
        }

        const auto str = ::sphaira::utils::hexIdToStr(info.content_id);
        const auto it = std::ranges::find_if(collections, [&str](const auto& e){
            return FindIC(e.name, str.str);
        });

        R_UNLESS(it != collections.cend(), Result_YatiNcaNotFound);

        log_write("found: %s\n", str.str);
        cnmt.infos.emplace_back(packed_info);
        auto& nca = cnmt.ncas.emplace_back(*it);
        nca.type = info.content_type;
    }

    // update header
    cnmt.meta_header = header.meta_header;
    cnmt.meta_header.content_count = cnmt.infos.size() + 1;
    cnmt.meta_header.storage_id = 0;

    cnmt.key.id = header.title_id;
    cnmt.key.version = header.title_version;
    cnmt.key.type = header.meta_type;
    cnmt.key.install_type = NcmContentInstallType_Full;
    std::memset(cnmt.key.padding, 0, sizeof(cnmt.key.padding));

    cnmt.content_info.content_id = cnmt.content_id;
    cnmt.content_info.content_type = NcmContentType_Meta;
    cnmt.content_info.attr = 0;
    ncmU64ToContentInfoSize(cnmt.size, &cnmt.content_info);
    cnmt.content_info.id_offset = 0;

    u32 orig_req_sys_ver = 0;
    if (cnmt.key.type == NcmContentMetaType_Application && cnmt.extended_header.size() >= sizeof(NcmApplicationMetaExtendedHeader)) {
        NcmApplicationMetaExtendedHeader ext{};
        std::memcpy(&ext, cnmt.extended_header.data(), sizeof(ext));
        orig_req_sys_ver = ext.required_system_version;
    } else if (cnmt.key.type == NcmContentMetaType_Patch && cnmt.extended_header.size() >= sizeof(NcmPatchMetaExtendedHeader)) {
        NcmPatchMetaExtendedHeader ext{};
        std::memcpy(&ext, cnmt.extended_header.data(), sizeof(ext));
        orig_req_sys_ver = ext.required_system_version;
    }
    cnmt.original_required_system_version = orig_req_sys_ver;

    if (config.lower_system_version) {
        auto extended_header = (ncm::ExtendedHeader*)cnmt.extended_header.data();
        log_write("patching version\n");
        if (cnmt.key.type == NcmContentMetaType_Application) {
            extended_header->application.required_system_version = 0;
        } else if (cnmt.key.type == NcmContentMetaType_Patch) {
            extended_header->patch.required_system_version = 0;
        }
    }

    // sort ncas
    const auto sorter = [](NcaCollection& lhs, NcaCollection& rhs) -> bool {
        return lhs.type > rhs.type;
    };

    std::ranges::sort(cnmt.ncas, sorter);

    log_write("found all cnmts\n");
    R_SUCCEED();
}

Result Yati::ParseTicketsIntoCollection(std::vector<TikCollection>& tickets, const container::Collections& collections, bool read_data) {
    for (const auto& collection : collections) {
        if (path::EndsWithIC(collection.name, ".tik")) {
            TikCollection entry{};
            keys::parse_hex_key(entry.rights_id.c, collection.name.c_str());
            const auto str = collection.name.substr(0, collection.name.length() - 4) + ".cert";

            const auto cert = std::ranges::find_if(collections, [&str](auto& e){
                return FindIC(e.name, str);
            });

            R_UNLESS(cert != collections.cend(), Result_YatiCertNotFound);
            entry.ticket.resize(collection.size);
            entry.cert.resize(cert->size);

            // only supported on non-stream installs.
            if (read_data) {
                u64 bytes_read;
                R_TRY(source->Read(entry.ticket.data(), collection.offset, entry.ticket.size(), &bytes_read));
                R_TRY(source->Read(entry.cert.data(), cert->offset, entry.cert.size(), &bytes_read));
            }

            tickets.emplace_back(entry);
        }
    }

    R_SUCCEED();
}

Result Yati::GetLatestVersion(const CnmtCollection& cnmt, u32& version_out, bool& skip) {
    const auto app_id = ncm::GetAppId(cnmt.key);
    version_out = cnmt.key.version;

    for (auto& db : ncm_db) {
        s32 db_list_total;
        s32 db_list_count;
        std::vector<NcmContentMetaKey> keys(1);
        if (R_SUCCEEDED(ncmContentMetaDatabaseList(std::addressof(db), std::addressof(db_list_total), std::addressof(db_list_count), keys.data(), keys.size(), NcmContentMetaType_Unknown, app_id, 0, UINT64_MAX, NcmContentInstallType_Full))) {
            if (db_list_total != keys.size()) {
                keys.resize(db_list_total);
                if (keys.size()) {
                    R_TRY(ncmContentMetaDatabaseList(std::addressof(db), std::addressof(db_list_total), std::addressof(db_list_count), keys.data(), keys.size(), NcmContentMetaType_Unknown, app_id, 0, UINT64_MAX, NcmContentInstallType_Full));
                }
            }

            for (auto& key : keys) {
                log_write("found record: %016lX type: %u version: %u\n", key.id, key.type, key.version);

                if (key.id == cnmt.key.id && cnmt.key.version == key.version) {
                    if (config.skip_if_already_installed == 1) {
                        log_write("skipping as already installed\n");
                        skip = true;
                    } else if (config.skip_if_already_installed == 2) {
                        log_write("prompting for already installed title %016lX\n", key.id);
                        if (!pbox->PromptReinstall(cnmt.name)) {
                            log_write("user chose to skip already installed title\n");
                            skip = true;
                        } else {
                            log_write("user chose to reinstall already installed title\n");
                            skip = false;
                        }
                    }
                }

                // check if we are downgrading
                if (cnmt.key.type == NcmContentMetaType_Patch) {
                    if (cnmt.key.type == key.type && cnmt.key.version < key.version && !config.allow_downgrade) {
                        log_write("skipping due to it being lower\n");
                        skip = true;
                    }
                } else {
                    version_out = std::max(version_out, key.version);
                }
            }
        }
    }

    R_SUCCEED();
}

Result Yati::ShouldSkip(const CnmtCollection& cnmt, bool& skip) {
    if (!skip && config.skip_if_already_installed) {
        bool has;
        R_TRY(ncmContentMetaDatabaseHas(std::addressof(db), std::addressof(has), std::addressof(cnmt.key)));
        if (has) {
            if (config.skip_if_already_installed == 1) {
                log_write("\tskipping: [ncmContentMetaDatabaseHas()]\n");
                skip = true;
            } else if (config.skip_if_already_installed == 2) {
                log_write("prompting for already installed title %016lX\n", cnmt.key.id);
                if (!pbox->PromptReinstall(cnmt.name)) {
                    log_write("user chose to skip already installed title\n");
                    skip = true;
                } else {
                    log_write("user chose to reinstall already installed title\n");
                    skip = false;
                }
            }
        }
    }

    // skip invalid types
    if (!skip) {
        if (!(cnmt.key.type & 0x80)) {
            log_write("\tskipping: invalid: %u\n", cnmt.key.type);
            skip = true;
        } else if (!config.title_ids.empty() && std::ranges::find(config.title_ids, cnmt.key.id) == config.title_ids.end()) {
            log_write("\tskipping: not selected: %016lX\n", cnmt.key.id);
            skip = true;
        } else if (config.skip_base && cnmt.key.type == NcmContentMetaType_Application) {
            log_write("\tskipping: [NcmContentMetaType_Application]\n");
            skip = true;
        } else if (config.skip_patch && cnmt.key.type == NcmContentMetaType_Patch) {
            log_write("\tskipping: [NcmContentMetaType_Application]\n");
            skip = true;
        } else if (config.skip_addon && cnmt.key.type == NcmContentMetaType_AddOnContent) {
            log_write("\tskipping: [NcmContentMetaType_AddOnContent]\n");
            skip = true;
        } else if (config.skip_data_patch && cnmt.key.type == NcmContentMetaType_DataPatch) {
            log_write("\tskipping: [NcmContentMetaType_DataPatch]\n");
            skip = true;
        }
    }

    R_SUCCEED();
}

Result Yati::ImportTickets(std::span<TikCollection> tickets) {
    for (auto& ticket : tickets) {
        if (ticket.required || config.ticket_only) {
            if (config.skip_ticket) {
                log_write("WARNING: skipping ticket install, but it's required!\n");
            } else {
                if (!ticket.patched) {
                    log_write("patching ticket\n");
                    R_TRY(es::PatchTicket(ticket.ticket, ticket.cert, ticket.key_gen, keys, config.convert_to_common_ticket));
                    ticket.patched = true;
                }

                log_write("installing ticket\n");
                R_TRY(es::ImportTicket(ticket.ticket.data(), ticket.ticket.size(), ticket.cert.data(), ticket.cert.size()));
                ticket.required = false;
            }
        }
    }

    R_SUCCEED();
}

Result Yati::RemoveInstalledNcas(const CnmtCollection& cnmt) {
    const auto app_id = ncm::GetAppId(cnmt.key);

    // remove current entries (if any).
    s32 db_list_total;
    s32 db_list_count;
    u64 id_min = cnmt.key.id;
    u64 id_max = cnmt.key.id;

    // if installing a patch, remove all previously installed patches.
    if (cnmt.key.type == NcmContentMetaType_Patch) {
        id_min = 0;
        id_max = UINT64_MAX;
    }

    log_write("listing keys\n");
    for (size_t i = 0; i < std::size(NCM_STORAGE_IDS); i++) {
        auto& cs = ncm_cs[i];
        auto& db = ncm_db[i];

        std::vector<NcmContentMetaKey> keys(1);
        R_TRY(ncmContentMetaDatabaseList(std::addressof(db), std::addressof(db_list_total), std::addressof(db_list_count), keys.data(), keys.size(), static_cast<NcmContentMetaType>(cnmt.key.type), app_id, id_min, id_max, NcmContentInstallType_Full));

        if (db_list_total != keys.size()) {
            keys.resize(db_list_total);
            if (keys.size()) {
                R_TRY(ncmContentMetaDatabaseList(std::addressof(db), std::addressof(db_list_total), std::addressof(db_list_count), keys.data(), keys.size(), static_cast<NcmContentMetaType>(cnmt.key.type), app_id, id_min, id_max, NcmContentInstallType_Full));
            }
        }

        for (const auto& key : keys) {
            log_write("found key: 0x%016lX type: %u version: %u\n", key.id, key.type, key.version);
            NcmContentMetaHeader header;
            u64 out_size;
            log_write("trying to get from db\n");
            R_TRY(ncmContentMetaDatabaseGet(std::addressof(db), std::addressof(key), std::addressof(out_size), std::addressof(header), sizeof(header)));
            R_UNLESS(out_size == sizeof(header), Result_YatiNcmDbCorruptHeader);
            log_write("trying to list infos\n");

            std::vector<NcmContentInfo> infos(header.content_count);
            s32 content_info_out;
            R_TRY(ncmContentMetaDatabaseListContentInfo(std::addressof(db), std::addressof(content_info_out), infos.data(), infos.size(), std::addressof(key), 0));
            R_UNLESS(content_info_out == infos.size(), Result_YatiNcmDbCorruptInfos);
            log_write("size matches\n");

            for (const auto& info : infos) {
                const auto it = std::ranges::find_if(cnmt.ncas, [&info](auto& e){
                    return !std::memcmp(&e.content_id, &info.content_id, sizeof(e.content_id));
                });

                // don't delete the nca if we skipped the install.
                if ((it != cnmt.ncas.cend() && it->skipped) || (!std::memcmp(&cnmt.content_id, &info.content_id, sizeof(cnmt.content_id)) && cnmt.skipped)) {
                    continue;
                }

                R_TRY(ncm::Delete(std::addressof(cs), std::addressof(info.content_id)));
            }

            log_write("trying to remove it\n");
            R_TRY(ncmContentMetaDatabaseRemove(std::addressof(db), std::addressof(key)));
            R_TRY(ncmContentMetaDatabaseCommit(std::addressof(db)));
            log_write("all done with this key\n\n");
        }
    }

    log_write("done with keys\n");
    R_SUCCEED();
}

Result Yati::RegisterNcasAndPushRecord(const CnmtCollection& cnmt, u32 latest_version_num) {
    const auto app_id = ncm::GetAppId(cnmt.key);

    // register all nca's
    if (!cnmt.skipped) {
        log_write("registering cnmt nca\n");
        R_TRY(ncm::Register(std::addressof(cs), std::addressof(cnmt.content_id), std::addressof(cnmt.placeholder_id)));
        log_write("registered cnmt nca\n");
    }

    for (auto& nca : cnmt.ncas) {
        if (!nca.skipped && nca.type != NcmContentType_DeltaFragment) {
            log_write("registering nca: %s\n", nca.name.c_str());
            R_TRY(ncm::Register(std::addressof(cs), std::addressof(nca.content_id), std::addressof(nca.placeholder_id)));
            log_write("registered nca: %s\n", nca.name.c_str());
        }
    }

    log_write("register'd all ncas\n");

    // build ncm meta and push to the database.
    BufHelper buf{};
    buf.write(std::addressof(cnmt.meta_header), sizeof(cnmt.meta_header));
    buf.write(cnmt.extended_header.data(), cnmt.extended_header.size());
    buf.write(std::addressof(cnmt.content_info), sizeof(cnmt.content_info));

    for (auto& info : cnmt.infos) {
        buf.write(std::addressof(info.info), sizeof(info.info));
    }

    pbox->SetInstallTransfer("Updating ncm database"_i18n);
    R_TRY(ncmContentMetaDatabaseSet(std::addressof(db), std::addressof(cnmt.key), buf.buf.data(), buf.tell()));
    R_TRY(ncmContentMetaDatabaseCommit(std::addressof(db)));

    // push record.
    ncm::ContentStorageRecord content_storage_record{};
    content_storage_record.key = cnmt.key;
    content_storage_record.storage_id = storage_id;
    pbox->SetInstallTransfer("Pushing application record"_i18n);

    R_TRY(ns::PushApplicationRecord(std::addressof(ns_app), app_id, std::addressof(content_storage_record), 1));
    if (hosversionAtLeast(6,0,0)) {
        R_TRY(avmInitialize());
        ON_SCOPE_EXIT(avmExit());

        R_TRY(avmPushLaunchVersion(app_id, latest_version_num));
    }
    log_write("pushed\n");
    if (cnmt.key.type == NcmContentMetaType_Patch || cnmt.key.type == NcmContentMetaType_AddOnContent) {
        orphan_content::NoteInstalled(app_id);
    }
    if (pbox) {
        pbox->OnTitleInstalled(app_id);
    }

    if (pbox && (cnmt.key.type == NcmContentMetaType_Application || cnmt.key.type == NcmContentMetaType_Patch)) {
        if (cnmt.original_required_system_version != 0) {
            const auto installed_fw = hats::getSystemFirmware();
            if (version::IsFirmwareLower(installed_fw, cnmt.original_required_system_version)) {
                ui::CompatibilityWarning warning{};
                warning.title_id = app_id;
                warning.required_hos = version::FormatPacked(cnmt.original_required_system_version);
                warning.installed_hos = installed_fw;
                if (cnmt.header.sdk_version != 0) {
                    warning.title_sdk = version::FormatSdkVersion(cnmt.header.sdk_version);
                }
                pbox->OnCompatibilityWarning(warning);
            }
        }
    }

    R_SUCCEED();
}

} // namespace sphaira::yati::detail
