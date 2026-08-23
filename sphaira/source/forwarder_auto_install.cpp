#include "forwarder_auto_install.hpp"
#include "forwarder_auto_plan.hpp"
#include "app.hpp"
#include "evman.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "image.hpp"
#include "log.hpp"
#include "nacp_util.hpp"
#include "nro.hpp"
#include "owo.hpp"
#include "path_util.hpp"
#include "ui/option_box.hpp"
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

bool IsKefirHubName(const std::string& raw_name) {
    std::string name = raw_name;
    std::ranges::transform(name, name.begin(), [](unsigned char c) { return std::tolower(c); });
    return name.find("kefir") != std::string::npos || name.find("sphaira") != std::string::npos;
}

bool IsOldHomebrewTitle(const std::string& raw_name, u64 tid, u64 kefirhub_tid) {
    if (tid == kefirhub_tid && kefirhub_tid != 0) {
        return false;
    }
    if (IsKefirHubName(raw_name)) {
        return false;
    }

    // Known Homebrew Menu / HBL forwarder IDs. Not Nintendo system titles
    // (0100000000001xxx) — those made every launch look like an old forwarder.
    if (tid == 0x03DB1280BD84000ULL || tid == 0x03DB12780BD84000ULL ||
        tid == 0x050000000000100DULL) {
        return true;
    }

    std::string name = raw_name;
    std::ranges::transform(name, name.begin(), [](unsigned char c) { return std::tolower(c); });

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

auto GetOwnProgramId() -> u64 {
    u64 tid{};
    if (R_FAILED(svcGetInfo(&tid, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0))) {
        return 0;
    }
    return tid;
}

auto TitleName(u64 tid) -> std::string {
    auto control_data = std::make_unique<NsApplicationControlData>();
    u64 actual_size = 0;
    if (R_FAILED(nsGetApplicationControlData(NsApplicationControlSource_Storage, tid, control_data.get(), sizeof(NsApplicationControlData), &actual_size))) {
        return {};
    }
    return nacp_util::GetName(control_data->nacp);
}

auto IsInstalled(u64 tid) -> bool {
    if (!tid || !hosversionAtLeast(2, 0, 0)) {
        return false;
    }
    bool installed = false;
    return R_SUCCEEDED(nsIsAnyApplicationEntityInstalled(tid, &installed)) && installed;
}

template<typename Fn>
void ForEachOldForwarder(u64 kefirhub_tid, u64 skip_tid, Fn&& fn) {
    const u64 known_bad_tids[] = {
        0x03DB1280BD84000ULL,
        0x03DB12780BD84000ULL,
        0x050000000000100DULL,
    };

    for (const u64 bad_tid : known_bad_tids) {
        if (g_stop_requested) {
            return;
        }
        if (bad_tid == kefirhub_tid || bad_tid == skip_tid) {
            continue;
        }
        if (IsInstalled(bad_tid) && IsOldHomebrewTitle(TitleName(bad_tid), bad_tid, kefirhub_tid)) {
            fn(bad_tid);
        }
    }

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
            if (app_id == kefirhub_tid || app_id == skip_tid) {
                continue;
            }
            if (IsOldHomebrewTitle(TitleName(app_id), app_id, kefirhub_tid)) {
                fn(app_id);
            }
        }

        offset += record_count;
    }
}

auto HasOldForwarder(u64 kefirhub_tid, u64 skip_tid) -> bool {
    bool found = false;
    ForEachOldForwarder(kefirhub_tid, skip_tid, [&](u64) {
        found = true;
    });
    return found;
}

void CleanOldInstalledForwarders(u64 kefirhub_tid, u64 skip_tid) {
    ForEachOldForwarder(kefirhub_tid, skip_tid, [](u64 tid) {
        log_write("[ForwarderAuto] Deleting old forwarder title ID: %016lx\n", tid);
        nsDeleteApplicationCompletely(tid);
    });
}

auto ClassifyLaunch(u64 own_tid, u64 kefirhub_tid) -> LaunchSource {
    if (!App::IsApplication()) {
        return LaunchSource::Album;
    }
    const auto name = own_tid ? TitleName(own_tid) : std::string{};
    if ((own_tid && own_tid == kefirhub_tid) || IsKefirHubName(name)) {
        return LaunchSource::NewForwarder;
    }
    if (own_tid && IsOldHomebrewTitle(name, own_tid, kefirhub_tid)) {
        return LaunchSource::OldForwarder;
    }
    return LaunchSource::Album;
}

void NotifyUi(Notice notice) {
    const char* key = nullptr;
    switch (notice) {
    case Notice::OldWillBeRemoved:
        key = "An old Homebrew Menu forwarder is still on the HOME Menu. It can cause errors and will be removed now. Keep using this Kefir Hub icon.";
        break;
    case Notice::UseNewNextTime:
        key = "Kefir Hub now has a HOME Menu icon. Next time, launch from that Kefir Hub icon, not this old one. The old forwarder will then be removed automatically.";
        break;
    case Notice::PreferHomeIcon:
        key = "Launch Kefir Hub from its HOME Menu icon, not Album. A Kefir Hub icon will be installed if needed, and any old Homebrew Menu forwarder will be removed.";
        break;
    case Notice::None:
        return;
    }

    const auto msg = i18n::get(key);
    evman::push(evman::FunctionalEventData{[msg]() {
        App::Push<ui::OptionBox>(msg, "OK"_i18n, [](auto){});
    }}, false);
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
    options.screenshot = true;
    options.video_capture = true;
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
    const u64 own_tid = GetOwnProgramId();

    if (R_FAILED(nsInitialize())) {
        return;
    }
    ON_SCOPE_EXIT(nsExit());

    const auto src = ClassifyLaunch(own_tid, kefirhub_tid);
    // Running from the Kefir Hub icon means it is already installed, even if the
    // path-hash TID does not match the title we are in.
    const bool new_installed = (src == LaunchSource::NewForwarder) || IsInstalled(kefirhub_tid);
    const bool old_installed = HasOldForwarder(kefirhub_tid, own_tid);
    const auto plan = Decide(src, new_installed, old_installed);

    log_write("[ForwarderAuto] launch=%d own=%016lx new=%u old=%u install=%u delete=%u\n",
        (int)src, own_tid, new_installed, old_installed, plan.install_new, plan.delete_old);

    if (g_stop_requested) {
        return;
    }

    if (plan.install_new) {
        InstallKefirHubForwarder(kefirhub_tid);
    }
    if (plan.delete_old) {
        CleanOldInstalledForwarders(kefirhub_tid, own_tid);
    }
    NotifyUi(plan.notice);
}

} // namespace

void StartCheck() {
    if (g_thread_created.exchange(true)) {
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
