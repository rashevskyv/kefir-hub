#include "account/account_link_internal.hpp"
#include "account/account_restore.hpp"
#include "app.hpp"
#include "app_paths.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "i18n.hpp"
#include "utils/utils.hpp"
#include <switch/services/pm.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>

namespace sphaira::account_link {
auto ApplyLinkPackages(const std::vector<TargetLink>& targets, u32& out_linked_count) -> Result {
    out_linked_count = 0;
    if (targets.empty()) {
        R_SUCCEED();
    }

    for (const auto& target : targets) {
        if (target.pkg.nas_id == 0) {
            log_write("[ACC] ApplyLinkPackages validation failed: nas_id is 0\n");
            return Result_FsInvalidType;
        }
        if (target.pkg.baas_data.size() < 24) {
            log_write("[ACC] ApplyLinkPackages validation failed: baas_data size < 24\n");
            return Result_FsInvalidType;
        }
        u64 emb_nas_id = 0;
        std::memcpy(&emb_nas_id, target.pkg.baas_data.data() + 16, sizeof(u64));
        if (emb_nas_id != target.pkg.nas_id) {
            log_write("[ACC] ApplyLinkPackages validation failed: baas emb_nas_id mismatch\n");
            return Result_FsInvalidType;
        }
        if (target.pkg.nas_files.empty()) {
            log_write("[ACC] ApplyLinkPackages validation failed: nas_files empty\n");
            return Result_FsInvalidType;
        }
        bool has_id_token = false;
        bool has_refresh_token = false;
        for (const auto& nf : target.pkg.nas_files) {
            if (!IsSafeDumpFileName(nf.filename)) {
                log_write("[ACC] ApplyLinkPackages validation failed: unsafe filename\n");
                return Result_FsInvalidType;
            }
            if (!NasFileMatches(nf.filename, target.pkg.nas_id)) {
                log_write("[ACC] ApplyLinkPackages validation failed: nas file mismatch\n");
                return Result_FsInvalidType;
            }
            if (nf.data.empty()) {
                log_write("[ACC] ApplyLinkPackages validation failed: empty payload\n");
                return Result_FsInvalidType;
            }
            const auto lower = ToLowerCopy(nf.filename);
            if (EndsWith(lower, "_id.token")) {
                has_id_token = true;
            } else if (EndsWith(lower, "_refresh.token")) {
                has_refresh_token = true;
            }
        }
        if (!has_id_token || !has_refresh_token) {
            log_write("[ACC] ApplyLinkPackages validation failed: missing tokens\n");
            return Result_FsInvalidType;
        }
    }

    for (size_t i = 0; i < targets.size(); i++) {
        for (size_t j = i + 1; j < targets.size(); j++) {
            if (targets[i].uid.uid[0] == targets[j].uid.uid[0] &&
                targets[i].uid.uid[1] == targets[j].uid.uid[1]) {
                log_write("[ACC] ApplyLinkPackages validation failed: duplicate target UID\n");
                return Result_FsInvalidType;
            }
        }
    }

    std::vector<u64> incoming_nas;
    for (const auto& target : targets) {
        if (std::find(incoming_nas.begin(), incoming_nas.end(), target.pkg.nas_id) != incoming_nas.end()) {
            log_write("[ACC] ApplyLinkPackages refused: two targets share incoming nas identity\n");
            return Result_FsInvalidType;
        }
        incoming_nas.push_back(target.pkg.nas_id);
    }

    log_write_error("[ACC_DIAG] enter validated ApplyLinkPackages targets=%u",
        static_cast<u32>(targets.size()));

    TerminateAccountDaemons();

    auto save = OpenAccountSaveWritable();
    const auto save_rc = save.GetFsOpenResult();
    if (R_FAILED(save_rc)) {
        log_write_error("[ACC_DIAG] OpenAccountSaveWritable failed rc=0x%X", save_rc);
        log_write("[ACC] OpenAccountSaveWritable failed 0x%X\n", save_rc);
        return save_rc;
    }
    log_write_error("[ACC_DIAG] OpenAccountSaveWritable ok rc=0x%X", save_rc);
    log_write("[ACC] OpenAccountSaveWritable ok\n");

    std::string existing_baas_dir;
    std::string existing_nas_dir;
    ResolveSuDirs(save, existing_baas_dir, existing_nas_dir);
    log_write_error("[ACC_DIAG] resolved dirs: baas=%d nas=%d",
        !existing_baas_dir.empty(), !existing_nas_dir.empty());

    if (!existing_baas_dir.empty()) {
        fs::Dir bd;
        const auto open_dir_rc = save.OpenDirectory(existing_baas_dir.c_str(), FsDirOpenMode_ReadFiles, &bd);
        if (R_FAILED(open_dir_rc)) {
            log_write_error("[ACC_DIAG] OpenDirectory baas failed rc=0x%X", open_dir_rc);
            return open_dir_rc;
        }
        std::vector<FsDirectoryEntry> baas_entries;
        const auto read_dir_rc = bd.ReadAll(baas_entries);
        if (R_FAILED(read_dir_rc)) {
            log_write_error("[ACC_DIAG] ReadAll baas failed rc=0x%X", read_dir_rc);
            return read_dir_rc;
        }

        for (const auto& e : baas_entries) {
            if (e.type != FsDirEntryType_File) {
                continue;
            }
            const std::string bf = e.name;
            const auto bf_path = existing_baas_dir + "/" + bf;
            std::vector<u8> bdata;
            const auto read_rc = save.read_entire_file(bf_path.c_str(), bdata);
            if (R_FAILED(read_rc)) {
                log_write_error("[ACC_DIAG] baas preflight: read file failed rc=0x%X", read_rc);
                log_write("[ACC] ApplyLinkPackages preflight: failed reading baas file rc=0x%X\n", read_rc);
                return read_rc;
            }
            if (bdata.size() < 24) {
                log_write_error("[ACC_DIAG] baas preflight: record too short size=%zu", bdata.size());
                log_write("[ACC] ApplyLinkPackages preflight: baas file shorter than 24 bytes (%zu)\n", bdata.size());
                return Result_FsInvalidType;
            }
            u64 file_nas = 0;
            std::memcpy(&file_nas, bdata.data() + 16, sizeof(u64));
            if (file_nas == 0) {
                log_write_error("[ACC_DIAG] baas preflight: zero embedded identity ignored");
                log_write("[ACC] ApplyLinkPackages preflight: zero embedded identity ignored\n");
                continue;
            }
            for (const auto& target : targets) {
                if (file_nas == target.pkg.nas_id) {
                    bool belongs_to_target = false;
                    const auto cands = BaasCandidateNames(target.uid);
                    for (const auto& cand : cands) {
                        if (strcasecmp(bf.c_str(), cand.c_str()) == 0) {
                            belongs_to_target = true;
                            break;
                        }
                    }
                    if (!belongs_to_target) {
                        log_write_error("[ACC_DIAG] baas preflight: collision with incoming identity");
                        log_write("[ACC] ApplyLinkPackages collision: incoming nas already present in baas not belonging to destination uid\n");
                        return Result_FsInvalidType;
                    }
                }
            }
        }
    }

    log_write_error("[ACC_DIAG] baas preflight ok");

    fs::FsNativeSd sd;
    char stamp[32]{};
    const auto t = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::localtime(&t));
    const auto rollback_dir = std::string("/config/kefir/account_link_rollback/") + stamp;
    const auto rb_base_rc = sd.CreateDirectoryRecursively(rollback_dir.c_str());
    if (R_FAILED(rb_base_rc)) {
        log_write_error("[ACC_DIAG] rollback dir create base failed rc=0x%X", rb_base_rc);
        return rb_base_rc;
    }
    const auto rb_baas_rc = sd.CreateDirectoryRecursively((rollback_dir + "/baas").c_str());
    if (R_FAILED(rb_baas_rc)) {
        log_write_error("[ACC_DIAG] rollback dir create baas failed rc=0x%X", rb_baas_rc);
        return rb_baas_rc;
    }
    const auto rb_nas_rc = sd.CreateDirectoryRecursively((rollback_dir + "/nas").c_str());
    if (R_FAILED(rb_nas_rc)) {
        log_write_error("[ACC_DIAG] rollback dir create nas failed rc=0x%X", rb_nas_rc);
        return rb_nas_rc;
    }
    log_write_error("[ACC_DIAG] rollback dirs created ok");

