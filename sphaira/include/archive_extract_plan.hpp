#pragma once

#include "path_util.hpp"
#include "zip_extract_plan.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace sphaira::archive {

// extracted with libarchive; zip keeps its own minizip path.
inline constexpr std::string_view EXTENSIONS[] = {
    "rar", "7z", "tar", "gz", "tgz", "xz", "txz", "bz2", "tbz2",
};

// where an archive entry lands, relative to the target folder; nullopt = skip it.
// `raw` is a bare compressed stream (file.gz, file.xz): its one entry has no name,
// so it is named after the archive without the compression extension.
inline auto OutputPath(std::string_view entry, bool raw, std::string_view archive_path) -> std::optional<std::string> {
    if (raw) {
        const auto slash = archive_path.find_last_of('/');
        auto name = slash == archive_path.npos ? archive_path : archive_path.substr(slash + 1);
        if (const auto ext = path::Extension(name); !ext.empty()) {
            name.remove_suffix(ext.size() + 1);
        }
        if (name.empty() || !path::IsSafeArchiveEntry(name)) {
            return std::nullopt;
        }
        return std::string{name};
    }

    auto n = zip_extract::NormalizeZipEntry(entry);
    while (!n.empty() && n.back() == '/') {
        n.pop_back();
    }
    if (!path::IsSafeArchiveEntry(n)) {
        return std::nullopt;
    }
    return n;
}

} // namespace sphaira::archive
