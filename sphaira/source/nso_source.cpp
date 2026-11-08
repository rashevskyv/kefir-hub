// Where the Nintendo Switch Online add flow gets its facts: installed apps, the overlay, the official game list.

#include "nso_internal.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "repack/program_romfs.hpp"
#include "repack/romfs_image.hpp"
#include "title_info.hpp"
#include "title_nsp.hpp"
#include "yati/nx/keys.hpp"

#include <algorithm>

namespace sphaira::nso_ui {
namespace {

constexpr u64 MAX_READ = 64ull * 1024 * 1024;

struct Romfs {
    repack::ProgramRomfs game;
    std::vector<repack::RomfsFile> files;
};

auto LatestProgram(u64 tid, u32 flags, title::ContentInfoEntry& out) -> bool {
    title::MetaEntries metas;
    if (R_FAILED(title::GetMetaEntries(tid, metas, flags)) || metas.empty()) return false;
    const auto it = std::ranges::max_element(metas, {}, [](const auto& m) { return m.version; });
    return R_SUCCEEDED(title::BuildContentEntry(*it, out));
}

auto FindProgram(title::ContentInfoEntry& e) -> NcmContentInfo* {
    const auto it = std::ranges::find_if(e.content_infos, [](const auto& c) {
        return c.content_type == NcmContentType_Program && c.id_offset == 0;
    });
    return it == e.content_infos.end() ? nullptr : &*it;
}

// The installed app's romfs, the update on top of the base, as the game sees it. Needs prod.keys.
bool OpenRomfs(nso::System s, Romfs& out, std::string& error) {
    const u64 tid = nso::Info(s).base_tid;
    keys::Keys keys;
    if (R_FAILED(keys::parse_keys(keys, true))) {
        error = "prod.keys is needed to read the official game list";
        return false;
    }
    title::ContentInfoEntry base;
    NcmContentInfo* base_prog = nullptr;
    if (!LatestProgram(tid, title::ContentFlag_Application, base) || !(base_prog = FindProgram(base))) {
        error = "The Nintendo Switch Online app for this ROM is not installed";
        return false;
    }
    title::ContentInfoEntry patch;
    NcmContentInfo* patch_prog = nullptr;
    if (LatestProgram(tid, title::ContentFlag_Patch, patch)) patch_prog = FindProgram(patch);

    auto& base_cs = title::GetNcmCs(base.status.storageID);
    auto* patch_cs = patch_prog ? &title::GetNcmCs(patch.status.storageID) : nullptr;
    Result rc = repack::OpenProgramRomfs(keys, &base_cs, base_prog->content_id, patch_cs, patch_prog ? &patch_prog->content_id : nullptr, out.game);
    if (R_SUCCEEDED(rc)) {
        const auto g = out.game;
        rc = repack::ListImage([g](void* buf, u64 off, u64 size) { return g.storage->Read2(buf, g.offset + off, size); }, out.files);
    }
    if (R_FAILED(rc)) {
        log_write("[nso] cannot read the romfs of %016lX: 0x%X\n", tid, rc);
        error = "Cannot read the official game list from the Nintendo Switch Online app";
        return false;
    }
    return true;
}

bool ReadRomfsFile(const Romfs& r, const repack::RomfsFile& f, std::vector<u8>& out) {
    if (f.size > MAX_READ) return false;
    out.resize(f.size);
    return R_SUCCEEDED(r.game.storage->Read2(out.data(), r.game.offset + f.offset, f.size));
}

auto ReadText(fs::FsNativeSd& sd, const fs::FsPath& path, std::string& out) -> bool {
    std::vector<u8> bytes;
    if (R_FAILED(sd.read_entire_file(path, bytes))) return false;
    out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
}

} // namespace

auto OverlayPath(nso::System s, const std::string& rel) -> fs::FsPath {
    return fs::FsPath{nso::JoinPath(nso::OverlayRomfs(s), rel).c_str()};
}

auto DetectInstalled() -> nso::Installed {
    nso::Installed r{};
    for (const auto& s : nso::kSystems) {
        title::MetaEntries metas;
        r[size_t(s.id)] = R_SUCCEEDED(title::GetMetaEntries(s.base_tid, metas, title::ContentFlag_Application)) && !metas.empty();
    }
    return r;
}

// a run cut between the two renames of WriteAtomic leaves the old db as .bak: put it back
void RecoverDb(fs::FsNativeSd& sd, const fs::FsPath& db) {
    const fs::FsPath bak{(std::string{db.s} + ".bak").c_str()};
    if (!sd.FileExists(db) && sd.FileExists(bak)) sd.RenameFile(bak, db);
}

bool LoadOverlayOnly(nso::System s, SystemState& st) {
    fs::FsNativeSd sd;
    RecoverDb(sd, fs::FsPath{OverlayPath(s, nso::DbRelPath())});
    const auto marker = OverlayPath(s, nso::MarkerRelPath());
    if (!sd.FileExists(marker) || !ReadText(sd, marker, st.marker_text)) return false;
    const auto db = OverlayPath(s, nso::DbRelPath());
    st.overlay_db = sd.FileExists(db) && ReadText(sd, db, st.db_text);
    st.hub_created_db = sd.FileExists(OverlayPath(s, nso::CreatedFlagRelPath()));
    return true;
}

bool LoadState(nso::System s, SystemState& st) {
    if (st.loaded) return st.error.empty();
    st.loaded = true;
    fs::FsNativeSd sd;
    const auto db = OverlayPath(s, nso::DbRelPath());
    RecoverDb(sd, fs::FsPath{db});
    if (sd.FileExists(db)) {
        // our overlay or a mod of someone else: edited in place
        if (!ReadText(sd, db, st.db_text)) {
            st.error = "Cannot read the game list of the overlay";
            return false;
        }
        st.overlay_db = true;
    } else {
        Romfs romfs;
        if (!OpenRomfs(s, romfs, st.error)) return false;
        const auto it = std::ranges::find(romfs.files, nso::DbRelPath(), &repack::RomfsFile::path);
        std::vector<u8> bytes;
        if (it == romfs.files.end() || !ReadRomfsFile(romfs, *it, bytes) || bytes.empty()) {
            st.error = "Cannot read the official game list from the Nintendo Switch Online app";
            return false;
        }
        st.db_text.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    const auto marker = OverlayPath(s, nso::MarkerRelPath());
    if (sd.FileExists(marker)) ReadText(sd, marker, st.marker_text);
    st.hub_created_db = sd.FileExists(OverlayPath(s, nso::CreatedFlagRelPath()));
    return true;
}

auto ReadDonorSfromsig() -> std::vector<u8> {
    std::vector<u8> sig(256, 0);
    Romfs romfs;
    std::string error;
    if (OpenRomfs(nso::System::Snes, romfs, error)) {
        std::vector<std::string> paths;
        for (const auto& f : romfs.files) paths.push_back(f.path);
        const auto want = nso::FindSfromsig(paths);
        const auto it = std::ranges::find(romfs.files, want, &repack::RomfsFile::path);
        std::vector<u8> bytes;
        if (!want.empty() && it != romfs.files.end() && ReadRomfsFile(romfs, *it, bytes) && bytes.size() == 256) {
            return bytes;
        }
    }
    log_write("[nso] no donor .sfromsig, writing zeros (%s)\n", error.c_str());
    return sig;
}

} // namespace sphaira::nso_ui
