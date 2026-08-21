# Patches the fetched ftpsrv library. Run as a FetchContent PATCH_COMMAND
# (working directory = ftpsrv source root):
#   ${CMAKE_COMMAND} -P .../patch_ftpsrv.cmake
#
# Each patch is idempotent: skipped if its marker is already present.
#
# 1. anonymous (no login) FTP: accept ANY username/password, including empty.
#    upstream only accepts the literal "anonymous" and rejects empty USER with 501.
# 2. restrict which root devices are exposed: sphaira exposes only the microSD
#    card and the install folder over FTP (all the NAND/system mounts are hidden).
# 3. root-drop routing: let sphaira claim writes aimed at the root of the server
#    (the level that lists the card and the install folder), so an installable
#    file dropped there is installed instead of copied -- same as the MTP root.

# --- 1. src/ftpsrv.c : anonymous login ------------------------------------
if(EXISTS "src/ftpsrv.c")
    file(READ "src/ftpsrv.c" src)
    if(src MATCHES "sphaira: anonymous access")
        message(STATUS "[ftpsrv-patch] ftpsrv.c already patched")
    else()
        set(user_old
"    if (rc <= 0 || rc >= sizeof(username)) {
        ftp_client_msg(session, 501, \"Syntax error in parameters or arguments.\");
    } else if (g_ftp.cfg.anon) {
        if (strcmp(username, \"anonymous\")) {
            ftp_client_msg(session, 530, \"Not logged in.\");
        } else {
            session->auth_mode = FTP_AUTH_MODE_VALID;
            ftp_client_msg(session, 230, \"User logged in, proceed.\");
        }
    } else if (strcmp(username, g_ftp.cfg.user)) {")
        set(user_new
"    if (g_ftp.cfg.anon) {
        /* sphaira: anonymous access accepts any (or empty) username. */
        session->auth_mode = FTP_AUTH_MODE_VALID;
        ftp_client_msg(session, 230, \"User logged in, proceed.\");
    } else if (rc <= 0 || rc >= sizeof(username)) {
        ftp_client_msg(session, 501, \"Syntax error in parameters or arguments.\");
    } else if (strcmp(username, g_ftp.cfg.user)) {")
        set(pass_old
"    if (rc <= 0 || rc >= sizeof(password)) {
        ftp_client_msg(session, 501, \"Syntax error in parameters or arguments.\");
    } else if (session->auth_mode != FTP_AUTH_MODE_NEED_PASS) {")
        set(pass_new
"    if (g_ftp.cfg.anon) {
        /* sphaira: anonymous access accepts any password (clients may send PASS after 230). */
        session->auth_mode = FTP_AUTH_MODE_VALID;
        ftp_client_msg(session, 230, \"User logged in, proceed.\");
    } else if (rc <= 0 || rc >= sizeof(password)) {
        ftp_client_msg(session, 501, \"Syntax error in parameters or arguments.\");
    } else if (session->auth_mode != FTP_AUTH_MODE_NEED_PASS) {")
        string(REPLACE "${user_old}" "${user_new}" src "${src}")
        string(REPLACE "${pass_old}" "${pass_new}" src "${src}")
        if(NOT src MATCHES "sphaira: anonymous access")
            message(FATAL_ERROR "[ftpsrv-patch] failed to apply anon-login patch to ftpsrv.c")
        endif()
        file(WRITE "src/ftpsrv.c" "${src}")
        message(STATUS "[ftpsrv-patch] applied anonymous-login patch")
    endif()
else()
    message(STATUS "[ftpsrv-patch] src/ftpsrv.c not found (cwd unexpected), skipping")
endif()

# --- 2a. src/platform/nx/vfs_nx.h : declare the visibility setter ----------
set(vfs_h "src/platform/nx/vfs_nx.h")
if(EXISTS "${vfs_h}")
    file(READ "${vfs_h}" hdr)
    if(hdr MATCHES "sphaira: restrict which root devices")
        message(STATUS "[ftpsrv-patch] vfs_nx.h already patched")
    else()
        set(h_old
"void vfs_nx_init(const struct VfsNxCustomPath* custom, bool enable_devices, bool save_writable, bool mount_bis, bool skip_ascii_convert);
void vfs_nx_exit(void);")
        set(h_new
"void vfs_nx_init(const struct VfsNxCustomPath* custom, bool enable_devices, bool save_writable, bool mount_bis, bool skip_ascii_convert);
/* sphaira: restrict which root devices are exposed (names without ':'). empty list = all. */
void vfs_nx_set_visible_devices(const char* const* names, unsigned count);
void vfs_nx_exit(void);")
        string(REPLACE "${h_old}" "${h_new}" hdr "${hdr}")
        if(NOT hdr MATCHES "sphaira: restrict which root devices")
            message(FATAL_ERROR "[ftpsrv-patch] failed to apply visibility decl to vfs_nx.h")
        endif()
        file(WRITE "${vfs_h}" "${hdr}")
        message(STATUS "[ftpsrv-patch] applied vfs_nx.h visibility decl")
    endif()
else()
    message(STATUS "[ftpsrv-patch] ${vfs_h} not found, skipping")
endif()

# --- 2b. src/platform/nx/vfs_nx.c : implement + apply the allowlist --------
set(vfs_c "src/platform/nx/vfs_nx.c")
if(EXISTS "${vfs_c}")
    file(READ "${vfs_c}" impl)
    if(impl MATCHES "sphaira: optional allowlist of visible root")
        message(STATUS "[ftpsrv-patch] vfs_nx.c already patched")
    else()
        set(c_old
"void vfs_nx_add_device(const char* name, enum VFS_TYPE type) {
    if (g_device_count >= DEVICE_NUM) {
        return;
    }")
        set(c_new
"/* sphaira: optional allowlist of visible root device names (without ':'). */
static char g_sphaira_visible[DEVICE_NUM][32];
static unsigned g_sphaira_visible_count = 0;

void vfs_nx_set_visible_devices(const char* const* names, unsigned count) {
    if (count > DEVICE_NUM) {
        count = DEVICE_NUM;
    }
    g_sphaira_visible_count = count;
    for (unsigned i = 0; i < count; i++) {
        snprintf(g_sphaira_visible[i], sizeof(g_sphaira_visible[i]), \"%s\", names[i]);
    }
}

static int vfs_nx_device_visible(const char* name) {
    if (g_sphaira_visible_count == 0) {
        return 1;
    }
    for (unsigned i = 0; i < g_sphaira_visible_count; i++) {
        if (!strcmp(name, g_sphaira_visible[i])) {
            return 1;
        }
    }
    return 0;
}

void vfs_nx_add_device(const char* name, enum VFS_TYPE type) {
    if (!vfs_nx_device_visible(name)) {
        return;
    }
    if (g_device_count >= DEVICE_NUM) {
        return;
    }")
        string(REPLACE "${c_old}" "${c_new}" impl "${impl}")
        if(NOT impl MATCHES "sphaira: optional allowlist of visible root")
            message(FATAL_ERROR "[ftpsrv-patch] failed to apply allowlist to vfs_nx.c")
        endif()
        file(WRITE "${vfs_c}" "${impl}")
        message(STATUS "[ftpsrv-patch] applied vfs_nx.c device allowlist")
    endif()
else()
    message(STATUS "[ftpsrv-patch] ${vfs_c} not found, skipping")
endif()

# --- 3a. src/platform/nx/vfs_nx.h : declare the root-drop router setter -----
# separate block (own marker) so it still applies to a checkout that already has
# the patches above.
if(EXISTS "${vfs_h}")
    file(READ "${vfs_h}" hdr)
    if(hdr MATCHES "sphaira: claim writes to the server root")
        message(STATUS "[ftpsrv-patch] vfs_nx.h root router already patched")
    else()
        set(rh_old
"void vfs_nx_exit(void);")
        set(rh_new
"/* sphaira: claim writes to the server root. called for a write-mode open whose
 * path has no device prefix; return non-zero to send it to the custom vfs
 * (the install folder) instead of the microSD card. NULL = never route. */
void vfs_nx_set_root_write_router(int (*router)(const char* path));
void vfs_nx_exit(void);")
        string(REPLACE "${rh_old}" "${rh_new}" hdr "${hdr}")
        if(NOT hdr MATCHES "sphaira: claim writes to the server root")
            message(FATAL_ERROR "[ftpsrv-patch] failed to apply root router decl to vfs_nx.h")
        endif()
        file(WRITE "${vfs_h}" "${hdr}")
        message(STATUS "[ftpsrv-patch] applied vfs_nx.h root router decl")
    endif()
endif()

# --- 3b. src/platform/nx/vfs_nx.c : root-drop routing hook -----------------
if(EXISTS "${vfs_c}")
    file(READ "${vfs_c}" impl)
    if(impl MATCHES "sphaira: root-drop routing")
        message(STATUS "[ftpsrv-patch] vfs_nx.c root router already patched")
    else()
        set(r_old
"int ftp_vfs_open(struct FtpVfsFile* f, const char* path, enum FtpVfsOpenMode mode) {
    f->type = get_type(path);
    return g_vfs[f->type]->open(&f->root, fix_path(path, f->type), mode);
}")
        set(r_new
"/* sphaira: root-drop routing -- see vfs_nx_set_root_write_router(). */
static int (*g_sphaira_root_router)(const char* path) = NULL;

void vfs_nx_set_root_write_router(int (*router)(const char* path)) {
    g_sphaira_root_router = router;
}

int ftp_vfs_open(struct FtpVfsFile* f, const char* path, enum FtpVfsOpenMode mode) {
    f->type = get_type(path);

    /* sphaira: a write aimed at the server root (a path with no device prefix,
     * which would otherwise land on the microSD card) may be claimed by the
     * custom vfs so the file is installed instead of copied. */
    if (mode == FtpVfsOpenMode_WRITE && f->type == VFS_TYPE_FS &&
        g_vfs[VFS_TYPE_USER] && g_sphaira_root_router && g_sphaira_root_router(path)) {
        f->type = VFS_TYPE_USER;
        return g_vfs[VFS_TYPE_USER]->open(&f->root, path, mode);
    }

    return g_vfs[f->type]->open(&f->root, fix_path(path, f->type), mode);
}")
        string(REPLACE "${r_old}" "${r_new}" impl "${impl}")
        if(NOT impl MATCHES "sphaira: root-drop routing")
            message(FATAL_ERROR "[ftpsrv-patch] failed to apply root router to vfs_nx.c")
        endif()
        file(WRITE "${vfs_c}" "${impl}")
        message(STATUS "[ftpsrv-patch] applied vfs_nx.c root-drop router")
    endif()
endif()

# --- 4a. src/platform/nx/vfs_nx.h : declare filesystem mutation callback ---
if(EXISTS "${vfs_h}")
    file(READ "${vfs_h}" hdr)
    if(hdr MATCHES "sphaira: filesystem mutation notifications")
        message(STATUS "[ftpsrv-patch] vfs_nx.h mutation notifications already patched")
    else()
        set(mh_old
"void vfs_nx_exit(void);")
        set(mh_new
"/* sphaira: filesystem mutation notifications */
typedef enum VfsNxMutation {
    VFS_NX_MUTATION_FILE_CREATED = 0,
    VFS_NX_MUTATION_FILE_DELETED = 1,
    VFS_NX_MUTATION_DIR_CREATED  = 2,
    VFS_NX_MUTATION_DIR_DELETED  = 3,
    VFS_NX_MUTATION_RENAME_FILE  = 4,
    VFS_NX_MUTATION_RENAME_DIR   = 5,
} VfsNxMutation;

typedef void (*vfs_nx_mutation_cb)(enum VfsNxMutation type, const char* path1, const char* path2);
void vfs_nx_set_mutation_callback(vfs_nx_mutation_cb cb);
void vfs_nx_exit(void);")
        string(REPLACE "${mh_old}" "${mh_new}" hdr "${hdr}")
        if(NOT hdr MATCHES "sphaira: filesystem mutation notifications")
            message(FATAL_ERROR "[ftpsrv-patch] failed to apply mutation decl to vfs_nx.h")
        endif()
        file(WRITE "${vfs_h}" "${hdr}")
        message(STATUS "[ftpsrv-patch] applied vfs_nx.h mutation notifications decl")
    endif()
endif()

# --- 4b. src/platform/nx/vfs/vfs_nx_fs.h : track open write path & fs -------
set(vfs_fs_h "src/platform/nx/vfs/vfs_nx_fs.h")
if(EXISTS "${vfs_fs_h}")
    file(READ "${vfs_fs_h}" fsh)
    if(fsh MATCHES "sphaira: track open write path")
        message(STATUS "[ftpsrv-patch] vfs_nx_fs.h already patched")
    else()
        set(fh_old
"struct VfsFsFile {
    FsFile fd;
    s64 off;
    s64 chunk_size;
    bool is_write;
    bool is_valid;")
        set(fh_new
"struct VfsFsFile {
    FsFile fd;
    s64 off;
    s64 chunk_size;
    bool is_write;
    bool is_valid;
    /* sphaira: track open write path & fs */
    FsFileSystem* fs;
    char nxpath[FS_MAX_PATH];")
        string(REPLACE "${fh_old}" "${fh_new}" fsh "${fsh}")
        if(NOT fsh MATCHES "sphaira: track open write path")
            message(FATAL_ERROR "[ftpsrv-patch] failed to apply file tracking to vfs_nx_fs.h")
        endif()
        file(WRITE "${vfs_fs_h}" "${fsh}")
        message(STATUS "[ftpsrv-patch] applied vfs_nx_fs.h file tracking")
    endif()
endif()

# --- 4c. src/platform/nx/vfs/vfs_nx_fs.c : mutation callbacks on success ---
set(vfs_fs_c "src/platform/nx/vfs/vfs_nx_fs.c")
if(EXISTS "${vfs_fs_c}")
    file(READ "${vfs_fs_c}" fsc)
    if(fsc MATCHES "sphaira: filesystem mutation callbacks")
        message(STATUS "[ftpsrv-patch] vfs_nx_fs.c already patched")
    else()
        set(fc_decl_old
"#include \"ftpsrv_vfs.h\"
#include \"../utils.h\"")
        set(fc_decl_new
"#include \"ftpsrv_vfs.h\"
#include \"../vfs_nx.h\"
#include \"../utils.h\"
#include <stdio.h>

/* sphaira: filesystem mutation callbacks */
static vfs_nx_mutation_cb g_sphaira_mutation_cb = NULL;

void vfs_nx_set_mutation_callback(vfs_nx_mutation_cb cb) {
    g_sphaira_mutation_cb = cb;
}")
        set(fc_open_old
"static int vfs_fs_open(void* user, const char* path, enum FtpVfsOpenMode mode) {
    struct VfsFsFile* f = user;
    FsFileSystem* fs = NULL;
    char nxpath[FS_MAX_PATH];
    if (fsdev_wrapTranslatePath(path, &fs, nxpath)) {
        return -1;
    }
    return vfs_fs_internal_open(fs, f, nxpath, mode);
}")
        set(fc_open_new
"static int vfs_fs_open(void* user, const char* path, enum FtpVfsOpenMode mode) {
    struct VfsFsFile* f = user;
    FsFileSystem* fs = NULL;
    char nxpath[FS_MAX_PATH];
    if (fsdev_wrapTranslatePath(path, &fs, nxpath)) {
        return -1;
    }
    int ret = vfs_fs_internal_open(fs, f, nxpath, mode);
    if (ret == 0 && (mode == FtpVfsOpenMode_WRITE || mode == FtpVfsOpenMode_APPEND)) {
        f->fs = fs;
        snprintf(f->nxpath, sizeof(f->nxpath), \"%s\", nxpath);
    }
    return ret;
}")
        set(fc_close_old
"static int vfs_fs_close(void* user) {
    struct VfsFsFile* f = user;
    if (!vfs_fs_isfile_open(f)) {
        return -1;
    }
    return vfs_fs_internal_close(f);
}")
        set(fc_close_new
"static int vfs_fs_close(void* user) {
    struct VfsFsFile* f = user;
    if (!vfs_fs_isfile_open(f)) {
        return -1;
    }
    const bool is_write = f->is_write;
    char nxpath[FS_MAX_PATH];
    FsFileSystem* fs = f->fs;
    if (is_write) {
        snprintf(nxpath, sizeof(nxpath), \"%s\", f->nxpath);
    }
    int ret = vfs_fs_internal_close(f);
    if (ret == 0 && is_write && g_sphaira_mutation_cb) {
        if (fs) {
            fsFsCommit(fs);
        }
        g_sphaira_mutation_cb(VFS_NX_MUTATION_FILE_CREATED, nxpath, NULL);
    }
    return ret;
}")
        set(fc_ops_old
"static int vfs_fs_mkdir(const char* path) {
    FsFileSystem* fs = NULL;
    char nxpath[FS_MAX_PATH];
    if (fsdev_wrapTranslatePath(path, &fs, nxpath)) {
        return -1;
    }
    return vfs_fs_internal_mkdir(fs, nxpath);
}

static int vfs_fs_unlink(const char* path) {
    FsFileSystem* fs = NULL;
    char nxpath[FS_MAX_PATH];
    if (fsdev_wrapTranslatePath(path, &fs, nxpath)) {
        return -1;
    }
    return vfs_fs_internal_unlink(fs, nxpath);
}

static int vfs_fs_rmdir(const char* path) {
    FsFileSystem* fs = NULL;
    char nxpath[FS_MAX_PATH];
    if (fsdev_wrapTranslatePath(path, &fs, nxpath)) {
        return -1;
    }
    return vfs_fs_internal_rmdir(fs, nxpath);
}

static int vfs_fs_rename(const char* src, const char* dst) {
    FsFileSystem* fs = NULL;
    FsFileSystem* fs_dst = NULL;
    char nxpath_src[FS_MAX_PATH];
    char nxpath_dst[FS_MAX_PATH];
    if (fsdev_wrapTranslatePath(src, &fs, nxpath_src) || fsdev_wrapTranslatePath(dst, &fs_dst, nxpath_dst)) {
        return -1;
    }

    // cannot rename across different fs.
    if (fs != fs_dst) {
        errno = EXDEV; // this will do for the error.
        return -1;
    }

    return vfs_fs_internal_rename(fs, nxpath_src, nxpath_dst);
}")
        set(fc_ops_new
"static int vfs_fs_mkdir(const char* path) {
    FsFileSystem* fs = NULL;
    char nxpath[FS_MAX_PATH];
    if (fsdev_wrapTranslatePath(path, &fs, nxpath)) {
        return -1;
    }
    int ret = vfs_fs_internal_mkdir(fs, nxpath);
    if (ret == 0 && g_sphaira_mutation_cb) {
        fsFsCommit(fs);
        g_sphaira_mutation_cb(VFS_NX_MUTATION_DIR_CREATED, nxpath, NULL);
    }
    return ret;
}

static int vfs_fs_unlink(const char* path) {
    FsFileSystem* fs = NULL;
    char nxpath[FS_MAX_PATH];
    if (fsdev_wrapTranslatePath(path, &fs, nxpath)) {
        return -1;
    }
    int ret = vfs_fs_internal_unlink(fs, nxpath);
    if (ret == 0 && g_sphaira_mutation_cb) {
        fsFsCommit(fs);
        g_sphaira_mutation_cb(VFS_NX_MUTATION_FILE_DELETED, nxpath, NULL);
    }
    return ret;
}

static int vfs_fs_rmdir(const char* path) {
    FsFileSystem* fs = NULL;
    char nxpath[FS_MAX_PATH];
    if (fsdev_wrapTranslatePath(path, &fs, nxpath)) {
        return -1;
    }
    int ret = vfs_fs_internal_rmdir(fs, nxpath);
    if (ret == 0 && g_sphaira_mutation_cb) {
        fsFsCommit(fs);
        g_sphaira_mutation_cb(VFS_NX_MUTATION_DIR_DELETED, nxpath, NULL);
    }
    return ret;
}

static int vfs_fs_rename(const char* src, const char* dst) {
    FsFileSystem* fs = NULL;
    FsFileSystem* fs_dst = NULL;
    char nxpath_src[FS_MAX_PATH];
    char nxpath_dst[FS_MAX_PATH];
    if (fsdev_wrapTranslatePath(src, &fs, nxpath_src) || fsdev_wrapTranslatePath(dst, &fs_dst, nxpath_dst)) {
        return -1;
    }

    // cannot rename across different fs.
    if (fs != fs_dst) {
        errno = EXDEV; // this will do for the error.
        return -1;
    }

    FsDirEntryType type = FsDirEntryType_File;
    fsFsGetEntryType(fs, nxpath_src, &type);

    int ret = vfs_fs_internal_rename(fs, nxpath_src, nxpath_dst);
    if (ret == 0 && g_sphaira_mutation_cb) {
        fsFsCommit(fs);
        g_sphaira_mutation_cb(type == FsDirEntryType_Dir ? VFS_NX_MUTATION_RENAME_DIR : VFS_NX_MUTATION_RENAME_FILE, nxpath_src, nxpath_dst);
    }
    return ret;
}")
        string(REPLACE "${fc_decl_old}" "${fc_decl_new}" fsc "${fsc}")
        string(REPLACE "${fc_open_old}" "${fc_open_new}" fsc "${fsc}")
        string(REPLACE "${fc_close_old}" "${fc_close_new}" fsc "${fsc}")
        string(REPLACE "${fc_ops_old}" "${fc_ops_new}" fsc "${fsc}")
        if(NOT fsc MATCHES "sphaira: filesystem mutation callbacks")
            message(FATAL_ERROR "[ftpsrv-patch] failed to apply mutation callbacks to vfs_nx_fs.c")
        endif()
        file(WRITE "${vfs_fs_c}" "${fsc}")
        message(STATUS "[ftpsrv-patch] applied vfs_nx_fs.c mutation callbacks")
    endif()
endif()
