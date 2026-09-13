#include "ui/menus/kefir_menu.hpp"

#include "ui/error_box.hpp"
#include "ui/menus/ghdl.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/sidebar.hpp"

#include "ams_su.h"
#include "app.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "hats_version.hpp"
#include "i18n.hpp"
#include "net.hpp"
#include "threaded_file_transfer.hpp"
#include "utils/utils.hpp"

#include <yyjson.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <string_view>
#include <utility>
#include "ui/menus/kefir/kefir_changelog.hpp"
#include "ui/menus/kefir/kefir_firmware.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/popup_list.hpp"
#include "ui/hold_confirm_box.hpp"
#include "text_helper.hpp"
#include "path_util.hpp"
#include "../../web_qr.hpp"


namespace sphaira::ui::menu::kefir {
using namespace detail;

namespace {

constexpr float DOWNGRADE_BUTTON_HEIGHT = 70.f;

class DowngradeWarningBox final : public Widget {
public:
    using Callback = std::function<void(std::optional<s64>)>;

    DowngradeWarningBox(
        const std::string& current_version,
        const std::string& target_version,
        const std::string& confirm_label,
        Callback cb
    )
    : m_current_version{current_version}
    , m_target_version{target_version}
    , m_callback{std::move(cb)}
    , m_qr{QrCode::Encode("https://switch.customfw.xyz/downgrade_fw")}
    {
        m_pos.w = 880.f;
        m_pos.h = 560.f;
        m_pos.x = (SCREEN_WIDTH - m_pos.w) / 2.f;
        m_pos.y = (SCREEN_HEIGHT - m_pos.h) / 2.f;

        const std::string text_a = "\uE0E1 " + "Cancel"_i18n;
        const std::string text_b = "\uE0EF " + confirm_label;

        auto box = m_pos;
        box.w /= 2.f;
        box.y = m_pos.y + m_pos.h - DOWNGRADE_BUTTON_HEIGHT;
        box.h = DOWNGRADE_BUTTON_HEIGHT;

        m_entries.emplace_back(text_a, box);
        box.x += box.w;
        m_entries.emplace_back(text_b, box);

        m_index = 1;
        m_entries[0].Selected(false);
        m_entries[1].Selected(true);
        LayoutButtons();

        SetActions(
            std::make_pair(Button::LEFT, Action{[this](){
                SetIndex(0);
            }}),
            std::make_pair(Button::RIGHT, Action{[this](){
                SetIndex(1);
            }}),
            std::make_pair(Button::A, Action{[this](){
                m_callback(m_index);
                SetPop();
            }}),
            std::make_pair(Button::B, Action{[this](){
                m_callback(0);
                SetPop();
            }}),
            std::make_pair(Button::START, Action{[this](){
                m_callback(1);
                SetPop();
            }})
        );
    }

    auto Update(Controller* controller, TouchInfo* touch) -> void override {
        Widget::Update(controller, touch);

        if (touch->is_clicked) {
            for (s64 i = 0; i < static_cast<s64>(m_entries.size()); i++) {
                if (touch->in_range(m_entries[i].GetPos())) {
                    SetIndex(i);
                    m_callback(i);
                    SetPop();
                    break;
                }
            }
        }
    }

    auto Draw(NVGcontext* vg, Theme* theme) -> void override {
        gfx::dimBackground(vg);
        gfx::drawRect(vg, m_pos, theme->GetColour(ThemeEntryID_POPUP), 5.f);

        // 1. Title
        const std::string title = "Firmware downgrade warning"_i18n;
        gfx::drawTextBold(vg, m_pos.x + m_pos.w / 2.f, m_pos.y + 18.f, 23.f,
            theme->GetColour(ThemeEntryID_TEXT_SELECTED), title.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);

        constexpr float pad_x = 36.f;
        const float col1_x = m_pos.x + pad_x;
        const float content_w = m_pos.w - pad_x * 2.f;

        // 2. Compact version rows: Current and Target side-by-side
        const std::string current_label = "Current:"_i18n;
        const std::string target_label = "Target:"_i18n;
        const float y_ver = m_pos.y + 54.f;

        nvgSave(vg);
        nvgFontSize(vg, 16.f);
        float b1[4]{}, b2[4]{};
        nvgTextBounds(vg, 0.f, 0.f, current_label.c_str(), nullptr, b1);
        nvgTextBounds(vg, 0.f, 0.f, target_label.c_str(), nullptr, b2);
        const float cur_lbl_w = b1[2] - b1[0];
        const float tgt_lbl_w = b2[2] - b2[0];
        nvgRestore(vg);

        gfx::drawTextBold(vg, col1_x, y_ver, 16.f, theme->GetColour(ThemeEntryID_TEXT), current_label.c_str());
        gfx::drawText(vg, col1_x + cur_lbl_w + 8.f, y_ver, 16.f, theme->GetColour(ThemeEntryID_TEXT), m_current_version.c_str());

        const float target_col_x = col1_x + 280.f;
        gfx::drawTextBold(vg, target_col_x, y_ver, 16.f, theme->GetColour(ThemeEntryID_TEXT), target_label.c_str());
        gfx::drawText(vg, target_col_x + tgt_lbl_w + 8.f, y_ver, 16.f, theme->GetColour(ThemeEntryID_TEXT), m_target_version.c_str());

        // 3. Automated fix info
        const float y_fix = y_ver + 26.f;
        const std::string fix_label = "Downgrade fix:"_i18n;
        const std::string fix_text = "An automated fix will run in TegraExplorer after install to delete save 8000000000000073 and reboot to Hekate."_i18n;
        gfx::drawTextBold(vg, col1_x, y_fix, 15.f, theme->GetColour(ThemeEntryID_TEXT), fix_label.c_str());
        nvgSave(vg);
        nvgFontSize(vg, 15.f);
        float fix_b[4]{};
        nvgTextBounds(vg, 0.f, 0.f, fix_label.c_str(), nullptr, fix_b);
        const float fix_lbl_w = fix_b[2] - fix_b[0];
        nvgRestore(vg);
        gfx::drawTextBox(vg, col1_x + fix_lbl_w + 8.f, y_fix, 14.f, content_w - fix_lbl_w - 8.f,
            theme->GetColour(ThemeEntryID_TEXT_INFO), fix_text.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_TOP, nullptr, 1.3f);

        // 4. Maintenance Mode instructions
        const float y_maint = y_fix + 38.f;
        const std::string maint_title = "If the console fails to boot or shows an error (Maintenance Mode):"_i18n;
        gfx::drawTextBold(vg, col1_x, y_maint, 15.f, theme->GetColour(ThemeEntryID_TEXT_SELECTED), maint_title.c_str());

        const float y_step1 = y_maint + 24.f;
        const std::string step1 = "1. Launch firmware; wait for Nintendo and Kefir boot logos to pass."_i18n;
        gfx::drawTextBox(vg, col1_x + 8.f, y_step1, 14.f, content_w - 8.f, theme->GetColour(ThemeEntryID_TEXT), step1.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_TOP, nullptr, 1.3f);

        const float y_step2 = y_step1 + 22.f;
        const std::string step2 = "2. Press and hold both Volume buttons (+ and -) until Maintenance Mode opens."_i18n;
        gfx::drawTextBox(vg, col1_x + 8.f, y_step2, 14.f, content_w - 8.f, theme->GetColour(ThemeEntryID_TEXT), step2.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_TOP, nullptr, 1.3f);

        const float y_step3 = y_step2 + 22.f;
        const std::string step3 = "3. Select 'Initialize Console Without Deleting Save Data'."_i18n;
        gfx::drawTextBox(vg, col1_x + 8.f, y_step3, 14.f, content_w - 8.f, theme->GetColour(ThemeEntryID_TEXT), step3.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_TOP, nullptr, 1.3f);

        const float y_warn = y_step3 + 22.f;
        const std::string warn_note = "Warning: All installed games and system settings will be wiped; saves are preserved."_i18n;
        gfx::drawTextBox(vg, col1_x + 8.f, y_warn, 14.f, content_w - 8.f, theme->GetColour(ThemeEntryID_TEXT_INFO), warn_note.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_TOP, nullptr, 1.3f);

        // 5. Guide & QR code section + Responsibility note
        const float guide_y = y_warn + 34.f;

        constexpr int qr_border = 3;
        constexpr float qr_scale = 3.f;
        constexpr float qr_total_w = (QrCode::SIZE + qr_border * 2) * qr_scale;
        const float qr_x = m_pos.x + m_pos.w - pad_x - qr_total_w;
        const float qr_y = guide_y;

        // Draw scan-ready QR code with white quiet zone border
        gfx::drawRect(vg, qr_x, qr_y, qr_total_w, qr_total_w, nvgRGBA(255, 255, 255, 255), 4.f);
        nvgBeginPath(vg);
        for (int qy = 0; qy < QrCode::SIZE; qy++) {
            for (int qx = 0; qx < QrCode::SIZE; qx++) {
                if (m_qr.Get(qx, qy)) {
                    nvgRect(vg, qr_x + (qx + qr_border) * qr_scale, qr_y + (qy + qr_border) * qr_scale, qr_scale, qr_scale);
                }
            }
        }
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 255));
        nvgFill(vg);

