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
auto QueryIdTokenCacheRaw(Service* srv, u32& out_size) -> Result {
    out_size = 0;
    alignas(16) u8 buf[0xC00]{};

    const auto try_cmd = [&](u32 cmd_id) -> Result {
        out_size = 0;
        return serviceDispatchOut(srv, cmd_id, out_size,
            .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
            .buffers = { { buf, sizeof(buf) } });
    };

    Result rc = MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    if (hosversionAtLeast(19, 0, 0)) {
        rc = try_cmd(4);
        if (R_FAILED(rc)) {
            rc = try_cmd(3);
        }
    } else {
        rc = try_cmd(3);
        if (R_FAILED(rc)) {
            rc = try_cmd(4);
        }
    }

    return rc;
}

auto QueryIdTokenCache(Service* srv) -> bool {
    u32 actual_size = 0;
    const auto rc = QueryIdTokenCacheRaw(srv, actual_size);
    return R_SUCCEEDED(rc) && actual_size > 0;
}

auto QueryHorizonUserLink(const AccountUid& uid, bool& out_linked, LinkKind& out_kind) -> Result {
    out_linked = false;
    out_kind = LinkKind::None;

    Service accsu{};
    R_TRY(OpenAccSu(&accsu));
    ON_SCOPE_EXIT(serviceClose(&accsu));

    Service admin{};
    R_TRY(serviceDispatchIn(&accsu, 250, uid,
        .out_num_objects = 1,
        .out_objects = &admin));
    ON_SCOPE_EXIT(serviceClose(&admin));

    bool is_linked = false;
    const auto rc = serviceDispatchOut(&admin, 250, is_linked); // IsLinkedWithNintendoAccount
    if (rc == ResultNetworkServiceAccountRegistrationRequired) {
        out_linked = false;
        out_kind = LinkKind::None;
        R_SUCCEED();
    }
    if (R_FAILED(rc)) {
        out_linked = false;
        out_kind = LinkKind::None;
        return rc;
    }

    if (!is_linked) {
        out_linked = false;
        out_kind = LinkKind::None;
        R_SUCCEED();
    }

    out_linked = true;
    if (QueryIdTokenCache(&admin)) {
        out_kind = LinkKind::Official;
    } else {
        out_kind = LinkKind::Offline;
    }

    R_SUCCEED();
}


auto QueryNintendoAccountId(const AccountUid& uid, u64& out_nas_id) -> Result {
    out_nas_id = 0;
    Service accsu{};
    R_TRY(OpenAccSu(&accsu));
    ON_SCOPE_EXIT(serviceClose(&accsu));

    Service admin{};
    R_TRY(serviceDispatchIn(&accsu, 250, uid,
        .out_num_objects = 1,
        .out_objects = &admin));
    ON_SCOPE_EXIT(serviceClose(&admin));

    bool is_linked = false;
    const auto lrc = serviceDispatchOut(&admin, 250, is_linked); // IsLinkedWithNintendoAccount
    if (R_FAILED(lrc) || !is_linked) {
        return ResultNetworkServiceAccountRegistrationRequired;
    }

    R_TRY(serviceDispatchOut(&admin, 120, out_nas_id)); // GetNasId
    if (out_nas_id == 0) {
        return ResultNetworkServiceAccountRegistrationRequired;
    }
    R_SUCCEED();
}

auto FindLiveUidByNasId(u64 nas_id, AccountUid& out_uid) -> bool {
    out_uid = {};
    if (nas_id == 0) {
        return false;
    }

    const auto live = App::GetAccountList();

    for (const auto& base : live) {
        u64 ipc_nas = 0;
        const auto qrc = QueryNintendoAccountId(base.uid, ipc_nas);
        log_write("[ACC] QueryNintendoAccountId rc=0x%X\n", qrc);

        // Replace only when pack nas is proven equal (IPC nas == pack nas).
        if (R_SUCCEEDED(qrc) && ipc_nas != 0 && ipc_nas == nas_id) {
            out_uid = base.uid;
            log_write("[ACC] target identity is IPC-linked to live profile\n");
            return true;
        }

        bool horizon_linked = false;
        const auto hrc = QueryHorizonLinkStatus(base.uid, horizon_linked);
        if (R_SUCCEEDED(hrc) && horizon_linked) {
            if (R_SUCCEEDED(qrc) && ipc_nas != 0 && ipc_nas != nas_id) {
                log_write("[ACC] linked profile has different identity; Create still allowed\n");
            } else if (R_FAILED(qrc) || ipc_nas == 0) {
                // Unproven Query is not Replace: need baas proof or Create.
                log_write("[ACC] linked profile identity unproven (Query rc=0x%X); Create allowed until baas proves match\n", qrc);
            }
        }
    }

    auto save = TryOpenAccountSave();
    if (R_FAILED(save.GetFsOpenResult())) {
        // Query failed and 0010 closed → cannot prove equality; Create allowed.
        log_write("[ACC] FindLiveUidByNasId: account save closed 0x%X (Create allowed)\n",
            save.GetFsOpenResult());
        return false;
    }
    std::string baas_dir;
    std::string nas_dir;
    ResolveSuDirs(save, baas_dir, nas_dir);
    if (baas_dir.empty()) {
        // Save open but no baas tree → nothing can hold this nas; Create is safe.
        log_write("[ACC] FindLiveUidByNasId: baas dir missing (Create allowed)\n");
        return false;
    }

    for (const auto& bf : ListDirFiles(save, baas_dir)) {
        std::vector<u8> data;
        if (R_FAILED(save.read_entire_file((baas_dir + "/" + bf).c_str(), data)) || data.size() < 24) {
            continue;
        }
        u64 file_nas = 0;
        std::memcpy(&file_nas, data.data() + 16, sizeof(u64));
        if (file_nas != nas_id) {
            continue;
        }
        for (const auto& base : live) {
            const auto cands = BaasCandidateNames(base.uid);
            for (const auto& cand : cands) {
                if (strcasecmp(bf.c_str(), cand.c_str()) == 0) {
                    out_uid = base.uid;
                    log_write("[ACC] identity baas matches live profile\n");
                    return true;
                }
            }
        }
        log_write("[ACC] orphan baas record found (uid not live)\n");
    }

    // Save open and no baas carries this nas → Create allowed.
    log_write("[ACC] identity not in baas; Create allowed\n");
    return false;
}

