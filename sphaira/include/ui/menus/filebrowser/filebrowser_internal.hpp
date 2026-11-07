#pragma once

#include "ui/menus/filebrowser.hpp"

#include <string>
#include <string_view>
#include <switch.h>

namespace sphaira::ui::menu::filebrowser {

extern UEvent g_change_uevent;

inline constexpr FsEntry FS_ENTRY_DEFAULT{
    "microSD card", "/", FsType::Sd, FsEntryFlag_Assoc,
};

inline constexpr FsEntry FS_ENTRIES[]{
    FS_ENTRY_DEFAULT,
    { "Image System memory", "/", FsType::ImageNand },
    { "Image microSD card", "/", FsType::ImageSd},
};

auto MakeNetworkDeviceName(std::string_view url) -> std::string;
auto MakeNetworkRoot(std::string_view url) -> std::string;
// the path for the title: a network mount point (net_1a2b3c4d:/) is shown as the name of the source.
auto DisplayPath(const FsEntry& entry, const std::string& path) -> std::string;

#ifdef BUILD_SMB2
extern int g_smb_ref_count; // main thread only (FsView::SetFs, ~FsView)
void ParseSmbUrl(const std::string& url, std::string& server, std::string& share);
auto UrlEncode(const std::string& value, bool keep_slash = false) -> std::string;
#endif

auto IsSameNetworkLocation(const FsEntry& lhs, const FsEntry& rhs) -> bool;
void metadata_thread_func(void* user);
auto HasExtraRootSources() -> bool;
auto IdentifyPayload(fs::Fs* fs, const fs::FsPath& file_path) -> std::string;
auto MakeLauncherLabel(const FileAssocEntry& assoc) -> std::string;
// the emulators and players that can open a file of this extension in this folder, best first (the rom lists use them too)
auto LoadAssocList() -> std::vector<FileAssocEntry>;
auto FindAssocFor(const std::vector<FileAssocEntry>& all, std::string_view folder_path, std::string_view extension, std::string_view internal_extension) -> std::vector<FileAssocEntry>;

} // namespace sphaira::ui::menu::filebrowser
