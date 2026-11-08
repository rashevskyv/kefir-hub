// What the UI calls. Pure: no file access, no zlib. The libnx layer reads/writes the files and compresses.
//
//   1. PlanTarget(name, head, size, installed)  -> which NSO app (or a list to choose from, or an error)
//   2. BuildAdd(request)                         -> files to write + the new db text (+ marker text)
//   3. BuildRemove(db, marker, code)             -> new db + marker + directory to delete
#pragma once
#include "common.hpp"
#include "entry.hpp"
#include "n64.hpp"
#include "rom.hpp"
#include "systems.hpp"
#include "titlesdb.hpp"

#include <string_view>

namespace nso {

// ---------------------------------------------------------------- 1. target
struct TargetPlan {
    enum class Status { Chosen, NeedChoice, Error } status = Status::Error;
    System chosen = System::Nes;       // valid when Chosen
    std::vector<System> candidates;    // installed apps to pick from (NeedChoice; also set when Chosen)
    std::vector<System> detected;      // what the file looks like, before the installed filter
    std::string error;
};

inline TargetPlan PlanTarget(std::string_view file_name, ByteSpan head, u64 size, const Installed& installed) {
    TargetPlan t;
    t.detected = DetectSystem(head, size, ExtOf(file_name));
    for (System s : t.detected) if (installed[size_t(s)]) t.candidates.push_back(s);
    if (t.detected.empty()) {
        t.error = "This ROM type is not supported";
    } else if (t.candidates.empty()) {
        t.error = "The Nintendo Switch Online app for this ROM is not installed";
    } else if (t.candidates.size() == 1) {
        t.status = TargetPlan::Status::Chosen;
        t.chosen = t.candidates[0];
    } else {
        t.status = TargetPlan::Status::NeedChoice;
    }
    return t;
}

// ---------------------------------------------------------------- marker file (our own entries)
// <romfs>/titles/.kefir_added : one code per line (text after a tab or space is ignored).
inline std::vector<std::string> MarkerCodes(std::string_view m) {
    std::vector<std::string> v;
    while (!m.empty()) {
        const size_t nl = m.find('\n');
        std::string_view line = m.substr(0, nl);
        m = nl == std::string_view::npos ? std::string_view{} : m.substr(nl + 1);
        const size_t e = line.find_first_of("\t \r");
        line = line.substr(0, e);
        if (!line.empty()) v.emplace_back(line);
    }
    return v;
}
inline bool MarkerHas(std::string_view m, std::string_view code) {
    for (auto& c : MarkerCodes(m)) if (c == code) return true;
    return false;
}
// `note` (BuildAdd writes the ROM crc32 as 8 hex digits) is stored after a tab.
inline std::string MarkerAdd(std::string_view m, std::string_view code, std::string_view note = {}) {
    std::string o(m);
    if (!o.empty() && o.back() != '\n') o += '\n';
    if (!MarkerHas(m, code)) {
        o += std::string(code);
        if (!note.empty()) o += "\t" + std::string(note);
        o += "\n";
    }
    return o;
}
inline std::string MarkerNote(std::string_view m, std::string_view code) {
    while (!m.empty()) {
        const size_t nl = m.find('\n');
        std::string_view line = m.substr(0, nl);
        m = nl == std::string_view::npos ? std::string_view{} : m.substr(nl + 1);
        while (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const size_t t = line.find('\t');
        if (t != std::string_view::npos && line.substr(0, t) == code) return std::string(line.substr(t + 1));
    }
    return {};
}
inline std::string MarkerRemove(std::string_view m, std::string_view code) {
    std::string o;
    while (!m.empty()) {
        const size_t nl = m.find('\n');
        std::string_view line = m.substr(0, nl);
        m = nl == std::string_view::npos ? std::string_view{} : m.substr(nl + 1);
        const std::string_view c = line.substr(0, line.find_first_of("\t \r"));
        if (!c.empty() && c != code) { o.append(line); o += '\n'; }
    }
    return o;
}

// "Super Game (USA) [!].sfc" -> "Super Game"
inline std::string TitleFromFileName(std::string_view name) {
    const size_t s = name.find_last_of("/\\");
    if (s != std::string_view::npos) name.remove_prefix(s + 1);
    const size_t d = name.rfind('.');
    if (d != std::string_view::npos && d > 0) name = name.substr(0, d);
    std::string t(name);
    for (;;) {  // strip trailing (..) and [..] tags
        while (!t.empty() && (t.back() == ' ' || t.back() == '_')) t.pop_back();
        if (t.empty() || (t.back() != ')' && t.back() != ']')) break;
        const size_t o = t.rfind(t.back() == ')' ? '(' : '[');
        if (o == std::string::npos || o == 0) break;
        t.erase(o);
    }
    for (auto& c : t) if (c == '_') c = ' ';
    return t.empty() ? std::string(name) : t;
}

// ---------------------------------------------------------------- 2. add
struct OutFile {
    enum class Kind { Bytes, Source, DonorSfromsig } kind = Kind::Bytes;
    std::string path;      // relative to the overlay romfs dir, e.g. "titles/S-3123_e/3123_e.sfrom"
    Bytes data;            // Bytes: content
                           // Source: empty, the content is the source ROM file unchanged
                           // DonorSfromsig: empty, the content is a 256-byte .sfromsig the caller copies from the
                           //   installed game's RomFS (any official one; NOT verified against the ROM in the HIDE mod: UNPROVEN on stock)
    bool compress = false; // true: zlib-compress (level 9) the content before writing (.bnz, .dtz)
};

struct CoverSpec {
    std::string path;      // relative to romfs
    CoverKind kind = CoverKind::Png;  // Png: plain 8-bit RGB/RGBA PNG. XtxZ: EncodeXtx(rgba) then zlib level 9
    bool is_details = false;
    int w = 512, h = 512;  // cover: scale to fit inside w x h, keep aspect (longer side = 512).
                           // details: exactly w x h (letterbox the source). The caller supplies a placeholder if the user has none.
};

struct AddRequest {
    System system = System::Nes;
    ByteSpan rom;                  // the whole ROM file
    std::string file_name;         // used for the default title
    std::string title;             // "" -> TitleFromFileName
    std::string publisher = "Unknown";
    std::string release_date = "2000-01-01";
    int players = 1;
    bool simultaneous = false;
    std::string db_text;           // overlay db if it exists, else the installed app's official db text
    std::string marker_text;       // current .kefir_added ("" when absent)
    bool n64_raw_bin = false;      // N64: write <code>.bin (raw z64) instead of the safe .bnz (zlib); .bin for a NEW code is UNPROVEN
    u32 mtime = 0;                 // N64 .dtz file time (unix seconds)
    std::span<const N64Config> n64_table = std::span<const N64Config>(kN64Configs);
};

struct AddResult {
    bool ok = false;
    std::string error;
    std::string code;
    std::string overlay_romfs;     // "/atmosphere/contents/<tid>/romfs"
    std::vector<OutFile> files;    // ROM (+ sig, dtz)
    std::vector<CoverSpec> covers; // cover + details screen (always write both)
    std::string db_text;           // the NEW db (replace <romfs>/titles/lclassics.titlesdb, atomically: temp + rename)
    std::string marker_text;       // the NEW marker (<romfs>/titles/.kefir_added)
    std::string entry_json;
    bool experimental = false;     // UI must warn: "may not work, please send feedback"
    std::string experimental_reason;
};

inline AddResult BuildAdd(const AddRequest& r) {
    AddResult out;
    auto fail = [&](std::string e) { out.error = std::move(e); return out; };
    const System s = r.system;

    const titlesdb::Db db = titlesdb::Scan(r.db_text);
    if (!db.ok) return fail("The titles database is not valid JSON");
    std::set<std::string> taken;
    for (auto& e : db.entries) taken.insert(e.code);

    const Normalized n = Normalize(s, r.rom);
    if (!n.ok) return fail(n.error);
    const ByteSpan view = View(n, r.rom);

    IdSeed seed;
    seed.crc = Crc32(view);
    seed.pal = n.n64_pal;
    if (s == System::Gb) { seed.cgb = GbIsCgb(r.rom); seed.gb_word = GbHeaderWord(r.rom); }
    if (s == System::Gba) seed.gba_code = GbaGameCode(r.rom);
    bool first_taken = false;
    out.code = AllocateCode(s, seed, taken, &first_taken);
    if (out.code.empty()) return fail("No free code");
    const std::string crc_hex = Fmt("%08x", seed.crc);
    for (const auto& c : MarkerCodes(r.marker_text)) {
        if (MarkerNote(r.marker_text, c) == crc_hex) return fail("This ROM was already added");
    }

    const Paths p = MakePaths(s, out.code, n.ext, r.n64_raw_bin);
    EntryArgs a;
    a.code = out.code;
    a.title = r.title.empty() ? TitleFromFileName(r.file_name) : r.title;
    a.publisher = r.publisher;
    a.release_date = r.release_date;
    a.players = r.players < 1 ? 1 : (r.players > 4 ? 4 : r.players);
    a.simultaneous = r.simultaneous;
    a.cgb = seed.cgb;
    out.entry_json = BuildEntry(s, a, p);

    out.db_text = r.db_text;
    if (!titlesdb::Add(out.db_text, out.code, out.entry_json)) return fail("Cannot edit the titles database");
    out.marker_text = MarkerAdd(r.marker_text, out.code, crc_hex);
    out.overlay_romfs = OverlayRomfs(s);

    OutFile rom;
    rom.path = p.dir + "/" + p.rom_file;
    rom.kind = n.passthrough ? OutFile::Kind::Source : OutFile::Kind::Bytes;
    if (!n.passthrough) rom.data = n.bytes;
    rom.compress = (s == System::N64 || s == System::N64Mature) && !r.n64_raw_bin;
    out.files.push_back(std::move(rom));

    if (s == System::Snes) {
        OutFile sig;
        sig.kind = OutFile::Kind::DonorSfromsig;
        sig.path = p.dir + "/" + p.sig_file;
        out.files.push_back(std::move(sig));
    }
    if (s == System::N64 || s == System::N64Mature) {
        const N64Config* cfg = FindN64Config(r.n64_table, n.n64_crc1, n.n64_crc2);
        OutFile dtz;
        dtz.path = p.dir + "/" + p.dtz_file;
        dtz.data = BuildDtzCpio(n.n64_pal, cfg, r.mtime);
        dtz.compress = true;
        out.files.push_back(std::move(dtz));
        if (!cfg) {
            out.experimental = true;
            out.experimental_reason = "No tested emulator config for this N64 game: it may not start. Please send feedback.";
        }
    }

    const SystemInfo& si = Info(s);
    out.covers.push_back({p.dir + "/" + p.cover_file, si.cover, false, 512, 512});
    out.covers.push_back({p.dir + "/" + p.details_file, si.cover, true, si.details_w, si.details_h});
    out.ok = true;
    return out;
}

// ---------------------------------------------------------------- 3. remove
struct RemoveResult {
    bool ok = false;
    std::string error;
    std::string db_text;         // new db (entry removed)
    std::string marker_text;     // new marker
    std::string delete_dir;      // relative to romfs: "titles/<code>" (delete recursively)
    bool marker_empty = false;   // nothing of ours left: the caller may also delete the marker (the db stays as is)
};

// Only codes that we wrote (listed in the marker) can be removed; official entries never are.
inline RemoveResult BuildRemove(std::string_view db_text, std::string_view marker_text, std::string_view code) {
    RemoveResult r;
    if (code.empty() || code.size() > 32) { r.error = "Bad code"; return r; }
    for (char c : code) {
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-' || c == '_')) {
            r.error = "Bad code"; return r;
        }
    }
    if (!MarkerHas(marker_text, code)) { r.error = "This game was not added by the Hub"; return r; }
    r.db_text = std::string(db_text);
    if (!titlesdb::Scan(r.db_text).ok) { r.error = "The titles database is not valid JSON"; return r; }
    while (titlesdb::Remove(r.db_text, code)) {}  // absent from the db is fine: clean up the leftovers
    r.marker_text = MarkerRemove(marker_text, code);
    r.marker_empty = MarkerCodes(r.marker_text).empty();
    r.delete_dir = "titles/" + std::string(code);
    r.ok = true;
    return r;
}

// Codes that we added and that are still in the db.
inline std::vector<std::string> ListAdded(std::string_view db_text, std::string_view marker_text) {
    std::vector<std::string> v;
    const titlesdb::Db db = titlesdb::Scan(db_text);
    for (auto& c : MarkerCodes(marker_text)) if (db.ok && titlesdb::Has(db, c)) v.push_back(c);
    return v;
}

}  // namespace nso
