#include "nro.hpp"
#include "defines.hpp"
#include "evman.hpp"
#include "app.hpp"
#include "log.hpp"
#include "nacp_util.hpp"

#include <switch.h>
#include <vector>
#include <cstring>
#include <string_view>
#include <minIni.h>

namespace sphaira {
namespace {

struct NroData {
    NroStart start;
    NroHeader header;
};

auto nro_parse_internal(fs::Fs* fs, const fs::FsPath& path, NroEntry& entry) -> Result {
    entry.path = path;

    // todo: special sorting for fw 2.0.0 to make it not look like shit
    if (hosversionAtLeast(3,0,0)) {
        // it doesn't matter if we fail
        entry.timestamp.is_valid = false;
        fs->GetFileTimeStampRaw(entry.path, &entry.timestamp);
        // if (R_FAILED(fsFsGetFileTimeStampRaw(fs, entry.path, &entry.timestamp))) {
        //     // log_write("failed to get timestamp for: %s\n", path);
        // }
    }

    fs::File f;
    R_TRY(fs->OpenFile(entry.path, FsOpenMode_Read, &f));

    // todo: buffer reads to 16k to avoid 2 fs read calls per entry.
    NroData data;
    u64 bytes_read;
    R_TRY(f.Read(0, &data, sizeof(data), FsReadOption_None, &bytes_read));
    R_UNLESS(data.header.magic == NROHEADER_MAGIC, Result_NroBadMagic);

    NroAssetHeader asset;
    R_TRY(f.Read(data.header.size, &asset, sizeof(asset), FsReadOption_None, &bytes_read));
    // R_UNLESS(asset.magic == NROASSETHEADER_MAGIC, Result_NroBadMagic);

    // we can avoid a GetSize() call by calculating the size manually.
    entry.size = data.header.size;

    // some .nro (vgedit) have bad nacp, fake the nacp
    auto& nacp = entry.nacp;
    if (asset.magic != NROASSETHEADER_MAGIC || asset.nacp.offset == 0 || asset.nacp.size != sizeof(NacpStruct)) {
        std::memset(&asset, 0, sizeof(asset));
        std::memset(&nacp, 0, sizeof(nacp));

        // get the name without the .nro
        const auto file_name = std::strrchr(path, '/') + 1;
        const auto file_name_len = std::strlen(file_name);
        std::strncpy(nacp.lang.name, file_name, file_name_len - 4);
        std::strcpy(nacp.lang.author, "Unknown");
        std::strcpy(nacp.display_version, "Unknown");

        entry.icon_offset = entry.icon_size = 0;
        entry.is_nacp_valid = false;
    } else {
        entry.size += sizeof(asset) + asset.icon.size + asset.nacp.size + asset.romfs.size;
        R_TRY(f.Read(data.header.size + asset.nacp.offset, &nacp.lang, sizeof(nacp.lang), FsReadOption_None, &bytes_read));
        R_TRY(f.Read(data.header.size + asset.nacp.offset + offsetof(NacpStruct, display_version), nacp.display_version, sizeof(nacp.display_version), FsReadOption_None, &bytes_read));

        // lazy load the icons
        entry.icon_size = asset.icon.size;
        entry.icon_offset = data.header.size + asset.icon.offset;
        entry.is_nacp_valid = true;
    }

    R_SUCCEED();
}

// this function is recursive by 1 level deep
// if the nro is in switch/folder/folder2/app.nro it will NOT be found
// switch/folder/app.nro for example will work fine.
auto nro_scan_internal(fs::Fs* fs, const fs::FsPath& path, std::vector<NroEntry>& nros, bool nested, bool scan_all_dir, bool root) -> Result {
    // we don't need to scan for folders if we are not root
    u32 dir_open_type = FsDirOpenMode_ReadFiles | FsDirOpenMode_NoFileSize;
    if (root) {
        dir_open_type |= FsDirOpenMode_ReadDirs;
    }

    fs::Dir d;
    R_TRY(fs->OpenDirectory(path, dir_open_type, &d));

    // we won't run out of memory here
    std::vector<FsDirectoryEntry> entries;
    R_TRY(d.ReadAll(entries));

    for (const auto& e : entries) {
        // skip hidden files / folders
        if ('.' == e.name[0]) {
            continue;
        }

        if (e.type == FsDirEntryType_Dir) {
            // assert(!root && "dir should only be scanned on non-root!");
            fs::FsPath fullpath;
            std::snprintf(fullpath, sizeof(fullpath), "%s/%s/%s.nro", path.s, e.name, e.name);

            // fast path for detecting an nro in a folder
            NroEntry entry;
            if (R_SUCCEEDED(nro_parse_internal(fs, fullpath, entry))) {
                // log_write("NRO: fast path for: %s\n", fullpath);
                nros.emplace_back(entry);
            } else {
                // slow path...
                std::snprintf(fullpath, sizeof(fullpath), "%s/%s", path.s, e.name);
                nro_scan_internal(fs, fullpath, nros, nested, scan_all_dir, false);
            }
        } else if (e.type == FsDirEntryType_File && std::string_view{e.name}.ends_with(".nro")) {
            fs::FsPath fullpath;
            std::snprintf(fullpath, sizeof(fullpath), "%s/%s", path.s, e.name);

            NroEntry entry;
            if (R_SUCCEEDED(nro_parse_internal(fs, fullpath, entry))) {
                nros.emplace_back(entry);
                if (!root && !scan_all_dir) {
                    // log_write("NRO: slow path for: %s\n", fullpath);
                    R_SUCCEED();
                }
            } else {
                log_write("error when trying to parse %s\n", fullpath.s);
            }
        }
    }

    R_SUCCEED();
}

auto nro_scan_internal(const fs::FsPath& path, std::vector<NroEntry>& nros, bool nested, bool scan_all_dir, bool root) -> Result {
    fs::FsNativeSd fs;
    return nro_scan_internal(&fs, path, nros, nested, scan_all_dir, root);
}

auto nro_get_icon_internal(fs::File* f, u64 size, u64 offset) -> std::vector<u8> {
    // protect again really messed up sizes.
    if (size > 1024 * 1024) {
        return {};
    }

    std::vector<u8> icon;
    u64 bytes_read{};
    icon.resize(size);

    R_TRY_RESULT(f->Read(offset, icon.data(), icon.size(), FsReadOption_None, &bytes_read), {});
    R_UNLESS(bytes_read == icon.size(), {});

    return icon;
}

auto launch_internal(const std::string& path, const std::string& argv) -> Result {
    R_TRY(envSetNextLoad(path.c_str(), argv.c_str()));

    log_write("set launch with path: %s argv: %s\n", path.c_str(), argv.c_str());

    evman::push(evman::LaunchNroEventData{path, argv});
    R_SUCCEED();
}

} // namespace

/*
    NRO INFO PAGE:
    - icon
    - name
    - author
    - path
    - filesize
    - launch count
    - timestamp created
    - timestamp modified
*/

auto nro_verify(std::span<const u8> data) -> Result {
    NroData nro;
    R_UNLESS(data.size() >= sizeof(nro), Result_NroBadSize);

    memcpy(&nro, data.data(), sizeof(nro));
    R_UNLESS(nro.header.magic == NROHEADER_MAGIC, Result_NroBadMagic);

    R_SUCCEED();
}

auto nro_parse(const fs::FsPath& path, NroEntry& entry) -> Result {
    fs::FsNativeSd fs;
    return nro_parse_internal(&fs, path, entry);
}

auto nro_scan(const fs::FsPath& path, std::vector<NroEntry>& nros, bool nested, bool scan_all_dir) -> Result {
    return nro_scan_internal(path, nros, nested, scan_all_dir, true);
}

auto nro_get_icon(const fs::FsPath& path, u64 size, u64 offset) -> std::vector<u8> {
    fs::FsNativeSd fs;
    fs::File f;
    R_TRY_RESULT(fs.OpenFile(path, FsOpenMode_Read, &f), {});
    return nro_get_icon_internal(&f, size, offset);
}

auto nro_get_icon(const fs::FsPath& path) -> std::vector<u8> {
    fs::FsNativeSd fs;
    NroData data;
    NroAssetHeader asset;
    u64 bytes_read;

    fs::File f;
    R_TRY_RESULT(fs.OpenFile(path, FsOpenMode_Read, &f), {});

    R_TRY_RESULT(f.Read(0, &data, sizeof(data), FsReadOption_None, &bytes_read), {});
    R_UNLESS(data.header.magic == NROHEADER_MAGIC, {});
    R_TRY_RESULT(f.Read(data.header.size, &asset, sizeof(asset), FsReadOption_None, &bytes_read), {});
    R_UNLESS(asset.magic == NROASSETHEADER_MAGIC, {});

    return nro_get_icon_internal(&f, asset.icon.size, data.header.size + asset.icon.offset);
}

auto nro_get_nacp(const fs::FsPath& path, NacpStruct& nacp) -> Result {
    fs::FsNativeSd fs;
    NroData data;
    NroAssetHeader asset;
    u64 bytes_read;

    fs::File f;
    R_TRY(fs.OpenFile(path, FsOpenMode_Read, &f));

    R_TRY(f.Read(0, &data, sizeof(data), FsReadOption_None, &bytes_read));
    R_UNLESS(data.header.magic == NROHEADER_MAGIC, Result_NroBadMagic);
    R_TRY(f.Read(data.header.size, &asset, sizeof(asset), FsReadOption_None, &bytes_read));
    R_UNLESS(asset.magic == NROASSETHEADER_MAGIC, Result_NroBadMagic);
    R_TRY(f.Read(data.header.size + asset.nacp.offset, &nacp, sizeof(nacp), FsReadOption_None, &bytes_read));

    R_SUCCEED();
}

auto nro_update_info(const fs::FsPath& path, std::string_view name, std::span<const u8> icon) -> Result {
    R_UNLESS(!name.empty() && name.size() < sizeof(NacpLanguageEntry::name), Result_NroBadSize);
    R_UNLESS(!icon.empty() && icon.size() <= 1024 * 1024, Result_NroBadSize);

    fs::FsNativeSd fs;
    std::vector<u8> original;
    R_TRY(fs.read_entire_file(path, original));
    R_UNLESS(original.size() >= sizeof(NroData), Result_NroBadSize);

    NroData nro{};
    std::memcpy(&nro, original.data(), sizeof(nro));
    R_UNLESS(nro.header.magic == NROHEADER_MAGIC, Result_NroBadMagic);

    const auto asset_base = static_cast<std::size_t>(nro.header.size);
    R_UNLESS(asset_base <= original.size() && sizeof(NroAssetHeader) <= original.size() - asset_base, Result_NroBadSize);

    NroAssetHeader asset{};
    std::memcpy(&asset, original.data() + asset_base, sizeof(asset));
    R_UNLESS(asset.magic == NROASSETHEADER_MAGIC, Result_NroBadMagic);

    const auto section_valid = [&original, asset_base](const NroAssetSection& section) {
        const auto tail_size = original.size() - asset_base;
        return section.offset <= tail_size && section.size <= tail_size - section.offset;
    };
    R_UNLESS(section_valid(asset.nacp) && asset.nacp.size == sizeof(NacpStruct), Result_NroBadSize);
    R_UNLESS(section_valid(asset.romfs), Result_NroBadSize);

    NacpStruct nacp{};
    std::memcpy(&nacp, original.data() + asset_base + asset.nacp.offset, sizeof(nacp));
    for (std::size_t i = 0; i < 16; i++) {
        auto& lang = nacp_util::GetLanguageEntry(nacp, i);
        std::memset(lang.name, 0, sizeof(lang.name));
        std::memcpy(lang.name, name.data(), name.size());
    }

    // the asset sections are rebuilt back to back, the icon is the one that resized.
    NroAssetHeader updated_asset = asset;
    updated_asset.icon.offset = sizeof(NroAssetHeader);
    updated_asset.icon.size = icon.size();
    updated_asset.nacp.offset = updated_asset.icon.offset + updated_asset.icon.size;
    updated_asset.nacp.size = sizeof(NacpStruct);
    updated_asset.romfs.offset = updated_asset.nacp.offset + updated_asset.nacp.size;

    const auto updated_size = asset_base + updated_asset.romfs.offset + updated_asset.romfs.size;

    const auto tmp_name = path.toString() + ".sphaira.tmp";
    const auto bak_name = path.toString() + ".sphaira.bak";
    R_UNLESS(tmp_name.size() < FS_MAX_PATH && bak_name.size() < FS_MAX_PATH, Result_NroBadSize);
    const fs::FsPath tmp_path{tmp_name};
    const fs::FsPath bak_path{bak_name};

    bool success = false;
    ON_SCOPE_EXIT([&]{
        if (!success) {
            fs.DeleteFile(tmp_path);
        }
    });

    // Remove any stale backup from a previously crashed run so RenameFile won't fail with 0x402.
    fs.DeleteFile(bak_path);

    if (auto rc = fs.CreateFile(tmp_path, updated_size, 0); R_FAILED(rc) && rc != FsError_PathAlreadyExists) {
        return rc;
    }

    fs::File f;
    R_TRY(fs.OpenFile(tmp_path, FsOpenMode_Write, &f));
    R_TRY(f.SetSize(updated_size));

    s64 write_off = 0;
    R_TRY(f.Write(write_off, original.data(), asset_base, FsWriteOption_None));
    write_off += asset_base;

    R_TRY(f.Write(write_off, &updated_asset, sizeof(updated_asset), FsWriteOption_None));
    write_off += sizeof(updated_asset);

    R_TRY(f.Write(write_off, icon.data(), icon.size(), FsWriteOption_None));
    write_off += icon.size();

    R_TRY(f.Write(write_off, &nacp, sizeof(nacp), FsWriteOption_None));
    write_off += sizeof(nacp);

    if (updated_asset.romfs.size) {
        const u8* romfs_src = original.data() + asset_base + asset.romfs.offset;
        s64 romfs_rem = updated_asset.romfs.size;
        s64 romfs_off = 0;
        constexpr s64 CHUNK_SIZE = 64 * 1024;
        while (romfs_rem > 0) {
            const s64 chunk = std::min(romfs_rem, CHUNK_SIZE);
            R_TRY(f.Write(write_off, romfs_src + romfs_off, chunk, FsWriteOption_None));
            write_off += chunk;
            romfs_off += chunk;
            romfs_rem -= chunk;
        }
    }
    f.Close();

    // Verify actual file size on disk before touching the original NRO
    s64 written_size = 0;
    fs::File check_f;
    if (R_SUCCEEDED(fs.OpenFile(tmp_path, FsOpenMode_Read, &check_f))) {
        check_f.GetSize(&written_size);
        check_f.Close();
    }
    R_UNLESS(written_size == static_cast<s64>(updated_size), Result_NroBadSize);

    R_TRY(fs.RenameFile(path, bak_path));
    const auto rename_rc = fs.RenameFile(tmp_path, path);
    if (R_FAILED(rename_rc)) {
        fs.RenameFile(bak_path, path);
        log_write("[NRO] Failed to rename temp file to %s (rc: 0x%X), restored backup %s\n", path.toString().c_str(), rename_rc, bak_name.c_str());
        return rename_rc;
    }

    fs.DeleteFile(bak_path);
    success = true;
    R_SUCCEED();
}

auto nro_launch(std::string path, std::string args) -> Result {
    if (path.empty()) {
        return 1;
    }

    // keeps compat with hbloader
    // https://github.com/ITotalJustice/Gamecard-Installer-NX/blob/master/source/main.c#L73
    // https://github.com/ITotalJustice/Gamecard-Installer-NX/blob/d549c5f916dea814fa0a7e5dc8c903fa3044ba15/source/main.c#L29
    if (!path.starts_with("sdmc:")) {
        path = "sdmc:" + path;
    }

    if (args.empty()) {
        args = nro_add_arg(path);
    } else {
        args = nro_add_arg(path) + ' ' + args;
    }

    return launch_internal(path, args);
}

auto nro_add_arg(std::string arg) -> std::string {
    if (arg.contains(' ')) {
        return '\"' + arg + '\"';
    }
    return arg;
}

auto nro_add_arg_file(std::string arg) -> std::string {
    if (!arg.starts_with("sdmc:")) {
        arg = "sdmc:" + arg;
    }
    if (arg.contains(' ')) {
        return '\"' + arg + '\"';
    }
    return arg;
}

auto nro_normalise_path(const std::string& p) -> std::string {
    if (p.starts_with("sdmc:")) {
        return p.substr(5);
    }
    return p;
}

auto nro_find(std::span<const NroEntry> array, std::string_view name, std::string_view author, const fs::FsPath& path) -> std::optional<NroEntry> {
    const auto it = std::find_if(array.cbegin(), array.cend(), [name, author, path](auto& e){
        if (!name.empty() && !author.empty() && !path.empty()) {
            return e.GetName() == name && e.GetAuthor() == author && e.path == path;
        } else if (!name.empty()) {
            return e.GetName() == name;
        } else if (!author.empty()) {
            return e.GetAuthor() == author;
        } else if (!path.empty()) {
            return e.path == path;
        }
        return false;
    });

    if (it == array.cend()) {
        return std::nullopt;
    }

    return *it;
}

auto nro_find_name(std::span<const NroEntry> array, std::string_view name) -> std::optional<NroEntry> {
    return nro_find(array, name, {}, {});
}

auto nro_find_author(std::span<const NroEntry> array, std::string_view author) -> std::optional<NroEntry> {
    return nro_find(array, {}, author, {});
}

auto nro_find_path(std::span<const NroEntry> array, const fs::FsPath& path) -> std::optional<NroEntry> {
    return nro_find(array, {}, {}, path);
}

} // namespace sphaira
