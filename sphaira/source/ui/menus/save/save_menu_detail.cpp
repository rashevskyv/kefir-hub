#include "app.hpp"
#include "log.hpp"
#include "image.hpp"
#include "ui/menus/save/save_menu_detail.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "app_paths.hpp"
#include "download.hpp"
#include "fs.hpp"
#include <cstring>
#include <cstdio>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <span>
#include <string>

namespace sphaira::ui::menu::save::detail {

// https://switchbrew.org/wiki/Flash_Filesystem#SystemSaveData
auto GetSystemSaveName(u64 system_save_data_id) -> const char* {
    switch (system_save_data_id) {
        case 0x8000000000000000: return "fs"; break;
        case 0x8000000000000010: return "account"; break;
        case 0x8000000000000011: return "account"; break;
        case 0x8000000000000020: return "nfc"; break;
        case 0x8000000000000030: return "ns"; break;
        case 0x8000000000000031: return "ns"; break;
        case 0x8000000000000040: return "ns"; break;
        case 0x8000000000000041: return "ns"; break;
        case 0x8000000000000043: return "ns"; break;
        case 0x8000000000000044: return "ns"; break;
        case 0x8000000000000045: return "ns"; break;
        case 0x8000000000000046: return "ns"; break;
        case 0x8000000000000047: return "ns"; break;
        case 0x8000000000000048: return "ns"; break;
        case 0x8000000000000049: return "ns"; break;
        case 0x800000000000004A: return "ns"; break;
        case 0x8000000000000050: return "settings"; break;
        case 0x8000000000000051: return "settings"; break;
        case 0x8000000000000052: return "settings"; break;
        case 0x8000000000000053: return "settings"; break;
        case 0x8000000000000054: return "settings"; break;
        case 0x8000000000000060: return "ssl"; break;
        case 0x8000000000000061: return "ssl"; break; // guessing
        case 0x8000000000000070: return "nim"; break;
        case 0x8000000000000071: return "nim"; break;
        case 0x8000000000000072: return "nim"; break;
        case 0x8000000000000073: return "nim"; break;
        case 0x8000000000000074: return "nim"; break;
        case 0x8000000000000075: return "nim"; break;
        case 0x8000000000000076: return "nim"; break;
        case 0x8000000000000077: return "nim"; break;
        case 0x8000000000000078: return "nim"; break;
        case 0x8000000000000080: return "friends"; break;
        case 0x8000000000000081: return "friends"; break;
        case 0x8000000000000082: return "friends"; break;
        case 0x8000000000000090: return "bcat"; break;
        case 0x8000000000000091: return "bcat"; break;
        case 0x8000000000000092: return "bcat"; break;
        case 0x80000000000000A0: return "bcat"; break;
        case 0x80000000000000A1: return "bcat"; break;
        case 0x80000000000000A2: return "bcat"; break;
        case 0x80000000000000B0: return "bsdsockets"; break;
        case 0x80000000000000C1: return "bcat"; break;
        case 0x80000000000000C2: return "bcat"; break;
        case 0x80000000000000D1: return "erpt"; break;
        case 0x80000000000000E0: return "es"; break;
        case 0x80000000000000E1: return "es"; break;
        case 0x80000000000000E2: return "es"; break;
        case 0x80000000000000E3: return "es"; break;
        case 0x80000000000000E4: return "es"; break;
        case 0x80000000000000F0: return "ns"; break;
        case 0x8000000000000100: return "pctl"; break;
        case 0x8000000000000110: return "npns"; break;
        case 0x8000000000000120: return "ncm"; break;
        case 0x8000000000000121: return "ncm"; break;
        case 0x8000000000000122: return "ncm"; break;
        case 0x8000000000000130: return "migration"; break;
        case 0x8000000000000131: return "migration"; break;
        case 0x8000000000000132: return "migration"; break;
        case 0x8000000000000133: return "migration"; break;
        case 0x8000000000000140: return "capsrv"; break;
        case 0x8000000000000150: return "olsc"; break;
        case 0x8000000000000151: return "olsc"; break;
        case 0x8000000000000152: return "olsc"; break;
        case 0x8000000000000153: return "olsc"; break;
        case 0x8000000000000180: return "sdb"; break;
        case 0x8000000000000190: return "glue"; break;
        case 0x8000000000000200: return "bcat"; break;
        case 0x8000000000000210: return "account"; break;
        case 0x8000000000000220: return "erpt"; break;
        case 0x8000000000001010: return "qlaunch"; break;
        case 0x8000000000001011: return "qlaunch"; break;
        case 0x8000000000001020: return "swkbd"; break;
        case 0x8000000000001021: return "swkbd"; break;
        case 0x8000000000001030: return "auth"; break;
        case 0x8000000000001040: return "miiEdit"; break;
        case 0x8000000000001050: return "miiEdit"; break;
        case 0x8000000000001060: return "LibAppletShop"; break;
        case 0x8000000000001061: return "LibAppletShop"; break;
        case 0x8000000000001070: return "LibAppletWeb"; break;
        case 0x8000000000001071: return "LibAppletWeb"; break;
        case 0x8000000000001080: return "LibAppletOff"; break;
        case 0x8000000000001081: return "LibAppletOff"; break;
        case 0x8000000000001090: return "LibAppletLns"; break;
        case 0x8000000000001091: return "LibAppletLns"; break;
        case 0x80000000000010A0: return "LibAppletAuth"; break;
        case 0x80000000000010A1: return "LibAppletAuth"; break;
        case 0x80000000000010B0: return "playerSelect"; break;
        case 0x80000000000010C0: return "myPage"; break;
        case 0x80000000000010E1: return "qlaunch"; break;
        case 0x8000000000001100: return "qlaunch"; break;
        case 0x8000000000002000: return "DevMenu"; break;
        case 0x8000000000002020: return "ns"; break;
        case 0x8000000000010002: return "bcat"; break;
        case 0x8000000000010003: return "bcat"; break;
        case 0x8000000000010004: return "bcat"; break;
        case 0x8000000000010005: return "bcat"; break;
        case 0x8000000000010006: return "bcat"; break;
        case 0x8000000000010007: return "bcat"; break;
    }

    return "Unknown";
}

void FakeNacpEntryForSystem(Entry& e) {
    e.status = title::NacpLoadStatus::Loaded;

    // fake the nacp entry
    std::snprintf(e.lang.name, sizeof(e.lang.name), "%s | %016lX", GetSystemSaveName(e.system_save_data_id), e.system_save_data_id);
    std::strcpy(e.lang.author, "Nintendo"); // literal, bounded
}

auto IsValidGameTitleId(u64 id) -> bool {
    if (id == 0) {
        return false;
    }
    // Standard Nintendo Switch game application IDs are in 0x0100... range.
    const u64 prefix = id >> 48;
    return prefix >= 0x0100 && prefix <= 0x01FF;
}

auto FormatTitleIdHex(u64 id) -> std::string {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016lX", id);
    return std::string(buf);
}

auto IsValidBoundedJpeg(std::span<const u8> data) -> bool {
    // Bounded: minimum ~100 bytes, maximum bounded to 1 MB (256x256 jpeg is ~20-50 KB).
    if (data.size() < 100 || data.size() > 1024 * 1024) {
        return false;
    }
    // JPEG SOI marker: 0xFF, 0xD8, 0xFF.
    if (data[0] != 0xFF || data[1] != 0xD8 || data[2] != 0xFF) {
        return false;
    }
    return true;
}

auto BuildRemoteIconUrl(u64 app_id) -> std::string {
    return "https://api.nlib.cc/nx/" + FormatTitleIdHex(app_id) + "/icon/256";
}

auto GetTitleIconCachePath(u64 app_id) -> fs::FsPath {
    return paths::DATA_ROOT + "/cache/icons/" + FormatTitleIdHex(app_id) + ".jpg";
}

namespace {
std::unordered_set<u64> s_missing_session_ids;
std::unordered_set<u64> s_in_flight_ids;
std::unordered_set<u64> s_checked_cache_ids;
std::unordered_map<u64, std::vector<u8>> s_session_icon_cache;
} // namespace

bool LoadControlImage(Entry& e, title::ThreadResultData* result) {
    if (e.image) {
        return false;
    }

    // 1. Keep local/NXTC/sys-tweak icon loading first.
    if (result && !result->icon.empty()) {
        TimeStamp ts;
        const auto image = ImageLoadFromMemory(result->icon, ImageFlag_JPEG);
        if (!image.data.empty()) {
            const int img = nvgCreateImageRGBA(App::GetVg(), image.w, image.h, 0, image.data.data());
            if (img > 0) {
                e.image = img;
                log_write("\t[image load] time taken: %.2fs %zums\n", ts.GetSecondsD(), ts.GetMs());
                return true;
            }
        }
    }

    // Do not attempt network/cache fallback for system saves or empty IDs.
    if (IsSystemLikeSave(e.save_data_type) || e.application_id == 0) {
        return false;
    }

    // Do not fetch remote icon while local icon retrieval is still in progress.
    if (e.status == title::NacpLoadStatus::Progress) {
        return false;
    }

    const u64 id = e.application_id;
    if (!IsValidGameTitleId(id)) {
        return false;
    }

    // Check session icon cache first. Holds validated compressed JPEG bytes per Title ID (~25 KiB)
    // rather than decoded RGBA (256 KiB). For multiple entries sharing the same Title ID
    // (e.g. several backup archives of one game), each entry decodes and obtains its own independent
    // NanoVG texture handle so FreeEntry can delete safely.
    auto it = s_session_icon_cache.find(id);
    if (it != s_session_icon_cache.end()) {
        const auto image = ImageLoadIcon(it->second);
        if (!image.data.empty()) {
            const int img = nvgCreateImageRGBA(App::GetVg(), image.w, image.h, 0, image.data.data());
            if (img > 0) {
                e.image = img;
                return true;
            }
        }
        return false;
    }

    // 2. If determined missing/unavailable in this session, skip immediately (no repeated SD reads).
    if (s_missing_session_ids.contains(id)) {
        return false;
    }

    // 3. If currently downloading, skip immediately without SD access.
    if (s_in_flight_ids.contains(id)) {
        return false;
    }

    // 4. Check persistent disk cache at most once per session.
    const bool need_cache_read = !s_checked_cache_ids.contains(id);
    if (!need_cache_read) {
        return false;
    }

    s_checked_cache_ids.insert(id);

    const auto cache_path = GetTitleIconCachePath(id);
    fs::FsNativeSd sd;
    if (sd.FileExists(cache_path)) {
        std::vector<u8> icon_data;
        if (R_SUCCEEDED(sd.read_entire_file(cache_path, icon_data)) && !icon_data.empty()) {
            if (IsValidBoundedJpeg(icon_data)) {
                const auto image = ImageLoadIcon(icon_data);
                if (!image.data.empty()) {
                    s_session_icon_cache[id] = std::move(icon_data);
                    const int img = nvgCreateImageRGBA(App::GetVg(), image.w, image.h, 0, image.data.data());
                    if (img > 0) {
                        e.image = img;
                        return true;
                    }
                    return false;
                }
            }
        }
        // Corrupt or invalid cached file: delete bad file so remote fetch proceeds and recovers it.
        // Do not mark missing! Record checked so we don't re-read corrupt file before download completes.
        sd.DeleteFile(cache_path);
    }

    // 5. Download asynchronously so drawing and navigation never wait on the network.
    s_in_flight_ids.insert(id);
    const auto url = BuildRemoteIconUrl(id);

    const bool enqueued = curl::Api().ToMemoryAsync(
        curl::Url{url},
        curl::Priority{curl::Priority::Normal},
        curl::StopToken{},
        curl::OnComplete{[id, cache_path](auto& res) {
            // On network failure, 404, or non-JPEG response: keep placeholder and avoid repeating in session.
            // Note: The post-transfer check validates that the completed response is a valid bounded JPEG
            // before saving to persistent storage; network transmission buffering is handled by libcurl/download engine.
            if (!res.success || res.code != 200 || res.data.empty() || !IsValidBoundedJpeg(res.data)) {
                s_in_flight_ids.erase(id);
                s_missing_session_ids.insert(id);
                return;
            }

            // Decode and validate the completed JPEG before caching or marking it available.
            const auto decoded = ImageLoadIcon(res.data);
            if (decoded.data.empty()) {
                s_in_flight_ids.erase(id);
                s_missing_session_ids.insert(id);
                return;
            }

            // Cache successful result persistently by Title ID. Overwrites any previous corrupt file.
            fs::FsNativeSd fs_write;
            const auto dir = paths::DATA_ROOT + "/cache/icons";
            fs_write.CreateDirectoryRecursively(dir);
            if (R_FAILED(fs_write.write_entire_file(cache_path, res.data))) {
                s_in_flight_ids.erase(id);
                s_missing_session_ids.insert(id);
                return;
            }

            // Store validated compressed JPEG bytes in session cache.
            s_session_icon_cache[id] = res.data;
            s_in_flight_ids.erase(id);
        }}
    );

    if (!enqueued) {
        s_in_flight_ids.erase(id);
        s_missing_session_ids.insert(id);
    }

    return false;
}

void LoadResultIntoEntry(Entry& e, title::ThreadResultData* result) {
    if (result) {
        e.status = result->status;
        if (!title::IsPlaceholderName(result->lang.name) || e.lang.name[0] == '\0') {
            e.lang = result->lang;
        } else {
            e.lang.author[0] = '\0';
        }
    }
}

void LoadControlEntry(Entry& e, bool force_image_load) {
    if (e.status != title::NacpLoadStatus::Loaded) {
        if (IsSystemLikeSave(e.save_data_type)) {
            FakeNacpEntryForSystem(e);
        } else {
            LoadResultIntoEntry(e, title::Get(e.application_id));
        }
    }

    if (force_image_load && e.status == title::NacpLoadStatus::Loaded) {
        LoadControlImage(e, title::Get(e.application_id));
    }
}

} // namespace sphaira::ui::menu::save::detail
