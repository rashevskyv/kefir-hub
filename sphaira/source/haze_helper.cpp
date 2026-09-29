#include "haze/haze_internal.hpp"

#include "app.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "evman.hpp"
#include "i18n.hpp"
#include "ui/progress_box.hpp"
#include <usbhsfs.h>

#include <functional>
#include <memory>
#include <string>
#include <haze.h>
#if ENABLE_NETWORK_INSTALL
#include "ui/menus/dbi_menu.hpp"
#endif

namespace sphaira::haze {

#if ENABLE_NETWORK_INSTALL
// ive given up with good names.
void on_thing() {
    log_write("[MTP] doing on_thing\n");
    SCOPED_MUTEX(&g_shared_data.mutex);
    log_write("[MTP] locked on_thing\n");

    if (!g_shared_data.in_progress) {
        if (!g_shared_data.current_file.empty()) {
            log_write("[MTP] pushing new file data\n");
            if (!g_shared_data.on_start || !g_shared_data.on_start(g_shared_data.current_file.c_str())) {
                g_shared_data.current_file.clear();
            } else {
                log_write("[MTP] success on new file push\n");
                g_shared_data.in_progress = true;
            }
        }
    }
}

bool HasActiveTransfer() {
    SCOPED_MUTEX(&g_shared_data.mutex);
    return !g_shared_data.current_file.empty() || g_shared_data.in_progress;
}
#else
bool HasActiveTransfer() {
    return false;
}
#endif

namespace {

void StartMtpProgressBox() {
    evman::push(evman::FunctionalEventData {
        []() {
            log_write("[MTP-UI] UI event triggered, creating ProgressBox\n");
            std::string init_filename;
            {
                SCOPED_MUTEX(&g_mtp_ui_mutex);
                if (g_should_exit || (!g_mtp_transfer_active && g_mtp_transfer_seq == g_mtp_handled_seq)) {
                    g_mtp_ui_alive = false;
                    return;
                }
                init_filename = g_mtp_current_filename;
            }

            // The ProgressBox worker starts in its constructor, before PushTransfer
            // decides whether the app can own it.
            auto push_state = std::make_shared<std::atomic<int>>(0);
            auto pbox_ptr = std::make_unique<ui::ProgressBox>(0, "Copying via MTP"_i18n, MtpParentDir(init_filename), [push_state](auto pbox) -> Result {
                while (push_state->load() == 0 && !pbox->ShouldExit()) {
                    svcSleepThread(1000000ULL);
                }
                if (push_state->load() != 1) {
                    R_SUCCEED();
                }
                std::string current_filename;
                u64 last_seq = 0;
                bool is_active = false;
                {
                    SCOPED_MUTEX(&g_mtp_ui_mutex);
                    g_mtp_pbox = pbox;
                    current_filename = g_mtp_current_filename;
                    last_seq = g_mtp_transfer_seq;
                    is_active = g_mtp_transfer_active;
                }
                if (!current_filename.empty()) {
                    pbox->SetTitle(MtpParentDir(current_filename));
                    pbox->NewTransferForce(GetLastComponent(current_filename.c_str()));
                }

                u64 idle_start = is_active ? 0 : armTicksToNs(armGetSystemTick());
                constexpr u64 IDLE_TIMEOUT_NS = 1500000000ULL; // 1.5s

                while (!pbox->ShouldExit() && !g_should_exit) {
                    waitSingle(waiterForUEvent(&g_mtp_done_event), 50000000ULL); // 50ms
                    if (pbox->ShouldExit() || g_should_exit) {
                        break;
                    }

                    bool update_label = false;
                    std::string filename;

                    {
                        SCOPED_MUTEX(&g_mtp_ui_mutex);
                        if (g_mtp_transfer_seq != last_seq) {
                            last_seq = g_mtp_transfer_seq;
                            filename = g_mtp_current_filename;
                            update_label = true;
                        }
                        is_active = g_mtp_transfer_active;
                    }

                    if (update_label && !filename.empty()) {
                        pbox->SetTitle(MtpParentDir(filename));
                        pbox->NewTransferForce(GetLastComponent(filename.c_str()));
                    }

                    const u64 now = armTicksToNs(armGetSystemTick());
                    if (update_label) {
                        idle_start = is_active ? 0 : now;
                    } else if (is_active) {
                        idle_start = 0;
                    } else {
                        if (idle_start == 0) {
                            idle_start = now;
                        } else if (now - idle_start >= IDLE_TIMEOUT_NS) {
                            SCOPED_MUTEX(&g_mtp_ui_mutex);
                            if (!g_mtp_transfer_active && g_mtp_transfer_seq == last_seq) {
                                break;
                            }
                        }
                    }
                }

                {
                    SCOPED_MUTEX(&g_mtp_ui_mutex);
                    if (g_mtp_handled_seq < last_seq) {
                        g_mtp_handled_seq = last_seq;
                    }
                    g_mtp_pbox = nullptr;
                }

                R_SUCCEED();
            }, [push_state](Result rc) {
                bool relaunch = false;
                {
                    SCOPED_MUTEX(&g_mtp_ui_mutex);
                    g_mtp_pbox = nullptr;
                    if (push_state->load() == 1 && !g_should_exit && (g_mtp_transfer_active || g_mtp_transfer_seq != g_mtp_handled_seq)) {
                        g_mtp_ui_alive = true;
                        relaunch = true;
                    } else {
                        g_mtp_ui_alive = false;
                    }
                }

                if (relaunch) {
                    StartMtpProgressBox();
                }
            });

            if (!App::PushTransfer(std::move(pbox_ptr))) {
                push_state->store(-1);
                SCOPED_MUTEX(&g_mtp_ui_mutex);
                g_mtp_ui_alive = false;
            } else {
                push_state->store(1);
            }
        }
    }, false);
}

} // namespace

void haze_callback(const ::haze::CallbackData *data) {
    if (g_should_exit) {
        return;
    }

    auto& e = *data;

    switch (e.type) {
        case ::haze::CallbackType_OpenSession:
            log_write("[LIBHAZE] Opening Session\n");
            App::Notify("MTP connected"_i18n);
            if (App::IsApplet()) {
                App::Notify("Applet Mode has limited memory. NSZ packages are unlikely to install. Use Title Mode for reliable installation."_i18n);
            }
            break;
        case ::haze::CallbackType_CloseSession:
            log_write("[LIBHAZE] Closing Session\n");
            App::Notify("MTP disconnected"_i18n);
            {
                SCOPED_MUTEX(&g_mtp_ui_mutex);
                g_mtp_transfer_active = false;
                g_mtp_handled_seq = g_mtp_transfer_seq;
                if (g_mtp_pbox) {
                    g_mtp_pbox->RequestExit();
                }
            }
            ueventSignal(&g_mtp_done_event);
            if (auto session = App::GetActiveInstallSession()) {
                if (session->GetOrigin() == ui::menu::dbi::TransportOrigin::Mtp
                    && session->GetState() == ui::menu::dbi::State::Installing
                    && session->AllPackagesTerminal()) {
                    session->TransitionToSummary();
                }
            }
            break;

        case ::haze::CallbackType_CreateFile: log_write("[LIBHAZE] Creating File: %s\n", e.file.filename); break;
        case ::haze::CallbackType_DeleteFile: log_write("[LIBHAZE] Deleting File: %s\n", e.file.filename); break;

        case ::haze::CallbackType_RenameFile: log_write("[LIBHAZE] Rename File: %s -> %s\n", e.rename.filename, e.rename.newname); break;
        case ::haze::CallbackType_RenameFolder: log_write("[LIBHAZE] Rename Folder: %s -> %s\n", e.rename.filename, e.rename.newname); break;

        case ::haze::CallbackType_CreateFolder: log_write("[LIBHAZE] Creating Folder: %s\n", e.file.filename); break;
        case ::haze::CallbackType_DeleteFolder: log_write("[LIBHAZE] Deleting Folder: %s\n", e.file.filename); break;

        case ::haze::CallbackType_ReadBegin:
        case ::haze::CallbackType_WriteBegin: {
            log_write("[LIBHAZE] Transfer Begin: %s \n", e.file.filename);
#if ENABLE_NETWORK_INSTALL
            {
                SCOPED_MUTEX(&g_shared_data.mutex);
                if (g_shared_data.in_progress && !g_shared_data.current_file.empty()) {
                    if (std::strstr(e.file.filename, g_shared_data.current_file.c_str()) != nullptr) {
                        break;
                    }
                }
            }
#endif
            bool trigger_ui = false;
            {
                SCOPED_MUTEX(&g_mtp_ui_mutex);
                g_mtp_current_filename = e.file.filename;
                g_mtp_transfer_active = true;
                g_mtp_transfer_seq++;
                if (!g_mtp_ui_alive) {
                    g_mtp_ui_alive = true;
                    trigger_ui = true;
                }
            }
            ueventSignal(&g_mtp_done_event);

            if (trigger_ui) {
                StartMtpProgressBox();
            }
            break;
        }

        case ::haze::CallbackType_ReadProgress:
        case ::haze::CallbackType_WriteProgress: {
            SCOPED_MUTEX(&g_mtp_ui_mutex);
            if (g_mtp_pbox) {
                const s64 offset = e.progress.offset;
                const s64 chunk = e.progress.size;
                const s64 transferred = (offset >= 0 && chunk >= 0 && offset <= INT64_MAX - chunk) ? (offset + chunk) : offset;
                const s64 total = e.progress.total > 0 ? e.progress.total : 0;
                g_mtp_pbox->UpdateTransferForce(transferred, total);
            }
            break;
        }

        case ::haze::CallbackType_ReadEnd:
        case ::haze::CallbackType_WriteEnd: {
            log_write("[LIBHAZE] Transfer Finished: %s\n", e.file.filename);
            {
                SCOPED_MUTEX(&g_mtp_ui_mutex);
                g_mtp_transfer_active = false;
            }
            ueventSignal(&g_mtp_done_event);
            break;
        }
    }

    App::NotifyFlashLed();
}

bool Init() {
    SCOPED_MUTEX(&g_mutex);
    if (g_is_running) {
        log_write("[MTP] already enabled, cannot open\n");
        return false;
    }

    struct MtpStorageDef {
        bool enabled;
        std::string custom_name;
        const char* default_name;
        std::function<std::shared_ptr<::haze::FileSystemProxyImpl>(const char* display_name)> factory;
    };

    std::vector<MtpStorageDef> storage_defs;
    storage_defs.push_back({
        App::GetMtpShowSd(),
        App::GetMtpNameSd(),
        "microSD card",
        [](const char* display_name) {
            return MakeFsProxy(std::make_unique<fs::FsNativeSd>(), "", display_name);
        }
    });

#if ENABLE_NETWORK_INSTALL
    storage_defs.push_back({
        App::GetMtpShowInstall(),
        App::GetMtpNameInstall(),
        "Install (NSP, XCI, NSZ, XCZ)",
        [](const char* display_name) {
            return MakeFsInstallProxy("install", display_name);
        }
    });
#endif

    storage_defs.push_back({
        App::GetMtpShowSaves(),
        "", // no custom-name option for the Saves drive.
        "Saves",
        [](const char* display_name) {
            return MakeFsSaveProxy("saves", display_name);
        }
    });

    storage_defs.push_back({
        App::GetMtpShowRawSaves(),
        "",
        "NAND Saves (USER:/save)",
        [](const char* display_name) {
            return MakeFsProxy(std::make_unique<fs::FsNativeBis>(FsBisPartitionId_User), "user_save", display_name, "/save");
        }
    });

    storage_defs.push_back({
        App::GetMtpShowRawSystemSaves(),
        "",
        "NAND System Saves (SYSTEM:/save)",
        [](const char* display_name) {
            return MakeFsProxy(std::make_unique<fs::FsNativeBis>(FsBisPartitionId_System), "system_save", display_name, "/save");
        }
    });

    storage_defs.push_back({
        App::GetMtpShowGames(),
        "", // no custom-name option for the Games drive.
        "Games (read-only)",
        [](const char* display_name) {
            return MakeFsGameProxy("games", display_name);
        }
    });

    // user-configured extra folders (Settings -> Network -> MTP), each exposed
    // as its own storage rooted at that folder. names must be unique so libhaze
    // can address them; display defaults to the folder's leaf name.
    {
        int idx = 0;
        for (const auto& folder : App::GetMtpFolders()) {
            const char* leaf = std::strrchr(folder.c_str(), '/');
            std::string display = (leaf && leaf[1]) ? std::string(leaf + 1) : folder;
            std::string internal = "mntf" + std::to_string(idx++);
            storage_defs.push_back({
                true,
                std::move(display),
                "Folder",
                [folder, internal](const char* display_name) {
                    return MakeFsProxy(std::make_unique<fs::FsNativeSd>(), internal.c_str(), display_name, folder.c_str());
                }
            });
        }
    }

    // pinned mounts (folders or virtual mounts) requested from the file manager.
    // each needs its own internal id, or two storages would fight over one
    // devoptab name.
    for (const auto& pin : g_pinned) {
        storage_defs.push_back({
            true,
            pin.display_name,
            "Mounted",
            [pin](const char* display_name) {
                return MakeFsProxy(pin.fs_factory(), pin.internal.c_str(), display_name, pin.base_path.c_str());
            }
        });
    }

    for (const auto& def : storage_defs) {
        if (def.enabled) {
            const char* name = def.custom_name.empty() ? def.default_name : def.custom_name.c_str();
            g_fs_entries.emplace_back(def.factory(name));
        }
    }

    if (g_fs_entries.empty()) {
        log_write("[MTP] No MTP storages enabled\n");
        App::Notify("No MTP storages enabled"_i18n);
        return false;
    }

    ueventCreate(&g_mtp_done_event, true);
    {
        SCOPED_MUTEX(&g_mtp_ui_mutex);
        g_mtp_ui_alive = false;
        g_mtp_pbox = nullptr;
        g_mtp_transfer_active = false;
        g_mtp_transfer_seq = 0;
        g_mtp_handled_seq = 0;
        g_mtp_current_filename.clear();
    }

    g_should_exit = false;
    // always drop the host stack, even if the "USB storage" setting is off:
    // detection keeps usbhsfs up whenever MTP is not actually serving a PC.
    if (usbHsFsGetStatusChangeUserEvent()) {
        usbHsFsExit();
    }

    if (!::haze::Initialize(haze_callback, THREAD_PRIO, THREAD_CORE, g_fs_entries)) {
        g_fs_entries.clear();
        if (App::GetWriteProtect()) {
            usbHsFsSetFileSystemMountFlags(UsbHsFsMountFlags_ReadOnly);
        }
        usbHsFsInitialize(1);
        return false;
    }

    log_write("[MTP] started in %s mode\n", App::IsApplet() ? "applet" : "title");
    return g_is_running = true;
}

void Exit(bool reinit_usb_host) {
    SCOPED_MUTEX(&g_mutex);
    if (!g_is_running) {
        return;
    }

    g_is_running = false;
    g_should_exit = true;
    ueventSignal(&g_mtp_done_event);
    {
        SCOPED_MUTEX(&g_mtp_ui_mutex);
        g_mtp_transfer_active = false;
        g_mtp_handled_seq = g_mtp_transfer_seq;
        if (g_mtp_pbox) {
            g_mtp_pbox->RequestExit();
        }
    }
    ::haze::Exit();
    g_fs_entries.clear();

    log_write("[MTP] exitied\n");

    if (reinit_usb_host) {
        // hand the port back to host so a flash drive is visible again.
        if (App::GetWriteProtect()) {
            usbHsFsSetFileSystemMountFlags(UsbHsFsMountFlags_ReadOnly);
        }
        usbHsFsInitialize(1);
    }
}

bool IsRunning() {
    SCOPED_MUTEX(&g_mutex);
    return g_is_running;
}

bool MountFs(std::vector<PinnedMount> mounts) {
    // stop any running session, install the pinned storages, then (re)start so
    // the PC re-enumerates with the new storages present.
    Exit();
    g_pinned = std::move(mounts);
    for (size_t i = 0; i < g_pinned.size(); i++) {
        g_pinned[i].internal = "mnt" + std::to_string(i);
    }
    return Init();
}

void UnmountPinned() {
    g_pinned.clear();
    Exit();
}

bool HasPinned() {
    return !g_pinned.empty();
}

std::string GetPinnedName() {
    std::string out;
    for (const auto& pin : g_pinned) {
        if (!out.empty()) out += ", ";
        out += pin.display_name;
    }
    return out;
}

} // namespace sphaira::haze
