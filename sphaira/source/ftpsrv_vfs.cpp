#include "ftpsrv_internal.hpp"
#include "app.hpp"
#include "log.hpp"
#include "ui/menus/dbi_menu.hpp"
#include "ui/menus/install_stream_menu_base.hpp"
#include <algorithm>
#include <cstring>
#include <deque>
#include <string>
#include <vector>
#include <switch.h>
#include <ftpsrv.h>

namespace sphaira::ftpsrv {

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

    if (auto session = App::GetActiveInstallSession()) {
        if (session->GetOrigin() == ui::menu::dbi::TransportOrigin::Ftp) {
            if (!session->HasQueuedFile(path)) {
                session->EnqueueFile(path);
            }
        }
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
    bool should_check_summary{false};
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

                if (g_shared_data.queued_files.empty()) {
                    should_check_summary = true;
                }
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

    if (should_check_summary && !ui::menu::stream::BackgroundInstaller::IsInstalling()) {
        if (auto session = App::GetActiveInstallSession()) {
            if (session->GetOrigin() == ui::menu::dbi::TransportOrigin::Ftp) {
                session->TransitionToSummary();
            }
        }
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

std::vector<std::string> GetQueuedInstallFiles() {
    SCOPED_MUTEX(&g_shared_data.mutex);
    std::vector<std::string> res;
    for (const auto& q : g_shared_data.queued_files) {
        res.push_back(q.path);
    }
    return res;
}

bool HasMoreQueuedFiles() {
    SCOPED_MUTEX(&g_shared_data.mutex);
    return g_shared_data.queued_files.size() > 1;
}

bool HasActiveOrQueuedFiles() {
    SCOPED_MUTEX(&g_shared_data.mutex);
    return !g_shared_data.queued_files.empty() || g_shared_data.in_progress;
}
#else
std::vector<std::string> GetQueuedInstallFiles() {
    return {};
}

bool HasMoreQueuedFiles() {
    return false;
}

bool HasActiveOrQueuedFiles() {
    return false;
}
#endif

} // namespace sphaira::ftpsrv
