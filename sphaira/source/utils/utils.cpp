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
            payload_path
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

bool findTegraExplorerPayload(fs::FsPath& out) {
    fs::FsNativeSd fs;
    fs::Dir dir;
    if (R_FAILED(fs.OpenDirectory(LOCKPICK_PAYLOAD_DIR, FsDirOpenMode_ReadFiles | FsDirOpenMode_NoFileSize, &dir))) {
        log_write("findTegraExplorerPayload: cannot open %s\n", LOCKPICK_PAYLOAD_DIR);
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
    if (out.empty()) {
        log_write("findTegraExplorerPayload: no TegraExplorer .bin in %s\n", LOCKPICK_PAYLOAD_DIR);
        return false;
    }
    log_write("findTegraExplorerPayload: %s\n", static_cast<const char*>(out));
    return true;
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
// NOTE: HATS installer payload handles the actual swapping on boot
// This function is kept for potential future use
bool swapPayload(const char* path) {
    constexpr const char* PAYLOAD_BIN = "/payload.bin";
    constexpr const char* PAYLOAD_BAK = "/payload.bak";
    constexpr const char* UPDATE_BIN = "/bootloader/update.bin";
    constexpr const char* UPDATE_BAK = "/bootloader/update.bak";

    log_write("swapPayload: swapping with HATS installer: %s\n", path);

    // Step 1: Read HATS installer into memory
    FILE* f_installer = fopen(path, "rb");
    if (!f_installer) {
        log_write("swapPayload: HATS installer not found: %s\n", path);
        return false;
    }
    fseek(f_installer, 0, SEEK_END);
    long installer_size = ftell(f_installer);
    fseek(f_installer, 0, SEEK_SET);

    if (installer_size <= 0) {
        log_write("swapPayload: invalid HATS installer size: %ld\n", installer_size);
        fclose(f_installer);
        return false;
    }

    std::vector<u8> installer_data(installer_size);
    size_t bytes_read = fread(installer_data.data(), 1, installer_size, f_installer);
    fclose(f_installer);

    if (bytes_read != (size_t)installer_size) {
        log_write("swapPayload: failed to read HATS installer\n");
        return false;
    }
    log_write("swapPayload: read HATS installer (%ld bytes)\n", installer_size);

    fs::FsNativeSd fs;
    fs.CreateDirectory("/bootloader");

    // Helper lambda to swap a payload file
    auto swap_file = [&](const char* src_path, const char* bak_path) {
        FILE* f_src = fopen(src_path, "rb");
        if (!f_src) {
            log_write("swapPayload: %s not found, skipping\n", src_path);
            return;
        }
        fclose(f_src);

        log_write("swapPayload: backing up %s to %s\n", src_path, bak_path);

        // Read original
        f_src = fopen(src_path, "rb");
        fseek(f_src, 0, SEEK_END);
        long size = ftell(f_src);
        fseek(f_src, 0, SEEK_SET);

        if (size > 0) {
            std::vector<u8> original(size);
            fread(original.data(), 1, size, f_src);
            fclose(f_src);

            // Write backup
            FILE* f_bak = fopen(bak_path, "wb");
            if (f_bak) {
                fwrite(original.data(), 1, size, f_bak);
                fclose(f_bak);
                log_write("swapPayload: backed up %s (%ld bytes)\n", src_path, size);
            }

            // Write HATS installer
            FILE* f_dst = fopen(src_path, "wb");
            if (f_dst) {
                fwrite(installer_data.data(), 1, installer_size, f_dst);
                fclose(f_dst);
                log_write("swapPayload: wrote HATS installer to %s (%ld bytes)\n", src_path, installer_size);
            }
        } else {
            fclose(f_src);
        }
    };

    // Step 2: Swap /payload.bin (modchip looks here first)
    swap_file(PAYLOAD_BIN, PAYLOAD_BAK);

    // Step 3: Swap /bootloader/update.bin (modchip fallback)
    swap_file(UPDATE_BIN, UPDATE_BAK);

    // Step 4: Sync filesystem
    log_write("swapPayload: syncing filesystem...\n");
    fsdevCommitDevice("sdmc");

    log_write("swapPayload: swap complete\n");
    return true;
}

// Revert payload swap (restore hekate from backup)
// Returns true if reverted, false if no backup existed
bool revertPayloadSwap() {
    constexpr const char* PAYLOAD_BIN = "/payload.bin";
    constexpr const char* PAYLOAD_BAK = "/payload.bak";
    constexpr const char* UPDATE_BIN = "/bootloader/update.bin";
    constexpr const char* UPDATE_BAK = "/bootloader/update.bak";

    bool reverted = false;

    // Helper lambda to restore a file from backup
    auto restore_file = [&](const char* dst_path, const char* bak_path) {
        FILE* f_bak = fopen(bak_path, "rb");
        if (!f_bak) {
            return;
        }

        fseek(f_bak, 0, SEEK_END);
        long bak_size = ftell(f_bak);
        fseek(f_bak, 0, SEEK_SET);

        if (bak_size > 0) {
            std::vector<u8> backup_data(bak_size);
            fread(backup_data.data(), 1, bak_size, f_bak);
            fclose(f_bak);

            FILE* f_dst = fopen(dst_path, "wb");
            if (f_dst) {
                fwrite(backup_data.data(), 1, bak_size, f_dst);
                fclose(f_dst);
                log_write("revertPayloadSwap: restored %s (%ld bytes)\n", dst_path, bak_size);
                reverted = true;
            }
        } else {
            fclose(f_bak);
        }
        remove(bak_path);
    };

    // Restore all backups
    restore_file(PAYLOAD_BIN, PAYLOAD_BAK);
    restore_file(UPDATE_BIN, UPDATE_BAK);

    if (!reverted) {
        log_write("revertPayloadSwap: no backup found, nothing to revert\n");
        return false;
    }

    // Sync filesystem
    fsdevCommitDevice("sdmc");

    log_write("revertPayloadSwap: revert complete\n");
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

// Reboot to a payload file via Hekate one-shot payload API.
// Returns true on success, false on failure
bool rebootToPayload(const char* path) {
    if (!path || !*path) {
        log_write("rebootToPayload: invalid null/empty path\n");
        return false;
    }

    log_write("rebootToPayload: requested payload launch for: %s\n", path);

    if (!isHekatePayloadApiSupported()) {
        log_write("rebootToPayload: Hekate payload API marker missing or unsupported\n");
        return false;
    }

    std::string rel_path;
    std::string abs_path;
    if (!normalizeAndValidatePayloadPath(path, rel_path, abs_path)) {
        log_write("rebootToPayload: path normalization/validation failed for: %s\n", path);
        return false;
    }

    fs::FsNativeSd sd;
    if (!sd.FileExists(abs_path.c_str())) {
        log_write("rebootToPayload: payload file does not exist: %s\n", abs_path.c_str());
        return false;
    }

    std::string request = "[launch]\nversion=1\npayload=" + rel_path + "\n";

    sd.CreateDirectoryRecursively("/config/kefir");
    sd.DeleteFile(HEKATE_REQUEST_TMP_PATH);

    if (R_FAILED(sd.write_entire_file(HEKATE_REQUEST_TMP_PATH, std::vector<u8>(request.begin(), request.end())))) {
        log_write("rebootToPayload: failed to write temporary request file\n");
        return false;
    }

    sd.DeleteFile(HEKATE_REQUEST_PATH);
    if (R_FAILED(sd.RenameFile(HEKATE_REQUEST_TMP_PATH, HEKATE_REQUEST_PATH))) {
        log_write("rebootToPayload: failed to rename temporary request file to %s\n", HEKATE_REQUEST_PATH);
        sd.DeleteFile(HEKATE_REQUEST_TMP_PATH);
        return false;
    }

    fsdevCommitDevice("sdmc");

    log_write("rebootToPayload: Hekate payload request written for %s, rebooting...\n", rel_path.c_str());

    const Result rc = requestForcedReboot();
    if (R_FAILED(rc)) {
        log_write("rebootToPayload: requestForcedReboot failed: 0x%x\n", rc);
        return false;
    }

    return true;
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
