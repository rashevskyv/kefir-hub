#include "haze/haze_internal.hpp"

#include "app.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "evman.hpp"
#include "i18n.hpp"
#include "title_info.hpp"
#include "title_export_name.hpp"
#include "title_nsp.hpp"
#include "mtp_games_path.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "ui/menus/homebrew.hpp"
#include "ui/progress_box.hpp"
#include <usbhsfs.h>

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <functional>
#include <haze.h>
#include <haze/results.hpp>

namespace sphaira::haze {

#if ENABLE_NETWORK_INSTALL
struct FsInstallProxy final : FsProxyVfs {
    using FsProxyVfs::FsProxyVfs;

    Result FailedIfNotEnabled() {
        SCOPED_MUTEX(&g_shared_data.mutex);
        if (!g_shared_data.enabled) {
            App::Notify("Please launch MTP install menu before trying to install"_i18n);
            R_THROW(FsError_NotImplemented);
        }
        R_SUCCEED();
    }

    Result IsValidFileType(const char* name) {
        const char* ext = std::strrchr(name, '.');
        if (!ext) {
            R_THROW(FsError_NotImplemented);
        }

        bool found = false;
        for (size_t i = 0; i < std::size(SUPPORTED_EXT); i++) {
            if (!strcasecmp(ext, SUPPORTED_EXT[i])) {
                found = true;
                break;
            }
        }

        if (!found) {
            R_THROW(FsError_NotImplemented);
        }

        R_SUCCEED();
    }

    Result GetTotalSpace(const char *path, s64 *out) override {
        if (App::GetInstallSdEnable()) {
            return fs::FsNativeContentStorage(FsContentStorageId_SdCard).GetTotalSpace("/", out);
        } else {
            return fs::FsNativeContentStorage(FsContentStorageId_User).GetTotalSpace("/", out);
        }
    }
    Result GetFreeSpace(const char *path, s64 *out) override {
        if (App::GetInstallSdEnable()) {
            return fs::FsNativeContentStorage(FsContentStorageId_SdCard).GetFreeSpace("/", out);
        } else {
            return fs::FsNativeContentStorage(FsContentStorageId_User).GetFreeSpace("/", out);
        }
    }

    Result GetEntryType(const char *path, FsDirEntryType *out_entry_type) override {
        R_TRY(FsProxyVfs::GetEntryType(path, out_entry_type));
        if (*out_entry_type == FsDirEntryType_File) {
            R_TRY(FailedIfNotEnabled());
        }
        R_SUCCEED();
    }
    Result CreateFile(const char* path, s64 size, u32 option) override {
        R_TRY(FailedIfNotEnabled());
        R_TRY(IsValidFileType(path));
        R_TRY(FsProxyVfs::CreateFile(path, size, option));
        R_SUCCEED();
    }
    Result OpenFile(const char *path, u32 mode, FsFile *out_file) override {
        R_TRY(FailedIfNotEnabled());
        R_TRY(IsValidFileType(path));
        R_TRY(FsProxyVfs::OpenFile(path, mode, out_file));
        log_write("[MTP] done file open: %s mode: 0x%X\n", path, mode);

        if (mode & FsOpenMode_Write) {
            const auto& e = m_entries[out_file->s.object_id];

            // check if we already have this file queued.
            {
                SCOPED_MUTEX(&g_shared_data.mutex);
                log_write("[MTP] checking if empty\n");
                R_UNLESS(g_shared_data.current_file.empty(), FsError_NotImplemented);
                log_write("[MTP] is empty\n");
                g_shared_data.current_file = e.name;
            }
            on_thing();
        }

        log_write("[MTP] got file: %s\n", path);
        R_SUCCEED();
    }
    Result WriteFile(FsFile *file, s64 off, const void *buf, u64 write_size, u32 option) override {
        SCOPED_MUTEX(&g_shared_data.mutex);
        if (!g_shared_data.enabled) {
            log_write("[MTP] failing as not enabled\n");
            R_THROW(FsError_NotImplemented);
        }

        if (!g_shared_data.on_write || !g_shared_data.on_write(buf, write_size)) {
            log_write("[MTP] failing as not written\n");
            R_THROW(::haze::ResultCancelled());
        }

        R_TRY(FsProxyVfs::WriteFile(file, off, buf, write_size, option));
        R_SUCCEED();
    }
    void CloseFile(FsFile *file) override {
        bool update{};
        {
            SCOPED_MUTEX(&g_shared_data.mutex);
            if (file->s.own_handle & FsOpenMode_Write) {
                log_write("[MTP] closing current file\n");
                if (g_shared_data.on_close) {
                    g_shared_data.on_close();
                }

                g_shared_data.in_progress = false;
                g_shared_data.current_file.clear();
                update = true;
            }
        }

        if (update) {
            on_thing();
        }

        FsProxyVfs::CloseFile(file);
    }

    // installs are already multi-threaded via yati.
    bool MultiThreadTransfer(s64 size, bool read) override {
        App::IsFileBaseEmummc();
        return false;
    }
};
#endif

#if ENABLE_NETWORK_INSTALL
std::shared_ptr<::haze::FileSystemProxyImpl> MakeFsInstallProxy(const char* name, const char* display_name) {
    return std::make_shared<FsInstallProxy>(name, display_name);
}

void InitInstallMode(OnInstallStart on_start, OnInstallWrite on_write, OnInstallClose on_close) {
    SCOPED_MUTEX(&g_shared_data.mutex);
    g_shared_data.on_start = on_start;
    g_shared_data.on_write = on_write;
    g_shared_data.on_close = on_close;
    g_shared_data.enabled = true;
}

void DisableInstallMode() {
    SCOPED_MUTEX(&g_shared_data.mutex);
    g_shared_data.enabled = false;
}
#endif

} // namespace sphaira::haze