        // Left text next to QR
        const float left_w = qr_x - col1_x - 18.f;
        const std::string guide_label = "Manual downgrade guide:"_i18n;
        gfx::drawTextBold(vg, col1_x, guide_y, 15.f, theme->GetColour(ThemeEntryID_TEXT), guide_label.c_str());

        const float guide_url_y = guide_y + 22.f;
        gfx::drawText(vg, col1_x, guide_url_y, 14.f, theme->GetColour(ThemeEntryID_TEXT_INFO), "https://switch.customfw.xyz/downgrade_fw");

        const std::string scan_hint = "If you prefer manual downgrade or issues persist, scan the QR code to open the guide."_i18n;
        const float scan_hint_y = guide_url_y + 20.f;
        gfx::drawTextBox(vg, col1_x, scan_hint_y, 13.f, left_w, theme->GetColour(ThemeEntryID_TEXT), scan_hint.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_TOP, nullptr, 1.3f);

        const std::string resp_text = "By continuing, you accept full responsibility."_i18n;
        const float resp_y = scan_hint_y + 36.f;
        gfx::drawTextBox(vg, col1_x, resp_y, 13.f, left_w, theme->GetColour(ThemeEntryID_TEXT_INFO), resp_text.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_TOP, nullptr, 1.3f);

        // 6. Separator line and buttons
        gfx::drawRect(vg, m_spacer_line, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
        for (auto& entry : m_entries) {
            entry.Draw(vg, theme);
        }
    }

    auto OnFocusGained() noexcept -> void override {
        Widget::OnFocusGained();
        SetHidden(false);
    }

    auto OnFocusLost() noexcept -> void override {
        Widget::OnFocusLost();
        SetHidden(true);
    }

    auto IsModal() const -> bool override {
        return true;
    }

private:
    void SetIndex(s64 index) {
        if (m_index != index && index >= 0 && index < static_cast<s64>(m_entries.size())) {
            m_entries[m_index].Selected(false);
            m_index = index;
            m_entries[m_index].Selected(true);
        }
    }

    void LayoutButtons() {
        m_spacer_line = Vec4{m_pos.x, m_pos.y + m_pos.h - DOWNGRADE_BUTTON_HEIGHT - 2.f, m_pos.w, 2.f};

        auto box = m_pos;
        box.w = m_pos.w / 2.f;
        box.y = m_pos.y + m_pos.h - DOWNGRADE_BUTTON_HEIGHT;
        box.h = DOWNGRADE_BUTTON_HEIGHT;

        m_entries[0].UpdateLayout(box);
        box.x += box.w;
        m_entries[1].UpdateLayout(box);
    }

private:
    const std::string m_current_version;
    const std::string m_target_version;
    const Callback m_callback;
    const QrCode m_qr;

