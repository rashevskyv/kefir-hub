#pragma once

#include "app.hpp"
#include "fs.hpp"
#include "ui/progress_box.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/menus/save/save_paths.hpp"
#include <minizip/zip.h>
#include <minizip/unzip.h>
#include <cstdio>
#include <string>

namespace sphaira::ui::menu::save {

struct RecoveryStreamContext {
    std::FILE* fp = nullptr;
    bool write_failed = false;
    bool flush_failed = false;
    bool sync_failed = false;
    bool close_failed = false;
};

voidpf RecoveryOpen(voidpf opaque, const void* filename, int mode);
ZPOS64_T RecoveryTell(voidpf opaque, voidpf stream);
long RecoverySeek(voidpf opaque, voidpf stream, ZPOS64_T offset, int origin);
uLong RecoveryRead(voidpf opaque, voidpf stream, void* buf, uLong size);
uLong RecoveryWrite(voidpf opaque, voidpf stream, const void* buf, uLong size);
int RecoveryClose(voidpf opaque, voidpf stream);
int RecoveryError(voidpf opaque, voidpf stream);

bool IsInvalidSavePathChar(char c);

Result WriteSaveBackupZip(
    ProgressBox* pbox,
    fs::Fs* target_fs,
    const fs::FsPath& temp_path,
    fs::Fs* save_fs,
    const Entry& e,
    const FsSaveDataExtraData& extra,
    const filebrowser::FsDirCollections& collections,
    const std::string& account_name,
    bool dbi_format,
    bool compressed,
    bool recovery_mode = false,
    bool checked_stream = false);

} // namespace sphaira::ui::menu::save
