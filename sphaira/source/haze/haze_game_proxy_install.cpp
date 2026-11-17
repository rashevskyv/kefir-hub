// The "Install to SD" / "Install to NAND" folders of the MTP Games drive. A file dropped
// there streams to the background installer like the microSD root drop (haze_fs_proxy.cpp);
// the queued path names the folder, RunInstall() reads the target back from it.
// A dropped folder works as on the Install drive (haze_install_proxy.cpp): every game in it
// installs, a game's mods folder goes to /atmosphere/contents/<tid>, other files are skipped.
#include "haze/haze_game_proxy_internal.hpp"

namespace sphaira::haze {

#if ENABLE_NETWORK_INSTALL
namespace {

bool InstallEnabled() {
    SCOPED_MUTEX(&g_shared_data.mutex);
    return g_shared_data.enabled;
}

} // namespace

auto FsGameProxy::FindInstallEntry(const std::string& name) -> FsDirectoryEntry* {
    const auto it = std::ranges::find_if(m_install_entries, [&](const auto& e) { return !strcasecmp(e.name, name.c_str()); });
    return it != m_install_entries.end() ? &*it : nullptr;
}

Result FsGameProxy::InstallEntryType(const sphaira::mtp::ParsedPath& pp, FsDirEntryType* out) {
    if (m_install_dirs.contains(pp.filename)) {
        *out = FsDirEntryType_Dir;
        R_SUCCEED();
    }
    if (const auto sd = sphaira::mtp::ModsDropSdPath(pp.filename, m_names.mods)) {
        return m_sd.GetEntryType(sd->c_str(), out);
    }
    R_UNLESS(FindInstallEntry(pp.filename), FsError_PathNotFound);
    *out = FsDirEntryType_File;
    R_SUCCEED();
}

Result FsGameProxy::InstallMkdir(const sphaira::mtp::ParsedPath& pp) {
    if (const auto sd = sphaira::mtp::ModsDropSdPath(pp.filename, m_names.mods)) {
        log_write("[MTP-GAMES] mods folder %s -> %s\n", pp.filename.c_str(), sd->c_str());
        return m_sd.CreateDirectoryRecursively(sd->c_str());
    }
    m_install_dirs.emplace(pp.filename);
    R_SUCCEED();
}

Result FsGameProxy::InstallDelete(const sphaira::mtp::ParsedPath& pp) {
    if (const auto drop = sphaira::mtp::ClassifyInstallDrop(pp.filename, m_names.mods); drop.kind == sphaira::mtp::DropKind::Mod) {
        return m_sd.DeleteFile(drop.sd_path.c_str());
    }
    // nothing is stored; succeed so Explorer's replace flow goes on.
    std::erase_if(m_install_entries, [&](const auto& e) { return !strcasecmp(e.name, pp.filename.c_str()); });
    R_SUCCEED();
}

Result FsGameProxy::InstallCreate(const sphaira::mtp::ParsedPath& pp, s64 size) {
    const auto drop = sphaira::mtp::ClassifyInstallDrop(pp.filename, m_names.mods);
    if (drop.kind == sphaira::mtp::DropKind::Mod) {
        const fs::FsPath sd{drop.sd_path.c_str()};
        log_write("[MTP-GAMES] mod file %s -> %s\n", pp.filename.c_str(), sd.s);
        R_TRY(m_sd.CreateDirectoryRecursivelyWithPath(sd));
        // a restore replaces the file: the folder lists nothing, so Explorer never asks.
        m_sd.DeleteFile(sd);
        return m_sd.CreateFile(sd, size, 0);
    }
    R_UNLESS(drop.kind != sphaira::mtp::DropKind::Reject, FsError_NotImplemented);
    R_UNLESS(drop.kind == sphaira::mtp::DropKind::Skip || InstallEnabled(), FsError_NotImplemented);
    // one install at a time: only the file in flight needs to stay visible.
    std::erase_if(m_install_entries, [&](const auto& e) { return strcasecmp(e.name, pp.filename.c_str()) != 0; });
    if (auto e = FindInstallEntry(pp.filename)) {
        e->file_size = size;
    } else {
        m_install_entries.emplace_back(MakeVirtualFileEntry(pp.filename, size));
    }
    R_SUCCEED();
}

Result FsGameProxy::InstallOpen(const sphaira::mtp::ParsedPath& pp, u32 mode, FileHandle& h) {
    const auto drop = sphaira::mtp::ClassifyInstallDrop(pp.filename, m_names.mods);
    if (drop.kind == sphaira::mtp::DropKind::Mod) {
        R_TRY(m_sd.OpenFile(drop.sd_path.c_str(), mode, &h.sd));
        h.is_sd = true;
        R_SUCCEED();
    }
    const auto entry = FindInstallEntry(pp.filename);
    R_UNLESS(entry || (mode & FsOpenMode_Write), FsError_PathNotFound);
    if (drop.kind == sphaira::mtp::DropKind::Skip) {
        log_write("[MTP-GAMES] skipping %s\n", pp.filename.c_str());
        h.skip = true;
        R_SUCCEED();
    }
    R_UNLESS(drop.kind == sphaira::mtp::DropKind::Install, FsError_NotImplemented);
    if (mode & FsOpenMode_Write) {
        {
            SCOPED_MUTEX(&g_shared_data.mutex);
            R_UNLESS(g_shared_data.enabled, FsError_NotImplemented);
            // only one transfer can be queued at a time.
            R_UNLESS(g_shared_data.current_file.empty(), FsError_NotImplemented);
            g_shared_data.current_file = sphaira::mtp::InstallQueuePath(pp.install_sd, pp.filename);
        }
        log_write("[MTP-GAMES] install %s to %s\n", pp.filename.c_str(), pp.install_sd ? "SD" : "NAND");
        // on_thing() locks the mutex itself, it must be called after releasing.
        on_thing();
    }
    h.install = true;
    h.mode = mode;
    h.install_name = pp.filename;
    h.install_size = entry ? entry->file_size : 0;
    R_SUCCEED();
}

// runs once per usb packet: no logging here.
Result FsGameProxy::InstallWrite(FileHandle& h, s64 off, const void* buf, u64 size) {
    {
        SCOPED_MUTEX(&g_shared_data.mutex);
        R_UNLESS(g_shared_data.enabled, FsError_NotImplemented);
        if (!g_shared_data.on_write || !g_shared_data.on_write(buf, size)) {
            R_THROW(::haze::ResultCancelled());
        }
    }
    h.install_size = std::max<s64>(h.install_size, off + static_cast<s64>(size));
    R_SUCCEED();
}

void FsGameProxy::InstallClose(FileHandle& h) {
    if (!(h.mode & FsOpenMode_Write)) {
        return;
    }
    {
        SCOPED_MUTEX(&g_shared_data.mutex);
        if (g_shared_data.on_close) {
            g_shared_data.on_close();
        }
        g_shared_data.in_progress = false;
        g_shared_data.current_file.clear();
    }
    on_thing();
    if (auto e = FindInstallEntry(h.install_name)) {
        e->file_size = h.install_size;
    }
}
#else
Result FsGameProxy::InstallEntryType(const sphaira::mtp::ParsedPath&, FsDirEntryType*) { R_THROW(FsError_PathNotFound); }
Result FsGameProxy::InstallMkdir(const sphaira::mtp::ParsedPath&) { R_THROW(FsError_NotImplemented); }
Result FsGameProxy::InstallDelete(const sphaira::mtp::ParsedPath&) { R_THROW(FsError_NotImplemented); }
auto FsGameProxy::FindInstallEntry(const std::string&) -> FsDirectoryEntry* { return nullptr; }
Result FsGameProxy::InstallCreate(const sphaira::mtp::ParsedPath&, s64) { R_THROW(FsError_NotImplemented); }
Result FsGameProxy::InstallOpen(const sphaira::mtp::ParsedPath&, u32, FileHandle&) { R_THROW(FsError_PathNotFound); }
Result FsGameProxy::InstallWrite(FileHandle&, s64, const void*, u64) { R_THROW(FsError_NotImplemented); }
void FsGameProxy::InstallClose(FileHandle&) {}
#endif

} // namespace sphaira::haze
