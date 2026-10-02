#include "ui/menus/save/save_paths.hpp"
#include "defines.hpp"
#include "log.hpp"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <set>
#include <vector>

namespace sphaira::ui::menu::save {

auto SaveEntryKey(const FsSaveDataInfo& e) -> std::string {
    char key[0x80];
    std::snprintf(key, sizeof(key), "%u:%u:%016lX:%016lX:%016lX:%016lX:%u:%u",
        e.save_data_space_id, e.save_data_type, e.application_id,
        e.system_save_data_id, e.uid.uid[0], e.uid.uid[1],
        e.save_data_rank, e.save_data_index);
    return key;
}

constexpr std::array<FsSaveDataSpaceId, 7> CONCRETE_SAVE_DATA_SPACES{
    FsSaveDataSpaceId_System,
    FsSaveDataSpaceId_User,
    FsSaveDataSpaceId_SdSystem,
    FsSaveDataSpaceId_Temporary,
    FsSaveDataSpaceId_SdUser,
    FsSaveDataSpaceId_ProperSystem,
    FsSaveDataSpaceId_SafeMode,
};

auto DiscoverSaveDataInfo(const AccountUid* uid_filter, const std::optional<u8>& type_filter) -> std::vector<FsSaveDataInfo> {
    std::vector<FsSaveDataInfo> out;
    std::set<std::string> seen_keys;

    for (size_t i = 0; i < CONCRETE_SAVE_DATA_SPACES.size(); i++) {
        const auto space = CONCRETE_SAVE_DATA_SPACES[i];
        FsSaveDataInfoReader reader;
        const auto open_rc = fsOpenSaveDataInfoReader(&reader, space);
        if (R_FAILED(open_rc)) {
            // a space this console refuses (ProperSystem: 2002-6001) fails on every scan, twice
            // per UI action: log it once per run. R_VALUE drops the reserved bits, which fs
            // fills with a per-call counter (the value looked like it grew by 0x400000).
            static std::atomic<u32> s_logged_spaces{0};
            const u32 bit = 1u << i;
            if (!(s_logged_spaces.fetch_or(bit) & bit)) {
                log_write("[SAVE] fsOpenSaveDataInfoReader failed for space %d: 0x%x (logged once)\n", static_cast<int>(space), R_VALUE(open_rc));
            }
            continue;
        }

        ON_SCOPE_EXIT(fsSaveDataInfoReaderClose(&reader));

        std::vector<FsSaveDataInfo> staged;
        std::vector<FsSaveDataInfo> chunk(256);
        bool read_failed = false;

        while (true) {
            s64 count = 0;
            const auto read_rc = fsSaveDataInfoReaderRead(&reader, chunk.data(), chunk.size(), &count);
            if (R_FAILED(read_rc)) {
                log_write("[SAVE] fsSaveDataInfoReaderRead failed for space %d: 0x%x\n", static_cast<int>(space), read_rc);
                read_failed = true;
                break;
            }
            if (count <= 0) {
                break;
            }
            staged.insert(staged.end(), chunk.begin(), chunk.begin() + count);
        }

        if (read_failed) {
            continue;
        }

        for (const auto& info : staged) {
            if (type_filter.has_value() && info.save_data_type != *type_filter) {
                continue;
            }
            if (uid_filter != nullptr && std::memcmp(&info.uid, uid_filter, sizeof(AccountUid)) != 0) {
                continue;
            }

            const auto key = SaveEntryKey(info);
            if (seen_keys.insert(key).second) {
                out.emplace_back(info);
            }
        }
    }

    return out;
}

} // namespace sphaira::ui::menu::save
