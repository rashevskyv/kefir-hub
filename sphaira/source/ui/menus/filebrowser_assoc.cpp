#include "ui/menus/filebrowser_assoc.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "nro.hpp"
#include "defines.hpp"
#include <cstring>
#include <algorithm>

namespace sphaira::ui::menu::filebrowser::detail {

constexpr const char* NXMP_PATHS[]{
    "/switch/nxmp/nxmp.nro",
    "/switch/nxmp.nro",
};

auto RomDatabaseEntry::IsDatabase(std::string_view name) const -> bool {
    if (IsSamePath(name, folder) || IsSamePath(name, database)) {
        return true;
    }

    for (const auto& str : alias) {
        if (!str.empty() && IsSamePath(name, str)) {
            return true;
        }
    }

    return false;
}

auto IsSamePath(std::string_view a, std::string_view b) -> bool {
    return a.length() == b.length() && !strncasecmp(a.data(), b.data(), a.length());
}

auto IsExtension(std::string_view ext, std::span<const std::string_view> list) -> bool {
    for (auto e : list) {
        if (e.length() == ext.length() && !strncasecmp(ext.data(), e.data(), ext.length())) {
            return true;
        }
    }
    return false;
}

auto IsExtension(std::string_view ext1, std::string_view ext2) -> bool {
    return ext1.length() == ext2.length() && !strncasecmp(ext1.data(), ext2.data(), ext1.length());
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
