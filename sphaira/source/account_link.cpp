#include "account_link.hpp"
#include "account_restore.hpp"
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

bool g_daemons_terminated = false;

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
    return fs::FsNativeSave(FsSaveDataType_System, FsSaveDataSpaceId_System, &attr, true);
}

auto OpenAccountSaveWritable() -> fs::FsNativeSave {
    FsSaveDataAttribute attr{};
    attr.system_save_data_id = ACCOUNT_SAVE_ID;
    attr.save_data_type = FsSaveDataType_System;
    return fs::FsNativeSave(FsSaveDataType_System, FsSaveDataSpaceId_System, &attr, false);
}

void TerminateAccountDaemons() {
    if (R_SUCCEEDED(pmshellInitialize())) {
        ON_SCOPE_EXIT(pmshellExit());
        // Do not kill ns (0015/001F) or friends (000E): from the Hub applet that
        // User-Breaks am (0100000000000023) and can bootloop after ApplyLink.
        pmshellTerminateProgram(0x010000000000000CULL); // BCAT
        pmshellTerminateProgram(0x010000000000001EULL); // ACCOUNT
        pmshellTerminateProgram(0x010000000000003EULL); // OLSC
        g_daemons_terminated = true;
        log_write("[ACC] terminated BCAT/ACCOUNT/OLSC (reboot needed; ns/friends kept)\n");
    }
}

