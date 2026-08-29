#include "account_link.hpp"
#include "app.hpp"
#include "app_paths.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "utils/utils.hpp"

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
    const auto lower = ToLowerCopy(name);
    for (const auto& prefix : NasPrefixes(nas_id)) {
        if (lower.rfind(ToLowerCopy(prefix), 0) == 0) {
            return true;
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

auto CheckHandoffPreconditions(fs::FsNativeSd& sd) -> Result {
    if (sd.FileExists("/startup.te") || sd.FileExists("/payload.bak")) {
        return FsError_PathAlreadyExists;
    }
    if (!sd.FileExists("/bootloader/payloads/TegraExplorer.bin") ||
        !sd.FileExists("/bootloader/update.bin")) {
        return FsError_PathNotFound;
    }
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

auto UidHex(const AccountUid& uid) -> std::string {
    return UidDashedRfc(uid);
}

auto QueryHorizonLinkStatus(const AccountUid& uid, bool& out_linked) -> Result {
    out_linked = false;
    Service accsu{};
    R_TRY(OpenAccSu(&accsu));
    ON_SCOPE_EXIT(serviceClose(&accsu));

    Service manager{};
    R_TRY(serviceDispatchIn(&accsu, 102, uid,
        .out_num_objects = 1,
        .out_objects = &manager));
    ON_SCOPE_EXIT(serviceClose(&manager));

    const auto rc = serviceDispatch(&manager, 0); // CheckAvailability
    if (R_SUCCEEDED(rc)) {
        out_linked = true;
        R_SUCCEED();
    }
    if (rc == ResultNetworkServiceAccountRegistrationRequired) {
        out_linked = false;
        R_SUCCEED();
    }
    return rc;
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
        const auto rc = QueryHorizonLinkStatus(u.uid, linked);
        if (R_SUCCEEDED(rc)) {
            u.linked_known = true;
            u.horizon_linked = linked;
            u.kind = linked ? LinkKind::Offline : LinkKind::None;
        } else {
            u.linked_known = false;
            u.horizon_linked = false;
            u.kind = LinkKind::None;
            log_write("[ACC] Horizon link check failed 0x%X\n", rc);
        }
    }

    auto save = TryOpenAccountSave();
    if (R_SUCCEEDED(save.GetFsOpenResult())) {
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

            if (nas_id == 0) {
                QueryNintendoAccountId(u.uid, nas_id);
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

auto ValidateLinkPackage(const std::string& pkg_dir, u64& out_nas_id, std::vector<std::string>& out_nas_files) -> Result {
    out_nas_id = 0;
    out_nas_files.clear();
    R_UNLESS(!pkg_dir.empty(), FsError_PathNotFound);

    auto norm_path = pkg_dir;
    while (!norm_path.empty() && (norm_path.back() == '/' || norm_path.back() == '\\')) {
        norm_path.pop_back();
    }
    R_UNLESS(!norm_path.empty(), FsError_PathNotFound);

    if (norm_path.find("..") != std::string::npos || norm_path.find('\\') != std::string::npos || norm_path.find("//") != std::string::npos) {
        return FsError_PathNotFound;
    }

    const auto pkg_name = BaseName(norm_path);
    R_UNLESS(!pkg_name.empty(), FsError_PathNotFound);
    for (char c : pkg_name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_' && c != '-') {
            return FsError_PathNotFound;
        }
    }

    const auto allowed_root_app = paths::DATA_ROOT + "/account_links";
    const auto allowed_root_kefir = std::string("/config/kefir/account_links");
    const auto expected_app = allowed_root_app + "/" + pkg_name;
    const auto expected_kefir = allowed_root_kefir + "/" + pkg_name;

    if (norm_path != expected_app && norm_path != expected_kefir) {
        return FsError_PathNotFound;
    }

    fs::FsNativeSd sd;
    const auto manifest_path = norm_path + "/manifest.txt";
    R_UNLESS(sd.FileExists(manifest_path.c_str()), Result_FsInvalidType);

    std::vector<u8> manifest_bytes;
    R_TRY(sd.read_entire_file(manifest_path.c_str(), manifest_bytes));
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
    R_UNLESS(kv["version"] == "2", Result_FsInvalidType);
    R_UNLESS(kv["system_save"] == "8000000000000010", Result_FsInvalidType);
    R_UNLESS(kv["idgen_0011_included"] == "false", Result_FsInvalidType);
    R_UNLESS(kv["baas_file"] == "baas/link.dat", Result_FsInvalidType);

    const auto& nas_hex = kv["nintendo_account_id"];
    R_UNLESS(!nas_hex.empty() && nas_hex.size() <= 16, Result_FsInvalidType);
    for (char c : nas_hex) {
        R_UNLESS(std::isxdigit(static_cast<unsigned char>(c)), Result_FsInvalidType);
    }

    char* end = nullptr;
    out_nas_id = std::strtoull(nas_hex.c_str(), &end, 16);
    R_UNLESS(end && *end == '\0' && out_nas_id != 0, Result_FsInvalidType);

    const auto baas_path = norm_path + "/baas/link.dat";
    R_UNLESS(sd.FileExists(baas_path.c_str()), Result_FsInvalidType);

    std::vector<u8> baas_sample;
    R_TRY(sd.read_entire_file(baas_path.c_str(), baas_sample));
    R_UNLESS(baas_sample.size() >= 24, Result_FsInvalidType);

    u64 baas_nas_id = 0;
    std::memcpy(&baas_nas_id, baas_sample.data() + 16, sizeof(u64));
    R_UNLESS(baas_nas_id == out_nas_id, Result_FsInvalidType);

    const auto nas_dir = norm_path + "/nas";
    R_UNLESS(sd.DirExists(nas_dir.c_str()), Result_FsInvalidType);

    fs::Dir d;
    R_TRY(sd.OpenDirectory(nas_dir.c_str(), FsDirOpenMode_ReadFiles, &d));
    std::vector<FsDirectoryEntry> entries;
    R_TRY(d.ReadAll(entries));

    for (const auto& e : entries) {
        if (e.type != FsDirEntryType_File) {
            continue;
        }
        std::string fname = e.name;
        bool safe = !fname.empty();
        for (char c : fname) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_' && c != '-') {
                safe = false;
                break;
            }
        }
        if (!safe) {
            continue;
        }
        if (NasFileMatches(fname, out_nas_id)) {
            out_nas_files.push_back(fname);
        }
    }

    R_UNLESS(!out_nas_files.empty(), Result_FsInvalidType);
    R_SUCCEED();
}

auto PrepareOfficialLinkExport(const AccountUid& uid, std::string& out_pkg_dir) -> Result {
    fs::FsNativeSd sd;
    R_TRY(CheckHandoffPreconditions(sd));

    u64 nas_id = 0;
    R_TRY(QueryNintendoAccountId(uid, nas_id));

    const bool is_emummc = App::IsEmummc();

    char stamp[32]{};
    const auto t = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::localtime(&t));
    char uid_suffix[9]{};
    std::snprintf(uid_suffix, sizeof(uid_suffix), "%08x", static_cast<unsigned>(uid.uid[0] & 0xffffffffu));
    const auto base_dir = paths::DATA_ROOT + "/account_links/" + stamp + "_" + uid_suffix;
    out_pkg_dir = base_dir;
    for (u32 collision = 1; sd.DirExists(out_pkg_dir.c_str()); collision++) {
        out_pkg_dir = base_dir + "_" + std::to_string(collision);
    }

    R_TRY(sd.CreateDirectoryRecursively(out_pkg_dir.c_str()));
    R_TRY(sd.CreateDirectoryRecursively((out_pkg_dir + "/baas").c_str()));
    R_TRY(sd.CreateDirectoryRecursively((out_pkg_dir + "/nas").c_str()));

    const auto source_uid_str = UidHex(uid);
    const auto nas_hex_str = NasHex(nas_id);
    const auto nand_str = std::string(is_emummc ? "emummc" : "sysmmc");

    std::string manifest;
    manifest += "format=kefir_account_link\n";
    manifest += "version=2\n";
    manifest += "system_save=8000000000000010\n";
    manifest += "idgen_0011_included=false\n";
    manifest += "source_nand=" + nand_str + "\n";
    manifest += "source_uid=" + source_uid_str + "\n";
    manifest += "nintendo_account_id=" + nas_hex_str + "\n";
    manifest += "baas_file=baas/link.dat\n";
    R_TRY(sd.write_entire_file((out_pkg_dir + "/manifest.txt").c_str(),
        std::vector<u8>(manifest.begin(), manifest.end())));

    const std::string readme =
        "Kefir Hub official Nintendo Account link export\n"
        "Format version: 2\n"
        "\n"
        "This bundle contains a local official Nintendo Account link export prepared for the selected user profile.\n"
        "System save 0x8000000000000010 data will be extracted offline via TegraExplorer.\n"
        "\n"
        "idgen_0011_included=false\n"
        "System save 0x8000000000000011 (idgen:/context.bin) is intentionally omitted because it contains\n"
        "console-specific UID generator state, not an individual user's Nintendo Account linkage.\n"
        "\n"
        "SECURITY WARNING:\n"
        "Files under baas/ and nas/ may contain private Nintendo Account identifiers or cached credentials.\n"
        "Do NOT share these files, publish them, or embed them in a distributable application or NRO.\n";
    R_TRY(sd.write_entire_file((out_pkg_dir + "/README.txt").c_str(),
        std::vector<u8>(readme.begin(), readme.end())));

    const auto pkg_name = BaseName(out_pkg_dir);
    for (char c : pkg_name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_' && c != '-') {
            return FsError_PathNotFound;
        }
    }

    const auto prefixes = NasPrefixes(nas_id);

    std::string te;
    te += "# REQUIRE SD\n";
    te += "# REQUIRE KEYS\n";
    te += "# REQUIRE MINERVA\n";
    te += "# REQUIRE VER 4.0.0\n\n";

    te += "cleanup = {\n";
    te += "    if (fsexists(\"sd:/payload.bak\")) {\n";
    te += "        writefile(\"sd:/payload.bin\", readfile(\"sd:/payload.bak\"))\n";
    te += "        delfile(\"sd:/payload.bak\")\n";
    te += "    }\n";
    te += "    if (fsexists(\"sd:/startup.te\")) {\n";
    te += "        delfile(\"sd:/startup.te\")\n";
    te += "    }\n";
    te += "    if (fsexists(\"sd:/bootloader/update.bin\")) {\n";
    te += "        payload(\"sd:/bootloader/update.bin\")\n";
    te += "    }\n";
    te += "}\n\n";

    te += "clear()\n";
    te += "println(\"Kefir Hub: export official account link\")\n";
    if (is_emummc) {
        te += "println(\"Source SYSTEM: emuMMC\")\n\n";
    } else {
        te += "println(\"Source SYSTEM: sysMMC\")\n\n";
    }

    te += "pkg = \"sd:/config/kefir/account_links/" + pkg_name + "\"\n";
    te += "mkdir(pkg)\n";
    te += "mkdir(combinepath(pkg, \"baas\"))\n";
    te += "mkdir(combinepath(pkg, \"nas\"))\n";
    te += "writefile(combinepath(pkg, \"result.txt\"), (\"operation=export\\nsource_uid=" + source_uid_str + "\\nsource_nand=" + nand_str + "\\nstage=starting\\n\").bytes())\n\n";

    if (is_emummc) {
        te += "rc = mountemu(\"SYSTEM\")\n";
    } else {
        te += "rc = mountsys(\"SYSTEM\")\n";
    }

    te += "if (rc) {\n";
    te += "    println(\"SYSTEM mount failed\", rc)\n";
    te += "    writefile(combinepath(pkg, \"result.txt\"), (\"Export failed: SYSTEM mount failed (\" + rc.str() + \")\\nstage=mount_system\\n\").bytes())\n";
    te += "    pause()\n";
    te += "    cleanup()\n";
    te += "    exit()\n";
    te += "}\n\n";

    te += "bis = \"bis:/save/8000000000000010\"\n";
    te += "if (!fsexists(bis)) {\n";
    te += "    println(\"Save 8000000000000010 not found on SYSTEM\")\n";
    te += "    writefile(combinepath(pkg, \"result.txt\"), (\"Export failed: Save 8000000000000010 not found on SYSTEM\\nstage=check_save\\n\").bytes())\n";
    te += "    pause()\n";
    te += "    cleanup()\n";
    te += "    exit()\n";
    te += "}\n\n";

    te += "saveObj = readsave(bis)\n\n";

    te += "baasListing = saveObj.readdir(\"/su/baas\")\n";
    te += "if (baasListing.result) {\n";
    te += "    println(\"Error: cannot read /su/baas in save 0010\", baasListing.result)\n";
    te += "    writefile(combinepath(pkg, \"result.txt\"), (\"Export failed: cannot read /su/baas in save 0010 (\" + baasListing.result.str() + \")\\nstage=read_baas\\n\").bytes())\n";
    te += "    pause()\n";
    te += "    cleanup()\n";
    te += "    exit()\n";
    te += "}\n\n";

    te += "baasMatchCount = 0\n";
    te += "selectedIndex = 0\n";
    te += "curIndex = 0\n";
    te += "baasListing.files.foreach(\"bfile\") {\n";
    te += "    bbytes = saveObj.read(\"/su/baas/\" + bfile)\n";
    te += "    if (bbytes.len() >= 24) {\n";
    te += "        match = 1\n";
    for (int i = 0; i < 8; i++) {
        const auto byte_val = static_cast<unsigned>((nas_id >> (8 * i)) & 0xffu);
        te += "        if (!(bbytes[" + std::to_string(16 + i) + "] == " + std::to_string(byte_val) + ")) {\n";
        te += "            match = 0\n";
        te += "        }\n";
    }
    te += "        if (match) {\n";
    te += "            baasMatchCount = baasMatchCount + 1\n";
    te += "            selectedIndex = curIndex\n";
    te += "        }\n";
    te += "    }\n";
    te += "    curIndex = curIndex + 1\n";
    te += "}\n\n";

    te += "if (!(baasMatchCount == 1)) {\n";
    te += "    println(\"Error: source baas file not found in 0010\")\n";
    te += "    writefile(combinepath(pkg, \"result.txt\"), (\"Export failed: source baas file not found in 0010\\nstage=find_baas\\n\").bytes())\n";
    te += "    pause()\n";
    te += "    cleanup()\n";
    te += "    exit()\n";
    te += "}\n\n";

    te += "selectedBaas = baasListing.files[selectedIndex]\n";
    te += "bbytes = saveObj.read(\"/su/baas/\" + selectedBaas)\n";
    te += "if (!(bbytes.len() >= 24)) {\n";
    te += "    println(\"Error: source baas file not found in 0010\")\n";
    te += "    writefile(combinepath(pkg, \"result.txt\"), (\"Export failed: source baas file not found in 0010\\nstage=find_baas\\n\").bytes())\n";
    te += "    pause()\n";
    te += "    cleanup()\n";
    te += "    exit()\n";
    te += "}\n\n";
    te += "writefile(combinepath(pkg, \"baas/link.dat\"), bbytes)\n\n";

    te += "nasListing = saveObj.readdir(\"/su/nas\")\n";
    te += "if (nasListing.result) {\n";
    te += "    println(\"Error: cannot read /su/nas in save 0010\", nasListing.result)\n";
    te += "    writefile(combinepath(pkg, \"result.txt\"), (\"Export failed: cannot read /su/nas in save 0010 (\" + nasListing.result.str() + \")\\nstage=read_nas\\n\").bytes())\n";
    te += "    pause()\n";
    te += "    cleanup()\n";
    te += "    exit()\n";
    te += "}\n\n";

    te += "nasCopied = 0\n";
    te += "nasListing.files.foreach(\"nfile\") {\n";
    te += "    match = 0\n";
    for (const auto& pfx : prefixes) {
        const auto len_str = std::to_string(pfx.length());
        te += "    if (!match && (nfile.len() >= " + len_str + ")) {\n";
        te += "        namePrefix = nfile - (nfile.len() - " + len_str + ")\n";
        te += "        if (namePrefix == \"" + pfx + "\") {\n";
        te += "            match = 1\n";
        te += "        }\n";
        te += "    }\n";
    }
    te += "    if (match) {\n";
    te += "        ndata = saveObj.read(\"/su/nas/\" + nfile)\n";
    te += "        writefile(combinepath(pkg, \"nas/\" + nfile), ndata)\n";
    te += "        nasCopied = nasCopied + 1\n";
    te += "    }\n";
    te += "}\n\n";

    te += "if (!nasCopied) {\n";
    te += "    println(\"Error: no matching NAS files found in 0010\")\n";
    te += "    writefile(combinepath(pkg, \"result.txt\"), (\"Export failed: no matching NAS files found in 0010\\nstage=find_nas\\n\").bytes())\n";
    te += "    pause()\n";
    te += "    cleanup()\n";
    te += "    exit()\n";
    te += "}\n\n";

    te += R"(resReport = "Export completed successfully\nsource_uid=" + ")" + source_uid_str + R"(\nsource_nand=)" + nand_str + R"(\nbaas=baas/link.dat\nnas_copied=" + nasCopied.str() + "\n")" "\n";
    te += "writefile(combinepath(pkg, \"result.txt\"), resReport.bytes())\n\n";

    te += "println(\"Export completed successfully.\")\n";
    te += "println(\"Exported baas/link.dat and\", nasCopied, \"NAS file(s).\")\n";
    te += "println(\"Press any button to return.\")\n";
    te += "pause()\n";
    te += "cleanup()\n";

    R_TRY(sd.write_entire_file("/startup.te", std::vector<u8>(te.begin(), te.end())));
    fsdevCommitDevice("sdmc");

    if (!utils::rebootToPayload("/bootloader/payloads/TegraExplorer.bin")) {
        sd.DeleteFile("/startup.te");
        fsdevCommitDevice("sdmc");
        return FsError_PathNotFound;
    }

    R_SUCCEED();
}