    std::string baas_dir = existing_baas_dir;
    std::string nas_dir = existing_nas_dir;
    if (baas_dir.empty()) {
        if (!save.DirExists("/su")) {
            R_TRY(save.CreateDirectoryRecursively("/su"));
        }
        if (!save.DirExists("/su/baas")) {
            R_TRY(save.CreateDirectoryRecursively("/su/baas"));
        }
        baas_dir = "/su/baas";
    }
    if (nas_dir.empty()) {
        if (!save.DirExists("/su")) {
            R_TRY(save.CreateDirectoryRecursively("/su"));
        }
        if (!save.DirExists("/su/nas")) {
            R_TRY(save.CreateDirectoryRecursively("/su/nas"));
        }
        nas_dir = "/su/nas";
    }

    auto backup_save_file = [&](const std::string& src_path, const std::string& dst_subpath) -> Result {
        std::vector<u8> data;
        if (R_SUCCEEDED(save.read_entire_file(src_path.c_str(), data)) && !data.empty()) {
            R_TRY(sd.write_entire_file((rollback_dir + "/" + dst_subpath).c_str(), data));
        }
        R_SUCCEED();
    };

    const auto existing_nas_files = ListDirFiles(save, nas_dir);

    std::vector<u64> old_nas_ids_to_clean;
    u32 baas_removed = 0;
    auto delete_baas = [&](const std::string& bf) -> Result {
        const auto baas_full = baas_dir + "/" + bf;
        if (!save.FileExists(baas_full.c_str())) {
            R_SUCCEED();
        }
        R_TRY(backup_save_file(baas_full, "baas/" + bf));
        std::vector<u8> old_baas;
        if (R_SUCCEEDED(save.read_entire_file(baas_full.c_str(), old_baas)) && old_baas.size() >= 24) {
            u64 old_nas_id = 0;
            std::memcpy(&old_nas_id, old_baas.data() + 16, sizeof(u64));
            if (old_nas_id != 0 &&
                std::find(incoming_nas.begin(), incoming_nas.end(), old_nas_id) == incoming_nas.end() &&
                std::find(old_nas_ids_to_clean.begin(), old_nas_ids_to_clean.end(), old_nas_id) == old_nas_ids_to_clean.end()) {
                old_nas_ids_to_clean.push_back(old_nas_id);
            }
        }
        log_write("[ACC] removing old baas file for target\n");
        R_TRY(save.DeleteFile(baas_full.c_str()));
        baas_removed++;
        R_SUCCEED();
    };

