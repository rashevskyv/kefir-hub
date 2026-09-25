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
bool g_daemons_terminated = false;

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
    const auto pm_rc = pmshellInitialize();
    log_write_error("[ACC_DIAG] pmshellInitialize rc=0x%X", pm_rc);
    if (R_SUCCEEDED(pm_rc)) {
        ON_SCOPE_EXIT(pmshellExit());
        // Do not kill ns (0015/001F) or friends (000E): from the Hub applet that
        // User-Breaks am (0100000000000023) and can bootloop after ApplyLink.
        const auto bcat_rc = pmshellTerminateProgram(0x010000000000000CULL); // BCAT
        log_write_error("[ACC_DIAG] terminate BCAT rc=0x%X", bcat_rc);
        const auto acc_rc = pmshellTerminateProgram(0x010000000000001EULL);  // ACCOUNT
        log_write_error("[ACC_DIAG] terminate ACCOUNT rc=0x%X", acc_rc);
        const auto olsc_rc = pmshellTerminateProgram(0x010000000000003EULL); // OLSC
        log_write_error("[ACC_DIAG] terminate OLSC rc=0x%X", olsc_rc);
        g_daemons_terminated = true;
        log_write("[ACC] terminated BCAT/ACCOUNT/OLSC (reboot needed; ns/friends kept)\n");
    }
}

auto OpenAccountSaveForExport(bool may_terminate_account) -> fs::FsNativeSave {
    auto save = TryOpenAccountSave();
    if (R_SUCCEEDED(save.GetFsOpenResult())) {
        return save;
    }
    if (!may_terminate_account) {
        log_write("[ACC] account save read-only failed 0x%X (no terminate)\n", save.GetFsOpenResult());
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


auto UidHex(const AccountUid& uid) -> std::string {
    return UidDashedRfc(uid);
}


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
