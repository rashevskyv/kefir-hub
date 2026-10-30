#pragma once

// BIOS downloader: the pure part. Which emulator wants which files where, built from the
// manifest in romfs:/bios/bios.json (made by tools/bios/build_manifest.py from retrobios).
// No libnx here, so tests/test_bios_plan.cpp runs it on the host.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::bios {

struct File {
    std::string dest;   // RetroArch destination under system/, may hold a folder ("fbneo/neogeo.zip")
    std::string path;   // path in the retrobios repo ("bios/Sony/PlayStation/scph5501.bin")
    std::string sha1;
    std::uint64_t size{};
};

struct System {
    std::string id;     // "sony-playstation"
    std::string name;   // "Sony - PlayStation"
    std::string tico;   // Tico folder under /tico/system/, empty when Tico has none
    std::vector<File> files;
};

struct Manifest {
    std::string source; // "https://raw.githubusercontent.com/Abdess/retrobios/main/"
    std::vector<System> systems;
    // libretro core name (as in <core>_libretro_libnx.nro) -> system ids
    std::vector<std::pair<std::string, std::vector<std::string>>> cores;
};

// the emulators the Hub knows the BIOS folder of. Paths from the emulators' docs
// (HaulNX wiki, Reference-BIOS-Files); the console-verified ones are marked.
enum class Target {
    RetroArch,  // /retroarch/cores/system/<dest>              (libretro System.dat)
    Tico,       // /tico/system/<tico folder>/<file name>       (wiki: psx verified)
    Flycast,    // /flycast/data/<file name>                    (dc, naomi, atomiswave)
    MelonDS,    // /switch/melonds/<file name>                  (nds)
    DraStic,    // /switch/drastic/system/nds_bios_arm7.bin ... (nds, renamed)
    NetherSX2,  // /switch/nethersx2/bios/<file name>           (ps2)
    DuckStation,// /switch/duckstation/bios/<file name>         (psx)
    Count,
};

struct TargetInfo {
    Target target;
    const char* name;
    const char* detect;    // a folder whose presence means the emulator is installed
    const char* bios_root; // where the files go (with the trailing slash)
    // systems this emulator can use; empty = every system of the manifest (RetroArch, Tico by its folder map)
    std::vector<std::string_view> systems;
};

inline const std::vector<TargetInfo>& Targets() {
    static const std::vector<TargetInfo> t{
        {Target::RetroArch, "RetroArch", "/retroarch/cores", "/retroarch/cores/system/", {}},
        {Target::Tico, "Tico", "/tico", "/tico/system/", {}},
        {Target::Flycast, "Flycast", "/flycast", "/flycast/data/", {"sega-dreamcast", "sega-dreamcast-arcade"}},
        {Target::MelonDS, "melonDS", "/switch/melonds", "/switch/melonds/", {"nintendo-ds", "nintendo-dsi"}},
        {Target::DraStic, "DraStic", "/switch/drastic", "/switch/drastic/system/", {"nintendo-ds"}},
        {Target::NetherSX2, "NetherSX2", "/switch/nethersx2", "/switch/nethersx2/bios/", {"sony-playstation-2"}},
        {Target::DuckStation, "DuckStation", "/switch/duckstation", "/switch/duckstation/bios/", {"sony-playstation"}},
    };
    return t;
}

inline const TargetInfo& Info(Target target) {
    return Targets()[static_cast<size_t>(target)];
}

// "pcsx_rearmed_libretro_libnx.nro" -> "pcsx_rearmed"; "" when it is not a core file
inline std::string CoreName(std::string_view nro) {
    constexpr std::string_view tail = "_libretro_libnx.nro";
    if (nro.size() <= tail.size() || nro.substr(nro.size() - tail.size()) != tail) {
        return {};
    }
    std::string out{nro.substr(0, nro.size() - tail.size())};
    for (auto& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

inline std::string FileName(std::string_view dest) {
    const auto slash = dest.rfind('/');
    return std::string{slash == std::string_view::npos ? dest : dest.substr(slash + 1)};
}

// DraStic wants the DS dumps under its own names
inline std::string DrasticName(std::string_view name) {
    if (name == "bios7.bin") return "nds_bios_arm7.bin";
    if (name == "bios9.bin") return "nds_bios_arm9.bin";
    if (name == "firmware.bin") return "nds_firmware.bin";
    return std::string{name};
}

// full path on the SD card for one file of one system, empty when this emulator has no place for it
inline std::string DestPath(Target target, const System& sys, const File& file) {
    const auto& info = Info(target);
    switch (target) {
        case Target::RetroArch:
            return std::string{info.bios_root} + file.dest;
        case Target::Tico:
            if (sys.tico.empty()) return {};
            return std::string{info.bios_root} + sys.tico + "/" + FileName(file.dest);
        case Target::DraStic:
            return std::string{info.bios_root} + DrasticName(FileName(file.dest));
        default:
            return std::string{info.bios_root} + FileName(file.dest);
    }
}

// the systems an emulator can take files for at all
inline bool Accepts(Target target, const System& sys) {
    const auto& info = Info(target);
    if (target == Target::Tico) {
        return !sys.tico.empty();
    }
    if (info.systems.empty()) {
        return true;
    }
    for (auto s : info.systems) {
        if (s == sys.id) return true;
    }
    return false;
}

// system ids the installed RetroArch cores map to (unknown cores map to nothing)
inline std::vector<std::string> SystemsOfCores(const Manifest& m, const std::vector<std::string>& cores) {
    std::vector<std::string> out;
    for (const auto& core : cores) {
        for (const auto& [name, ids] : m.cores) {
            if (name != core) continue;
            for (const auto& id : ids) {
                bool seen = false;
                for (const auto& o : out) seen = seen || o == id;
                if (!seen) out.push_back(id);
            }
        }
    }
    return out;
}

struct Job {
    std::string url;   // source + path, the path percent-encoded
    std::string dest;  // full SD path
    std::string sha1;
    std::uint64_t size{};
};

inline std::string EncodePath(std::string_view path) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : path) {
        const bool keep = std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~';
        if (keep) {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

// the download list for one emulator. `only` (system ids) limits it to those systems; empty = all it accepts.
inline std::vector<Job> Plan(const Manifest& m, Target target, const std::vector<std::string>& only) {
    std::vector<Job> jobs;
    for (const auto& sys : m.systems) {
        if (!Accepts(target, sys)) continue;
        if (!only.empty()) {
            bool wanted = false;
            for (const auto& id : only) wanted = wanted || id == sys.id;
            if (!wanted) continue;
        }
        for (const auto& f : sys.files) {
            auto dest = DestPath(target, sys, f);
            if (dest.empty()) continue;
            jobs.push_back({m.source + EncodePath(f.path), std::move(dest), f.sha1, f.size});
        }
    }
    return jobs;
}

inline std::uint64_t TotalSize(const std::vector<Job>& jobs) {
    std::uint64_t n = 0;
    for (const auto& j : jobs) n += j.size;
    return n;
}

} // namespace sphaira::bios
