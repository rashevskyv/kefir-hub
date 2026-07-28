#include "ftpsrv_helper.hpp"

#include "app.hpp"
#include "fs.hpp"
#include "log.hpp"

#include <algorithm>
#include <cstring>
#include <sys/iosupport.h>
#include <sys/dirent.h>
#include <minIni.h>
#include <ftpsrv.h>
#include <ftpsrv_vfs.h>
#include <nx/vfs_nx.h>
#include <nx/utils.h>

namespace sphaira::ftpsrv {
namespace {

static std::string g_ftp_mounted_name{};
static std::string g_ftp_mounted_path{};
static devoptab_t g_ftp_devoptab{};
static bool g_ftp_devoptab_registered = false;

static std::string FixFtpMountPath(const char* path) {
    if (!path) return g_ftp_mounted_path;
    while (*path == '/') path++;
    if (const char* colon = strchr(path, ':')) path = colon + 1;
    while (*path == '/') path++;
    std::string full = g_ftp_mounted_path;
    if (!full.empty() && full.back() != '/' && path[0] != '\0') full += '/';
    full += path;
    return full;
}

static int ftp_mount_open(struct _reent *r, void* fileStruct, const char* path, int flags, int mode) {
    const auto full = FixFtpMountPath(path);
    int fd = open(full.c_str(), flags, mode);
    if (fd < 0) {
        if (r) r->_errno = errno;
        return -1;
    }
    *static_cast<int*>(fileStruct) = fd;
    if (r) r->_errno = 0;
    return 0;
}

static int ftp_mount_close(struct _reent *r, void* fileStruct) {
    int fd = *static_cast<int*>(fileStruct);
    int res = close(fd);
    if (r && res < 0) r->_errno = errno;
    return res;
}

static ssize_t ftp_mount_read(struct _reent *r, void* fileStruct, char* ptr, size_t len) {
    int fd = *static_cast<int*>(fileStruct);
    ssize_t res = read(fd, ptr, len);
    if (r && res < 0) r->_errno = errno;
    return res;
}

static ssize_t ftp_mount_write(struct _reent *r, void* fileStruct, const char* ptr, size_t len) {
    int fd = *static_cast<int*>(fileStruct);
    ssize_t res = write(fd, ptr, len);
    if (r && res < 0) r->_errno = errno;
    return res;
}

static off_t ftp_mount_seek(struct _reent *r, void* fileStruct, off_t pos, int dir) {
    int fd = *static_cast<int*>(fileStruct);
    off_t res = lseek(fd, pos, dir);
    if (r && res < 0) r->_errno = errno;
    return res;
}

static int ftp_mount_fstat(struct _reent *r, void* fileStruct, struct stat* st) {
    int fd = *static_cast<int*>(fileStruct);
    int res = fstat(fd, st);
    if (r && res < 0) r->_errno = errno;
    return res;
}

static int ftp_mount_stat(struct _reent *r, const char* path, struct stat* st) {
    const auto full = FixFtpMountPath(path);
    int res = stat(full.c_str(), st);
    if (r && res < 0) r->_errno = errno;
    return res;
}

static int ftp_mount_unlink(struct _reent *r, const char* path) {
    const auto full = FixFtpMountPath(path);
    int res = unlink(full.c_str());
    if (r && res < 0) r->_errno = errno;
    return res;
}

static int ftp_mount_mkdir(struct _reent *r, const char* path, int mode) {
    const auto full = FixFtpMountPath(path);
    int res = mkdir(full.c_str(), mode);
    if (r && res < 0) r->_errno = errno;
    return res;
}

static int ftp_mount_rmdir(struct _reent *r, const char* path) {
    const auto full = FixFtpMountPath(path);
    int res = rmdir(full.c_str());
    if (r && res < 0) r->_errno = errno;
    return res;
}

static int ftp_mount_rename(struct _reent *r, const char* old_path, const char* new_path) {
    const auto old_full = FixFtpMountPath(old_path);
    const auto new_full = FixFtpMountPath(new_path);
    int res = rename(old_full.c_str(), new_full.c_str());
    if (r && res < 0) r->_errno = errno;
    return res;
}

struct FtpDirState {
    DIR* dir;
};

static DIR_ITER* ftp_mount_diropen(struct _reent *r, DIR_ITER *dirState, const char* path) {
    const auto full = FixFtpMountPath(path);
    auto* state = static_cast<FtpDirState*>(dirState->dirStruct);
    state->dir = opendir(full.c_str());
    if (!state->dir) {
        if (r) r->_errno = errno;
        return nullptr;
    }
    if (r) r->_errno = 0;
    return dirState;
}

static int ftp_mount_dirreset(struct _reent *r, DIR_ITER *dirState) {
    auto* state = static_cast<FtpDirState*>(dirState->dirStruct);
    if (state->dir) rewinddir(state->dir);
    if (r) r->_errno = 0;
    return 0;
}

static int ftp_mount_dirnext(struct _reent *r, DIR_ITER *dirState, char* filename, struct stat* filestat) {
    auto* state = static_cast<FtpDirState*>(dirState->dirStruct);
    if (!state->dir) return -1;
    struct dirent* entry = readdir(state->dir);
    if (!entry) {
        if (r) r->_errno = ENOENT;
        return -1;
    }
    std::strncpy(filename, entry->d_name, NAME_MAX);
    if (filestat) {
        std::memset(filestat, 0, sizeof(*filestat));
        filestat->st_mode = (entry->d_type == DT_DIR) ? S_IFDIR : S_IFREG;
    }
    if (r) r->_errno = 0;
    return 0;
}

static int ftp_mount_dirclose(struct _reent *r, DIR_ITER *dirState) {
    auto* state = static_cast<FtpDirState*>(dirState->dirStruct);
    if (state->dir) {
        closedir(state->dir);
        state->dir = nullptr;
    }
    if (r) r->_errno = 0;
    return 0;
}

void UpdateFtpVisibleDevices() {
    std::vector<const char*> visible;
    visible.push_back("sdmc");
#if ENABLE_NETWORK_INSTALL
    visible.push_back("install");
#endif
    if (!g_ftp_mounted_name.empty()) {
        visible.push_back(g_ftp_mounted_name.c_str());
        vfs_nx_add_device(g_ftp_mounted_name.c_str(), VFS_TYPE_FS);
    }
    vfs_nx_set_visible_devices(visible.data(), visible.size());
}

#if ENABLE_NETWORK_INSTALL
// one entry per file the client has open on the install folder. the head of the
// queue is the file currently being streamed; the rest wait their turn.
struct QueuedFile {
    std::string path;
    // the installer refused to take this file (something else is installing).
    // the transfer is failed with EIO instead of being left to sit on a data
    // connection that never becomes ready -- a silent stall makes the client
    // time out and re-upload the same file forever.
    bool failed;
    // when the file was opened, used to bound how long a queued file waits for
    // the installer before it is failed.
    u64 queued_tick;
    // last start attempt, so the retry from isfile_ready() (once per poll) does
    // not hammer the installer.
    u64 attempt_tick;
};

struct InstallSharedData {
    Mutex mutex;
    std::deque<QueuedFile> queued_files;