auto PrepareOfficialLinkApply(const AccountUid& target_uid, const std::string& pkg_dir) -> Result {
    fs::FsNativeSd sd;
    R_TRY(CheckHandoffPreconditions(sd));

    u64 nas_id = 0;
    std::vector<std::string> nas_files;
    R_TRY(ValidateLinkPackage(pkg_dir, nas_id, nas_files));

    const bool is_emummc = App::IsEmummc();

    const auto pkg_name = BaseName(pkg_dir);
    for (char c : pkg_name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_' && c != '-') {
            return FsError_PathNotFound;
        }
    }

    char stamp[32]{};
    const auto t = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::localtime(&t));

    const auto target_cands = BaasCandidateNames(target_uid);
    const auto target_rfc = UidDashedRfc(target_uid);
    const auto nand_str = std::string(is_emummc ? "emummc" : "sysmmc");

    std::string te;
    te += "# REQUIRE SD\n";
    te += "# REQUIRE KEYS\n";
    te += "# REQUIRE MINERVA\n";
    te += "# REQUIRE VER 4.0.0\n\n";

    te += "cleanup = {\n";
    te += "    if (fsexists(\"sd:/payload.bak\")) {\n";
    te += "        writefile(\"sd:/payload.bin\", readfile(\"sd:/payload.bak\"))\n";
    te += "        delfile(\"sd:/payload.bak\")\n";
    te += "    }\n";
    te += "    if (fsexists(\"sd:/startup.te\")) {\n";
    te += "        delfile(\"sd:/startup.te\")\n";
    te += "    }\n";
    te += "    if (fsexists(\"sd:/bootloader/update.bin\")) {\n";
    te += "        payload(\"sd:/bootloader/update.bin\")\n";
    te += "    }\n";
    te += "}\n\n";

    te += "clear()\n";
    te += "println(\"Kefir Hub: apply official account link\")\n";
    if (is_emummc) {
        te += "println(\"Target SYSTEM: emuMMC\")\n\n";
    } else {
        te += "println(\"Target SYSTEM: sysMMC\")\n\n";
    }

    te += "pkg = \"sd:/config/kefir/account_links/" + pkg_name + "\"\n";
    te += "rollback = combinepath(pkg, \"rollback_" + std::string(stamp) + "\")\n";
    te += "mkdir(rollback)\n";
    te += "mkdir(combinepath(rollback, \"baas\"))\n";
    te += "mkdir(combinepath(rollback, \"nas\"))\n";
    te += "writefile(combinepath(pkg, \"result_apply.txt\"), (\"operation=apply\\ntarget_uid=" + target_rfc + "\\ntarget_nand=" + nand_str + "\\nstage=starting\\n\").bytes())\n\n";

    if (is_emummc) {
        te += "rc = mountemu(\"SYSTEM\")\n";
    } else {
        te += "rc = mountsys(\"SYSTEM\")\n";
    }

    te += "if (rc) {\n";
    te += "    println(\"SYSTEM mount failed\", rc)\n";
    te += "    writefile(combinepath(pkg, \"result_apply.txt\"), (\"Apply failed\\ntarget_uid=" + target_rfc + "\\ntarget_nand=" + nand_str + "\\nstage=mount_system\\nerror=SYSTEM mount failed (\" + rc.str() + \")\\n\").bytes())\n";
    te += "    pause()\n";
    te += "    cleanup()\n";
    te += "    exit()\n";
    te += "}\n\n";

    te += "bis = \"bis:/save/8000000000000010\"\n";
    te += "if (!fsexists(bis)) {\n";
    te += "    println(\"Save 8000000000000010 not found on SYSTEM\")\n";
    te += "    writefile(combinepath(pkg, \"result_apply.txt\"), (\"Apply failed\\ntarget_uid=" + target_rfc + "\\ntarget_nand=" + nand_str + "\\nstage=check_save\\nerror=Save 8000000000000010 not found on SYSTEM\\n\").bytes())\n";
    te += "    pause()\n";
    te += "    cleanup()\n";
    te += "    exit()\n";
    te += "}\n\n";

    te += "saveObj = readsave(bis)\n";
    te += "baasListing = saveObj.readdir(\"/baas\")\n";
    te += "if (baasListing.result) {\n";
    te += "    println(\"Error: cannot read /baas in save 0010\", baasListing.result)\n";
    te += "    rep = \"Apply failed\\ntarget_uid=" + target_rfc + "\\ntarget_nand=" + nand_str + "\\nstage=read_baas\\nerror=cannot read /baas in save 0010 (\" + baasListing.result.str() + \")\\n\"\n";
    te += "    writefile(combinepath(pkg, \"result_apply.txt\"), rep.bytes())\n";
    te += "    pause()\n";
    te += "    cleanup()\n";
    te += "    exit()\n";
    te += "}\n\n";

    te += "nasListing = saveObj.readdir(\"/nas\")\n";
    te += "if (nasListing.result) {\n";
    te += "    println(\"Error: cannot read /nas in save 0010\", nasListing.result)\n";
    te += "    rep = \"Apply failed\\ntarget_uid=" + target_rfc + "\\ntarget_nand=" + nand_str + "\\nstage=read_nas\\nerror=cannot read /nas in save 0010 (\" + nasListing.result.str() + \")\\n\"\n";
    te += "    writefile(combinepath(pkg, \"result_apply.txt\"), rep.bytes())\n";
    te += "    pause()\n";
    te += "    cleanup()\n";
    te += "    exit()\n";
    te += "}\n\n";

    te += "errors = 0\n";
    te += "nasWritten = 0\n\n";

    te += "# 1. Backup and delete any existing target baas candidates\n";
    te += "cands = [\n";
    for (size_t i = 0; i < target_cands.size(); i++) {
        te += "    \"" + target_cands[i] + "\"";
        if (i + 1 < target_cands.size()) {
            te += ",";
        }
        te += "\n";
    }
    te += "]\n\n";

    te += "cands.foreach(\"cand\") {\n";
    te += "    candPath = \"/baas/\" + cand\n";
    te += "    if (baasListing.files.contains(cand)) {\n";
    te += "        oldBytes = saveObj.read(candPath)\n";
    te += "        wrc = writefile(combinepath(rollback, \"baas/\" + cand), oldBytes)\n";
    te += "        if (wrc) {\n";
    te += "            println(\"Failed to write rollback for\", candPath, wrc)\n";
    te += "            errors = errors + 1\n";
    te += "        } .else() {\n";
    te += "            drc = saveObj.delete(candPath)\n";
    te += "            if (drc) {\n";
    te += "                println(\"Failed to delete\", candPath, drc)\n";
    te += "                errors = errors + 1\n";
    te += "            }\n";
    te += "        }\n";
    te += "    }\n";
    te += "}\n\n";

    te += "# 2. Read source baas/link.dat and write target baas file\n";
    te += "baasSrc = combinepath(pkg, \"baas/link.dat\")\n";
    te += "baasData = readfile(baasSrc)\n";
    te += "if (baasData.len() < 24) {\n";
    te += "    println(\"Error: invalid baas/link.dat in package\")\n";
    te += "    errors = errors + 1\n";
    te += "} .else() {\n";
    te += "    if (!errors) {\n";
    te += "        dstBaas = \"/baas/" + target_rfc + ".dat\"\n";
    te += "        crc = saveObj.create(dstBaas, baasData.len())\n";
    te += "        if (crc) {\n";
    te += "            println(\"Failed to create\", dstBaas, crc)\n";
    te += "            errors = errors + 1\n";
    te += "        } .else() {\n";
    te += "            wrc = saveObj.write(dstBaas, baasData)\n";
    te += "            if (wrc) {\n";
    te += "                println(\"Failed to write\", dstBaas, wrc)\n";
    te += "                errors = errors + 1\n";
    te += "            }\n";
    te += "        }\n";
    te += "    }\n";
    te += "}\n\n";

    te += "# 3. Copy matching NAS files\n";
    te += "nasFiles = [\n";
    for (size_t i = 0; i < nas_files.size(); i++) {
        te += "    \"" + nas_files[i] + "\"";
        if (i + 1 < nas_files.size()) {
            te += ",";
        }
        te += "\n";
    }
    te += "]\n\n";

    te += "nasFiles.foreach(\"nname\") {\n";
    te += "    if (!errors) {\n";
    te += "        srcPath = combinepath(pkg, \"nas/\" + nname)\n";
    te += "        dstPath = \"/nas/\" + nname\n";
    te += "        ndata = readfile(srcPath)\n";
    te += "        if (!ndata.len()) {\n";
    te += "            println(\"Failed to read\", srcPath)\n";
    te += "            errors = errors + 1\n";
    te += "        } .else() {\n";
    te += "            if (nasListing.files.contains(nname)) {\n";
    te += "                oldNas = saveObj.read(dstPath)\n";
    te += "                wrc = writefile(combinepath(rollback, \"nas/\" + nname), oldNas)\n";
    te += "                if (wrc) {\n";
    te += "                    println(\"Failed to write rollback for\", dstPath, wrc)\n";
    te += "                    errors = errors + 1\n";
    te += "                } .else() {\n";
    te += "                    drc = saveObj.delete(dstPath)\n";
    te += "                    if (drc) {\n";
    te += "                        println(\"Failed to delete\", dstPath, drc)\n";
    te += "                        errors = errors + 1\n";
    te += "                    }\n";
    te += "                }\n";
    te += "            }\n";
    te += "            if (!errors) {\n";
    te += "                crc = saveObj.create(dstPath, ndata.len())\n";
    te += "                if (crc) {\n";
    te += "                    println(\"Failed to create\", dstPath, crc)\n";
    te += "                    errors = errors + 1\n";
    te += "                } .else() {\n";
    te += "                    wrc = saveObj.write(dstPath, ndata)\n";
    te += "                    if (wrc) {\n";
    te += "                        println(\"Failed to write\", dstPath, wrc)\n";
    te += "                        errors = errors + 1\n";
    te += "                    } .else() {\n";
    te += "                        nasWritten = nasWritten + 1\n";
    te += "                    }\n";
    te += "                }\n";
    te += "            }\n";
    te += "        }\n";
    te += "    }\n";
    te += "}\n\n";

    te += "if (!errors && nasWritten > 0) {\n";
    te += "    println(\"Writing changes to save 0010...\")\n";
    te += "    commitRc = saveObj.commit()\n";
    te += "    if (!commitRc) {\n";
    te += "        println(\"Commit succeeded!\")\n";
    te += "        rep = \"Apply succeeded\\ntarget_uid=" + target_rfc + "\\ntarget_nand=" + nand_str + R"(\nnas_written=" + nasWritten.str() + "\nrollback=" + rollback + "\n")" + "\n";
    te += "        writefile(combinepath(pkg, \"result_apply.txt\"), rep.bytes())\n";
    te += "        println(\"Official Nintendo Account link applied successfully.\")\n";
    te += "        println(\"Wrote baas file and\", nasWritten, \"NAS file(s).\")\n";
    te += "        println(\"Rollback backup saved to:\", rollback)\n";
    te += "    } .else() {\n";
    te += "        println(\"Commit failed with error:\", commitRc)\n";
    te += "        rep = \"Apply failed\\ntarget_uid=" + target_rfc + "\\ntarget_nand=" + nand_str + R"(\nstage=commit\nnas_written=" + nasWritten.str() + "\ncommit_error=" + commitRc.str() + "\nrollback=" + rollback + "\n")" + "\n";
    te += "        writefile(combinepath(pkg, \"result_apply.txt\"), rep.bytes())\n";
    te += "    }\n";
    te += "} .else() {\n";
    te += "    println(\"Errors occurred during apply. Changes were NOT committed.\")\n";
    te += "    rep = \"Apply failed\\ntarget_uid=" + target_rfc + "\\ntarget_nand=" + nand_str + R"(\nstage=apply_files\nnas_written=" + nasWritten.str() + "\nerrors=" + errors.str() + "\nrollback=" + rollback + "\n")" + "\n";
    te += "    writefile(combinepath(pkg, \"result_apply.txt\"), rep.bytes())\n";
    te += "}\n\n";

    te += "println(\"Press any button to return.\")\n";
    te += "pause()\n";
    te += "cleanup()\n";

    R_TRY(sd.write_entire_file("/startup.te", std::vector<u8>(te.begin(), te.end())));
    fsdevCommitDevice("sdmc");

    if (!utils::rebootToPayload("/bootloader/payloads/TegraExplorer.bin")) {
        sd.DeleteFile("/startup.te");
        fsdevCommitDevice("sdmc");
        return FsError_PathNotFound;
    }

    R_SUCCEED();
}

