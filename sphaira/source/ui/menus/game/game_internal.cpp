#include "ui/menus/game/game_internal.hpp"

#include "app.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "image.hpp"
#include "ui/error_box.hpp"
#include "ui/nvg_util.hpp"
#include "ui/menus/game_list_info.hpp"
#include "ui/menus/grid_menu_base.hpp"
#include "yati/nx/ncm.hpp"
#include "yati/nx/ns.hpp"
#include "ui/menus/save/save_slot_backend.hpp"

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <ranges>

namespace sphaira::ui::menu::game {

using title::ContentInfoEntry;
using title::BuildContentEntry;
using grid::FormatBytes;

NspSource::NspSource(const std::vector<NspEntry>& entries) : m_entries{entries} {
    m_is_file_based_emummc = App::IsFileBaseEmummc();
}

Result NspSource::Read(const std::string& path, void* buf, s64 off, s64 size, u64* bytes_read) {
    const auto it = std::ranges::find_if(m_entries, [&path](auto& e){
        return path.find(e.path.s) != path.npos;
    });
    R_UNLESS(it != m_entries.end(), Result_GameBadReadForDump);

    const auto rc = it->Read(buf, off, size, bytes_read);
    if (m_is_file_based_emummc) {
        svcSleepThread(2e+6); // 2ms
    }
    return rc;
}

auto NspSource::GetName(const std::string& path) const -> std::string {
    const auto it = std::ranges::find_if(m_entries, [&path](auto& e){
        return path.find(e.path.s) != path.npos;
    });

    if (it != m_entries.end()) {
        return it->application_name;
    }

    return {};
}

auto NspSource::GetSize(const std::string& path) const -> s64 {
    const auto it = std::ranges::find_if(m_entries, [&path](auto& e){
        return path.find(e.path.s) != path.npos;
    });

    if (it != m_entries.end()) {
        return it->nsp_size;
    }

    return 0;
}

auto NspSource::GetIcon(const std::string& path) const -> int {
    const auto it = std::ranges::find_if(m_entries, [&path](auto& e){
        return path.find(e.path.s) != path.npos;
    });

    if (it != m_entries.end()) {
        return it->icon;
    }

    return App::GetDefaultImage();
}

Result Notify(Result rc, const std::string& error_message) {
    if (R_FAILED(rc)) {
        App::Push<ui::ErrorBox>(rc,
            i18n::get(error_message)
        );
    } else {
        App::Notify("Success"_i18n);
    }

    return rc;
}
Result GetMetaEntries(const Entry& e, title::MetaEntries& out, u32 flags) {
    return title::GetMetaEntries(e.app_id, out, flags);
}

bool LoadControlImage(Entry& e, title::ThreadResultData* result) {
    if (!e.image && result && !result->icon.empty()) {
        TimeStamp ts;
        const auto image = ImageLoadFromMemory(result->icon, ImageFlag_JPEG);
        if (!image.data.empty()) {
            e.image = nvgCreateImageRGBA(App::GetVg(), image.w, image.h, 0, image.data.data());
            log_write("\t[image load] time taken: %.2fs %zums\n", ts.GetSecondsD(), ts.GetMs());
            return true;
        }
    }

    return false;
}

void LoadResultIntoEntry(Entry& e, title::ThreadResultData* result) {
    if (result) {
        e.status = result->status;
        e.lang = result->lang;
        e.status = result->status;
    }
}

void LoadControlEntry(Entry& e, bool force_image_load) {
    if (e.status != title::NacpLoadStatus::Loaded) {
        LoadResultIntoEntry(e, title::Get(e.app_id));
    }

    if (force_image_load && e.status == title::NacpLoadStatus::Loaded) {
        LoadControlImage(e, title::Get(e.app_id));
    }
}

// the nsp itself is built by title::BuildNspEntries (shared with the MTP games
// drive); this only adds what the dump ui needs - the loaded name and icon.
Result BuildNspEntries(Entry& e, u32 flags, std::vector<NspEntry>& out) {
    LoadControlEntry(e);

    const auto first = out.size();
    R_TRY(title::BuildNspEntries(e.app_id, e.GetName(), flags, App::GetApp()->m_dump_app_folder.Get(), out));

    for (auto i = first; i < out.size(); i++) {
        out[i].icon = e.image;
    }

    R_SUCCEED();
}

void FreeEntry(NVGcontext* vg, Entry& e) {
    nvgDeleteImage(vg, e.image);
    e.image = 0;
}

void LaunchEntry(const Entry& e) {
    const auto rc = appletRequestLaunchApplication(e.app_id, nullptr);
    Notify(rc, "Failed to launch application");
}

Result CreateSave(u64 app_id, AccountUid uid) {
    save::SaveCreationRequest req{};
    save::SaveBackendStatus status = save::SaveBackendStatus::Success;
    R_TRY(save::PlanAccountSaveCreation(app_id, uid, nullptr, req, &status));

    const auto res = save::CreateSaveDataChecked(req);
    if (!res.verified) {
        return R_FAILED(res.rc) ? res.rc : FsError_PathNotFound;
    }

    R_SUCCEED();
}

auto ContentFlagFromMetaType(u8 meta_type) -> u32 {
    return title::ContentMetaTypeToContentFlag(meta_type);
}

// an empty /atmosphere/contents/<tid> is just a folder - LayeredFS only loads
// something when there is something in it, so the badge (and the details stat)
// distinguish "no folder", "empty folder" and "mods present".
void ProbeModsFolder(Entry& entry) {
    fs::FsNativeSd sd;
    const auto path = title::GetContentsPath(entry.app_id);
    entry.mods_folder = sd.DirExists(path);

    s64 count{};
    entry.layeredfs = entry.mods_folder &&
        R_SUCCEEDED(sd.DirGetEntryCount(path, &count, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles)) &&
        count > 0;
}

Result LoadGameSummary(Entry& entry) {
    if (entry.summary_attempted) {
        return entry.summary_result;
    }

    entry.summary_attempted = true;
    ProbeModsFolder(entry);

    title::MetaEntries entries;
    entry.summary_result = GetMetaEntries(entry, entries);
    if (R_FAILED(entry.summary_result)) {
        return entry.summary_result;
    }

    for (const auto& status : entries) {
        ContentInfoEntry info;
        if (const auto rc = BuildContentEntry(status, info); R_FAILED(rc)) {
            entry.summary_result = rc;
            continue;
        }

        entry.content_flags |= ContentFlagFromMetaType(status.meta_type);
        u64 size{};
        for (const auto& content : info.content_infos) {
            u64 content_size{};
            ncmContentInfoSizeToU64(&content, &content_size);
            size += content_size;
        }

        if (status.storageID == NcmStorageId_SdCard) {
            entry.sd_size += size;
        } else if (status.storageID == NcmStorageId_BuiltInUser) {
            entry.nand_size += size;
        } else if (status.storageID == NcmStorageId_GameCard) {
            entry.on_gamecard = true;
            entry.gc_size += size;
        }
    }

    return entry.summary_result;
}

auto StorageName(u8 storage_id) -> std::string {
    return i18n::get(ncm::GetReadableStorageIdStr(storage_id));
}

// whole sentences per direction rather than "<verb> to " + StorageName(): the
// inflected languages need the storage in a different case there, and gluing a
// nominative noun onto a preposition reads broken.
auto MovingToLabel(u8 target) -> std::string {
    return target == NcmStorageId_SdCard ? "Moving to SD"_i18n : "Moving to NAND"_i18n;
}

auto MovedToLabel(u8 target) -> std::string {
    return target == NcmStorageId_SdCard ? "Moved to SD"_i18n : "Moved to NAND"_i18n;
}

// ProgressBox turns on ApmCpuBoostMode_FastLoad for every transfer ("Boost CPU
// during transfer", on by default). FastLoad raises the cpu clock but drops the
// *gpu* to 76.8 MHz, roughly a sixth of normal. That is a fine trade for a
// cpu-bound install (ncz/nsz decompression) and a terrible one for a move: the
// work is ncm reading and writing, there is nothing to boost, while the games
// grid still rendering behind the box drops to a few frames a second. That is
// what "the progress bar does not move and the console feels hung" was - the
// transfer underneath had been running the whole time.
//
// Call once per progress box, at the top of its worker: the boost is ref
// counted, so this hands back only the reference that box took.
void DropBoostForMove() {
    App::SetBoostMode(false);
}

// a move is never obviously reversible and can shuffle several GB, so spell out
// what it touches first: which components change storage, which stay where they
// are, and what both storages look like afterwards.
auto BuildMoveSummary(const title::MovePlan& plan, NcmStorageId target) -> std::string {
    const auto target_name = StorageName(target);

    std::string msg;
    for (const auto& e : plan.move) {
        msg += i18n::get(ncm::GetReadableMetaTypeStr(e.status.meta_type)) + ": " +
            StorageName(e.status.storageID) + " > " + target_name +
            "  (" + FormatBytes(e.size) + ")\n";
    }
    for (const auto& e : plan.stay) {
        msg += i18n::get(ncm::GetReadableMetaTypeStr(e.status.meta_type)) + ": " +
            "stays on"_i18n + " " + StorageName(e.status.storageID) +
            "  (" + FormatBytes(e.size) + ")\n";
    }

    s64 nand_free{}, sd_free{};
    fs::GetStorageSpaces(&nand_free, nullptr, &sd_free, nullptr);
    const auto moved = static_cast<s64>(plan.move_size);
    const auto nand_after = nand_free + (target == NcmStorageId_SdCard ? moved : -moved);
    const auto sd_after = sd_free + (target == NcmStorageId_SdCard ? -moved : moved);

    msg += "\n" + "Moving"_i18n + " " + FormatBytes(plan.move_size) + "\n";
    msg += StorageName(NcmStorageId_BuiltInUser) + ": " + FormatBytes(nand_free) + " > " + FormatBytes(std::max<s64>(0, nand_after)) + " " + "free"_i18n + "\n";
    msg += StorageName(NcmStorageId_SdCard) + ": " + FormatBytes(sd_free) + " > " + FormatBytes(std::max<s64>(0, sd_after)) + " " + "free"_i18n;
    return msg;
}

auto CollectEntryBadgeLabels(const Entry& e, bool include_storage,
    std::array<const char*, kMaxGameBadges>& out) -> std::size_t
{
    return CollectGameBadgeLabels(
        e.sd_size != 0, e.nand_size != 0, e.on_gamecard,
        e.content_flags & title::ContentFlag_Application,
        e.content_flags & (title::ContentFlag_Patch | title::ContentFlag_DataPatch),
        e.content_flags & title::ContentFlag_AddOnContent,
        e.layeredfs,
        include_storage, out);
}

auto GameBadgeColour(const char* label) -> NVGcolor {
    if (!std::strcmp(label, "SD")) {
        return nvgRGBA(0, 128, 160, 255);
    }
    if (!std::strcmp(label, "NAND")) {
        return nvgRGBA(90, 100, 130, 255);
    }
    if (!std::strcmp(label, "GC")) {
        return nvgRGBA(40, 140, 230, 255);
    }
    if (!std::strcmp(label, "Base")) {
        return nvgRGBA(0, 78, 190, 255);
    }
    if (!std::strcmp(label, "DLC")) {
        return nvgRGBA(112, 35, 175, 255);
    }
    if (!std::strcmp(label, "Update")) {
        return nvgRGBA(190, 76, 0, 255);
    }
    if (!std::strcmp(label, "LayeredFS")) {
        return nvgRGBA(0, 112, 58, 255);
    }
    return nvgRGBA(180, 24, 24, 255);
}

void DrawGameCardOutline(NVGcontext* vg, const Vec4& v) {
    nvgBeginPath(vg);
    nvgStrokeWidth(vg, 3.f);
    nvgStrokeColor(vg, nvgRGBA(40, 140, 230, 255));
    nvgRoundedRect(vg, v.x, v.y, v.w, v.h, 5.f);
    nvgStroke(vg);
}

void DrawGameBadges(NVGcontext* vg, Theme*, const Vec4& image, const Entry& entry) {
    std::array<const char*, kMaxGameBadges> labels{};
    const auto count = CollectEntryBadgeLabels(entry, false, labels);
    if (!count) {
        return;
    }

    const bool compact = image.w < 80.f;
    const float font = compact ? 8.f : (image.w < 130.f ? 11.f : 13.f);
    const float height = font + (compact ? 5.f : 7.f);
    const float margin = compact ? 2.f : 5.f;
    const float gap = compact ? 1.f : 3.f;
    float bounds[4]{};
    float width = compact ? 20.f : 26.f;
    nvgFontSize(vg, font);
    gfx::textBounds(vg, 0, 0, bounds, "Base");
    width = std::max(width, bounds[2] - bounds[0] + (compact ? 6.f : 12.f));
    for (size_t i = 0; i < count; i++) {
        gfx::textBounds(vg, 0, 0, bounds, labels[i]);
        width = std::max(width, bounds[2] - bounds[0] + (compact ? 6.f : 12.f));
    }
    width = std::min(width, image.w - margin * 2.f);

    const float x = image.x + margin;
    float y = image.y + margin;
    for (size_t i = 0; i < count; i++) {
        gfx::drawRect(vg, x - 1.f, y - 1.f, width + 2.f, height + 2.f, nvgRGBA(0, 0, 0, 255), 5.f);
        gfx::drawRect(vg, x, y, width, height, GameBadgeColour(labels[i]), 4.f);
        gfx::drawText(vg, x + width * 0.5f, y + height * 0.5f, font,
            nvgRGBA(255, 255, 255, 255), labels[i], NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        y += height + gap;
    }
}

constexpr float LIST_BADGE_FONT = 12.f;
constexpr float LIST_BADGE_PAD_X = 7.f;
constexpr float LIST_BADGE_H = 20.f;
constexpr float LIST_BADGE_GAP = 4.f;

auto MeasureListBadges(NVGcontext* vg, const Entry& e) -> float {
    std::array<const char*, kMaxGameBadges> labels{};
    const auto count = CollectEntryBadgeLabels(e, true, labels);
    nvgFontSize(vg, LIST_BADGE_FONT);
    float bounds[4]{};
    float total = 0.f;
    for (size_t i = 0; i < count; i++) {
        gfx::textBounds(vg, 0, 0, bounds, labels[i]);
        total += bounds[2] - bounds[0] + LIST_BADGE_PAD_X * 2.f;
        if (i) {
            total += LIST_BADGE_GAP;
        }
    }
    return total;
}

void DrawListBadges(NVGcontext* vg, const Vec4& row, const Entry& e, bool has_size) {
    std::array<const char*, kMaxGameBadges> labels{};
    const auto count = CollectEntryBadgeLabels(e, true, labels);
    if (!count) {
        return;
    }

    nvgFontSize(vg, LIST_BADGE_FONT);
    float widths[kMaxGameBadges]{};
    float bounds[4]{};
    float total = 0.f;
    for (size_t i = 0; i < count; i++) {
        gfx::textBounds(vg, 0, 0, bounds, labels[i]);
        widths[i] = bounds[2] - bounds[0] + LIST_BADGE_PAD_X * 2.f;
        total += widths[i];
        if (i) {
            total += LIST_BADGE_GAP;
        }
    }

    float x_right = row.x + row.w - 15.f;
    if (has_size) {
        nvgFontSize(vg, 18.f);
        gfx::textBounds(vg, 0, 0, bounds, grid::LIST_INFO_VALUE_SAMPLE);
        x_right -= (bounds[2] - bounds[0]) + grid::LIST_INFO_COL_GAP;
        nvgFontSize(vg, LIST_BADGE_FONT);
    }

    const float y = row.y + (row.h - LIST_BADGE_H) / 2.f;
    float x = x_right - total;
    for (size_t i = 0; i < count; i++) {
        gfx::drawRect(vg, x - 1.f, y - 1.f, widths[i] + 2.f, LIST_BADGE_H + 2.f, nvgRGBA(0, 0, 0, 255), 4.f);
        gfx::drawRect(vg, x, y, widths[i], LIST_BADGE_H, GameBadgeColour(labels[i]), 3.f);
        gfx::drawText(vg, x + widths[i] * 0.5f, y + LIST_BADGE_H * 0.5f, LIST_BADGE_FONT,
            nvgRGBA(255, 255, 255, 255), labels[i], NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        x += widths[i] + LIST_BADGE_GAP;
    }
}

auto FormatPlaytime(u64 nanoseconds) -> std::string {
    if (!nanoseconds) {
        return "-";
    }

    const auto minutes = nanoseconds / 60000000000ULL;
    if (minutes < 60) {
        return std::to_string(minutes) + " " + "min"_i18n;
    }
    return std::to_string(minutes / 60) + " " + "h"_i18n + " " + std::to_string(minutes % 60) + " " + "min"_i18n;
}

auto FormatLastPlayed(u64 posix_seconds) -> std::string {
    if (!posix_seconds) {
        return "-";
    }

    const auto t = (time_t)posix_seconds;
    struct tm tm{};
    localtime_r(&t, &tm);

    char out[24];
    std::snprintf(out, sizeof(out), "%02u/%02u/%u", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
    return out;
}

// Removes a title's installed content and its application record while always
// preserving user save data. Unlike nsDeleteApplicationCompletely (which also
// wipes saves and fails outright when any part of the title lives on an inserted
// gamecard), this deletes only the installed (NAND/SD) content and drops the
// record so the title leaves the games list. The gamecard and saves are never
// touched.
Result DeleteApplicationKeepSave(u64 app_id) {
    title::MetaEntries entries;
    // best-effort: an empty/failed listing just means there is no installed
    // content to remove (e.g. a leftover gamecard record), we still drop the record.
    title::GetMetaEntries(app_id, entries);

    const bool on_gamecard = std::ranges::any_of(entries, [](const auto& s){
        return s.storageID == NcmStorageId_GameCard;
    });
    const bool has_installed = std::ranges::any_of(entries, [](const auto& s){
        return s.storageID == NcmStorageId_SdCard || s.storageID == NcmStorageId_BuiltInUser;
    });

    // delete installed content (base/update/DLC on NAND/SD); saves stay intact.
    const auto rc = nsDeleteApplicationEntity(app_id);
    // surface a genuine failure only for fully-installed titles. When any content
    // lives on the inserted gamecard it can't be removed and ns reports an error
    // even though the installed portion may be gone — tolerate it and still drop
    // the record.
    if (R_FAILED(rc) && has_installed && !on_gamecard) {
        return rc;
    }

    // drop the application record so the title leaves the games list.
    Service srv{}, *srv_ptr = &srv;
    if (hosversionAtLeast(3,0,0)) {
        R_TRY(nsGetApplicationManagerInterface(&srv));
    } else {
        srv_ptr = nsGetServiceSession_ApplicationManagerInterface();
    }
    ON_SCOPE_EXIT(serviceClose(&srv));

    return ns::DeleteApplicationRecord(srv_ptr, app_id);
}

} // namespace sphaira::ui::menu::game