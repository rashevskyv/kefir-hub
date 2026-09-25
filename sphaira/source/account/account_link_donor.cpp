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
auto LoadDonorPackageFromPath(const std::string& base_path, RomfsDonorPackage& out_pkg) -> Result {
    out_pkg = {};

    std::vector<u8> manifest_bytes;
    R_TRY(fs::read_entire_file((base_path + "/manifest.txt").c_str(), manifest_bytes));
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

    const auto baas_path = base_path + "/" + kv["baas_file"];
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

        const auto fpath = base_path + "/nas/" + fname;
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


auto LoadRomfsDonorPackage(RomfsDonorPackage& out_pkg) -> Result {
    R_TRY(romfsInit());
    ON_SCOPE_EXIT(romfsExit());
    return LoadDonorPackageFromPath("romfs:/account_link", out_pkg);
}

auto LoadRomfsDonorPackages(std::vector<RomfsDonorPackage>& out_packages) -> Result {
    out_packages.clear();

    R_TRY(romfsInit());
    ON_SCOPE_EXIT(romfsExit());

    std::vector<u8> pool_bytes;
    R_TRY(fs::read_entire_file("romfs:/account_link/pool.txt", pool_bytes));
    R_UNLESS(!pool_bytes.empty(), Result_FsInvalidType);

    const std::string pool_str(pool_bytes.begin(), pool_bytes.end());
    std::unordered_map<std::string, std::string> kv;
    size_t line_start = 0;
    while (line_start < pool_str.size()) {
        auto line_end = pool_str.find('\n', line_start);
        if (line_end == std::string::npos) {
            line_end = pool_str.size();
        }
        auto line = pool_str.substr(line_start, line_end - line_start);
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

    R_UNLESS(kv["format"] == "kefir_account_pool", Result_FsInvalidType);
    R_UNLESS(kv["version"] == "1", Result_FsInvalidType);
    const auto& donors_csv = kv["donors"];
    R_UNLESS(!donors_csv.empty(), Result_FsInvalidType);

    std::vector<std::string> donor_paths;
    size_t csv_start = 0;
    while (csv_start < donors_csv.size()) {
        auto comma = donors_csv.find(',', csv_start);
        if (comma == std::string::npos) {
            comma = donors_csv.size();
        }
        auto dpath = donors_csv.substr(csv_start, comma - csv_start);
        while (!dpath.empty() && std::isspace(static_cast<unsigned char>(dpath.front()))) {
            dpath.erase(dpath.begin());
        }
        while (!dpath.empty() && std::isspace(static_cast<unsigned char>(dpath.back()))) {
            dpath.pop_back();
        }
        if (!dpath.empty()) {
            donor_paths.push_back(dpath);
        }
        csv_start = comma + 1;
    }
    R_UNLESS(!donor_paths.empty(), Result_FsInvalidType);

    std::vector<RomfsDonorPackage> loaded_packages;
    loaded_packages.reserve(donor_paths.size());

    for (const auto& rel_path : donor_paths) {
        R_UNLESS(!rel_path.empty(), Result_FsInvalidType);
        R_UNLESS(rel_path.front() != '/', Result_FsInvalidType);
        R_UNLESS(rel_path.find(':') == std::string::npos, Result_FsInvalidType);
        R_UNLESS(rel_path.find('\\') == std::string::npos, Result_FsInvalidType);
        R_UNLESS(rel_path.find("..") == std::string::npos, Result_FsInvalidType);

        const std::string full_path = (rel_path == ".")
            ? "romfs:/account_link"
            : "romfs:/account_link/" + rel_path;

        RomfsDonorPackage pkg;
        R_TRY(LoadDonorPackageFromPath(full_path, pkg));
        loaded_packages.push_back(std::move(pkg));
    }

    R_UNLESS(!loaded_packages.empty(), Result_FsInvalidType);

    for (size_t i = 0; i < loaded_packages.size(); i++) {
        for (size_t j = i + 1; j < loaded_packages.size(); j++) {
            if (loaded_packages[i].nas_id == loaded_packages[j].nas_id) {
                log_write("[ACC] LoadRomfsDonorPackages failed: duplicate nas identity across pool\n");
                return Result_FsInvalidType;
            }
        }
    }

    out_packages = std::move(loaded_packages);
    log_write("[ACC] LoadRomfsDonorPackages loaded %u donor packages\n", static_cast<u32>(out_packages.size()));
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


auto LinkAllFromRomfsDonor(u32& out_linked_count) -> Result {
    out_linked_count = 0;

    std::vector<RomfsDonorPackage> pool;
    const auto load_rc = LoadRomfsDonorPackages(pool);
    if (R_FAILED(load_rc)) {
        log_write("[ACC] LoadRomfsDonorPackages failed 0x%X\n", load_rc);
        return load_rc;
    }
    log_write("[ACC] LoadRomfsDonorPackages ok, pool size %u\n", static_cast<u32>(pool.size()));

    const auto all_users = ListUsers();
    for (const auto& u : all_users) {
        if (!u.linked_known) {
            log_write("[ACC] LinkAllFromRomfsDonor refused: live user with unknown link status\n");
            return Result_FsInvalidType;
        }
    }

    std::vector<AccountUid> target_uids;
    for (const auto& u : all_users) {
        if (u.linked_known && !u.horizon_linked) {
            target_uids.push_back(u.uid);
        }
    }
    log_write("[ACC] LinkAllFromRomfsDonor eligible unlinked=%u\n", static_cast<u32>(target_uids.size()));

    if (target_uids.empty()) {
        R_SUCCEED();
    }

    std::vector<RomfsDonorPackage> available_donors;
    for (const auto& donor : pool) {
        AccountUid live_uid{};
        if (!FindLiveUidByNasId(donor.nas_id, live_uid)) {
            available_donors.push_back(donor);
        }
    }
    log_write("[ACC] LinkAllFromRomfsDonor available unused donors=%u\n", static_cast<u32>(available_donors.size()));

    if (target_uids.size() > available_donors.size()) {
        log_write("[ACC] LinkAllFromRomfsDonor refused: unlinked targets (%u) > available donors (%u)\n",
            static_cast<u32>(target_uids.size()), static_cast<u32>(available_donors.size()));
        return Result_FsInvalidType;
    }

    std::vector<TargetLink> targets;
    targets.reserve(target_uids.size());
    for (size_t i = 0; i < target_uids.size(); i++) {
        targets.push_back({target_uids[i], available_donors[i]});
    }

    return ApplyLinkPackages(targets, out_linked_count);
}


} // namespace sphaira::account_link
