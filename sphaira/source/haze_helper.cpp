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
#endif

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
                g_mtp_new_transfer = true;
                if (!g_mtp_ui_alive) {
                    g_mtp_ui_alive = true;
                    trigger_ui = true;
                }
            }

            if (trigger_ui) {
                ueventClear(&g_mtp_done_event);
                evman::push(evman::FunctionalEventData {
                    []() {
                        log_write("[MTP-UI] UI event triggered, creating ProgressBox\n");
                        std::string init_filename;
                        {
                            SCOPED_MUTEX(&g_mtp_ui_mutex);
                            init_filename = g_mtp_current_filename;
                        }
                        App::PushTransfer(std::make_unique<ui::ProgressBox>(0, "Copying via MTP"_i18n, MtpParentDir(init_filename), [init_filename](auto pbox) -> Result {
                            std::string current_filename;
                            {
                                SCOPED_MUTEX(&g_mtp_ui_mutex);
                                g_mtp_pbox = pbox;
                                current_filename = g_mtp_current_filename;
                                if (current_filename == init_filename) {
                                    g_mtp_new_transfer = false;
                                }
                            }
                            pbox->SetTitle(MtpParentDir(current_filename));
                            pbox->NewTransferForce(GetLastComponent(current_filename.c_str()));
                            
                            while (!pbox->ShouldExit() && !g_should_exit) {
                                auto rc = waitSingle(waiterForUEvent(&g_mtp_done_event), 100000000ULL); // 100ms
                                if (R_SUCCEEDED(rc)) {
                                    bool has_new = false;
                                    for (int i = 0; i < 15; i++) {
                                        if (pbox->ShouldExit() || g_should_exit) break;
                                        if (g_mtp_new_transfer) {
                                            has_new = true;
                                            g_mtp_new_transfer = false;
                                            break;
                                        }
                                        svcSleepThread(100000000ULL); // 100ms
                                    }
                                    if (has_new) {
                                        ueventClear(&g_mtp_done_event);
                                        std::string next_filename;
                                        {
                                            SCOPED_MUTEX(&g_mtp_ui_mutex);
                                            next_filename = g_mtp_current_filename;
                                        }
                                        // address line = folder, current line = file name.
                                        pbox->SetTitle(MtpParentDir(next_filename));
                                        pbox->NewTransferForce(GetLastComponent(next_filename.c_str()));
                                        continue;
                                    }
                                    break;
                                }
                                
                                if (g_mtp_new_transfer) {
                                    std::string next_filename;
                                    {
                                        SCOPED_MUTEX(&g_mtp_ui_mutex);
                                        g_mtp_new_transfer = false;
                                        next_filename = g_mtp_current_filename;
                                    }
                                    pbox->SetTitle(MtpParentDir(next_filename));
                                    pbox->NewTransferForce(GetLastComponent(next_filename.c_str()));
                                }
                            }
                            
                            R_SUCCEED();
                        }, [](Result rc) {
                            SCOPED_MUTEX(&g_mtp_ui_mutex);
                            g_mtp_pbox = nullptr;
                            g_mtp_ui_alive = false;
                        }));
                    }
                }, false);
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

void Exit() {
    SCOPED_MUTEX(&g_mutex);
    if (!g_is_running) {
        return;
    }

    g_is_running = false;
    g_should_exit = true;
    ueventSignal(&g_mtp_done_event);
    {
        SCOPED_MUTEX(&g_mtp_ui_mutex);
        if (g_mtp_pbox) {
            g_mtp_pbox->RequestExit();
        }
    }
    ::haze::Exit();
    g_fs_entries.clear();

    log_write("[MTP] exitied\n");

    // hand the port back to host so a flash drive is visible again.
    if (App::GetWriteProtect()) {
        usbHsFsSetFileSystemMountFlags(UsbHsFsMountFlags_ReadOnly);
    }
    usbHsFsInitialize(1);
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