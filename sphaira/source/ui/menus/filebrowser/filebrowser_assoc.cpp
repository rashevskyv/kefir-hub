#include "ui/menus/filebrowser_assoc.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/menus/homebrew.hpp"
#include "path_util.hpp"
#include "app_paths.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "nro.hpp"
#include "defines.hpp"
#include <minIni.h>
#include <dirent.h>
#include <cstring>
#include <algorithm>
#include <ranges>
#include <string_view>
#include <strings.h>

namespace sphaira::ui::menu::filebrowser::detail {

constexpr const char* NXMP_PATHS[]{
    "/switch/nxmp/nxmp.nro",
    "/switch/nxmp.nro",
};

auto RomDatabaseEntry::IsDatabase(std::string_view name) const -> bool {
    if (path::EqualsIC(name, folder) || path::EqualsIC(name, database)) {
        return true;
    }

    for (const auto& str : alias) {
        if (!str.empty() && path::EqualsIC(name, str)) {
            return true;
        }
    }

    return false;
}

auto GetRomDatabaseFromPath(std::string_view path) -> RomDatabaseIndexs {
    if (path.length() <= 1) {
        return {};
    }

    RomDatabaseIndexs indexs;
    const auto db_name = path.substr(path.find_last_of('/') + 1);

    for (int i = 0; i < std::size(PATHS); i++) {
        const auto& p = PATHS[i];
        if (p.IsDatabase(db_name)) {
            log_write("found it :) %.*s\n", (int)p.database.length(), p.database.data());
            indexs.emplace_back(i);
        }
    }

    if (indexs.empty()) {
        const auto last_off = path.substr(0, path.find_last_of('/'));
        if (const auto off = last_off.find_last_of('/'); off != std::string_view::npos) {
            const auto db_name2 = last_off.substr(off + 1);
            for (int i = 0; i < std::size(PATHS); i++) {
                const auto& p = PATHS[i];
                if (p.IsDatabase(db_name2)) {
                    log_write("found it :) %.*s\n", (int)p.database.length(), p.database.data());
                    indexs.emplace_back(i);
                }
            }
        }
    }

    return indexs;
}

auto GetRomIcon(std::string filename, const RomDatabaseIndexs& db_indexs, const NroEntry& nro) -> std::vector<u8> {
    if (db_indexs.empty()) {
        log_write("using nro image\n");
        return nro_get_icon(nro.path, nro.icon_size, nro.icon_offset);
    }

    constexpr std::string_view bad_chars{"&*/:`<>?\\|\""};
    for (auto& c : filename) {
        for (auto bad_c : bad_chars) {
            if (c == bad_c) {
                c = '_';
                break;
            }
        }
    }

    #define RA_BOXART_NAME "/Named_Boxarts/"
    #define RA_THUMBNAIL_PATH "/retroarch/thumbnails/"
    #define RA_BOXART_EXT ".png"

    for (auto db_idx : db_indexs) {
        const auto system_name = std::string{PATHS[db_idx].database.data(), PATHS[db_idx].database.length()};
        auto system_name_gh = system_name + "/master";
        for (auto& c : system_name_gh) {
            if (c == ' ') {
                c = '_';
            }
        }

        const std::string thumbnail_path = system_name + RA_BOXART_NAME + filename + RA_BOXART_EXT;
        const std::string ra_thumbnail_path = RA_THUMBNAIL_PATH + thumbnail_path;

        log_write("starting image convert on: %s\n", ra_thumbnail_path.c_str());

        std::vector<u8> image_file;
        if (R_SUCCEEDED(fs::FsNativeSd().read_entire_file(ra_thumbnail_path, image_file))) {
            return image_file;
        }
    }

    log_write("using nro image\n");
    return nro_get_icon(nro.path, nro.icon_size, nro.icon_offset);
}

auto GetNxmpPath() -> const char* {
    fs::FsNativeSd fs;
    for (auto& path : NXMP_PATHS) {
        if (fs.FileExists(path)) {
            return path;
        }
    }
    return nullptr;
}

auto HasNxmp() -> bool {
    return GetNxmpPath() != nullptr;
}

} // namespace sphaira::ui::menu::filebrowser::detail

