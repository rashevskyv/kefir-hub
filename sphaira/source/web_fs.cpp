#include "web_fs.hpp"
#include "web_http.hpp"
#include "app.hpp"
#include "location.hpp"
#include "path_util.hpp"

#include <algorithm>
#include <cstring>

namespace sphaira {

using namespace web::detail;

// the mounted folders, canonicalised. read fresh on every request: the mounts
// belong to the app (App::SetMountedFolders), not to this server, so mounting
// from the file browser takes effect on a server that is already running --
// including one started from Tools, which serves the whole card and never sets
// a mount of its own.
//
// the card root is not a mount: it is what the server already serves, so
// mounting it would only produce a second way to reach the same listing.
auto GetMountRoots() -> std::vector<std::string> {
    std::vector<std::string> out;
    for (const auto& p : App::GetMountedFolders()) {
        const auto raw = p.toString();
        if (raw.empty()) {
            continue;
        }

        auto clean = CanonicalizeAbsolutePath(raw);
        if (clean == "/") {
            continue;
        }

        out.push_back(std::move(clean));
    }
    return out;
}

// the first mount, or empty. only for the one spot that needs a single default:
// the upload target when the request names no folder.
auto GetMountRoot() -> std::string {
    const auto roots = GetMountRoots();
    return roots.empty() ? std::string{} : roots.front();
}

// what the root lists, mirroring the file browser's own root: the card plus
// whatever is mounted. network locations (WebDAV / SMB) are left out because
// they are only devoptab-mounted while the browser is sitting inside them --
// listing them here would be links that fail the moment the user leaves.
auto GetRootSources() -> std::vector<RootSource> {
    std::vector<RootSource> out;

    for (const auto& mount : GetMountRoots()) {
        const char* leaf = std::strrchr(mount.c_str(), '/');
        out.push_back({mount, (leaf && leaf[1]) ? leaf + 1 : "Mounted Folder", "Mounted Folder (" + mount + ")"});
    }

    for (const auto& e : location::GetStdio(false)) {
        out.push_back({e.mount + "/", e.name, "USB storage"});
    }

    for (const auto& e : location::GetMtpHostDevices(false)) {
        out.push_back({e.mount + "/", e.name, "MTP device"});
    }

    out.push_back({"/", "sd", "microSD Card"});
    return out;
}

// which source a path lives in: the longest source root containing it. drives
// the "up" link, so leaving a source lands on the root page rather than on the
// card folder that happens to sit above a mount.
auto SourceRootFor(const std::string& path) -> std::string {
    std::string best = "/";
    for (const auto& s : GetRootSources()) {
        if (s.path == "/" || s.path.size() <= best.size()) {
            continue;
        }
        if (path == s.path || path.starts_with(s.path.back() == '/' ? s.path : s.path + "/")) {
            best = s.path;
        }
    }
    return best;
}

auto SourceNameFor(const std::string& root) -> std::string {
    for (const auto& s : GetRootSources()) {
        if (s.path == root) {
            return s.name;
        }
    }
    return root;
}

// a devoptab prefix ("ums0:/…") names a mounted source and is served through
// stdio; everything else is a plain microSD path.
auto OpenFs(std::string_view path) -> std::unique_ptr<fs::Fs> {
    if (path.find(':') != std::string_view::npos) {
        return std::make_unique<fs::FsStdio>();
    }
    return std::make_unique<fs::FsNativeSd>();
}

} // namespace sphaira