auto OpenAccountSaveForExport() -> fs::FsNativeSave {
    auto save = TryOpenAccountSave();
    if (R_SUCCEEDED(save.GetFsOpenResult())) {
        return save;
    }
    log_write("[ACC] account save read-only failed 0x%X, terminating daemons\n", save.GetFsOpenResult());
    TerminateAccountDaemons();
    return OpenAccountSaveWritable();
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

auto OpenAccSu(Service* out) -> Result {
    R_TRY(smGetService(out, "acc:su"));
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

auto ResolveSuDirs(fs::Fs& save, std::string& baas_dir, std::string& nas_dir) -> void {
    baas_dir.clear();
    nas_dir.clear();
    if (save.DirExists("/su/baas")) {
        baas_dir = "/su/baas";
    } else if (save.DirExists("/baas")) {
        baas_dir = "/baas";
    }
    if (save.DirExists("/su/nas")) {
        nas_dir = "/su/nas";
    } else if (save.DirExists("/nas")) {
        nas_dir = "/nas";
    }
}

auto BaasStillReferencesNas(fs::Fs& save, const std::string& baas_dir, u64 nas_id) -> bool {
    if (baas_dir.empty() || nas_id == 0) {
        return false;
    }
    for (const auto& bf : ListDirFiles(save, baas_dir)) {
        std::vector<u8> data;
        if (R_FAILED(save.read_entire_file((baas_dir + "/" + bf).c_str(), data)) || data.size() < 24) {
            continue;
        }
        u64 id = 0;
        std::memcpy(&id, data.data() + 16, sizeof(u64));
        if (id == nas_id) {
            return true;
        }
    }
    return false;
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

auto LoadUserPackLinkPackage(const std::string& pack_dir, LinkPackage& out_pkg) -> Result {
    out_pkg = {};
    fs::FsNativeSd sd;

    const auto baas_dir = pack_dir + "/baas";
    const auto nas_dir = pack_dir + "/nas";
    if (!sd.DirExists(baas_dir.c_str()) || !sd.DirExists(nas_dir.c_str())) {
        return Result_FsInvalidType;
    }

    fs::Dir bd;
    R_TRY(sd.OpenDirectory(baas_dir.c_str(), FsDirOpenMode_ReadFiles, &bd));
    std::vector<FsDirectoryEntry> baas_entries;
    R_TRY(bd.ReadAll(baas_entries));

    std::vector<std::string> baas_files;
    for (const auto& e : baas_entries) {
        if (e.type == FsDirEntryType_File && IsSafeDumpFileName(e.name)) {
            baas_files.push_back(e.name);
        }
    }
    if (baas_files.empty()) {
        return Result_FsInvalidType;
    }

    std::vector<u8> baas_data;
    std::string baas_fname;
    for (const auto& name : baas_files) {
        std::vector<u8> candidate;
        if (R_FAILED(sd.read_entire_file((baas_dir + "/" + name).c_str(), candidate)) || candidate.size() < 24) {
            continue;
        }
        u64 cand_nas = 0;
        std::memcpy(&cand_nas, candidate.data() + 16, sizeof(u64));
        if (cand_nas == 0) {
            continue;
        }
        baas_fname = name;
        baas_data = std::move(candidate);
        break;
    }
    if (baas_data.size() < 24) {
        return Result_FsInvalidType;
    }

    u64 nas_id = 0;
    std::memcpy(&nas_id, baas_data.data() + 16, sizeof(u64));
    if (nas_id == 0) {
        return Result_FsInvalidType;
    }

    fs::Dir nd;
    R_TRY(sd.OpenDirectory(nas_dir.c_str(), FsDirOpenMode_ReadFiles, &nd));
    std::vector<FsDirectoryEntry> nas_entries;
    R_TRY(nd.ReadAll(nas_entries));

    bool has_id_token = false;
    bool has_refresh_token = false;
    std::vector<DonorNasFile> loaded_nas_files;

    for (const auto& e : nas_entries) {
        if (e.type != FsDirEntryType_File) {
            continue;
        }
        const std::string fname = e.name;
        if (!IsSafeDumpFileName(fname) || !NasFileMatches(fname, nas_id)) {
            continue;
        }
        std::vector<u8> fdata;
        if (R_FAILED(sd.read_entire_file((nas_dir + "/" + fname).c_str(), fdata)) || fdata.empty()) {
            continue;
        }
        const auto lower = ToLowerCopy(fname);
        if (EndsWith(lower, "_id.token")) {
            has_id_token = true;
        } else if (EndsWith(lower, "_refresh.token")) {
            has_refresh_token = true;
        }
        loaded_nas_files.push_back({fname, std::move(fdata)});
    }

    if (!has_id_token || !has_refresh_token || loaded_nas_files.empty()) {
        return Result_FsInvalidType;
    }

    out_pkg.nas_id = nas_id;
    out_pkg.baas_data = std::move(baas_data);
    out_pkg.nas_files = std::move(loaded_nas_files);
    R_SUCCEED();
}

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

    TerminateAccountDaemons();

    auto save = OpenAccountSaveWritable();
    const auto save_rc = save.GetFsOpenResult();
    if (R_FAILED(save_rc)) {
        log_write("[ACC] OpenAccountSaveWritable failed 0x%X\n", save_rc);
        return save_rc;
    }
    log_write("[ACC] OpenAccountSaveWritable ok\n");

    fs::FsNativeSd sd;
    char stamp[32]{};
    const auto t = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::localtime(&t));
    const auto rollback_dir = std::string("/config/kefir/account_link_rollback/") + stamp;
    R_TRY(sd.CreateDirectoryRecursively(rollback_dir.c_str()));
    R_TRY(sd.CreateDirectoryRecursively((rollback_dir + "/baas").c_str()));
    R_TRY(sd.CreateDirectoryRecursively((rollback_dir + "/nas").c_str()));

    std::string baas_dir;
    std::string nas_dir;
    ResolveSuDirs(save, baas_dir, nas_dir);
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
    std::vector<u64> incoming_nas;
    for (const auto& target : targets) {
        if (std::find(incoming_nas.begin(), incoming_nas.end(), target.pkg.nas_id) != incoming_nas.end()) {
            log_write("[ACC] ApplyLinkPackages refused: two targets share nas %llx\n",
                static_cast<unsigned long long>(target.pkg.nas_id));
            return Result_FsInvalidType;
        }
        incoming_nas.push_back(target.pkg.nas_id);
    }

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
        log_write("[ACC] removing baas %s\n", bf.c_str());
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
            if (!drop) {
                std::vector<u8> data;
                if (R_SUCCEEDED(save.read_entire_file((baas_dir + "/" + bf).c_str(), data)) && data.size() >= 24) {
                    u64 id = 0;
                    std::memcpy(&id, data.data() + 16, sizeof(u64));
                    if (id == target.pkg.nas_id) {
                        drop = true;
                    }
                }
            }
            if (drop) {
                R_TRY(delete_baas(bf));
            }
        }

        const auto new_baas_path = baas_dir + "/" + UidDashedLinkalho(target.uid) + ".dat";
        auto baas_for_uid = target.pkg.baas_data;
        std::memcpy(baas_for_uid.data(), &target.uid, sizeof(AccountUid));
        R_TRY(save.write_entire_file(new_baas_path.c_str(), baas_for_uid));
        log_write("[ACC] baas bound to %s nas %llx (one file)\n",
            UidDashedRfc(target.uid).c_str(),
            static_cast<unsigned long long>(target.pkg.nas_id));

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
        log_write("[ACC] Commit failed 0x%X\n", commit_rc);
        return commit_rc;
    }
    log_write("[ACC] Commit ok\n");

    out_linked_count = static_cast<u32>(targets.size());
    log_write("[ACC] ApplyLinkPackages completed for %u target(s)\n", out_linked_count);
    R_SUCCEED();
}

