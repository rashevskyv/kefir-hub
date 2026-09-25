#pragma once

#include "fs.hpp"
#include "nro.hpp"

#include <vector>
#include <string>
#include <string_view>
#include <cstdint>
#include <cstring>
#include <strings.h>

namespace sphaira::ui::menu::filebrowser {

enum FsEntryFlag {
    FsEntryFlag_None,
    // write protected.
    FsEntryFlag_ReadOnly = 1 << 0,
    // supports file assoc.
    FsEntryFlag_Assoc = 1 << 1,
    // doesn't support file stat.
    FsEntryFlag_NoStatFile = 1 << 2,
    // doesn't support dir stat.
    FsEntryFlag_NoStatDir = 1 << 3,
};

enum class FsType {
    Sd,
    ImageNand,
    ImageSd,
    Stdio,
    Network,
    Root,
    // read-only mount of a .zip archive, entered from within another view.
    Archive,
    // read-only mount of one installed content component's NCA files.
    Content,
};

enum class ConnectionStatus {
    Unknown,
    Connected,
    Failed,
};

enum class SelectedType {
    None,
    Copy,
    Cut,
    Delete,
};

enum class ViewSide {
    Left,
    Right,
};

enum SortType {
    SortType_Size,
    SortType_Alphabetical,
};

enum OrderType {
    OrderType_Descending,
    OrderType_Ascending,
};

struct FsEntry {
    fs::FsPath name{};
    fs::FsPath root{};
    FsType type{};
    u32 flags{FsEntryFlag_None};
    fs::FsPath url{};
    fs::FsPath protocol{};
    fs::FsPath user{};
    fs::FsPath pass{};
    u16 port{};
    ConnectionStatus status{ConnectionStatus::Unknown};

    // identity for FsType::Content (a mounted installed component).
    u64 content_app_id{};
    u8 content_meta_type{};
    u8 content_storage_id{};

    auto IsReadOnly() const -> bool {
        return flags & FsEntryFlag_ReadOnly;
    }

    auto IsAssoc() const -> bool {
        return flags & FsEntryFlag_Assoc;
    }

    auto NoStatFile() const -> bool {
        return flags & FsEntryFlag_NoStatFile;
    }

    auto NoStatDir() const -> bool {
        return flags & FsEntryFlag_NoStatDir;
    }

    auto IsSame(const FsEntry& e) const {
        return root == e.root && type == e.type;
    }
};

// roughly 1kib in size per entry
struct FileEntry : FsDirectoryEntry {
    std::string extension{}; // if any
    std::string internal_name{}; // if any
    std::string internal_extension{}; // if any
    s64 file_count{-1}; // number of files in a folder, non-recursive
    s64 dir_count{-1}; // number folders in a folder, non-recursive
    FsTimeStampRaw time_stamp{};
    bool checked_extension{}; // did we already search for an ext?
    bool checked_internal_extension{}; // did we already search for an ext?
    bool metadata_loaded{}; // remote size/timestamp or child counts are ready
    bool metadata_failed{};
    bool selected{}; // is this file selected?
    std::string title_label{}; // game/module name for a title id folder, if known
    u64 title_id{}; // non-zero while an async title name lookup is outstanding
    ConnectionStatus connection_status{ConnectionStatus::Unknown};
    FsEntry virtual_target_entry{};

    auto IsFile() const -> bool {
        return type == FsDirEntryType_File;
    }

    auto IsDir() const -> bool {
        return !IsFile();
    }

    auto IsHidden() const -> bool {
        return name[0] == '.';
    }

    auto GetName() const -> std::string {
        return name;
    }

    auto GetExtension() const -> std::string {
        if (!checked_extension) {
            if (auto ext = std::strrchr(name, '.')) {
                return ext+1;
            }
        }
        return extension;
    }

    auto GetInternalName() const -> std::string {
        if (!internal_name.empty()) {
            return internal_name;
        }
        return GetName();
    }

    auto GetInternalExtension() const -> std::string {
        if (!internal_extension.empty()) {
            return internal_extension;
        }
        return GetExtension();
    }

    auto IsSelected() const -> bool {
        return selected;
    }
};

struct FileAssocEntry {
    fs::FsPath path{}; // ini name
    std::string name{}; // ini name
    std::string argument{}; // optional fixed argument
    std::vector<std::string> ext{}; // list of ext
    std::vector<std::string> database{}; // list of systems
    bool use_base_name{}; // if set, uses base name (rom.zip) otherwise uses internal name (rom.gba)

    auto IsExtension(std::string_view extension, std::string_view internal_extension) const -> bool {
        for (const auto& assoc_ext : ext) {
            if (extension.length() == assoc_ext.length() && !strncasecmp(assoc_ext.data(), extension.data(), assoc_ext.length())) {
                return true;
            }
            if (internal_extension.length() == assoc_ext.length() && !strncasecmp(assoc_ext.data(), internal_extension.data(), assoc_ext.length())) {
                return true;
            }
        }
        return false;
    }

    auto GetRomArgs(const fs::FsPath& rom_path) const -> std::string {
        const auto file_arg = nro_add_arg_file(rom_path);
        if (argument.empty()) {
            return file_arg;
        }
        return nro_add_arg(argument) + " " + file_arg;
    }
};

struct LastFile {
    fs::FsPath name{};
    s64 index{};
    float offset{};
    s64 entries_count{};
};

struct FsDirCollection {
    fs::FsPath path{};
    fs::FsPath parent_name{};
    std::vector<FsDirectoryEntry> files{};
    std::vector<FsDirectoryEntry> dirs{};
};

using FsDirCollections = std::vector<FsDirCollection>;

} // namespace sphaira::ui::menu::filebrowser