auto PrepareOfficialLinkLayoutProbe(const AccountUid& uid) -> Result {
    fs::FsNativeSd sd;
    R_TRY(CheckHandoffPreconditions(sd));

    u64 nas_id = 0;
    bool nas_id_known = false;
    if (R_SUCCEEDED(QueryNintendoAccountId(uid, nas_id)) && nas_id != 0) {
        nas_id_known = true;
    }

    const bool is_emummc = App::IsEmummc();

    char stamp[32]{};
    const auto t = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::localtime(&t));
    const auto base_dir = paths::DATA_ROOT + "/account_links/layout_probe_" + stamp;
    auto out_probe_dir = base_dir;
    for (u32 collision = 1; sd.DirExists(out_probe_dir.c_str()); collision++) {
        out_probe_dir = base_dir + "_" + std::to_string(collision);
    }

    R_TRY(sd.CreateDirectoryRecursively(out_probe_dir.c_str()));

    const auto probe_name = BaseName(out_probe_dir);
    for (char c : probe_name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_' && c != '-') {
            return FsError_PathNotFound;
        }
    }

    const auto nand_str = std::string(is_emummc ? "emummc" : "sysmmc");
    const auto rfc_lower_name = ToLowerCopy(UidDashedRfc(uid)) + ".dat";
    const auto rfc_upper_name = ToUpperCopy(UidDashedRfc(uid)) + ".dat";
    const auto linkalho_lower_name = ToLowerCopy(UidDashedLinkalho(uid)) + ".dat";
    const auto linkalho_upper_name = ToUpperCopy(UidDashedLinkalho(uid)) + ".dat";
    const auto raw_upper_name = UidHexRaw(uid) + ".dat";
    const auto raw_lower_name = ToLowerCopy(UidHexRaw(uid)) + ".dat";

    std::string te;
    te += "# REQUIRE SD\n";
    te += "# REQUIRE KEYS\n";
    te += "# REQUIRE MINERVA\n";
    te += "# REQUIRE VER 4.0.0\n\n";

    te += "cleanup = {\n";
    te += "    if (fsexists(\"sd:/payload.bak\")) {\n";
    te += "        writefile(\"sd:/payload.bin\", readfile(\"sd:/payload.bak\"))\n";
    te += "        delfile(\"sd:/payload.bak\")\n";
    te += "    }\n";
    te += "    if (fsexists(\"sd:/startup.te\")) {\n";
    te += "        delfile(\"sd:/startup.te\")\n";
    te += "    }\n";
    te += "    if (fsexists(\"sd:/bootloader/update.bin\")) {\n";
    te += "        payload(\"sd:/bootloader/update.bin\")\n";
    te += "    }\n";
    te += "}\n\n";

    te += "clear()\n";
    te += "println(\"Kefir Hub: probe official-link save layout\")\n";
    if (is_emummc) {
        te += "println(\"Probe SYSTEM: emuMMC\")\n\n";
    } else {
        te += "println(\"Probe SYSTEM: sysMMC\")\n\n";
    }

    te += "pkg = \"sd:/config/kefir/account_links/" + probe_name + "\"\n";
    te += "mkdir(pkg)\n";
    te += "writefile(combinepath(pkg, \"result_probe.txt\"), (\"operation=layout_probe\\ntarget_nand=" + nand_str + "\\nnas_id_known=" + (nas_id_known ? "1" : "0") + "\\nstage=starting\\n\").bytes())\n\n";

    if (is_emummc) {
        te += "rc = mountemu(\"SYSTEM\")\n";
    } else {
        te += "rc = mountsys(\"SYSTEM\")\n";
    }

    te += "if (rc) {\n";
    te += "    println(\"SYSTEM mount failed\", rc)\n";
    te += "    writefile(combinepath(pkg, \"result_probe.txt\"), (\"operation=layout_probe\\ntarget_nand=" + nand_str + "\\nnas_id_known=" + (nas_id_known ? "1" : "0") + "\\nstage=mount_system\\nerror=SYSTEM mount failed (\" + rc.str() + \")\\n\").bytes())\n";
    te += "    pause()\n";
    te += "    cleanup()\n";
    te += "    exit()\n";
    te += "}\n\n";

    te += "bis = \"bis:/save/8000000000000010\"\n";
    te += "if (!fsexists(bis)) {\n";
    te += "    println(\"Save 8000000000000010 not found on SYSTEM\")\n";
    te += "    writefile(combinepath(pkg, \"result_probe.txt\"), (\"operation=layout_probe\\ntarget_nand=" + nand_str + "\\nnas_id_known=" + (nas_id_known ? "1" : "0") + "\\nstage=check_save\\nerror=Save 8000000000000010 not found on SYSTEM\\n\").bytes())\n";
    te += "    pause()\n";
    te += "    cleanup()\n";
    te += "    exit()\n";
    te += "}\n\n";

    te += "saveObj = readsave(bis)\n\n";

    te += "suListing = saveObj.readdir(\"/su\")\n";
    te += "if (suListing.result) {\n";
    te += "    println(\"Error: cannot read /su in save 0010\", suListing.result)\n";
    te += "    writefile(combinepath(pkg, \"result_probe.txt\"), (\"operation=layout_probe\\ntarget_nand=" + nand_str + "\\nnas_id_known=" + (nas_id_known ? "1" : "0") + "\\nstage=read_su\\nerror=cannot read /su in save 0010 (\" + suListing.result.str() + \")\\n\").bytes())\n";
    te += "    pause()\n";
    te += "    cleanup()\n";
    te += "    exit()\n";
    te += "}\n\n";

    te += "has_registry = 0\n";
    te += "if ((suListing.files.contains(\"registry.dat\"))) {\n";
    te += "    has_registry = 1\n";
    te += "}\n\n";

    te += "has_profiles = 0\n";
    te += "if ((suListing.folders.contains(\"avators\"))) {\n";
    te += "    avListing = saveObj.readdir(\"/su/avators\")\n";
    te += "    if (!avListing.result) {\n";
    te += "        if ((avListing.files.contains(\"profiles.dat\"))) {\n";
    te += "            has_profiles = 1\n";
    te += "        }\n";
    te += "    }\n";
    te += "}\n\n";

    te += "baasListing = saveObj.readdir(\"/su/baas\")\n";
    te += "if (baasListing.result) {\n";
    te += "    println(\"Error: cannot read /su/baas in save 0010\", baasListing.result)\n";
    te += "    writefile(combinepath(pkg, \"result_probe.txt\"), (\"operation=layout_probe\\ntarget_nand=" + nand_str + "\\nnas_id_known=" + (nas_id_known ? "1" : "0") + "\\nstage=read_baas\\nerror=cannot read /su/baas in save 0010 (\" + baasListing.result.str() + \")\\n\").bytes())\n";
    te += "    pause()\n";
    te += "    cleanup()\n";
    te += "    exit()\n";
    te += "}\n\n";

    te += "nasListing = saveObj.readdir(\"/su/nas\")\n";
    te += "if (nasListing.result) {\n";
    te += "    println(\"Error: cannot read /su/nas in save 0010\", nasListing.result)\n";
    te += "    writefile(combinepath(pkg, \"result_probe.txt\"), (\"operation=layout_probe\\ntarget_nand=" + nand_str + "\\nnas_id_known=" + (nas_id_known ? "1" : "0") + "\\nstage=read_nas\\nerror=cannot read /su/nas in save 0010 (\" + nasListing.result.str() + \")\\n\").bytes())\n";
    te += "    pause()\n";
    te += "    cleanup()\n";
    te += "    exit()\n";
    te += "}\n\n";

    te += "baas_file_count = baasListing.files.len()\n";
    te += "baas_uid_rfc_name_count = 0\n";
    te += "baas_uid_linkalho_name_count = 0\n";
    te += "baas_uid_raw_name_count = 0\n";
    te += "baas_nas_content_match_count = 0\n\n";

    te += "baasListing.files.foreach(\"bfile\") {\n";
    te += "    if ((bfile == \"" + rfc_lower_name + "\")) {\n";
    te += "        baas_uid_rfc_name_count = (baas_uid_rfc_name_count + 1)\n";
    te += "    } .else() {\n";
    te += "        if ((bfile == \"" + rfc_upper_name + "\")) {\n";
    te += "            baas_uid_rfc_name_count = (baas_uid_rfc_name_count + 1)\n";
    te += "        }\n";
    te += "    }\n";
    te += "    if ((bfile == \"" + linkalho_lower_name + "\")) {\n";
    te += "        baas_uid_linkalho_name_count = (baas_uid_linkalho_name_count + 1)\n";
    te += "    } .else() {\n";
    te += "        if ((bfile == \"" + linkalho_upper_name + "\")) {\n";
    te += "            baas_uid_linkalho_name_count = (baas_uid_linkalho_name_count + 1)\n";
    te += "        }\n";
    te += "    }\n";
    te += "    if ((bfile == \"" + raw_upper_name + "\")) {\n";
    te += "        baas_uid_raw_name_count = (baas_uid_raw_name_count + 1)\n";
    te += "    } .else() {\n";
    te += "        if ((bfile == \"" + raw_lower_name + "\")) {\n";
    te += "            baas_uid_raw_name_count = (baas_uid_raw_name_count + 1)\n";
    te += "        }\n";
    te += "    }\n";
    if (nas_id_known) {
        te += "    bbytes = saveObj.read(\"/su/baas/\" + bfile)\n";
        te += "    if ((bbytes.len() >= 24)) {\n";
        te += "        match = 1\n";
        for (int i = 0; i < 8; i++) {
            const auto byte_val = static_cast<unsigned>((nas_id >> (8 * i)) & 0xffu);
            te += "        if (!(bbytes[" + std::to_string(16 + i) + "] == " + std::to_string(byte_val) + ")) {\n";
            te += "            match = 0\n";
            te += "        }\n";
        }
        te += "        if (match) {\n";
        te += "            baas_nas_content_match_count = (baas_nas_content_match_count + 1)\n";
        te += "        }\n";
        te += "    }\n";
    }
    te += "}\n\n";

    te += "nas_file_count = nasListing.files.len()\n";
    te += "nas_prefix_match_count = 0\n\n";

    if (nas_id_known) {
        const auto prefixes = NasPrefixes(nas_id);
        te += "nasListing.files.foreach(\"nfile\") {\n";
        te += "    match = 0\n";
        for (const auto& pfx : prefixes) {
            const auto len_str = std::to_string(pfx.length());
            te += "    if ((!match) && ((nfile.len() >= " + len_str + "))) {\n";
            te += "        namePrefix = (nfile - (nfile.len() - " + len_str + "))\n";
            te += "        if ((namePrefix == \"" + pfx + "\")) {\n";
            te += "            match = 1\n";
            te += "        }\n";
            te += "    }\n";
        }
        te += "    if (match) {\n";
        te += "        nas_prefix_match_count = (nas_prefix_match_count + 1)\n";
        te += "    }\n";
        te += "}\n\n";
    }

    te += "resReport = \"operation=layout_probe\\ntarget_nand=" + nand_str + "\\nnas_id_known=" + (nas_id_known ? "1" : "0") + "\\nbaas_file_count=\" + baas_file_count.str() + \"\\nbaas_uid_rfc_name_count=\" + baas_uid_rfc_name_count.str() + \"\\nbaas_uid_linkalho_name_count=\" + baas_uid_linkalho_name_count.str() + \"\\nbaas_uid_raw_name_count=\" + baas_uid_raw_name_count.str() + \"\\nbaas_nas_content_match_count=\" + baas_nas_content_match_count.str() + \"\\nnas_file_count=\" + nas_file_count.str() + \"\\nnas_prefix_match_count=\" + nas_prefix_match_count.str() + \"\\nhas_registry=\" + has_registry.str() + \"\\nhas_profiles=\" + has_profiles.str() + \"\\n\"\n";
    te += "writefile(combinepath(pkg, \"result_probe.txt\"), resReport.bytes())\n\n";

    te += "println(\"Layout probe completed successfully.\")\n";
    te += "println(\"baas files:\", baas_file_count)\n";
    te += "println(\"baas RFC name matches:\", baas_uid_rfc_name_count)\n";
    te += "println(\"baas Linkalho name matches:\", baas_uid_linkalho_name_count)\n";
    te += "println(\"baas raw hex matches:\", baas_uid_raw_name_count)\n";
    te += "println(\"baas NAS content matches:\", baas_nas_content_match_count)\n";
    te += "println(\"nas files:\", nas_file_count)\n";
    te += "println(\"nas prefix matches:\", nas_prefix_match_count)\n";
    te += "println(\"has registry:\", has_registry)\n";
    te += "println(\"has profiles:\", has_profiles)\n";
    te += "println(\"\")\n";
    te += "println(\"Press any button to return.\")\n";
    te += "pause()\n";
    te += "cleanup()\n";

    R_TRY(sd.write_entire_file("/startup.te", std::vector<u8>(te.begin(), te.end())));
    fsdevCommitDevice("sdmc");

    if (!utils::rebootToPayload("/bootloader/payloads/TegraExplorer.bin")) {
        sd.DeleteFile("/startup.te");
        fsdevCommitDevice("sdmc");
        return FsError_PathNotFound;
    }

    R_SUCCEED();
}

