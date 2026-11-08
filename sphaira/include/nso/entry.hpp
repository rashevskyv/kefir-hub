// titlesdb entries, file layout and code allocation per system. Field sets are the PROVEN ones
// (NES/SNES/N64/Genesis/GB/GBA rebuilt byte for byte from mods; Virtual Boy follows the 16 official entries: LIKELY).
#pragma once
#include "common.hpp"
#include "json.hpp"
#include "rom.hpp"
#include "systems.hpp"

#include <cstdio>
#include <set>
#include <string_view>

namespace nso {

struct EntryArgs {
    std::string code;
    std::string title;
    std::string publisher = "Unknown";
    std::string release_date = "2000-01-01";  // YYYY-MM-DD
    std::string lcla6_date;                   // "" -> DefaultAddedDate(system, release_date)
    int players = 1;
    bool simultaneous = false;
    bool cgb = false;                         // Game Boy: platform CGB (.gbc) or DMG
};

// Paths for one title. Files are written under <romfs>/titles/<code>/. db paths start with "/titles/".
struct Paths {
    std::string dir;          // "titles/<code>"  (relative to romfs, no trailing slash)
    std::string rom_file;     // file name of the ROM we write ("3123_e.sfrom", "CLV-P-NAAAE.nes", "N-1234_e.bnz" ...)
    std::string rom_db;       // "rom" value in the db
    std::string cover_file, cover_db;
    std::string details_file, details_db;
    std::string sig_file;     // SNES only
    std::string dtz_file;     // N64 only
};

inline Paths MakePaths(System s, const std::string& code, std::string_view rom_ext, bool n64_raw_bin = false) {
    Paths p;
    p.dir = "titles/" + code;
    const std::string d = "/titles/" + code + "/";
    const bool xtx = Info(s).cover == CoverKind::XtxZ;
    std::string stem = code;
    if (s == System::Snes) stem = code.substr(2);  // "S-3123_e" -> "3123_e"
    if (s == System::N64 || s == System::N64Mature) {
        p.rom_file = code + (n64_raw_bin ? ".bin" : ".bnz");
        p.rom_db = d + code + ".bin";  // the db always says .bin
        p.dtz_file = code + ".dtz";
    } else {
        p.rom_file = stem + "." + std::string(rom_ext);
        p.rom_db = d + p.rom_file;
    }
    if (s == System::Snes) p.sig_file = stem + ".sfromsig";
    p.cover_file = code + (xtx ? ".xtx.z" : ".png");
    p.details_file = code + (s == System::Nes ? "00" : "-details") + (xtx ? ".xtx.z" : ".png");
    p.cover_db = d + p.cover_file;
    p.details_db = d + p.details_file;
    return p;
}

inline std::string DefaultAddedDate(System s, const std::string& release_date) {
    switch (s) {
        case System::Snes: return "2030-11-01";   // the mods use future dates for added games
        case System::Nes: return "2032-01-01";
        case System::N64:
        case System::N64Mature: return "2022-06-12";
        default: return release_date;
    }
}

inline std::string BuildEntry(System s, const EntryArgs& a, const Paths& p) {
    using json::Obj;
    const std::string lcla6 = a.lcla6_date.empty() ? DefaultAddedDate(s, a.release_date) : a.lcla6_date;
    const std::string ja = "\xE3\x83\xBC";  // U+30FC
    const std::string lower_title = AsciiLower(a.title);
    Obj e;
    switch (s) {
        case System::Snes:  // order of snes_entry()
            e.Str("code", a.code).Str("rom", p.rom_db).Str("sort_title", lower_title).Str("details_screen", p.details_db)
                .Str("copyright", "added for your fun").Str("title", a.title).Int("armet_threshold", 0)
                .Str("lcla6_release_date", lcla6).Str("armet_version", "off").Str("sort_publisher", a.publisher)
                .Str("title_zhHant", ja).Int("save_count", 1).Int("volume", 100).Str("title_zhHans", ja)
                .Str("publisher", a.publisher).Str("title_ko", a.title).Raw("rewind_interval", "1.5")
                .Str("release_date", a.release_date).Int("players_count", a.players).Str("cover", p.cover_db)
                .Bool("simultaneous", a.simultaneous).Raw("autoplay", "[]");
            break;
        case System::Nes:  // order of nes_entry()
            e.Str("sort_title", lower_title).Str("publisher", a.publisher).Str("code", a.code).Str("rom", p.rom_db)
                .Str("copyright", "added for your fun").Str("title", a.title).Int("volume", 72).Raw("rewind_interval", "1.5")
                .Str("release_date", a.release_date).Int("players_count", a.players).Str("cover", p.cover_db)
                .Ints("overscan", {0, 0, 9, 3}).Str("armet_version", "v1").Str("lcla6_release_date", lcla6)
                .Int("save_count", 0).Bool("simultaneous", a.simultaneous).Ints("fadein", {3, 2})
                .Str("details_screen", p.details_db).Int("armet_threshold", 80).Str("sort_publisher", a.publisher)
                .Str("title_zhHant", ja).Str("title_zhHans", ja).Str("title_ko", "-");
            break;
        case System::N64:  // FIELD_ORDER of make_entry()
        case System::N64Mature:
            e.Str("code", a.code).Bool("cold_reset", false).Str("controller_position", "right")
                .Int("control_opt_layout_pattern", 1).Int("sram_file_size", 164096)
                .Str("copyright", "\xC2\xA9" + a.publisher).Str("cover", p.cover_db).Str("details_screen", p.details_db)
                .Bool("GPU384MHz", false).Str("lcla6_release_date", lcla6).Int("players_count", a.players)
                .Str("publisher", a.publisher).Str("release_date", a.release_date).Str("rom", p.rom_db)
                .Int("save_count", 1).Bool("simultaneous", false).Str("sort_publisher", AsciiLower(a.publisher))
                .Str("sort_title", lower_title).Str("title", a.title).Str("title_ko", a.title)
                .Str("title_zhHans", ja).Str("title_zhHant", ja).Int("volume", 80);
            break;
        default: {  // Genesis, GB, GBA, VB: ASCII-sorted keys (build_entry())
            e.Str("code", a.code).Str("copyright", "\xC2\xA9" + a.publisher).Str("cover", p.cover_db)
                .Str("details_screen", p.details_db).Str("lcla6_release_date", lcla6).Int("players_count", a.players)
                .Str("publisher", a.publisher).Str("release_date", a.release_date).Raw("rewind_interval", "1.5")
                .Int("save_count", 1).Str("sort_publisher", AsciiLower(a.publisher)).Str("sort_title", lower_title)
                .Str("title", a.title).Str("title_ko", a.title).Str("rom", p.rom_db);
            if (s == System::Genesis) {
                e.Str("machine_country", "us").Ints("overscan", {8, 8}).Bool("simultaneous", a.simultaneous)
                    .Str("title_zhHans", ja).Str("title_zhHant", ja).Int("volume", 66).Raw("z80c", "0.993");
            } else if (s == System::Gb) {
                e.Str("platform", a.cgb ? "CGB" : "DMG").Int("volume", 100).Str("CGB-Default", "None")
                    .Str("CGB-Nostalgic", "None");
                if (!a.cgb) e.Str("DMG_MGB-Default", "None").Str("DMG_MGB-Nostalgic", "High");
            } else if (s == System::Gba) {
                e.Ints("fadein", {3, 0}).Bool("simultaneous", a.simultaneous).Int("sram_file_size", 164096)
                    .Int("volume", 100).Str("title_zhHans", ja).Str("title_zhHant", ja);
            } else {  // Virtual Boy
                e.Int("volume", 100).Str("title_zhHans", ja).Str("title_zhHant", ja);
            }
            e.Sort();
        }
    }
    return e.Text();
}

// ---------------------------------------------------------------- code allocation
// Deterministic: the first candidate comes from the (normalized) ROM, later ones probe +1.
// `taken` = every code already in the db. The same ROM gets the same first candidate (idempotent).
struct IdSeed {
    u32 crc = 0;           // Crc32 of the bytes the app reads
    u32 gb_word = 0;       // Game Boy: checksum word stored at 0x14E
    std::string gba_code;  // GBA: 4 chars [0-9A-Z]
    bool cgb = false;
    bool pal = false;
};

inline std::string Fmt(const char* f, unsigned v) {
    char b[32];
    std::snprintf(b, sizeof b, f, v);
    return b;
}

inline std::string AllocateCode(System s, const IdSeed& id, const std::set<std::string>& taken, bool* first_candidate_taken = nullptr) {
    auto cand = [&](unsigned k) -> std::string {  // k-th candidate
        switch (s) {
            case System::Snes: return Fmt("S-%04u_e", 3000 + (id.crc % 7000 + k) % 7000);
            case System::Nes: {
                unsigned v = (id.crc % 17576 + k) % 17576;
                std::string c = "CLV-P-N";
                c += char('A' + v / 676); c += char('A' + v / 26 % 26); c += char('A' + v % 26);
                return c + "E";
            }
            case System::N64:
            case System::N64Mature: return Fmt("N-%04u", 1000 + (id.crc % 9000 + k) % 9000) + (id.pal ? "_p" : "_e");
            case System::Vb: return Fmt("V-%04u_e", 1000 + (id.crc % 9000 + k) % 9000);
            case System::Genesis: return Fmt("M-%04u_e", (id.crc % 10000 + k) % 10000);
            case System::Gb: return Fmt(id.cgb ? "C-%04X_e" : "D-%04X_e", (id.gb_word + k) & 0xFFFF);
            case System::Gba: {
                static const char* a = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
                std::string c = id.gba_code.size() == 4 ? id.gba_code : "0000";
                unsigned v = 0;
                for (char ch : c) v = v * 36 + unsigned(std::string_view(a).find(ch));
                v = (v + k) % (36u * 36 * 36 * 36);
                for (int i = 3; i >= 0; i--, v /= 36) c[size_t(i)] = a[v % 36];
                return "A-" + c + "_e";
            }
        }
        return {};
    };
    if (first_candidate_taken) *first_candidate_taken = taken.count(cand(0)) != 0;
    for (unsigned k = 0; k < 100000; k++) {
        std::string c = cand(k);
        if (!taken.count(c)) return c;
    }
    return {};
}

}  // namespace nso
