#include "forwarder_auto_install.hpp"
#include "app.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "path_util.hpp"
#include "yati/yati.hpp"
#include "ui/install_progress.hpp"
#include <switch.h>
#include <vector>
#include <string>
#include <cstring>
#include <atomic>
#include <cstdlib>

namespace sphaira::forwarder_auto {
namespace {

std::atomic_bool g_stop_requested{false};
std::atomic_bool g_thread_active{false};
std::atomic_bool g_thread_created{false};
Thread g_check_thread{};

class SilentInstallProgress final : public ui::InstallProgress {
public:
    SilentInstallProgress() {
        ueventCreate(&m_cancel_event, true);
    }

    Result CheckCancelled() override {
        if (g_stop_requested) {
            return 1;
        }
        return 0;
    }

    UEvent* GetInstallCancelEvent() override {
        return &m_cancel_event;
    }

    void SetInstallTitle(const std::string&) override {}
    void SetInstallImage(std::vector<u8>&) override {}
    void SetInstallTransfer(const std::string&) override {}
    void UpdateInstallTransfer(s64, s64) override {}
    void UpdateInstallReadWrite(s64, s64) override {}
    void InstallYield() override {
        svcSleepThread(1000000ULL);
    }
    bool PromptReinstall(const std::string&) override {
        return false;
    }
    void OnInstallSkipped() override {}
    void OnCompatibilityWarning(const ui::CompatibilityWarning&) override {}

    void OnTitleInstalled(u64 title_id) override {
        m_installed_title_id = title_id;
    }

    [[nodiscard]] u64 GetInstalledTitleId() const {
        return m_installed_title_id;
    }

private:
    UEvent m_cancel_event{};
    u64 m_installed_title_id{0};
};

u64 ExtractTitleIdFromName(const std::string& name) {
    auto start = name.find('[');
    while (start != std::string::npos) {
        auto end = name.find(']', start + 1);
        if (end != std::string::npos && end - start - 1 == 16) {
            const std::string hex_str = name.substr(start + 1, 16);
            char* end_ptr = nullptr;
            const u64 tid = std::strtoull(hex_str.c_str(), &end_ptr, 16);
            if (end_ptr == hex_str.c_str() + 16 && tid != 0) {
                return tid;
            }
        }
        start = name.find('[', start + 1);
    }
    return 0;
}

void ThreadFunc(void*) {
    ON_SCOPE_EXIT(g_thread_active = false);

    log_write("[ForwarderAuto] Checking installed forwarders...\n");

    if (App::IsApplication()) {
        u64 current_tid = 0;
        if (hosversionAtLeast(3, 0, 0)) {
            svcGetInfo(&current_tid, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0);
        }
        log_write("[ForwarderAuto] Running in Application mode (TID: %016lx), forwarder already active\n", current_tid);
        return;
    }

    if (R_SUCCEEDED(nsInitialize())) {
        ON_SCOPE_EXIT(nsExit());

        if (hosversionAtLeast(2, 0, 0)) {
            bool is_installed = false;

            // Check standard Homebrew Menu forwarder title IDs
            if (R_SUCCEEDED(nsIsAnyApplicationEntityInstalled(0x010000000000100D, &is_installed)) && is_installed) {
                log_write("[ForwarderAuto] Forwarder 010000000000100D is already installed\n");
                return;
            }
            if (R_SUCCEEDED(nsIsAnyApplicationEntityInstalled(0x050000000000100D, &is_installed)) && is_installed) {
                log_write("[ForwarderAuto] Forwarder 050000000000100D is already installed\n");
                return;
            }

            // Check Sphaira's own generated forwarder IDs based on executable path
            u64 hash_data[SHA256_HASH_SIZE / sizeof(u64)]{};
            const auto exe_path = App::GetExePath().toString();
            if (!exe_path.empty()) {
                const auto hash_path = exe_path + exe_path;
                sha256CalculateHash(hash_data, hash_path.data(), hash_path.length());
                const u64 owo_tid = 0x0500000000000000 | (hash_data[0] & 0x00FFFFFFFFFFF000);
                const u64 owo_old_tid = 0x0100000000000000 | (hash_data[0] & 0x00FFFFFFFFFFF000);

                if (R_SUCCEEDED(nsIsAnyApplicationEntityInstalled(owo_tid, &is_installed)) && is_installed) {
                    log_write("[ForwarderAuto] Forwarder %016lx is already installed\n", owo_tid);
                    return;
                }
                if (R_SUCCEEDED(nsIsAnyApplicationEntityInstalled(owo_old_tid, &is_installed)) && is_installed) {
                    log_write("[ForwarderAuto] Forwarder %016lx is already installed\n", owo_old_tid);
                    return;
                }
            }
        }
    }

    if (g_stop_requested) {
        return;
    }

    log_write("[ForwarderAuto] Forwarder not found, scanning /Games for Homebrew menu*.nsp...\n");

    fs::FsNativeSd sd;
    fs::Dir dir;
    std::string nsp_path;
    u64 nsp_tid{0};

    if (R_SUCCEEDED(sd.OpenDirectory("/Games", FsDirOpenMode_ReadFiles, &dir))) {
        std::vector<FsDirectoryEntry> entries;
        if (R_SUCCEEDED(dir.ReadAll(entries))) {
            for (const auto& entry : entries) {
                if (g_stop_requested) {
                    return;
                }
                if (path::StartsWithIC(entry.name, "Homebrew menu") && path::EndsWithIC(entry.name, ".nsp")) {
                    nsp_path = std::string("/Games/") + entry.name;
                    nsp_tid = ExtractTitleIdFromName(entry.name);
                    log_write("[ForwarderAuto] Found NSP: %s (TID: %016lx)\n", nsp_path.c_str(), nsp_tid);
                    break;
                }
            }
        }
    }

    if (nsp_path.empty() || g_stop_requested) {
        if (nsp_path.empty()) {
            log_write("[ForwarderAuto] No matching Homebrew menu NSP found in /Games\n");
        }
        return;
    }

    // If the found NSP title is already installed on the system, skip reinstallation
    if (nsp_tid != 0 && R_SUCCEEDED(nsInitialize())) {
        ON_SCOPE_EXIT(nsExit());
        bool is_installed = false;
        if (R_SUCCEEDED(nsIsAnyApplicationEntityInstalled(nsp_tid, &is_installed)) && is_installed) {
            log_write("[ForwarderAuto] Found NSP title %016lx is already installed, skipping\n", nsp_tid);
            return;
        }
    }

    log_write("[ForwarderAuto] Starting silent background installation of %s\n", nsp_path.c_str());

    SilentInstallProgress progress;
    yati::ConfigOverride override{};
    override.skip_if_already_installed = 1;

    const auto rc = yati::InstallFromFile(&progress, &sd, nsp_path, override);
    if (R_FAILED(rc)) {
        log_write("[ForwarderAuto] Failed to install forwarder: 0x%X\n", rc);
        return;
    }

    log_write("[ForwarderAuto] Forwarder installed successfully (TID: %016lx)\n", progress.GetInstalledTitleId());
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
