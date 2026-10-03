#pragma once

// DOCS_DEMO builds only: fictional content for docs screenshots.
// Data lives on the SD card at sdmc:/config/kefir/demo/ (fixtures: docs/site/fixtures/sdmc/).

#include <switch.h>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace sphaira::wifi {
struct WifiProfile;
}

namespace sphaira::demo {

struct Update {
    u32 version{};
    std::string display{};
    s64 size{};
};

struct Save {
    int user{};    // index in the Eden profile list
    s64 size{};
};

struct Title {
    u64 id{};
    std::vector<std::pair<std::string, std::string>> names{}; // language code -> name
    std::string publisher{};
    std::string version{};
    std::string icon{};   // path relative to the demo folder
    std::string build_id{}; // main NSO Build ID (16 hex digits) for the cheat screens; empty = unknown
    u8 storage{};         // NcmStorageId
    s64 size{};
    std::vector<Update> updates{};
    int dlc{};
    std::vector<Save> saves{};
};

// size of each add-on: titles.json lists only how many a game has.
constexpr s64 DLC_SIZE = 256ll * 1024 * 1024;

// titles.json, parsed once; empty if the file is missing.
std::span<const Title> Titles();
// the demo title for a base, update (base+0x800) or add-on (base+0x1000+n) id.
const Title* FindTitle(u64 id);
// name in the current UI language, "en" if that one is missing.
std::string Name(const Title& t);
// the cover JPEG, empty if the file is missing.
std::vector<u8> Icon(const Title& t);

// save_discovery.cpp: one account save per titles.json "saves" entry, appended to the User space list
// (uid = the Eden profile at that index; a missing profile skips the save).
void AppendSaves(FsSaveDataSpaceId space, std::vector<FsSaveDataInfo>& out);

// wifi_manager.cpp: two saved networks, the first one connected. They replace the list: Eden's own profile
// has no name and would read as a broken entry.
void SetWifiProfiles(std::vector<wifi::WifiProfile>& out);

// the demo USB drive (location.cpp) is a folder on the SD; header paths show it as "ums0:" like a real drive.
constexpr const char* USB_ROOT = "sdmc:/config/kefir/demo/usb";
std::string DisplayPath(const std::string& path);

// NAND user partition space from titles.json "nand" (Eden has no BIS filesystem); false if absent.
bool NandSpace(s64* free, s64* total);

// microSD space from titles.json "sd" (Eden's emulated card is a few GB); false if absent.
bool SdSpace(s64* free, s64* total);

// header shows "EmuNAND" (titles.json "nand.emummc"); App::IsEmummc() itself is not changed.
bool EmuNand();

} // namespace sphaira::demo