auto QueryHorizonLinkStatus(const AccountUid& uid, bool& out_linked) -> Result {
    LinkKind kind = LinkKind::None;
    return QueryHorizonUserLink(uid, out_linked, kind);
}

auto ListUsers() -> std::vector<User> {
    std::vector<User> out;
    for (const auto& base : App::GetAccountList()) {
        User u;
        u.uid = base.uid;
        u.nickname = base.nickname;
        u.uid_hex = UidHex(base.uid);
        out.push_back(std::move(u));
    }

    for (auto& u : out) {
        bool linked = false;
        LinkKind kind = LinkKind::None;
        const auto rc = QueryHorizonUserLink(u.uid, linked, kind);
        if (R_SUCCEEDED(rc)) {
            u.linked_known = true;
            u.horizon_linked = linked;
            u.kind = kind;
        } else {
            u.linked_known = false;
            u.horizon_linked = false;
            u.kind = LinkKind::None;
            log_write("[ACC] Horizon link check failed 0x%X\n", rc);
        }
    }

    bool save_open = false;
    auto save = TryOpenAccountSave();
    if (R_SUCCEEDED(save.GetFsOpenResult())) {
        save_open = true;
        std::string baas_dir;
        std::string nas_dir;
        ResolveSuDirs(save, baas_dir, nas_dir);

        const auto baas_files = !baas_dir.empty() ? ListDirFiles(save, baas_dir) : std::vector<std::string>{};
        const auto nas_files = !nas_dir.empty() ? ListDirFiles(save, nas_dir) : std::vector<std::string>{};

        for (auto& u : out) {
            if (!u.horizon_linked) {
                u.kind = LinkKind::None;
                continue;
            }

            u64 nas_id = 0;
            if (R_FAILED(QueryNintendoAccountId(u.uid, nas_id)) || nas_id == 0) {
                nas_id = 0;
                const auto cands = BaasCandidateNames(u.uid);
                std::string matched_baas;
                for (const auto& cand : cands) {
                    for (const auto& bf : baas_files) {
                        if (strcasecmp(bf.c_str(), cand.c_str()) == 0) {
                            matched_baas = bf;
                            break;
                        }
                    }
                    if (!matched_baas.empty()) {
                        break;
                    }
                }

                if (!matched_baas.empty()) {
                    std::vector<u8> baas_data;
                    if (R_SUCCEEDED(save.read_entire_file((baas_dir + "/" + matched_baas).c_str(), baas_data)) && baas_data.size() >= 24) {
                        std::memcpy(&nas_id, baas_data.data() + 16, sizeof(u64));
                    }
                }
            }

            if (nas_id != 0) {
                bool has_id_token = false;
                bool has_refresh_token = false;
                for (const auto& nf : nas_files) {
                    if (NasFileMatches(nf, nas_id)) {
                        const auto lower = ToLowerCopy(nf);
                        if (EndsWith(lower, "_id.token")) {
                            has_id_token = true;
                        } else if (EndsWith(lower, "_refresh.token")) {
                            has_refresh_token = true;
                        }
                    }
                }

                if (has_id_token && has_refresh_token) {
                    u.kind = LinkKind::Official;
                } else {
                    u.kind = LinkKind::Offline;
                }
            } else {
                u.kind = LinkKind::Offline;
            }
        }
    }

#if DOCS_DEMO
    // docs screenshots: the first two profiles show as linked (Eden has no Nintendo Account data).
    for (size_t i = 0; i < out.size() && i < 2; i++) {
        out[i].linked_known = out[i].horizon_linked = true;
        out[i].kind = LinkKind::Offline;
    }
#endif

    u32 count_official = 0;
    u32 count_offline = 0;
    u32 count_none = 0;
    for (const auto& u : out) {
        if (u.kind == LinkKind::Official) {
            count_official++;
        } else if (u.kind == LinkKind::Offline) {
            count_offline++;
        } else {
            count_none++;
        }
    }
    log_write("[ACC] ListUsers: save_open=%d official=%u linked_unverified=%u none=%u\n",
        save_open ? 1 : 0, count_official, count_offline, count_none);

    return out;
}


} // namespace sphaira::account_link
