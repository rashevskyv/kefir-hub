#pragma once

#include <algorithm>
#include <cctype>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace sphaira::utils {

#pragma pack(push, 1)
struct TegraExplorerFooter {
    char magic[4];          // "KFRP"
    uint8_t format_version; // 1
    uint8_t payload_type;   // 1
    uint8_t app_version_major;
    uint8_t app_version_minor;
    uint8_t app_version_patch;
    uint8_t reserved[3];
    uint32_t kefir_version;
    char magic_end[4];      // "PRFK"
};
#pragma pack(pop)
static_assert(sizeof(TegraExplorerFooter) == 20);

struct TegraExplorerVersion {
    uint32_t major{0};
    uint32_t minor{0};
    uint32_t patch{0};
    uint32_t kefir{0};

    auto operator<=>(const TegraExplorerVersion&) const = default;
};

enum class PayloadMetaStatus {
    Missing,
    Unreadable,
    Empty,
    Unrecognized,
    Valid
};

enum class TegraExplorerSelectionAction {
    UseExistingSd,
    UpgradeSdFromRomfs,
    InstallSdFromRomfs,
    FailNoPayload
};

struct TegraExplorerPayloadState {
    PayloadMetaStatus status{PayloadMetaStatus::Missing};
    TegraExplorerVersion version{};
};

inline bool parseTegraExplorerVersion(const uint8_t* data, size_t size, TegraExplorerVersion& out_ver) {
    out_ver = {};
    if (!data || size < sizeof(TegraExplorerFooter)) {
        return false;
    }
    const size_t search_start = size >= 512 ? size - 512 : 0;
    for (size_t i = size - sizeof(TegraExplorerFooter); i >= search_start; --i) {
        if (std::memcmp(&data[i], "KFRP", 4) == 0 &&
            std::memcmp(&data[i + 16], "PRFK", 4) == 0) {
            TegraExplorerFooter footer{};
            std::memcpy(&footer, &data[i], sizeof(footer));
            if (footer.format_version != 1 || footer.payload_type != 1) {
                if (i == 0) break;
                continue;
            }
            out_ver.major = footer.app_version_major;
            out_ver.minor = footer.app_version_minor;
            out_ver.patch = footer.app_version_patch;
            out_ver.kefir = footer.kefir_version;
            return true;
        }
        if (i == 0) {
            break;
        }
    }
    return false;
}

inline PayloadMetaStatus inspectTegraExplorerPayloadData(const uint8_t* data, size_t size, TegraExplorerVersion& out_ver) {
    out_ver = {};
    if (size == 0) {
        return PayloadMetaStatus::Empty;
    }
    if (!data) {
        return PayloadMetaStatus::Missing;
    }
    if (parseTegraExplorerVersion(data, size, out_ver)) {
        return PayloadMetaStatus::Valid;
    }
    return PayloadMetaStatus::Unrecognized;
}

inline bool isTegraExplorerPayload(std::string_view path) {
    if (path.empty()) {
        return false;
    }
    const size_t last_slash = path.find_last_of("/\\");
    std::string_view filename = (last_slash != std::string_view::npos)
        ? path.substr(last_slash + 1)
        : path;

    if (filename.size() < 4) {
        return false;
    }

    std::string lower{filename};
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (lower.compare(lower.size() - 4, 4, ".bin") != 0) {
        return false;
    }

    return lower.find("tegraexplorer") != std::string::npos ||
           lower.find("tegra_explorer") != std::string::npos;
}

inline TegraExplorerSelectionAction decideTegraExplorerPayloadAction(
    const TegraExplorerPayloadState& sd,
    const TegraExplorerPayloadState& romfs)
{
    const bool sd_readable = (sd.status == PayloadMetaStatus::Valid || sd.status == PayloadMetaStatus::Unrecognized);
    const bool romfs_available = (romfs.status == PayloadMetaStatus::Valid || romfs.status == PayloadMetaStatus::Unrecognized);

    if (!romfs_available) {
        if (sd_readable) {
            return TegraExplorerSelectionAction::UseExistingSd;
        }
        return TegraExplorerSelectionAction::FailNoPayload;
    }

    if (!sd_readable) {
        return TegraExplorerSelectionAction::InstallSdFromRomfs;
    }

    // Never overwrite a known-version SD payload with unrecognized ROMFS data
    if (sd.status == PayloadMetaStatus::Valid && romfs.status != PayloadMetaStatus::Valid) {
        return TegraExplorerSelectionAction::UseExistingSd;
    }

    // Replace unrecognized/corrupted SD payload with known-valid ROMFS
    if (sd.status != PayloadMetaStatus::Valid && romfs.status == PayloadMetaStatus::Valid) {
        return TegraExplorerSelectionAction::UpgradeSdFromRomfs;
    }

    // Both are valid: compare versions
    if (sd.status == PayloadMetaStatus::Valid && romfs.status == PayloadMetaStatus::Valid) {
        if (sd.version < romfs.version) {
            return TegraExplorerSelectionAction::UpgradeSdFromRomfs;
        }
        return TegraExplorerSelectionAction::UseExistingSd;
    }

    // Both are unrecognized: preserve existing SD payload
    return TegraExplorerSelectionAction::UseExistingSd;
}

} // namespace sphaira::utils
