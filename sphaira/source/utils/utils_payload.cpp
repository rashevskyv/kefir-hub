#include "utils/utils.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "path_util.hpp"

#include <cstring>
#include <cstdio>
#include <switch.h>
#include <vector>
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <compare>

namespace sphaira::utils {

// Hekate IPL ini manipulation
namespace {
    constexpr const char* HEKATE_INI_PATH = "/bootloader/hekate_ipl.ini";
    constexpr const char* HEKATE_INI_BAK_PATH = "/bootloader/hekate_ipl.ini.bak";
    constexpr const char* LOCKPICK_PAYLOAD_DIR = "/bootloader/payloads";

    bool backupHekateIni() {
        FILE* f_bak_check = fopen(HEKATE_INI_BAK_PATH, "rb");
        if (f_bak_check) {
            fseek(f_bak_check, 0, SEEK_END);
            const long bak_size = ftell(f_bak_check);
            fclose(f_bak_check);
            if (bak_size > 0) {
                log_write("backupHekateIni: backup already exists (%ld bytes), preserving it\n", bak_size);
                return true;
            }
        }

        FILE* f_ini = fopen(HEKATE_INI_PATH, "rb");
        if (!f_ini) {
            log_write("backupHekateIni: original hekate_ipl.ini not found\n");
            return true;
        }

        fseek(f_ini, 0, SEEK_END);
        const long size = ftell(f_ini);
        fseek(f_ini, 0, SEEK_SET);

        if (size <= 0) {
            fclose(f_ini);
            return true;
        }

        std::vector<u8> buffer(size);
        fread(buffer.data(), 1, size, f_ini);
        fclose(f_ini);

        FILE* f_bak = fopen(HEKATE_INI_BAK_PATH, "wb");
        if (!f_bak) {
            log_write("backupHekateIni: failed to create backup\n");
            return false;
        }

        fwrite(buffer.data(), 1, size, f_bak);
        fflush(f_bak);
        fsdevCommitDevice("sdmc");
        fclose(f_bak);
        log_write("backupHekateIni: created backup (%ld bytes)\n", size);
        return true;
    }

    bool writeHekateAutobootIni(const char* payload_path) {
        if (!payload_path || !*payload_path) {
            log_write("writeHekateAutobootIni: invalid empty payload path\n");
            return false;
        }

        std::string_view p{payload_path};
        if (p.starts_with("sdmc:/")) p.remove_prefix(6);
        else if (p.starts_with("sdmc:")) p.remove_prefix(5);
        else if (p.starts_with("sd:/")) p.remove_prefix(4);
        else if (p.starts_with("sd:")) p.remove_prefix(3);
        while (!p.empty() && p.front() == '/') {
            p.remove_prefix(1);
        }
        std::string rel_p{p};

        FILE* f_out = fopen(HEKATE_INI_PATH, "wb");
        if (!f_out) {
            log_write("writeHekateAutobootIni: failed to open %s for writing\n", HEKATE_INI_PATH);
            return false;
        }

        const int written = std::fprintf(
            f_out,
            "[config]\n"
            "autoboot=1\n"
            "autoboot_list=0\n"
            "bootwait=0\n"
            "verification=1\n"
            "backlight=100\n"
            "autohosoff=2\n"
            "autonogc=1\n"
            "updater2p=1\n"
            "\n"
            "[HATS Payload]\n"
            "payload=%s\n",
            rel_p.c_str()
        );

        fclose(f_out);
        fsdevCommitDevice("sdmc");
        return written > 0;
    }

