// creates and installs nca's on the fly
// based on hacbrewpack (romfs creation) and yati (installation)
#include <switch.h>
#include <cstring>
#include <vector>
#include <string>
#include <string_view>
#include <span>

#include "yati/nx/nca.hpp"
#include "yati/nx/ncm.hpp"
#include "yati/nx/ns.hpp"
#include "yati/nx/es.hpp"
#include "yati/nx/keys.hpp"
#include "yati/nx/crypto.hpp"

#include "owo_internal.hpp"
#include "defines.hpp"
#include "app.hpp"
#include "nacp_util.hpp"
#include "ui/progress_box.hpp"
#include "i18n.hpp"
#include "log.hpp"

namespace sphaira {
namespace {

constexpr const u8 HBL_MAIN_DATA[]{
    #embed <exefs/main>
};

constexpr const u8 HBL_NPDM_DATA[]{
    #embed <exefs/main.npdm>
};

auto install_forwader_internal(ui::ProgressBox* pbox, OwoConfig& config, NcmStorageId storage_id) -> Result {
    if (pbox) {
        pbox->SetTitle(config.name);
        pbox->SetImageDataConst(config.icon);
    }

    R_UNLESS(!config.nro_path.empty(), Result_OwoBadArgs);
    R_UNLESS(!config.icon.empty(), Result_OwoBadArgs);

    R_TRY(splCryptoInitialize());
    ON_SCOPE_EXIT(splCryptoExit());

    R_TRY(ncmInitialize());
    ON_SCOPE_EXIT(ncmExit());

    R_TRY(nsInitialize());
    ON_SCOPE_EXIT(nsExit());

    keys::Keys keys;
    R_TRY(keys::parse_keys(keys, false));

    // fix args to include nro path
    if (config.args.empty()) {
        config.args = config.nro_path;
    } else {
        config.args = config.nro_path + ' ' + config.args;
    }

    // create tid by using explicit id or a hash over path + args
    u64 hash_data[SHA256_HASH_SIZE / sizeof(u64)];
    const auto hash_path = config.nro_path + config.args;
    sha256CalculateHash(hash_data, hash_path.data(), hash_path.length());
    const u64 default_tid = 0x0500000000000000 | (hash_data[0] & 0x00FFFFFFFFFFF000);
    const u64 tid = config.title_id.value_or(default_tid);
    const u64 old_tid = 0x0100000000000000 | (tid & 0x00FFFFFFFFFFF000);

    std::vector<NcaEntry> nca_entries;

    // the editor sets these per-forwarder, everyone else takes the global defaults.
    const auto options = config.options.value_or(App::GetForwarderOptions());

    // create program
    if (config.program_nca.empty()) {
        if (pbox) {
            pbox->NewTransfer("Creating Program"_i18n).UpdateTransfer(0, 8);
        }
        FileEntries exefs;
        add_file_entry(exefs, "main", HBL_MAIN_DATA);
        add_file_entry(exefs, "main.npdm", HBL_NPDM_DATA);

        FileEntries romfs;
        add_file_entry(romfs, "/nextArgv", config.args.data(), config.args.length());
        add_file_entry(romfs, "/nextNroPath", config.nro_path.data(), config.nro_path.length());

        FileEntries logo;
        if (!config.logo.empty()) {
            add_file_entry(logo, "NintendoLogo.png", config.logo);
        }
        if (!config.gif.empty()) {
            add_file_entry(logo, "StartupMovie.gif", config.gif);
        }

        NpdmPatch npdm_patch;
        npdm_patch.tid = tid;
        npdm_patch.address_space = options.address_space;
        npdm_patch.svc_debug_mode = options.svc_debug_mode;
        npdm_patch.core_mode = options.core_mode;
        R_UNLESS(patch_npdm(exefs[1].data, npdm_patch), Result_OwoBadArgs);

        nca_entries.emplace_back(
            create_program_nca(tid, keys, exefs, romfs, logo)
        );
    } else {
        nca_entries.emplace_back(
            BufHelper{config.program_nca}, NcmContentType_Program
        );
    }

    // create control
    {
        if (pbox) {
            pbox->NewTransfer("Creating Control"_i18n).UpdateTransfer(1, 8);
        }
        // patch nacp
        NcapPatch nacp_patch{};
        nacp_patch.tid = tid;
        nacp_patch.name = config.name;
        nacp_patch.author = config.author;
        nacp_patch.profile_selection = options.profile_selection;
        nacp_patch.screenshot = options.screenshot;
        nacp_patch.video_capture = options.video_capture;
        patch_nacp(config.nacp, nacp_patch);

        FileEntries romfs;
        add_file_entry(romfs, "/control.nacp", &config.nacp, sizeof(config.nacp));
        add_file_entry(romfs, "/icon_AmericanEnglish.dat", config.icon);

        nca_entries.emplace_back(
            create_control_nca(tid, keys, romfs)
        );
    }

    // create meta
    NcmContentMetaHeader content_meta_header;
    NcmContentMetaKey content_meta_key;
    ncm::ContentStorageRecord content_storage_record;
    NcmContentMetaData content_meta_data;
    {
        if (pbox) {
            pbox->NewTransfer("Creating Meta"_i18n).UpdateTransfer(2, 8);
        }
        const auto meta_entry = create_meta_nca(tid, keys, storage_id, nca_entries);

        nca_entries.emplace_back(meta_entry.nca_entry);
        content_meta_header = meta_entry.content_meta_header;
        content_meta_key = meta_entry.content_meta_key;
        content_storage_record = meta_entry.content_storage_record;
        content_meta_data = meta_entry.content_meta_data;
    }

    // write ncas
    {
        NcmContentStorage cs;
        R_TRY(ncmOpenContentStorage(&cs, storage_id));
        ON_SCOPE_EXIT(ncmContentStorageClose(&cs));

        for (const auto& nca : nca_entries) {
            if (pbox) {
                pbox->NewTransfer("Writing Nca"_i18n).UpdateTransfer(3, 8);
            }
            NcmContentId content_id;
            NcmPlaceHolderId placeholder_id;
            std::memcpy(&content_id, nca.hash, sizeof(content_id));
            R_TRY(ncmContentStorageGeneratePlaceHolderId(&cs, &placeholder_id));
            ncmContentStorageDeletePlaceHolder(&cs, &placeholder_id);
            R_TRY(ncmContentStorageCreatePlaceHolder(&cs, &content_id, &placeholder_id, nca.data.size()));
            R_TRY(ncmContentStorageWritePlaceHolder(&cs, &placeholder_id, 0, nca.data.data(), nca.data.size()));
            ncmContentStorageDelete(&cs, &content_id);
            R_TRY(ncmContentStorageRegister(&cs, &content_id, &placeholder_id));
        }
    }

    // setup database
    {
        if (pbox) {
            pbox->NewTransfer("Updating ncm database"_i18n).UpdateTransfer(4, 8);
        }
        NcmContentMetaDatabase db;
        R_TRY(ncmOpenContentMetaDatabase(&db, storage_id));
        ON_SCOPE_EXIT(ncmContentMetaDatabaseClose(&db));

        R_TRY(ncmContentMetaDatabaseSet(&db, &content_meta_key, &content_meta_data, sizeof(content_meta_data)));
        R_TRY(ncmContentMetaDatabaseCommit(&db));
    }

    // push record
    {
        if (pbox) {
            pbox->NewTransfer("Pushing application record"_i18n).UpdateTransfer(5, 8);
        }
        Service srv{}, *srv_ptr = &srv;
        bool already_installed{};

        if (hosversionAtLeast(3,0,0)) {
            R_TRY(nsGetApplicationManagerInterface(&srv));
        } else {
            srv_ptr = nsGetServiceSession_ApplicationManagerInterface();
        }
        ON_SCOPE_EXIT(serviceClose(&srv));


        if (hosversionAtLeast(2,0,0)) {
            R_TRY(nsIsAnyApplicationEntityInstalled(tid, &already_installed));
        }

        // remove old id for forwarders.
        const auto rc = nsDeleteApplicationCompletely(old_tid);
        if (R_FAILED(rc) && rc != 0x410) { // not found
            App::Notify("Failed to remove old forwarder, please manually remove it!"_i18n);
        }

        // remove previous application record
        if (already_installed || hosversionBefore(2,0,0)) {
            const auto rc = ns::DeleteApplicationRecord(srv_ptr, tid);
            R_UNLESS(R_SUCCEEDED(rc) || hosversionBefore(2,0,0), rc);
        }

        R_TRY(ns::PushApplicationRecord(srv_ptr, tid, &content_storage_record, 1));

        // force flush
        if (already_installed || hosversionBefore(2,0,0)) {
            const auto rc = ns::InvalidateApplicationControlCache(srv_ptr, tid);
            R_UNLESS(R_SUCCEEDED(rc) || hosversionBefore(2,0,0), rc);
        }
    }

    R_SUCCEED();
}

} // namespace

auto install_forwarder(ui::ProgressBox* pbox, OwoConfig& config, NcmStorageId storage_id) -> Result {
    return install_forwader_internal(pbox, config, storage_id);
}

auto install_forwarder(OwoConfig& config, NcmStorageId storage_id) -> Result {
    App::Push<ui::ProgressBox>(0, "Installing Forwarder"_i18n, config.name, [config, storage_id](auto pbox) mutable -> Result {
        return install_forwarder(pbox, config, storage_id);
    });
    R_SUCCEED();
}

} // namespace sphaira