    for (const auto& target : targets) {
        const auto cands = BaasCandidateNames(target.uid);
        const auto live_baas = ListDirFiles(save, baas_dir);
        for (const auto& bf : live_baas) {
            bool drop = false;
            for (const auto& cand : cands) {
                if (strcasecmp(bf.c_str(), cand.c_str()) == 0) {
                    drop = true;
                    break;
                }
            }
            if (drop) {
                R_TRY(delete_baas(bf));
            }
        }

        const auto new_baas_path = baas_dir + "/" + UidDashedLinkalho(target.uid) + ".dat";
        R_TRY(save.write_entire_file(new_baas_path.c_str(), target.pkg.baas_data));
        log_write("[ACC] baas bound to target uid\n");

        for (const auto& nf : target.pkg.nas_files) {
            const auto nas_dst = nas_dir + "/" + nf.filename;
            if (save.FileExists(nas_dst.c_str())) {
                R_TRY(backup_save_file(nas_dst, "nas/" + nf.filename));
                R_TRY(save.DeleteFile(nas_dst.c_str()));
            }
            R_TRY(save.write_entire_file(nas_dst.c_str(), nf.data));
        }
    }

    if (baas_removed == 0) {
        log_write("[ACC] ApplyLinkPackages: no existing baas removed\n");
    } else {
        log_write("[ACC] ApplyLinkPackages: removed %u baas file(s)\n", baas_removed);
    }

    for (const auto old_id : old_nas_ids_to_clean) {
        if (BaasStillReferencesNas(save, baas_dir, old_id)) {
            continue;
        }
        for (const auto& nf : existing_nas_files) {
            if (NasFileMatches(nf, old_id)) {
                const auto nas_full = nas_dir + "/" + nf;
                R_TRY(backup_save_file(nas_full, "nas/" + nf));
                R_TRY(save.DeleteFile(nas_full.c_str()));
            }
        }
    }

