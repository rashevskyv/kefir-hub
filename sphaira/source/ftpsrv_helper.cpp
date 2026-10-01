#include "ftpsrv_helper.hpp"
#include "ftpsrv_internal.hpp"

#include "app.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "net.hpp"
#include "ui/menus/homebrew.hpp"
#include "ui/menus/dbi_menu.hpp"
#include "ui/menus/install_stream_menu_base.hpp"

#include <algorithm>
#include <cstring>
#include <minIni.h>
#include <ftpsrv.h>
#include <ftpsrv_vfs.h>
#include <nx/vfs_nx.h>
#include <nx/utils.h>

namespace sphaira::ftpsrv {
namespace {

void FtpMutationCallback(enum VfsNxMutation type, const char* p1, const char* p2) {
    switch (type) {
        case VFS_NX_MUTATION_FILE_CREATED:
            if (p1) {
                ui::menu::homebrew::NotifyFileCreated(p1);
            }
            break;
        case VFS_NX_MUTATION_FILE_DELETED:
            if (p1) {
                ui::menu::homebrew::NotifyFileDeleted(p1);
            }
            break;
        case VFS_NX_MUTATION_DIR_CREATED:
            if (p1) {
                ui::menu::homebrew::NotifyDirectoryCreated(p1);
            }
            break;
        case VFS_NX_MUTATION_DIR_DELETED:
            if (p1) {
                ui::menu::homebrew::NotifyDirectoryDeleted(p1);
            }
            break;
        case VFS_NX_MUTATION_RENAME_FILE:
            if (p1 && p2) {
                ui::menu::homebrew::NotifyRename(p1, p2, false);
            }
            break;
        case VFS_NX_MUTATION_RENAME_DIR:
            if (p1 && p2) {
                ui::menu::homebrew::NotifyRename(p1, p2, true);
            }
            break;
    }
}

// the folder shared over ftp alongside "sdmc:" and "install:", if any. held as a
// plain microSD path ("/games/roms"): ftpsrv exposes it as a root device backed
// by the card's filesystem with that path as a *shortcut*, which is exactly how
// its own "switch:" / "atmosphere_contents:" aliases are built.
//
// a newlib devoptab is useless here -- ftpsrv resolves every path through its
// own fsdev wrapper (fsdev_wrapTranslatePath) and never touches newlib, so a
// device registered with AddDevice() shows up in the root listing and then fails
// to open with ENODEV.
struct Mount {
    std::string name;
    std::string path;
    // the same path in the form the fsdev wrapper wants (no leading slash).
    // held because the wrapper keeps it as a bare pointer.
    std::string shortcut;

