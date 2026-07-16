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

} // namespace

void SignalChange() {
    ueventSignal(&g_change_uevent);
}

Menu::Menu(u32 flags) : grid::Menu{"Saves"_i18n, flags} {
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
    std::vector<RecentBackupDir> out;
    for (auto& opt : m_recent_backup_dirs) {
        RecentBackupDir dir;
        if (ParseRecentBackupDir(opt.Get(), dir) && RecentBackupDirExists(dir)) {
            out.emplace_back(std::move(dir));
        }
    }

    return out;
}

void Menu::AddRecentBackupDir(const RecentBackupDir& dir) {
    const auto key = SerializeRecentBackupDir(dir);
    const auto id = MakeLocationKey(dir);

    // move-to-front, drop duplicates, keep at most RECENT_BACKUP_DIR_MAX.
    // de-dup is by normalized location (MakeLocationKey - display name ignored),
    // so re-picking the same folder never leaves a stale twin behind just
    // because its shown name differs.
    std::vector<std::string> keys{key};
    for (auto& opt : m_recent_backup_dirs) {
        const auto v = opt.Get();
        if (v.empty()) {
            continue;
        }

        RecentBackupDir parsed;
        if (!ParseRecentBackupDir(v, parsed)) {
            continue;
        }

        if (MakeLocationKey(parsed) == id) {
            continue;
        }

        if (keys.size() < m_recent_backup_dirs.size()) {
            keys.emplace_back(v);
        }
    }

    for (size_t i = 0; i < m_recent_backup_dirs.size(); i++) {
        m_recent_backup_dirs[i].Set(i < keys.size() ? keys[i] : "");
    }
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
            first = i18n::get(GetSaveTypeLabel(type));
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
            i18n::get(GetSaveTypeLabel(type)),
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

void Menu::DisplaySaveOptions() {
    auto options = std::make_unique<Sidebar>("Save Options"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    SidebarEntryArray::Items layout_items;
    layout_items.push_back("Icon"_i18n);
    layout_items.push_back("Grid"_i18n);
    layout_items.push_back("HB Menu"_i18n);
    layout_items.push_back("List"_i18n);

    // Map layout index: Icon=1,Grid=2,HbMenu=3,List=0 -> sidebar index 0,1,2,3
    // LayoutType: List=0, Grid=1, GridDetail=2, HbMenu=3
    // We store: 0=Icon(Grid), 1=Grid(Grid), 2=HbMenu, 3=List
    static const int layout_map[] = {
        grid::LayoutType_Grid,     // Icon -> same as Grid for saves
        grid::LayoutType_Grid,     // Grid
        grid::LayoutType_HbMenu,   // HB Menu
        grid::LayoutType_List,     // List
    };
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
    }, cur_idx, "Choose how saves are displayed on screen."_i18n);

    options->Add<SidebarEntryArray>("Sort"_i18n, SidebarEntryArray::Items{"Updated"_i18n}, [this](s64& index_out){
        m_sort.Set(index_out);
        SortAndFindLastFile(false);
    }, m_sort.Get(), "Select which field to sort saves by."_i18n);

    options->Add<SidebarEntryArray>("Order"_i18n, SidebarEntryArray::Items{"Descending"_i18n, "Ascending"_i18n}, [this](s64& index_out){
        m_order.Set(index_out);
        SortAndFindLastFile(false);
    }, m_order.Get(), "Sort saves from newest to oldest or oldest to newest."_i18n);

    options->Add<SidebarEntryCallback>("Accounts"_i18n, [this](){
        DisplayAccountOptions();
    }, "Filter saves by user account."_i18n);

    options->Add<SidebarEntryCallback>("Data Types"_i18n, [this](){
        DisplayDataTypeOptions();
    }, "Choose which save data types to display."_i18n);

    options->Add<SidebarEntryHeader>("SYNC"_i18n);

    options->Add<SidebarEntryCallback>("Sync with remote"_i18n, [this](){
        SyncSavesRemote();
    }, "Two-way sync of backup ZIPs for the currently selected saves with a WebDAV location. Only the backup library on the microSD card is synced: the standard /dumps folder and the DBI folder /switch/DBI/saves. Backups saved to other folders or storage devices are not covered. Every archive missing on the other side is copied: new local backups are uploaded to WebDAV, new remote backups are downloaded to the SD card. Files with the same name are never overwritten and are not compared by date or content. After downloading from remote, use Restore to apply a backup."_i18n);

    options->Add<SidebarEntryCallback>("Advanced"_i18n, [this](){
        auto options = std::make_unique<Sidebar>("Advanced Options"_i18n, Sidebar::Side::RIGHT);
        ON_SCOPE_EXIT(App::Push(std::move(options)));

        options->Add<SidebarEntryBool>("Auto backup on restore"_i18n, m_auto_backup_on_restore.Get(), [this](bool& v_out){
            m_auto_backup_on_restore.Set(v_out);
        }, "Automatically create a backup before restoring a save."_i18n);

        options->Add<SidebarEntryBool>("Compress backup"_i18n, m_compress_save_backup.Get(), [this](bool& v_out){
            m_compress_save_backup.Set(v_out);
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
    m_list->OnUpdate(controller, touch, m_index, m_entries.size(), [this](bool touch, auto i) {
        if (touch && m_index == i) {
            FireAction(Button::A);
        } else {
            App::PlaySoundEffect(SoundEffect_Focus);
            SetIndex(i);
        }
    }, this);
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    if (m_entries.empty()) {
        gfx::drawTextArgs(vg, GetX() + GetW() / 2.f, GetY() + GetH() / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Empty..."_i18n.c_str());
        return;
    }

    if (m_layout.Get() == grid::LayoutType_HbMenu) {
        auto& e = m_entries[m_index];
        u64 id = IsSystemLikeSave(e.save_data_type) ? e.system_save_data_id : e.application_id;
        char title_id[33];
        std::snprintf(title_id, sizeof(title_id), "%016lX", id);
        
        const auto account = (e.save_data_type == FsSaveDataType_Account && !m_all_accounts) ?
            GetAccountName(e.uid) : GetAccountSummary();
        DrawHbMenuHeader(vg, theme, e.image, e.GetName(), e.GetAuthor(), title_id, account.c_str());
    }

    // max images per frame, in order to not hit io / gpu too hard.
    const int image_load_max = 2;
    int image_load_count = 0;

    m_list->Draw(vg, theme, m_entries.size(), [this, &image_load_count](NVGcontext* vg, Theme* theme, Vec4 v, s64 pos) {
        const auto& [x, y, w, h] = v;
        auto& e = m_entries[pos];

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

        const auto selected = pos == m_index;
        Vec4 image_v = v;
        if (!IsSystemLikeSave(e.save_data_type)) {
            image_v = DrawEntry(vg, theme, m_layout.Get(), v, selected, e.image, e.GetName(), e.GetAuthor(), "");
        } else {
            image_v = DrawEntryNoImage(vg, theme, m_layout.Get(), v, selected, e.GetName(), e.GetAuthor(), "");
            gfx::drawRect(vg, v, theme->GetColour(ThemeEntryID_GRID), 5);
            gfx::drawTextArgs(vg, image_v.x + image_v.w / 2, image_v.y + image_v.w / 2, 20, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT), detail::GetSystemSaveName(e.system_save_data_id));
        }

        if (e.selected) {
            gfx::drawRect(vg, image_v, theme->GetColour(ThemeEntryID_FOCUS), 5);
            gfx::drawText(vg, image_v.x + image_v.w / 2, image_v.y + image_v.h / 2, 24.f, "\uE14B", nullptr, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_SELECTED));
        }
    });
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
        SetTitleSubHeading(GetAccountSummary() + " | " + GetDataTypeSummary());
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

    if (m_accounts.empty()) {
        return;
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
    SetTitleSubHeading(title);
}

void Menu::ReadSaveEntries(u8 data_type, s64 account_index, std::vector<Entry>& out) const {
    if (m_accounts.empty()) {
        return;
    }

    FsSaveDataSpaceId space_id;
    FsSaveDataFilter filter;
    const auto index = account_index >= 0 ? account_index : 0;
    if (index >= static_cast<s64>(m_accounts.size())) {
        return;
    }

    GetFsSaveAttr(m_accounts[index], data_type, space_id, filter);

    FsSaveDataInfoReader reader;
    if (R_FAILED(fsOpenSaveDataInfoReaderWithFilter(&reader, space_id, &filter))) {
        log_write("[SAVE] failed to open reader\n");
        return;
    }
    ON_SCOPE_EXIT(fsSaveDataInfoReaderClose(&reader));

    std::vector<FsSaveDataInfo> info_list(ENTRY_CHUNK_COUNT);
    while (true) {
        s64 record_count{};
        if (R_FAILED(fsSaveDataInfoReaderRead(&reader, info_list.data(), info_list.size(), &record_count))) {
            log_write("failed fsSaveDataInfoReaderRead()\n");
            break;
        }

        // finished parsing all entries.
        if (!record_count) {
            break;
        }

        for (s32 i = 0; i < record_count; i++) {
            out.emplace_back(info_list[i]);
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

    if (m_accounts.empty()) {
        SetIndex(0);
        return;
    }

    if (m_account_enabled.size() != m_accounts.size()) {
        m_account_enabled.assign(m_accounts.size(), false);
        m_account_enabled[m_account_index] = true;
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
    m_entries = std::move(grouped);

    log_write("games found: %zu time_taken: %.2f seconds %zu ms %zu ns\n", m_entries.size(), ts.GetSecondsD(), ts.GetMs(), ts.GetNs());
    this->Sort();
    SetIndex(0);
    ClearSelection();
}

void Menu::Sort() {
    // const auto sort = m_sort.Get();
    const auto order = m_order.Get();

    if (order == OrderType_Ascending) {
        if (!m_is_reversed) {
            std::ranges::reverse(m_entries);
            m_is_reversed = true;
        }
    } else {
        if (m_is_reversed) {
            std::ranges::reverse(m_entries);
            m_is_reversed = false;
        }
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
        const auto row = m_list->GetRow();
        const auto page = m_list->GetPage();
        // guesstimate where the position is
        if (index >= page) {
            m_list->SetYoff((((index - page) + row) / row) * m_list->GetMaxY());
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
}

void Menu::OnLayoutChange() {
    m_index = 0;
    grid::Menu::OnLayoutChange(m_list, m_layout.Get());
}

void Menu::PromptSaveAction() {
    if (m_entries.empty()) {
        return;
    }

    PopupList::Items items;
    items.emplace_back("Backup"_i18n);
    items.emplace_back("Restore"_i18n);

    // Backup/Restore are navigation into a further options menu, not a value
    // choice, so render a submenu chevron rather than a "current value" tick.
    auto popup = std::make_unique<PopupList>("Save Action"_i18n, items, [this](auto op_index) {
        if (!op_index) {
            return;
        }

        PromptSaveTypeOptions(*op_index == 1);
    });
    popup->SetMenuStyle(true);
    App::Push(std::move(popup));
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

void Menu::PromptSaveTypeOptions(bool restore) {
    const auto seeds = GetSelectedEntries();
    std::vector<s64> all_account_indexes;
    for (s64 i = 0; i < static_cast<s64>(m_accounts.size()); i++) {
        all_account_indexes.emplace_back(i);
    }

    const std::vector<u8> all_types{SAVE_TYPES.begin(), SAVE_TYPES.end()};
    const auto available_entries = CollectActionEntries(seeds, all_types, all_account_indexes);
    if (available_entries.empty()) {
        App::Push<OptionBox>("No matching saves found."_i18n, "OK"_i18n);
        return;
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

    auto options = std::make_unique<Sidebar>(restore ? "Restore Options"_i18n : "Backup Options"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    options->Add<SidebarEntryCallback>(restore ? "Start Restore"_i18n : "Start Backup"_i18n, [this, state, seeds, restore]() {
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

        const auto location_index = std::min<s64>(state->location_index, static_cast<s64>(state->locations.size() - 1));
        const auto location = state->locations[location_index];
        const auto backup_root = state->location_base_paths[location_index];

        App::PopToMenu();
        if (restore) {
            StartRestore(entries, location, backup_root);
        } else {
            BackupSaves(entries, location, backup_root);
        }
    }, restore ? "Begin restoring saves from the selected location."_i18n : "Begin backing up saves to the selected location."_i18n);

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

    if (!restore) {
        options->Add<SidebarEntryBool>("Auto-sync after backup"_i18n, m_save_autosync.Get(), [this](bool& v_out){
            m_save_autosync.Set(v_out);
        }, "After each Backup, automatically upload only the newly created backup ZIP to WebDAV. Does not sync your whole backup library - use Sync with remote (Save Options) for that."_i18n);
    } else {
        options->Add<SidebarEntryBool>("Include remote backups"_i18n, m_restore_include_remote.Get(), [this](bool& v_out){
            m_restore_include_remote.Set(v_out);
        }, "Before showing the backup list, download any backups that exist on your WebDAV remote but are missing on this console, so they can be restored too. Remote-only backups are marked with a cloud icon. Only applies when a single save is selected."_i18n);
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
            "    " + i18n::get(GetSaveTypeLabel(type)) :
            i18n::get(GetSaveTypeLabel(type));

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

} // namespace sphaira::ui::menu::save
