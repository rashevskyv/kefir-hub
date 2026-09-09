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
namespace {

// IRAM payload constants (from reboot_to_payload in ftpsrv)
constexpr u32 IRAM_PAYLOAD_MAX_SIZE = 0x24000;
constexpr uintptr_t AMS_IWRAM_OFFSET = 0x40010000;

HashStr hexIdToStrInternal(auto id) {
    HashStr str{};
    const auto id_lower = std::byteswap(*(u64*)id.c);
    const auto id_upper = std::byteswap(*(u64*)(id.c + 0x8));
    std::snprintf(str.str, 0x21, "%016lx%016lx", id_lower, id_upper);
    return str;
}

std::string formatSizeInetrnal(double size, double base) {
    static const char* const suffixes[] = { "B", "KB", "MB", "GB", "TB", "PB", "EB" };
    size_t suffix_index = 0;

    while (size >= base && suffix_index < std::size(suffixes) - 1) {
        size /= base;
        suffix_index++;
    }

    char buffer[32];
    if (suffix_index == 0) {
        std::snprintf(buffer, sizeof(buffer), "%.0f %s", size, suffixes[suffix_index]);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.2f %s", size, suffixes[suffix_index]);
    }

    return buffer;
}

} // namespace

HashStr hexIdToStr(FsRightsId id) {
    return hexIdToStrInternal(id);
}

HashStr hexIdToStr(NcmRightsId id) {
    return hexIdToStrInternal(id.rights_id);
}

HashStr hexIdToStr(NcmContentId id) {
    return hexIdToStrInternal(id);
}

std::string formatSizeStorage(u64 size) {
    return formatSizeInetrnal(size, 1024.0);
}

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

// Swap payload.bin with HATS installer (no reboot)
// Returns true on success, false on failure
// Swap /payload.bin with target payload (preserving Hekate in /bootloader/update.bin)
// Returns true on success, false on failure
bool swapPayload(const char* path) {
    constexpr const char* PAYLOAD_BIN = "/payload.bin";
    constexpr const char* UPDATE_BIN = "/bootloader/update.bin";

    log_write("swapPayload: swapping %s with target payload: %s\n", PAYLOAD_BIN, path);

    fs::FsNativeSd sd;
    sd.CreateDirectoryRecursively("/bootloader");

    // Ensure /bootloader/update.bin contains Hekate before modifying /payload.bin
    if (!sd.FileExists(UPDATE_BIN) && sd.FileExists(PAYLOAD_BIN)) {
        std::vector<u8> hekate_data;
        if (R_SUCCEEDED(sd.read_entire_file(PAYLOAD_BIN, hekate_data)) && !hekate_data.empty()) {
            FILE* f_upd = fopen(UPDATE_BIN, "wb");
            if (f_upd) {
                fwrite(hekate_data.data(), 1, hekate_data.size(), f_upd);
                fflush(f_upd);
                fclose(f_upd);
                log_write("swapPayload: preserved Hekate from %s to %s (%zu bytes)\n",
                    PAYLOAD_BIN, UPDATE_BIN, hekate_data.size());
            } else {
                sd.write_entire_file(UPDATE_BIN, hekate_data);
            }
        }
    }

    // Read replacement payload into memory
    std::vector<u8> payload_data;
    if (R_FAILED(sd.read_entire_file(path, payload_data)) || payload_data.empty()) {
        log_write("swapPayload: failed to read replacement payload: %s\n", path);
        return false;
    }

    // Replace /payload.bin
    sd.DeleteFile(PAYLOAD_BIN);
    FILE* fp = fopen(PAYLOAD_BIN, "wb");
    bool written = false;
    if (fp) {
        written = (fwrite(payload_data.data(), 1, payload_data.size(), fp) == payload_data.size());
        fflush(fp);
        fclose(fp);
    }
    if (!written) {
        if (R_FAILED(sd.write_entire_file(PAYLOAD_BIN, payload_data))) {
            log_write("swapPayload: failed to write %s\n", PAYLOAD_BIN);
            return false;
        }
    }

    fsdevCommitDevice("sdmc");
    sd.Commit();
    log_write("swapPayload: successfully swapped %s with %s (%zu bytes)\n",
        PAYLOAD_BIN, path, payload_data.size());
    return true;
}

