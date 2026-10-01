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

} // namespace sphaira::ui::menu::filebrowser
