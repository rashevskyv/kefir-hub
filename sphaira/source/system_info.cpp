#include "system_info.hpp"
#include "system_info_internal.hpp"
#include "system_info_parse.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"

#include <switch.h>
#include <cstring>
#include <string>
#include <vector>

namespace sphaira::system_info {
namespace {

// exosphere's ApiInfo config items (Atmosphère libstratosphere spl::ConfigItem).
enum ExoConfigItem : u32 {
    Exo_Version = 65000,
    Exo_GitCommitHash = 65003,
    Exo_HasRcmBugPatch = 65004,
    Exo_BlankProdInfo = 65005,
    Exo_AllowCalWrites = 65006,
    Exo_EmummcType = 65007,
    Exo_ForceEnableUsb30 = 65010,
    Exo_SupportedHosVersion = 65011,
};

auto GetSpl(u32 item, u64& out) -> bool {
    return R_SUCCEEDED(splGetConfig(static_cast<SplConfigItem>(item), &out));
}

auto Mac(const u8* a) -> std::string {
    return Fmt("%02X:%02X:%02X:%02X:%02X:%02X", a[0], a[1], a[2], a[3], a[4], a[5]);
}

// the first PRODINFO_MIN_READ bytes of a file, or empty.
auto ReadHead(fs::Fs& fs, const fs::FsPath& path, std::vector<u8>& out) -> bool {
    fs::File file;
    if (R_FAILED(fs.OpenFile(path, FsOpenMode_Read, &file))) {
        return false;
    }
    out.resize(PRODINFO_MIN_READ);
    u64 bytes_read{};
    if (R_FAILED(file.Read(0, out.data(), out.size(), FsReadOption_None, &bytes_read)) || bytes_read != out.size()) {
        out.clear();
        return false;
    }
    return true;
}

// a PRODINFO backup with a real serial under `dir`: Atmosphère writes
// automatic_backups/<serial>_PRODINFO.bin, hekate writes backup/<id>/PRODINFO
// (or partitions/PRODINFO). Any file whose name says PRODINFO is tried; the
// content decides, so a BLANK_PRODINFO_*.bin backup never supplies a serial.
auto SerialFromBackupDir(fs::Fs& fs, const std::string& dir, int depth, std::string& out_path) -> std::string {
    fs::Dir d;
    std::vector<FsDirectoryEntry> entries;
    if (R_FAILED(fs.OpenDirectory(fs::FsPath{dir}, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &d))) {
        return {};
    }
    d.ReadAll(entries);

    std::vector<u8> head;
    for (const auto& e : entries) {
        const auto path = dir + "/" + e.name;
        if (e.type == FsDirEntryType_File) {
            if (!std::strstr(e.name, "PRODINFO")) {
                continue;
            }
            if (ReadHead(fs, fs::FsPath{path}, head)) {
                if (auto serial = SerialFromProdinfo(head); !serial.empty()) {
                    out_path = path;
                    return serial;
                }
            }
        } else if (e.type == FsDirEntryType_Dir && depth > 0) {
            if (auto serial = SerialFromBackupDir(fs, path, depth - 1, out_path); !serial.empty()) {
                return serial;
            }
        }
    }
    return {};
}

struct SerialInfo {
    std::string serial;   // the real one, or ""
    std::string source;   // translated, where it came from
    std::string system;   // what settings returned, verbatim (shown when blank)
};

// the console's serial, from the first source that has a real one: system
// settings; the raw PRODINFO partition; a backup on the SD card. The source
// is named next to the number, and a number is never derived or guessed.
auto FindSerial() -> SerialInfo {
    SerialInfo info{};

    SetSysSerialNumber sys{};
    if (R_SUCCEEDED(setsysGetSerialNumber(&sys))) {
        info.system = std::string{TrimSerial(std::string_view{sys.number, strnlen(sys.number, sizeof(sys.number))})};
        if (IsRealSerial(info.system)) {
            info.serial = info.system;
            info.source = "System settings"_i18n;
            return info;
        }
    }

    FsStorage cal0{};
    if (R_SUCCEEDED(fsOpenBisStorage(&cal0, FsBisPartitionId_CalibrationBinary))) {
        std::vector<u8> head(PRODINFO_MIN_READ);
        const auto rc = fsStorageRead(&cal0, 0, head.data(), head.size());
        fsStorageClose(&cal0);
        if (R_SUCCEEDED(rc)) {
            if (auto serial = SerialFromProdinfo(head); !serial.empty()) {
                info.serial = serial;
                info.source = "PRODINFO partition"_i18n;
                return info;
            }
        }
    }

    fs::FsNativeSd sd;
    std::string path;
    std::string serial = SerialFromBackupDir(sd, "/atmosphere/automatic_backups", 0, path);
    if (serial.empty()) {
        serial = SerialFromBackupDir(sd, "/backup", 2, path);
    }
    if (!serial.empty()) {
        info.serial = serial;
        info.source = "Backup file"_i18n + ": " + path;
        return info;
    }

    u64 blank{};
    info.source = GetSpl(Exo_BlankProdInfo, blank) && blank ? "Hidden by Atmosphere (blank_prodinfo)"_i18n : "Not found"_i18n;
    return info;
}

} // namespace

auto CollectConsole() -> Group {
    GroupBuilder g{"Console"_i18n};

    SetSysFirmwareVersion fw{};
    if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw))) {
        g.Line("Firmware"_i18n, fw.display_version);
        g.Line("Firmware name"_i18n, fw.display_title);
        g.Line("Firmware hash"_i18n, fw.version_hash);
    }

    u64 v{};
    SetSysProductModel model{};
    if (R_SUCCEEDED(setsysGetProductModel(&model))) {
        static constexpr const char* models[] = {"Unknown", "Nintendo Switch (Erista)", "Copper (development)", "Nintendo Switch (Mariko)", "Nintendo Switch Lite", "Calcio (development)", "Nintendo Switch OLED"};
        g.Line("Model"_i18n, static_cast<size_t>(model) < std::size(models) ? i18n::get(models[model]) : Fmt("%d", (int)model));
    }
    if (GetSpl(SplConfigItem_HardwareType, v)) {
        static constexpr const char* types[] = {"Icosa", "Copper", "Hoag", "Iowa", "Calcio", "Aula"};
        g.Line("Hardware type"_i18n, v < std::size(types) ? types[v] : Fmt("%lu", v));
        g.Line("SoC"_i18n, v == 0 || v == 1 ? "Tegra X1 (Erista)" : "Tegra X1+ (Mariko)");
    }
    if (GetSpl(SplConfigItem_IsRetail, v)) {
        g.Line("Unit"_i18n, v ? "Retail"_i18n : "Development"_i18n);
    }
    if (GetSpl(SplConfigItem_Version, v)) {
        g.Line("Burnt fuses"_i18n, Fmt("%lu", v));
    }
    if (GetSpl(SplConfigItem_DramId, v)) {
        g.Line("DRAM id"_i18n, Fmt("%lu", v));
    }
    if (GetSpl(SplConfigItem_DeviceId, v)) {
        g.Line("Device id"_i18n, Fmt("%016lX", v));
    }
    if (GetSpl(SplConfigItem_IsKiosk, v)) {
        g.Line("Kiosk unit"_i18n, YesNo(v));
    }
    if (GetSpl(SplConfigItem_IsChargerHiZModeEnabled, v)) {
        g.Line("Charger HiZ mode"_i18n, v ? "On"_i18n : "Off"_i18n);
    }

    const auto serial = FindSerial();
    g.Line("Serial number"_i18n, serial.serial.empty() ? "Not available"_i18n : serial.serial);
    g.Line("Serial number source"_i18n, serial.source);
    if (!serial.system.empty() && serial.system != serial.serial) {
        g.Line("Serial number (system)"_i18n, serial.system + " (" + "blank"_i18n + ")");
    }

    SetSysDeviceNickName nick{};
    if (R_SUCCEEDED(setsysGetDeviceNickname(&nick))) {
        g.Line("Console nickname"_i18n, nick.nickname);
    }
    u64 lang{};
    if (R_SUCCEEDED(setGetSystemLanguage(&lang))) {
        char code[9]{};
        std::memcpy(code, &lang, 8);
        g.Line("Language"_i18n, code);
    }
    SetRegion region{};
    if (R_SUCCEEDED(setGetRegionCode(&region))) {
        static constexpr const char* regions[] = {"Japan", "USA", "Europe", "Australia", "Hong Kong / Taiwan / Korea", "China"};
        g.Line("Region"_i18n, static_cast<size_t>(region) < std::size(regions) ? i18n::get(regions[region]) : Fmt("%d", (int)region));
    }
    if (R_SUCCEEDED(pctlInitialize())) {
        bool restricted{};
        if (R_SUCCEEDED(pctlIsRestrictionEnabled(&restricted))) {
            g.Line("Parental controls"_i18n, restricted ? "On"_i18n : "Off"_i18n);
        }
        pctlExit();
    }
    return g.group;
}

