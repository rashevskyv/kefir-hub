#include "utils/utils.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "path_util.hpp"

#include <cstring>
#include <cstdio>
#include <switch.h>
#include <vector>
#include <string>
#include <string_view>

namespace sphaira::utils {

// Swap payload.bin with target payload (preserving Hekate in /bootloader/update.bin)
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

        if (!line.empty() && line.front() != '#' && line.front() != ';') {
            if (line.front() == '[' && line.back() == ']') {
                const auto section = line.substr(1, line.size() - 2);
                in_api_section = (section == "hekate-payload-api");
            } else if (in_api_section) {
                const auto eq = line.find('=');
                if (eq != std::string_view::npos) {
                    auto key = line.substr(0, eq);
                    auto val = line.substr(eq + 1);
                    while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.remove_suffix(1);
                    while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.remove_prefix(1);
                    if (key == "version" && val == "1") {
                        version_1 = true;
                    }
                }
            }
        }

        start = (end < content.size()) ? end + 1 : content.size();
    }

    return version_1;
}

} // namespace

bool rebootToPayload(const char* path) {
    if (!path || !*path) {
        log_write("rebootToPayload: invalid payload path\n");
        return false;
    }

    fs::FsNativeSd sd;

    std::string abs_path = path;
    if (!abs_path.starts_with("/")) {
        abs_path = "/" + abs_path;
    }

    std::string rel_path = abs_path;
    while (rel_path.starts_with("/")) {
        rel_path.erase(0, 1);
    }

    log_write("rebootToPayload: requested payload path=%s (rel=%s)\n", abs_path.c_str(), rel_path.c_str());

    if (!sd.FileExists(abs_path.c_str())) {
        log_write("rebootToPayload: payload file %s does not exist on SD\n", abs_path.c_str());
        return false;
    }

    // Try Hekate Payload API first (clean handoff without modifying payload.bin or hekate_ipl.ini)
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

} // namespace sphaira::utils
