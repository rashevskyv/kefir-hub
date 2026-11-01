#pragma once

// A save backup zip that was zipped again by hand often has everything inside one folder:
//   MyBackup/.nx_save_meta.bin, MyBackup/file0, MyBackup/sub/file1
// Such a zip is read as if the folder was not there. This is done only when the meta file is inside
// that folder and every other entry is inside it too (macOS leftovers excepted), so a save that
// really holds a single folder is never changed. Nothing here touches switch.h: it is tested on the host.

#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <strings.h>
#include <vector>

namespace sphaira::savezip {

inline auto IsWrapperJunk(std::string_view name) -> bool {
    return name.starts_with("__MACOSX/") || name == ".DS_Store";
}

// the wrapper folder with its trailing slash ("MyBackup/"), or empty when the zip is not wrapped.
// meta_name is the file that marks a backup (.nx_save_meta.bin), root_meta_names are the ones that mean "not wrapped" when at the root.
inline auto DetectWrapperPrefix(const std::vector<std::string>& names, std::string_view meta_name, const std::vector<std::string_view>& root_meta_names) -> std::string {
    const auto equals_ic = [](std::string_view a, std::string_view b) {
        return a.size() == b.size() && !strncasecmp(a.data(), b.data(), a.size());
    };

    std::string prefix;
    for (const auto& name : names) {
        const auto slash = name.find('/');
        if (slash == std::string::npos) {
            for (const auto root : root_meta_names) {
                if (equals_ic(name, root)) {
                    return {}; // the meta is at the root: not wrapped
                }
            }
            continue;
        }

        if (slash == name.rfind('/') && equals_ic(std::string_view{name}.substr(slash + 1), meta_name)) {
            const auto candidate = name.substr(0, slash + 1);
            if (!prefix.empty() && prefix != candidate) {
                return {}; // two folders hold a meta file: not a plain wrapper
            }
            prefix = candidate;
        }
    }

    if (prefix.empty()) {
        return {};
    }

    for (const auto& name : names) {
        if (!std::string_view{name}.starts_with(prefix) && !IsWrapperJunk(name)) {
            return {}; // something lives outside the folder
        }
    }
    return prefix;
}

// the name seen from inside the wrapper; nullopt for the folder itself and for entries outside it.
inline auto StripWrapper(std::string_view name, std::string_view prefix) -> std::optional<std::string_view> {
    if (prefix.empty()) {
        return name;
    }
    if (!name.starts_with(prefix) || name.size() == prefix.size()) {
        return std::nullopt;
    }
    return name.substr(prefix.size());
}

} // namespace sphaira::savezip
