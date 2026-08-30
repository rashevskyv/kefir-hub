#include "account_link.hpp"
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
namespace {

constexpr u64 ACCOUNT_SAVE_ID = 0x8000000000000010ULL;
constexpr Result ResultNetworkServiceAccountRegistrationRequired = MAKERESULT(124, 200);

auto ToLowerCopy(std::string s) -> std::string {
    for (auto& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

auto ToUpperCopy(std::string s) -> std::string {
    for (auto& c : s) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

auto NasHex(u64 nas_id) -> std::string {
    char buf[17]{};
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(nas_id));
    return buf;
}

auto NasHexShort(u64 nas_id) -> std::string {
    char buf[17]{};
    std::snprintf(buf, sizeof(buf), "%llx", static_cast<unsigned long long>(nas_id));
    return buf;
}

auto UidDashedLinkalho(const AccountUid& uid) -> std::string {
    char buf[40]{};
    std::snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%02x%02x-%08x%04x",
        static_cast<unsigned>(uid.uid[0] & 0xffffffffu),
        static_cast<unsigned>((uid.uid[0] >> 32) & 0xffffu),
        static_cast<unsigned>((uid.uid[0] >> 48) & 0xffffu),
        static_cast<unsigned>(uid.uid[1] & 0xffu),
        static_cast<unsigned>((uid.uid[1] >> 8) & 0xffu),
        static_cast<unsigned>((uid.uid[1] >> 32) & 0xffffffffu),
        static_cast<unsigned>((uid.uid[1] >> 16) & 0xffffu));
    return buf;
}

auto UidDashedRfc(const AccountUid& uid) -> std::string {
    char buf[40]{};
    std::snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%04x%08x",
        static_cast<unsigned>(uid.uid[0] & 0xffffffffu),
        static_cast<unsigned>((uid.uid[0] >> 32) & 0xffffu),
        static_cast<unsigned>((uid.uid[0] >> 48) & 0xffffu),
        static_cast<unsigned>(uid.uid[1] & 0xffffu),
        static_cast<unsigned>((uid.uid[1] >> 16) & 0xffffu),
        static_cast<unsigned>((uid.uid[1] >> 32) & 0xffffffffu));
    return buf;
}

auto UidHexRaw(const AccountUid& uid) -> std::string {
    char buf[33]{};
    std::snprintf(buf, sizeof(buf), "%016llX%016llX",
        static_cast<unsigned long long>(uid.uid[0]),
        static_cast<unsigned long long>(uid.uid[1]));
    return buf;
}

auto BaasCandidateNames(const AccountUid& uid) -> std::vector<std::string> {
    const auto dashed_l = UidDashedLinkalho(uid);
    const auto dashed_r = UidDashedRfc(uid);
    const auto raw = UidHexRaw(uid);
    std::vector<std::string> cands;
    auto add_cand = [&](const std::string& name) {
        if (!name.empty() && std::find(cands.begin(), cands.end(), name) == cands.end()) {
            cands.push_back(name);
        }
    };
    add_cand(dashed_r + ".dat");
    add_cand(dashed_l + ".dat");
    add_cand(raw + ".dat");
    add_cand(ToLowerCopy(raw) + ".dat");
    add_cand(ToLowerCopy(dashed_r) + ".dat");
    add_cand(ToLowerCopy(dashed_l) + ".dat");
    return cands;
}

auto BaseName(const std::string& path) -> std::string {
    auto p = path;
    while (!p.empty() && (p.back() == '/' || p.back() == '\\')) {
        p.pop_back();
    }
    const auto slash = p.find_last_of("/\\");
    if (slash == std::string::npos) {
        return p;
    }
    return p.substr(slash + 1);
}

auto NasPrefixes(u64 nas_id) -> std::vector<std::string> {
    std::vector<std::string> out;
    const auto hex16_l = ToLowerCopy(NasHex(nas_id));
    const auto hex16_u = ToUpperCopy(NasHex(nas_id));
    const auto hex_sh_l = ToLowerCopy(NasHexShort(nas_id));
    const auto hex_sh_u = ToUpperCopy(NasHexShort(nas_id));
    auto add_unique = [&](const std::string& s) {
        if (!s.empty() && std::find(out.begin(), out.end(), s) == out.end()) {
            out.push_back(s);
        }
    };
    add_unique(hex16_l);
    add_unique(hex16_u);
    add_unique(hex_sh_l);
    add_unique(hex_sh_u);
    return out;
}

auto NasFileMatches(const std::string& name, u64 nas_id) -> bool {
    if (nas_id == 0) {
        return false;
    }
    const auto lower = ToLowerCopy(name);
    for (const auto& prefix : NasPrefixes(nas_id)) {
        const auto lower_prefix = ToLowerCopy(prefix);
        if (lower.size() > lower_prefix.size() && lower.rfind(lower_prefix, 0) == 0) {
            const char next = lower[lower_prefix.size()];
            if (next == '.' || next == '_') {
                return true;
            }
        }
    }
    return false;
}

auto TryOpenAccountSave() -> fs::FsNativeSave {
    FsSaveDataAttribute attr{};
    attr.system_save_data_id = ACCOUNT_SAVE_ID;
    attr.save_data_type = FsSaveDataType_System;
    return fs::FsNativeSave(FsSaveDataType_System, FsSaveDataSpaceId_System, &attr, false);
}

auto OpenAccountSaveWritable() -> fs::FsNativeSave {
    FsSaveDataAttribute attr{};
    attr.system_save_data_id = ACCOUNT_SAVE_ID;
    attr.save_data_type = FsSaveDataType_System;
    return fs::FsNativeSave(FsSaveDataType_System, FsSaveDataSpaceId_System, &attr, false);
}

auto ListDirFiles(fs::Fs& f, const std::string& dir) -> std::vector<std::string> {
    std::vector<std::string> out;
    if (!f.DirExists(dir.c_str())) {
        return out;
    }
    fs::Dir d;
    if (R_FAILED(f.OpenDirectory(dir.c_str(), FsDirOpenMode_ReadFiles, &d))) {
        return out;
    }
    std::vector<FsDirectoryEntry> entries;
    if (R_FAILED(d.ReadAll(entries))) {
        return out;
    }
    for (const auto& e : entries) {
        if (e.type == FsDirEntryType_File) {
            out.emplace_back(e.name);
        }
    }
    return out;
}

auto CopyFile(fs::Fs& from, const std::string& src, fs::Fs& to, const std::string& dst) -> Result {
    std::vector<u8> data;
    R_TRY(from.read_entire_file(src.c_str(), data));
    R_TRY(to.write_entire_file(dst.c_str(), data));
    R_SUCCEED();
}

auto OpenAccSu(Service* out) -> Result {
    R_TRY(smGetService(out, "acc:su"));
    R_SUCCEED();
}

auto QueryAdministratorNintendoLink(const AccountUid& uid, bool& out_linked) -> Result {
    out_linked = false;

    Service accsu{};
    R_TRY(OpenAccSu(&accsu));
    ON_SCOPE_EXIT(serviceClose(&accsu));

    Service administrator{};
    R_TRY(serviceDispatchIn(&accsu, 250, uid,
        .out_num_objects = 1,
        .out_objects = &administrator));
    ON_SCOPE_EXIT(serviceClose(&administrator));

    u8 is_linked = 0;
    R_TRY(serviceDispatchOut(&administrator, 250, is_linked));

    out_linked = (is_linked != 0);
    R_SUCCEED();
}

auto IsSafeDumpFileName(const std::string& name) -> bool {
    if (name.empty()) {
        return false;
    }
    for (char c : name) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

auto EndsWith(const std::string& str, const std::string& suffix) -> bool {
    if (str.size() < suffix.size()) {
        return false;
    }
    return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

auto LoadRomfsDonorPackage(RomfsDonorPackage& out_pkg) -> Result {
    out_pkg = {};

    R_TRY(romfsInit());
    ON_SCOPE_EXIT(romfsExit());

    std::vector<u8> manifest_bytes;
    R_TRY(fs::read_entire_file("romfs:/account_link/manifest.txt", manifest_bytes));
    R_UNLESS(!manifest_bytes.empty(), Result_FsInvalidType);

    const std::string manifest_str(manifest_bytes.begin(), manifest_bytes.end());
    std::unordered_map<std::string, std::string> kv;
    size_t line_start = 0;
    while (line_start < manifest_str.size()) {
        auto line_end = manifest_str.find('\n', line_start);
        if (line_end == std::string::npos) {
            line_end = manifest_str.size();
        }
        auto line = manifest_str.substr(line_start, line_end - line_start);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto eq = line.find('=');
        if (eq != std::string::npos) {
            const auto k = line.substr(0, eq);
            const auto v = line.substr(eq + 1);
            kv[k] = v;
        }
        line_start = line_end + 1;
    }

    R_UNLESS(kv["format"] == "kefir_account_link", Result_FsInvalidType);
    R_UNLESS(kv["version"] == "3", Result_FsInvalidType);
    R_UNLESS(kv["romfs"] == "true", Result_FsInvalidType);
    R_UNLESS(kv["system_save"] == "8000000000000010", Result_FsInvalidType);
    R_UNLESS(kv["idgen_0011_included"] == "false", Result_FsInvalidType);
    R_UNLESS(kv["baas_file"] == "baas/link.dat", Result_FsInvalidType);

    const auto& nas_hex = kv["nintendo_account_id"];
    R_UNLESS(!nas_hex.empty() && nas_hex.size() <= 16, Result_FsInvalidType);
    for (char c : nas_hex) {
        R_UNLESS(std::isxdigit(static_cast<unsigned char>(c)), Result_FsInvalidType);
    }

    char* end = nullptr;
    const u64 nas_id = std::strtoull(nas_hex.c_str(), &end, 16);
    R_UNLESS(end && *end == '\0' && nas_id != 0, Result_FsInvalidType);

    const auto& nas_files_csv = kv["nas_files"];
    R_UNLESS(!nas_files_csv.empty(), Result_FsInvalidType);

    const auto baas_path = "romfs:/account_link/" + kv["baas_file"];
    std::vector<u8> baas_data;
    R_TRY(fs::read_entire_file(baas_path.c_str(), baas_data));
    R_UNLESS(baas_data.size() >= 24, Result_FsInvalidType);

    u64 baas_nas_id = 0;
    std::memcpy(&baas_nas_id, baas_data.data() + 16, sizeof(u64));
    R_UNLESS(baas_nas_id == nas_id, Result_FsInvalidType);

    std::vector<std::string> file_list;
    size_t csv_start = 0;
    while (csv_start < nas_files_csv.size()) {
        auto comma = nas_files_csv.find(',', csv_start);
        if (comma == std::string::npos) {
            comma = nas_files_csv.size();
        }
        auto fname = nas_files_csv.substr(csv_start, comma - csv_start);
        while (!fname.empty() && std::isspace(static_cast<unsigned char>(fname.front()))) {
            fname.erase(fname.begin());
        }
        while (!fname.empty() && std::isspace(static_cast<unsigned char>(fname.back()))) {
            fname.pop_back();
        }
        if (!fname.empty()) {
            file_list.push_back(fname);
        }
        csv_start = comma + 1;
    }
    R_UNLESS(!file_list.empty(), Result_FsInvalidType);

    bool has_id_token = false;
    bool has_refresh_token = false;
    std::vector<DonorNasFile> loaded_nas_files;
    loaded_nas_files.reserve(file_list.size());

    for (const auto& fname : file_list) {
        R_UNLESS(IsSafeDumpFileName(fname), Result_FsInvalidType);
        R_UNLESS(NasFileMatches(fname, nas_id), Result_FsInvalidType);

        const auto fpath = "romfs:/account_link/nas/" + fname;
        std::vector<u8> fdata;
        R_TRY(fs::read_entire_file(fpath.c_str(), fdata));
        R_UNLESS(!fdata.empty(), Result_FsInvalidType);

        const auto lower = ToLowerCopy(fname);
        if (EndsWith(lower, "_id.token")) {
            has_id_token = true;
        } else if (EndsWith(lower, "_refresh.token")) {
            has_refresh_token = true;
        }

        loaded_nas_files.push_back(DonorNasFile{fname, std::move(fdata)});
    }

    R_UNLESS(has_id_token && has_refresh_token, Result_FsInvalidType);

    out_pkg.nas_id = nas_id;
    out_pkg.baas_data = std::move(baas_data);
    out_pkg.nas_files = std::move(loaded_nas_files);
    R_SUCCEED();
}

auto LinkAllFromRomfsDonor(u32& out_linked_count) -> Result {
    out_linked_count = 0;

    RomfsDonorPackage donor_pkg;
    R_TRY(LoadRomfsDonorPackage(donor_pkg));

    const auto all_users = ListUsers();
    std::vector<User> targets;
    for (const auto& u : all_users) {
        if (!u.linked_known) {
            continue;
        }
        if (u.kind == LinkKind::Offline || (u.kind == LinkKind::None && !u.horizon_linked)) {
            targets.push_back(u);
        }
    }

    if (targets.empty()) {
        R_SUCCEED();
    }

    if (R_SUCCEEDED(pmshellInitialize())) {
        ON_SCOPE_EXIT(pmshellExit());
        pmshellTerminateProgram(0x010000000000000CULL); // BCAT
        pmshellTerminateProgram(0x010000000000001EULL); // ACCOUNT
        pmshellTerminateProgram(0x010000000000003EULL); // OLSC
    }

    auto save = OpenAccountSaveWritable();
    R_TRY(save.GetFsOpenResult());

    if (!save.DirExists("/su")) {
        R_TRY(save.CreateDirectoryRecursively("/su"));
    }
    if (!save.DirExists("/su/baas")) {
        R_TRY(save.CreateDirectoryRecursively("/su/baas"));
    }
    if (!save.DirExists("/su/nas")) {
        R_TRY(save.CreateDirectoryRecursively("/su/nas"));
    }

    fs::FsNativeSd sd;
    char stamp[32]{};
    const auto t = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::localtime(&t));
    const auto rollback_dir = std::string("/config/kefir/account_link_rollback/") + stamp;
    sd.CreateDirectoryRecursively(rollback_dir.c_str());
    sd.CreateDirectoryRecursively((rollback_dir + "/baas").c_str());
    sd.CreateDirectoryRecursively((rollback_dir + "/nas").c_str());

    auto backup_save_file = [&](const std::string& src_path, const std::string& dst_subpath) {
        std::vector<u8> data;
        if (R_SUCCEEDED(save.read_entire_file(src_path.c_str(), data)) && !data.empty()) {
            sd.write_entire_file((rollback_dir + "/" + dst_subpath).c_str(), data);
        }
    };

    const auto existing_baas_files = ListDirFiles(save, "/su/baas");
    const auto existing_nas_files = ListDirFiles(save, "/su/nas");

    std::vector<User> final_targets;
    for (const auto& target : targets) {
        if (target.kind == LinkKind::None && target.linked_known && !target.horizon_linked) {
            final_targets.push_back(target);
            continue;
        }

        if (target.kind == LinkKind::Offline) {
            u64 nas_id = 0;
            const auto cands = BaasCandidateNames(target.uid);
            for (const auto& cand : cands) {
                for (const auto& bf : existing_baas_files) {
                    if (strcasecmp(bf.c_str(), cand.c_str()) == 0) {
                        const auto baas_full = "/su/baas/" + bf;
                        std::vector<u8> baas_data;
                        if (R_SUCCEEDED(save.read_entire_file(baas_full.c_str(), baas_data)) && baas_data.size() >= 24) {
                            std::memcpy(&nas_id, baas_data.data() + 16, sizeof(u64));
                        }
                        break;
                    }
                }
                if (nas_id != 0) {
                    break;
                }
            }

            if (nas_id != 0) {
                bool has_id_token = false;
                bool has_refresh_token = false;
                for (const auto& nf : existing_nas_files) {
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
                    continue;
                }
            }

            final_targets.push_back(target);
        }
    }

    if (final_targets.empty()) {
        R_SUCCEED();
    }

    std::vector<u64> old_nas_ids_to_clean;

    for (const auto& target : final_targets) {
        const auto cands = BaasCandidateNames(target.uid);
        for (const auto& cand : cands) {
            for (const auto& bf : existing_baas_files) {
                if (strcasecmp(bf.c_str(), cand.c_str()) == 0) {
                    const auto baas_full = "/su/baas/" + bf;
                    backup_save_file(baas_full, "baas/" + bf);

                    std::vector<u8> old_baas;
                    if (R_SUCCEEDED(save.read_entire_file(baas_full.c_str(), old_baas)) && old_baas.size() >= 24) {
                        u64 old_nas_id = 0;
                        std::memcpy(&old_nas_id, old_baas.data() + 16, sizeof(u64));
                        if (old_nas_id != 0 && old_nas_id != donor_pkg.nas_id) {
                            if (std::find(old_nas_ids_to_clean.begin(), old_nas_ids_to_clean.end(), old_nas_id) == old_nas_ids_to_clean.end()) {
                                old_nas_ids_to_clean.push_back(old_nas_id);
                            }
                        }
                    }
                    save.DeleteFile(baas_full.c_str());
                }
            }
        }

        const auto new_baas_path = "/su/baas/" + UidDashedLinkalho(target.uid) + ".dat";
        R_TRY(save.write_entire_file(new_baas_path.c_str(), donor_pkg.baas_data));
    }

    for (const auto old_id : old_nas_ids_to_clean) {
        for (const auto& nf : existing_nas_files) {
            if (NasFileMatches(nf, old_id)) {
                const auto nas_full = "/su/nas/" + nf;
                backup_save_file(nas_full, "nas/" + nf);
                save.DeleteFile(nas_full.c_str());
            }
        }
    }

    for (const auto& nf : donor_pkg.nas_files) {
        const auto nas_dst = "/su/nas/" + nf.filename;
        if (save.FileExists(nas_dst.c_str())) {
            backup_save_file(nas_dst, "nas/" + nf.filename);
            save.DeleteFile(nas_dst.c_str());
        }
        R_TRY(save.write_entire_file(nas_dst.c_str(), nf.data));
    }

    R_TRY(save.Commit());

    out_linked_count = static_cast<u32>(final_targets.size());
    log_write("[ACC] LinkAllFromRomfsDonor completed for %u profile(s)\n", out_linked_count);
    R_SUCCEED();
}

auto UidHex(const AccountUid& uid) -> std::string {
    return UidDashedRfc(uid);
}

auto QueryIdTokenCacheRaw(Service* manager, u32& out_size) -> Result {
    out_size = 0;
    alignas(16) u8 buf[0xC00]{};

    const auto try_cmd = [&](u32 cmd_id) -> Result {
        out_size = 0;
        return serviceDispatchOut(manager, cmd_id, out_size,
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

auto QueryIdTokenCache(Service* manager) -> bool {
    u32 actual_size = 0;
    const auto rc = QueryIdTokenCacheRaw(manager, actual_size);
    return R_SUCCEEDED(rc) && actual_size > 0;
}

auto QueryHorizonUserLink(const AccountUid& uid, bool& out_linked, LinkKind& out_kind) -> Result {
    out_linked = false;
    out_kind = LinkKind::None;

    Service accsu{};
    R_TRY(OpenAccSu(&accsu));
    ON_SCOPE_EXIT(serviceClose(&accsu));

    Service manager{};
    R_TRY(serviceDispatchIn(&accsu, 102, uid,
        .out_num_objects = 1,
        .out_objects = &manager));
    ON_SCOPE_EXIT(serviceClose(&manager));

    const auto rc = serviceDispatch(&manager, 0); // CheckAvailability
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

    out_linked = true;
    if (QueryIdTokenCache(&manager)) {
        out_kind = LinkKind::Official;
    } else {
        out_kind = LinkKind::Offline;
    }

    R_SUCCEED();
}

auto QueryHorizonLinkStatus(const AccountUid& uid, bool& out_linked) -> Result {
    LinkKind kind = LinkKind::None;
    return QueryHorizonUserLink(uid, out_linked, kind);
}

auto QueryNintendoAccountId(const AccountUid& uid, u64& out_nas_id) -> Result {
    out_nas_id = 0;
    Service accsu{};
    R_TRY(OpenAccSu(&accsu));
    ON_SCOPE_EXIT(serviceClose(&accsu));

    Service manager{};
    R_TRY(serviceDispatchIn(&accsu, 102, uid,
        .out_num_objects = 1,
        .out_objects = &manager));
    ON_SCOPE_EXIT(serviceClose(&manager));

    const auto rc = serviceDispatch(&manager, 0); // CheckAvailability
    if (R_FAILED(rc)) {
        return rc;
    }

    R_TRY(serviceDispatchOut(&manager, 120, out_nas_id));
    if (out_nas_id == 0) {
        return ResultNetworkServiceAccountRegistrationRequired;
    }
    R_SUCCEED();
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

    u32 admin_probed = 0;
    u32 admin_true = 0;
    u32 admin_false = 0;
    u32 admin_failed = 0;
    for (const auto& u : out) {
        if (u.linked_known && u.horizon_linked) {
            admin_probed++;
            bool admin_linked = false;
            if (R_SUCCEEDED(QueryAdministratorNintendoLink(u.uid, admin_linked))) {
                if (admin_linked) {
                    admin_true++;
                } else {
                    admin_false++;
                }
            } else {
                admin_failed++;
            }
        }
    }
    log_write("[ACC] AdminProbe: linked=%u true=%u false=%u failed=%u\n",
        admin_probed, admin_true, admin_false, admin_failed);

    bool save_open = false;
    auto save = TryOpenAccountSave();
    if (R_SUCCEEDED(save.GetFsOpenResult())) {
        save_open = true;
        std::string baas_dir;
        if (save.DirExists("/su/baas")) {
            baas_dir = "/su/baas";
        } else if (save.DirExists("/baas")) {
            baas_dir = "/baas";
        }

        std::string nas_dir;
        if (save.DirExists("/su/nas")) {
            nas_dir = "/su/nas";
        } else if (save.DirExists("/nas")) {
            nas_dir = "/nas";
        }

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
    log_write("[ACC] ListUsers: save_open=%d official=%u offline=%u none=%u\n",
        save_open ? 1 : 0, count_official, count_offline, count_none);

    return out;
}

auto ExportAccountSave(std::string& out_dir) -> Result {
    auto save = TryOpenAccountSave();
    R_TRY(save.GetFsOpenResult());

    fs::FsNativeSd sd;
    char stamp[32]{};
    const auto t = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::localtime(&t));
    out_dir = paths::DATA_ROOT + "/account_export/" + stamp;

    bool any{};
    for (const char* dir : {"/baas", "/nas"}) {
        if (!save.DirExists(dir)) {
            continue;
        }
        const auto dst_dir = out_dir + dir;
        sd.CreateDirectoryRecursively(dst_dir.c_str());
        for (const auto& name : ListDirFiles(save, dir)) {
            R_TRY(CopyFile(save, std::string(dir) + "/" + name, sd, dst_dir + "/" + name));
            any = true;
        }
    }
    R_UNLESS(any, Result_FsEmpty);
    log_write("[ACC] export written to %s\n", out_dir.c_str());
    R_SUCCEED();
}

namespace {

auto HasSuspendedApplication() -> bool {
    if (R_FAILED(pmdmntInitialize())) {
        return true;
    }
    ON_SCOPE_EXIT(pmdmntExit());
    u64 application_pid = 0;
    const auto rc = pmdmntGetApplicationProcessId(&application_pid);
    if (R_FAILED(rc)) {
        return true;
    }
    return application_pid != 0;
}

bool g_launch_link_prompted = false;

} // namespace

auto CanOfferLaunchLink() -> bool {
    if (g_launch_link_prompted) {
        return false;
    }
    if (App::IsApplet() && HasSuspendedApplication()) {
        return false;
    }
    const auto users = ListUsers();
    for (const auto& u : users) {
        if (!u.linked_known) {
            continue;
        }
        if (u.kind == LinkKind::Offline || (u.kind == LinkKind::None && !u.horizon_linked)) {
            return true;
        }
    }
    return false;
}

auto IsLinkGated(std::string& out_reason) -> bool {
    if (App::IsApplet() && HasSuspendedApplication()) {
        out_reason = "Linking is unavailable in applet mode while a game is suspended. Please close the suspended game or launch Kefir Hub as an installed title."_i18n;
        return true;
    }
    return false;
}

void SetLaunchLinkPrompted(bool val) {
    g_launch_link_prompted = val;
}

namespace {

auto RunProbeSaveLock() -> Result {
    u32 save_before = 0;
    {
        auto save = TryOpenAccountSave();
        if (R_SUCCEEDED(save.GetFsOpenResult())) {
            save_before = 1;
        }
    }

    u32 suspend_ok = 0;
    Result suspend_rc = 0;
    u32 save_after = 0;

    Service accsu{};
    Result accsu_rc = OpenAccSu(&accsu);
    if (R_SUCCEEDED(accsu_rc)) {
        ON_SCOPE_EXIT(serviceClose(&accsu));
        Service daemon_session{};
        suspend_rc = serviceDispatch(&accsu, 299,
            .out_num_objects = 1,
            .out_objects = &daemon_session);
        if (R_SUCCEEDED(suspend_rc)) {
            suspend_ok = 1;
            ON_SCOPE_EXIT(serviceClose(&daemon_session));
            auto save = TryOpenAccountSave();
            if (R_SUCCEEDED(save.GetFsOpenResult())) {
                save_after = 1;
            }
        }
    } else {
        suspend_rc = accsu_rc;
    }

    log_write("[ACC] SuspendSaveProbe: suspend_ok=%u suspend_rc=0x%X save_before=%u save_after=%u\n",
        suspend_ok, suspend_rc, save_before, save_after);
    R_SUCCEED();
}

auto RunProbeIdTokenCache() -> Result {
    u32 linked_count = 0;
    u32 present_count = 0;
    u32 empty_count = 0;
    u32 failed_count = 0;

    Service accsu{};
    if (R_FAILED(OpenAccSu(&accsu))) {
        failed_count++;
    } else {
        ON_SCOPE_EXIT(serviceClose(&accsu));

        for (const auto& base : App::GetAccountList()) {
            Service manager{};
            Result rc = serviceDispatchIn(&accsu, 102, base.uid,
                .out_num_objects = 1,
                .out_objects = &manager);
            if (R_FAILED(rc)) {
                failed_count++;
                continue;
            }
            ON_SCOPE_EXIT(serviceClose(&manager));

            Result avail_rc = serviceDispatch(&manager, 0); // CheckAvailability
            if (avail_rc == ResultNetworkServiceAccountRegistrationRequired) {
                continue;
            }
            if (R_FAILED(avail_rc)) {
                failed_count++;
                continue;
            }

            linked_count++;
            u32 actual_size = 0;
            Result cache_rc = QueryIdTokenCacheRaw(&manager, actual_size);
            if (R_SUCCEEDED(cache_rc)) {
                if (actual_size > 0) {
                    present_count++;
                } else {
                    empty_count++;
                }
            } else {
                failed_count++;
            }
        }
    }

    log_write("[ACC] IdTokenCacheProbe: linked=%u present=%u empty=%u failed=%u\n",
        linked_count, present_count, empty_count, failed_count);
    R_SUCCEED();
}

auto RunProbeUserResource() -> Result {
    u32 linked_count = 0;
    u32 ok_count = 0;
    u32 base_nonempty_count = 0;
    u32 image_nonempty_count = 0;
    u32 failed_count = 0;

    Service accsu{};
    if (R_FAILED(OpenAccSu(&accsu))) {
        failed_count++;
    } else {
        ON_SCOPE_EXIT(serviceClose(&accsu));

        for (const auto& base : App::GetAccountList()) {
            Service manager{};
            Result rc = serviceDispatchIn(&accsu, 102, base.uid,
                .out_num_objects = 1,
                .out_objects = &manager);
            if (R_FAILED(rc)) {
                failed_count++;
                continue;
            }
            ON_SCOPE_EXIT(serviceClose(&manager));

            Result avail_rc = serviceDispatch(&manager, 0); // CheckAvailability
            if (avail_rc == ResultNetworkServiceAccountRegistrationRequired) {
                continue;
            }
            if (R_FAILED(avail_rc)) {
                failed_count++;
                continue;
            }

            linked_count++;

            u64 nas_id = 0;
            std::vector<u8> base_buf(0x24F, 0);
            std::vector<u8> image_buf(0x20000, 0);

            Result res_rc = serviceDispatchOut(&manager, 130, nas_id,
                .buffer_attrs = {
                    SfBufferAttr_HipcMapAlias | SfBufferAttr_Out | SfBufferAttr_FixedSize,
                    SfBufferAttr_HipcMapAlias | SfBufferAttr_Out,
                },
                .buffers = {
                    { base_buf.data(), base_buf.size() },
                    { image_buf.data(), image_buf.size() },
                });

            if (R_SUCCEEDED(res_rc)) {
                ok_count++;
                bool base_nonempty = false;
                for (u8 b : base_buf) {
                    if (b != 0) {
                        base_nonempty = true;
                        break;
                    }
                }
                bool image_nonempty = false;
                for (u8 b : image_buf) {
                    if (b != 0) {
                        image_nonempty = true;
                        break;
                    }
                }
                if (base_nonempty) {
                    base_nonempty_count++;
                }
                if (image_nonempty) {
                    image_nonempty_count++;
                }
            } else {
                failed_count++;
            }
        }
    }

    log_write("[ACC] UserResourceProbe: linked=%u ok=%u base_nonempty=%u image_nonempty=%u failed=%u\n",
        linked_count, ok_count, base_nonempty_count, image_nonempty_count, failed_count);
    R_SUCCEED();
}

auto RunProbeTokenUpdate() -> Result {
    // IManagerForSystemService cmd 160 RequiresUpdateNetworkServiceAccountIdTokenCache
    // exists on HOS 15.0.0+, but its IPC parameter / buffer ABI is not formally
    // confirmed in available project and platform headers. To ensure safety without
    // speculative IPC layout guesses, we do not invoke cmd 160 and report unavailable.
    log_write("[ACC] TokenUpdateProbe: supported=0 linked=0 update=0 current=0 failed=0 unavailable=1\n");
    R_SUCCEED();
}

auto RunProbeAdminState() -> Result {
    u32 linked_count = 0;
    u32 registered_true = 0;
    u32 registered_false = 0;
    u32 link_true = 0;
    u32 link_false = 0;
    u32 failed_count = 0;

    Service accsu{};
    if (R_FAILED(OpenAccSu(&accsu))) {
        failed_count++;
    } else {
        ON_SCOPE_EXIT(serviceClose(&accsu));

        for (const auto& base : App::GetAccountList()) {
            Service manager{};
            Result rc = serviceDispatchIn(&accsu, 102, base.uid,
                .out_num_objects = 1,
                .out_objects = &manager);
            if (R_FAILED(rc)) {
                failed_count++;
                continue;
            }
            ON_SCOPE_EXIT(serviceClose(&manager));

            Result avail_rc = serviceDispatch(&manager, 0); // CheckAvailability
            if (avail_rc == ResultNetworkServiceAccountRegistrationRequired) {
                continue;
            }
            if (R_FAILED(avail_rc)) {
                failed_count++;
                continue;
            }

            linked_count++;

            Service administrator{};
            Result admin_rc = serviceDispatchIn(&accsu, 250, base.uid,
                .out_num_objects = 1,
                .out_objects = &administrator);
            if (R_FAILED(admin_rc)) {
                failed_count++;
                continue;
            }
            ON_SCOPE_EXIT(serviceClose(&administrator));

            u8 is_registered = 0;
            Result reg_rc = serviceDispatchOut(&administrator, 200, is_registered);

            u8 is_linked = 0;
            Result link_rc = serviceDispatchOut(&administrator, 250, is_linked);

            if (R_SUCCEEDED(reg_rc) && R_SUCCEEDED(link_rc)) {
                if (is_registered != 0) {
                    registered_true++;
                } else {
                    registered_false++;
                }
                if (is_linked != 0) {
                    link_true++;
                } else {
                    link_false++;
                }
            } else {
                failed_count++;
            }
        }
    }

    log_write("[ACC] AdminStateProbe: linked=%u registered_true=%u registered_false=%u link_true=%u link_false=%u failed=%u\n",
        linked_count, registered_true, registered_false, link_true, link_false, failed_count);
    R_SUCCEED();
}

} // namespace

auto RunDiagnostic(DiagnosticKind kind) -> Result {
    switch (kind) {
        case DiagnosticKind::SaveLock:
            return RunProbeSaveLock();
        case DiagnosticKind::IdTokenCache:
            return RunProbeIdTokenCache();
        case DiagnosticKind::UserResource:
            return RunProbeUserResource();
        case DiagnosticKind::TokenUpdate:
            return RunProbeTokenUpdate();
        case DiagnosticKind::AdminState:
            return RunProbeAdminState();
    }
    R_SUCCEED();
}

} // namespace sphaira::account_link
