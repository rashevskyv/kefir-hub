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
#include <memory>

namespace sphaira::forwarder_auto {
namespace {

std::atomic_bool g_stop_requested{false};
std::atomic_bool g_thread_active{false};
std::atomic_bool g_thread_created{false};
Thread g_check_thread{};

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
            if (!NeedsTitleLookup(app_id)) {
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

auto HasOwnKefirHubIcon(u64 kefirhub_tid, u64 skip_tid) -> bool {
    if (IsInstalled(kefirhub_tid)) {
        return true;
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
                return false;
            }
            const u64 app_id = records[i].application_id;
            if (app_id == skip_tid) {
                continue;
            }
            if ((app_id & 0xFF00000000000000ULL) != 0x0500000000000000ULL) {
                continue;
            }
            if (IsKefirHubName(TitleName(app_id))) {
                return true;
            }
        }

        offset += record_count;
    }
    return false;
}

void CleanStaleOwnForwarders(u64 kefirhub_tid, u64 skip_tid) {
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
            if ((app_id & 0xFF00000000000000ULL) != 0x0500000000000000ULL) {
                continue;
            }
            if (IsStaleOwnForwarder(TitleName(app_id), app_id, kefirhub_tid)) {
                log_write("[ForwarderAuto] Deleting stale Kefir Hub forwarder: %016lx\n", app_id);
                nsDeleteApplicationCompletely(app_id);
            }
        }

        offset += record_count;
    }
}

void NotifyUi(const Plan& plan) {
    const char* key = nullptr;
    switch (plan.notice) {
    case Notice::OldWillBeRemoved:
        key = "An old Homebrew Menu forwarder is still on the HOME Menu. It can cause errors, so it is being removed now. You can keep using this Kefir Hub icon.";
        break;
    case Notice::UseNewNextTime:
        key = plan.install_new
            ? "Kefir Hub is installing a HOME Menu icon so you can launch it without this old Homebrew Menu forwarder. Next time, open that new icon. This old one cannot be removed while you are using it; it will be removed automatically afterwards."
            : "Kefir Hub already has a HOME Menu icon. Next time, launch from that icon. This old Homebrew Menu forwarder cannot be removed while you are using it; it will be removed automatically afterwards.";
        break;
    case Notice::PreferHomeIcon:
        if (plan.install_new && plan.delete_old) {
            key = "Kefir Hub is installing a HOME Menu icon so you can launch it like a normal app, without Album. An old Homebrew Menu forwarder is being removed now because it can cause errors.";
        } else if (plan.install_new) {
            key = "Kefir Hub is installing a HOME Menu icon so you can launch it like a normal app, without Album.";
        } else {
            key = "An old Homebrew Menu forwarder is being removed from the HOME Menu because it can cause errors. Next time, launch Kefir Hub from its HOME Menu icon, not Album.";
        }
        break;
    case Notice::None:
        return;
    }

    const auto msg = i18n::get(key);
    evman::push(evman::FunctionalEventData{[msg]() {
        App::Push<ui::OptionBox>(msg, "OK"_i18n, [](auto){});
    }}, false);
}

auto InstallKefirHubForwarder(u64 kefirhub_tid) -> bool {
    if (g_stop_requested) {
        return false;
    }

    log_write("[ForwarderAuto] Generating and installing KefirHub forwarder (%016lx)...\n", kefirhub_tid);

    const auto exe_path = App::GetExePath();
    OwoConfig config{};
    config.nro_path = exe_path.toString();
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
        return false;
    }
    log_write("[ForwarderAuto] KefirHub forwarder installed successfully (%016lx)\n", kefirhub_tid);
    return true;
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

    const auto own_name = own_tid ? TitleName(own_tid) : std::string{};
    const auto src = ClassifyLaunch(App::IsApplication(), own_tid, kefirhub_tid, own_name);
    const bool new_installed = (src == LaunchSource::NewForwarder) || IsInstalled(kefirhub_tid);
    const bool old_installed = HasOldForwarder(kefirhub_tid, own_tid);
    auto plan = Decide(src, new_installed, old_installed);

    // nxlink / Album argv is not the HOME-icon path. Installing from it would
    // mint a second 0x05 title and stall ns until the NCA is done. If a Kefir
    // Hub icon already exists, leave it.
    if (src == LaunchSource::Album && plan.install_new && HasOwnKefirHubIcon(kefirhub_tid, own_tid)) {
        log_write("[ForwarderAuto] skip install: a Kefir Hub HOME icon already exists\n");
        plan.install_new = false;
        if (!plan.delete_old) {
            plan.notice = Notice::None;
        }
    }

    log_write("[ForwarderAuto] launch=%d own=%016lx new=%u old=%u install=%u delete=%u\n",
        (int)src, own_tid, new_installed, old_installed, plan.install_new, plan.delete_old);

    if (g_stop_requested) {
        return;
    }

    bool have_new = (src == LaunchSource::NewForwarder) || IsInstalled(kefirhub_tid);
    if (plan.install_new) {
        if (InstallKefirHubForwarder(kefirhub_tid)) {
            have_new = true;
        }
    }
    if (plan.delete_old) {
        CleanOldInstalledForwarders(kefirhub_tid, own_tid);
    }
    // Our previous HOME icon (different path-hash TID) is not an HBL forwarder,
    // so the old-forwarder pass skips it. Remove it once the current one exists,
    // except the title we launched from — that one goes on the next launch.
    if (have_new) {
        CleanStaleOwnForwarders(kefirhub_tid, own_tid);
    }
    NotifyUi(plan);
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
