#include "auto_update.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "path_util.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string_view>

namespace sphaira::auto_update {

fs::FsPath ResolveInstallDestination(const fs::FsPath& running_exe_path) {
    if (!running_exe_path.empty() && strcasecmp(running_exe_path.s, "/hbmenu.nro") != 0) {
        return running_exe_path;
    }

    fs::FsNativeSd fs;
    if (fs.FileExists("/switch/kefir-hub.nro")) {
        return "/switch/kefir-hub.nro";
    }
    if (fs.FileExists("/switch/sphaira/sphaira.nro")) {
        return "/switch/sphaira/sphaira.nro";
    }
    if (fs.FileExists("/switch/kefir-hub/kefir-hub.nro")) {
        return "/switch/kefir-hub/kefir-hub.nro";
    }
    if (fs.FileExists("/switch/sphaira.nro")) {
        return "/switch/sphaira.nro";
    }

    // Default standard path
    return "/switch/sphaira/sphaira.nro";
}

bool InstallNroUpdate(const fs::FsPath& staging_path, const fs::FsPath& dest_path, bool replace_hbmenu) {
    fs::FsNativeSd fs;
    if (!fs.FileExists(staging_path)) {
        log_write("[AutoUpdate] Staging file does not exist: %s\n", staging_path.s);
        return false;
    }

    fs::File file;
    if (R_FAILED(fs.OpenFile(staging_path, FsOpenMode_Read, &file))) {
        log_write("[AutoUpdate] Failed to open staging file: %s\n", staging_path.s);
        return false;
    }

    s64 file_size = 0;
    const Result rc = file.GetSize(&file_size);
    file.Close();

    if (R_FAILED(rc) || file_size < 1024) {
        log_write("[AutoUpdate] Staging file too small: %ld bytes\n", file_size);
        return false;
    }

    fs.CreateDirectoryRecursivelyWithPath(dest_path);

    // Delete existing destination file before copy to ensure clean overwrite
    fs.DeleteFile(dest_path);

    if (R_FAILED(fs.copy_entire_file(dest_path, staging_path))) {
        log_write("[AutoUpdate] Failed to copy %s to %s\n", staging_path.s, dest_path.s);
        return false;
    }

    if (replace_hbmenu) {
        fs.DeleteFile("/hbmenu.nro");
        if (R_FAILED(fs.copy_entire_file("/hbmenu.nro", staging_path))) {
            log_write("[AutoUpdate] Warning: failed to copy update to /hbmenu.nro\n");
        }
    }

    return true;
}

} // namespace sphaira::auto_update