auto PrepareAccountSaveDump(bool& out_rebooted) -> Result {
    out_rebooted = false;
    fs::FsNativeSd sd;
    const bool is_emummc = App::IsEmummc();
    const auto nand_str = std::string(is_emummc ? "emummc" : "sysmmc");

    auto save = TryOpenAccountSave();
    if (R_SUCCEEDED(save.GetFsOpenResult())) {
        char stamp[32]{};
        const auto t = std::time(nullptr);
        std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::localtime(&t));
        const auto base_dir = paths::DATA_ROOT + "/account_save_dump/" + stamp;
        auto out_dump_dir = base_dir;
        for (u32 collision = 1; sd.DirExists(out_dump_dir.c_str()); collision++) {
            out_dump_dir = base_dir + "_" + std::to_string(collision);
        }

        R_TRY(sd.CreateDirectoryRecursively(out_dump_dir.c_str()));
        sd.CreateDirectoryRecursively((out_dump_dir + "/su").c_str());
        sd.CreateDirectoryRecursively((out_dump_dir + "/su/baas").c_str());
        sd.CreateDirectoryRecursively((out_dump_dir + "/su/nas").c_str());
        sd.CreateDirectoryRecursively((out_dump_dir + "/su/avators").c_str());
        sd.CreateDirectoryRecursively((out_dump_dir + "/su/cache").c_str());

        const std::string readme = "WARNING: Research dump of account save 0x8000000000000010 (/su tree). Contains account tokens and identifiers for all local profiles. Do not share.\n";
        sd.write_entire_file(out_dump_dir + "/README.txt", std::vector<u8>(readme.begin(), readme.end()));

        u32 su_listed = 0, su_file_count = 0;
        u32 baas_listed = 0, baas_file_count = 0;
        u32 nas_listed = 0, nas_file_count = 0;
        u32 avators_listed = 0, avators_file_count = 0;
        u32 cache_listed = 0, cache_file_count = 0;
        u32 skipped_unsafe_names = 0;

        auto copy_dir = [&](const std::string& dir, u32& listed, u32& count) {
            if (!save.DirExists(dir.c_str())) {
                return;
            }
            for (const auto& name : ListDirFiles(save, dir)) {
                listed++;
                if (!IsSafeDumpFileName(name)) {
                    skipped_unsafe_names++;
                    continue;
                }
                if (R_SUCCEEDED(CopyFile(save, dir + "/" + name, sd, out_dump_dir + dir + "/" + name))) {
                    count++;
                }
            }
        };

        copy_dir("/su", su_listed, su_file_count);
        copy_dir("/su/baas", baas_listed, baas_file_count);
        copy_dir("/su/nas", nas_listed, nas_file_count);
        copy_dir("/su/avators", avators_listed, avators_file_count);
        copy_dir("/su/cache", cache_listed, cache_file_count);

        if ((su_file_count + baas_file_count + nas_file_count + avators_file_count) > 0) {
            std::string result_txt;
            result_txt += "operation=account_save_dump\n";
            result_txt += "method=horizon\n";
            result_txt += "target_nand=" + nand_str + "\n";
            result_txt += "su_listed=" + std::to_string(su_listed) + "\n";
            result_txt += "su_file_count=" + std::to_string(su_file_count) + "\n";
            result_txt += "baas_listed=" + std::to_string(baas_listed) + "\n";
            result_txt += "baas_file_count=" + std::to_string(baas_file_count) + "\n";
            result_txt += "nas_listed=" + std::to_string(nas_listed) + "\n";
            result_txt += "nas_file_count=" + std::to_string(nas_file_count) + "\n";
            result_txt += "avators_listed=" + std::to_string(avators_listed) + "\n";
            result_txt += "avators_file_count=" + std::to_string(avators_file_count) + "\n";
            result_txt += "cache_listed=" + std::to_string(cache_listed) + "\n";
            result_txt += "cache_file_count=" + std::to_string(cache_file_count) + "\n";
            result_txt += "skipped_unsafe_names=" + std::to_string(skipped_unsafe_names) + "\n";
            sd.write_entire_file(out_dump_dir + "/result.txt", std::vector<u8>(result_txt.begin(), result_txt.end()));
            fsdevCommitDevice("sdmc");

            out_rebooted = false;
            R_SUCCEED();
        }
    }

    R_TRY(CheckHandoffPreconditions(sd));

    char stamp[32]{};
    const auto t = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::localtime(&t));
    const auto base_dir = paths::DATA_ROOT + "/account_save_dump/" + stamp;
    auto out_dump_dir = base_dir;
    for (u32 collision = 1; sd.DirExists(out_dump_dir.c_str()); collision++) {
        out_dump_dir = base_dir + "_" + std::to_string(collision);
    }

    R_TRY(sd.CreateDirectoryRecursively(out_dump_dir.c_str()));

    const auto dump_name = BaseName(out_dump_dir);
    for (char c : dump_name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_' && c != '-') {
            return FsError_PathNotFound;
        }
    }

    const std::string readme = "WARNING: Research dump of account save 0x8000000000000010 (/su tree). Contains account tokens and identifiers for all local profiles. Do not share.\n";
    sd.write_entire_file(out_dump_dir + "/README.txt", std::vector<u8>(readme.begin(), readme.end()));

    std::string te;
    te += "# REQUIRE SD\n";
    te += "# REQUIRE KEYS\n";
    te += "# REQUIRE MINERVA\n";
    te += "# REQUIRE VER 4.0.0\n\n";

    te += "cleanup = {\n";
    te += "    if (fsexists(\"sd:/payload.bak\")) {\n";
    te += "        writefile(\"sd:/payload.bin\", readfile(\"sd:/payload.bak\"))\n";
    te += "        delfile(\"sd:/payload.bak\")\n";
    te += "    }\n";
    te += "    if (fsexists(\"sd:/startup.te\")) {\n";
    te += "        delfile(\"sd:/startup.te\")\n";
    te += "    }\n";
    te += "    if (fsexists(\"sd:/bootloader/update.bin\")) {\n";
    te += "        payload(\"sd:/bootloader/update.bin\")\n";
    te += "    }\n";
    te += "}\n\n";

    te += "clear()\n";
    te += "println(\"Kefir Hub: dump account save 0010\")\n";
    if (is_emummc) {
        te += "println(\"Dump SYSTEM: emuMMC\")\n\n";
    } else {
        te += "println(\"Dump SYSTEM: sysMMC\")\n\n";
    }

    te += "pkg = \"sd:/config/kefir/account_save_dump/" + dump_name + "\"\n";
    te += "mkdir(pkg)\n";
    te += "mkdir(combinepath(pkg, \"su\"))\n";
    te += "mkdir(combinepath(pkg, \"su/baas\"))\n";
    te += "mkdir(combinepath(pkg, \"su/nas\"))\n";
    te += "mkdir(combinepath(pkg, \"su/avators\"))\n";
    te += "mkdir(combinepath(pkg, \"su/cache\"))\n";
    te += "writefile(combinepath(pkg, \"result.txt\"), (\"operation=account_save_dump\\nmethod=tegra\\ntarget_nand=" + nand_str + "\\nstage=starting\\n\").bytes())\n\n";

    if (is_emummc) {
        te += "rc = mountemu(\"SYSTEM\")\n";
    } else {
        te += "rc = mountsys(\"SYSTEM\")\n";
    }

    te += "if (rc) {\n";
    te += "    println(\"SYSTEM mount failed\", rc)\n";
    te += "    writefile(combinepath(pkg, \"result.txt\"), (\"operation=account_save_dump\\nmethod=tegra\\ntarget_nand=" + nand_str + "\\nstage=mount_system\\nerror=SYSTEM mount failed (\" + rc.str() + \")\\n\").bytes())\n";
    te += "    pause()\n";
    te += "    cleanup()\n";
    te += "    exit()\n";
    te += "}\n\n";

    te += "bis = \"bis:/save/8000000000000010\"\n";
    te += "if (!fsexists(bis)) {\n";
    te += "    println(\"Save 8000000000000010 not found on SYSTEM\")\n";
    te += "    writefile(combinepath(pkg, \"result.txt\"), (\"operation=account_save_dump\\nmethod=tegra\\ntarget_nand=" + nand_str + "\\nstage=check_save\\nerror=Save 8000000000000010 not found on SYSTEM\\n\").bytes())\n";
    te += "    pause()\n";
    te += "    cleanup()\n";
    te += "    exit()\n";
    te += "}\n\n";

    te += "saveObj = readsave(bis)\n\n";

    te += "suListing = saveObj.readdir(\"/su\")\n";
    te += "if (suListing.result) {\n";
    te += "    println(\"Error: cannot read /su in save 0010\", suListing.result)\n";
    te += "    writefile(combinepath(pkg, \"result.txt\"), (\"operation=account_save_dump\\nmethod=tegra\\ntarget_nand=" + nand_str + "\\nstage=read_su\\nerror=cannot read /su in save 0010 (\" + suListing.result.str() + \")\\n\").bytes())\n";
    te += "    pause()\n";
    te += "    cleanup()\n";
    te += "    exit()\n";
    te += "}\n\n";

    te += "suFiles = suListing.files.copy()\n";
    te += "su_listed = suFiles.len()\n";
    te += "su_file_count = 0\n";
    te += "suFiles.foreach(\"suName\") {\n";
    te += "    data = saveObj.read(\"/su/\" + suName)\n";
    te += "    wrc = writefile(combinepath(pkg, \"su/\" + suName), data)\n";
    te += "    if (!wrc) {\n";
    te += "        su_file_count = (su_file_count + 1)\n";
    te += "    }\n";
    te += "}\n\n";

    te += "baas_listed = 0\n";
    te += "baas_file_count = 0\n";
    te += "baasListing = saveObj.readdir(\"/su/baas\")\n";
    te += "if (!baasListing.result) {\n";
    te += "    baasFiles = baasListing.files.copy()\n";
    te += "    baas_listed = baasFiles.len()\n";
    te += "    baasFiles.foreach(\"baasName\") {\n";
    te += "        data = saveObj.read(\"/su/baas/\" + baasName)\n";
    te += "        wrc = writefile(combinepath(pkg, \"su/baas/\" + baasName), data)\n";
    te += "        if (!wrc) {\n";
    te += "            baas_file_count = (baas_file_count + 1)\n";
    te += "        }\n";
    te += "    }\n";
    te += "}\n\n";

    te += "nas_listed = 0\n";
    te += "nas_file_count = 0\n";
    te += "nasListing = saveObj.readdir(\"/su/nas\")\n";
    te += "if (!nasListing.result) {\n";
    te += "    nasFiles = nasListing.files.copy()\n";
    te += "    nas_listed = nasFiles.len()\n";
    te += "    nasFiles.foreach(\"nasName\") {\n";
    te += "        data = saveObj.read(\"/su/nas/\" + nasName)\n";
    te += "        wrc = writefile(combinepath(pkg, \"su/nas/\" + nasName), data)\n";
    te += "        if (!wrc) {\n";
    te += "            nas_file_count = (nas_file_count + 1)\n";
    te += "        }\n";
    te += "    }\n";
    te += "}\n\n";

    te += "avators_listed = 0\n";
    te += "avators_file_count = 0\n";
    te += "avatorsListing = saveObj.readdir(\"/su/avators\")\n";
    te += "if (!avatorsListing.result) {\n";
    te += "    avFiles = avatorsListing.files.copy()\n";
    te += "    avators_listed = avFiles.len()\n";
    te += "    avFiles.foreach(\"avName\") {\n";
    te += "        data = saveObj.read(\"/su/avators/\" + avName)\n";
    te += "        wrc = writefile(combinepath(pkg, \"su/avators/\" + avName), data)\n";
    te += "        if (!wrc) {\n";
    te += "            avators_file_count = (avators_file_count + 1)\n";
    te += "        }\n";
    te += "    }\n";
    te += "}\n\n";

    te += "cache_listed = 0\n";
    te += "cache_file_count = 0\n";
    te += "cacheListing = saveObj.readdir(\"/su/cache\")\n";
    te += "if (!cacheListing.result) {\n";
    te += "    cacheFiles = cacheListing.files.copy()\n";
    te += "    cache_listed = cacheFiles.len()\n";
    te += "    cacheFiles.foreach(\"cacheName\") {\n";
    te += "        data = saveObj.read(\"/su/cache/\" + cacheName)\n";
    te += "        wrc = writefile(combinepath(pkg, \"su/cache/\" + cacheName), data)\n";
    te += "        if (!wrc) {\n";
    te += "            cache_file_count = (cache_file_count + 1)\n";
    te += "        }\n";
    te += "    }\n";
    te += "}\n\n";

    te += "resReport = \"operation=account_save_dump\\nmethod=tegra\\ntarget_nand=" + nand_str + "\\nsu_listed=\" + su_listed.str() + \"\\nsu_file_count=\" + su_file_count.str() + \"\\nbaas_listed=\" + baas_listed.str() + \"\\nbaas_file_count=\" + baas_file_count.str() + \"\\nnas_listed=\" + nas_listed.str() + \"\\nnas_file_count=\" + nas_file_count.str() + \"\\navators_listed=\" + avators_listed.str() + \"\\navators_file_count=\" + avators_file_count.str() + \"\\ncache_listed=\" + cache_listed.str() + \"\\ncache_file_count=\" + cache_file_count.str() + \"\\nskipped_unsafe_names=0\\nstage=done\\n\"\n";
    te += "writefile(combinepath(pkg, \"result.txt\"), resReport.bytes())\n\n";

    te += "println(\"Account save dump completed successfully.\")\n";
    te += "println(\"su listed: \" + su_listed.str() + \" copied: \" + su_file_count.str())\n";
    te += "println(\"baas listed: \" + baas_listed.str() + \" copied: \" + baas_file_count.str())\n";
    te += "println(\"nas listed: \" + nas_listed.str() + \" copied: \" + nas_file_count.str())\n";
    te += "println(\"avators listed: \" + avators_listed.str() + \" copied: \" + avators_file_count.str())\n";
    te += "println(\"cache listed: \" + cache_listed.str() + \" copied: \" + cache_file_count.str())\n";
    te += "println(\"\")\n";
    te += "println(\"Press any button to return.\")\n";
    te += "pause()\n";
    te += "cleanup()\n";

    R_TRY(sd.write_entire_file("/startup.te", std::vector<u8>(te.begin(), te.end())));
    fsdevCommitDevice("sdmc");

    if (!utils::rebootToPayload("/bootloader/payloads/TegraExplorer.bin")) {
        sd.DeleteFile("/startup.te");
        fsdevCommitDevice("sdmc");
        return FsError_PathNotFound;
    }

    out_rebooted = true;
    R_SUCCEED();
}

} // namespace sphaira::account_link
