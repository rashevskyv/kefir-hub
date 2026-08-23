#include "forwarder_auto_install.hpp"
#include "app.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "path_util.hpp"
#include "owo.hpp"
#include "nro.hpp"
#include "image.hpp"
#include "nacp_util.hpp"
#include <switch.h>
#include <vector>
#include <string>
#include <cstring>
#include <atomic>
#include <algorithm>
#include <cctype>
#include <memory>

namespace sphaira::forwarder_auto {
namespace {

std::atomic_bool g_stop_requested{false};
std::atomic_bool g_thread_active{false};
std::atomic_bool g_thread_created{false};
Thread g_check_thread{};

bool IsOldHomebrewTitle(const std::string& raw_name, u64 tid, u64 kefirhub_tid) {
    if (tid == kefirhub_tid && kefirhub_tid != 0) {
        return false;
    }

    // Specific known Title IDs for Homebrew Menu / HBL forwarders
    if (tid == 0x03DB1280BD84000ULL || tid == 0x03DB12780BD84000ULL ||
        tid == 0x010000000000100DULL || tid == 0x050000000000100DULL) {
        return true;
    }

    std::string name = raw_name;
    std::ranges::transform(name, name.begin(), [](unsigned char c) { return std::tolower(c); });

    // Never delete KefirHub or Sphaira itself
    if (name.find("kefir") != std::string::npos || name.find("sphaira") != std::string::npos) {
        return false;
    }

    if (name == "hbm" || name == "hbl" || name == "hbmenu" || name == "hblauncher" || name == "nx-hbmenu") {
        return true;
    }

    if (name.find("homebrew menu") != std::string::npos ||
        name.find("homebrew launcher") != std::string::npos ||
        name.find("hblauncher") != std::string::npos ||
        name.find("hbmenu") != std::string::npos ||
        name.find("nx-hbmenu") != std::string::npos) {
        return true;
    }

    return false;
}

void CleanOldInstalledForwarders(u64 kefirhub_tid) {
    if (R_FAILED(nsInitialize())) {
        return;
    }
    ON_SCOPE_EXIT(nsExit());

    // 1. Explicit deletion of known Title IDs
    const u64 known_bad_tids[] = {
        0x03DB1280BD84000ULL,
        0x03DB12780BD84000ULL,
        0x010000000000100DULL,
        0x050000000000100DULL,
    };

    for (const u64 bad_tid : known_bad_tids) {
        if (g_stop_requested) {
            return;
        }
        if (bad_tid == kefirhub_tid) {
            continue;
        }
        bool is_installed = false;
        if (hosversionAtLeast(2, 0, 0)) {
            if (R_SUCCEEDED(nsIsAnyApplicationEntityInstalled(bad_tid, &is_installed)) && is_installed) {
                log_write("[ForwarderAuto] Deleting old forwarder title ID: %016lx\n", bad_tid);
                nsDeleteApplicationCompletely(bad_tid);
            }
        }
    }

    // 2. Enumerate installed applications to find any other Homebrew Menu / HBL forwarders by name
    constexpr s32 CHUNK_SIZE = 32;
    std::vector<NsApplicationRecord> records(CHUNK_SIZE);
    s32 offset = 0;

    while (!g_stop_requested) {
        s32 record_count = 0;
        if (R_FAILED(nsListApplicationRecord(records.data(), records.size(), offset, &record_count)) || record_count <= 0) {
            break;
        }

        for (s32 i = 0; i < record_count; i++) {
            if (g_stop_requested) {
                return;
            }
            const u64 app_id = records[i].application_id;
            if (app_id == kefirhub_tid) {
                continue;
            }

            auto control_data = std::make_unique<NsApplicationControlData>();
            u64 actual_size = 0;
            if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, app_id, control_data.get(), sizeof(NsApplicationControlData), &actual_size))) {
                std::string title_name = nacp_util::GetName(control_data->nacp);
                if (IsOldHomebrewTitle(title_name, app_id, kefirhub_tid)) {
                    log_write("[ForwarderAuto] Deleting old forwarder title %016lx (%s)\n", app_id, title_name.c_str());
                    nsDeleteApplicationCompletely(app_id);
                }
            }
        }

        offset += record_count;
    }
}

