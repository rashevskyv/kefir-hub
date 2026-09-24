#pragma once

#include "ui/menus/kefir_menu.hpp"
#include "ui/widget.hpp"
#include "ui/option_box.hpp"
#include "ui/nvg_util.hpp"
#include "web_qr.hpp"
#include <string>
#include <vector>
#include <functional>
#include <optional>

namespace sphaira::ui::menu::kefir {

constexpr float DOWNGRADE_BUTTON_HEIGHT = 70.f;
constexpr const char* NXLINKS_URL = "https://raw.githubusercontent.com/rashevskyv/nx-links/master/nx-links.json";
constexpr const char* CACHE_DIR = "/config/kefir-updater";
constexpr const char* NXLINKS_CACHE = "/config/kefir-updater/nx-links.json";
constexpr const char* AMS_ZIP = "/config/kefir-updater/atmo.zip";
constexpr const char* FIRMWARE_ZIP = "/config/kefir-updater/firmware.zip";
constexpr const char* KEFIR_PATH = "/kefir";
constexpr const char* FIRMWARE_DEST = "/firmware";
constexpr const char* KEFIR_VERSION_PATH = "/switch/kefir-updater/version";
constexpr const char* KEFIR_CHANGELOG_URL = "https://raw.githubusercontent.com/rashevskyv/kefir/master/changelog_full";
constexpr const char* COPY_FILES_TXT = "/config/kefir-updater/copy_files.txt";
constexpr const char* STAGED_COPY_FILES_TXT = "/kefir/config/kefir-updater/copy_files.txt";
constexpr const char* DOWNGRADE_FIX_SAVE = "/save/8000000000000073";
constexpr size_t UPDATE_TASK_BUFFER_SIZE = 0x100000;
constexpr s64 TILE_COLUMNS = 3;
constexpr s64 TILE_EMPTY = -1;
constexpr s64 UPDATER_LIST_PAGE_ROWS = 6;
constexpr float UPDATER_LIST_ROW_HEIGHT = 74.f;
constexpr float UPDATER_LIST_ROW_GAP = 8.f;
constexpr float UPDATER_INFO_Y_OFFSET = 11.f;
constexpr float UPDATER_INFO_ROW_GAP = 25.f;
constexpr float UPDATER_LIST_TOP_OFFSET = 1.f + 66.f;
constexpr float UPDATER_TILE_TOP_OFFSET = 1.f + 112.f;
constexpr float UPDATER_TILE_CLIP_TOP_OFFSET = UPDATER_TILE_TOP_OFFSET - 35.f;

class DowngradeWarningBox final : public Widget {
public:
    using Callback = std::function<void(std::optional<s64>)>;

    DowngradeWarningBox(
        const std::string& current_version,
        const std::string& target_version,
        const std::string& confirm_label,
        Callback cb
    );

    auto Update(Controller* controller, TouchInfo* touch) -> void override;
    auto Draw(NVGcontext* vg, Theme* theme) -> void override;
    auto OnFocusGained() noexcept -> void override;
    auto OnFocusLost() noexcept -> void override;
    auto IsModal() const -> bool override { return true; }

private:
    void SetIndex(s64 index);
    void LayoutButtons();

    const std::string m_current_version;
    const std::string m_target_version;
    const Callback m_callback;
    const QrCode m_qr;

    s64 m_index{1};
    Vec4 m_spacer_line{};
    std::vector<OptionBoxEntry> m_entries{};
};

auto EntryDescription(const UpdaterEntry& entry) -> const char*;
auto EntryIsFolder(const UpdaterEntry& entry) -> bool;
auto EntryIsDownload(const UpdaterEntry& entry) -> bool;
void DrawUpdaterEntryIcon(NVGcontext* vg, Theme* theme, const UpdaterEntry& entry, float x, float y, bool selected, bool disabled = false);
auto EntryDisplayName(const UpdaterEntry& entry) -> std::string;
auto IsSelectableEntry(const UpdaterEntry& entry) -> bool;
auto TravelDirection(s64 index, s64 previous, s64 count) -> s64;
auto ResolveSelectableIndex(const std::vector<UpdaterEntry>& entries, s64 index, s64 previous) -> s64;
auto TileSlots(const std::vector<UpdaterEntry>& entries) -> std::vector<s64>;
auto ResolveTileSlotIndex(const std::vector<s64>& slots, s64 index, s64 previous) -> s64;
auto TileGroupLabel(UpdaterEntryType type) -> const char*;
void AddSectionEntry(std::vector<UpdaterEntry>& out, std::string name);
void AppendEntriesOfType(std::vector<UpdaterEntry>& out, const std::vector<UpdaterEntry>& entries, UpdaterEntryType type);
void BuildSectionedEntries(std::vector<UpdaterEntry>& out, const std::vector<UpdaterEntry>& downloads);
auto ParseUpdaterLinks(const fs::FsPath& path, std::vector<UpdaterEntry>& out, std::string& latest_kefir) -> bool;

} // namespace sphaira::ui::menu::kefir
