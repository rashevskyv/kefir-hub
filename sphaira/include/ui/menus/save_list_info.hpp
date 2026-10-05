#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace sphaira::ui::menu::save {

inline constexpr std::size_t kMaxSaveBadges = 8;

enum SaveTypeMask : uint32_t {
    SaveTypeMask_None       = 0,
    SaveTypeMask_System     = 1u << 0, // FsSaveDataType_System = 0
    SaveTypeMask_Account    = 1u << 1, // FsSaveDataType_Account = 1
    SaveTypeMask_Bcat       = 1u << 2, // FsSaveDataType_Bcat = 2
    SaveTypeMask_Device     = 1u << 3, // FsSaveDataType_Device = 3
    SaveTypeMask_Temporary  = 1u << 4, // FsSaveDataType_Temporary = 4
    SaveTypeMask_Cache      = 1u << 5, // FsSaveDataType_Cache = 5
    SaveTypeMask_SystemBcat = 1u << 6, // FsSaveDataType_SystemBcat = 6
};

inline constexpr auto SaveTypeToMask(uint8_t type) -> uint32_t {
    if (type <= 6) {
        return 1u << type;
    }
    return 0;
}

inline auto CollectSaveBadgeLabels(
    uint32_t mask,
    std::array<const char*, kMaxSaveBadges>& out) -> std::size_t
{
    std::size_t n = 0;
    if (mask & SaveTypeMask_Account) {
        out[n++] = "Account";
    }
    if (mask & SaveTypeMask_Device) {
        out[n++] = "Device";
    }
    if (mask & SaveTypeMask_Bcat) {
        out[n++] = "BCAT";
    }
    if (mask & SaveTypeMask_Cache) {
        out[n++] = "Cache";
    }
    if (mask & SaveTypeMask_Temporary) {
        out[n++] = "Temporary";
    }
    if (mask & SaveTypeMask_System) {
        out[n++] = "System";
    }
    if (mask & SaveTypeMask_SystemBcat) {
        out[n++] = "System BCAT";
    }
    return n;
}

inline auto FormatSaveTypesSummary(uint32_t mask) -> std::string {
    std::array<const char*, kMaxSaveBadges> labels{};
    const auto count = CollectSaveBadgeLabels(mask, labels);
    if (!count) {
        return "-";
    }
    std::string s;
    for (std::size_t i = 0; i < count; i++) {
        if (i) {
            s += ", ";
        }
        s += labels[i];
    }
    return s;
}


} // namespace sphaira::ui::menu::save
