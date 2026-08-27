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
        return Result_FsAlreadyExists;
    }
    if (!sd.FileExists("/payload.bin") ||
        !sd.FileExists("/bootloader/payloads/TegraExplorer.bin") ||
        !sd.FileExists("/bootloader/update.bin")) {
        return FsError_PathNotFound;
    }
    R_SUCCEED();
}

} // namespace

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
        } else {
            u.linked_known = false;
            u.horizon_linked = false;
            log_write("[ACC] Horizon link check failed 0x%X uid %s\n", rc, u.uid_hex.c_str());
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
    R_UNLESS(!pkg_dir.empty(), Result_FsInvalidPath);

    auto norm_path = pkg_dir;
    while (!norm_path.empty() && (norm_path.back() == '/' || norm_path.back() == '\\')) {
        norm_path.pop_back();
    }
    R_UNLESS(!norm_path.empty(), Result_FsInvalidPath);

    if (norm_path.find("..") != std::string::npos || norm_path.find('\\') != std::string::npos || norm_path.find("//") != std::string::npos) {
        return Result_FsInvalidPath;
    }

    const auto pkg_name = BaseName(norm_path);
    R_UNLESS(!pkg_name.empty(), Result_FsInvalidPath);
    for (char c : pkg_name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_' && c != '-') {
            return Result_FsInvalidPath;
        }
    }

    const auto allowed_root_app = paths::DATA_ROOT + "/account_links";
    const auto allowed_root_kefir = std::string("/config/kefir/account_links");
    const auto expected_app = allowed_root_app + "/" + pkg_name;
    const auto expected_kefir = allowed_root_kefir + "/" + pkg_name;

    if (norm_path != expected_app && norm_path != expected_kefir) {
        return Result_FsInvalidPath;
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

    try {
        size_t idx = 0;
        out_nas_id = std::stoull(nas_hex, &idx, 16);
        R_UNLESS(idx == nas_hex.size() && out_nas_id != 0, Result_FsInvalidType);
    } catch (...) {
        return Result_FsInvalidType;
    }

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

    std::string manifest;
    manifest += "format=kefir_account_link\n";
    manifest += "version=2\n";
    manifest += "system_save=8000000000000010\n";
    manifest += "idgen_0011_included=false\n";
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
            return Result_FsInvalidPath;
        }
    }

    const auto cands = BaasCandidateNames(uid);

    std::vector<std::string> prefixes;
    auto add_pfx = [&](const std::string& p) {
        if (!p.empty() && std::find(prefixes.begin(), prefixes.end(), p) == prefixes.end()) {
            prefixes.push_back(p);
        }
    };
    add_pfx(ToLowerCopy(NasHex(nas_id)));
    add_pfx(ToUpperCopy(NasHex(nas_id)));
    add_pfx(ToLowerCopy(NasHexShort(nas_id)));
    add_pfx(ToUpperCopy(NasHexShort(nas_id)));

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
    te += "println(\"\")\n\n";

    te += "targets = [\"Cancel\"].copy()\n";
    te += "targets.add(\"emuMMC SYSTEM\")\n";
    te += "targets.add(\"sysMMC SYSTEM\")\n";
    te += "choice = menu(targets, 0)\n";
    te += "if (!choice) {\n";
    te += "    cleanup()\n";
    te += "    exit()\n";
    te += "}\n\n";

    te += "if (choice == 1) {\n";
    te += "    if (!emu()) {\n";
    te += "        println(\"No emuMMC\")\n";
    te += "        pause()\n";
    te += "        cleanup()\n";
    te += "        exit()\n";
    te += "    }\n";
    te += "    rc = mountemu(\"SYSTEM\")\n";
    te += "} .else() {\n";
    te += "    rc = mountsys(\"SYSTEM\")\n";
    te += "}\n\n";

    te += "if (rc) {\n";
    te += "    println(\"SYSTEM mount failed\", rc)\n";
    te += "    pause()\n";
    te += "    cleanup()\n";
    te += "    exit()\n";
    te += "}\n\n";

    te += "bis = \"bis:/save/8000000000000010\"\n";
    te += "if (!fsexists(bis)) {\n";
    te += "    println(\"Save 8000000000000010 not found on SYSTEM\")\n";
    te += "    pause()\n";
    te += "    cleanup()\n";
    te += "    exit()\n";
    te += "}\n\n";

    te += "pkg = \"sd:/config/kefir/account_links/" + pkg_name + "\"\n";
    te += "mkdir(pkg)\n";
    te += "mkdir(combinepath(pkg, \"baas\"))\n";
    te += "mkdir(combinepath(pkg, \"nas\"))\n\n";

    te += "saveObj = readsave(bis)\n\n";

    te += "baasListing = saveObj.readdir(\"/baas\")\n";
    te += "if (baasListing.result) {\n";
    te += "    println(\"Error: cannot read /baas in save 0010\", baasListing.result)\n";
    te += "    writefile(combinepath(pkg, \"result.txt\"), \"Export failed: cannot read /baas in save 0010\\n\")\n";
    te += "    pause()\n";
    te += "    cleanup()\n";
    te += "    exit()\n";
    te += "}\n\n";

    te += "cands = [\n";
    for (size_t i = 0; i < cands.size(); i++) {
        te += "    \"" + cands[i] + "\"";
        if (i + 1 < cands.size()) {
            te += ",";
        }
        te += "\n";
    }
    te += "]\n\n";

    te += "foundBaas = 0\n";
    te += "cands.foreach(\"cand\") {\n";
    te += "    if (!foundBaas && baasListing.files.contains(cand)) {\n";
    te += "        bbytes = saveObj.read(\"/baas/\" + cand)\n";
    te += "        if (bbytes.len() >= 24) {\n";
    te += "            writefile(combinepath(pkg, \"baas/link.dat\"), bbytes)\n";
    te += "            foundBaas = 1\n";
    te += "        }\n";
    te += "    }\n";
    te += "}\n\n";

    te += "if (!foundBaas) {\n";
    te += "    println(\"Error: source baas file not found in 0010\")\n";
    te += "    writefile(combinepath(pkg, \"result.txt\"), \"Export failed: source baas file not found in 0010\\n\")\n";
    te += "    pause()\n";
    te += "    cleanup()\n";
    te += "    exit()\n";
    te += "}\n\n";

    te += "nasListing = saveObj.readdir(\"/nas\")\n";
    te += "if (nasListing.result) {\n";
    te += "    println(\"Error: cannot read /nas in save 0010\", nasListing.result)\n";
    te += "    writefile(combinepath(pkg, \"result.txt\"), \"Export failed: cannot read /nas in save 0010\\n\")\n";
    te += "    pause()\n";
    te += "    cleanup()\n";
    te += "    exit()\n";
    te += "}\n\n";

    te += "pfxList = [\n";
    for (size_t i = 0; i < prefixes.size(); i++) {
        te += "    \"" + prefixes[i] + "\".bytes()";
        if (i + 1 < prefixes.size()) {
            te += ",";
        }
        te += "\n";
    }
    te += "]\n\n";

    te += "nasCopied = 0\n";
    te += "nasListing.files.foreach(\"nfile\") {\n";
    te += "    nbytes = nfile.bytes()\n";
    te += "    match = 0\n";
    te += "    pfxList.foreach(\"pfx\") {\n";
    te += "        if (!match && nbytes.len() >= pfx.len()) {\n";
    te += "            if (nbytes.slice(0, pfx.len()) == pfx) {\n";
    te += "                match = 1\n";
    te += "            }\n";
    te += "        }\n";
    te += "    }\n";
    te += "    if (match) {\n";
    te += "        ndata = saveObj.read(\"/nas/" + nfile + "\")\n";
    te += "        writefile(combinepath(pkg, \"nas/\" + nfile), ndata)\n";
    te += "        nasCopied = nasCopied + 1\n";
    te += "    }\n";
    te += "}\n\n";

    te += "if (!nasCopied) {\n";
    te += "    println(\"Error: no matching NAS files found in 0010\")\n";
    te += "    writefile(combinepath(pkg, \"result.txt\"), \"Export failed: no matching NAS files found in 0010\\n\")\n";
    te += "    pause()\n";
    te += "    cleanup()\n";
    te += "    exit()\n";
    te += "}\n\n";

    te += R"(resReport = "Export completed successfully\nbaas=baas/link.dat\nnas_copied=" + nasCopied + "\n")" "\n";
    te += "writefile(combinepath(pkg, \"result.txt\"), resReport)\n\n";

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

    const auto pkg_name = BaseName(pkg_dir);
    for (char c : pkg_name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_' && c != '-') {
            return Result_FsInvalidPath;
        }
    }

    char stamp[32]{};
    const auto t = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::localtime(&t));

    const auto target_cands = BaasCandidateNames(target_uid);
    const auto target_rfc = UidDashedRfc(target_uid);

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
    te += "println(\"\")\n\n";

    te += "targets = [\"Cancel\"].copy()\n";
    te += "targets.add(\"emuMMC SYSTEM\")\n";
    te += "targets.add(\"sysMMC SYSTEM\")\n";
    te += "choice = menu(targets, 0)\n";
    te += "if (!choice) {\n";
    te += "    cleanup()\n";
    te += "    exit()\n";
    te += "}\n\n";

    te += "if (choice == 1) {\n";
    te += "    if (!emu()) {\n";
    te += "        println(\"No emuMMC\")\n";
    te += "        pause()\n";
    te += "        cleanup()\n";
    te += "        exit()\n";
    te += "    }\n";
    te += "    rc = mountemu(\"SYSTEM\")\n";
    te += "} .else() {\n";
    te += "    rc = mountsys(\"SYSTEM\")\n";
    te += "}\n\n";

    te += "if (rc) {\n";
    te += "    println(\"SYSTEM mount failed\", rc)\n";
    te += "    pause()\n";
    te += "    cleanup()\n";
    te += "    exit()\n";
    te += "}\n\n";

    te += "bis = \"bis:/save/8000000000000010\"\n";
    te += "if (!fsexists(bis)) {\n";
    te += "    println(\"Save 8000000000000010 not found on SYSTEM\")\n";
    te += "    pause()\n";
    te += "    cleanup()\n";
    te += "    exit()\n";
    te += "}\n\n";

    te += "pkg = \"sd:/config/kefir/account_links/" + pkg_name + "\"\n";
    te += "rollback = combinepath(pkg, \"rollback_" + std::string(stamp) + "\")\n";
    te += "mkdir(rollback)\n";
    te += "mkdir(combinepath(rollback, \"baas\"))\n";
    te += "mkdir(combinepath(rollback, \"nas\"))\n\n";

    te += "saveObj = readsave(bis)\n";
    te += "baasListing = saveObj.readdir(\"/baas\")\n";
    te += "if (baasListing.result) {\n";
    te += "    println(\"Error: cannot read /baas in save 0010\", baasListing.result)\n";
    te += "    rep = \"Apply failed\\ntarget_uid=" + target_rfc + "\\nerror=cannot read /baas in save 0010\\n\"\n";
    te += "    writefile(combinepath(pkg, \"result_apply.txt\"), rep)\n";
    te += "    pause()\n";
    te += "    cleanup()\n";
    te += "    exit()\n";
    te += "}\n\n";

    te += "nasListing = saveObj.readdir(\"/nas\")\n";
    te += "if (nasListing.result) {\n";
    te += "    println(\"Error: cannot read /nas in save 0010\", nasListing.result)\n";
    te += "    rep = \"Apply failed\\ntarget_uid=" + target_rfc + "\\nerror=cannot read /nas in save 0010\\n\"\n";
    te += "    writefile(combinepath(pkg, \"result_apply.txt\"), rep)\n";
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
    te += "        rep = \"Apply succeeded\\ntarget_uid=" + target_rfc + R"(\nnas_written=" + nasWritten + "\nrollback=" + rollback + "\n")" + "\n";
    te += "        writefile(combinepath(pkg, \"result_apply.txt\"), rep)\n";
    te += "        println(\"Official Nintendo Account link applied successfully.\")\n";
    te += "        println(\"Wrote baas file and\", nasWritten, \"NAS file(s).\")\n";
    te += "        println(\"Rollback backup saved to:\", rollback)\n";
    te += "    } .else() {\n";
    te += "        println(\"Commit failed with error:\", commitRc)\n";
    te += "        rep = \"Apply failed\\ntarget_uid=" + target_rfc + R"(\nnas_written=" + nasWritten + "\ncommit_error=" + commitRc + "\nrollback=" + rollback + "\n")" + "\n";
    te += "        writefile(combinepath(pkg, \"result_apply.txt\"), rep)\n";
    te += "    }\n";
    te += "} .else() {\n";
    te += "    println(\"Errors occurred during apply. Changes were NOT committed.\")\n";
    te += "    rep = \"Apply failed\\ntarget_uid=" + target_rfc + R"(\nnas_written=" + nasWritten + "\nerrors=" + errors + "\nrollback=" + rollback + "\n")" + "\n";
    te += "    writefile(combinepath(pkg, \"result_apply.txt\"), rep)\n";
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

} // namespace sphaira::account_link
