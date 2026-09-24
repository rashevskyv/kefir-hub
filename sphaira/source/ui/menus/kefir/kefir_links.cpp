#include "ui/menus/kefir/kefir_internal.hpp"
#include "ui/menus/kefir/kefir_firmware.hpp"
#include "i18n.hpp"
#include "utils/utils.hpp"
#include <yyjson.h>

#include <cmath>
#include <cstring>
#include <string_view>
#include <utility>

namespace sphaira::ui::menu::kefir {

auto EntryDescription(const UpdaterEntry& entry) -> const char* {
    if (entry.type == UpdaterEntryType::Network) {
        return "Open GitHub releases and direct links.";
    }
    if (entry.type == UpdaterEntryType::CustomLink) {
        return "Enter a ZIP URL and extract it to the SD card.";
    }
    if (entry.type == UpdaterEntryType::FirmwareManual) {
        return "Install a firmware already saved on the SD card.";
    }
    return entry.url.c_str();
}

auto EntryIsFolder(const UpdaterEntry& entry) -> bool {
    return entry.type == UpdaterEntryType::Network ||
        entry.type == UpdaterEntryType::FirmwareManual;
}

auto EntryIsDownload(const UpdaterEntry& entry) -> bool {
    return entry.type == UpdaterEntryType::Kefir ||
        entry.type == UpdaterEntryType::Firmware ||
        entry.type == UpdaterEntryType::CustomLink;
}

void DrawUpdaterEntryIcon(NVGcontext* vg, Theme* theme, const UpdaterEntry& entry, float x, float y, bool selected, bool disabled) {
    if (!EntryIsFolder(entry) && !EntryIsDownload(entry)) {
        return;
    }

    const auto colour = theme->GetColour(disabled ? ThemeEntryID_TEXT_INFO : (selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT_INFO));

    nvgSave(vg);
    nvgStrokeColor(vg, colour);
    nvgStrokeWidth(vg, 2.f);

    if (EntryIsFolder(entry)) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y + 3.f, 28.f, 19.f, 3.f);
        nvgRect(vg, x + 3.f, y, 11.f, 5.f);
        nvgStroke(vg);
    } else {
        nvgBeginPath(vg);
        nvgMoveTo(vg, x + 14.f, y);
        nvgLineTo(vg, x + 14.f, y + 17.f);
        nvgMoveTo(vg, x + 7.f, y + 10.f);
        nvgLineTo(vg, x + 14.f, y + 17.f);
        nvgLineTo(vg, x + 21.f, y + 10.f);
        nvgMoveTo(vg, x + 5.f, y + 23.f);
        nvgLineTo(vg, x + 23.f, y + 23.f);
        nvgStroke(vg);
    }

    nvgRestore(vg);
}

auto EntryDisplayName(const UpdaterEntry& entry) -> std::string {
    if (entry.type == UpdaterEntryType::FirmwareManual) {
        return "Install manually"_i18n;
    }

    if (entry.type != UpdaterEntryType::Kefir) {
        return entry.name;
    }

    if (const auto version = detail::ExtractKefirVersion(entry.name, entry.url); !version.empty()) {
        return "Version " + version;
    }

    auto name = entry.name;
    if (name.starts_with("Kefir")) {
        name.erase(0, std::strlen("Kefir"));
        name = ::sphaira::utils::TrimAsciiWhitespace(name);
    }
    return name.empty() ? "Version" : "Version " + name;
}

auto IsSelectableEntry(const UpdaterEntry& entry) -> bool {
    return entry.type != UpdaterEntryType::Section;
}

auto TravelDirection(s64 index, s64 previous, s64 count) -> s64 {
    const auto forward = index >= previous;
    const auto wrapped = std::abs(index - previous) > count / 2;
    return (forward != wrapped) ? 1 : -1;
}

auto ResolveSelectableIndex(const std::vector<UpdaterEntry>& entries, s64 index, s64 previous) -> s64 {
    const auto count = static_cast<s64>(entries.size());
    if (!count) {
        return 0;
    }

    index = (index % count + count) % count;
    if (IsSelectableEntry(entries[index])) {
        return index;
    }

    const auto direction = TravelDirection(index, previous, count);
    for (s64 i = 1; i < count; i++) {
        const auto j = ((index + direction * i) % count + count) % count;
        if (IsSelectableEntry(entries[j])) {
            return j;
        }
    }

    return 0;
}