    s64 m_index{1};
    Vec4 m_spacer_line{};
    std::vector<OptionBoxEntry> m_entries{};
};

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

void DrawUpdaterEntryIcon(NVGcontext* vg, Theme* theme, const UpdaterEntry& entry, float x, float y, bool selected, bool disabled = false) {
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

// Direction the cursor travelled to land on `index`, given where it came from.
// A wrap reads as a jump across most of the list, but the cursor really moved
// one step the other way, so the sign is flipped for it.
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

    // captions are stepped over in the direction of travel, wrapping round the
    // ends. Entry 0 is the "KEFIR" caption, so without the wrap pressing up on
    // the first real row bounced off it and the last row was unreachable.
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

auto AddJsonEntries(yyjson_val* object, UpdaterEntryType type, std::vector<UpdaterEntry>& out, std::string& latest_kefir, bool& latest_from_pack) -> bool {
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

} // namespace

Menu::Menu() : MenuBase{"Updater", MenuFlag_None} {
    RefreshSystemInfo();

    this->SetActions(
        std::make_pair(Button::A, Action{"Open"_i18n, [this](){
            if (!m_entries.empty() && !m_loading) {
                OpenSelected();
            }
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }}),
        std::make_pair(Button::X, Action{"Refresh"_i18n, [this](){
            m_retry_on_connect = false;
            m_loaded = false;
            RefreshSystemInfo();
            FetchLinks();
        }}),
        std::make_pair(Button::START, Action{"Options"_i18n, [this](){
            DisplayOptions();
        }})
    );

    OnLayoutChange();
}

Menu::~Menu() = default;

void Menu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    if (m_retry_on_connect && !m_loading && net::IsConnectedCached()) {
        m_retry_on_connect = false;
        FetchLinks();
    }

    if (m_entries.empty()) {
        return;
    }

    if (static_cast<UpdaterViewMode>(m_view_mode.Get()) == UpdaterViewMode::Tiles) {
        if (controller->GotDown(Button::RIGHT)) {
            MoveTileSelection(1);
            return;
        } else if (controller->GotDown(Button::LEFT)) {
            MoveTileSelection(-1);
            return;
        } else if (controller->GotDown(Button::DOWN)) {
            MoveTileSelection(TILE_COLUMNS);
            return;
        } else if (controller->GotDown(Button::UP)) {
            MoveTileSelection(-TILE_COLUMNS);
            return;
        } else if (controller->GotDown(Button::R2)) {
            MoveTileSelection(TILE_COLUMNS * 2);
            return;
        } else if (controller->GotDown(Button::L2)) {
            MoveTileSelection(-TILE_COLUMNS * 2);
            return;
        }

        m_list->OnUpdate(controller, touch, m_tile_index, m_tile_entries.size(), [this](bool touch, auto i) {
            const auto tile_index = ResolveTileSlotIndex(m_tile_entries, i, m_tile_index);
            if (touch && m_tile_index == tile_index) {
                FireAction(Button::A);
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                m_tile_index = tile_index;
                if (tile_index >= 0 && tile_index < static_cast<s64>(m_tile_entries.size()) && m_tile_entries[tile_index] != TILE_EMPTY) {
                    SetIndex(m_tile_entries[tile_index]);
                }
            }
        }, this);
    } else {
        m_list->OnUpdate(controller, touch, m_index, m_entries.size(), [this](bool touch, auto i) {
            if (touch && m_index == i) {
                FireAction(Button::A);
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                SetIndex(i);
            }
        }, this);
    }
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    const auto tiles = static_cast<UpdaterViewMode>(m_view_mode.Get()) == UpdaterViewMode::Tiles;
    const auto info_colour = theme->GetColour(ThemeEntryID_TEXT_INFO);
    const auto info_y = GetY() + UPDATER_INFO_Y_OFFSET;
    nvgSave(vg);
    nvgFontSize(vg, 17.f);
    nvgFillColor(vg, info_colour);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

    // Current Kefir
    const std::string current_kefir_lbl = "Current Kefir:"_i18n;
    nvgText(vg, 80.f, info_y, current_kefir_lbl.c_str(), nullptr);
    nvgText(vg, 81.f, info_y, current_kefir_lbl.c_str(), nullptr);
    float ck_bounds[4]{};
    nvgTextBounds(vg, 80.f, info_y, current_kefir_lbl.c_str(), nullptr, ck_bounds);
    nvgText(vg, ck_bounds[2] + 7.f, info_y, m_current_kefir.c_str(), nullptr);

    // Latest Kefir
    const std::string latest_kefir_lbl = "Latest Kefir:"_i18n;
    nvgText(vg, 650.f, info_y, latest_kefir_lbl.c_str(), nullptr);
    nvgText(vg, 651.f, info_y, latest_kefir_lbl.c_str(), nullptr);
    float lk_bounds[4]{};
    nvgTextBounds(vg, 650.f, info_y, latest_kefir_lbl.c_str(), nullptr, lk_bounds);
    nvgText(vg, lk_bounds[2] + 7.f, info_y, m_latest_kefir.c_str(), nullptr);

    // Current Firmware
    const std::string current_fw_lbl = "Current Firmware:"_i18n;
    nvgText(vg, 80.f, info_y + UPDATER_INFO_ROW_GAP, current_fw_lbl.c_str(), nullptr);
    nvgText(vg, 81.f, info_y + UPDATER_INFO_ROW_GAP, current_fw_lbl.c_str(), nullptr);
    float cf_bounds[4]{};
    nvgTextBounds(vg, 80.f, info_y + UPDATER_INFO_ROW_GAP, current_fw_lbl.c_str(), nullptr, cf_bounds);
    nvgText(vg, cf_bounds[2] + 7.f, info_y + UPDATER_INFO_ROW_GAP, m_current_firmware.c_str(), nullptr);

    // Console
    const std::string console_lbl = "Console:"_i18n;
    nvgText(vg, 650.f, info_y + UPDATER_INFO_ROW_GAP, console_lbl.c_str(), nullptr);
    nvgText(vg, 651.f, info_y + UPDATER_INFO_ROW_GAP, console_lbl.c_str(), nullptr);
    float c_bounds[4]{};
    nvgTextBounds(vg, 650.f, info_y + UPDATER_INFO_ROW_GAP, console_lbl.c_str(), nullptr, c_bounds);
    nvgText(vg, c_bounds[2] + 7.f, info_y + UPDATER_INFO_ROW_GAP, m_console_revision.c_str(), nullptr);

    nvgRestore(vg);

    if (m_loading) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO),
            "Loading updater links...");
        return;
    }

    if (!m_error_message.empty()) {
        gfx::drawTextArgs(vg, 80.f, GetY() + 71.f, 17.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_ERROR),
            "%s", m_error_message.c_str());
    }

    if (m_entries.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO),
            "No updater entries found");
        return;
    }

    if (tiles) {
        DrawTiles(vg, theme);
    } else {
        DrawList(vg, theme);
    }
}

