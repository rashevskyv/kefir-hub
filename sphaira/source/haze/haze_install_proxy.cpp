// The MTP "Install (NSP, XCI, NSZ, XCZ)" drive (the default; Settings → MTP can move the install folders
// into the Games drive instead, haze_game_proxy_install.cpp) and the installer callbacks every MTP install
// path shares (that drive, the Games install folders, the microSD root drop).
// The drive takes whole folders too (a Games "Merged" folder copied to the PC and back): every
// NSP/XCI/NSZ/XCZ in them installs, the files of a game's mods folder go to its
// /atmosphere/contents/<tid> on the SD card, other files are skipped (mtp::ClassifyInstallDrop).
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

namespace sphaira::haze {

#if ENABLE_NETWORK_INSTALL
struct FsInstallProxy final : FsProxyVfs {
    using FsProxyVfs::FsProxyVfs;

    // FsFile.s.session tags what an open file is; FsProxyVfs uses object_id and own_handle.
    enum HandleKind : u32 { Handle_Install = 0, Handle_Mod = 1, Handle_Skip = 2 };

    Result FailedIfNotEnabled() {
        SCOPED_MUTEX(&g_shared_data.mutex);
        if (!g_shared_data.enabled) {
            App::Notify("Please launch MTP install menu before trying to install"_i18n);
            R_THROW(FsError_NotImplemented);
        }
        R_SUCCEED();
    }

