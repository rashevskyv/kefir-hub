#include "repack/repack.hpp"
#include "repack/nca_build.hpp"
#include "repack/romfs_image.hpp"

#include "app.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "title_info.hpp"
#include "ui/progress_box.hpp"

#include <algorithm>

namespace sphaira::repack {
namespace {

auto FindContent(title::ContentInfoEntry& e, u8 type) -> NcmContentInfo* {
    const auto it = std::ranges::find_if(e.content_infos, [type](const auto& c) {
        return c.content_type == type && c.id_offset == 0;
    });
    return it == e.content_infos.end() ? nullptr : &*it;
}

auto FindComponent(std::vector<title::ContentInfoEntry>& entries, u8 meta_type) -> title::ContentInfoEntry* {
    const auto it = std::ranges::find_if(entries, [meta_type](const auto& e) { return e.status.meta_type == meta_type; });
    return it == entries.end() ? nullptr : &*it;
}

// the installed base, also when the repack leaves it out: the update's romfs patches it.
Result GetBase(u64 app_id, std::vector<title::ContentInfoEntry>& entries, title::ContentInfoEntry& out) {
    if (const auto base = FindComponent(entries, NcmContentMetaType_Application)) {
        out = *base;
        R_SUCCEED();
    }
    title::MetaEntries metas;
    R_TRY(title::GetMetaEntries(app_id, metas, title::ContentFlag_Application));
    R_UNLESS(!metas.empty(), Result_RepackUnsupportedNca);
    return title::BuildContentEntry(metas.front(), out);
}

Result BakeMod(ui::ProgressBox* pbox, u64 app_id, std::vector<title::ContentInfoEntry>& entries, std::vector<title::NspEntry::Generated>& generated) {
    auto target = FindComponent(entries, NcmContentMetaType_Patch);
    const bool is_patch = target;
    if (!target) {
        target = FindComponent(entries, NcmContentMetaType_Application);
    }
    R_UNLESS(target, Result_RepackUnsupportedNca);

    title::ContentInfoEntry base;
    R_TRY(GetBase(app_id, entries, base));
    const auto base_prog = FindContent(base, NcmContentType_Program);
    const auto prog = FindContent(*target, NcmContentType_Program);
    const auto meta = FindContent(*target, NcmContentType_Meta);
    R_UNLESS(base_prog && prog && meta, Result_RepackUnsupportedNca);

    keys::Keys keys;
    R_TRY(keys::parse_keys(keys, true));
    auto& base_cs = title::GetNcmCs(base.status.storageID);
    auto& cs = title::GetNcmCs(target->status.storageID);

    pbox->NewTransfer("Reading the game romfs"_i18n);
    ProgramRomfs game;
    R_TRY(OpenProgramRomfs(keys, &base_cs, base_prog->content_id, is_patch ? &cs : nullptr, is_patch ? &prog->content_id : nullptr, game));

    std::shared_ptr<yati::source::Base> romfs;
    ModCount count;
    R_TRY(OpenModdedRomfs(pbox, game, title::GetContentsPath(app_id), romfs, &count));
    R_UNLESS(count.replaced || count.added, Result_RepackNoMod);
    s64 romfs_size;
    R_TRY(romfs->GetSize(&romfs_size));

    NcaFile orig;
    R_TRY(OpenNca(keys, &cs, prog->content_id, orig));
    BuiltNca built_prog;
    R_TRY(BuildProgramNca(pbox, keys, orig, romfs, romfs_size, built_prog));

    R_TRY(OpenNca(keys, &cs, meta->content_id, orig));
    BuiltNca built_meta;
    R_TRY(BuildMetaNca(keys, orig, built_prog, built_meta));

    for (auto [info, nca] : {std::pair{prog, &built_prog}, std::pair{meta, &built_meta}}) {
        info->content_id = nca->id;
        ncmU64ToContentInfoSize(nca->size, info);
        generated.push_back({nca->id, nca->data});
    }
    log_write("[repack] mod baked into %016lX v%u: %u files replaced, %u added\n", target->status.application_id, target->status.version, count.replaced, count.added);
    R_SUCCEED();
}

} // namespace

auto HasRomfsMod(u64 app_id) -> bool {
    fs::FsNativeSd sd;
    const auto dir = title::GetContentsPath(app_id);
    return sd.DirExists(fs::AppendPath(dir, "romfs")) || sd.FileExists(fs::AppendPath(dir, "romfs.bin"));
}

void PrepareModRepack(u64 app_id, std::string name, u32 flags, std::function<void(title::NspEntry&&)> on_ready) {
    auto nsp = std::make_shared<title::NspEntry>();
    App::Push<ui::ProgressBox>(0, "Repacking"_i18n, name, [app_id, name, flags, nsp](auto pbox) -> Result {
        R_TRY(title::BuildMergedNspEntry(app_id, name.c_str(), flags & ~FLAG_WITH_MOD, *nsp, [pbox, app_id](auto& entries, auto& generated) {
            return BakeMod(pbox, app_id, entries, generated);
        }));
        // "<name> [...].nsp" -> "<name> [...][MOD].nsp": the file says it is not the plain game.
        std::string path = nsp->path.toString();
        if (path.ends_with(".nsp")) {
            path.insert(path.size() - 4, "[MOD]");
        }
        nsp->path = path.c_str();
        R_SUCCEED();
    }, [nsp, on_ready](Result rc) {
        if (R_SUCCEEDED(rc)) {
            on_ready(std::move(*nsp));
        } else if (rc != Result_TransferCancelled) {
            App::PushErrorBox(rc, rc == Result_RepackNoMod ? "The mods folder has no files."_i18n : "Failed to prepare repack NSP"_i18n);
        }
    });
}

} // namespace sphaira::repack
