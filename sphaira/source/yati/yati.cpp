#include "yati/yati.hpp"
#include "yati_internal.hpp"
#include "path_util.hpp"
#include "yati/source/file.hpp"
#include "yati/container/nsp.hpp"
#include "yati/container/xci.hpp"

#include "yati/nx/ncz.hpp"
#include "yati/nx/nca.hpp"
#include "yati/nx/ncm.hpp"
#include "yati/nx/ns.hpp"
#include "yati/nx/es.hpp"
#include "yati/nx/keys.hpp"
#include "yati/nx/crypto.hpp"

#include "ui/progress_box.hpp"
#include "ui/menus/install_plan.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "utils/utils.hpp"
#include <sys/statvfs.h>

#include <minIni.h>
#include <algorithm>
#include <atomic>

namespace sphaira::yati {
namespace detail {

Yati::Yati(ui::InstallProgress* _pbox, source::Base* _source) : pbox{_pbox}, source{_source} {
    App::SetAutoSleepDisabled(true);
}

Yati::~Yati() {
    splCryptoExit();
    serviceClose(std::addressof(ns_app));
    nsExit();
    es::Exit();

    for (size_t i = 0; i < std::size(NCM_STORAGE_IDS); i++) {
        ncmContentMetaDatabaseClose(std::addressof(ncm_db[i]));
        ncmContentStorageClose(std::addressof(ncm_cs[i]));
    }

    App::SetAutoSleepDisabled(false);
}

Result Yati::Setup(const ConfigOverride& override) {
    config.sd_card_install = override.sd_card_install.value_or(App::GetInstallSdEnable());
    config.allow_downgrade = App::GetApp()->m_allow_downgrade.Get();
    config.skip_if_already_installed = override.skip_if_already_installed.value_or(App::GetApp()->m_skip_if_already_installed.Get());
    config.ticket_only = App::GetApp()->m_ticket_only.Get();
    config.skip_base = App::GetApp()->m_skip_base.Get();
    config.skip_patch = App::GetApp()->m_skip_patch.Get();
    config.skip_addon = App::GetApp()->m_skip_addon.Get();
    config.skip_data_patch = App::GetApp()->m_skip_data_patch.Get();
    config.skip_ticket = App::GetApp()->m_skip_ticket.Get();
    config.skip_nca_hash_verify = override.skip_nca_hash_verify.value_or(App::GetApp()->m_skip_nca_hash_verify.Get());
    config.skip_rsa_header_fixed_key_verify = override.skip_rsa_header_fixed_key_verify.value_or(App::GetApp()->m_skip_rsa_header_fixed_key_verify.Get());
    config.skip_rsa_npdm_fixed_key_verify = override.skip_rsa_npdm_fixed_key_verify.value_or(App::GetApp()->m_skip_rsa_npdm_fixed_key_verify.Get());
    config.ignore_distribution_bit = override.ignore_distribution_bit.value_or(App::GetApp()->m_ignore_distribution_bit.Get());
    config.convert_to_common_ticket = override.convert_to_common_ticket.value_or(App::GetApp()->m_convert_to_common_ticket.Get());
    config.convert_to_standard_crypto = override.convert_to_standard_crypto.value_or(App::GetApp()->m_convert_to_standard_crypto.Get());
    config.lower_master_key = override.lower_master_key.value_or(App::GetApp()->m_lower_master_key.Get());
    config.lower_system_version = override.lower_system_version.value_or(App::GetApp()->m_lower_system_version.Get());
    storage_id = config.sd_card_install ? NcmStorageId_SdCard : NcmStorageId_BuiltInUser;
    if (pbox) {
        pbox->SetInstallTarget(config.sd_card_install);
    }

    R_TRY(source->GetOpenResult());
    R_TRY(splCryptoInitialize());
    R_TRY(nsInitialize());
    R_TRY(nsGetApplicationManagerInterface(std::addressof(ns_app)));
    R_TRY(es::Initialize());

    for (size_t i = 0; i < std::size(NCM_STORAGE_IDS); i++) {
        R_TRY(ncmOpenContentMetaDatabase(std::addressof(ncm_db[i]), NCM_STORAGE_IDS[i]));
        R_TRY(ncmOpenContentStorage(std::addressof(ncm_cs[i]), NCM_STORAGE_IDS[i]));
    }

    cs = ncm_cs[config.sd_card_install];
    db = ncm_db[config.sd_card_install];

    R_TRY(parse_keys(keys, true));
    R_SUCCEED();
}

Result Yati::InstallNcaInternal(std::span<TikCollection> tickets, NcaCollection& nca) {
    if (config.skip_if_already_installed == 1 || config.ticket_only) {
        R_TRY(ncmContentStorageHas(std::addressof(cs), std::addressof(nca.skipped), std::addressof(nca.content_id)));
        if (nca.skipped) {
            log_write("\tskipped nca as it's already installed ncmContentStorageHas()\n");
            R_TRY(ncmContentStorageReadContentIdFile(std::addressof(cs), std::addressof(nca.header), sizeof(nca.header), std::addressof(nca.content_id), 0));
            crypto::cryptoAes128Xts(std::addressof(nca.header), std::addressof(nca.header), keys.header_key, 0, 0x200, sizeof(nca.header), false);

            R_TRY(HasRequiredTicket(nca.header, tickets));
            R_SUCCEED();
        }
    }

    log_write("generateing placeholder\n");
    R_TRY(ncmContentStorageGeneratePlaceHolderId(std::addressof(cs), std::addressof(nca.placeholder_id)));

    // this preallocates nca.size bytes and runs *before* the pipeline threads
    // exist, so nothing is draining the stream buffer while it works. on a
    // multi-gb nca that window is long enough for a streaming source (mtp) to
    // back up and have its host time the transfer out -- time it so the log
    // says how much slack the ingest buffer actually needs.
    log_write("creating placeholder\n");
    const auto placeholder_start = armTicksToNs(armGetSystemTick());
    R_TRY(ncmContentStorageCreatePlaceHolder(std::addressof(cs), std::addressof(nca.content_id), std::addressof(nca.placeholder_id), nca.size));
    const auto placeholder_ns = armTicksToNs(armGetSystemTick()) - placeholder_start;
    log_write("created placeholder for %lld bytes in %llu ms\n", (long long)nca.size, placeholder_ns / 1000000ULL);

    log_write("opening thread\n");
    ThreadData t_data{this, tickets, std::addressof(nca)};

    #define READ_THREAD_CORE 1
    #define DECOMPRESS_THREAD_CORE 2
    #define WRITE_THREAD_CORE 0
    // #define READ_THREAD_CORE 2
    // #define DECOMPRESS_THREAD_CORE 2
    // #define WRITE_THREAD_CORE 2

    Thread t_read{};
    R_TRY(threadCreate(&t_read, readFunc, std::addressof(t_data), nullptr, 1024*64, PRIO_PREEMPTIVE, READ_THREAD_CORE));
    ON_SCOPE_EXIT(threadClose(&t_read));

    Thread t_decompress{};
    R_TRY(threadCreate(&t_decompress, decompressFunc, std::addressof(t_data), nullptr, 1024*64, PRIO_PREEMPTIVE, DECOMPRESS_THREAD_CORE));
    ON_SCOPE_EXIT(threadClose(&t_decompress));

    Thread t_write{};
    R_TRY(threadCreate(&t_write, writeFunc, std::addressof(t_data), nullptr, 1024*64, PRIO_PREEMPTIVE, WRITE_THREAD_CORE));
    ON_SCOPE_EXIT(threadClose(&t_write));

    log_write("starting threads\n");
    R_TRY(threadStart(std::addressof(t_read)));
    ON_SCOPE_EXIT(threadWaitForExit(std::addressof(t_read)));

    R_TRY(threadStart(std::addressof(t_decompress)));
    ON_SCOPE_EXIT(threadWaitForExit(std::addressof(t_decompress)));

    R_TRY(threadStart(std::addressof(t_write)));
    ON_SCOPE_EXIT(threadWaitForExit(std::addressof(t_write)));

    const auto waiter_progress = waiterForUEvent(t_data.GetProgressEvent());
    const auto waiter_cancel = waiterForUEvent(pbox->GetInstallCancelEvent());
    const auto waiter_done = waiterForUEvent(t_data.GetDoneEvent());

    for (;;) {
        s32 idx;
        if (R_FAILED(waitMulti(&idx, UINT64_MAX, waiter_progress, waiter_cancel, waiter_done))) {
            break;
        }

        if (!idx) {
            pbox->UpdateInstallTransfer(t_data.GetWriteOffset(), t_data.GetWriteSize());
            pbox->UpdateInstallReadWrite(t_data.GetReadOffset(), t_data.GetWriteOffset());
        } else {
            if (idx == 1) {
                source->SignalCancel();
            }
            break;
        }
    }

    // wait for all threads to close.
    log_write("waiting for threads to close\n");
    while (t_data.IsAnyRunning()) {
        t_data.WakeAllThreads();
        pbox->InstallYield();

        if (R_FAILED(waitSingleHandle(t_read.handle, 1000))) {
            continue;
        } else if (R_FAILED(waitSingleHandle(t_decompress.handle, 1000))) {
            continue;
        } else if (R_FAILED(waitSingleHandle(t_write.handle, 1000))) {
            continue;
        }
        break;
    }
    log_write("threads closed\n");

    // if any of the threads failed, wake up all threads so they can exit.
    if (R_FAILED(t_data.GetResults())) {
        log_write("some reads failed, waking threads: %s\n", nca.name.c_str());
        log_write("returning due to fail: %s\n", nca.name.c_str());
        return t_data.GetResults();
    }
    R_TRY(t_data.GetResults());

    NcmContentId content_id{};
    std::memcpy(std::addressof(content_id), nca.hash, sizeof(content_id));

    log_write("old id: %s new id: %s\n", ::sphaira::utils::hexIdToStr(nca.content_id).str, ::sphaira::utils::hexIdToStr(content_id).str);
    if (!config.skip_nca_hash_verify && !nca.modified) {
        if (std::memcmp(&nca.content_id, nca.hash, sizeof(nca.content_id))) {
            log_write("nca hash is invalid!!!!\n");
            R_UNLESS(!std::memcmp(&nca.content_id, nca.hash, sizeof(nca.content_id)), Result_YatiInvalidNcaSha256);
        } else {
            log_write("nca hash is valid!\n");
        }
    } else {
        log_write("skipping nca sha256 verify\n");
    }

    R_SUCCEED();
}

Result Yati::InstallNca(std::span<TikCollection> tickets, NcaCollection& nca) {
    log_write("in install nca\n");
    pbox->SetInstallTransfer(nca.name);
    keys::parse_hex_key(std::addressof(nca.content_id), nca.name.c_str());

    R_TRY(InstallNcaInternal(tickets, nca));

    fs::FsPath path;
    if (nca.skipped) {
        R_TRY(ncmContentStorageGetPath(std::addressof(cs), path, sizeof(path), std::addressof(nca.content_id)));
    } else {
        R_TRY(ncmContentStorageFlushPlaceHolder(std::addressof(cs)));
        R_TRY(ncmContentStorageGetPlaceHolderPath(std::addressof(cs), path, sizeof(path), std::addressof(nca.placeholder_id)));
    }

    if (nca.header.content_type == nca::ContentType_Program) {
        // todo: verify npdm key.
    } else if (nca.header.content_type == nca::ContentType_Control) {
        NacpLanguageEntry entry;
        std::vector<u8> icon;
        // this may fail if tickets aren't installed and the nca uses title key crypto.
        if (R_SUCCEEDED(nca::ParseControl(path, nca.header.program_id, &entry, sizeof(entry), &icon))) {
            pbox->SetInstallTitle(entry.name);
            pbox->SetInstallImage(icon);
        }
    }

    R_SUCCEED();
}

Result InstallInternal(ui::InstallProgress* pbox, source::Base* source, const container::Collections& collections, const ConfigOverride& override) {
    auto yati = std::make_unique<Yati>(pbox, source);
    R_TRY(yati->Setup(override));

    std::vector<TikCollection> tickets{};
    R_TRY(yati->ParseTicketsIntoCollection(tickets, collections, true));

    std::vector<CnmtCollection> cnmts{};
    for (const auto& collection : collections) {
        log_write("found collection: %s\n", collection.name.c_str());
        if (path::EndsWithIC(collection.name, ".cnmt.nca") || path::EndsWithIC(collection.name, ".cnmt.ncz")) {
            auto& cnmt = cnmts.emplace_back(NcaCollection{collection});
            cnmt.type = NcmContentType_Meta;
        }
    }

    for (auto& cnmt : cnmts) {
        ON_SCOPE_EXIT(
            ncmContentStorageDeletePlaceHolder(std::addressof(yati->cs), std::addressof(cnmt.placeholder_id));
            for (auto& nca : cnmt.ncas) {
                ncmContentStorageDeletePlaceHolder(std::addressof(yati->cs), std::addressof(nca.placeholder_id));
            }
        );

        R_TRY(yati->InstallCnmtNca(tickets, cnmt, collections));

        u32 latest_version_num;
        bool skip = false;
        R_TRY(yati->GetLatestVersion(cnmt, latest_version_num, skip));
        R_TRY(yati->ShouldSkip(cnmt, skip));

        if (skip) {
            log_write("skipping install!\n");
            pbox->OnInstallSkipped();
            continue;
        }

        log_write("installing nca's\n");
        for (auto& nca : cnmt.ncas) {
            R_TRY(yati->InstallNca(tickets, nca));
        }

        R_TRY(yati->ImportTickets(tickets));
        R_TRY(yati->RemoveInstalledNcas(cnmt));
        R_TRY(yati->RegisterNcasAndPushRecord(cnmt, latest_version_num));
    }

    log_write("success!\n");
    R_SUCCEED();
}

Result InstallInternalStream(ui::InstallProgress* pbox, source::Base* source, container::Collections collections, const ConfigOverride& override) {
    auto yati = std::make_unique<Yati>(pbox, source);
    R_TRY(yati->Setup(override));

    // not supported with stream installs (yet).
    yati->config.skip_if_already_installed = false;
    yati->config.convert_to_standard_crypto = false;
    yati->config.lower_master_key = false;

    std::vector<NcaCollection> ncas{};
    std::vector<CnmtCollection> cnmts{};
    std::vector<TikCollection> tickets{};

    ON_SCOPE_EXIT(
        for (const auto& cnmt : cnmts) {
            ncmContentStorageDeletePlaceHolder(std::addressof(yati->cs), std::addressof(cnmt.placeholder_id));
        }

        for (const auto& nca : ncas) {
            ncmContentStorageDeletePlaceHolder(std::addressof(yati->cs), std::addressof(nca.placeholder_id));
        }
    );

    // fill ticket entries, the data will be filled later on.
    R_TRY(yati->ParseTicketsIntoCollection(tickets, collections, false));

    // sort based on lowest offset.
    const auto sorter = [](const container::CollectionEntry& lhs, const container::CollectionEntry& rhs) -> bool {
        return lhs.offset < rhs.offset;
    };

    std::ranges::sort(collections, sorter);

    for (const auto& collection : collections) {
        if (path::EndsWithIC(collection.name, ".nca") || path::EndsWithIC(collection.name, ".ncz")) {
            auto& nca = ncas.emplace_back(NcaCollection{collection});
            if (path::EndsWithIC(collection.name, ".cnmt.nca") || path::EndsWithIC(collection.name, ".cnmt.ncz")) {
                auto& cnmt = cnmts.emplace_back(nca);
                cnmt.type = NcmContentType_Meta;
                R_TRY(yati->InstallCnmtNca(tickets, cnmt, collections));
            } else {
                R_TRY(yati->InstallNca(tickets, nca));
            }
        } else if (path::EndsWithIC(collection.name, ".tik") || path::EndsWithIC(collection.name, ".cert")) {
            FsRightsId rights_id{};
            keys::parse_hex_key(rights_id.c, collection.name.c_str());
            const auto str = collection.name.substr(0, collection.name.length() - 4) + ".cert";

            auto entry = std::ranges::find_if(tickets, [&rights_id](auto& e){
                return !std::memcmp(&rights_id, &e.rights_id, sizeof(rights_id));
            });

            // this will never fail...but just in case.
            R_UNLESS(entry != tickets.end(), Result_YatiCertNotFound);

            u64 bytes_read;
            if (path::EndsWithIC(collection.name, ".tik")) {
                R_TRY(source->Read(entry->ticket.data(), collection.offset, entry->ticket.size(), &bytes_read));
            } else {
                R_TRY(source->Read(entry->cert.data(), collection.offset, entry->cert.size(), &bytes_read));
            }
        }
    }

    for (auto& cnmt : cnmts) {
        // copy nca structs into cnmt.
        for (auto& cnmt_nca : cnmt.ncas) {
            auto it = std::ranges::find_if(ncas, [&cnmt_nca](auto& e){
                return e.name == cnmt_nca.name;
            });

            R_UNLESS(it != ncas.cend(), Result_YatiNczSectionNotFound);
            const auto type = cnmt_nca.type;
            cnmt_nca = *it;
            cnmt_nca.type = type;
        }

        u32 latest_version_num;
        bool skip = false;
        R_TRY(yati->GetLatestVersion(cnmt, latest_version_num, skip));
        R_TRY(yati->ShouldSkip(cnmt, skip));

        if (skip) {
            log_write("skipping install!\n");
            pbox->OnInstallSkipped();
            continue;
        }

        R_TRY(yati->ImportTickets(tickets));
        R_TRY(yati->RemoveInstalledNcas(cnmt));
        R_TRY(yati->RegisterNcasAndPushRecord(cnmt, latest_version_num));
    }

    log_write("success!\n");
    R_SUCCEED();
}

} // namespace detail

Result InstallFromFile(ui::InstallProgress* pbox, fs::Fs* fs, const fs::FsPath& path, const ConfigOverride& override) {
    log_write("[YATI] InstallFromFile start for %s\n", path.s);
    auto source = std::make_unique<source::File>(fs, path);
    log_write("[YATI] source::File opened, open result: 0x%X\n", R_VALUE(source->GetOpenResult()));
    const auto rc = InstallFromSource(pbox, source.get(), path, override);
    log_write("[YATI] InstallFromFile finished, result: 0x%X\n", R_VALUE(rc));
    return rc;
}

Result InstallFromSource(ui::InstallProgress* pbox, source::Base* source, const fs::FsPath& path, const ConfigOverride& override) {
    const auto ext = std::strrchr(path.s, '.');
    R_UNLESS(ext, Result_YatiContainerNotFound);

    std::unique_ptr<container::Base> container;
    if (!strcasecmp(ext, ".nsp") || !strcasecmp(ext, ".nsz")) {
        container = std::make_unique<container::Nsp>(source);
    } else if (!strcasecmp(ext, ".xci") || !strcasecmp(ext, ".xcz")) {
        container = std::make_unique<container::Xci>(source);
    }

    R_UNLESS(container, Result_YatiContainerNotFound);
    log_write("[YATI] InstallFromSource: container initialized, calling InstallFromContainer\n");
    const auto rc = InstallFromContainer(pbox, container.get(), override);
    log_write("[YATI] InstallFromSource: InstallFromContainer returned 0x%X\n", R_VALUE(rc));
    return rc;
}

Result InstallFromContainer(ui::InstallProgress* pbox, container::Base* container, const ConfigOverride& override) {
    container::Collections collections;
    log_write("[YATI] InstallFromContainer: calling GetCollections\n");
    const auto rc = container->GetCollections(collections);
    log_write("[YATI] InstallFromContainer: GetCollections returned 0x%X, total entries: %zu\n", R_VALUE(rc), collections.size());
    R_TRY(rc);
    return InstallFromCollections(pbox, container->GetSource(), collections, override);
}

Result InstallFromCollections(ui::InstallProgress* pbox, source::Base* source, const container::Collections& collections, const ConfigOverride& override) {
    ConfigOverride dynamic_override = override;
    if (!dynamic_override.sd_card_install.has_value()) {
        s64 total_size = 0;
        bool is_compressed = false;
        for (const auto& entry : collections) {
            total_size += entry.size;
            if (path::EndsWithIC(entry.name, ".ncz")) {
                is_compressed = true;
            }
        }
        dynamic_override.sd_card_install = ChooseInstallTarget(total_size, is_compressed);
    }

    if (source->IsStream()) {
        return detail::InstallInternalStream(pbox, source, collections, dynamic_override);
    } else {
        return detail::InstallInternal(pbox, source, collections, dynamic_override);
    }
}

} // namespace sphaira::yati