void Menu::DrawList(NVGcontext* vg, Theme* theme) {
    m_list->Draw(vg, theme, m_entries.size(), m_index, [this](auto* vg, auto* theme, Vec4 v, auto i) {
        const auto& entry = m_entries[i];
        if (entry.type == UpdaterEntryType::Section) {
            const auto top_pad = 32.f;
            const Vec4 band{v.x, v.y + top_pad, v.w, 26.f};
            gfx::drawRect(vg, band.x, band.y, band.w, band.h, theme->GetColour(ThemeEntryID_SELECTED_BACKGROUND), 4.f);
            gfx::drawRect(vg, v.x + 15.f, band.y + 7.f, 4.f, band.h - 14.f, theme->GetColour(ThemeEntryID_TEXT_SELECTED), 2.f);
            gfx::drawTextArgs(vg, v.x + 30.f, band.y + band.h / 2.f, 16.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_SELECTED),
                "%s", entry.name.c_str());
            return;
        }

        const auto selected = m_index == i;
        const auto downgrade = entry.type == UpdaterEntryType::Firmware && IsDowngrade(entry.name);
        const auto unsupported = entry.type == UpdaterEntryType::Firmware && !IsFirmwareSupported(entry.name);
        const auto kefir_update = IsKefirUpdate(entry);
        const auto text_id = selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;
        const auto name_id = unsupported ? ThemeEntryID_TEXT_INFO : (downgrade ? ThemeEntryID_ERROR : (kefir_update ? ThemeEntryID_TEXT_SELECTED : text_id));

        if (selected) {
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else if (i != m_entries.size() - 1) {
            gfx::drawRect(vg, v.x, v.y + v.h, v.w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
        }

        std::string name = EntryDisplayName(entry);
        if (unsupported) {
            name += " [UNSUPPORTED]";
        }

        const auto text_x = v.x + 55.f;
        DrawUpdaterEntryIcon(vg, theme, entry, v.x + 15.f, v.y + 24.f, selected, unsupported);

        gfx::drawTextBox(vg, text_x, v.y + 11.f, 23.f, v.w - 230.f,
            theme->GetColour(name_id), name.c_str());

        if (kefir_update) {
            gfx::drawTextArgs(vg, v.x + v.w - 15.f, v.y + 17.f, 15.f,
                NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT_SELECTED),
                "UPDATE");
        } else if (entry.type != UpdaterEntryType::Kefir) {
            const auto label = unsupported ? UnsupportedFirmwareLabel(m_supported_firmware) : (entry.type == UpdaterEntryType::Firmware && downgrade ? "DOWNGRADE" : TypeLabel(entry.type));
            const auto label_colour = (entry.type == UpdaterEntryType::Firmware && downgrade) ? theme->GetColour(ThemeEntryID_ERROR) : theme->GetColour(ThemeEntryID_TEXT_INFO);
            gfx::drawTextArgs(vg, v.x + v.w - 15.f, v.y + 17.f, 15.f,
                NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, label_colour,
                "%s", label.c_str());
        }

        gfx::drawTextBox(vg, text_x, v.y + 44.f, 16.f, v.w - 70.f,
            theme->GetColour(ThemeEntryID_TEXT_INFO), EntryDescription(entry));
    });
}

void Menu::DrawTiles(NVGcontext* vg, Theme* theme) {
    m_list->Draw(vg, theme, m_tile_entries.size(), m_tile_index, [this](auto* vg, auto* theme, Vec4 v, auto tile_i) {
        const auto entry_index = m_tile_entries[tile_i];
        if (entry_index == TILE_EMPTY) {
            return;
        }

        const auto& entry = m_entries[entry_index];
        const auto selected = m_index == entry_index;
        const auto downgrade = entry.type == UpdaterEntryType::Firmware && IsDowngrade(entry.name);
        const auto unsupported = entry.type == UpdaterEntryType::Firmware && !IsFirmwareSupported(entry.name);
        const auto kefir_update = IsKefirUpdate(entry);

        s64 previous_entry_index = TILE_EMPTY;
        for (s64 i = tile_i - 1; i >= 0; i--) {
            if (m_tile_entries[i] != TILE_EMPTY) {
                previous_entry_index = m_tile_entries[i];
                break;
            }
        }

        const bool show_group = previous_entry_index == TILE_EMPTY ||
            TileGroupLabel(entry.type) != std::string_view{TileGroupLabel(m_entries[previous_entry_index].type)};

        if (show_group) {
            gfx::drawTextArgs(vg, v.x, v.y - 33.f, 17.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
                theme->GetColour(ThemeEntryID_TEXT_SELECTED), "%s", TileGroupLabel(entry.type));
        }

        const auto tile = v;
        if (selected) {
            gfx::drawRectOutline(vg, theme, 4.f, tile);
        } else {
            gfx::drawRect(vg, tile, theme->GetColour(ThemeEntryID_LINE_SEPARATOR), 4.f);
        }

        // Draw icon container frame (subtle background)
        gfx::drawRect(vg, tile.x + 20.f, tile.y + 20.f, 115.f, 115.f, nvgRGBA(0, 0, 0, 25), 8.f);

        // Center and scale the vector icon inside the 115x115 container
        // Original icon size: 28x23. With 2.5x scale: 70x57.5.
        const float ix = tile.x + 20.f + (115.f - 70.f) / 2.f;
        const float iy = tile.y + 20.f + (115.f - 57.5f) / 2.f;

        nvgSave(vg);
        nvgTranslate(vg, ix, iy);
        nvgScale(vg, 2.5f, 2.5f);
        DrawUpdaterEntryIcon(vg, theme, entry, 0.f, 0.f, selected, unsupported);
        nvgRestore(vg);

        // Draw texts on the right side of the card
        const auto name_colour = unsupported ? theme->GetColour(ThemeEntryID_TEXT_INFO) : downgrade ? theme->GetColour(ThemeEntryID_ERROR) :
            (kefir_update ? theme->GetColour(ThemeEntryID_TEXT_SELECTED) : theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT));

        std::string name = EntryDisplayName(entry);
        if (unsupported) {
            name += " [UNSUPPORTED]";
        }

        const float text_x = tile.x + 148.f;
        const float text_clip_w = tile.w - 20.f - 148.f;

        // 1. Title/Name
        gfx::drawTextBox(vg, text_x, tile.y + 24.f, 18.f, text_clip_w, name_colour, name.c_str());

        // 2. Type/Status
        const auto type_label = kefir_update ? std::string{"UPDATE"} : (unsupported ? UnsupportedFirmwareLabel(m_supported_firmware) : (entry.type == UpdaterEntryType::Firmware && downgrade ? "DOWNGRADE" : TypeLabel(entry.type)));
        const auto type_colour = kefir_update ? theme->GetColour(ThemeEntryID_TEXT_SELECTED) : ((entry.type == UpdaterEntryType::Firmware && downgrade) ? theme->GetColour(ThemeEntryID_ERROR) : theme->GetColour(ThemeEntryID_TEXT_INFO));
        gfx::drawTextBox(vg, text_x, tile.y + 68.f, 14.f, text_clip_w, type_colour, type_label.c_str());

        // 3. Description
        const char* description = EntryDescription(entry);
        if (entry.type == UpdaterEntryType::Kefir || entry.type == UpdaterEntryType::Firmware) {
            description = "";
        }
        gfx::drawTextBox(vg, text_x, tile.y + 92.f, 14.f, text_clip_w, theme->GetColour(ThemeEntryID_TEXT_INFO), description, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, nullptr, 1.5f);
    });
}

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();
    RefreshSystemInfo();

    // a folder or zip was chosen in the manual-install picker; now that it has
    // closed and we are the top menu again, kick off validation/installation.
    if (m_pending_manual_firmware) {
        const auto path = *m_pending_manual_firmware;
        const bool is_zip = m_pending_manual_firmware_is_zip;
        m_pending_manual_firmware.reset();
        m_pending_manual_firmware_is_zip = false;

        if (is_zip) {
            StartManualZipFirmware(path);
        } else {
            std::string name = path.s;
            if (const auto slash = name.find_last_of('/'); slash != std::string::npos) {
                name = name.substr(slash + 1);
            }
            if (name.empty()) {
                name = "Firmware";
            }

            PromptInstallFirmware(name, path);
        }
        return;
    }

    if (!m_loaded && !m_loading) {
        FetchLinks();
    }
}

