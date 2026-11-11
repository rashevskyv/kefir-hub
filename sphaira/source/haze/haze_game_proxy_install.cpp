// The "Install to SD" / "Install to NAND" folders of the MTP Games drive. A file dropped
// there streams to the background installer like the microSD root drop (haze_fs_proxy.cpp);
// the queued path names the folder, RunInstall() reads the target back from it.
#include "haze/haze_game_proxy_internal.hpp"

namespace sphaira::haze {

#if ENABLE_NETWORK_INSTALL
namespace {

bool IsInstallFile(const std::string& name) {
    const auto dot = name.rfind('.');
    if (dot == std::string::npos) {
        return false;
    }
    return std::ranges::any_of(SUPPORTED_EXT, [&](const char* ext) { return !strcasecmp(name.c_str() + dot, ext); });
}

bool InstallEnabled() {
    SCOPED_MUTEX(&g_shared_data.mutex);
    return g_shared_data.enabled;
}

} // namespace

auto FsGameProxy::FindInstallEntry(const std::string& name) -> FsDirectoryEntry* {
    const auto it = std::ranges::find_if(m_install_entries, [&](const auto& e) { return !strcasecmp(e.name, name.c_str()); });
    return it != m_install_entries.end() ? &*it : nullptr;
}

Result FsGameProxy::InstallCreate(const sphaira::mtp::ParsedPath& pp, s64 size) {
    R_UNLESS(IsInstallFile(pp.filename), FsError_NotImplemented);
    R_UNLESS(InstallEnabled(), FsError_NotImplemented);
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
    const auto entry = FindInstallEntry(pp.filename);
    R_UNLESS(entry || (mode & FsOpenMode_Write), FsError_PathNotFound);
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
auto FsGameProxy::FindInstallEntry(const std::string&) -> FsDirectoryEntry* { return nullptr; }
Result FsGameProxy::InstallCreate(const sphaira::mtp::ParsedPath&, s64) { R_THROW(FsError_NotImplemented); }
Result FsGameProxy::InstallOpen(const sphaira::mtp::ParsedPath&, u32, FileHandle&) { R_THROW(FsError_PathNotFound); }
Result FsGameProxy::InstallWrite(FileHandle&, s64, const void*, u64) { R_THROW(FsError_NotImplemented); }
void FsGameProxy::InstallClose(FileHandle&) {}
#endif

} // namespace sphaira::haze