// Revert payload swap (restore Hekate from /bootloader/update.bin)
// Returns true if reverted, false if no backup existed
bool revertPayloadSwap() {
    constexpr const char* PAYLOAD_BIN = "/payload.bin";
    constexpr const char* UPDATE_BIN = "/bootloader/update.bin";

    fs::FsNativeSd sd;
    if (!sd.FileExists(UPDATE_BIN)) {
        log_write("revertPayloadSwap: %s not found, nothing to restore\n", UPDATE_BIN);
        return false;
    }

    std::vector<u8> hekate_data;
    if (R_FAILED(sd.read_entire_file(UPDATE_BIN, hekate_data)) || hekate_data.empty()) {
        log_write("revertPayloadSwap: failed to read %s\n", UPDATE_BIN);
        return false;
    }

    sd.DeleteFile(PAYLOAD_BIN);
    FILE* fp = fopen(PAYLOAD_BIN, "wb");
    bool written = false;
    if (fp) {
        written = (fwrite(hekate_data.data(), 1, hekate_data.size(), fp) == hekate_data.size());
        fflush(fp);
        fclose(fp);
    }
    if (!written) {
        if (R_FAILED(sd.write_entire_file(PAYLOAD_BIN, hekate_data))) {
            log_write("revertPayloadSwap: failed to write %s\n", PAYLOAD_BIN);
            return false;
        }
    }

    fsdevCommitDevice("sdmc");
    sd.Commit();
    log_write("revertPayloadSwap: successfully restored %s from %s (%zu bytes)\n",
        PAYLOAD_BIN, UPDATE_BIN, hekate_data.size());
    return true;
}

Result requestForcedReboot() {
    Result rc = appletRequestToReboot();
    if (R_SUCCEEDED(rc)) {
        return rc;
    }

    rc = spsmInitialize();
    if (R_SUCCEEDED(rc)) {
        rc = spsmShutdown(true);
        spsmExit();
        if (R_SUCCEEDED(rc)) {
            return rc;
        }
    }

    rc = bpcInitialize();
    if (R_SUCCEEDED(rc)) {
        rc = bpcRebootSystem();
        bpcExit();
        if (R_SUCCEEDED(rc)) {
            return rc;
        }
    }

    return rc;
}

namespace {

constexpr const char* HEKATE_API_MARKER_PATH = "/config/kefir/hekate-payload-api.ini";
constexpr const char* HEKATE_REQUEST_PATH = "/config/kefir/hekate-payload-request.ini";
constexpr const char* HEKATE_REQUEST_TMP_PATH = "/config/kefir/hekate-payload-request.ini.tmp";

bool isHekatePayloadApiSupported() {
    fs::FsNativeSd sd;
    std::vector<u8> data;
    if (R_FAILED(sd.read_entire_file(HEKATE_API_MARKER_PATH, data)) || data.empty()) {
        log_write("isHekatePayloadApiSupported: capability marker %s not found or empty\n", HEKATE_API_MARKER_PATH);
        return false;
    }

    std::string_view content(reinterpret_cast<const char*>(data.data()), data.size());
    bool in_api_section = false;
    bool version_1 = false;

    size_t start = 0;
    while (start < content.size()) {
        size_t end = content.find_first_of("\r\n", start);
        if (end == std::string_view::npos) {
            end = content.size();
        }
        std::string_view line = content.substr(start, end - start);
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
            line.remove_prefix(1);
        }
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
            line.remove_suffix(1);
        }

        if (!line.empty() && line.front() != ';' && line.front() != '#') {
            if (line.front() == '[' && line.back() == ']') {
                std::string_view sec = line.substr(1, line.size() - 2);
                in_api_section = (sec == "api");
            } else if (in_api_section) {
                auto eq = line.find('=');
                if (eq != std::string_view::npos) {
                    std::string_view key = line.substr(0, eq);
                    std::string_view val = line.substr(eq + 1);
                    while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.remove_suffix(1);
                    while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.remove_prefix(1);
                    if (key == "version" && val == "1") {
                        version_1 = true;
                    }
                }
            }
        }

        start = content.find_first_not_of("\r\n", end);
        if (start == std::string_view::npos) {
            break;
        }
    }

    if (!version_1) {
        log_write("isHekatePayloadApiSupported: capability marker missing valid version=1 in [api]\n");
        return false;
    }

    return true;
}

bool normalizeAndValidatePayloadPath(std::string_view raw_path, std::string& out_rel_path, std::string& out_abs_sd_path) {
    if (raw_path.empty()) {
        return false;
    }

    if (raw_path.starts_with("sdmc:/")) {
        raw_path.remove_prefix(6);
    } else if (raw_path.starts_with("sdmc:")) {
        raw_path.remove_prefix(5);
    } else if (raw_path.starts_with("sd:/")) {
        raw_path.remove_prefix(4);
    } else if (raw_path.starts_with("sd:")) {
        raw_path.remove_prefix(3);
    }

    while (!raw_path.empty() && (raw_path.front() == '/' || raw_path.front() == '\\')) {
        if (raw_path.front() == '\\') {
            return false;
        }
        raw_path.remove_prefix(1);
    }

    if (raw_path.empty()) {
        return false;
    }

    for (char c : raw_path) {
        if (c == '\\' || c == ':' || c == '*' || c == '?' || static_cast<unsigned char>(c) < 32 || c == 127) {
            return false;
        }
    }

    size_t seg_start = 0;
    while (seg_start < raw_path.size()) {
        size_t seg_end = raw_path.find('/', seg_start);
        if (seg_end == std::string_view::npos) {
            seg_end = raw_path.size();
        }
        std::string_view segment = raw_path.substr(seg_start, seg_end - seg_start);
        if (segment.empty() || segment == "." || segment == "..") {
            return false;
        }
        seg_start = seg_end + 1;
    }

    if (raw_path.size() < 4) {
        return false;
    }
    std::string_view ext = raw_path.substr(raw_path.size() - 4);
    if (!path::EqualsIC(ext, ".bin")) {
        return false;
    }

    out_rel_path = std::string(raw_path);
    out_abs_sd_path = "/" + out_rel_path;
    return true;
}

} // namespace