    const auto commit_rc = save.Commit();
    if (R_FAILED(commit_rc)) {
        log_write_error("[ACC_DIAG] Commit failed rc=0x%X", commit_rc);
        log_write("[ACC] Commit failed 0x%X\n", commit_rc);
        return commit_rc;
    }
    log_write_error("[ACC_DIAG] Commit ok rc=0x%X", commit_rc);
    log_write("[ACC] Commit ok\n");

    out_linked_count = static_cast<u32>(targets.size());
    log_write_error("[ACC_DIAG] ApplyLinkPackages completed count=%u", out_linked_count);
    log_write("[ACC] ApplyLinkPackages completed for %u target(s)\n", out_linked_count);
    R_SUCCEED();
}


auto UnlinkLinkedProfiles(const std::vector<AccountUid>& uids, u32& out_unlinked_count) -> Result {
    out_unlinked_count = 0;
    R_UNLESS(!uids.empty(), Result_FsEmpty);

    TerminateAccountDaemons();

    auto save = OpenAccountSaveWritable();
    const auto save_rc = save.GetFsOpenResult();
    if (R_FAILED(save_rc)) {
        log_write("[ACC] Unlink OpenAccountSaveWritable failed 0x%X\n", save_rc);
        return save_rc;
    }

    std::string baas_dir;
    std::string nas_dir;
    ResolveSuDirs(save, baas_dir, nas_dir);
    R_UNLESS(!baas_dir.empty(), Result_FsEmpty);

    fs::FsNativeSd sd;
    char stamp[32]{};
    const auto t = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::localtime(&t));
    const auto rollback_dir = std::string("/config/kefir/account_link_rollback/") + stamp;
    R_TRY(sd.CreateDirectoryRecursively(rollback_dir.c_str()));
    R_TRY(sd.CreateDirectoryRecursively((rollback_dir + "/baas").c_str()));
    R_TRY(sd.CreateDirectoryRecursively((rollback_dir + "/nas").c_str()));

    auto backup_save_file = [&](const std::string& src_path, const std::string& dst_subpath) -> Result {
        std::vector<u8> data;
        if (R_SUCCEEDED(save.read_entire_file(src_path.c_str(), data)) && !data.empty()) {
            R_TRY(sd.write_entire_file((rollback_dir + "/" + dst_subpath).c_str(), data));
        }
        R_SUCCEED();
    };

    const auto existing_baas = ListDirFiles(save, baas_dir);
    const auto existing_nas = nas_dir.empty() ? std::vector<std::string>{} : ListDirFiles(save, nas_dir);
    std::vector<u64> nas_ids_to_clean;

    for (const auto& uid : uids) {
        const auto cands = BaasCandidateNames(uid);
        bool removed_any = false;
        for (const auto& cand : cands) {
            for (const auto& bf : existing_baas) {
                if (strcasecmp(bf.c_str(), cand.c_str()) != 0) {
                    continue;
                }
                const auto baas_full = baas_dir + "/" + bf;
                R_TRY(backup_save_file(baas_full, "baas/" + bf));

                std::vector<u8> baas_data;
                if (R_SUCCEEDED(save.read_entire_file(baas_full.c_str(), baas_data)) && baas_data.size() >= 24) {
                    u64 nas_id = 0;
                    std::memcpy(&nas_id, baas_data.data() + 16, sizeof(u64));
                    if (nas_id != 0 &&
                        std::find(nas_ids_to_clean.begin(), nas_ids_to_clean.end(), nas_id) == nas_ids_to_clean.end()) {
                        nas_ids_to_clean.push_back(nas_id);
                    }
                }

                R_TRY(save.DeleteFile(baas_full.c_str()));
                removed_any = true;
            }
        }
        if (removed_any) {
            out_unlinked_count++;
        }
    }

    if (!nas_dir.empty()) {
        for (const auto nas_id : nas_ids_to_clean) {
            if (BaasStillReferencesNas(save, baas_dir, nas_id)) {
                continue;
            }
            for (const auto& nf : existing_nas) {
                if (!NasFileMatches(nf, nas_id)) {
                    continue;
                }
                const auto nas_full = nas_dir + "/" + nf;
                R_TRY(backup_save_file(nas_full, "nas/" + nf));
                R_TRY(save.DeleteFile(nas_full.c_str()));
            }
        }
    }

    const auto commit_rc = save.Commit();
    if (R_FAILED(commit_rc)) {
        log_write("[ACC] Unlink Commit failed 0x%X\n", commit_rc);
        return commit_rc;
    }
    log_write("[ACC] UnlinkLinkedProfiles completed for %u profile(s)\n", out_unlinked_count);
    R_SUCCEED();
}


