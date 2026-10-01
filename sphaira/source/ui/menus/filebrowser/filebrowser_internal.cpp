#include "ui/menus/filebrowser/filebrowser_internal.hpp"

#include "app.hpp"
#include "fs.hpp"
#include "location.hpp"
#include "path_util.hpp"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::ui::menu::filebrowser {
constinit UEvent g_change_uevent;

std::string MakeNetworkDeviceName(std::string_view url) {
    u32 hash = 2166136261u;
    for (const auto c : url) {
        hash ^= static_cast<u8>(c);
        hash *= 16777619u;
    }

    char name[16]{};
    std::snprintf(name, sizeof(name), "net_%08x", hash);
    return name;
}

std::string MakeNetworkRoot(std::string_view url) {
    return MakeNetworkDeviceName(url) + ":/";
}



#ifdef BUILD_SMB2
int g_smb_ref_count = 0; // main thread only (FsView::SetFs, ~FsView)

void ParseSmbUrl(const std::string& url, std::string& server, std::string& share) {
    if (url.rfind("smb://", 0) != 0) return;
    size_t host_start = 6;
    size_t slash_pos = url.find('/', host_start);
    if (slash_pos == std::string::npos) {
        server = url.substr(host_start);
        share = "";
    } else {
        server = url.substr(host_start, slash_pos - host_start);
        share = url.substr(slash_pos + 1);
    }
}

std::string UrlEncode(const std::string& value, bool keep_slash) {
    constexpr char HEX[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size());

    for (const char c : value) {
        const auto ch = static_cast<unsigned char>(c);
        const bool unreserved =
            (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~';
        if (unreserved || (keep_slash && ch == '/')) {
            encoded += static_cast<char>(ch);
        } else {
            encoded += '%';
            encoded += HEX[ch >> 4];
            encoded += HEX[ch & 0x0F];
        }
    }

    return encoded;
}
#endif

bool IsSameNetworkLocation(const FsEntry& lhs, const FsEntry& rhs) {
    return lhs.type == FsType::Network && rhs.type == FsType::Network &&
        lhs.url.toString() == rhs.url.toString() &&
        lhs.user.toString() == rhs.user.toString() &&
        lhs.pass.toString() == rhs.pass.toString();
}

void metadata_thread_func(void* user) {
    static_cast<FsView*>(user)->MetadataThreadFunction();
}

// is there anything in the root besides the microSD card? the root is a source
// picker, and a picker with one entry is just a keypress in the way -- so with
// nothing mounted the browser treats the card itself as the top level.
bool HasExtraRootSources() {
    return App::GetGodModeEnabled()
        || !location::GetStdio(false).empty()
        || !location::GetMtpHostDevices(false).empty()
        || !location::Load().empty();
}

auto IdentifyPayload(fs::Fs* fs, const fs::FsPath& file_path) -> std::string {
    if (!fs) {
        return {};
    }

    if (path::EqualsIC(file_path, "/switch/DBI/translation.bin") && fs->FileExists("/switch/DBI/DBI.nro")) {
        return "DBI translation file";
    }

    fs::File f;
    if (R_FAILED(fs->OpenFile(file_path, FsOpenMode_Read, &f))) {
        return {};
    }

    s64 file_size = 0;
    if (R_FAILED(f.GetSize(&file_size)) || file_size < 512) {
        return {};
    }

    constexpr size_t kMaxScanSize = 256 * 1024;
    std::vector<u8> buf(std::min<size_t>(file_size, kMaxScanSize));
    u64 bytes_read = 0;
    if (R_FAILED(f.Read(0, buf.data(), buf.size(), FsReadOption_None, &bytes_read)) || bytes_read < 512) {
        return {};
    }

    std::string_view data(reinterpret_cast<const char*>(buf.data()), bytes_read);

    if (data.find("TegraExplorer") != std::string_view::npos) {
        return "TegraExplorer";
    }
    if (data.find("Incognito_RCM") != std::string_view::npos) {
        return "Incognito_RCM";
    }
    if (data.find("Atmosphere") != std::string_view::npos || data.find("Atmosph\xc3\xa8re") != std::string_view::npos) {
        if (data.find("fusee") != std::string_view::npos || data.find("FUSEE") != std::string_view::npos) {
            return "Atmosphère Fusee";
        }
    }
    if (data.find("CTCaer") != std::string_view::npos || data.find("HEKATE") != std::string_view::npos || data.find("CTCBOOT") != std::string_view::npos) {
        return "Hekate";
    }
    if (data.find("SX OS") != std::string_view::npos || data.find("Team Xecuter") != std::string_view::npos) {
        return "SX OS Bootloader";
    }
    if (data.find("biskeydump") != std::string_view::npos) {
        return "Biskeydump";
    }

    return {};
}

// two launchers can ship the same core (Tico and RetroArch both bundle
// genesis_plus_gx), and the ini name alone is identical for both. tag the row
// with the folder the nro actually lives in.
auto MakeLauncherLabel(const FileAssocEntry& assoc) -> std::string {
    std::string_view path{assoc.path.s};
    if (path.starts_with('/')) {
        path.remove_prefix(1);
    }

    const auto slash = path.find('/');
    if (slash == std::string_view::npos) {
        return assoc.name;
    }

    auto root = std::string{path.substr(0, slash)};
    if (path::EqualsIC(root, "retroarch")) {
        return "RetroArch \u2014 " + assoc.name;
    }
    if (path::EqualsIC(root, "tico")) {
        return "TICO \u2014 " + assoc.name;
    }

    // /switch/<launcher>/... is the common layout, the useful name is deeper.
    if (path::EqualsIC(root, "switch")) {
        const auto rest = path.substr(slash + 1);
        const auto next = rest.find('/');
        if (next == std::string_view::npos) {
            return assoc.name;
        }
        root = std::string{rest.substr(0, next)};
    }

    return assoc.name + "  (" + root + ")";
}
} // namespace sphaira::ui::menu::filebrowser