void Menu::DisplayOptions() {
    auto options = std::make_unique<Sidebar>("Updater Options"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    SidebarEntryArray::Items view_items{
        "List"_i18n,
        "Grid"_i18n,
    };

    options->Add<SidebarEntryArray>("Layout"_i18n, view_items, [this](s64& index_out) {
        m_view_mode.Set(index_out);
        OnLayoutChange();
    }, m_view_mode.Get(), "Switch between list and grid view for the updater."_i18n);

    // the whole downgrade-fix surface is hidden while it has no working
    // implementation, so nothing in the ui promises something it cannot do.
    if (detail::IsDowngradeFixAvailable()) {
        // policy for the downgrade fix when installing a lower firmware.
        // items order must match enum DowngradeFixMode.
        SidebarEntryArray::Items downgrade_items{
            "Automatic"_i18n,
            "Optional (ask)"_i18n,
            "Off"_i18n,
        };
        options->Add<SidebarEntryArray>("Downgrade fix"_i18n, downgrade_items, [this](s64& index_out) {
            m_downgrade_fix_mode.Set(index_out);
        }, m_downgrade_fix_mode.Get(), "When installing a lower firmware: Automatic deletes the system save 8000000000000073, Optional asks, Off never deletes it."_i18n);

        // run the downgrade fix on its own so it can be tested in isolation.
        options->Add<SidebarEntryCallback>("Apply downgrade fix"_i18n, [](){
            App::Push<OptionBox>(
                "Apply downgrade fix now?\n\nThis will reboot into TegraExplorer to delete the system save 8000000000000073.",
                "Cancel"_i18n, "Apply"_i18n, 0,
                [](auto op_index) {
                    if (!op_index || *op_index != 1) {
                        return;
                    }
                    if (!detail::StageAndLaunchDowngradeFix(App::IsEmummc())) {
                        App::Push<OptionBox>("Failed to stage or launch TegraExplorer downgrade fix.", "OK"_i18n);
                    }
                });
        }, "Delete system save 8000000000000073 (via TegraExplorer)."_i18n);
    }
}

void Menu::OnLayoutChange() {
    auto make_content_pos = [this](float top) {
        auto pos = m_pos;
        pos.y = top;
        pos.h = std::max(0.f, GetY() + GetH() - top);
        return pos;
    };

    Vec4 content_pos{};
    if (static_cast<UpdaterViewMode>(m_view_mode.Get()) == UpdaterViewMode::Tiles) {
        constexpr float x = 75.f;
        constexpr float tile_w = 370.f;
        constexpr float tile_h = 155.f;
        constexpr float x_gap = 10.f;
        constexpr float y_gap = 65.f;
        content_pos = make_content_pos(GetY() + UPDATER_TILE_CLIP_TOP_OFFSET);
        const Vec4 v{x, GetY() + UPDATER_TILE_TOP_OFFSET, tile_w, tile_h};
        m_list = std::make_unique<List>(3, 2 * 3, content_pos, v, Vec2{x_gap, y_gap});
    } else {
        content_pos = make_content_pos(GetY() + UPDATER_LIST_TOP_OFFSET);
        const Vec4 v{75.f, GetY() + UPDATER_LIST_TOP_OFFSET, 1220.f - 150.f, UPDATER_LIST_ROW_HEIGHT};
        m_list = std::make_unique<List>(1, UPDATER_LIST_PAGE_ROWS, content_pos, v, Vec2{0.f, UPDATER_LIST_ROW_GAP});
    }
    m_list->SetLayout(List::Layout::GRID);
    m_list->SetScrollBarPos(m_pos.x + m_pos.w, content_pos.y, content_pos.h);

    m_tile_entries = TileSlots(m_entries);
    const auto it = std::ranges::find(m_tile_entries, m_index);
    m_tile_index = it == m_tile_entries.end() ? 0 : std::distance(m_tile_entries.begin(), it);
    EnsureTileVisible();
}

void Menu::EnsureTileVisible() {
    if (!m_list || static_cast<UpdaterViewMode>(m_view_mode.Get()) != UpdaterViewMode::Tiles || m_tile_entries.empty()) {
        return;
    }

    constexpr s64 visible_rows = 2;
    const auto row = m_tile_index / TILE_COLUMNS;
    const auto first_visible_row = static_cast<s64>(m_list->GetYoff() / m_list->GetMaxY());

    if (row < first_visible_row) {
        m_list->SetYoff(static_cast<float>(row) * m_list->GetMaxY());
    } else if (row >= first_visible_row + visible_rows) {
        m_list->SetYoff(static_cast<float>(row - visible_rows + 1) * m_list->GetMaxY());
    }
}

bool Menu::MoveTileSelection(s64 step) {
    if (m_tile_entries.empty()) {
        return false;
    }

    const auto old_index = m_tile_index;
    const auto size = static_cast<s64>(m_tile_entries.size());
    // wraps rather than clamps, so the tile grid scrolls round the same way
    // every other list in the app does.
    const auto target = ((m_tile_index + step) % size + size) % size;
    const auto next_index = ResolveTileSlotIndex(m_tile_entries, target, m_tile_index);
    if (next_index == old_index || m_tile_entries[next_index] == TILE_EMPTY) {
        return false;
    }

    m_tile_index = next_index;
    SetIndex(m_tile_entries[m_tile_index]);
    App::PlaySoundEffect(SoundEffect_Focus);
    return true;
}

void Menu::FetchLinks() {
    m_loading = true;
    m_error_message.clear();
    m_entries.clear();
    m_tile_entries.clear();
    m_latest_kefir = "Unknown";
    BuildSectionedEntries(m_entries, {});
    m_tile_entries = TileSlots(m_entries);
    m_index = 0;
    SetIndex(0);

    fs::FsNativeSd().CreateDirectoryRecursively(CACHE_DIR);

    curl::Api().ToFileAsync(
        curl::Url{NXLINKS_URL},
        curl::Path{NXLINKS_CACHE},
        curl::Flags{curl::Flag_Cache},
        curl::StopToken{this->GetToken()},
        curl::OnComplete{[this](auto& result) {
            m_loading = false;
            m_loaded = true;

            std::vector<UpdaterEntry> entries;
            std::string latest_kefir;
            if (!result.success || !ParseUpdaterLinks(result.path, entries, latest_kefir)) {
                m_retry_on_connect = !net::IsConnectedCached();
                m_error_message = "Failed to load updater lists.";
                m_index = 0;
                SetIndex(0);
                return false;
            }

            m_retry_on_connect = false;
            BuildSectionedEntries(m_entries, entries);
            m_tile_entries = TileSlots(m_entries);
            m_latest_kefir = latest_kefir.empty() ? "Unknown" : latest_kefir;

            if (entries.empty()) {
                m_error_message = "No Kefir or firmware downloads found.";
            }

            m_index = 0;
            SetIndex(0);
            return true;
        }}
    );
}

void Menu::SetIndex(s64 index) {
    m_index = ResolveSelectableIndex(m_entries, index, m_index);
    if (m_list) {
        if (static_cast<UpdaterViewMode>(m_view_mode.Get()) == UpdaterViewMode::List) {
            const auto max = UPDATER_LIST_ROW_HEIGHT + UPDATER_LIST_ROW_GAP;
            const auto start = static_cast<s64>(m_list->GetYoff() / max);
            if (m_index < start) {
                m_list->SetYoff(m_index * max);
            } else if (m_index >= start + UPDATER_LIST_PAGE_ROWS) {
                m_list->SetYoff((m_index - UPDATER_LIST_PAGE_ROWS + 1) * max);
            }
        }
    }

    if (m_index <= 1) {
        m_list->SetYoff(0);
    }

    const auto it = std::ranges::find(m_tile_entries, m_index);
    if (it != m_tile_entries.end()) {
        m_tile_index = std::distance(m_tile_entries.begin(), it);
        EnsureTileVisible();
    }

    UpdateSubheading();
}

void Menu::OpenSelected() {
    if (m_entries.empty() || m_index >= static_cast<s64>(m_entries.size())) {
        return;
    }

    const auto entry = m_entries[m_index];
    switch (entry.type) {
        case UpdaterEntryType::Network:
            App::Push<ui::menu::gh::Menu>(MenuFlag_None);
            break;
        case UpdaterEntryType::CustomLink:
            ui::menu::gh::DownloadDirectLink();
            break;
        case UpdaterEntryType::Kefir:
            InstallKefir(entry);
            break;
        case UpdaterEntryType::Firmware:
            DownloadFirmware(entry);
            break;
        case UpdaterEntryType::FirmwareManual:
            OpenManualFirmwarePicker();
            break;
        case UpdaterEntryType::Section:
            break;
    }
}

void Menu::OpenManualFirmwarePicker() {
    auto browser = std::make_unique<::sphaira::ui::menu::filebrowser::Menu>(MenuFlag_None);
    browser->SetFolderPicker([this](const fs::FsPath& selected_path) {
        // record the choice; consumed in OnFocusGained once the browser closes,
        // so nothing is pushed over the soon-to-be-popped file browser.
        m_pending_manual_firmware = selected_path;
        m_pending_manual_firmware_is_zip = ::sphaira::path::EqualsIC(text_helper::GetExtension(selected_path.s), "zip");
    });
    App::Push(std::move(browser));
}

void Menu::StartManualZipFirmware(const fs::FsPath& zip_path) {
    std::string name = zip_path.s;
    if (const auto slash = name.find_last_of('/'); slash != std::string::npos) {
        name = name.substr(slash + 1);
    }
    if (name.empty()) {
        name = "Firmware";
    }

    App::Push<ProgressBox>(0, "Extracting"_i18n, name,
        [zip_path](auto pbox) -> Result {
            return detail::ExtractManualFirmwareZip(pbox, zip_path);
        },
        [this, name, zip_path](Result rc) {
            if (R_FAILED(rc)) {
                detail::CleanupManualFirmwareStaging();
                if (rc == Result_TransferCancelled) {
                    return;
                }
                App::Push<ErrorBox>(rc, "Failed to extract " + name);
                return;
            }

            PromptInstallFirmware(name, detail::MANUAL_FIRMWARE_DEST, std::nullopt, zip_path);
        });
}

void Menu::InstallKefir(const UpdaterEntry& entry, std::function<void()> on_success) {
    App::Push<KefirChangelogBox>(entry,
        [this, entry, on_success = std::move(on_success)]() mutable {
            App::Push<ProgressBox>(0, "Installing"_i18n, entry.name,
                [entry](auto pbox) -> Result {
                    return detail::DownloadAndInstallKefir(pbox, entry);
                },
                [this, entry, on_success = std::move(on_success)](Result rc) mutable {
                    if (R_FAILED(rc)) {
                        if (rc == Result_TransferCancelled) {
                            return;
                        }
                        if (rc == Result_AppstoreFailedZipDownload) {
                            App::Push<ErrorBox>(rc, "Failed to download " + entry.name);
                        } else {
                            App::Push<ErrorBox>(rc, "Failed to install " + entry.name);
                        }
                        return;
                    }

                    RefreshSystemInfo();
                    if (on_success) {
                        on_success();
                        return;
                    }

                    App::Push<OptionBox>(
                        "Kefir package installed.\n\nReboot now?",
                        "Later"_i18n, "Reboot"_i18n, 1,
                        [](auto op_index) {
                            if (op_index && *op_index == 1) {
                                utils::requestForcedReboot();
                            }
                        });
                });
        });
}

void Menu::DownloadFirmware(const UpdaterEntry& entry, bool skip_support_check) {
    if (!skip_support_check && !IsFirmwareSupported(entry.name)) {
        PromptKefirThenFirmware(entry);
        return;
    }

    // a downgrade is fully acknowledged BEFORE anything is downloaded: the
    // warning and the downgrade-fix choice are both answered up front, so
    // nothing is fetched until the user has accepted it.
    const auto downgrade = PromptDowngradeAck(entry.name, "Download"_i18n,
        [this, entry](bool apply_fix) {
            StartFirmwareDownload(entry, apply_fix);
        });

    if (downgrade) {
        return;
    }

    std::string message = "Download firmware " + entry.name + "?\n\n";
    message += "It will be staged at ";
    message += FIRMWARE_ZIP;
    message += " and extracted to /firmware.";

    App::Push<OptionBox>(message, "Cancel"_i18n, "Download"_i18n, 1,
        [this, entry](auto op_index) {
            if (!op_index || *op_index != 1) {
                return;
            }

            StartFirmwareDownload(entry, std::nullopt);
        });
}

void Menu::StartFirmwareDownload(const UpdaterEntry& entry, std::optional<bool> acked_downgrade_fix) {
    App::Push<ProgressBox>(0, "Downloading"_i18n, entry.name,
        [entry](auto pbox) -> Result {
            return detail::DownloadAndExtractFirmware(pbox, entry);
        },
        [this, entry, acked_downgrade_fix](Result rc) {
            if (R_FAILED(rc)) {
                if (rc == Result_TransferCancelled) {
                    return;
                }
                App::Push<ErrorBox>(rc, "Failed to download " + entry.name);
                return;
            }

            PromptInstallFirmware(entry.name, "/firmware", acked_downgrade_fix);
        });
}

bool Menu::PromptDowngradeAck(const std::string& target_version, const std::string& confirm_label, std::function<void(bool)> on_ack, std::function<void()> on_cancel) {
    if (!IsDowngrade(target_version)) {
        return false;
    }

    App::Push<DowngradeWarningBox>(
        m_current_firmware,
        target_version,
        confirm_label,
        [this, on_ack = std::move(on_ack), on_cancel = std::move(on_cancel)](auto op_index) {
            if (!op_index || *op_index != 1) {
                if (on_cancel) {
                    on_cancel();
                }
                return;
            }

            // nothing to ask while the fix has no working implementation.
            if (!detail::IsDowngradeFixAvailable()) {
                on_ack(false);
                return;
            }

            // apply the configured downgrade-fix policy.
            switch (m_downgrade_fix_mode.Get()) {
                case DowngradeFixMode_Off:
                    on_ack(false);
                    break;
                case DowngradeFixMode_Automatic:
                    on_ack(true);
                    break;
                case DowngradeFixMode_Optional:
                default: {
                    std::string msg = "Apply downgrade fix?\n\n";
                    msg += "This stages TegraExplorer downgrade fix to delete system save 8000000000000073 after install.\n\n";
                    msg += "Choose No to install without it.";
                    App::Push<OptionBox>(msg, "No"_i18n, "Yes"_i18n, 1,
                        [on_ack, on_cancel](auto fix_index) {
                            if (!fix_index) {
                                if (on_cancel) {
                                    on_cancel();
                                }
                                return;
                            }
                            on_ack(*fix_index == 1);
                        });
                    break;
                }
            }
        });

    return true;
}

void Menu::PromptInstallFirmware(const std::string& display_name, const fs::FsPath& path, std::optional<bool> acked_downgrade_fix, std::optional<fs::FsPath> origin_zip) {
    auto validation = std::make_shared<FirmwareValidation>();
    App::Push<ProgressBox>(0, "Validating"_i18n, display_name,
        [validation, path](auto pbox) -> Result {
            pbox->NewTransfer("Validating firmware contents...");
            return detail::ValidateFirmware(validation.get(), path);
        },
        [this, display_name, path, validation, acked_downgrade_fix, origin_zip](Result rc) {
            if (R_FAILED(rc)) {
                if (origin_zip) {
                    detail::CleanupManualFirmwareStaging();
                }
                App::Push<ErrorBox>(rc, "Firmware validation failed");
                return;
            }

            const auto version = detail::FormatFirmwareVersion(validation->info.version);
            const bool use_exfat = validation->info.exfat_supported &&
                                   R_SUCCEEDED(validation->validation.exfat_result);

            auto prompt_install_confirm = [this, display_name, path, version, use_exfat, origin_zip](bool apply_fix) {
                std::string message = "Install firmware " + version + " on " + detail::GetFirmwareTargetName() + "?\n\n";
                message += use_exfat ? "FAT32 + exFAT support\n" : "FAT32 support only\n";
                message += "Do not power off the console during installation.";

                App::Push<OptionBox>(message, "Cancel"_i18n, "Install"_i18n, 1,
                    [this, display_name, path, apply_fix, origin_zip](auto op_index) {
                        if (!op_index || *op_index != 1) {
                            if (origin_zip) {
                                detail::CleanupManualFirmwareStaging();
                            }
                            return;
                        }

                        InstallFirmware(display_name, path, apply_fix, origin_zip);
                    });
            };

            // the downgrade warning/fix is only relevant when installing a LOWER firmware.
            if (!IsDowngrade(version)) {
                prompt_install_confirm(false);
                return;
            }

            // downloaded firmware already warned before fetching it, so
            // the warning is not repeated here.
            if (acked_downgrade_fix.has_value()) {
                prompt_install_confirm(*acked_downgrade_fix);
                return;
            }

            // manual install or initially unknown download: warn immediately after validation.
            if (!PromptDowngradeAck(version, "Continue"_i18n,
                    [prompt_install_confirm](bool apply_fix) {
                        prompt_install_confirm(apply_fix);
                    },
                    [origin_zip]() {
                        if (origin_zip) {
                            detail::CleanupManualFirmwareStaging();
                        }
                    })) {
                prompt_install_confirm(false);
            }
        });
}

void Menu::InstallFirmware(const std::string& display_name, const fs::FsPath& path, bool apply_downgrade_fix, std::optional<fs::FsPath> origin_zip) {
    auto fix = std::make_shared<DowngradeFixResult>();

    App::Push<ProgressBox>(0, "Updating Firmware"_i18n, display_name,
        [path, apply_downgrade_fix, fix](auto pbox) -> Result {
            FirmwareValidation validation{};
            R_TRY(detail::ValidateFirmware(&validation, path));
            const bool use_exfat = validation.info.exfat_supported &&
                                   R_SUCCEEDED(validation.validation.exfat_result);
            return detail::InstallValidatedFirmware(pbox, use_exfat, path, apply_downgrade_fix, fix.get());
        },
        [apply_downgrade_fix, fix, origin_zip](Result rc) {
            if (R_FAILED(rc)) {
                if (origin_zip) {
                    detail::CleanupManualFirmwareStaging();
                }
                App::Push<ErrorBox>(rc, "Firmware update failed");
                return;
            }

            auto prompt_reboot = [apply_downgrade_fix, fix]() {
                if (apply_downgrade_fix && fix->staged) {
                    std::string message = "Firmware downgrade installed successfully.\n\nConsole will reboot to TegraExplorer to automatically apply the downgrade fix (delete system save 8000000000000073).\n\nReboot now?";
                    App::Push<OptionBox>(
                        message,
                        "Later"_i18n, "Reboot"_i18n, 1,
                        [](auto op_index) {
                            if (op_index && *op_index == 1) {
                                fs::FsPath te_bin;
                                if (utils::findTegraExplorerPayload(te_bin)) {
                                    utils::rebootToPayload(static_cast<const char*>(te_bin));
                                } else {
                                    utils::requestForcedReboot();
                                }
                            }
                        });
                    return;
                }

                std::string message = "Firmware update applied successfully.";
                const auto fix_note = detail::DescribeDowngradeFix(*fix);
                if (!fix_note.empty()) {
                    message += "\n\n" + fix_note;
                }
                message += "\n\nReboot now?";

                App::Push<OptionBox>(
                    message,
                    "Later"_i18n, "Reboot"_i18n, 1,
                    [](auto op_index) {
                        if (op_index && *op_index == 1) {
                            utils::requestForcedReboot();
                        }
                    });
            };

            if (origin_zip) {
                const auto zip = *origin_zip;
                std::string zip_name = zip.s;
                if (const auto slash = zip_name.find_last_of('/'); slash != std::string::npos) {
                    zip_name = zip_name.substr(slash + 1);
                }
                std::string zip_msg = "Delete original firmware archive?\n\n"_i18n + zip_name;
                App::Push<OptionBox>(
                    zip_msg,
                    "Keep"_i18n, "Delete"_i18n, 0,
                    [zip, prompt_reboot = std::move(prompt_reboot)](auto op_index) {
                        if (op_index && *op_index == 1) {
                            fs::FsNativeSd fs;
                            if (R_SUCCEEDED(fs.GetFsOpenResult())) {
                                if (fs.FileExists(zip)) {
                                    fs.DeleteFile(zip);
                                    fs.Commit();
                                }
                            }
                        }
                        prompt_reboot();
                    });
            } else {
                prompt_reboot();
            }
        }, false);
}

void Menu::UpdateSubheading() {
    const auto count = detail::SelectableCount(m_entries);
    if (m_entries.empty() || !count) {
        this->SetSubHeading("0 / 0");
        return;
    }

    const auto& entry = m_entries[m_index];
    this->SetSubHeading(std::to_string(detail::SelectablePosition(m_entries, m_index)) + " / " + std::to_string(count) + " - " + detail::TypeLabel(entry.type));
}

void Menu::RefreshSystemInfo() {
    m_current_kefir = detail::ReadFirstLine(KEFIR_VERSION_PATH);
    m_supported_firmware = detail::ReadCurrentKefirSupportedFirmware();
    m_current_firmware = hats::getSystemFirmware();
    m_console_revision = hats::isErista() ? "Erista (v1)" : "Mariko (v2)";
    if (m_latest_kefir.empty()) {
        m_latest_kefir = "Unknown";
    }
}

bool Menu::IsDowngrade(const std::string& target_version) const {
    return detail::IsVersionLower(target_version, m_current_firmware);
}

bool Menu::IsFirmwareSupported(const std::string& target_version) const {
    if (!detail::IsKnownVersion(m_supported_firmware)) {
        return true;
    }

    return !detail::IsVersionLower(m_supported_firmware, target_version);
}

bool Menu::IsKefirUpdate(const UpdaterEntry& entry) const {
    if (entry.type != UpdaterEntryType::Kefir) {
        return false;
    }
    if (!detail::IsKnownVersion(m_current_kefir)) {
        return false;
    }
    const auto target = detail::ExtractKefirVersion(entry.name, entry.url);
    if (!detail::IsKnownVersion(target)) {
        return false;
    }
    return detail::IsVersionLower(m_current_kefir, target);
}

bool Menu::FindKefirUpdate(UpdaterEntry& out) const {
    for (const auto& entry : m_entries) {
        if (entry.type == UpdaterEntryType::Kefir && entry.pack) {
            out = entry;
            return true;
        }
    }

    for (const auto& entry : m_entries) {
        if (entry.type == UpdaterEntryType::Kefir) {
            out = entry;
            return true;
        }
    }

    return false;
}

void Menu::PromptKefirThenFirmware(const UpdaterEntry& firmware_entry) {
    UpdaterEntry kefir_entry;
    if (!FindKefirUpdate(kefir_entry)) {
        App::Push<ErrorBox>(0x1, "Firmware " + firmware_entry.name + " is not supported by the current Kefir, and no Kefir update entry was found.");
        return;
    }

    App::Push<OptionBox>(
        detail::FirmwareUnsupportedReason(firmware_entry.name, m_supported_firmware),
        "Cancel"_i18n, "Update Kefir"_i18n, 1,
        [this, kefir_entry, firmware_entry](auto op_index) {
            if (!op_index || *op_index != 1) {
                return;
            }

            InstallKefir(kefir_entry, [this, firmware_entry]() {
                DownloadFirmware(firmware_entry, true);
            });
        });

}

} // namespace sphaira::ui::menu::kefir