    auto Classify(const char* path) const -> sphaira::mtp::Drop {
        return sphaira::mtp::ClassifyInstallDrop(FixPath(path).s, m_mods_name);
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
        const auto fixed = FixPath(path);
        if (const auto it = m_dropped.find(fixed.s); it != m_dropped.end()) {
            *out_entry_type = it->second;
            R_SUCCEED();
        }
        if (const auto sd = sphaira::mtp::ModsDropSdPath(fixed.s, m_mods_name)) {
            return m_sd.GetEntryType(sd->c_str(), out_entry_type);
        }
        R_TRY(FsProxyVfs::GetEntryType(path, out_entry_type));
        if (*out_entry_type == FsDirEntryType_File) {
            R_TRY(FailedIfNotEnabled());
        }
        R_SUCCEED();
    }
    // a dropped folder: a game's mods folder is made on the SD card, any other one is only
    // remembered so the files inside it can arrive.
    Result CreateDirectory(const char* path) override {
        const auto fixed = FixPath(path);
        if (const auto sd = sphaira::mtp::ModsDropSdPath(fixed.s, m_mods_name)) {
            log_write("[MTP] mods folder %s -> %s\n", fixed.s, sd->c_str());
            return m_sd.CreateDirectoryRecursively(sd->c_str());
        }
        m_dropped[fixed.s] = FsDirEntryType_Dir;
        R_SUCCEED();
    }
    Result CreateFile(const char* path, s64 size, u32 option) override {
        const auto drop = Classify(path);
        switch (drop.kind) {
            case sphaira::mtp::DropKind::Mod: {
                const fs::FsPath sd{drop.sd_path.c_str()};
                log_write("[MTP] mod file %s -> %s\n", path, sd.s);
                R_TRY(m_sd.CreateDirectoryRecursivelyWithPath(sd));
                // a restore replaces the file: the drive lists nothing, so Explorer never asks.
                m_sd.DeleteFile(sd);
                return m_sd.CreateFile(sd, size, option);
            }
            case sphaira::mtp::DropKind::Skip:
                log_write("[MTP] skipping %s\n", path);
                m_dropped[FixPath(path).s] = FsDirEntryType_File;
                R_SUCCEED();
            case sphaira::mtp::DropKind::Reject:
                R_THROW(FsError_NotImplemented);
            case sphaira::mtp::DropKind::Install:
                break;
        }
        R_TRY(FailedIfNotEnabled());
        // files of different folders share one flat list by name: a second drop of the same
        // name replaces the old entry (one install runs at a time).
        if (const auto file_name = GetFileName(path)) {
            std::erase_if(m_entries, [file_name](const auto& e) { return !strcasecmp(file_name, e.name); });
        }
        R_TRY(FsProxyVfs::CreateFile(path, size, option));
        R_SUCCEED();
    }
    Result DeleteFile(const char* path) override {
        const auto drop = Classify(path);
        if (drop.kind == sphaira::mtp::DropKind::Mod) {
            return m_sd.DeleteFile(drop.sd_path.c_str());
        }
        if (m_dropped.erase(FixPath(path).s)) {
            R_SUCCEED();
        }
        return FsProxyVfs::DeleteFile(path);
    }
    Result OpenFile(const char *path, u32 mode, FsFile *out_file) override {
        const auto drop = Classify(path);
        if (drop.kind == sphaira::mtp::DropKind::Mod) {
            // ponytail: one mod file at a time, libhaze sends one object at a time.
            R_TRY(m_sd.OpenFile(drop.sd_path.c_str(), mode, &m_mod_file));
            std::memset(out_file, 0, sizeof(*out_file));
            out_file->s.session = Handle_Mod;
            R_SUCCEED();
        }
        if (drop.kind == sphaira::mtp::DropKind::Skip) {
            R_UNLESS(m_dropped.contains(FixPath(path).s), FsError_PathNotFound);
            std::memset(out_file, 0, sizeof(*out_file));
            out_file->s.session = Handle_Skip;
            R_SUCCEED();
        }
        R_TRY(FailedIfNotEnabled());
        R_UNLESS(drop.kind == sphaira::mtp::DropKind::Install, FsError_NotImplemented);
        R_TRY(FsProxyVfs::OpenFile(path, mode, out_file));
        out_file->s.session = Handle_Install;
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
    Result GetFileSize(FsFile *file, s64 *out_size) override {
        switch (file->s.session) {
            case Handle_Mod: return m_mod_file.GetSize(out_size);
            case Handle_Skip: *out_size = 0; R_SUCCEED();
            default: return FsProxyVfs::GetFileSize(file, out_size);
        }
    }
    Result SetFileSize(FsFile *file, s64 size) override {
        switch (file->s.session) {
            case Handle_Mod: return m_mod_file.SetSize(size);
            case Handle_Skip: R_SUCCEED();
            default: return FsProxyVfs::SetFileSize(file, size);
        }
    }
    Result WriteFile(FsFile *file, s64 off, const void *buf, u64 write_size, u32 option) override {
        if (file->s.session == Handle_Mod) {
            return m_mod_file.Write(off, buf, write_size, option);
        }
        if (file->s.session == Handle_Skip) {
            R_SUCCEED();
        }
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
        if (file->s.session != Handle_Install) {
            if (file->s.session == Handle_Mod) {
                m_mod_file.Close();
            }
            std::memset(file, 0, sizeof(*file));
            return;
        }
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

    // a dropped folder lists nothing: its games are installed, its mods are on the SD card.
    Result OpenDirectory(const char *path, u32 mode, FsDir *out_dir) override {
        R_TRY(FsProxyVfs::OpenDirectory(path, mode, out_dir));
        out_dir->s.own_handle = FixPath(path) != "/";
        R_SUCCEED();
    }
    Result ReadDirectory(FsDir *d, s64 *out_total_entries, size_t max_entries, FsDirectoryEntry *buf) override {
        if (d->s.own_handle) {
            *out_total_entries = 0;
            R_SUCCEED();
        }
        return FsProxyVfs::ReadDirectory(d, out_total_entries, max_entries, buf);
    }
    Result GetDirectoryEntryCount(FsDir *d, s64 *out_count) override {
        if (d->s.own_handle) {
            *out_count = 0;
            R_SUCCEED();
        }
        return FsProxyVfs::GetDirectoryEntryCount(d, out_count);
    }

    // installs are already multi-threaded via yati.
    bool MultiThreadTransfer(s64 size, bool read) override {
        App::IsFileBaseEmummc();
        return false;
    }

private:
    // the Games drive's mods folder name in this locale ("Mods" is always accepted too).
    const std::string m_mods_name{"Mods"_i18n};
    // folders and skipped files of a dropped folder, by full path.
    std::map<std::string, FsDirEntryType, CaseInsensitiveLess> m_dropped{};
    fs::FsNativeSd m_sd{};
    fs::File m_mod_file{};
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