    bool operator==(const Mount& rhs) const {
        return name == rhs.name && path == rhs.path;
    }
};

std::vector<Mount> g_ftp_mounts{};

// device names live in a char[32] (with the ':') on both sides of the vfs, so a
// long folder name would be silently truncated into something that no longer
// matches what the listing advertises.
constexpr size_t MOUNT_NAME_MAX = 24;

// the allowlist vfs_nx_add_device() filters against (see patch_ftpsrv.cmake).
// must be set before vfs_nx_init(), and must already name the mounted folder or
// its device is dropped on the floor.
void UpdateFtpVisibleDevices() {
    std::vector<const char*> visible;
    visible.push_back("sdmc");
#if ENABLE_NETWORK_INSTALL
    visible.push_back("install");
#endif
    for (const auto& m : g_ftp_mounts) {
        visible.push_back(m.name.c_str());
    }
    vfs_nx_set_visible_devices(visible.data(), visible.size());
}

// adds the mounted folder as its own root device. must run *after* vfs_nx_init:
// that is what mounts the card (whose FsFileSystem the shortcut borrows) and
// what hands g_device to the root listing.
void MountFolderDevices() {
    if (g_ftp_mounts.empty()) {
        return;
    }

    auto* sdmc = fsdev_wrapGetDeviceFileSystem("sdmc");
    if (!sdmc) {
        log_write("[FTP] cannot mount folders: no sdmc device\n");
        return;
    }

    for (auto& m : g_ftp_mounts) {
        // the shortcut is pasted into "/%s/%s" by fsdev_wrapTranslatePath(), so
        // it is stored without its leading slash: "/games/roms" would resolve to
        // "//games/roms/...", and a path starting with a double separator is the
        // one shape worth not handing to fs.
        m.shortcut = m.path;
        while (!m.shortcut.empty() && m.shortcut.front() == '/') {
            m.shortcut.erase(m.shortcut.begin());
        }

        // own = false: the filesystem belongs to the "sdmc" entry, unmounting
        // this alias must not close it. the shortcut is kept by pointer, hence
        // the long-lived string.
        if (fsdev_wrapMountDevice(m.name.c_str(), m.shortcut.c_str(), *sdmc, false)) {
            log_write("[FTP] cannot mount %s -> %s\n", m.name.c_str(), m.path.c_str());
            continue;
        }

        vfs_nx_add_device(m.name.c_str(), VFS_TYPE_FS);
        log_write("[FTP] mounted %s: -> %s\n", m.name.c_str(), m.path.c_str());
    }
}


const char* INI_PATH = "/config/ftpsrv/config.ini";
constexpr int THREAD_PRIO = PRIO_PREEMPTIVE;
constexpr int THREAD_CORE = 2;
FtpSrvConfig g_ftpsrv_config = {0};
std::atomic_bool g_should_exit = false;
bool g_is_running{false}; // guarded by g_mutex (Init/Exit/IsRunning)
Thread g_thread;
Mutex g_mutex{};

void ftp_log_callback(enum FTP_API_LOG_TYPE type, const char* msg) {
    App::NotifyFlashLed();
}

void ftp_progress_callback(void) {
    App::NotifyFlashLed();
}

void loop(void* arg) {
    log_write("[FTP] loop entered\n");

    while (!g_should_exit) {
        // binding while the interface is down produces a socket that never
        // receives anything, so wait for an ip rather than spin on a dead
        // listener (also covers the ftp server being started offline).
        while (!g_should_exit && !net::IsConnected()) {
            svcSleepThread(1e+9);
        }

        auto resume_gen = net::ResumeGeneration();
        ftpsrv_init(&g_ftpsrv_config);
        while (!g_should_exit) {
            if (ftpsrv_loop(100) != FTP_API_LOOP_ERROR_OK) {
                svcSleepThread(1e+6);
                break;
            }

            // a wake from sleep leaves the listening socket bound to an
            // interface that no longer exists: only a re-init starts listening
            // again. see net::NotifyResume.
            if (net::ResumeGeneration() != resume_gen) {
                log_write("[FTP] resume detected, restarting listener\n");
                break;
            }
        }
        ftpsrv_exit();
    }

    log_write("[FTP] loop exitied\n");
}

} // namespace

bool Init() {
    SCOPED_MUTEX(&g_mutex);
    if (g_is_running) {
        log_write("[FTP] already enabled, cannot open\n");
        return false;
    }

    if (R_FAILED(fsdev_wrapMountSdmc())) {
        log_write("[FTP] cannot mount sdmc\n");
        return false;
    }

    g_ftpsrv_config.log_callback = ftp_log_callback;
    g_ftpsrv_config.progress_callback = ftp_progress_callback;
    g_ftpsrv_config.anon = ini_getbool("Login", "anon", 0, INI_PATH);
    ini_gets("Login", "user", "", g_ftpsrv_config.user, sizeof(g_ftpsrv_config.user), INI_PATH);
    ini_gets("Login", "pass", "", g_ftpsrv_config.pass, sizeof(g_ftpsrv_config.pass), INI_PATH);
    g_ftpsrv_config.port = ini_getl("Network", "port", 5000, INI_PATH); // 5000 to keep compat with older sphaira
    g_ftpsrv_config.timeout = ini_getl("Network", "timeout", 0, INI_PATH);
    g_ftpsrv_config.use_localtime = ini_getbool("Misc", "use_localtime", 0, INI_PATH);
    bool log_enabled = ini_getbool("Log", "log", 0, INI_PATH);

    // get nx config
    bool mount_devices = ini_getbool("Nx", "mount_devices", 1, INI_PATH);
    bool mount_bis = ini_getbool("Nx", "mount_bis", 0, INI_PATH);
    bool save_writable = ini_getbool("Nx", "save_writable", 0, INI_PATH);
    g_ftpsrv_config.port = ini_getl("Nx", "app_port", g_ftpsrv_config.port, INI_PATH); // compat

    // get Nx-App overrides
    g_ftpsrv_config.anon = ini_getbool("Nx-App", "anon", g_ftpsrv_config.anon, INI_PATH);
    ini_gets("Nx-App", "user", g_ftpsrv_config.user, g_ftpsrv_config.user, sizeof(g_ftpsrv_config.user), INI_PATH);
    ini_gets("Nx-App", "pass", g_ftpsrv_config.pass, g_ftpsrv_config.pass, sizeof(g_ftpsrv_config.pass), INI_PATH);
    g_ftpsrv_config.port = ini_getl("Nx-App", "port", g_ftpsrv_config.port, INI_PATH);
    g_ftpsrv_config.timeout = ini_getl("Nx-App", "timeout", g_ftpsrv_config.timeout, INI_PATH);
    g_ftpsrv_config.use_localtime = ini_getbool("Nx-App", "use_localtime", g_ftpsrv_config.use_localtime, INI_PATH);
    log_enabled = ini_getbool("Nx-App", "log", log_enabled, INI_PATH);
    mount_devices = ini_getbool("Nx-App", "mount_devices", mount_devices, INI_PATH);
    mount_bis = ini_getbool("Nx-App", "mount_bis", mount_bis, INI_PATH);
    save_writable = ini_getbool("Nx-App", "save_writable", save_writable, INI_PATH);

    // App settings are the source of truth for login/port (editable in Settings).
    g_ftpsrv_config.anon = App::GetFtpAnon();
    if (!g_ftpsrv_config.anon) {
        const auto user = App::GetFtpUser();
        const auto pass = App::GetFtpPass();
        std::snprintf(g_ftpsrv_config.user, sizeof(g_ftpsrv_config.user), "%s", user.c_str());
        std::snprintf(g_ftpsrv_config.pass, sizeof(g_ftpsrv_config.pass), "%s", pass.c_str());
        // no credentials set -> allow anonymous so the user is never locked out.
        if (user.empty() && pass.empty()) {
            g_ftpsrv_config.anon = true;
        }
    }
    if (const auto port = App::GetFtpPort(); port > 0 && port <= 65535) {
        g_ftpsrv_config.port = port;
    }

    g_should_exit = false;
    mount_devices = true;
    g_ftpsrv_config.timeout = 0;

    if (!g_ftpsrv_config.port) {
        log_write("[FTP] no port config\n");
        return false;
    }

#if ENABLE_NETWORK_INSTALL
    const VfsNxCustomPath custom = {
        .name = "install",
        .user = NULL,
        .func = &g_vfs_install,
    };

    vfs_nx_set_root_write_router(ftp_root_write_router);
    vfs_nx_set_mutation_callback(FtpMutationCallback);
    log_write("[FTP] root-drop routing and mutation callbacks armed\n");
    UpdateFtpVisibleDevices();
    vfs_nx_init(&custom, mount_devices, save_writable, mount_bis, false);
#else
    vfs_nx_set_mutation_callback(FtpMutationCallback);
    UpdateFtpVisibleDevices();
    vfs_nx_init(NULL, mount_devices, save_writable, mount_bis, false);
#endif

    MountFolderDevices();

    Result rc;
    if (R_FAILED(rc = threadCreate(&g_thread, loop, nullptr, nullptr, 1024*16, THREAD_PRIO, THREAD_CORE))) {
        log_write("[FTP] failed to create nxlink thread: 0x%X\n", rc);
        return false;
    }

    if (R_FAILED(rc = svcSetThreadCoreMask(g_thread.handle, THREAD_CORE, THREAD_AFFINITY_DEFAULT(THREAD_CORE)))) {
        log_write("[FTP] failed to set core mask: 0x%X\n", rc);
        return false;
    }

    if (R_FAILED(rc = threadStart(&g_thread))) {
        log_write("[FTP] failed to start nxlink thread: 0x%X\n", rc);
        threadClose(&g_thread);
        return false;
    }

    log_write("[FTP] started\n");
    return g_is_running = true;
}

void Exit() {
    SCOPED_MUTEX(&g_mutex);
    if (!g_is_running) {
        return;
    }

    g_is_running = false;
    g_should_exit = true;

    threadWaitForExit(&g_thread);
    threadClose(&g_thread);

    vfs_nx_exit();
    fsdev_wrapUnmountAll();
    memset(&g_ftpsrv_config, 0, sizeof(g_ftpsrv_config));

    log_write("[FTP] exitied\n");
}


bool IsRunning() {
    SCOPED_MUTEX(&g_mutex);
    return g_is_running;
}

unsigned GetPort() {
    SCOPED_MUTEX(&g_mutex);
    return g_ftpsrv_config.port;
}

bool IsAnon() {
    SCOPED_MUTEX(&g_mutex);
    return g_ftpsrv_config.anon;
}

const char* GetUser() {
    SCOPED_MUTEX(&g_mutex);
    return g_ftpsrv_config.user;
}

const char* GetPass() {
    SCOPED_MUTEX(&g_mutex);
    return g_ftpsrv_config.pass;
}

namespace {

// applies a new set of mounts to a server that may already be up. the device
// list is built once, inside vfs_nx_init(), and ftpsrv offers no way to add or
// drop a root device afterwards -- so the server is bounced instead of poked at.
// this also keeps Mount::shortcut (handed to the fsdev wrapper as a bare
// pointer) from being reassigned while a mount still references it.
void ApplyMounts(std::vector<Mount> mounts) {
    {
        SCOPED_MUTEX(&g_mutex);
        // bouncing the server drops every client connection, so never do it for
        // a change that is not one -- re-selecting the same folders (or the card
        // root, which is not a mount at all) used to kick the user off.
        if (g_ftp_mounts == mounts) {
            return;
        }
    }

    const bool was_running = IsRunning();
    if (was_running) {
        Exit();
    }

    {
        SCOPED_MUTEX(&g_mutex);
        g_ftp_mounts = std::move(mounts);
    }

    if (was_running) {
        Init();
    }
}

// device name for a mounted folder: its leaf, made safe and unique.
auto MakeMountName(const std::string& clean, const std::vector<Mount>& taken) -> std::string {
    const char* leaf = std::strrchr(clean.c_str(), '/');
    std::string name = (leaf && leaf[1]) ? (leaf + 1) : "mounted";

    if (name.length() > MOUNT_NAME_MAX) {
        name.resize(MOUNT_NAME_MAX);
    }

    // fsdev_wrapMountDevice() passes the name straight to snprintf() as the
    // format string, so a folder called "100%" would read off the stack.
    std::replace(name.begin(), name.end(), '%', '_');

    // a device name that collides with a built-in would shadow it in the root
    // listing (or be shadowed by it, depending on lookup order).
    if (name == "sd" || name == "sdmc" || name == "install") {
        name += "_folder";
    }

    // two selected folders can share a leaf ("/a/roms" and "/b/roms"); the
    // second gets a suffix so both are reachable.
    const auto is_taken = [&taken](const std::string& n) {
        return std::ranges::any_of(taken, [&n](const Mount& m) { return m.name == n; });
    };

    if (is_taken(name)) {
        const auto stem = name;
        for (int i = 2; ; i++) {
            name = stem + "_" + std::to_string(i);
            if (!is_taken(name)) {
                break;
            }
        }
    }

    return name;
}

} // namespace

void SetFtpMountedFolders(const std::vector<std::string>& paths) {
    std::vector<Mount> mounts;

    for (const auto& path : paths) {
        // the card root is what ftp already serves as "sdmc:"; mounting it again
        // would just be a duplicate device.
        if (path.empty() || path == "/") {
            continue;
        }

        std::string clean = path;
        while (clean.length() > 1 && clean.back() == '/') {
            clean.pop_back();
        }

        mounts.push_back(Mount{MakeMountName(clean, mounts), std::move(clean), {}});
    }

    ApplyMounts(std::move(mounts));
}

void ClearFtpMountedFolders() {
    ApplyMounts({});
}

std::vector<std::string> GetFtpMountedNames() {
    SCOPED_MUTEX(&g_mutex);
    std::vector<std::string> out;
    out.reserve(g_ftp_mounts.size());
    for (const auto& m : g_ftp_mounts) {
        out.push_back(m.name);
    }
    return out;
}

} // namespace sphaira::ftpsrv

extern "C" {

void log_file_write(const char* msg) {
    log_write("%s", msg);
}

void log_file_fwrite(const char* fmt, ...) {
    va_list v{};
    va_start(v, fmt);
    log_write_arg(fmt, &v);
    va_end(v);
}

} // extern "C"