auto TileSlots(const std::vector<UpdaterEntry>& entries) -> std::vector<s64> {
    std::vector<s64> out;
    bool has_group_entries{};

    for (s64 i = 0; i < static_cast<s64>(entries.size()); i++) {
        const auto& entry = entries[i];
        if (entry.type == UpdaterEntryType::Section) {
            if (has_group_entries) {
                while (out.size() % TILE_COLUMNS) {
                    out.emplace_back(TILE_EMPTY);
                }
            }
            has_group_entries = false;
            continue;
        }

        out.emplace_back(i);
        has_group_entries = true;
    }

    while (out.size() % TILE_COLUMNS) {
        out.emplace_back(TILE_EMPTY);
    }

    return out;
}

auto ResolveTileSlotIndex(const std::vector<s64>& slots, s64 index, s64 previous) -> s64 {
    const auto count = static_cast<s64>(slots.size());
    if (!count) {
        return 0;
    }

    index = (index % count + count) % count;
    if (slots[index] != TILE_EMPTY) {
        return index;
    }

    const auto direction = TravelDirection(index, previous, count);
    for (s64 i = 1; i < count; i++) {
        const auto j = ((index + direction * i) % count + count) % count;
        if (slots[j] != TILE_EMPTY) {
            return j;
        }
    }

    return 0;
}

auto TileGroupLabel(UpdaterEntryType type) -> const char* {
    switch (type) {
        case UpdaterEntryType::Kefir:
            return "KEFIR";
        case UpdaterEntryType::Firmware:
        case UpdaterEntryType::FirmwareManual:
            return "FIRMWARE";
        case UpdaterEntryType::Network:
        case UpdaterEntryType::CustomLink:
            return "OTHER";
        case UpdaterEntryType::Section:
            return "";
    }

    return "";
}

void AddSectionEntry(std::vector<UpdaterEntry>& out, std::string name) {
    out.push_back({
        .type = UpdaterEntryType::Section,
        .name = std::move(name),
        .url = {},
        .pack = false,
    });
}

void AppendEntriesOfType(std::vector<UpdaterEntry>& out, const std::vector<UpdaterEntry>& entries, UpdaterEntryType type) {
    for (const auto& entry : entries) {
        if (entry.type == type) {
            out.push_back(entry);
        }
    }
}

void BuildSectionedEntries(std::vector<UpdaterEntry>& out, const std::vector<UpdaterEntry>& downloads) {
    out.clear();

    AddSectionEntry(out, "KEFIR");
    AppendEntriesOfType(out, downloads, UpdaterEntryType::Kefir);

    AddSectionEntry(out, "FIRMWARE");
    AppendEntriesOfType(out, downloads, UpdaterEntryType::Firmware);
    out.push_back({
        .type = UpdaterEntryType::FirmwareManual,
        .name = "Install manually",
        .url = {},
        .pack = false,
    });
}

static auto AddJsonEntries(yyjson_val* object, UpdaterEntryType type, std::vector<UpdaterEntry>& out, std::string& latest_kefir, bool& latest_from_pack) -> bool {
    if (!object || !yyjson_is_obj(object)) {
        return false;
    }

    bool found{};
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(object, &iter);
    yyjson_val* key;
    while ((key = yyjson_obj_iter_next(&iter))) {
        auto value = yyjson_obj_iter_get_val(key);
        const auto name = yyjson_get_str(key);
        const auto url = yyjson_get_str(value);
        if (!name || !url || !*url) {
            continue;
        }

        UpdaterEntry entry{
            .type = type,
            .name = name,
            .url = url,
            .pack = std::string_view{name}.find("[PACK]") != std::string_view::npos,
        };

        if (entry.type == UpdaterEntryType::Kefir && (latest_kefir.empty() || (entry.pack && !latest_from_pack))) {
            latest_kefir = detail::MakeKefirLatestLabel(entry);
            latest_from_pack = entry.pack;
        }

        out.push_back(std::move(entry));
        found = true;
    }

    return found;
}

auto ParseUpdaterLinks(const fs::FsPath& path, std::vector<UpdaterEntry>& out, std::string& latest_kefir) -> bool {
    out.clear();
    latest_kefir.clear();

    auto doc = yyjson_read_file(path, YYJSON_READ_NOFLAG, nullptr, nullptr);
    if (!doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    auto root = yyjson_doc_get_root(doc);
    auto cfws = root ? yyjson_obj_get(root, "cfws") : nullptr;
    auto atmosphere = cfws ? yyjson_obj_get(cfws, "Atmosphere") : nullptr;
    auto firmwares = root ? yyjson_obj_get(root, "firmwares") : nullptr;

    bool latest_from_pack{};
    bool found{};
    found |= AddJsonEntries(atmosphere, UpdaterEntryType::Kefir, out, latest_kefir, latest_from_pack);
    found |= AddJsonEntries(firmwares, UpdaterEntryType::Firmware, out, latest_kefir, latest_from_pack);
    return found;
}

} // namespace sphaira::ui::menu::kefir