    void* user;
    OnInstallStart on_start;
    OnInstallWrite on_write;
    OnInstallClose on_close;

    bool in_progress;
    bool enabled;
};
#endif

const char* INI_PATH = "/config/ftpsrv/config.ini";
constexpr int THREAD_PRIO = PRIO_PREEMPTIVE;
constexpr int THREAD_CORE = 2;
FtpSrvConfig g_ftpsrv_config = {0};
std::atomic_bool g_should_exit = false;
bool g_is_running{false};
Thread g_thread;
Mutex g_mutex{};

void ftp_log_callback(enum FTP_API_LOG_TYPE type, const char* msg) {
    sphaira::App::NotifyFlashLed();
}

void ftp_progress_callback(void) {
    sphaira::App::NotifyFlashLed();
}

#if ENABLE_NETWORK_INSTALL
InstallSharedData g_shared_data{};

const char* SUPPORTED_EXT[] = {
    ".nsp", ".xci", ".nsz", ".xcz",
};

// homebrew (.nro) is accepted here too: it streams through the same background
// installer, which writes it to /switch/<name>/ instead of installing a title.
const char* NRO_EXT[] = {
    ".nro",
};

struct VfsUserData {
    char* path;
    int valid;
};

// how long a queued file may wait for the installer to become free before its
// transfer is failed. the installer is usually busy only for the few seconds it
// takes the previous file to drain out of the stream buffer, so waiting beats
// erroring out -- but waiting forever is what made a client (which sees nothing
// but a data connection that never accepts a byte) time out and start the whole
// upload again, over and over.
constexpr u64 QUEUE_WAIT_TIMEOUT_NS = 20ULL*1000ULL*1000ULL*1000ULL;

// minimum gap between start attempts for the same queued file.
constexpr u64 START_RETRY_INTERVAL_NS = 500ULL*1000ULL*1000ULL;

bool IsInstallableExt(const char* path) {
    const char* ext = std::strrchr(path, '.');
    if (!ext) {
        return false;
    }

    for (size_t i = 0; i < std::size(SUPPORTED_EXT); i++) {
        if (!strcasecmp(ext, SUPPORTED_EXT[i])) {
            return true;
        }
    }
    for (size_t i = 0; i < std::size(NRO_EXT); i++) {
        if (!strcasecmp(ext, NRO_EXT[i])) {
            return true;
        }
    }

    return false;
}

// caller must hold g_shared_data.mutex.
QueuedFile* FindQueued(const char* path) {
    if (!path) {
        return nullptr;
    }

    for (auto& e : g_shared_data.queued_files) {
        if (e.path == path) {
            return &e;
        }
    }

    return nullptr;
}

// tries to hand the head of the queue to the installer. called when a file is
// opened / closed and from isfile_ready(), so a file that arrived while the
// installer was busy starts as soon as it frees up rather than sitting until the
// client gives up.
void on_thing() {
    SCOPED_MUTEX(&g_shared_data.mutex);

    if (g_shared_data.in_progress || g_shared_data.queued_files.empty()) {
        return;
    }

    auto& head = g_shared_data.queued_files.front();
    if (head.failed) {
        return; // already reported, waiting for the client to close it.
    }

    const auto now = armTicksToNs(armGetSystemTick());
    if (head.attempt_tick && now - head.attempt_tick < START_RETRY_INTERVAL_NS) {
        return;
    }
    head.attempt_tick = now;

    if (g_shared_data.on_start && g_shared_data.on_start(head.path.c_str())) {
        log_write("[FTP] install started: %s\n", head.path.c_str());
        g_shared_data.in_progress = true;
        return;
    }

    // the installer is busy. keep the file queued and retry on the next poll,
    // but never silently forever: past the timeout fail only this file (the
    // rest of the queue is untouched) so the client gets a real error.
    if (now - head.queued_tick >= QUEUE_WAIT_TIMEOUT_NS) {
        log_write("[FTP] installer stayed busy, failing %s\n", head.path.c_str());
        head.failed = true;
    }
}

int vfs_install_open(void* user, const char* path, enum FtpVfsOpenMode mode) {
    {
        SCOPED_MUTEX(&g_shared_data.mutex);
        auto data = static_cast<VfsUserData*>(user);
        data->valid = 0;

        if (mode != FtpVfsOpenMode_WRITE) {
            errno = EACCES;
            return -1;
        }

        if (!g_shared_data.enabled) {
            errno = EACCES;
            return -1;
        }

        if (!IsInstallableExt(path)) {
            errno = EINVAL;
            return -1;
        }

        // check if we already have this file queued.
        if (FindQueued(path)) {
            errno = EEXIST;
            return -1;
        }

        g_shared_data.queued_files.push_back(QueuedFile{path, false, armTicksToNs(armGetSystemTick()), 0});
        data->path = strdup(path);
        data->valid = true;
    }

    on_thing();
    log_write("[FTP] got file: %s\n", path);
    return 0;
}

int vfs_install_read(void* user, void* buf, size_t size) {
    errno = EACCES;
    return -1;
}

int vfs_install_write(void* user, const void* buf, size_t size) {
    SCOPED_MUTEX(&g_shared_data.mutex);
    if (!g_shared_data.enabled) {
        errno = EACCES;
        return -1;
    }

    auto data = static_cast<VfsUserData*>(user);
    if (!data->valid) {
        errno = EACCES;
        return -1;
    }

    // the installer never took this file (see on_thing): fail the transfer so
    // the client reports an error instead of retrying the upload forever.
    const auto entry = FindQueued(data->path);
    if (!entry || entry->failed) {
        errno = EIO;
        return -1;
    }

    if (!g_shared_data.on_write || !g_shared_data.on_write(buf, size)) {
        errno = EIO;
        return -1;
    }

    return size;
}

int vfs_install_seek(void* user, const void* buf, size_t size, size_t off) {
    errno = ESPIPE;
    return -1;
}

int vfs_install_isfile_open(void* user) {
    SCOPED_MUTEX(&g_shared_data.mutex);
    auto data = static_cast<VfsUserData*>(user);
    return data->valid;
}

int vfs_install_isfile_ready(void* user) {
    // retry a start that was refused earlier: the installer is usually busy only
    // for the seconds it takes the previous file to finish, and this runs once
    // per poll, so a queued file starts the moment it can.
    on_thing();

    SCOPED_MUTEX(&g_shared_data.mutex);
    auto data = static_cast<VfsUserData*>(user);
    if (!data->valid || !data->path) {
        return 1; // let write() / close() report the error.
    }

    if (g_shared_data.queued_files.empty() || g_shared_data.queued_files.front().path != data->path) {
        return 0; // queued behind another file, wait our turn.
    }

    // a failed head counts as ready so ftpsrv goes on to write(), which fails
    // the transfer, rather than polling a connection that never accepts data.
    return g_shared_data.in_progress || g_shared_data.queued_files.front().failed;
}

int vfs_install_close(void* user) {
    {
        SCOPED_MUTEX(&g_shared_data.mutex);
        auto data = static_cast<VfsUserData*>(user);
        if (data->valid) {
            auto it = std::find_if(g_shared_data.queued_files.cbegin(), g_shared_data.queued_files.cend(), [data](const QueuedFile& e) {
                return data->path && e.path == data->path;
            });

            if (it != g_shared_data.queued_files.cend()) {
                // only the file that actually reached the installer has to be
                // closed out; a queued (or failed) one was never handed over.
                if (it == g_shared_data.queued_files.cbegin() && g_shared_data.in_progress) {
                    log_write("[FTP] closing current file: %s\n", it->path.c_str());
                    if (g_shared_data.on_close) {
                        g_shared_data.on_close();
                    }

                    g_shared_data.in_progress = false;
                }

                g_shared_data.queued_files.erase(it);
            } else {
                log_write("[FTP] could not find file in queue...\n");
            }

            if (data->path) {
                free(data->path);
            }

            data->valid = 0;
        }

        memset(data, 0, sizeof(*data));
    }

    on_thing();
    return 0;
}

int vfs_install_opendir(void* user, const char* path) {
    return 0;
}

const char* vfs_install_readdir(void* user, void* user_entry) {
    return NULL;
}

int vfs_install_dirlstat(void* user, const void* user_entry, const char* path, struct stat* st) {
    st->st_nlink = 1;
    st->st_mode = S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH;
    return 0;
}

int vfs_install_isdir_open(void* user) {
    return 1;
}

int vfs_install_closedir(void* user) {
    return 0;
}

int vfs_install_stat(const char* path, struct stat* st) {
    st->st_nlink = 1;
    st->st_mode = S_IFDIR | S_IWUSR | S_IWGRP | S_IWOTH;
    return 0;
}

int vfs_install_mkdir(const char* path) {
    return -1;
}

int vfs_install_unlink(const char* path) {
    return -1;
}

int vfs_install_rmdir(const char* path) {
    return -1;
}

int vfs_install_rename(const char* src, const char* dst) {
    return -1;
}

FtpVfs g_vfs_install = {
    .open = vfs_install_open,
    .read = vfs_install_read,
    .write = vfs_install_write,
    .seek = vfs_install_seek,
    .close = vfs_install_close,
    .isfile_open = vfs_install_isfile_open,
    .isfile_ready = vfs_install_isfile_ready,
    .opendir = vfs_install_opendir,
    .readdir = vfs_install_readdir,
    .dirlstat = vfs_install_dirlstat,
    .closedir = vfs_install_closedir,
    .isdir_open = vfs_install_isdir_open,
    .stat = vfs_install_stat,
    .lstat = vfs_install_stat,
    .mkdir = vfs_install_mkdir,
    .unlink = vfs_install_unlink,
    .rmdir = vfs_install_rmdir,
    .rename = vfs_install_rename,
};

// name of the microSD card device as the ftp vfs exposes it, without the ':'.
constexpr const char* SDMC_DEVICE = "sdmc";

// routing for files dropped into a *root* over FTP, matching the root of the MTP
// drive (see ROOT_DROP_RULES in haze_helper.cpp): anything installable -- .nro
// included -- is handed to the installer as if it had been dropped into the
// install folder, everything else falls through to the card.
//
// two levels count as "root", because both are the same gesture to the user:
//   /game.nsp        the server root, the level listing "sdmc:" and "install:"
//   /sdmc:/game.nsp  the root of the microSD card itself
// anything deeper ("/sdmc:/switch/x.nro", "/switch/x.nro") is always a plain
// copy, so uploading a folder of games never installs anything by accident.
extern "C" int ftp_root_write_router(const char* path) {
    // one line per file created over ftp (not a hot path) with the exact path
    // the client asked for, plus why it was or wasn't routed. without this a
    // drop that lands on the card is indistinguishable from a drop the rule
    // deliberately left alone.
    if (!path) {
        return 0;
    }

    const char* name = path;
    while (*name == '/') {
        name++;
    }

    // strip the device prefix. only the card is routed: every other mount is
    // either hidden or has its own vfs.
    if (const auto colon = std::strchr(name, ':')) {
        const auto len = std::strlen(SDMC_DEVICE);
        if ((size_t)(colon - name) != len || strncasecmp(name, SDMC_DEVICE, len)) {
            log_write("[FTP] write %s: not the microSD card, plain copy\n", path);
            return 0;
        }

        name = colon + 1;
        while (*name == '/') {
            name++;
        }
    }

    // exactly one component left, i.e. the file sits in the root itself.
    if (!*name || std::strchr(name, '/')) {
        log_write("[FTP] write %s: not a root drop, plain copy\n", path);
        return 0;
    }

    if (!IsInstallableExt(name)) {
        log_write("[FTP] root drop %s: not installable, plain copy\n", name);
        return 0;
    }

    SCOPED_MUTEX(&g_shared_data.mutex);
    if (!g_shared_data.enabled) {
        // install mode was never armed (RegisterMtpCallbacks not run): the drop
        // silently becomes a plain copy, so say why rather than leave a mystery.
        log_write("[FTP] root drop %s NOT routed: install mode is off\n", name);
        return 0;
    }

    log_write("[FTP] root drop routed to installer: %s\n", name);
    return 1;
}
#endif

void loop(void* arg) {
    log_write("[FTP] loop entered\n");

    while (!g_should_exit) {
        ftpsrv_init(&g_ftpsrv_config);
        while (!g_should_exit) {
            if (ftpsrv_loop(100) != FTP_API_LOOP_ERROR_OK) {
                svcSleepThread(1e+6);
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
    int user_len = ini_gets("Login", "user", "", g_ftpsrv_config.user, sizeof(g_ftpsrv_config.user), INI_PATH);
    int pass_len = ini_gets("Login", "pass", "", g_ftpsrv_config.pass, sizeof(g_ftpsrv_config.pass), INI_PATH);
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
    user_len = ini_gets("Nx-App", "user", g_ftpsrv_config.user, g_ftpsrv_config.user, sizeof(g_ftpsrv_config.user), INI_PATH);
    pass_len = ini_gets("Nx-App", "pass", g_ftpsrv_config.pass, g_ftpsrv_config.pass, sizeof(g_ftpsrv_config.pass), INI_PATH);
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
    log_write("[FTP] root-drop routing armed\n");
    UpdateFtpVisibleDevices();
    vfs_nx_init(&custom, mount_devices, save_writable, mount_bis, false);
#else
    UpdateFtpVisibleDevices();
    vfs_nx_init(NULL, mount_devices, save_writable, mount_bis, false);
#endif

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

#if ENABLE_NETWORK_INSTALL
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

void SetFtpMountedFolder(const std::string& path) {
    SCOPED_MUTEX(&g_mutex);
    if (path.empty() || path == "/") {
        if (g_ftp_devoptab_registered && !g_ftp_mounted_name.empty()) {
            RemoveDevice(g_ftp_mounted_name.c_str());
            g_ftp_devoptab_registered = false;
        }
        g_ftp_mounted_name.clear();
        g_ftp_mounted_path.clear();
        UpdateFtpVisibleDevices();
        return;
    }

    std::string clean = path;
    while (clean.length() > 1 && clean.back() == '/') {
        clean.pop_back();
    }

    const char* leaf = std::strrchr(clean.c_str(), '/');
    std::string name = (leaf && leaf[1]) ? (leaf + 1) : "mounted";

    if (name == "sd" || name == "sdmc" || name == "install") {
        name += "_folder";
    }

    if (g_ftp_devoptab_registered && !g_ftp_mounted_name.empty()) {
        RemoveDevice(g_ftp_mounted_name.c_str());
        g_ftp_devoptab_registered = false;
    }

    g_ftp_mounted_name = name;
    g_ftp_mounted_path = "sdmc:" + clean;

    std::memset(&g_ftp_devoptab, 0, sizeof(g_ftp_devoptab));
    g_ftp_devoptab.name = g_ftp_mounted_name.c_str();
    g_ftp_devoptab.structSize = sizeof(devoptab_t);
    g_ftp_devoptab.open_r = ftp_mount_open;
    g_ftp_devoptab.close_r = ftp_mount_close;
    g_ftp_devoptab.read_r = ftp_mount_read;
    g_ftp_devoptab.write_r = ftp_mount_write;
    g_ftp_devoptab.seek_r = ftp_mount_seek;
    g_ftp_devoptab.fstat_r = ftp_mount_fstat;
    g_ftp_devoptab.stat_r = ftp_mount_stat;
    g_ftp_devoptab.unlink_r = ftp_mount_unlink;
    g_ftp_devoptab.mkdir_r = ftp_mount_mkdir;
    g_ftp_devoptab.rmdir_r = ftp_mount_rmdir;
    g_ftp_devoptab.rename_r = ftp_mount_rename;
    g_ftp_devoptab.dirStateSize = sizeof(FtpDirState);
    g_ftp_devoptab.diropen_r = ftp_mount_diropen;
    g_ftp_devoptab.dirreset_r = ftp_mount_dirreset;
    g_ftp_devoptab.dirnext_r = ftp_mount_dirnext;
    g_ftp_devoptab.dirclose_r = ftp_mount_dirclose;

    AddDevice(&g_ftp_devoptab);
    g_ftp_devoptab_registered = true;

    UpdateFtpVisibleDevices();
}

void ClearFtpMountedFolder() {
    SCOPED_MUTEX(&g_mutex);
    if (g_ftp_devoptab_registered && !g_ftp_mounted_name.empty()) {
        RemoveDevice(g_ftp_mounted_name.c_str());
        g_ftp_devoptab_registered = false;
    }
    g_ftp_mounted_name.clear();
    g_ftp_mounted_path.clear();
    UpdateFtpVisibleDevices();
}

std::string GetFtpMountedName() {
    SCOPED_MUTEX(&g_mutex);
    return g_ftp_mounted_name;
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
