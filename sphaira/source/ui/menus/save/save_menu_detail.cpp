#include "app.hpp"
#include "log.hpp"
#include "image.hpp"
#include "ui/menus/save/save_menu_detail.hpp"
#include "ui/menus/save/save_paths.hpp"
#include <cstring>
#include <cstdio>

namespace sphaira::ui::menu::save::detail {

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
    std::snprintf(e.lang.name, sizeof(e.lang.name), "%s | %016lX", GetSystemSaveName(e.system_save_data_id), e.system_save_data_id);
    std::strcpy(e.lang.author, "Nintendo");
}

bool LoadControlImage(Entry& e, title::ThreadResultData* result) {
    if (!e.image && result && !result->icon.empty()) {
        TimeStamp ts;
        const auto image = ImageLoadFromMemory(result->icon, ImageFlag_JPEG);
        if (!image.data.empty()) {
            e.image = nvgCreateImageRGBA(App::GetVg(), image.w, image.h, 0, image.data.data());
            log_write("\t[image load] time taken: %.2fs %zums\n", ts.GetSecondsD(), ts.GetMs());
            return true;
        }
    }

    return false;
}

void LoadResultIntoEntry(Entry& e, title::ThreadResultData* result) {
    if (result) {
        e.status = result->status;
        e.lang = result->lang;
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
