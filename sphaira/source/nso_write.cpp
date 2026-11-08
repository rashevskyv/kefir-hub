// Writes and removes the LayeredFS overlay files of the Nintendo Switch Online add flow.
// Only /atmosphere/contents/<tid>/romfs/... is touched, never the app itself.

#include "nso_internal.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "log.hpp"

namespace sphaira::nso_ui {
namespace {

auto P(const std::string& dir, const std::string& rel) -> fs::FsPath {
    return fs::FsPath{nso::JoinPath(dir, rel).c_str()};
}

Result WriteFile(fs::FsNativeSd& sd, const fs::FsPath& path, const std::vector<u8>& data) {
    R_TRY(sd.CreateDirectoryRecursivelyWithPath(path));
    return sd.write_entire_file(path, data);
}

auto ToBytes(const std::string& s) -> std::vector<u8> {
    return {s.begin(), s.end()};
}

// temp file, then rename: the db is never half written. FAT cannot rename over a file: the old one moves to .bak
// first and comes back from there (RecoverDb) if the run is cut before the new one is in place.
Result WriteAtomic(fs::FsNativeSd& sd, const fs::FsPath& path, const std::string& text) {
    const fs::FsPath tmp{(std::string{path.s} + ".tmp").c_str()};
    const fs::FsPath bak{(std::string{path.s} + ".bak").c_str()};
    R_TRY(WriteFile(sd, tmp, ToBytes(text)));
    const bool had = sd.FileExists(path);
    if (had) {
        if (sd.FileExists(bak)) R_TRY(sd.DeleteFile(bak));
        R_TRY(sd.RenameFile(path, bak));
    }
    const auto rc = sd.RenameFile(tmp, path);
    if (R_FAILED(rc)) {
        if (had) sd.RenameFile(bak, path);
        return rc;
    }
    if (had) sd.DeleteFile(bak);
    R_SUCCEED();
}

} // namespace

Result WriteAdd(const nso::AddResult& add, std::span<const u8> rom, const ImageResult& cover, SystemState& state, std::string& error) {
    fs::FsNativeSd sd;
    const auto& root = add.overlay_romfs;
    if (add.files.empty()) { error = "Cannot edit the titles database"; return Result_FsInvalidType; }

    const auto game_dir = P(root, nso::DirPart(add.files.front().path));
    if (sd.DirExists(game_dir)) {
        error = "A folder for this game already exists in the overlay";
        return Result_FsInvalidType;
    }

    Result rc = 0;
    const auto marker_path = P(root, nso::MarkerRelPath());
    const auto flag = P(root, nso::CreatedFlagRelPath());
    const bool flag_before = sd.FileExists(flag);
    const auto fail = [&](const char* msg) {
        error = msg;
        sd.DeleteDirectoryRecursively(game_dir);
        // the marker and the flag go back too: a stale crc in the marker would block the next try
        if (state.marker_text.empty()) sd.DeleteFile(marker_path);
        else WriteFile(sd, marker_path, ToBytes(state.marker_text));
        if (!flag_before) sd.DeleteFile(flag);
        return rc;
    };

    for (const auto& f : add.files) {
        std::vector<u8> data;
        switch (f.kind) {
            case nso::OutFile::Kind::Bytes: data = f.data; break;
            case nso::OutFile::Kind::Source: data.assign(rom.begin(), rom.end()); break;
            case nso::OutFile::Kind::DonorSfromsig: data = ReadDonorSfromsig(); break;
        }
        if (f.compress) {
            data = Zlib9(data);
            if (data.empty()) { rc = Result_FsInvalidType; return fail("Cannot compress the ROM"); }
        }
        rc = WriteFile(sd, P(root, f.path), data);
        if (R_FAILED(rc)) return fail("Cannot write the files to the SD card");
    }
    for (const auto& c : add.covers) {
        const auto data = RenderCover(c, cover);
        if (data.empty()) { rc = Result_FsInvalidType; return fail("Cannot make the cover image"); }
        rc = WriteFile(sd, P(root, c.path), data);
        if (R_FAILED(rc)) return fail("Cannot write the files to the SD card");
    }

    rc = WriteFile(sd, marker_path, ToBytes(add.marker_text));
    if (R_FAILED(rc)) return fail("Cannot write the files to the SD card");
    if (!state.overlay_db && !flag_before) {
        rc = WriteFile(sd, flag, ToBytes("1\n"));  // the Hub made this db: Remove may delete it again
        if (R_FAILED(rc)) return fail("Cannot write the files to the SD card");
        state.hub_created_db = true;
    }
    // the db last: until it is in place the game does not exist for the app
    rc = WriteAtomic(sd, P(root, nso::DbRelPath()), add.db_text);
    if (R_FAILED(rc)) return fail("Cannot write the files to the SD card");

    state.db_text = add.db_text;
    state.marker_text = add.marker_text;
    state.overlay_db = true;
    log_write("[nso] added %s to %s\n", add.code.c_str(), root.c_str());
    R_SUCCEED();
}

Result RemoveAdded(nso::System s, const std::string& code, std::string& error) {
    fs::FsNativeSd sd;
    SystemState st;
    if (!LoadOverlayOnly(s, st) || !st.overlay_db) {
        error = "This game was not added by the Hub";
        return Result_FsInvalidType;
    }
    const auto r = nso::BuildRemove(st.db_text, st.marker_text, code);
    if (!r.ok) {
        error = r.error;
        return Result_FsInvalidType;
    }
    const auto root = nso::OverlayRomfs(s);
    const auto clean = nso::CleanupAfterRemove(r.marker_empty, st.hub_created_db);

    if (!clean.db) R_TRY(WriteAtomic(sd, P(root, nso::DbRelPath()), r.db_text));
    if (clean.marker) {
        R_TRY(sd.DeleteFile(P(root, nso::MarkerRelPath())));
    } else {
        R_TRY(WriteFile(sd, P(root, nso::MarkerRelPath()), ToBytes(r.marker_text)));
    }
    if (clean.db) R_TRY(sd.DeleteFile(P(root, nso::DbRelPath())));
    if (clean.flag) sd.DeleteFile(P(root, nso::CreatedFlagRelPath()));
    // the entry and the marker are already gone: a folder that stays only wastes space, so it is no error
    const auto dir = P(root, r.delete_dir);
    if (sd.DirExists(dir) && R_FAILED(sd.DeleteDirectoryRecursively(dir))) {
        log_write("[nso] cannot delete %s\n", dir.s);
    }
    log_write("[nso] removed %s from %s\n", code.c_str(), root.c_str());
    R_SUCCEED();
}

} // namespace sphaira::nso_ui