void InstallKefirHubForwarder(u64 kefirhub_tid) {
    if (g_stop_requested) {
        return;
    }

    log_write("[ForwarderAuto] Generating and installing KefirHub forwarder (%016lx)...\n", kefirhub_tid);

    const auto exe_path = App::GetExePath();
    OwoConfig config{};
    config.nro_path = exe_path.toString();
    config.args = exe_path.toString();
    nro_get_nacp(exe_path, config.nacp);
    config.icon = nro_get_icon(exe_path);
    if (config.icon.empty()) {
        config.icon = ImageGetDefaultIcon();
    }

    config.name = "Kefir Hub";
    config.author = "rashevskyv";
    std::string nro_name = nacp_util::GetName(config.nacp);
    if (!nro_name.empty()) {
        config.name = nro_name;
    }
    std::string nro_author = nacp_util::GetAuthor(config.nacp);
    if (!nro_author.empty()) {
        config.author = nro_author;
    }

    ForwarderOptions options{};
    options.address_space = ForwarderAddressSpace::Bit39;
    options.screenshot = false;
    options.video_capture = false;
    options.profile_selection = false;
    options.svc_debug_mode = ForwarderSvcDebugMode::Disabled;
    config.options = options;

    const auto rc = install_forwarder(nullptr, config, NcmStorageId_SdCard);
    if (R_FAILED(rc)) {
        log_write("[ForwarderAuto] Failed to install KefirHub forwarder: 0x%X\n", rc);
    } else {
        log_write("[ForwarderAuto] KefirHub forwarder installed successfully (%016lx)\n", kefirhub_tid);
    }
}

void ThreadFunc(void*) {
    ON_SCOPE_EXIT(g_thread_active = false);

    const auto exe_path = App::GetExePath().toString();
    if (exe_path.empty()) {
        return;
    }

    u64 hash_data[SHA256_HASH_SIZE / sizeof(u64)]{};
    const auto hash_path = exe_path + exe_path;
    sha256CalculateHash(hash_data, hash_path.data(), hash_path.length());
    const u64 kefirhub_tid = 0x0500000000000000 | (hash_data[0] & 0x00FFFFFFFFFFF000);

    // Fast check: if KefirHub forwarder is ALREADY installed, exit immediately!
    // Zero overhead, no database scans, no deletions.
    if (R_SUCCEEDED(nsInitialize())) {
        ON_SCOPE_EXIT(nsExit());
        if (hosversionAtLeast(2, 0, 0)) {
            bool is_installed = false;
            if (R_SUCCEEDED(nsIsAnyApplicationEntityInstalled(kefirhub_tid, &is_installed)) && is_installed) {
                log_write("[ForwarderAuto] KefirHub forwarder %016lx is already installed, nothing to clean\n", kefirhub_tid);
                return;
            }
        }
    }

    if (g_stop_requested) {
        return;
    }

    log_write("[ForwarderAuto] KefirHub forwarder not found (%016lx). Cleaning old forwarders and installing...\n", kefirhub_tid);

    // 1. Delete old/incompatible Homebrew Menu / HBL forwarders
    CleanOldInstalledForwarders(kefirhub_tid);

    // 2. Create and install KefirHub forwarder
    InstallKefirHubForwarder(kefirhub_tid);
}

} // namespace

void StartCheck() {
    if (g_thread_created.exchange(true)) {
        return;
    }

    if (App::IsApplication()) {
        log_write("[ForwarderAuto] Running in Application mode, forwarder check skipped\n");
        g_thread_created = false;
        return;
    }

    g_stop_requested = false;
    g_thread_active = true;
    if (R_FAILED(threadCreate(&g_check_thread, ThreadFunc, nullptr, nullptr, 1024 * 64, PRIO_PREEMPTIVE, -2))) {
        log_write("[ForwarderAuto] failed to create forwarder check thread\n");
        g_thread_active = false;
        g_thread_created = false;
        return;
    }

    if (R_FAILED(threadStart(&g_check_thread))) {
        threadClose(&g_check_thread);
        log_write("[ForwarderAuto] failed to start forwarder check thread\n");
        g_thread_active = false;
        g_thread_created = false;
        return;
    }
}

void StopCheck() {
    if (g_thread_created.exchange(false)) {
        g_stop_requested = true;
        threadWaitForExit(&g_check_thread);
        threadClose(&g_check_thread);
        g_thread_active = false;
    }
}

} // namespace sphaira::forwarder_auto