namespace sphaira::ui::menu::filebrowser {
using namespace detail;

auto Menu::FindFileAssocFor() -> std::vector<FileAssocEntry> {
    // only support roms in correctly named folders, sorry!
    const auto db_indexs = GetRomDatabaseFromPath(view->m_path);
    const auto& entry = view->GetEntry();
    const auto extension = entry.GetExtension();
    const auto internal_extension = entry.GetInternalExtension();
    if (extension.empty() && internal_extension.empty()) {
        // log_write("failed to get extension for db: %s path: %s\n", database_entry.c_str(), m_path);
        return {};
    }

    std::vector<FileAssocEntry> out_entries;
    if (!db_indexs.empty()) {
        // if database isn't empty, then we are in a valid folder
        // search for an entry that matches the db and ext
        for (const auto& assoc : m_assoc_entries) {
            for (const auto& assoc_db : assoc.database) {
                // if (assoc_db == PATHS[db_idx].folder || assoc_db == PATHS[db_idx].database) {
                for (auto db_idx : db_indexs) {
                    if (PATHS[db_idx].IsDatabase(assoc_db)) {
                        if (assoc.IsExtension(extension, internal_extension)) {
                            out_entries.emplace_back(assoc);
                            goto jump;
                        }
                    }
                }
            }
            jump:
        }
    } else {
        // otherwise, if not in a valid folder, find an entry that doesn't
        // use a database, ie, not a emulator.
        // this is because media players and hbmenu can launch from anywhere
        // and the extension is enough info to know what type of file it is.
        // whereas with roms, a .iso can be used for multiple systems, so it needs
        // to be in the correct folder, ie psx, to know what system that .iso is for.
        for (const auto& assoc : m_assoc_entries) {
            if (assoc.database.empty()) {
                if (assoc.IsExtension(extension, internal_extension)) {
                    log_write("found ext: %s\n", assoc.path.s);
                    out_entries.emplace_back(assoc);
                }
            }
        }
    }

    enum class LauncherGroup {
        RetroArch = 0,
        TICO = 1,
        Other = 2,
    };

    auto GetLauncherGroup = [](std::string_view path) -> LauncherGroup {
        if (path.starts_with('/')) {
            path.remove_prefix(1);
        }
        const auto slash = path.find('/');
        const auto root = (slash != std::string_view::npos) ? path.substr(0, slash) : path;
        if (path::EqualsIC(root, "retroarch")) {
            return LauncherGroup::RetroArch;
        }
        if (path::EqualsIC(root, "tico")) {
            return LauncherGroup::TICO;
        }
        return LauncherGroup::Other;
    };

    std::ranges::stable_sort(out_entries, [&](const FileAssocEntry& a, const FileAssocEntry& b) {
        const auto group_a = GetLauncherGroup(a.path.s);
        const auto group_b = GetLauncherGroup(b.path.s);
        if (group_a != group_b) {
            return group_a < group_b;
        }
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    });

    return out_entries;
}

void Menu::LoadAssocEntriesPath(const fs::FsPath& path) {
    auto dir = opendir(path);
    if (!dir) {
        return;
    }
    ON_SCOPE_EXIT(closedir(dir));

    while (auto d = readdir(dir)) {
        if (d->d_name[0] == '.') {
            continue;
        }

        if (d->d_type != DT_REG) {
            continue;
        }

        const auto ext = std::strrchr(d->d_name, '.');
        if (!ext || strcasecmp(ext, ".ini")) {
            continue;
        }

        const auto full_path = GetNewPath(path, d->d_name);
        FileAssocEntry assoc{};

        ini_browse([](const mTCHAR *Section, const mTCHAR *Key, const mTCHAR *Value, void *UserData) {
            auto assoc = static_cast<FileAssocEntry*>(UserData);
            if (!std::strcmp(Key, "path")) {
                assoc->path = Value;
            } else if (!std::strcmp(Key, "name")) {
                assoc->name = Value;
            } else if (!std::strcmp(Key, "argument")) {
                assoc->argument = Value;
            } else if (!std::strcmp(Key, "supported_extensions") || !std::strcmp(Key, "extensions")) {
                for (const auto& p : std::views::split(std::string_view{Value}, '|')) {
                    if (p.empty()) {
                        continue;
                    }
                    assoc->ext.emplace_back(p.data(), p.size());
                }
            } else if (!std::strcmp(Key, "database")) {
                for (const auto& p : std::views::split(std::string_view{Value}, '|')) {
                    if (p.empty()) {
                        continue;
                    }
                    assoc->database.emplace_back(p.data(), p.size());
                }
            } else if (!std::strcmp(Key, "use_base_name")) {
                if (!std::strcmp(Value, "true") || !std::strcmp(Value, "1")) {
                    assoc->use_base_name = true;
                }
            }
            return 1;
        }, &assoc, full_path);

        if (assoc.ext.empty()) {
            continue;
        }

        if (assoc.name.empty()) {
            assoc.name.assign(d->d_name, ext - d->d_name);
        }

        // if path isn't empty, check if the file exists
        bool file_exists{};
        if (!assoc.path.empty()) {
            file_exists = view->m_fs->FileExists(assoc.path);
        } else {
            const auto nro_name = assoc.name + ".nro";
            for (const auto& nro : homebrew::GetNroEntries()) {
                const auto len = std::strlen(nro.path);
                if (len < nro_name.length()) {
                    continue;
                }
                if (!strcasecmp(nro.path + len - nro_name.length(), nro_name.c_str())) {
                    assoc.path = nro.path;
                    file_exists = true;
                    break;
                }
            }
        }

        // after all of that, the file doesn't exist :(
        if (!file_exists) {
            continue;
        }

        m_assoc_entries.emplace_back(assoc);
    }
}

static size_t CountAssocEntriesPath(const fs::FsPath& path) {
    auto dir = opendir(path);
    if (!dir) {
        return 0;
    }
    ON_SCOPE_EXIT(closedir(dir));

    size_t count = 0;
    while (auto d = readdir(dir)) {
        if (d->d_name[0] == '.') {
            continue;
        }

        if (d->d_type != DT_REG) {
            continue;
        }

        const auto ext = std::strrchr(d->d_name, '.');
        if (!ext || strcasecmp(ext, ".ini")) {
            continue;
        }

        count++;
    }

    return count;
}

void Menu::LoadAssocEntries() {
    size_t count = 0;
    const bool romfs_ok = R_SUCCEEDED(romfsInit());
    if (romfs_ok) {
        count += CountAssocEntriesPath("romfs:/assoc/");
    }
    count += CountAssocEntriesPath(paths::ASSOC);

    m_assoc_entries.reserve(count);

    // load from romfs first
    if (romfs_ok) {
        LoadAssocEntriesPath("romfs:/assoc/");
        romfsExit();
    }
    // then load custom entries
    LoadAssocEntriesPath(paths::ASSOC);
}

} // namespace sphaira::ui::menu::filebrowser
