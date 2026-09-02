#include "account/account_playtime.hpp"

#include "app_paths.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "log.hpp"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

namespace sphaira::account_playtime {
namespace {

static_assert(sizeof(PdmPlayEvent) == 0x38, "PdmPlayEvent must match PlayEvent.dat records");

constexpr u64 PDM_SAVE_ID = 0x80000000000000F0ULL;
constexpr s32 kBatch = 512;
constexpr u32 kMaxEvents = 1'500'000;

auto EncodeUid(const AccountUid& uid, u32 out[4]) -> void {
    out[0] = static_cast<u32>(uid.uid[0] >> 32);
    out[1] = static_cast<u32>(uid.uid[0]);
    out[2] = static_cast<u32>(uid.uid[1] >> 32);
    out[3] = static_cast<u32>(uid.uid[1]);
}

auto AccountUidMatches(const PdmPlayEvent& e, const u32 want[4]) -> bool {
    return std::memcmp(e.event_data.account.uid, want, sizeof(u32) * 4) == 0;
}

auto ParseBlob(const std::vector<u8>& data, std::vector<PdmPlayEvent>& out) -> bool {
    out.clear();
    if (data.size() < 8) {
        return false;
    }
    u32 count = 0;
    std::memcpy(&count, data.data() + 4, sizeof(count));
    const u64 need = 8ull + static_cast<u64>(count) * sizeof(PdmPlayEvent);
    // Header is u32 unk + u32 count (GBAtemp / on-disk PlayEvent.dat). count=0 is a
    // valid empty log; do not fall through to a raw 0x38-stride parse of padding.
    if (count <= kMaxEvents && need <= data.size()) {
        if (count > 0) {
            out.resize(count);
            std::memcpy(out.data(), data.data() + 8, static_cast<size_t>(count) * sizeof(PdmPlayEvent));
        }
        return true;
    }
    if (data.size() % sizeof(PdmPlayEvent) == 0) {
        const auto n = data.size() / sizeof(PdmPlayEvent);
        if (n > kMaxEvents) {
            return false;
        }
        out.resize(n);
        std::memcpy(out.data(), data.data(), data.size());
        return true;
    }
    return false;
}

auto SerializeBlob(const std::vector<PdmPlayEvent>& events, size_t min_size) -> std::vector<u8> {
    const u32 unk = 0;
    const u32 count = static_cast<u32>(events.size());
    const size_t body = 8 + events.size() * sizeof(PdmPlayEvent);
    std::vector<u8> out(std::max(body, min_size), 0);
    std::memcpy(out.data(), &unk, sizeof(unk));
    std::memcpy(out.data() + 4, &count, sizeof(count));
    if (!events.empty()) {
        std::memcpy(out.data() + 8, events.data(), events.size() * sizeof(PdmPlayEvent));
    }
    return out;
}

auto FilterOne(const PdmPlayEvent& e, const u32 want[4], bool& active, std::vector<PdmPlayEvent>& out) -> void {
    switch (e.play_event_type) {
    case PdmPlayEventType_Account:
        if (AccountUidMatches(e, want)) {
            out.push_back(e);
            if (e.event_data.account.type == 0) {
                active = true;
            } else if (e.event_data.account.type == 1) {
                active = false;
            }
        } else if (e.event_data.account.type == 0) {
            active = false;
        }
        break;
    case PdmPlayEventType_Applet:
    case PdmPlayEventType_PowerStateChange:
    case PdmPlayEventType_OperationModeChange:
        if (active) {
            out.push_back(e);
        }
        break;
    default:
        break;
    }
}

auto CollectFromPdmqry(const u32 want[4], std::vector<PdmPlayEvent>& out) -> Result {
    auto buf = std::make_unique<PdmPlayEvent[]>(kBatch);
    s32 total_entries = 0;
    s32 start = 0;
    s32 end = 0;
    const bool have_range = R_SUCCEEDED(pdmqryGetAvailablePlayEventRange(&total_entries, &start, &end));
    s32 index = have_range ? start : 0;
    s32 scanned = 0;
    bool active = false;

    for (;;) {
        s32 total_out = 0;
        const auto rc = pdmqryQueryPlayEvent(index, buf.get(), kBatch, &total_out);
        if (R_FAILED(rc) || total_out <= 0) {
            break;
        }
        for (s32 i = 0; i < total_out; i++) {
            FilterOne(buf[i], want, active, out);
        }
        index += total_out;
        scanned += total_out;
        if (have_range && scanned >= total_entries) {
            break;
        }
        if (!have_range && total_out < kBatch) {
            break;
        }
        if (scanned > static_cast<s32>(kMaxEvents)) {
            break;
        }
    }
    log_write("[PLAY] pdmqry scanned=%d kept=%zu range=%d start=%d end=%d total=%d\n",
        scanned, out.size(), have_range ? 1 : 0, start, end, total_entries);
    R_SUCCEED();
}

auto OpenPdmSave(bool read_only) -> fs::FsNativeSave {
    FsSaveDataAttribute attr{};
    attr.system_save_data_id = PDM_SAVE_ID;
    attr.save_data_type = FsSaveDataType_System;
    return fs::FsNativeSave(FsSaveDataType_System, FsSaveDataSpaceId_System, &attr, read_only);
}

auto FindPlayEventPath(fs::Fs& save) -> std::string {
    static const char* kCands[] = {
        "/PlayEvent.dat",
        "PlayEvent.dat",
        "/pdm/PlayEvent.dat",
    };
    for (const auto* c : kCands) {
        if (save.FileExists(c)) {
            return c;
        }
    }
    fs::Dir d;
    auto rc = save.OpenDirectory("/", FsDirOpenMode_ReadFiles | FsDirOpenMode_ReadDirs, &d);
    if (R_FAILED(rc)) {
        rc = save.OpenDirectory("", FsDirOpenMode_ReadFiles | FsDirOpenMode_ReadDirs, &d);
    }
    if (R_SUCCEEDED(rc)) {
        std::vector<FsDirectoryEntry> ents;
        if (R_SUCCEEDED(d.ReadAll(ents))) {
            for (const auto& e : ents) {
                if (e.type == FsDirEntryType_File && std::strcmp(e.name, "PlayEvent.dat") == 0) {
                    return std::string("/") + e.name;
                }
                log_write("[PLAY] 00F0 entry %s type=%d\n", e.name, e.type);
            }
        }
    }
    return "/PlayEvent.dat";
}

auto CollectFromSave(const u32 want[4], std::vector<PdmPlayEvent>& out) -> Result {
    auto save = OpenPdmSave(true);
    const auto open_rc = save.GetFsOpenResult();
    if (R_FAILED(open_rc)) {
        log_write("[PLAY] 00F0 read-only open 0x%X\n", open_rc);
        return open_rc;
    }
    const auto path = FindPlayEventPath(save);
    std::vector<u8> data;
    R_TRY(save.read_entire_file(path.c_str(), data));
    std::vector<PdmPlayEvent> all;
    R_UNLESS(ParseBlob(data, all), Result_FsInvalidType);
    bool active = false;
    for (const auto& e : all) {
        FilterOne(e, want, active, out);
    }
    log_write("[PLAY] 00F0 file scanned=%zu kept=%zu path=%s\n", all.size(), out.size(), path.c_str());
    R_SUCCEED();
}

auto RemapTo(std::vector<PdmPlayEvent>& events, const AccountUid& to) -> void {
    u32 encoded[4]{};
    EncodeUid(to, encoded);
    for (auto& e : events) {
        if (e.play_event_type == PdmPlayEventType_Account) {
            std::memcpy(e.event_data.account.uid, encoded, sizeof(encoded));
        }
    }
}

auto InstallPlaytimeTe(fs::FsNativeSd& sd) -> void {
    std::vector<u8> te;
    if (R_FAILED(romfsInit())) {
        log_write("[PLAY] romfsInit failed for playtime_restore.te\n");
        return;
    }
    {
        ON_SCOPE_EXIT(romfsExit());
        if (R_FAILED(fs::read_entire_file("romfs:/tegra/playtime_restore.te", te)) || te.empty()) {
            log_write("[PLAY] playtime_restore.te missing from romfs\n");
            return;
        }
    }
    sd.CreateDirectoryRecursively("/config/kefir/playtime_pending");
    sd.CreateDirectoryRecursively("/config/kefir/nand_transfer");
    sd.CreateDirectoryRecursively("/TegraExplorer/scripts");
    sd.write_entire_file("/config/kefir/playtime_pending/playtime_restore.te", te);
    sd.write_entire_file("/config/kefir/nand_transfer/playtime_restore.te", te);
    sd.write_entire_file("/TegraExplorer/scripts/playtime_restore.te", te);
}

} // namespace

auto CollectUserPlayEvents(const AccountUid& uid, std::vector<PdmPlayEvent>& out) -> Result {
    out.clear();
    u32 want[4]{};
    EncodeUid(uid, want);

    const bool pdm_ok = R_SUCCEEDED(pdmqryInitialize());
    ON_SCOPE_EXIT(if (pdm_ok) { pdmqryExit(); });
    if (pdm_ok) {
        R_TRY(CollectFromPdmqry(want, out));
        R_SUCCEED();
    }
    log_write("[PLAY] pdmqryInitialize failed, trying 00F0 file\n");
    R_TRY(CollectFromSave(want, out));
    R_SUCCEED();
}

auto WritePackPlayEvents(const std::string& dir, const std::vector<PdmPlayEvent>& events) -> Result {
    R_UNLESS(!dir.empty(), Result_FsInvalidType);
    if (events.empty()) {
        R_SUCCEED();
    }
    fs::FsNativeSd sd;
    const auto pdm_dir = dir + "/pdm";
    R_TRY(sd.CreateDirectoryRecursively(pdm_dir.c_str()));
    const auto blob = SerializeBlob(events, 0);
    R_TRY(sd.write_entire_file((pdm_dir + "/PlayEvent.dat").c_str(), blob));
    log_write("[PLAY] wrote %zu events to %s/pdm/PlayEvent.dat\n", events.size(), dir.c_str());
    R_SUCCEED();
}

auto LoadPackPlayEvents(const std::string& dir, std::vector<PdmPlayEvent>& out) -> Result {
    out.clear();
    fs::FsNativeSd sd;
    const auto path = dir + "/pdm/PlayEvent.dat";
    if (!sd.FileExists(path.c_str())) {
        return Result_FsInvalidType;
    }
    std::vector<u8> data;
    R_TRY(sd.read_entire_file(path.c_str(), data));
    R_UNLESS(ParseBlob(data, out), Result_FsInvalidType);
    R_SUCCEED();
}

auto PackHasPlayEvents(const std::string& dir) -> bool {
    fs::FsNativeSd sd;
    return sd.FileExists((dir + "/pdm/PlayEvent.dat").c_str());
}

auto PreparePlayHours(const std::vector<PlaySlice>& slices) -> Result {
    std::vector<PdmPlayEvent> incoming;
    for (const auto& s : slices) {
        if (s.events.empty()) {
            continue;
        }
        auto part = s.events;
        RemapTo(part, s.uid);
        incoming.insert(incoming.end(), part.begin(), part.end());
    }
    R_UNLESS(!incoming.empty(), Result_FsEmpty);

    // Read-only. Writable open of 00F0 under Horizon User-Breaks ns (2011-0301).
    auto save = OpenPdmSave(true);
    const auto open_rc = save.GetFsOpenResult();
    if (R_FAILED(open_rc)) {
        log_write("[PLAY] 00F0 read-only open 0x%X (no writable fallback)\n", open_rc);
        return open_rc;
    }

    const auto path = FindPlayEventPath(save);
    std::vector<u8> original;
    if (save.FileExists(path.c_str())) {
        R_TRY(save.read_entire_file(path.c_str(), original));
    }

    std::vector<PdmPlayEvent> dest;
    if (!original.empty()) {
        R_UNLESS(ParseBlob(original, dest), Result_FsInvalidType);
    }
    const auto dest_before = dest.size();
    if (dest.size() + incoming.size() > kMaxEvents) {
        log_write("[PLAY] merge would exceed cap dest=%zu in=%zu\n", dest.size(), incoming.size());
        return Result_FsInvalidType;
    }
    dest.insert(dest.end(), incoming.begin(), incoming.end());
    const auto blob = SerializeBlob(dest, original.size());

    fs::FsNativeSd sd;
    R_TRY(sd.CreateDirectoryRecursively("/config/kefir/playtime_pending"));
    R_TRY(sd.write_entire_file("/config/kefir/playtime_pending/PlayEvent.dat", blob));
    InstallPlaytimeTe(sd);
    log_write("[PLAY] pending merge dest=%zu + in=%zu -> %zu (TE playtime_restore.te)\n",
        dest_before, incoming.size(), dest.size());
    R_SUCCEED();
}

} // namespace sphaira::account_playtime