// Reboot to a payload file via Hekate one-shot payload API or autoboot fallback.
// Returns true on success, false on failure
bool rebootToPayload(const char* path) {
    if (!path || !*path) {
        log_write("rebootToPayload: invalid null/empty path\n");
        return false;
    }

    log_write("rebootToPayload: requested payload launch for: %s\n", path);

    std::string rel_path;
    std::string abs_path;
    if (!normalizeAndValidatePayloadPath(path, rel_path, abs_path)) {
        log_write("rebootToPayload: path normalization/validation failed for: %s\n", path);
        return false;
    }

    const auto lower_rel = toLower(rel_path);
    if (lower_rel.find("tegraexplorer") != std::string::npos || lower_rel.find("tegra_explorer") != std::string::npos) {
        fs::FsPath te_path;
        if (ensureTegraExplorerPayload(te_path)) {
            rel_path = static_cast<const char*>(te_path);
            if (rel_path.starts_with("/")) {
                rel_path = rel_path.substr(1);
            }
            abs_path = "/" + rel_path;
        }
    }

    fs::FsNativeSd sd;
    if (!sd.FileExists(abs_path.c_str())) {
        log_write("rebootToPayload: payload file does not exist: %s\n", abs_path.c_str());
        return false;
    }

    if (isHekatePayloadApiSupported()) {
        std::string request = "[launch]\nversion=1\npayload=" + rel_path + "\n";

        sd.CreateDirectoryRecursively("/config/kefir");
        sd.DeleteFile(HEKATE_REQUEST_TMP_PATH);

        if (R_SUCCEEDED(sd.write_entire_file(HEKATE_REQUEST_TMP_PATH, std::vector<u8>(request.begin(), request.end())))) {
            sd.DeleteFile(HEKATE_REQUEST_PATH);
            if (R_SUCCEEDED(sd.RenameFile(HEKATE_REQUEST_TMP_PATH, HEKATE_REQUEST_PATH))) {
                fsdevCommitDevice("sdmc");
                log_write("rebootToPayload: Hekate payload request written for %s, rebooting...\n", rel_path.c_str());
                const Result rc = requestForcedReboot();
                if (R_SUCCEEDED(rc)) {
                    return true;
                }
                log_write("rebootToPayload: requestForcedReboot failed after writing request: 0x%x\n", rc);
            } else {
                log_write("rebootToPayload: failed to rename temporary request file to %s\n", HEKATE_REQUEST_PATH);
                sd.DeleteFile(HEKATE_REQUEST_TMP_PATH);
            }
        } else {
            log_write("rebootToPayload: failed to write temporary request file\n");
        }
    } else {
        log_write("rebootToPayload: Hekate payload API marker missing or unsupported\n");
    }

    // Fallback: When Hekate Payload API is not available, swap /payload.bin with target payload
    // (preserving Hekate in /bootloader/update.bin) and set hekate_ipl.ini autoboot.
    log_write("rebootToPayload: falling back to payload.bin swap and hekate autoboot for %s\n", rel_path.c_str());
    const bool swapped = swapPayload(abs_path.c_str());
    const bool autoboot_set = setHekateAutobootPayload(rel_path.c_str());

    if (swapped || autoboot_set) {
        fsdevCommitDevice("sdmc");
        sd.Commit();
        const Result rc = requestForcedReboot();
        if (R_SUCCEEDED(rc)) {
            return true;
        }
        log_write("rebootToPayload: requestForcedReboot failed after fallback: 0x%x\n", rc);
    } else {
        log_write("rebootToPayload: fallback payload swap and autoboot both failed\n");
    }

    return false;
}

std::string Trim(std::string str) {
    const auto first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = str.find_last_not_of(" \t\r\n");
    str = str.substr(first, last - first + 1);
    if (str.size() >= 2 && ((str.front() == '\'' && str.back() == '\'') || (str.front() == '"' && str.back() == '"'))) {
        str = str.substr(1, str.size() - 2);
    }
    return str;
}

std::string TrimAsciiWhitespace(std::string value) {
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r' || value.back() == '\n')) {
        value.pop_back();
    }

    size_t start{};
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t' || value[start] == '\r' || value[start] == '\n')) {
        start++;
    }

    if (start) {
        value.erase(0, start);
    }
    return value;
}

} // namespace sphaira::utils