    std::string toLower(std::string value) {
        std::ranges::transform(value, value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    bool isLockpickPayloadName(const std::string& name) {
        const auto lower = toLower(name);
        return lower == "lockpick_rcm_pro.bin" || lower == "lockpick_rcm.bin";
    }

    bool findLockpickPayloadInDir(const char* dir_path, fs::FsPath& out) {
        fs::FsNativeSd fs;
        fs::Dir dir;
        if (R_FAILED(fs.OpenDirectory(dir_path, FsDirOpenMode_ReadFiles | FsDirOpenMode_NoFileSize, &dir))) {
            return false;
        }

        std::vector<FsDirectoryEntry> entries;
        if (R_FAILED(dir.ReadAll(entries))) {
            return false;
        }

        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            return toLower(a.name) < toLower(b.name);
        });

        for (const auto& entry : entries) {
            const auto lower = toLower(entry.name);
            if (lower == "lockpick_rcm_pro.bin") {
                out = fs::AppendPath(dir_path, entry.name);
                return true;
            }
        }

        for (const auto& entry : entries) {
            if (isLockpickPayloadName(entry.name)) {
                out = fs::AppendPath(dir_path, entry.name);
                return true;
            }
        }

        return false;
    }
}

// Set hekate_ipl.ini to auto-boot a payload through hekate.
// Backs up the original hekate_ipl.ini to .bak, then writes a temporary
// autoboot entry that points to payload_path.
bool setHekateAutobootPayload(const char* payload_path) {
    log_write("setHekateAutobootPayload: setting up autoboot for %s\n", payload_path);

    if (!backupHekateIni()) {
        log_write("setHekateAutobootPayload: failed to backup hekate ini\n");
        return false;
    }

    if (!writeHekateAutobootIni(payload_path)) {
        log_write("setHekateAutobootPayload: failed to write autoboot ini\n");
        return false;
    }

    fsdevCommitDevice("sdmc");
    log_write("setHekateAutobootPayload: hekate_ipl.ini updated successfully\n");
    return true;
}

bool findLockpickPayload(fs::FsPath& out) {
    return findLockpickPayloadInDir(LOCKPICK_PAYLOAD_DIR, out);
}

namespace {

#pragma pack(push, 1)
struct TegraExplorerFooter {
    char magic[4];
    u8 format_version;
    u8 payload_type;
    u8 app_version_major;
    u8 app_version_minor;
    u8 app_version_patch;
    u8 reserved[3];
    u32 kefir_version;
    char magic_end[4];
};
#pragma pack(pop)
static_assert(sizeof(TegraExplorerFooter) == 20);

struct TegraExplorerVersion {
    u32 major{0};
    u32 minor{0};
    u32 patch{0};
    u32 kefir{0};