auto ExportUserLinkPackage(const AccountUid& uid, const std::string& out_dir, std::string& out_link_status, bool may_terminate_account) -> Result
{
    out_link_status = "none";
    u64 nas_id = 0;
    QueryNintendoAccountId(uid, nas_id);

    auto save = OpenAccountSaveForExport(may_terminate_account);
    if (R_FAILED(save.GetFsOpenResult())) {
        log_write("[ACC] ExportUserLinkPackage save open failed 0x%X\n", save.GetFsOpenResult());
        out_link_status = (nas_id != 0) ? "unavailable" : "none";
        R_SUCCEED();
    }

    std::string baas_dir;
    std::string nas_dir;
    ResolveSuDirs(save, baas_dir, nas_dir);
    if (baas_dir.empty()) {
        out_link_status = (nas_id != 0) ? "unavailable" : "none";
        R_SUCCEED();
    }

    const auto baas_files = ListDirFiles(save, baas_dir);
    std::string matched_baas_name;
    std::vector<u8> matched_baas_data;
    if (nas_id != 0) {
        for (const auto& bf : baas_files) {
            std::vector<u8> bdata;
            if (R_SUCCEEDED(save.read_entire_file((baas_dir + "/" + bf).c_str(), bdata)) && bdata.size() >= 24) {
                u64 file_nas_id = 0;
                std::memcpy(&file_nas_id, bdata.data() + 16, sizeof(u64));
                if (file_nas_id == nas_id) {
                    matched_baas_name = bf;
                    matched_baas_data = std::move(bdata);
                    break;
                }
            }
        }
    }
    if (matched_baas_data.empty()) {
        const auto cands = BaasCandidateNames(uid);
        for (const auto& cand : cands) {
            for (const auto& bf : baas_files) {
                if (strcasecmp(bf.c_str(), cand.c_str()) != 0) {
                    continue;
                }
                std::vector<u8> bdata;
                if (R_SUCCEEDED(save.read_entire_file((baas_dir + "/" + bf).c_str(), bdata)) && bdata.size() >= 24) {
                    matched_baas_name = bf;
                    matched_baas_data = std::move(bdata);
                    std::memcpy(&nas_id, matched_baas_data.data() + 16, sizeof(u64));
                }
                break;
            }
            if (!matched_baas_data.empty()) {
                break;
            }
        }
    }

    if (matched_baas_data.empty()) {
        out_link_status = (nas_id != 0) ? "unavailable" : "none";
        R_SUCCEED();
    }
    R_UNLESS(IsSafeDumpFileName(matched_baas_name), Result_FsInvalidType);

    fs::FsNativeSd sd;
    R_TRY(sd.CreateDirectoryRecursively((out_dir + "/baas").c_str()));
    R_TRY(sd.write_entire_file((out_dir + "/baas/" + matched_baas_name).c_str(), matched_baas_data));

    bool has_id_token = false;
    bool has_refresh_token = false;
    if (!nas_dir.empty()) {
        R_TRY(sd.CreateDirectoryRecursively((out_dir + "/nas").c_str()));
        for (const auto& nf : ListDirFiles(save, nas_dir)) {
            if (NasFileMatches(nf, nas_id)) {
                R_UNLESS(IsSafeDumpFileName(nf), Result_FsInvalidType);
                std::vector<u8> ndata;
                R_TRY(save.read_entire_file((nas_dir + "/" + nf).c_str(), ndata));
                R_UNLESS(!ndata.empty(), Result_FsInvalidType);
                R_TRY(sd.write_entire_file((out_dir + "/nas/" + nf).c_str(), ndata));
                const auto lower = ToLowerCopy(nf);
                if (EndsWith(lower, "_id.token")) {
                    has_id_token = true;
                } else if (EndsWith(lower, "_refresh.token")) {
                    has_refresh_token = true;
                }
            }
        }
    }

    if (has_id_token && has_refresh_token) {
        out_link_status = "complete";
    } else {
        out_link_status = "incomplete";
    }

    R_SUCCEED();
}


} // namespace sphaira::account_link
