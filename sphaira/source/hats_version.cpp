#include "hats_version.hpp"
#include "fs.hpp"
#include "log.hpp"

#include <dirent.h>
#include <cstring>
#include <string>

namespace sphaira::hats {

std::string getHatsVersion() {
    std::string hatsVersion = "Not Found";

    auto dir = opendir("/");
    if (!dir) {
        return hatsVersion;
    }

    while (auto d = readdir(dir)) {
        if (d->d_type != DT_REG) {
            continue;
        }

        const char* name = d->d_name;

        // Check for fixed filename HATS_VERSION.txt
        if (std::strcmp(name, "HATS_VERSION.txt") == 0) {
            // Read the version from the file (first line is "# HATS-YYYY-MM-DD-hash")
            std::string path = "/" + std::string(name);
            FILE* f = fopen(path.c_str(), "r");
            if (f) {
                char line[64];
                if (fgets(line, sizeof(line), f)) {
                    // Remove trailing newline
                    size_t len = std::strlen(line);
                    if (len > 0 && line[len - 1] == '\n') {
                        line[len - 1] = '\0';
                    }
                    // Skip leading "# " if present
                    const char* start = line;
                    if (std::strncmp(line, "# ", 2) == 0) {
                        start = line + 2;
                    }
                    hatsVersion = std::string(start);
                }
                fclose(f);
                break;
            }
        }

        // Legacy support: check for old HATS-*.txt format
        size_t len = std::strlen(name);
        if (len > 9 && std::strncmp(name, "HATS-", 5) == 0 &&
            std::strcmp(name + len - 4, ".txt") == 0 &&
            hatsVersion == "Not Found") {
            hatsVersion = std::string(name, len - 4);
            break;
        }
    }

    closedir(dir);
    return hatsVersion;
}

std::string getSystemFirmware() {
    SetSysFirmwareVersion ver;
    if (R_SUCCEEDED(setsysInitialize())) {
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&ver))) {
            setsysExit();
            return ver.display_version;
        }
        setsysExit();
    }
    return "Unknown";
}

std::string getAtmosphereVersion() {
    u64 version;
    std::string res = "Unknown";

    // Initialize spl service
    if (R_FAILED(splInitialize())) {
        return res;
    }

    if (R_SUCCEEDED(splGetConfig((SplConfigItem)65000, &version))) {
        res = std::to_string((version >> 56) & ((1 << 8) - 1)) + "." +
              std::to_string((version >> 48) & ((1 << 8) - 1)) + "." +
              std::to_string((version >> 40) & ((1 << 8) - 1));
    }

    splExit();
    return res;
}

std::string getKefirVersion() {
    FILE* f = fopen("/switch/kefir-updater/version", "r");
    if (f) {
        char buf[64];
        if (fgets(buf, sizeof(buf), f)) {
            fclose(f);
            size_t len = std::strlen(buf);
            while (len > 0 && (buf[len - 1] == '\r' || buf[len - 1] == '\n' || buf[len - 1] == ' ')) {
                buf[--len] = '\0';
            }
            char* start = buf;
            while (*start == ' ' || *start == '\t') {
                start++;
            }
            if (*start != '\0' && std::strcmp(start, "Not Found") != 0 && std::strcmp(start, "Unknown") != 0) {
                std::string s(start);
                if (s.rfind("kefir", 0) != 0 && s.rfind("Kefir", 0) != 0) {
                    return "Kefir " + s;
                }
                return s;
            }
        } else {
            fclose(f);
        }
    }

    std::string hats = getHatsVersion();
    if (hats != "Not Found" && hats != "Unknown") {
        return hats;
    }

    return "";
}

std::string getSystemVersionString() {
    std::string fw = getSystemFirmware();
    std::string ams = getAtmosphereVersion();
    std::string kefir = getKefirVersion();

    std::string sys_str;
    if (fw != "Unknown" && ams != "Unknown") {
        sys_str = fw + "|AMS " + ams;
    } else if (fw != "Unknown") {
        sys_str = fw;
    } else if (ams != "Unknown") {
        sys_str = "AMS " + ams;
    }

    if (!kefir.empty()) {
        if (!sys_str.empty()) {
            return kefir + " · " + sys_str;
        }
        return kefir;
    }
    return sys_str;
}

bool isErista() {
    u64 hardware_type;
    bool result = true; // Default to Erista if unknown

    if (R_FAILED(splInitialize())) {
        return result;
    }

    if (R_SUCCEEDED(splGetConfig(SplConfigItem_HardwareType, &hardware_type))) {
        // Erista types are 0 (Icosa), 1 (Copper)
        // Mariko types are 2 (Hoag), 3 (Iowa), 4 (Calcio), 5 (Aula)
        result = hardware_type <= 1;
    }

    splExit();
    return result;
}

} // namespace sphaira::hats