auto LinkAllFromRomfsDonor(u32& out_linked_count) -> Result {
    out_linked_count = 0;

    RomfsDonorPackage donor_pkg;
    const auto load_rc = LoadRomfsDonorPackage(donor_pkg);
    if (R_FAILED(load_rc)) {
        log_write("[ACC] LoadRomfsDonorPackage failed 0x%X\n", load_rc);
        return load_rc;
    }
    log_write("[ACC] LoadRomfsDonorPackage ok\n");

    const auto all_users = ListUsers();
    std::vector<TargetLink> targets;
    for (const auto& u : all_users) {
        if (u.linked_known && !u.horizon_linked) {
            targets.push_back({u.uid, donor_pkg});
        }
    }
    log_write("[ACC] LinkAllFromRomfsDonor eligible unlinked=%u\n", static_cast<u32>(targets.size()));

    if (targets.empty()) {
        R_SUCCEED();
    }

    return ApplyLinkPackages(targets, out_linked_count);
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

auto UidHex(const AccountUid& uid) -> std::string {
    return UidDashedRfc(uid);
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

auto FindLiveUidByNasId(u64 nas_id, AccountUid& out_uid) -> bool {
    out_uid = {};
    if (nas_id == 0) {
        return false;
    }

    const auto live = App::GetAccountList();

    for (const auto& base : live) {
        u64 ipc_nas = 0;
        const auto qrc = QueryNintendoAccountId(base.uid, ipc_nas);
        log_write("[ACC] QueryNintendoAccountId uid=%s rc=0x%X nas=%llx\n",
            UidDashedRfc(base.uid).c_str(), qrc,
            static_cast<unsigned long long>(ipc_nas));

        // Replace only when pack nas is proven equal (IPC nas == pack nas).
        if (R_SUCCEEDED(qrc) && ipc_nas != 0 && ipc_nas == nas_id) {
            out_uid = base.uid;
            log_write("[ACC] nas %llx is IPC-linked to %s\n",
                static_cast<unsigned long long>(nas_id), UidDashedRfc(base.uid).c_str());
            return true;
        }

        bool horizon_linked = false;
        const auto hrc = QueryHorizonLinkStatus(base.uid, horizon_linked);
        if (R_SUCCEEDED(hrc) && horizon_linked) {
            if (R_SUCCEEDED(qrc) && ipc_nas != 0 && ipc_nas != nas_id) {
                log_write("[ACC] linked uid %s has different nas %llx (pack %llx); Create still allowed\n",
                    UidDashedRfc(base.uid).c_str(),
                    static_cast<unsigned long long>(ipc_nas),
                    static_cast<unsigned long long>(nas_id));
            } else if (R_FAILED(qrc) || ipc_nas == 0) {
                // Unproven Query is not Replace: need baas proof or Create.
                log_write("[ACC] linked uid %s nas unproven (Query rc=0x%X); Create allowed until baas proves match\n",
                    UidDashedRfc(base.uid).c_str(), qrc);
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

    auto uid_is_live = [&](const AccountUid& uid) -> bool {
        for (const auto& base : live) {
            if (base.uid.uid[0] == uid.uid[0] && base.uid.uid[1] == uid.uid[1]) {
                return true;
            }
        }
        return false;
    };

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
        AccountUid file_uid{};
        std::memcpy(&file_uid, data.data(), sizeof(AccountUid));
        if (uid_is_live(file_uid)) {
            out_uid = file_uid;
            log_write("[ACC] nas %llx is in baas %s for live %s\n",
                static_cast<unsigned long long>(nas_id), bf.c_str(), UidDashedRfc(file_uid).c_str());
            return true;
        }
        for (const auto& base : live) {
            const auto cands = BaasCandidateNames(base.uid);
            for (const auto& cand : cands) {
                if (strcasecmp(bf.c_str(), cand.c_str()) == 0) {
                    out_uid = base.uid;
                    log_write("[ACC] nas %llx baas filename %s matches live %s\n",
                        static_cast<unsigned long long>(nas_id), bf.c_str(),
                        UidDashedRfc(base.uid).c_str());
                    return true;
                }
            }
        }
        log_write("[ACC] nas %llx baas %s is orphan (uid not live)\n",
            static_cast<unsigned long long>(nas_id), bf.c_str());
    }

    // Save open and no baas carries this nas → Create allowed.
    log_write("[ACC] nas %llx not in baas; Create allowed\n",
        static_cast<unsigned long long>(nas_id));
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

auto ExportUserLinkPackage(const AccountUid& uid, const std::string& out_dir, std::string& out_link_status) -> Result
{
    out_link_status = "none";
    u64 nas_id = 0;
    QueryNintendoAccountId(uid, nas_id);

    auto save = OpenAccountSaveForExport();
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

namespace {

// Same source EdiZon / dmnt use: pm:dmnt GetApplicationProcessId.
// Success + non-zero PID => an application (usually the suspended game under album) exists.
// ProcessNotFound / failed query => no application — do NOT treat as suspended.
auto QueryBackgroundApplication(u64& out_pid, u64& out_program_id) -> bool {
    out_pid = 0;
    out_program_id = 0;

    if (R_FAILED(pmdmntInitialize())) {
        log_write("[ACC] pmdmntInitialize failed\n");
        return false;
    }
    ON_SCOPE_EXIT(pmdmntExit());

    const auto rc = pmdmntGetApplicationProcessId(&out_pid);
    if (R_FAILED(rc) || out_pid == 0) {
        log_write("[ACC] background application: none (rc=0x%X pid=%llu)\n",
            rc, static_cast<unsigned long long>(out_pid));
        out_pid = 0;
        return false;
    }

    if (R_SUCCEEDED(pmdmntGetProgramId(&out_program_id, out_pid))) {
        log_write("[ACC] background application: pid=%llu program=%016llX\n",
            static_cast<unsigned long long>(out_pid),
            static_cast<unsigned long long>(out_program_id));
    } else {
        log_write("[ACC] background application: pid=%llu program=unknown\n",
            static_cast<unsigned long long>(out_pid));
    }
    return true;
}

auto HasSuspendedApplication() -> bool {
    u64 pid = 0;
    u64 program_id = 0;
    return QueryBackgroundApplication(pid, program_id);
}

bool g_launch_link_prompted = false;

} // namespace

auto ConsumeAccountDaemonsTerminated() -> bool {
    const bool v = g_daemons_terminated;
    g_daemons_terminated = false;
    return v;
}

auto CanOfferLaunchLink() -> bool {
    if (g_launch_link_prompted) {
        log_write("[ACC] CanOfferLaunchLink: already prompted this session\n");
        return false;
    }
    if (account_restore::HasUnfinishedRestore()) {
        log_write("[ACC] CanOfferLaunchLink: unfinished restore takes priority\n");
        return false;
    }
    if (App::GetAccountLinkPromptSkip()) {
        log_write("[ACC] CanOfferLaunchLink: skipped by config\n");
        return false;
    }
    // Album/applet with a suspended game: skip offer (link needs reboot; gated).
    if (App::IsApplet()) {
        u64 pid = 0;
        u64 program_id = 0;
        if (QueryBackgroundApplication(pid, program_id)) {
            log_write("[ACC] CanOfferLaunchLink: gated applet+suspended program=%016llX\n",
                static_cast<unsigned long long>(program_id));
            return false;
        }
    }
    const auto users = ListUsers();
    u32 unlinked = 0;
    u32 unknown = 0;
    for (const auto& u : users) {
        if (!u.linked_known) {
            unknown++;
            continue;
        }
        if (!u.horizon_linked) {
            unlinked++;
        }
    }
    log_write("[ACC] CanOfferLaunchLink: users=%u unlinked=%u unknown=%u offer=%d\n",
        static_cast<u32>(users.size()), unlinked, unknown, unlinked > 0 ? 1 : 0);
    return unlinked > 0;
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

} // namespace sphaira::account_link