auto CollectAtmosphere() -> Group {
    GroupBuilder g{"Atmosphere"_i18n};
    u64 v{};
    if (GetSpl(Exo_Version, v)) {
        g.Line("Version"_i18n, Fmt("%lu.%lu.%lu", (v >> 56) & 0xFF, (v >> 48) & 0xFF, (v >> 40) & 0xFF));
        g.Line("Key generation"_i18n, Fmt("%lu", (v >> 32) & 0xFF));
        g.Line("Target firmware"_i18n, Fmt("%lu.%lu.%lu", (v >> 24) & 0xFF, (v >> 16) & 0xFF, (v >> 8) & 0xFF));
    }
    if (GetSpl(Exo_SupportedHosVersion, v)) {
        g.Line("Supported firmware"_i18n, Fmt("%lu.%lu.%lu", (v >> 24) & 0xFF, (v >> 16) & 0xFF, (v >> 8) & 0xFF));
    }
    if (GetSpl(Exo_GitCommitHash, v)) {
        g.Line("Git commit"_i18n, Fmt("%016lX", v));
    }
    if (GetSpl(Exo_HasRcmBugPatch, v)) {
        g.Line("RCM bug patched"_i18n, YesNo(v));
    }
    if (GetSpl(Exo_EmummcType, v)) {
        static constexpr const char* types[] = {"No", "Yes (partition)", "Yes (file)"};
        g.Line("emuMMC"_i18n, v < std::size(types) ? i18n::get(types[v]) : Fmt("%lu", v));
    } else {
        g.Line("emuMMC"_i18n, YesNo(App::IsEmummc()));
    }
    if (GetSpl(Exo_BlankProdInfo, v)) {
        g.Line("Blank PRODINFO"_i18n, v ? "On"_i18n : "Off"_i18n);
    }
    if (GetSpl(Exo_AllowCalWrites, v)) {
        g.Line("PRODINFO writes allowed"_i18n, YesNo(v));
    }
    if (GetSpl(Exo_ForceEnableUsb30, v)) {
        g.Line("USB 3.0 forced"_i18n, YesNo(v));
    }
    return g.group;
}

