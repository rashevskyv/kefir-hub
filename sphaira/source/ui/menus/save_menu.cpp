#include "app.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "download.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "location.hpp"
#include "image.hpp"
#include "threaded_file_transfer.hpp"
#include "minizip_helper.hpp"
#include "dumper.hpp"

#include "ui/menus/save_menu.hpp"
#include "ui/menus/filebrowser.hpp"
#include "ui/menus/file_picker.hpp"

#include "ui/sidebar.hpp"
#include "ui/error_box.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/nvg_util.hpp"

#include "ui/layout.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "ui/menus/save/save_locations.hpp"
#include "ui/menus/save/save_menu_detail.hpp"

#include "yati/nx/ncm.hpp"
#include "yati/nx/nca.hpp"

#include <utility>
#include <cstring>
#include <algorithm>
#include <set>
#include <minIni.h>
#include <minizip/unzip.h>
#include <minizip/zip.h>

namespace sphaira::ui::menu::save {
namespace {

constexpr float TAB_BAR_X = 40.f;
constexpr float TAB_BAR_W = 1200.f;
constexpr float TAB_BAR_TOP = 94.f;
constexpr float TAB_BAR_H = 44.f;
constexpr float TAB_BAR_GAP = 4.f;
constexpr float TAB_BAR_COUNT = 3.f;
constexpr float TAB_ITEM_W = (TAB_BAR_W - TAB_BAR_GAP * (TAB_BAR_COUNT - 1.f)) / TAB_BAR_COUNT;
constexpr Vec4 TAB_BAR_RECT{TAB_BAR_X, TAB_BAR_TOP, TAB_BAR_W, TAB_BAR_H};

constexpr auto ENTRY_CHUNK_COUNT = 1000;

constinit UEvent g_change_uevent;
constexpr std::array<u8, 7> SAVE_TYPE_VALUES{
    FsSaveDataType_System,
    FsSaveDataType_Account,
    FsSaveDataType_Bcat,
    FsSaveDataType_Device,
    FsSaveDataType_Temporary,
    FsSaveDataType_Cache,
    FsSaveDataType_SystemBcat,
};

void GetFsSaveAttr(const AccountProfileBase& acc, u8 data_type, FsSaveDataSpaceId& space_id, FsSaveDataFilter& filter) {
    std::memset(&filter, 0, sizeof(filter));

    space_id = FsSaveDataSpaceId_User;
    filter.attr.save_data_type = data_type;
    filter.filter_by_save_data_type = true;

    switch (data_type) {
        case FsSaveDataType_System:
        case FsSaveDataType_SystemBcat:
            space_id = FsSaveDataSpaceId_System;
            break;
        case FsSaveDataType_Account:
            space_id = FsSaveDataSpaceId_User;
            filter.attr.uid = acc.uid;
            filter.filter_by_user_id = true;
            break;
        case FsSaveDataType_Bcat:
        case FsSaveDataType_Device:
            space_id = FsSaveDataSpaceId_User;
            break;
        case FsSaveDataType_Temporary:
            space_id = FsSaveDataSpaceId_Temporary;
            break;
        case FsSaveDataType_Cache:
            space_id = FsSaveDataSpaceId_SdUser;
            break;
    }
}


void FreeEntry(NVGcontext* vg, Entry& e) {
    nvgDeleteImage(vg, e.image);
    e.image = 0;
}

// a thick border drawn *inside* the tile that fades from the given colour at
// the edge to fully transparent toward the centre, so the category of a save
// (deleted / backup) reads at a glance without a hard outer frame.
void DrawInnerBorder(NVGcontext* vg, const Vec4& v, const NVGcolor& col, float thickness, float radius) {
    const auto transparent = nvgRGBAf(col.r, col.g, col.b, 0.f);
    // box gradient: inside the inset rectangle -> transparent, blending out to
    // the solid colour over `thickness` toward the tile edge.
    const auto paint = nvgBoxGradient(vg,
        v.x + thickness, v.y + thickness, v.w - thickness * 2.f, v.h - thickness * 2.f,
        radius, thickness, transparent, col);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, v.x, v.y, v.w, v.h, radius);
    nvgFillPaint(vg, paint);
    nvgFill(vg);
}

auto FormatSaveTypeLabel(u8 data_type) -> std::string {
    if (data_type == FsSaveDataType_Account) {
        return "Account";
    }
    return i18n::get(GetSaveTypeLabel(data_type));
}

// right-hand column of a list row, DBI-style: save category in brackets, then
// its allocated size (backup rows show archive count when > 1).
auto FormatListInfo(const Entry& e) -> std::string {
    const std::string label = "[" + FormatSaveTypeLabel(e.save_data_type) + "]";
    if (e.is_backup) {
        return e.backup_count > 1 ? label + "  " + std::to_string(e.backup_count) + " archives" : label;
    }
    return e.size ? label + "  " + grid::FormatBytes(e.size) : label;
}

} // namespace

void SignalChange() {
    ueventSignal(&g_change_uevent);
}

Menu::Menu(u32 flags, u64 app_id_filter, Category category)
: grid::Menu{
    !app_id_filter ? "Saves"_i18n :
    (category == Category::Installed) ? "Installed Games"_i18n :
    (category == Category::Deleted)   ? "Deleted Games"_i18n :
    (category == Category::Backups)   ? "Backups"_i18n : "Saves"_i18n,
    flags
}
, m_app_id_filter{app_id_filter}
, m_category{category}
{
    this->SetActions(
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            if (m_selected_count) {
                ClearSelection();
            } else {
                SetPop();
            }
        }}),
        std::make_pair(Button::A, Action{"Actions"_i18n, [this](){
            PromptSaveAction();
        }}),
        std::make_pair(Button::X, Action{"Select"_i18n, [this](){
            ToggleCurrentSelection();
        }}),
        std::make_pair(Button::Y, Action{"Invert"_i18n, [this](){
            InvertSelection();
        }}),
        std::make_pair(Button::START, Action{"Options"_i18n, [this](){
            DisplaySaveOptions();
        }})
    );

    if (!m_app_id_filter) {
        this->SetAction(Button::L, Action{"Previous tab"_i18n, [this](){
            ChangeCategory(-1);
        }});
        this->SetAction(Button::R, Action{"Next tab"_i18n, [this](){
            ChangeCategory(1);
        }});
    }

    OnLayoutChange();
    nsInitialize();

    m_accounts = App::GetAccountList();

    // try and find the last / default account and set that.
    AccountUid uid{};
    if (R_FAILED(accountTrySelectUserWithoutInteraction(&uid, false))) {
        accountGetLastOpenedUser(&uid);
    }

    const auto it = std::ranges::find_if(m_accounts, [&uid](auto& e){
        return !std::memcmp(&uid, &e.uid, sizeof(uid));
    });

    if (it != m_accounts.end()) {
        m_account_index = std::distance(m_accounts.begin(), it);
        log_write("[SAVE] found account uid at: %zu\n", m_account_index);
    } else {
        log_write("[SAVE] account uid is not found: 0x%016lX%016lX\n", uid.uid[0], uid.uid[1]);
    }

    m_account_enabled.assign(m_accounts.size(), false);
    if (!m_account_enabled.empty()) {
        m_account_enabled[m_account_index] = true;
    }
    m_save_type_enabled.fill(false);
    m_save_type_enabled[SaveTypeIndex(FsSaveDataType_Account)] = true;

    // filtered to a single game: the account/type filters would only hide parts
    // of the very thing the caller asked to see, so open them all up.
    if (m_app_id_filter) {
        m_all_accounts = true;
        m_account_enabled.assign(m_accounts.size(), true);
        m_save_type_enabled.fill(true);
        // System is exclusive in GetSelectedSaveTypes and never belongs to a
        // game anyway.
        m_save_type_enabled[SaveTypeIndex(FsSaveDataType_System)] = false;
    }

    title::Init();
    ueventCreate(&g_change_uevent, true);
}

Menu::~Menu() {
    title::Exit();

    FreeEntries();
    nsExit();
}

void Menu::MarkFiltersChanged() {
    m_dirty = true;
}

auto Menu::GetRecentBackupDirs() -> std::vector<RecentBackupDir> {
    return LoadRecentBackupDirs();
}

void Menu::AddRecentBackupDir(const RecentBackupDir& dir) {
    PushRecentBackupDir(dir);
}

void Menu::ToggleCurrentSelection() {
    if (m_entries.empty()) {
        return;
    }

    m_entries[m_index].selected ^= 1;
    if (m_entries[m_index].selected) {
        m_selected_count++;
    } else {
        m_selected_count--;
    }

    // step onto the next entry, the way the file browser does.
    if (m_index + 1 < static_cast<s64>(m_entries.size())) {
        SetIndex(m_index + 1);
        const auto g = ComputeGridSections();
        m_list->EnsureVisible(EntryToDisplay(m_index, g), g.display_count);
    }
}

void Menu::InvertSelection() {
    if (m_entries.empty()) {
        return;
    }

    m_selected_count = 0;
    for (auto& e : m_entries) {
        e.selected ^= 1;
        if (e.selected) {
            m_selected_count++;
        }
    }
}

void Menu::ChangeCategory(s64 delta) {
    if (m_app_id_filter) {
        return;
    }

    constexpr std::array<Category, 3> categories{
        Category::Installed,
        Category::Deleted,
        Category::Backups,
    };

    s64 current_idx = -1;
    for (size_t i = 0; i < categories.size(); i++) {
        if (m_category == categories[i]) {
            current_idx = static_cast<s64>(i);
            break;
        }
    }

    s64 next_idx = 0;
    if (current_idx >= 0) {
        const auto count = static_cast<s64>(categories.size());
        next_idx = (current_idx + delta + count) % count;
    } else {
        next_idx = (delta > 0) ? 0 : static_cast<s64>(categories.size() - 1);
    }

    SetCategory(categories[next_idx]);
}

void Menu::SetCategory(Category category) {
    if (m_category == category) {
        return;
    }

    m_category = category;
    if (m_app_id_filter) {
        SetTitle(
            (m_category == Category::Installed) ? "Installed Games"_i18n :
            (m_category == Category::Deleted)   ? "Deleted Games"_i18n :
            (m_category == Category::Backups)   ? "Backups"_i18n : "Saves"_i18n
        );
    }
    App::PlaySoundEffect(SoundEffect_Focus);
    ScanHomebrew();
}

auto Menu::GetAccountSummary() const -> std::string {
    if (m_all_accounts) {
        return "All Accounts"_i18n;
    }

    size_t count{};
    std::string first;
    for (size_t i = 0; i < m_account_enabled.size() && i < m_accounts.size(); i++) {
        if (!m_account_enabled[i]) {
            continue;
        }

        if (first.empty()) {
            first = m_accounts[i].nickname;
        }
        count++;
    }

    if (!count) {
        return "None"_i18n;
    }
    if (count == 1) {
        return first;
    }

    return std::to_string(count) + " accounts";
}

auto Menu::GetAccountName(const AccountUid& uid) const -> std::string {
    for (const auto& account : m_accounts) {
        if (!std::memcmp(&uid, &account.uid, sizeof(uid))) {
            return account.nickname;
        }
    }

    char out[0x40];
    std::snprintf(out, sizeof(out), "%016lX%016lX", uid.uid[0], uid.uid[1]);
    return out;
}

auto Menu::GetDataTypeSummary() const -> std::string {
    if (m_save_type_enabled[SaveTypeIndex(FsSaveDataType_System)]) {
        return "System"_i18n;
    }

    size_t count{};
    std::string first;
    for (size_t i = 0; i < SAVE_TYPES.size(); i++) {
        const auto type = SAVE_TYPES[i];
        if (type == FsSaveDataType_System || !m_save_type_enabled[i]) {
            continue;
        }

        if (first.empty()) {
            first = FormatSaveTypeLabel(type);
        }
        count++;
    }

    if (!count) {
        return "None"_i18n;
    }
    if (count == 1) {
        return first;
    }

    return std::to_string(count) + " types";
}