    auto operator<=>(const TegraExplorerVersion&) const = default;
};

bool parseTegraExplorerVersion(const u8* data, size_t size, TegraExplorerVersion& out_ver) {
    out_ver = {};
    if (size < sizeof(TegraExplorerFooter)) {
        return false;
    }
    const size_t search_start = size >= 512 ? size - 512 : 0;
    for (size_t i = size - sizeof(TegraExplorerFooter); i >= search_start; --i) {
        if (std::memcmp(&data[i], "KFRP", 4) == 0 &&
            std::memcmp(&data[i + 16], "PRFK", 4) == 0) {
            TegraExplorerFooter footer{};
            std::memcpy(&footer, &data[i], sizeof(footer));
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

bool findExistingTegraExplorerPayloadOnSd(fs::FsPath& out) {
    fs::FsNativeSd fs;
    fs::Dir dir;
    if (R_FAILED(fs.OpenDirectory(LOCKPICK_PAYLOAD_DIR, FsDirOpenMode_ReadFiles | FsDirOpenMode_NoFileSize, &dir))) {
        log_write("findExistingTegraExplorerPayloadOnSd: cannot open %s\n", LOCKPICK_PAYLOAD_DIR);
        return false;
    }
    std::vector<FsDirectoryEntry> entries;
    if (R_FAILED(dir.ReadAll(entries))) {
        return false;
    }
    fs::FsPath exact;
    fs::FsPath any;
    for (const auto& entry : entries) {
        const auto lower = toLower(entry.name);
        if (lower.size() < 4 || lower.compare(lower.size() - 4, 4, ".bin") != 0) {
            continue;
        }
        if (lower.find("tegraexplorer") == std::string::npos && lower.find("tegra_explorer") == std::string::npos) {
            continue;
        }
        const auto path = fs::AppendPath(LOCKPICK_PAYLOAD_DIR, entry.name);
        if (lower == "tegraexplorer.bin") {
            exact = path;
            break;
        }
        if (any.empty()) {
            any = path;
        }
    }
    out = !exact.empty() ? exact : any;
    return !out.empty();
}

} // namespace

bool ensureTegraExplorerPayload(fs::FsPath& out) {
    std::vector<u8> romfs_data;
    bool has_romfs = false;
    if (R_SUCCEEDED(romfsInit())) {
        has_romfs = R_SUCCEEDED(fs::read_entire_file("romfs:/tegra/TegraExplorer.bin", romfs_data)) && !romfs_data.empty();
        romfsExit();
    }

    fs::FsPath sd_path;
    const bool has_sd = findExistingTegraExplorerPayloadOnSd(sd_path);

    if (!has_romfs) {
        if (has_sd) {
            out = sd_path;
            log_write("ensureTegraExplorerPayload: no romfs payload, using existing SD payload: %s\n", static_cast<const char*>(out));
            return true;
        }
        log_write("ensureTegraExplorerPayload: TegraExplorer.bin not found in romfs and not on SD\n");
        return false;
    }

    TegraExplorerVersion romfs_ver{};
    parseTegraExplorerVersion(romfs_data.data(), romfs_data.size(), romfs_ver);

    fs::FsNativeSd sd;
    if (!has_sd) {
        constexpr const char* target = "/bootloader/payloads/TegraExplorer.bin";
        sd.CreateDirectoryRecursively(LOCKPICK_PAYLOAD_DIR);
        FILE* fp = fopen(target, "wb");
        bool written = false;
        if (fp) {
            written = (fwrite(romfs_data.data(), 1, romfs_data.size(), fp) == romfs_data.size());
            fflush(fp);
            fclose(fp);
        }
        if (!written) {
            sd.write_entire_file(target, romfs_data);
        }
        fsdevCommitDevice("sdmc");
        sd.Commit();
        out = target;
        log_write("ensureTegraExplorerPayload: installed RomFS TegraExplorer v%u.%u.%u.%u to %s\n",
            romfs_ver.major, romfs_ver.minor, romfs_ver.patch, romfs_ver.kefir, target);
        return true;
    }

    std::vector<u8> sd_data;
    TegraExplorerVersion sd_ver{};
    if (R_SUCCEEDED(sd.read_entire_file(sd_path, sd_data)) && !sd_data.empty()) {
        parseTegraExplorerVersion(sd_data.data(), sd_data.size(), sd_ver);
    }

    log_write("ensureTegraExplorerPayload: SD v%u.%u.%u.%u vs RomFS v%u.%u.%u.%u\n",
        sd_ver.major, sd_ver.minor, sd_ver.patch, sd_ver.kefir,
        romfs_ver.major, romfs_ver.minor, romfs_ver.patch, romfs_ver.kefir);

    if (sd_ver < romfs_ver) {
        FILE* fp = fopen(static_cast<const char*>(sd_path), "wb");
        bool written = false;
        if (fp) {
            written = (fwrite(romfs_data.data(), 1, romfs_data.size(), fp) == romfs_data.size());
            fflush(fp);
            fclose(fp);
        }
        if (!written) {
            sd.write_entire_file(sd_path, romfs_data);
        }
        fsdevCommitDevice("sdmc");
        sd.Commit();
        log_write("ensureTegraExplorerPayload: upgraded %s to v%u.%u.%u.%u\n",
            static_cast<const char*>(sd_path),
            romfs_ver.major, romfs_ver.minor, romfs_ver.patch, romfs_ver.kefir);
    } else {
        log_write("ensureTegraExplorerPayload: keeping existing SD payload (%s, v%u.%u.%u.%u >= v%u.%u.%u.%u)\n",
            static_cast<const char*>(sd_path),
            sd_ver.major, sd_ver.minor, sd_ver.patch, sd_ver.kefir,
            romfs_ver.major, romfs_ver.minor, romfs_ver.patch, romfs_ver.kefir);
    }

    out = sd_path;
    return true;
}

bool findTegraExplorerPayload(fs::FsPath& out) {
    return ensureTegraExplorerPayload(out);
}

// Restore hekate_ipl.ini from backup
bool restoreHekateIni() {
    FILE* f_bak = fopen(HEKATE_INI_BAK_PATH, "rb");
    if (!f_bak) {
        log_write("restoreHekateIni: no backup found, nothing to restore\n");
        return false;
    }

    fseek(f_bak, 0, SEEK_END);
    long size = ftell(f_bak);
    fseek(f_bak, 0, SEEK_SET);

    if (size <= 0) {
        log_write("restoreHekateIni: backup is empty or invalid\n");
        fclose(f_bak);
        remove(HEKATE_INI_BAK_PATH);
        return false;
    }

    std::vector<u8> backup_data(size);
    fread(backup_data.data(), 1, size, f_bak);
    fclose(f_bak);

    FILE* f_out = fopen(HEKATE_INI_PATH, "wb");
    if (!f_out) {
        log_write("restoreHekateIni: failed to open %s for writing\n", HEKATE_INI_PATH);
        return false;
    }

    fwrite(backup_data.data(), 1, size, f_out);
    fclose(f_out);

    remove(HEKATE_INI_BAK_PATH);

    fsdevCommitDevice("sdmc");

    log_write("restoreHekateIni: hekate_ipl.ini restored from backup (%ld bytes)\n", size);
    return true;
}

} // namespace sphaira::utils