auto CollectHardware() -> Group {
    GroupBuilder g{"Hardware"_i18n};
    if (R_FAILED(setcalInitialize())) {
        return g.group;
    }
    ON_SCOPE_EXIT(setcalExit());

    SetCalBdAddress bt{};
    if (R_SUCCEEDED(setcalGetBdAddress(&bt))) {
        g.Line("Bluetooth MAC"_i18n, Mac(bt.bd_addr));
    }
    SetCalMacAddress wlan{};
    if (R_SUCCEEDED(setcalGetWirelessLanMacAddress(&wlan))) {
        g.Line("Wi-Fi MAC"_i18n, Mac(wlan.addr));
    }
    SetCalConfigurationId1 cfg{};
    if (R_SUCCEEDED(setcalGetConfigurationId1(&cfg))) {
        const auto* s = reinterpret_cast<const char*>(cfg.cfg);
        g.Line("Configuration id"_i18n, std::string(s, strnlen(s, sizeof(cfg.cfg))));
    }
    SetBatteryLot lot{};
    if (R_SUCCEEDED(setcalGetBatteryLot(&lot))) {
        g.Line("Battery lot"_i18n, std::string(lot.lot, strnlen(lot.lot, sizeof(lot.lot))));
    }
    SetCalSerialNumber serial{};
    if (R_SUCCEEDED(setcalGetSerialNumber(&serial))) {
        const std::string_view s{serial.number, strnlen(serial.number, sizeof(serial.number))};
        g.Line("Serial number (calibration)"_i18n, IsRealSerial(s) ? std::string{TrimSerial(s)} : std::string{TrimSerial(s)} + " (" + "blank"_i18n + ")");
    }
    return g.group;
}

auto Collect() -> std::vector<Group> {
    std::vector<Group> groups;
    const bool spl = R_SUCCEEDED(splInitialize());
    ON_SCOPE_EXIT(if (spl) splExit());

    for (auto* collect : {CollectConsole, CollectAtmosphere, CollectStorage, CollectPower, CollectBattery, CollectHardware, CollectPlayActivity}) {
        auto group = collect();
        if (!group.rows.empty()) {
            groups.push_back(std::move(group));
        }
    }
    log_write("[SYSINFO] %zu groups\n", groups.size());
    return groups;
}

} // namespace sphaira::system_info