void Menu::DisplayAccountOptions() {
    auto options = std::make_unique<Sidebar>("Accounts"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    options->Add<SidebarEntryCheckbox>(
        "All Accounts"_i18n,
        [this](){ return m_all_accounts; },
        [this](bool enabled) {
            m_all_accounts = enabled;
            if (!m_all_accounts && std::ranges::none_of(m_account_enabled, [](auto v){ return v; }) && !m_account_enabled.empty()) {
                m_account_enabled[m_account_index] = true;
            }
            MarkFiltersChanged();
        }, "Show saves from all user accounts."_i18n);

    for (size_t i = 0; i < m_accounts.size(); i++) {
        auto* entry = options->Add<SidebarEntryCheckbox>(
            m_accounts[i].nickname,
            [this, i](){ return i < m_account_enabled.size() && m_account_enabled[i]; },
            [this, i](bool enabled) {
                if (i >= m_account_enabled.size()) {
                    return;
                }

                m_all_accounts = false;
                m_account_enabled[i] = enabled;
                if (std::ranges::none_of(m_account_enabled, [](auto v){ return v; })) {
                    m_account_enabled[i] = true;
                }
                MarkFiltersChanged();
            }, "Filter saves by this user account."_i18n);

        entry->Depends(
            [this](){ return !m_all_accounts; },
            "All Accounts is enabled."_i18n,
            [this, i]() {
                if (i < m_account_enabled.size()) {
                    m_all_accounts = false;
                    m_account_enabled[i] = true;
                    MarkFiltersChanged();
                }
            });
    }
}

void Menu::DisplayDataTypeOptions() {
    auto options = std::make_unique<Sidebar>("Data Types"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    const auto system_index = SaveTypeIndex(FsSaveDataType_System);
    for (size_t i = 0; i < SAVE_TYPES.size(); i++) {
        const auto type = SAVE_TYPES[i];
        auto* entry = options->Add<SidebarEntryCheckbox>(
            FormatSaveTypeLabel(type),
            [this, i](){ return m_save_type_enabled[i]; },
            [this, i, type, system_index](bool enabled) {
                if (type == FsSaveDataType_System) {
                    m_save_type_enabled[i] = enabled;
                    if (!enabled) {
                        const auto account_index = SaveTypeIndex(FsSaveDataType_Account);
                        bool any{};
                        for (size_t n = 0; n < SAVE_TYPES.size(); n++) {
                            if (n != system_index && m_save_type_enabled[n]) {
                                any = true;
                                break;
                            }
                        }
                        if (!any) {
                            m_save_type_enabled[account_index] = true;
                        }
                    }
                } else {
                    m_save_type_enabled[system_index] = false;
                    m_save_type_enabled[i] = enabled;
                    bool any{};
                    for (size_t n = 0; n < SAVE_TYPES.size(); n++) {
                        if (n != system_index && m_save_type_enabled[n]) {
                            any = true;
                            break;
                        }
                    }
                    if (!any) {
                        m_save_type_enabled[i] = true;
                    }
                }

                MarkFiltersChanged();
            }, "Show or hide saves of this data type."_i18n);

        if (type != FsSaveDataType_System) {
            entry->Depends(
                [this, system_index](){ return !m_save_type_enabled[system_index]; },
                "System is enabled."_i18n,
                [this, i, system_index]() {
                    m_save_type_enabled[system_index] = false;
                    m_save_type_enabled[i] = true;
                    MarkFiltersChanged();
                });
        }
    }
}

void Menu::DisplayShowSavesOptions() {
    auto options = std::make_unique<Sidebar>("Show saves"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    options->Add<SidebarEntryCheckbox>(
        "Installed game saves"_i18n,
        [this](){ return App::GetSaveShowInstalled(); },
        [this](bool enabled){ App::SetSaveShowInstalled(enabled); MarkFiltersChanged(); },
        "Show saves belonging to games that are currently installed."_i18n);

    options->Add<SidebarEntryCheckbox>(
        "Deleted game saves"_i18n,
        [this](){ return App::GetSaveShowDeleted(); },
        [this](bool enabled){ App::SetSaveShowDeleted(enabled); MarkFiltersChanged(); },
        "Show orphaned saves whose game is no longer installed (marked with a grey border)."_i18n);

    options->Add<SidebarEntryCheckbox>(
        "Backups"_i18n,
        [this](){ return App::GetSaveShowBackups(); },
        [this](bool enabled){ App::SetSaveShowBackups(enabled); MarkFiltersChanged(); },
        "Show a tile for every game that has a save backup on the SD card, listed below the divider (marked with a yellow border)."_i18n);
}

void Menu::DisplaySaveOptions() {
    auto options = std::make_unique<Sidebar>("Save Options"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    if (!m_entries.empty()) {
        options->Add<SidebarEntryHeader>("ACTIONS"_i18n);
        options->Add<SidebarEntryCallback>("Backup"_i18n, [this](){
            PromptSaveTypeOptions(SaveOp::Backup);
        }, "Backup selected saves."_i18n)->SetIcon(ActionIcon::Save);
        options->Add<SidebarEntryCallback>("Restore"_i18n, [this](){
            PromptSaveTypeOptions(SaveOp::Restore);
        }, "Restore selected saves."_i18n)->SetIcon(ActionIcon::Refresh);
        options->Add<SidebarEntryCallback>("Delete"_i18n, [this](){
            PromptSaveTypeOptions(SaveOp::Delete);
        }, true, "Permanently delete save data for selected games."_i18n)->SetIcon(ActionIcon::Delete);
    }

    SidebarEntryArray::Items layout_items;
    layout_items.push_back("Icon"_i18n);
    layout_items.push_back("Grid"_i18n);
    layout_items.push_back("HB Menu"_i18n);
    layout_items.push_back("List"_i18n);

    // sidebar row -> LayoutType and back. Saves have no separate Icon layout,
    // so both of the first two rows map onto the grid.
    static const int layout_map_inv[] = {
        3, // LayoutType_List -> index 3
        0, // LayoutType_Grid -> index 0
        0, // LayoutType_GridDetail -> index 0
        2, // LayoutType_HbMenu -> index 2
    };
    const auto cur_layout = m_layout.Get();
    const auto cur_idx = (cur_layout >= 0 && cur_layout < 4) ? layout_map_inv[cur_layout] : 0;

    options->Add<SidebarEntryArray>("Layout"_i18n, layout_items, [this](s64& index_out){
        const int layout_map_local[] = {
            grid::LayoutType_Grid,
            grid::LayoutType_Grid,
            grid::LayoutType_HbMenu,
            grid::LayoutType_List,
        };
        const auto new_layout = layout_map_local[std::clamp<s64>(index_out, 0, 3)];
        m_layout.Set(new_layout);
        OnLayoutChange();
    }, cur_idx, "Choose how saves are displayed on screen."_i18n)->SetIcon(ActionIcon::Layout);

    options->Add<SidebarEntryArray>("Sort"_i18n, SidebarEntryArray::Items{"Updated"_i18n}, [this](s64& index_out){
        m_sort.Set(index_out);
        SortAndFindLastFile(false);
    }, m_sort.Get(), "Select which field to sort saves by."_i18n)->SetIcon(ActionIcon::Sort);

    options->Add<SidebarEntryArray>("Order"_i18n, SidebarEntryArray::Items{"Descending"_i18n, "Ascending"_i18n}, [this](s64& index_out){
        m_order.Set(index_out);
        SortAndFindLastFile(false);
    }, m_order.Get(), "Sort saves from newest to oldest or oldest to newest."_i18n)->SetIcon(ActionIcon::Sort);

    options->Add<SidebarEntryCallback>("Accounts"_i18n, [this](){
        DisplayAccountOptions();
    }, "Filter saves by user account."_i18n);

    options->Add<SidebarEntryCallback>("Data Types"_i18n, [this](){
        DisplayDataTypeOptions();
    }, "Choose which save data types to display."_i18n);

    options->Add<SidebarEntryCallback>("Show saves"_i18n, [this](){
        DisplayShowSavesOptions();
    }, "Choose which categories of saves are shown: installed games, deleted games and backups."_i18n);

    options->Add<SidebarEntryHeader>("SYNC"_i18n);

    options->Add<SidebarEntryCallback>("Sync with remote"_i18n, [this](){
        SyncSavesRemote();
    }, "Two-way sync of backup ZIPs for the currently selected saves with a WebDAV location. Only the backup library on the microSD card is synced: the standard /dumps folder and the DBI folder /switch/DBI/saves. Backups saved to other folders or storage devices are not covered. Every archive missing on the other side is copied: new local backups are uploaded to WebDAV, new remote backups are downloaded to the SD card. Files with the same name are never overwritten and are not compared by date or content. After downloading from remote, use Restore to apply a backup."_i18n);

    options->Add<SidebarEntryCallback>("Advanced"_i18n, [this](){
        auto options = std::make_unique<Sidebar>("Advanced Options"_i18n, Sidebar::Side::RIGHT);
        ON_SCOPE_EXIT(App::Push(std::move(options)));

        options->Add<SidebarEntryBool>("Auto backup on restore"_i18n, App::GetSaveAutoBackupOnRestore(), [](bool& v_out){
            App::SetSaveAutoBackupOnRestore(v_out);
        }, "Automatically create a backup before restoring a save."_i18n);

        options->Add<SidebarEntryBool>("Compress backup"_i18n, App::GetSaveCompressBackup(), [](bool& v_out){
            App::SetSaveCompressBackup(v_out);
        }, "Save backups as compressed ZIP archives to reduce disk space."_i18n);
    }, "Access advanced backup and restore settings."_i18n);
}



void Menu::Update(Controller* controller, TouchInfo* touch) {
    if (R_SUCCEEDED(waitSingle(waiterForUEvent(&g_change_uevent), 0))) {
        m_dirty = true;
    }

    if (m_dirty) {
        App::Notify("Updating application record list");
        SortAndFindLastFile(true);
    }

    MenuBase::Update(controller, touch);

    if (!m_app_id_filter && touch->is_clicked && touch->in_range(TAB_BAR_RECT)) {
        const auto tab = std::clamp<s64>((touch->cur.x - TAB_BAR_X) / (TAB_ITEM_W + TAB_BAR_GAP), 0, 2);
        constexpr std::array<Category, 3> categories{
            Category::Installed,
            Category::Deleted,
            Category::Backups,
        };
        SetCategory(categories[tab]);
        return;
    }

    const auto g = ComputeGridSections();
    const auto start_disp = EntryToDisplay(m_index, g);
    m_list->OnUpdate(controller, touch, start_disp, g.display_count, [this, g, start_disp](bool touch, s64 disp) {
        const auto entry = ResolveDisplay(disp, start_disp, g);
        if (entry < 0) {
            return; // landed on the empty divider gap; nothing to focus there.
        }

        if (touch && m_index == entry) {
            FireAction(Button::A);
        } else {
            App::PlaySoundEffect(SoundEffect_Focus);
            SetIndex(entry);
            // the cursor may have stepped over the divider gap, so nudge the
            // view to keep the newly focused tile visible.
            m_list->EnsureVisible(EntryToDisplay(entry, g), g.display_count);
        }
    }, this);
}

auto Menu::ComputeGridSections() const -> GridSections {
    GridSections g;
    g.row = std::max<s64>(1, m_list ? m_list->GetRow() : 1);
    g.horizontal = m_list && m_list->GetLayout() == List::Layout::HOME;

    const auto total = static_cast<s64>(m_entries.size());
    g.live_count = std::clamp<s64>(m_backup_start, 0, total);
    g.backup_count = total - g.live_count;

    if (g.backup_count > 0 && g.live_count > 0) {
        g.has_backups = true;
        // fill the remainder of the last live row, then add one empty row that
        // hosts the "Backups" divider label.
        g.base_fill = (g.row - g.live_count % g.row) % g.row;
        g.pad = g.base_fill + g.row;
    }

    g.first_backup_display = g.live_count + g.pad;
    g.display_count = g.live_count + g.pad + g.backup_count;
    return g;
}

auto Menu::EntryToDisplay(s64 entry, const GridSections& g) const -> s64 {
    if (entry < g.live_count) {
        return entry;
    }
    return entry + g.pad;
}

auto Menu::DisplayToEntry(s64 display, const GridSections& g) const -> s64 {
    if (display < g.live_count) {
        return display; // live save
    }
    if (display < g.first_backup_display) {
        return -1; // filler / divider gap
    }
    const auto entry = display - g.pad;
    return (entry >= 0 && entry < static_cast<s64>(m_entries.size())) ? entry : -1;
}

auto Menu::ResolveDisplay(s64 display, s64 from, const GridSections& g) const -> s64 {
    const auto entry = DisplayToEntry(display, g);
    if (entry >= 0) {
        return entry;
    }
    // filler: hop to the first backup when moving forward, otherwise back to
    // the last live save (mirrors the settings menu's caption stepping).
    if (display >= from) {
        return g.backup_count > 0 ? g.live_count : -1;
    }
    return g.live_count > 0 ? g.live_count - 1 : -1;
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    if (!m_app_id_filter) {
        DrawCategoryTabs(vg, theme);
    }

    if (m_entries.empty()) {
        const float empty_y = !m_app_id_filter ? (TAB_BAR_TOP + TAB_BAR_H + layout::FOOTER_LINE_Y) * 0.5f : (GetY() + GetH() / 2.f);
        gfx::drawTextArgs(vg, GetX() + GetW() / 2.f, empty_y, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Empty..."_i18n.c_str());
        return;
    }

    if (m_layout.Get() == grid::LayoutType_HbMenu) {
        auto& e = m_entries[m_index];
        u64 id = IsSystemLikeSave(e.save_data_type) ? e.system_save_data_id : e.application_id;
        char title_id[33];
        std::snprintf(title_id, sizeof(title_id), "%016lX", id);
        
        const auto account = e.is_backup ?
            FormatBackupAccount(e, m_accounts) :
            ((e.save_data_type == FsSaveDataType_Account && !m_all_accounts) ?
                GetAccountName(e.uid) : GetAccountSummary());

        const auto author_text = e.is_backup ?
            FormatBackupTimestamp(e.backup_timestamp) : std::string{e.GetAuthor()};

        if (!m_app_id_filter) {
            nvgSave(vg);
            nvgTranslate(vg, 0.f, 26.f);
            DrawHbMenuHeader(vg, theme, e.image, e.GetName(), author_text.c_str(), title_id, account.c_str());
            nvgRestore(vg);
        } else {
            DrawHbMenuHeader(vg, theme, e.image, e.GetName(), author_text.c_str(), title_id, account.c_str());
        }
    }

    // max images per frame, in order to not hit io / gpu too hard.
    const int image_load_max = 2;
    int image_load_count = 0;

    BackupColumnLayout backup_cols{};
    if (m_layout.Get() == grid::LayoutType_List) {
        nvgFontSize(vg, 15.f);
        nvgTextAlign(vg, NVG_ALIGN_LEFT);
        float bounds[4]{};

        for (const auto& e : m_entries) {
            if (!e.is_backup) {
                continue;
            }
            const auto cols = GetBackupSecondaryColumns(e, m_accounts);
            if (!cols.title_id.empty()) {
                gfx::textBounds(vg, 0, 0, bounds, cols.title_id.c_str());
                backup_cols.max_title_w = std::max(backup_cols.max_title_w, bounds[2] - bounds[0]);
            }
            if (!cols.account.empty()) {
                gfx::textBounds(vg, 0, 0, bounds, cols.account.c_str());
                backup_cols.max_account_w = std::max(backup_cols.max_account_w, bounds[2] - bounds[0]);
            }
            if (!cols.timestamp.empty()) {
                gfx::textBounds(vg, 0, 0, bounds, cols.timestamp.c_str());
                backup_cols.max_date_w = std::max(backup_cols.max_date_w, bounds[2] - bounds[0]);
            }
        }
    }

    const auto g = ComputeGridSections();
    m_list->Draw(vg, theme, g.display_count, EntryToDisplay(m_index, g), [this, &image_load_count, g, &backup_cols](NVGcontext* vg, Theme* theme, Vec4 v, s64 disp) {
        const auto entry = DisplayToEntry(disp, g);
        if (entry < 0) {
            return; // empty divider gap; the label is drawn with the first backup tile.
        }
        auto& e = m_entries[entry];

        if (e.status == title::NacpLoadStatus::None) {
            if (!IsSystemLikeSave(e.save_data_type)) {
                title::PushAsync(e.application_id);
                e.status = title::NacpLoadStatus::Progress;
            } else {
                detail::FakeNacpEntryForSystem(e);
            }
        } else if (e.status == title::NacpLoadStatus::Progress) {
            detail::LoadResultIntoEntry(e, title::GetAsync(e.application_id));
        }

        // lazy load image
        if (image_load_count < image_load_max) {
            if (detail::LoadControlImage(e, title::GetAsync(e.application_id))) {
                image_load_count++;
            }
        }

        const auto selected = entry == m_index;
        Vec4 image_v = v;
        const auto info = (m_layout.Get() == grid::LayoutType_List || m_layout.Get() == grid::LayoutType_GridDetail) ? FormatListInfo(e) : std::string{};
        const bool is_list = (m_layout.Get() == grid::LayoutType_List);
        const auto author_str = (e.is_backup && is_list)
            ? std::string{}
            : (e.is_backup ? FormatBackupSecondaryText(e, m_accounts) : std::string{e.GetAuthor()});

        if (!IsSystemLikeSave(e.save_data_type)) {
            image_v = DrawEntry(vg, theme, m_layout.Get(), v, selected, e.image, e.GetName(), author_str.c_str(), info.c_str(), e.selected);
        } else {
            image_v = DrawEntryNoImage(vg, theme, m_layout.Get(), v, selected, e.GetName(), author_str.c_str(), info.c_str(), e.selected);
            gfx::drawRect(vg, v, theme->GetColour(ThemeEntryID_GRID), 5);
            gfx::drawTextArgs(vg, image_v.x + image_v.w / 2, image_v.y + image_v.w / 2, 20, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT), detail::GetSystemSaveName(e.system_save_data_id));
        }

        if (e.is_backup && is_list) {
            DrawBackupSecondaryColumns(vg, theme, v, image_v, e, backup_cols, info.c_str());
        }

        // grey for deleted-game saves, yellow for backups, nothing otherwise.
        // framed on the whole tile so it reads in every layout.
        DrawCategoryBorder(vg, theme, v, e);

        DrawSelectionMark(vg, theme, m_layout.Get(), v, image_v, e.selected, m_selected_count > 0);

        // the "Backups" divider rides above the first backup tile, filling the
        // empty row reserved for it in ComputeGridSections().
        if (g.has_backups && disp == g.first_backup_display) {
            DrawSectionDivider(vg, theme, v, g);
        }
    });
}

void Menu::DrawBackupSecondaryColumns(NVGcontext* vg, Theme* theme, const Vec4& v, const Vec4& image_v, const Entry& e, const BackupColumnLayout& layout, const char* info) const {
    const float text_x = image_v.x + image_v.w + 14.f;
    const float text_y = v.y + v.h / 2.f + 12.f;
    const float text_clip_w = GetListTextClipWidth(vg, v, text_x, info);
    if (text_clip_w <= 0.f) {
        return;
    }

    const auto cols = GetBackupSecondaryColumns(e, m_accounts);
    const auto col = theme->GetColour(ThemeEntryID_TEXT_INFO);

    nvgSave(vg);
    nvgIntersectScissor(vg, text_x, 0.f, text_clip_w, SCREEN_HEIGHT);

    float cur_x = text_x;
    if (!cols.title_id.empty()) {
        gfx::drawText(vg, cur_x, text_y, 15.f, col, cols.title_id.c_str(), NVG_ALIGN_LEFT);
    }
    cur_x = text_x + layout.max_title_w;

    if (!cols.account.empty()) {
        gfx::drawText(vg, cur_x, text_y, 15.f, col, cols.account.c_str(), NVG_ALIGN_LEFT);
    }
    cur_x = cur_x + layout.max_account_w;

    if (!cols.timestamp.empty()) {
        gfx::drawText(vg, cur_x, text_y, 15.f, col, cols.timestamp.c_str(), NVG_ALIGN_LEFT);
    }
    cur_x = cur_x + layout.max_date_w;

    if (!cols.archive_count.empty()) {
        gfx::drawText(vg, cur_x, text_y, 15.f, col, cols.archive_count.c_str(), NVG_ALIGN_LEFT);
    }

    nvgRestore(vg);
}

void Menu::DrawCategoryBorder(NVGcontext* vg, Theme* theme, const Vec4& v, const Entry& e) {
    NVGcolor col;
    if (e.is_backup) {
        col = nvgRGB(0xF2, 0xC5, 0x22); // yellow: backup archive
    } else if (IsSystemLikeSave(e.save_data_type)) {
        return; // system saves are not games; leave them unframed
    } else if (m_installed_app_ids.contains(e.application_id)) {
        return; // installed game: ordinary save, no border
    } else {
        col = nvgRGB(0x9A, 0x9A, 0x9A); // grey: deleted-game save
    }

    const float thickness = (m_layout.Get() == grid::LayoutType_List) ? 2.f : 12.f;
    DrawInnerBorder(vg, v, col, thickness, 5.f);
}

void Menu::DrawSectionDivider(NVGcontext* vg, Theme* theme, const Vec4& first_backup_v, const GridSections& g) const {
    const auto label = "Backups"_i18n;
    const auto text_col = theme->GetColour(ThemeEntryID_TEXT_INFO);
    const auto line_col = theme->GetColour(ThemeEntryID_LINE_SEPARATOR);

    if (g.horizontal) {
        // sideways layout: a vertical rule in the empty column, label on top.
        const float cx = first_backup_v.x - m_list->GetMaxX() + first_backup_v.w / 2.f;
        gfx::drawRect(vg, cx - 1.f, first_backup_v.y, 2.f, first_backup_v.h, line_col);
        gfx::drawText(vg, cx, first_backup_v.y - 8.f, 20.f, text_col, label.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
        return;
    }

    // vertical layout: a full-width rule centred in the empty row above the
    // first backup tile, with the label sitting in a gap in the middle.
    const float cy = first_backup_v.y - m_list->GetMaxY() + first_backup_v.h / 2.f;
    const float dl = m_list->GetX();
    const float dr = m_list->GetX() + m_list->GetW();
    const float mid = (dl + dr) / 2.f;
    constexpr float font = 22.f;

    float bounds[4];
    nvgFontSize(vg, font);
    gfx::textBounds(vg, 0, 0, bounds, label.c_str());
    const float half_w = (bounds[2] - bounds[0]) / 2.f;
    constexpr float gap = 16.f;

    gfx::drawRect(vg, dl, cy - 1.f, std::max(0.f, (mid - half_w - gap) - dl), 2.f, line_col);
    const float rx = mid + half_w + gap;
    gfx::drawRect(vg, rx, cy - 1.f, std::max(0.f, dr - rx), 2.f, line_col);
    gfx::drawText(vg, mid, cy, font, text_col, label.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
}

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();
    if (m_entries.empty()) {
        ScanHomebrew();
    }
}

void Menu::SetIndex(s64 index) {
    if (m_entries.empty()) {
        m_index = 0;
        this->SetSubHeading("0 / 0");
        SetTitleSubHeading(GetAccountSummary() + " | " + GetDataTypeSummary(), true);
        return;
    }

    if (index < 0) {
        index = 0;
    } else if (index >= static_cast<s64>(m_entries.size())) {
        index = m_entries.size() - 1;
    }

    m_index = index;
    if (!m_index) {
        m_list->SetYoff(0);
    }

    u64 id{};
    if (!m_entries.empty()) {
        if (IsSystemLikeSave(m_entries[m_index].save_data_type)) {
            id = m_entries[m_index].system_save_data_id;
        } else {
            id = m_entries[m_index].application_id;
        }

        this->SetSubHeading(std::to_string(m_index + 1) + " / " + std::to_string(m_entries.size()));
    } else {
        this->SetSubHeading("0 / 0");
    }

    const auto account = (m_entries[m_index].save_data_type == FsSaveDataType_Account && !m_all_accounts) ?
        GetAccountName(m_entries[m_index].uid) : GetAccountSummary();

    char title[0x80];
    std::snprintf(title, sizeof(title), "%s | %s | %016lX", account.c_str(), GetSaveTypeLabel(m_entries[m_index].save_data_type), id);
    SetTitleSubHeading(title, true);
}

auto Menu::ListAccountSaves(const AccountUid& uid) -> std::vector<Entry> {
    std::vector<Entry> out;
    const auto infos = DiscoverSaveDataInfo(&uid, FsSaveDataType_Account);
    out.reserve(infos.size());
    for (const auto& info : infos) {
        out.emplace_back(info);
    }
    return out;
}

void Menu::ReadSaveEntries(u8 data_type, s64 account_index, std::vector<Entry>& out) const {
    if (data_type == FsSaveDataType_Account) {
        if (m_accounts.empty()) {
            return;
        }
        const auto index = account_index >= 0 ? account_index : 0;
        if (index >= static_cast<s64>(m_accounts.size())) {
            return;
        }
        const auto infos = DiscoverSaveDataInfo(&m_accounts[index].uid, FsSaveDataType_Account);
        for (const auto& info : infos) {
            out.emplace_back(info);
        }
    } else {
        const auto infos = DiscoverSaveDataInfo(nullptr, data_type);
        for (const auto& info : infos) {
            out.emplace_back(info);
        }
    }
}

auto Menu::GetSelectedAccountIndexes() const -> std::vector<s64> {
    std::vector<s64> out;
    if (m_all_accounts) {
        for (s64 i = 0; i < static_cast<s64>(m_accounts.size()); i++) {
            out.emplace_back(i);
        }
        return out;
    }

    for (s64 i = 0; i < static_cast<s64>(m_account_enabled.size()); i++) {
        if (m_account_enabled[i]) {
            out.emplace_back(i);
        }
    }

    if (out.empty() && !m_accounts.empty()) {
        out.emplace_back(m_account_index);
    }
    return out;
}

auto Menu::GetSelectedSaveTypes() const -> std::vector<u8> {
    std::vector<u8> out;
    if (m_save_type_enabled[SaveTypeIndex(FsSaveDataType_System)]) {
        out.emplace_back(FsSaveDataType_System);
        return out;
    }

    for (size_t i = 0; i < SAVE_TYPES.size(); i++) {
        if (SAVE_TYPES[i] != FsSaveDataType_System && m_save_type_enabled[i]) {
            out.emplace_back(SAVE_TYPES[i]);
        }
    }

    if (out.empty()) {
        out.emplace_back(FsSaveDataType_Account);
    }
    return out;
}

void Menu::ScanHomebrew() {
    TimeStamp ts;

    FreeEntries();
    ClearSelection();
    ueventClear(&g_change_uevent);
    m_entries.reserve(ENTRY_CHUNK_COUNT);
    m_is_reversed = false;
    m_dirty = false;

    if (!m_accounts.empty() && m_account_enabled.size() != m_accounts.size()) {
        m_account_enabled.assign(m_accounts.size(), false);
        if (m_account_index < static_cast<s64>(m_accounts.size())) {
            m_account_enabled[m_account_index] = true;
        }
    }

    const auto account_indexes = GetSelectedAccountIndexes();
    for (const auto type : GetSelectedSaveTypes()) {
        if (type == FsSaveDataType_Account) {
            for (const auto account_index : account_indexes) {
                ReadSaveEntries(type, account_index, m_entries);
            }
        } else {
            ReadSaveEntries(type, -1, m_entries);
        }
    }

    std::vector<Entry> grouped;
    std::vector<std::string> keys;
    grouped.reserve(m_entries.size());
    keys.reserve(m_entries.size());
    for (auto& e : m_entries) {
        const auto key = DisplayEntryKey(e);
        const auto it = std::ranges::find(keys, key);
        if (it == keys.end()) {
            keys.emplace_back(key);
            grouped.emplace_back(e);
            continue;
        }

        const auto index = std::distance(keys.begin(), it);
        if (grouped[index].save_data_type != FsSaveDataType_Account && e.save_data_type == FsSaveDataType_Account) {
            grouped[index] = e;
        }
    }

    // classify live saves as installed vs deleted-game, and drop whichever the
    // "Show saves" filter has turned off. system saves are governed by the Data
    // Types filter instead, so they are always kept here.
    BuildInstalledAppIds();
    bool show_installed = App::GetSaveShowInstalled();
    bool show_deleted = App::GetSaveShowDeleted();
    bool show_backups = App::GetSaveShowBackups();

    if (m_category == Category::Installed) {
        show_installed = true;
        show_deleted = false;
        show_backups = false;
    } else if (m_category == Category::Deleted) {
        show_installed = false;
        show_deleted = true;
        show_backups = false;
    } else if (m_category == Category::Backups) {
        show_installed = false;
        show_deleted = false;
        show_backups = true;
    }

    m_entries.clear();
    m_entries.reserve(grouped.size() + m_installed_apps.size());

    if (m_category == Category::Installed) {
        std::unordered_map<u64, Entry> live_saves;
        for (const auto& e : grouped) {
            if (!IsSystemLikeSave(e.save_data_type)) {
                live_saves.emplace(e.application_id, e);
            }
        }

        for (const auto app_id : m_installed_apps) {
            if (m_app_id_filter && app_id != m_app_id_filter) {
                continue;
            }

            auto it = live_saves.find(app_id);
            if (it != live_saves.end()) {
                m_entries.emplace_back(it->second);
            } else {
                Entry e{};
                e.application_id = app_id;
                e.save_data_type = FsSaveDataType_Account;
                e.is_backup = false;
                m_entries.emplace_back(e);
            }
        }
    } else if (m_category == Category::Deleted) {
        for (const auto& e : grouped) {
            if (m_app_id_filter && e.application_id != m_app_id_filter) {
                continue;
            }
            if (IsSystemLikeSave(e.save_data_type)) {
                continue;
            }
            if (!m_installed_app_ids.contains(e.application_id)) {
                m_entries.emplace_back(e);
            }
        }
    } else if (m_category == Category::Backups) {
        // Backups only
    } else {
        std::unordered_set<u64> added_installed;
        for (const auto& e : grouped) {
            if (m_app_id_filter && e.application_id != m_app_id_filter) {
                continue;
            }

            if (IsSystemLikeSave(e.save_data_type)) {
                m_entries.emplace_back(e);
                continue;
            }

            const bool installed = m_installed_app_ids.contains(e.application_id);
            if (installed) {
                if (show_installed) {
                    m_entries.emplace_back(e);
                    added_installed.insert(e.application_id);
                }
            } else {
                if (show_deleted) {
                    m_entries.emplace_back(e);
                }
            }
        }

        if (show_installed) {
            for (const auto app_id : m_installed_apps) {
                if (m_app_id_filter && app_id != m_app_id_filter) {
                    continue;
                }
                if (!added_installed.contains(app_id)) {
                    Entry e{};
                    e.application_id = app_id;
                    e.save_data_type = FsSaveDataType_Account;
                    e.is_backup = false;
                    m_entries.emplace_back(e);
                }
            }
        }
    }

    // backup tiles come after every live save; remember the boundary so the
    // grid can split the two sections with the "Backups" divider.
    m_backup_start = static_cast<s64>(m_entries.size());
    bool has_uninstalled = false;
    for (const auto& e : m_entries) {
        if (!IsSystemLikeSave(e.save_data_type) && !m_installed_app_ids.contains(e.application_id)) {
            has_uninstalled = true;
            break;
        }
    }

    std::vector<Entry> backups;
    if (show_backups || has_uninstalled) {
        ReadBackupEntries(backups);
    }

    std::unordered_map<u64, std::string> backup_name_lookup;
    for (const auto& b : backups) {
        if (b.application_id && !IsSystemLikeSave(b.save_data_type) && !backup_name_lookup.contains(b.application_id)) {
            if (b.lang.name[0] != '\0' && !title::IsPlaceholderName(b.lang.name)) {
                backup_name_lookup.emplace(b.application_id, b.lang.name);
            } else if (!b.dbi_game_dir.empty() && !IsHex16(b.dbi_game_dir)) {
                backup_name_lookup.emplace(b.application_id, b.dbi_game_dir);
            }
        }
    }

    if (show_backups) {
        for (auto& b : backups) {
            if (m_app_id_filter && b.application_id != m_app_id_filter) {
                continue;
            }
            if (!m_all_accounts && b.save_data_type == FsSaveDataType_Account) {
                bool match = false;
                for (const auto idx : account_indexes) {
                    if (idx >= 0 && idx < static_cast<s64>(m_accounts.size())) {
                        if (!std::memcmp(&b.uid, &m_accounts[idx].uid, sizeof(AccountUid))) {
                            match = true;
                            break;
                        }
                    }
                }
                if (!match) {
                    continue;
                }
            }
            m_entries.emplace_back(std::move(b));
        }
    }

    for (auto& e : m_entries) {
        if (!IsSystemLikeSave(e.save_data_type) && !m_installed_app_ids.contains(e.application_id)) {
            if (e.lang.name[0] == '\0') {
                const auto it = backup_name_lookup.find(e.application_id);
                if (it != backup_name_lookup.end() && !it->second.empty()) {
                    std::strncpy(e.lang.name, it->second.c_str(), sizeof(e.lang.name) - 1);
                    e.lang.name[sizeof(e.lang.name) - 1] = '\0';
                } else {
                    std::snprintf(e.lang.name, sizeof(e.lang.name), "Title %016lX", e.application_id);
                }
            }
            e.lang.author[0] = '\0';
        }
    }

    log_write("games found: %zu time_taken: %.2f seconds %zu ms %zu ns\n", m_entries.size(), ts.GetSecondsD(), ts.GetMs(), ts.GetNs());
    this->Sort();
    SetIndex(0);
    ClearSelection();
}

void Menu::BuildInstalledAppIds() {
    m_installed_app_ids.clear();
    m_installed_apps.clear();

    std::vector<NsApplicationRecord> records(ENTRY_CHUNK_COUNT);
    s32 offset = 0;
    while (true) {
        s32 count = 0;
        if (R_FAILED(nsListApplicationRecord(records.data(), records.size(), offset, &count)) || count <= 0) {
            break;
        }

        for (s32 i = 0; i < count; i++) {
            const auto app_id = records[i].application_id;
            if (!app_id) {
                continue;
            }
            if ((app_id & 0x0500000000000000) == 0x0500000000000000) {
                continue;
            }
            title::MetaEntries installed_content;
            if (R_FAILED(title::GetMetaEntries(app_id, installed_content)) || installed_content.empty()) {
                continue;
            }

            if (m_installed_app_ids.insert(app_id).second) {
                m_installed_apps.push_back(app_id);
            }
        }
        offset += count;
    }
}

void Menu::ReadBackupEntries(std::vector<Entry>& out) const {
    fs::FsNativeSd fs;
    std::unordered_map<std::string, size_t> group_map;
    std::vector<Entry> groups;
    std::unordered_set<std::string> seen_paths;

    const auto process_archive = [&](const fs::FsPath& path, std::string_view filename, std::string_view dbi_game_dir_name, int source_prio) {
        if (!seen_paths.insert(path.s).second) {
            return;
        }

        BackupArchiveInfo info{};
        if (!InspectBackupArchive(&fs, path, filename, dbi_game_dir_name, info)) {
            return;
        }

        info.source = source_prio;
        const auto key = BackupGroupKey(info);
        auto it = group_map.find(key);
        if (it == group_map.end()) {
            Entry e{};
            e.application_id = info.application_id;
            e.system_save_data_id = info.system_save_data_id;
            e.save_data_type = info.save_data_type;
            e.uid = info.uid;
            e.save_data_index = info.save_data_index;
            e.save_data_rank = info.save_data_rank;
            e.is_backup = true;
            e.backup_timestamp = info.timestamp;
            e.backup_count = 1;
            e.backup_path = path;
            e.dbi_game_dir = info.dbi_game_dir;
            e.source_timestamp = info.source_timestamp;
            e.commit_id = info.commit_id;

            if (IsSystemLikeSave(e.save_data_type)) {
                detail::FakeNacpEntryForSystem(e);
            } else if (!e.dbi_game_dir.empty() && !IsHex16(e.dbi_game_dir)) {
                std::strncpy(e.lang.name, e.dbi_game_dir.c_str(), sizeof(e.lang.name) - 1);
                e.lang.name[sizeof(e.lang.name) - 1] = '\0';
            }

            group_map.emplace(key, groups.size());
            groups.emplace_back(std::move(e));
        } else {
            auto& existing = groups[it->second];
            existing.backup_count++;

            bool is_newer = false;
            if (info.timestamp != existing.backup_timestamp) {
                is_newer = info.timestamp > existing.backup_timestamp;
            } else {
                is_newer = (path.toString() < existing.backup_path.toString());
            }

            if (is_newer) {
                existing.backup_timestamp = info.timestamp;
                existing.backup_path = path;
                existing.source_timestamp = info.source_timestamp;
                existing.commit_id = info.commit_id;
            }

            if (existing.dbi_game_dir.empty() && !info.dbi_game_dir.empty()) {
                existing.dbi_game_dir = info.dbi_game_dir;
                if (existing.lang.name[0] == '\0' && !IsHex16(existing.dbi_game_dir)) {
                    std::strncpy(existing.lang.name, existing.dbi_game_dir.c_str(), sizeof(existing.lang.name) - 1);
                    existing.lang.name[sizeof(existing.lang.name) - 1] = '\0';
                }
            }
        }
    };

    // 1. Scan DBI-format game backups: /switch/DBI/saves/<game>/<date>/<appid>_<type>_..zip
    const auto dbi_root = fs::AppendPath(fs.Root(), DBI_SAVES_PATH);
    filebrowser::FsDirCollection games{};
    filebrowser::FsView::get_collection(&fs, dbi_root, "", games, false, true, false);
    for (const auto& game : games.dirs) {
        const auto game_dir = fs::AppendPath(dbi_root, game.name);
        filebrowser::FsDirCollection dates{};
        filebrowser::FsView::get_collection(&fs, game_dir, "", dates, true, true, false);
        for (const auto& file : dates.files) {
            process_archive(fs::AppendPath(dates.path, file.name), file.name, game.name, 0);
        }
        for (const auto& date : dates.dirs) {
            filebrowser::FsDirCollection files{};
            filebrowser::FsView::get_collection(&fs, fs::AppendPath(game_dir, date.name), "", files, true, false, false);
            for (const auto& file : files.files) {
                process_archive(fs::AppendPath(files.path, file.name), file.name, game.name, 0);
            }
        }
    }

    // 2. Scan Sphaira root (/dumps) and custom search paths
    const auto scan_sphaira_root = [&](const fs::FsPath& root_path, int prio_base) {
        const auto root = fs::AppendPath(fs.Root(), root_path);
        filebrowser::FsDirCollection l1{};
        filebrowser::FsView::get_collection(&fs, root, "", l1, true, true, false);
        for (const auto& file : l1.files) {
            process_archive(fs::AppendPath(l1.path, file.name), file.name, "", prio_base);
        }
        for (const auto& dir1 : l1.dirs) {
            const auto dir1_path = fs::AppendPath(root, dir1.name);
            filebrowser::FsDirCollection l2{};
            filebrowser::FsView::get_collection(&fs, dir1_path, "", l2, true, true, false);
            for (const auto& file : l2.files) {
                process_archive(fs::AppendPath(l2.path, file.name), file.name, "", prio_base + 1);
            }
            for (const auto& dir2 : l2.dirs) {
                const auto dir2_path = fs::AppendPath(dir1_path, dir2.name);
                filebrowser::FsDirCollection l3{};
                filebrowser::FsView::get_collection(&fs, dir2_path, "", l3, true, false, false);
                for (const auto& file : l3.files) {
                    process_archive(fs::AppendPath(l3.path, file.name), file.name, "", prio_base + 2);
                }
            }
        }
    };

    scan_sphaira_root(fs::FsPath{DEFAULT_BACKUP_ROOT}, 1);

    int custom_prio = 10;
    for (const auto& custom_path_str : GetBackupSearchPaths()) {
        scan_sphaira_root(fs::FsPath{custom_path_str}, custom_prio);
        custom_prio += 5;
    }

    // Sort backups newest first by default
    std::ranges::sort(groups, [](const Entry& a, const Entry& b) {
        if (a.backup_timestamp != b.backup_timestamp) {
            return a.backup_timestamp > b.backup_timestamp;
        }
        return a.application_id < b.application_id;
    });

    for (auto& g : groups) {
        out.emplace_back(std::move(g));
    }
}

void Menu::Sort() {
    // const auto sort = m_sort.Get();
    const auto order = m_order.Get();
    const bool want_reversed = order == OrderType_Ascending;

    if (want_reversed != m_is_reversed) {
        // reverse the live-save and backup sections independently so the two
        // stay partitioned (live first, backups after) regardless of order.
        const auto mid = std::clamp<s64>(m_backup_start, 0, static_cast<s64>(m_entries.size()));
        std::reverse(m_entries.begin(), m_entries.begin() + mid);
        std::reverse(m_entries.begin() + mid, m_entries.end());
        m_is_reversed = want_reversed;
    }
}

void Menu::SortAndFindLastFile(bool scan) {
    if (m_entries.empty()) {
        if (scan) {
            ScanHomebrew();
        } else {
            Sort();
            SetIndex(0);
        }
        return;
    }

    const auto last_key = DisplayEntryKey(m_entries[m_index]);
    if (scan) {
        ScanHomebrew();
    } else {
        Sort();
    }
    SetIndex(0);

    s64 index = -1;
    for (u64 i = 0; i < m_entries.size(); i++) {
        if (last_key == DisplayEntryKey(m_entries[i])) {
            index = i;
            break;
        }
    }

    if (index >= 0) {
        const auto g = ComputeGridSections();
        const auto disp = EntryToDisplay(index, g);
        const auto row = m_list->GetRow();
        const auto page = m_list->GetPage();
        // guesstimate where the position is (in display slots, which include
        // the divider gap between live saves and backups).
        if (disp >= page) {
            m_list->SetYoff((((disp - page) + row) / row) * m_list->GetMaxY());
        } else {
            m_list->SetYoff(0);
        }
        SetIndex(index);
    }
}

void Menu::FreeEntries() {
    auto vg = App::GetVg();

    for (auto&p : m_entries) {
        FreeEntry(vg, p);
    }

    m_entries.clear();
    m_backup_start = 0;
}

void Menu::OnLayoutChange() {
    m_index = 0;
    grid::Menu::OnLayoutChange(m_list, m_layout.Get());

    if (!m_app_id_filter) {
        const Vec4 content_pos{40, 148, 1200, 488};
        switch (m_layout.Get()) {
            case grid::LayoutType_List: {
                const Vec2 pad{0, 2};
                const Vec4 v{75, 152, 1130, 70};
                m_list = std::make_unique<List>(1, 6, content_pos, v, pad);
            }   break;

            case grid::LayoutType_Grid: {
                const Vec2 pad{10, 10};
                const Vec4 v{93, 202, 174, 174};
                m_list = std::make_unique<List>(6, 6*2, content_pos, v, pad);
            }   break;

            case grid::LayoutType_GridDetail: {
                const Vec2 pad{10, 10};
                const Vec4 v{75, 150, 370, 155};
                m_list = std::make_unique<List>(3, 3*3, content_pos, v, pad);
            }   break;

            case grid::LayoutType_HbMenu: {
                const Vec2 pad{16, 0};
                const Vec4 v{80, 450, 140, 168};
                m_list = std::make_unique<List>(1, 7, content_pos, v, pad);
                m_list->SetLayout(List::Layout::HOME);
            }   break;

            default:
                break;
        }
    }
}

auto Menu::CollectGroupArchives(fs::Fs* fs, const Entry& group, const fs::FsPath& backup_root) const -> std::vector<BackupCandidate> {
    const auto candidates = CollectBackups(fs, group, backup_root);
    const auto target_key = BackupGroupKey(group);

    std::vector<BackupCandidate> out;
    for (const auto& c : candidates) {
        const auto slash = std::strrchr(c.path.s, '/');
        const auto fname = slash ? (slash + 1) : c.path.s;
        BackupArchiveInfo info{};
        if (InspectBackupArchive(fs, c.path, fname, group.dbi_game_dir, info)) {
            if (BackupGroupKey(info) == target_key) {
                out.emplace_back(c);
            }
        }
    }

    if (out.empty() && !group.backup_path.empty()) {
        out.emplace_back(BackupCandidate{group.backup_timestamp, group.backup_path, 0});
    }

    return out;
}

auto Menu::ResolveRestoreTarget(const Entry& backup, const AccountUid* explicit_uid) -> Entry {
    if (!backup.is_backup && backup.save_data_id != 0 && (!explicit_uid || std::memcmp(explicit_uid, &backup.uid, sizeof(AccountUid)) == 0)) {
        Entry target = backup;
        target.is_backup = false;
        return target;
    }

    Entry target = backup;
    target.is_backup = false;
    if (explicit_uid) {
        target.uid = *explicit_uid;
    }

    AccountProfileBase acc{};
    acc.uid = target.uid;
    FsSaveDataSpaceId space_id;
    FsSaveDataFilter filter;
    GetFsSaveAttr(acc, target.save_data_type, space_id, filter);

    target.save_data_id = 0;
    target.save_data_space_id = space_id;

    FsSaveDataInfoReader reader;
    if (R_SUCCEEDED(fsOpenSaveDataInfoReaderWithFilter(&reader, space_id, &filter))) {
        ON_SCOPE_EXIT(fsSaveDataInfoReaderClose(&reader));
        std::vector<FsSaveDataInfo> info_list(256);
        bool found = false;
        while (!found) {
            s64 record_count = 0;
            if (R_FAILED(fsSaveDataInfoReaderRead(&reader, info_list.data(), info_list.size(), &record_count)) || record_count <= 0) {
                break;
            }
            for (s32 i = 0; i < record_count; i++) {
                const auto& info = info_list[i];
                const bool id_match = IsSystemLikeSave(target.save_data_type)
                    ? (info.system_save_data_id == target.system_save_data_id)
                    : (info.application_id == target.application_id);
                if (id_match &&
                    info.save_data_type == target.save_data_type &&
                    info.save_data_index == target.save_data_index) {
                    if (target.save_data_type == FsSaveDataType_Account) {
                        if (std::memcmp(&info.uid, &target.uid, sizeof(AccountUid)) != 0) {
                            continue;
                        }
                    }
                    target.save_data_id = info.save_data_id;
                    target.save_data_space_id = info.save_data_space_id;
                    target.save_data_rank = info.save_data_rank;
                    target.size = info.size;
                    found = true;
                    break;
                }
            }
        }
    }

    return target;
}

void Menu::PromptSaveAction() {
    if (m_entries.empty()) {
        return;
    }

    const auto seeds = GetSelectedEntries();
    if (seeds.empty()) {
        return;
    }

    bool has_live = false;
    bool has_backup = false;
    for (const auto& s : seeds) {
        if (s.is_backup) {
            has_backup = true;
        } else {
            has_live = true;
        }
    }

    if (has_live && has_backup) {
        App::Push<OptionBox>("Please select only live saves or only backups."_i18n, "OK"_i18n);
        return;
    }

    if (has_live) {
        PromptLiveSaveAction(seeds);
    } else {
        PromptBackupGroupAction(seeds);
    }
}

void Menu::PromptLiveSaveAction(const std::vector<Entry>& seeds) {
    std::set<u64> selected_apps;
    std::set<u64> selected_sys;
    for (const auto& s : seeds) {
        if (!s.is_backup) {
            if (IsSystemLikeSave(s.save_data_type)) {
                selected_sys.insert(s.system_save_data_id);
            } else if (s.application_id != 0) {
                selected_apps.insert(s.application_id);
            }
        }
    }
    if (selected_apps.empty() && selected_sys.empty() && m_index < m_entries.size() && !m_entries[m_index].is_backup) {
        const auto& cur = m_entries[m_index];
        if (IsSystemLikeSave(cur.save_data_type)) {
            selected_sys.insert(cur.system_save_data_id);
        } else if (cur.application_id != 0) {
            selected_apps.insert(cur.application_id);
        }
    }

    PopupList::Items items;
    items.emplace_back("Create backup"_i18n);
    items.emplace_back("Create backup if newer"_i18n);
    items.emplace_back("Restore"_i18n);
    items.emplace_back("Open in file browser"_i18n);
    items.emplace_back("Delete"_i18n);
    const std::string select_game_label = ((selected_apps.size() + selected_sys.size()) > 1)
        ? "Select all saves for selected games"_i18n
        : "Select all saves for this game"_i18n;
    items.emplace_back(select_game_label);

    auto popup = std::make_unique<PopupList>("Save Action"_i18n, items, [this, seeds, selected_apps, selected_sys](auto op_index) {
        if (!op_index) {
            return;
        }

        switch (*op_index) {
            case 0:
                PromptSaveTypeOptions(SaveOp::Backup);
                break;

            case 1:
                CreateBackupIfNewer(seeds);
                break;

            case 2:
                PromptSaveTypeOptions(SaveOp::Restore);
                break;

            case 3:
                App::Push<OptionBox>("Live save filesystem browsing is not currently supported."_i18n, "OK"_i18n);
                break;

            case 4:
                PromptSaveTypeOptions(SaveOp::Delete);
                break;

            case 5: {
                for (auto& e : m_entries) {
                    if (!e.is_backup) {
                        const bool match = IsSystemLikeSave(e.save_data_type)
                            ? selected_sys.contains(e.system_save_data_id)
                            : selected_apps.contains(e.application_id);
                        if (match && !e.selected) {
                            e.selected = true;
                            m_selected_count++;
                        }
                    }
                }
                break;
            }

            default:
                break;
        }
    });
    popup->SetMenuStyle(true);
    App::Push(std::move(popup));
}

void Menu::CreateBackupIfNewer(const std::vector<Entry>& seeds) {
    auto to_backup_count = std::make_shared<size_t>(0);
    auto up_to_date_count = std::make_shared<size_t>(0);

    App::Push<ProgressBox>(0, "Create backup if newer"_i18n, "", [this, seeds, to_backup_count, up_to_date_count](auto pbox) -> Result {
        fs::FsNativeSd sd_fs;
        const fs::FsPath backup_root{DEFAULT_BACKUP_ROOT};

        std::vector<Entry> to_backup;

        for (size_t i = 0; i < seeds.size(); i++) {
            R_TRY(pbox->ShouldExitResult());
            const auto& e = seeds[i];
            pbox->SetTitle(e.GetName());
            pbox->UpdateTransfer(i + 1, seeds.size());

            const auto archives = CollectGroupArchives(&sd_fs, e, backup_root);
            if (archives.empty()) {
                to_backup.emplace_back(e);
                continue;
            }

            const auto& newest = archives.front();
            const auto slash = std::strrchr(newest.path.s, '/');
            BackupArchiveInfo binfo{};
            if (!InspectBackupArchive(&sd_fs, newest.path, slash ? slash + 1 : newest.path.s, "", binfo)) {
                to_backup.emplace_back(e);
                continue;
            }

            const auto space_id = static_cast<FsSaveDataSpaceId>(e.save_data_space_id);

            FsSaveDataExtraData live_extra{};
            const auto rc = fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(&live_extra, sizeof(live_extra), space_id, e.save_data_id);
            if (R_FAILED(rc)) {
                to_backup.emplace_back(e);
                continue;
            }

            bool is_up_to_date = false;
            if (live_extra.timestamp != 0 && binfo.source_timestamp != 0) {
                if (live_extra.timestamp == binfo.source_timestamp) {
                    if (live_extra.commit_id != 0 && binfo.commit_id != 0) {
                        is_up_to_date = (live_extra.commit_id == binfo.commit_id);
                    } else {
                        is_up_to_date = true;
                    }
                }
            } else if (live_extra.commit_id != 0 && binfo.commit_id != 0) {
                is_up_to_date = (live_extra.commit_id == binfo.commit_id);
            }

            if (is_up_to_date) {
                (*up_to_date_count)++;
            } else {
                to_backup.emplace_back(e);
            }
        }

        *to_backup_count = to_backup.size();

        if (to_backup.empty()) {
            R_SUCCEED();
        }

        const auto location = MakeSdCardDumpLocation();
        for (size_t i = 0; i < to_backup.size(); i++) {
            R_TRY(pbox->ShouldExitResult());
            auto& e = to_backup[i];
            detail::LoadControlEntry(e);
            pbox->SetTitle(e.GetName());
            if (e.image) {
                pbox->SetImage(e.image);
            } else if (auto data = title::Get(e.application_id); data && !data->icon.empty()) {
                pbox->SetImageDataConst(data->icon);
            } else {
                pbox->SetImage(0);
            }
            pbox->UpdateTransfer(i + 1, to_backup.size());
            R_TRY(BackupSaveInternal(pbox, location, e, App::GetSaveCompressBackup(), false, backup_root));
        }

        R_SUCCEED();
    }, [this, to_backup_count, up_to_date_count](Result rc) {
        if (R_SUCCEEDED(rc)) {
            if (*to_backup_count == 0 && *up_to_date_count > 0) {
                App::Push<OptionBox>("All selected saves are already up to date."_i18n, "OK"_i18n);
            } else if (*to_backup_count > 0 && *up_to_date_count > 0) {
                const std::string msg = std::to_string(*to_backup_count) + " " + "backup(s) created, "_i18n +
                    std::to_string(*up_to_date_count) + " " + "already up to date."_i18n;
                App::Push<OptionBox>(msg, "OK"_i18n);
            } else {
                App::Notify("Backup successful!"_i18n);
            }
        } else {
            App::PushErrorBox(rc, "Backup failed!"_i18n);
        }
        ClearSelection();
        ScanHomebrew();
    });
}

void Menu::PromptBackupGroupAction(const std::vector<Entry>& seeds) {
    const auto& focused = (m_index < m_entries.size()) ? m_entries[m_index] : seeds.front();

    enum class ActionType {
        VerifyIntegrity,
        DeleteOlder,
        Restore,
        RestoreForUser,
        OpenFileBrowser,
        Delete,
        SelectUser,
        SelectGame,
    };

    struct ActionItem {
        ActionType type;
        std::string label;
    };

    std::vector<ActionItem> actions;
    actions.push_back({ActionType::Restore, "Restore"_i18n});

    if (seeds.size() == 1 && seeds.front().save_data_type == FsSaveDataType_Account) {
        actions.push_back({ActionType::RestoreForUser, "Restore for user…"_i18n});
    }

    actions.push_back({ActionType::VerifyIntegrity, "Verify integrity"_i18n});
    actions.push_back({ActionType::DeleteOlder, "Delete older backups"_i18n});
    actions.push_back({ActionType::OpenFileBrowser, "Open in file browser"_i18n});

    const bool has_user_select = focused.is_backup &&
        focused.save_data_type == FsSaveDataType_Account &&
        (focused.uid.uid[0] != 0 || focused.uid.uid[1] != 0);
    if (has_user_select) {
        actions.push_back({ActionType::SelectUser, "Select all backups for this user"_i18n});
    }

    actions.push_back({ActionType::SelectGame, "Select all backups for this game"_i18n});
    actions.push_back({ActionType::Delete, "Delete"_i18n});

    PopupList::Items items;
    for (const auto& a : actions) {
        items.emplace_back(a.label);
    }

    auto popup = std::make_unique<PopupList>("Backup Action"_i18n, items, [this, seeds, focused, actions](auto op_index) {
        if (!op_index || *op_index >= static_cast<s64>(actions.size())) {
            return;
        }

        switch (actions[*op_index].type) {
            case ActionType::VerifyIntegrity:
                VerifyIntegrity(seeds);
                break;

            case ActionType::DeleteOlder:
                DeleteOlderBackups(seeds);
                break;

            case ActionType::Restore: {
                const auto accounts = App::GetAccountList();
                if (seeds.size() == 1) {
                    const auto& e = seeds.front();
                    if (e.save_data_type == FsSaveDataType_Account) {
                        bool uid_found = false;
                        for (const auto& acc : accounts) {
                            if (!std::memcmp(&e.uid, &acc.uid, sizeof(e.uid))) {
                                uid_found = true;
                                break;
                            }
                        }
                        if (!uid_found || (e.uid.uid[0] == 0 && e.uid.uid[1] == 0)) {
                            RestoreForUser(e);
                            return;
                        }
                    }
                    const auto location = MakeSdCardDumpLocation();
                    const fs::FsPath backup_root{DEFAULT_BACKUP_ROOT};
                    fs::FsNativeSd sd_fs;
                    const auto archives = CollectGroupArchives(&sd_fs, e, backup_root);

                    auto target = ResolveRestoreTarget(e);

                    if (archives.size() <= 1 && !archives.empty()) {
                        RestoreSavesPicked(std::move(target), location, backup_root, archives.front().path);
                    } else if (!archives.empty()) {
                        ShowRestorePickerPopup(std::move(target), location, backup_root, {}, archives);
                    } else if (!target.backup_path.empty()) {
                        const auto backup_path = target.backup_path;
                        RestoreSavesPicked(std::move(target), location, backup_root, backup_path);
                    } else {
                        StartRestore({std::move(target)}, location, backup_root);
                    }
                } else {
                    const auto is_local_account = [&](const AccountUid& uid) {
                        if (uid.uid[0] == 0 && uid.uid[1] == 0) {
                            return false;
                        }
                        for (const auto& acc : accounts) {
                            if (!std::memcmp(&uid, &acc.uid, sizeof(AccountUid))) {
                                return true;
                            }
                        }
                        return false;
                    };

                    std::vector<size_t> prompt_indices;
                    for (size_t i = 0; i < seeds.size(); i++) {
                        if (seeds[i].save_data_type == FsSaveDataType_Account && !is_local_account(seeds[i].uid)) {
                            prompt_indices.push_back(i);
                        }
                    }

                    if (!prompt_indices.empty() && accounts.empty()) {
                        App::Push<OptionBox>("No user accounts found on this console."_i18n, "OK"_i18n);
                        return;
                    }

                    auto chosen_uids = std::make_shared<std::vector<AccountUid>>(seeds.size());
                    PromptBatchRestoreAccountTargets(seeds, std::move(prompt_indices), accounts, chosen_uids, 0);
                }
                break;
            }

            case ActionType::RestoreForUser:
                RestoreForUser(seeds.front());
                break;

            case ActionType::OpenFileBrowser: {
                const auto& target = seeds.front();
                const auto slash = std::strrchr(target.backup_path.s, '/');
                if (slash) {
                    std::string dir(target.backup_path.s, slash - target.backup_path.s);
                    if (dir.empty()) dir = "/";
                    const filebrowser::FsEntry sd{"microSD card", "/", filebrowser::FsType::Sd};
                    App::Push<filebrowser::Menu>(MenuFlag_None, sd, dir.c_str());
                }
                break;
            }

            case ActionType::Delete:
                DeleteBackupGroups(seeds);
                break;

            case ActionType::SelectUser: {
                for (auto& e : m_entries) {
                    if (e.is_backup && e.save_data_type == FsSaveDataType_Account &&
                        !std::memcmp(&e.uid, &focused.uid, sizeof(AccountUid))) {
                        if (!e.selected) {
                            e.selected = true;
                            m_selected_count++;
                        }
                    }
                }
                break;
            }

            case ActionType::SelectGame: {
                const bool is_sys = IsSystemLikeSave(focused.save_data_type);
                for (auto& e : m_entries) {
                    if (e.is_backup) {
                        const bool match = is_sys
                            ? (IsSystemLikeSave(e.save_data_type) && e.system_save_data_id == focused.system_save_data_id)
                            : (!IsSystemLikeSave(e.save_data_type) && e.application_id == focused.application_id);
                        if (match) {
                            if (!e.selected) {
                                e.selected = true;
                                m_selected_count++;
                            }
                        }
                    }
                }
                break;
            }
        }
    });
    popup->SetMenuStyle(true);
    App::Push(std::move(popup));
}

void Menu::VerifyIntegrity(const std::vector<Entry>& seeds) {
    auto valid_count = std::make_shared<size_t>(0);
    auto invalid_count = std::make_shared<size_t>(0);
    auto failed_names = std::make_shared<std::vector<std::string>>();

    App::Push<ProgressBox>(0, "Verify integrity"_i18n, "", [this, seeds, valid_count, invalid_count, failed_names](auto pbox) -> Result {
        fs::FsNativeSd sd_fs;
        const fs::FsPath backup_root{DEFAULT_BACKUP_ROOT};

        std::vector<fs::FsPath> all_archives;
        std::set<std::string> seen;
        for (const auto& g : seeds) {
            const auto archives = CollectGroupArchives(&sd_fs, g, backup_root);
            for (const auto& a : archives) {
                if (seen.insert(a.path.toString()).second) {
                    all_archives.emplace_back(a.path);
                }
            }
        }

        for (size_t i = 0; i < all_archives.size(); i++) {
            R_TRY(pbox->ShouldExitResult());
            const auto& archive_path = all_archives[i];
            const auto slash = std::strrchr(archive_path.s, '/');
            const std::string name = slash ? (slash + 1) : archive_path.s;

            pbox->SetTitle(name);
            pbox->UpdateTransfer(i + 1, all_archives.size());

            bool ok = false;
            if (std::string_view{archive_path.s}.ends_with(".zip")) {
                ok = VerifyZipIntegrity(archive_path);
            } else if (IsRawSaveCandidate(&sd_fs, archive_path, name)) {
                ok = VerifyDisaIntegrity(&sd_fs, archive_path);
            }

            if (ok) {
                (*valid_count)++;
            } else {
                (*invalid_count)++;
                failed_names->emplace_back(name);
            }
        }

        R_SUCCEED();
    }, [valid_count, invalid_count, failed_names](Result rc) {
        if (R_FAILED(rc)) {
            App::PushErrorBox(rc, "Integrity verification failed!"_i18n);
            return;
        }

        if (*invalid_count == 0) {
            const std::string msg = "Integrity verified: all "_i18n + std::to_string(*valid_count) + " archive(s) are valid."_i18n;
            App::Push<OptionBox>(msg, "OK"_i18n);
        } else {
            std::string msg = "Integrity check failed: "_i18n + std::to_string(*invalid_count) + " corrupt archive(s) found:\n"_i18n;
            for (size_t i = 0; i < std::min<size_t>(5, failed_names->size()); i++) {
                msg += "• " + (*failed_names)[i] + "\n";
            }
            if (failed_names->size() > 5) {
                msg += "...and " + std::to_string(failed_names->size() - 5) + " more.";
            }
            App::Push<OptionBox>(msg, "OK"_i18n);
        }
    });
}

void Menu::DeleteOlderBackups(const std::vector<Entry>& seeds) {
    fs::FsNativeSd sd_fs;
    const fs::FsPath backup_root{DEFAULT_BACKUP_ROOT};

    struct PruneGroup {
        Entry group;
        std::vector<BackupCandidate> older;
    };
    std::vector<PruneGroup> to_prune;
    size_t total_older = 0;

    for (const auto& g : seeds) {
        const auto archives = CollectGroupArchives(&sd_fs, g, backup_root);
        if (archives.size() > 1) {
            std::vector<BackupCandidate> older(archives.begin() + 1, archives.end());
            total_older += older.size();
            to_prune.emplace_back(PruneGroup{g, std::move(older)});
        }
    }

    if (total_older == 0) {
        App::Notify("No older backups to delete."_i18n);
        return;
    }

    const std::string prompt = "Delete "_i18n + std::to_string(total_older) +
        " older backup archive(s)? The newest backup for each group will be kept."_i18n;

    App::Push<OptionBox>(prompt, "Back"_i18n, "Delete"_i18n, 0, [this, to_prune](auto choice) {
        if (choice && *choice == 1) {
            App::PopToMenu();
            auto deleted_count = std::make_shared<size_t>(0);
            auto failed_count = std::make_shared<size_t>(0);
            auto first_error = std::make_shared<Result>(0);

            App::Push<ProgressBox>(0, "Delete older backups"_i18n, "", [to_prune, deleted_count, failed_count, first_error](auto pbox) -> Result {
                fs::FsNativeSd sd_fs;
                for (const auto& item : to_prune) {
                    for (const auto& cand : item.older) {
                        const auto rc = sd_fs.DeleteFile(cand.path);
                        if (R_SUCCEEDED(rc)) {
                            (*deleted_count)++;
                            const auto slash = std::strrchr(cand.path.s, '/');
                            if (slash) {
                                std::string dir(cand.path.s, slash - cand.path.s);
                                sd_fs.DeleteDirectory(dir.c_str());
                            }
                        } else {
                            (*failed_count)++;
                            if (*first_error == 0) {
                                *first_error = rc;
                            }
                        }
                    }
                }
                return *first_error;
            }, [this, deleted_count, failed_count](Result rc) {
                if (*failed_count == 0 && *deleted_count > 0) {
                    App::Notify("Delete successful!"_i18n);
                } else if (*deleted_count > 0 && *failed_count > 0) {
                    const std::string msg = std::to_string(*deleted_count) + " " + "deleted, "_i18n +
                        std::to_string(*failed_count) + " " + "failed."_i18n;
                    App::PushErrorBox(rc, msg);
                } else {
                    App::PushErrorBox(rc, "Delete failed!"_i18n);
                }
                ClearSelection();
                ScanHomebrew();
            });
        }
    }, seeds.front().image);
}

void Menu::RestoreForUser(Entry e) {
    const auto accounts = App::GetAccountList();
    if (accounts.empty()) {
        App::Push<OptionBox>("No user accounts found on this console."_i18n, "OK"_i18n);
        return;
    }

    PopupList::Items items;
    for (const auto& acc : accounts) {
        items.emplace_back(acc.nickname);
    }

    auto popup = std::make_unique<PopupList>("Restore for user"_i18n, items, [this, e, accounts](auto op_index) mutable {
        if (!op_index || *op_index >= static_cast<s64>(accounts.size())) {
            return;
        }

        const auto chosen_uid = accounts[*op_index].uid;
        const auto location = MakeSdCardDumpLocation();
        const fs::FsPath backup_root{DEFAULT_BACKUP_ROOT};

        fs::FsNativeSd sd_fs;
        const auto archives = CollectGroupArchives(&sd_fs, e, backup_root);

        auto target = ResolveRestoreTarget(e, &chosen_uid);

        if (archives.size() <= 1 && !archives.empty()) {
            RestoreSavesPicked(std::move(target), location, backup_root, archives.front().path);
        } else if (!archives.empty()) {
            ShowRestorePickerPopup(std::move(target), location, backup_root, {}, archives);
        } else if (!target.backup_path.empty()) {
            const auto backup_path = target.backup_path;
            RestoreSavesPicked(std::move(target), location, backup_root, backup_path);
        } else {
            StartRestore({std::move(target)}, location, backup_root);
        }
    });
    App::Push(std::move(popup));
}

void Menu::PromptBatchRestoreAccountTargets(
    std::vector<Entry> seeds,
    std::vector<size_t> prompt_indices,
    std::vector<AccountProfileBase> accounts,
    std::shared_ptr<std::vector<AccountUid>> chosen_uids,
    size_t prompt_step) {

    if (prompt_step >= prompt_indices.size()) {
        std::vector<Entry> resolved;
        resolved.reserve(seeds.size());
        for (size_t i = 0; i < seeds.size(); i++) {
            const auto& s = seeds[i];
            const bool was_prompted = std::ranges::find(prompt_indices, i) != prompt_indices.end();
            if (was_prompted) {
                resolved.emplace_back(ResolveRestoreTarget(s, &(*chosen_uids)[i]));
            } else {
                resolved.emplace_back(ResolveRestoreTarget(s));
            }
        }

        const auto location = MakeSdCardDumpLocation();
        const fs::FsPath backup_root{DEFAULT_BACKUP_ROOT};
        RestoreSaves(std::move(resolved), location, backup_root);
        return;
    }

    const size_t seed_idx = prompt_indices[prompt_step];
    const auto& entry = seeds[seed_idx];

    PopupList::Items items;
    for (const auto& acc : accounts) {
        items.emplace_back(acc.nickname);
    }

    std::string game_name;
    if (entry.GetName() && entry.GetName()[0] != '\0') {
        game_name = entry.GetName();
    } else if (entry.application_id != 0) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "Title %016lX", entry.application_id);
        game_name = buf;
    }

    const std::string title = game_name.empty()
        ? "Restore for user"_i18n
        : "Restore for user"_i18n + ": " + game_name;

    auto popup = std::make_unique<PopupList>(title, items, [this, seeds = std::move(seeds), prompt_indices = std::move(prompt_indices), accounts = std::move(accounts), chosen_uids, prompt_step, seed_idx](auto op_index) mutable {
        if (!op_index || *op_index >= static_cast<s64>(accounts.size())) {
            return;
        }

        (*chosen_uids)[seed_idx] = accounts[*op_index].uid;
        PromptBatchRestoreAccountTargets(std::move(seeds), std::move(prompt_indices), std::move(accounts), chosen_uids, prompt_step + 1);
    });
    App::Push(std::move(popup));
}

void Menu::DeleteBackupGroups(const std::vector<Entry>& groups) {
    const auto prompt = groups.size() == 1
        ? "Are you sure you want to delete all backups for "_i18n + groups.front().GetName() + "?"
        : "Are you sure you want to delete all backups for the selected games?"_i18n;

    App::Push<OptionBox>(prompt, "Back"_i18n, "Delete"_i18n, 0, [this, groups](auto choice) {
        if (choice && *choice == 1) {
            App::PopToMenu();
            auto deleted_count = std::make_shared<size_t>(0);
            auto failed_count = std::make_shared<size_t>(0);
            auto first_error = std::make_shared<Result>(0);

            App::Push<ProgressBox>(0, "Delete backups"_i18n, "", [this, groups, deleted_count, failed_count, first_error](auto pbox) -> Result {
                fs::FsNativeSd sd_fs;
                const fs::FsPath backup_root{DEFAULT_BACKUP_ROOT};
                for (const auto& g : groups) {
                    auto archives = CollectGroupArchives(&sd_fs, g, backup_root);
                    for (const auto& a : archives) {
                        const auto rc = sd_fs.DeleteFile(a.path);
                        if (R_SUCCEEDED(rc)) {
                            (*deleted_count)++;
                            const auto slash = std::strrchr(a.path.s, '/');
                            if (slash) {
                                std::string dir(a.path.s, slash - a.path.s);
                                sd_fs.DeleteDirectory(dir.c_str());
                            }
                        } else {
                            (*failed_count)++;
                            if (*first_error == 0) {
                                *first_error = rc;
                            }
                        }
                    }
                }
                return *first_error;
            }, [this, deleted_count, failed_count](Result rc) {
                if (*failed_count == 0 && *deleted_count > 0) {
                    App::Notify("Delete successful!"_i18n);
                } else if (*deleted_count > 0 && *failed_count > 0) {
                    const std::string msg = std::to_string(*deleted_count) + " " + "deleted, "_i18n +
                        std::to_string(*failed_count) + " " + "failed."_i18n;
                    App::PushErrorBox(rc, msg);
                } else {
                    App::PushErrorBox(rc, "Delete failed!"_i18n);
                }
                ClearSelection();
                ScanHomebrew();
            });
        }
    }, groups.front().image);
}

auto Menu::CollectActionEntries(const std::vector<Entry>& seeds, const std::vector<u8>& types, const std::vector<s64>& account_indexes) -> std::vector<Entry> {
    std::set<u64> app_ids;
    std::set<u64> system_ids;
    for (const auto& e : seeds) {
        if (IsSystemLikeSave(e.save_data_type)) {
            system_ids.emplace(e.system_save_data_id);
        } else {
            app_ids.emplace(e.application_id);
        }
    }

    std::set<std::string> seen;
    std::vector<Entry> out;
    for (const auto type : types) {
        std::vector<Entry> scanned;
        if (type == FsSaveDataType_Account) {
            for (const auto account_index : account_indexes) {
                ReadSaveEntries(type, account_index, scanned);
            }
        } else {
            ReadSaveEntries(type, -1, scanned);
        }

        for (auto& e : scanned) {
            const auto matches = IsSystemLikeSave(e.save_data_type) ?
                system_ids.contains(e.system_save_data_id) :
                app_ids.contains(e.application_id);
            if (!matches) {
                continue;
            }

            const auto key = SaveEntryKey(e);
            if (seen.insert(key).second) {
                out.emplace_back(e);
            }
        }
    }

    return out;
}

void Menu::PromptSaveTypeOptions(SaveOp op) {
    const auto seeds = GetSelectedEntries();
    if (seeds.empty()) {
        return;
    }

    if (op == SaveOp::Delete && (m_category == Category::Backups || std::ranges::all_of(seeds, [](const auto& e){ return e.is_backup; }))) {
        const auto prompt = seeds.size() == 1
            ? "Are you sure you want to delete backups for "_i18n + seeds.front().GetName() + "?"
            : "Are you sure you want to delete backups for the selected games?"_i18n;

        App::Push<OptionBox>(prompt, "Back"_i18n, "Delete"_i18n, 0, [this, seeds](auto choice) {
            if (choice && *choice == 1) {
                App::PopToMenu();
                DeleteSaves(seeds);
            }
        }, seeds.front().image);
        return;
    }

    std::vector<s64> all_account_indexes;
    for (s64 i = 0; i < static_cast<s64>(m_accounts.size()); i++) {
        all_account_indexes.emplace_back(i);
    }

    const std::vector<u8> all_types{SAVE_TYPES.begin(), SAVE_TYPES.end()};
    auto available_entries = CollectActionEntries(seeds, all_types, all_account_indexes);
    if (available_entries.empty()) {
        if (op == SaveOp::Restore && !seeds.empty()) {
            available_entries = seeds;
        } else {
            App::Push<OptionBox>("No matching saves found."_i18n, "OK"_i18n);
            return;
        }
    }

    struct ActionState {
        bool all_accounts{true};
        std::vector<u8> account_enabled{};
        std::array<u8, SAVE_TYPE_VALUES.size()> type_available{};
        std::array<u8, SAVE_TYPE_VALUES.size()> type_enabled{};
        std::vector<dump::DumpLocation> locations{};
        std::vector<fs::FsPath> location_base_paths{};
        SidebarEntryArray::Items location_items{};
        // normalized key per entry (parallel to the vectors above), used to
        // detect when a freshly picked folder is already in this session's list.
        std::vector<std::string> location_keys{};
        s64 location_index{};
    };

    auto state = std::make_shared<ActionState>();
    state->account_enabled = m_account_enabled;
    if (state->account_enabled.size() != m_accounts.size()) {
        state->account_enabled.assign(m_accounts.size(), false);
        if (!state->account_enabled.empty()) {
            state->account_enabled[m_account_index] = true;
        }
    }

    for (const auto& e : available_entries) {
        state->type_available[SaveTypeIndex(e.save_data_type)] = true;
    }

    const auto system_index = SaveTypeIndex(FsSaveDataType_System);
    bool has_non_system{};
    for (size_t i = 0; i < SAVE_TYPES.size(); i++) {
        if (i != system_index && state->type_available[i]) {
            has_non_system = true;
            state->type_enabled[i] = true;
        }
    }
    if (!has_non_system && state->type_available[system_index]) {
        state->type_enabled[system_index] = true;
    }

    const fs::FsPath default_backup_root{DEFAULT_BACKUP_ROOT};
    const auto stdio_locations = location::GetStdio(true);

    // de-dup key set: the default /dumps entry and every stdio mount are
    // never repeated by the recent-folder history below.
    std::vector<fs::FsPath> stdio_backup_roots(stdio_locations.size());
    std::set<std::string> seen_keys;
    seen_keys.insert(MakeLocationKey(RecentBackupDir{false, "", "", default_backup_root}));
    for (size_t i = 0; i < stdio_locations.size(); i++) {
        stdio_backup_roots[i] = stdio_locations[i].dump_path.empty() ? default_backup_root : fs::FsPath{stdio_locations[i].dump_path};
        seen_keys.insert(MakeLocationKey(RecentBackupDir{true, stdio_locations[i].mount, "", stdio_backup_roots[i]}));
    }

    state->locations.emplace_back(MakeSdCardDumpLocation());
    state->location_base_paths.emplace_back(default_backup_root);
    state->location_items.emplace_back(MakeSdLocationLabel(default_backup_root));
    state->location_keys.emplace_back(MakeLocationKey(RecentBackupDir{false, "", "", default_backup_root}));

    // up to 5 most recently confirmed "Choose Folder..." picks, newest first.
    for (const auto& dir : GetRecentBackupDirs()) {
        if (!seen_keys.insert(MakeLocationKey(dir)).second) {
            continue;
        }

        state->locations.emplace_back(MakeDumpLocationFromRecent(dir));
        state->location_base_paths.emplace_back(dir.path);
        state->location_items.emplace_back(dir.stdio ? MakeLocationLabel(dir.name, dir.path) : MakeSdLocationLabel(dir.path));
        state->location_keys.emplace_back(MakeLocationKey(dir));
    }

    for (s32 i = 0; i < static_cast<s32>(stdio_locations.size()); i++) {
        dump::DumpLocation location{};
        location.entry = {dump::DumpLocationType_Stdio, i};
        location.stdio = stdio_locations;

        state->locations.emplace_back(std::move(location));
        state->location_base_paths.emplace_back(stdio_backup_roots[i]);
        state->location_items.emplace_back(MakeLocationLabel(stdio_locations[i].name, stdio_backup_roots[i]));
        state->location_keys.emplace_back(MakeLocationKey(RecentBackupDir{true, stdio_locations[i].mount, "", stdio_backup_roots[i]}));
    }

    const auto def_key = App::GetSaveDefaultLocation();
    if (!def_key.empty()) {
        for (s64 i = 0; i < static_cast<s64>(state->location_keys.size()); i++) {
            if (state->location_keys[i] == def_key) {
                state->location_index = i;
                break;
            }
        }
    }

    const auto title = (op == SaveOp::Restore) ? "Restore Options"_i18n :
                       (op == SaveOp::Delete)  ? "Delete Options"_i18n :
                                                 "Backup Options"_i18n;
    const auto action_label = (op == SaveOp::Restore) ? "Start Restore"_i18n :
                              (op == SaveOp::Delete)  ? "Delete Saves"_i18n :
                                                        "Start Backup"_i18n;
    const auto action_desc = (op == SaveOp::Restore) ? "Begin restoring saves from the selected location."_i18n :
                             (op == SaveOp::Delete)  ? "Permanently delete save data for selected games and accounts."_i18n :
                                                       "Begin backing up saves to the selected location."_i18n;

    auto options = std::make_unique<Sidebar>(title, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    options->Add<SidebarEntryCallback>(action_label, [this, state, seeds, op]() {
        std::vector<u8> selected_types;
        const auto system_index = SaveTypeIndex(FsSaveDataType_System);
        if (state->type_enabled[system_index]) {
            selected_types.emplace_back(FsSaveDataType_System);
        } else {
            for (size_t i = 0; i < SAVE_TYPES.size(); i++) {
                if (i != system_index && state->type_enabled[i]) {
                    selected_types.emplace_back(SAVE_TYPES[i]);
                }
            }
        }

        std::vector<s64> selected_accounts;
        if (state->all_accounts) {
            for (s64 i = 0; i < static_cast<s64>(m_accounts.size()); i++) {
                selected_accounts.emplace_back(i);
            }
        } else {
            for (s64 i = 0; i < static_cast<s64>(state->account_enabled.size()); i++) {
                if (state->account_enabled[i]) {
                    selected_accounts.emplace_back(i);
                }
            }
        }

        const auto entries = CollectActionEntries(seeds, selected_types, selected_accounts);
        if (entries.empty()) {
            App::Push<OptionBox>("No matching saves found."_i18n, "OK"_i18n);
            return;
        }

        if (op == SaveOp::Delete) {
            const auto prompt = seeds.size() == 1
                ? "Are you sure you want to delete save data for "_i18n + seeds.front().GetName() + "?"
                : "Are you sure you want to delete save data for the selected games?"_i18n;

            App::Push<OptionBox>(prompt, "Back"_i18n, "Delete"_i18n, 0, [this, entries](auto choice) {
                if (choice && *choice == 1) {
                    App::PopToMenu();
                    DeleteSaves(entries);
                }
            }, seeds.front().image);
            return;
        }

        const auto location_index = std::min<s64>(state->location_index, static_cast<s64>(state->locations.size() - 1));
        const auto location = state->locations[location_index];
        const auto backup_root = state->location_base_paths[location_index];

        App::PopToMenu();
        if (op == SaveOp::Restore) {
            StartRestore(entries, location, backup_root);
        } else {
            BackupSaves(entries, location, backup_root);
        }
    }, action_desc);

    if (op != SaveOp::Delete) {
        options->Add<SidebarEntryHeader>("LOCATION"_i18n);
        auto* location_entry = options->Add<SidebarEntryTextBase>("Location"_i18n, state->location_items[state->location_index], [](){}, "Choose the storage and folder for backups. Game saves are always written in DBI format to /switch/DBI/saves on the selected storage; the chosen folder is used for system save backups and for finding older backups during Restore."_i18n);
        location_entry->SetCallback([this, state, location_entry]() {
            auto items = state->location_items;
            const auto picker_index = static_cast<s64>(items.size());
            items.emplace_back("Choose Folder..."_i18n);

            App::Push<PopupList>("Location"_i18n, items, [this, state, location_entry, picker_index](auto op_index) {
                if (!op_index) {
                    return;
                }

                if (*op_index != picker_index) {
                    state->location_index = *op_index;
                    location_entry->SetValue(state->location_items[state->location_index]);
                    return;
                }

                App::Push<filepicker::Menu>(
                    filepicker::LocationCallback{[this, state, location_entry](const fs::FsPath& path, const filebrowser::FsEntry& fs_entry) -> bool {
                        const auto backup_root = NormalizeBackupRoot(path, fs_entry);
                        const auto is_stdio = fs_entry.type == filebrowser::FsType::Stdio;
                        const auto label = is_stdio ? MakeLocationLabel(fs_entry.name.toString(), backup_root) : MakeSdLocationLabel(backup_root);

                        const RecentBackupDir recent{
                            is_stdio,
                            is_stdio ? fs_entry.root.toString() : "",
                            fs_entry.name.toString(),
                            backup_root,
                        };
                        const auto key = MakeLocationKey(recent);

                        // if this folder is already in the current session's list
                        // (default, history or a mount, or a previous pick this
                        // session), just select it instead of adding a twin row.
                        const auto existing = std::ranges::find(state->location_keys, key);
                        if (existing != state->location_keys.end()) {
                            state->location_index = std::distance(state->location_keys.begin(), existing);
                        } else {
                            state->locations.emplace_back(MakeDumpLocationFromFsEntry(fs_entry));
                            state->location_base_paths.emplace_back(backup_root);
                            state->location_items.emplace_back(label);
                            state->location_keys.emplace_back(key);
                            state->location_index = static_cast<s64>(state->location_items.size() - 1);
                        }
                        location_entry->SetValue(state->location_items[state->location_index]);

                        AddRecentBackupDir(recent);

                        return true;
                    }},
                    std::vector<std::string>{},
                    fs::FsPath{},
                    true
                );
            }, state->location_index);
        });

        if (op == SaveOp::Backup) {
            options->Add<SidebarEntryBool>("Auto-sync after backup"_i18n, App::GetSaveAutosync(), [](bool& v_out){
                App::SetSaveAutosync(v_out);
            }, "After each Backup, automatically upload only the newly created backup ZIP to WebDAV. Does not sync your whole backup library - use Sync with remote (Save Options) for that."_i18n);
        } else if (op == SaveOp::Restore) {
            options->Add<SidebarEntryBool>("Include remote backups"_i18n, App::GetSaveRestoreIncludeRemote(), [](bool& v_out){
                App::SetSaveRestoreIncludeRemote(v_out);
            }, "Before showing the backup list, download any backups that exist on your WebDAV remote but are missing on this console, so they can be restored too. Remote-only backups are marked with a cloud icon. Only applies when a single save is selected."_i18n);
        }
    }

    const auto account_available = state->type_available[SaveTypeIndex(FsSaveDataType_Account)];
    if (account_available && m_accounts.size() > 1) {
        options->Add<SidebarEntryHeader>("ACCOUNTS"_i18n);
        options->Add<SidebarEntryCheckbox>(
            "All Accounts"_i18n,
            [state](){ return state->all_accounts; },
            [state](bool enabled) {
                state->all_accounts = enabled;
                if (!enabled && std::ranges::none_of(state->account_enabled, [](auto v){ return v; }) && !state->account_enabled.empty()) {
                    state->account_enabled[0] = true;
                }
            }, "Include saves from all user accounts."_i18n);

        for (size_t i = 0; i < m_accounts.size(); i++) {
            auto* entry = options->Add<SidebarEntryCheckbox>(
                std::string{"    "} + m_accounts[i].nickname,
                [state, i](){ return i < state->account_enabled.size() && state->account_enabled[i]; },
                [state, i](bool enabled) {
                    if (i >= state->account_enabled.size()) {
                        return;
                    }

                    state->all_accounts = false;
                    state->account_enabled[i] = enabled;
                    if (std::ranges::none_of(state->account_enabled, [](auto v){ return v; })) {
                        state->account_enabled[i] = true;
                    }
                }, "Include saves from this user account."_i18n);

            entry->Depends(
                [state](){ return !state->all_accounts; },
                "All Accounts is enabled."_i18n,
                [state, i]() {
                    if (i < state->account_enabled.size()) {
                        state->all_accounts = false;
                        state->account_enabled[i] = true;
                    }
            });
        }
    }

    const auto available_type_count = std::ranges::count_if(state->type_available, [](auto v){ return v; });
    const auto system_available = state->type_available[system_index];
    if (available_type_count <= 1) {
        return;
    }

    options->Add<SidebarEntryHeader>("SAVE TYPES"_i18n);
    for (size_t i = 0; i < SAVE_TYPES.size(); i++) {
        if (!state->type_available[i]) {
            continue;
        }

        const auto type = SAVE_TYPES[i];
        const auto label = (system_available && type != FsSaveDataType_System) ?
            "    " + FormatSaveTypeLabel(type) :
            FormatSaveTypeLabel(type);

        auto* entry = options->Add<SidebarEntryCheckbox>(
            label,
            [state, i](){ return state->type_enabled[i]; },
            [state, i, type, system_index](bool enabled) {
                if (type == FsSaveDataType_System) {
                    state->type_enabled[i] = enabled;
                } else {
                    state->type_enabled[system_index] = false;
                    state->type_enabled[i] = enabled;
                }

                bool any{};
                for (size_t n = 0; n < state->type_enabled.size(); n++) {
                    if (state->type_enabled[n]) {
                        any = true;
                        break;
                    }
                }
                if (!any) {
                    state->type_enabled[i] = true;
                }
            });

        if (type != FsSaveDataType_System) {
            entry->Depends(
                [state, system_index](){ return !state->type_enabled[system_index]; },
                "System is enabled."_i18n,
                [state, i, system_index]() {
                    state->type_enabled[system_index] = false;
                    state->type_enabled[i] = true;
                });
        }
    }
}

void Menu::DrawCategoryTabs(NVGcontext* vg, Theme* theme) {
    const std::array<std::string, 3> tab_names{
        "Installed Games"_i18n,
        "Deleted Games"_i18n,
        "Backups"_i18n,
    };

    s64 selected_tab = -1;
    switch (m_category) {
        case Category::Installed: selected_tab = 0; break;
        case Category::Deleted:   selected_tab = 1; break;
        case Category::Backups:   selected_tab = 2; break;
        default:                  selected_tab = -1; break;
    }

    const float body_top = TAB_BAR_TOP + TAB_BAR_H;
    const float radius = 8.f;

    const auto surface = theme->GetColour(ThemeEntryID_POPUP);
    const auto recessed = theme->GetColour(ThemeEntryID_GRID);
    const auto accent = theme->GetColour(ThemeEntryID_HIGHLIGHT_1);
    const auto line = theme->GetColour(ThemeEntryID_LINE);

    for (size_t i = 0; i < tab_names.size(); i++) {
        if (static_cast<s64>(i) == selected_tab) {
            continue;
        }
        const float x = TAB_BAR_X + i * (TAB_ITEM_W + TAB_BAR_GAP);
        const float y = TAB_BAR_TOP + 5.f;
        const float h = body_top - y;
        gfx::drawRectVarying(vg, Vec4{x, y, TAB_ITEM_W, h}, recessed, radius, radius, 0.f, 0.f);
        const auto label_colour = theme->GetColour(ThemeEntryID_TEXT_INFO);
        const float text_y = (y + body_top) * 0.5f;
        gfx::drawText(vg, x + TAB_ITEM_W * 0.5f, text_y, 20.f, label_colour, tab_names[i].c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }

    gfx::drawRect(vg, TAB_BAR_X, body_top, TAB_BAR_W, 1.f, line);

    if (selected_tab >= 0 && selected_tab < static_cast<s64>(tab_names.size())) {
        const float x = TAB_BAR_X + selected_tab * (TAB_ITEM_W + TAB_BAR_GAP);
        const float y = TAB_BAR_TOP;
        const float h = body_top - y + 1.f;
        gfx::drawRectVarying(vg, Vec4{x, y, TAB_ITEM_W, h}, surface, radius, radius, 0.f, 0.f);
        gfx::drawRectVarying(vg, Vec4{x, y, TAB_ITEM_W, 3.f}, accent, radius, radius, 0.f, 0.f);
        const auto label_colour = theme->GetColour(ThemeEntryID_TEXT);
        const float text_y = (y + body_top) * 0.5f;
        gfx::drawTextBold(vg, x + TAB_ITEM_W * 0.5f, text_y, 20.f, label_colour, tab_names[selected_tab].c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }
}

} // namespace sphaira::ui::menu::save
