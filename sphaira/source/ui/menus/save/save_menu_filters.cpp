#include "app.hpp"
#include "i18n.hpp"
#include "ui/menus/save_menu.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "save_menu_internal.hpp"
#include "ui/sidebar.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace sphaira::ui::menu::save {

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
    if (!m_app_id_filter && m_layout.Get() == grid::LayoutType_Grid) {
        OnLayoutChange();
    }
    if (m_app_id_filter) {
        SetTitle(
            (m_category == Category::Installed) ? "Installed Games"_i18n :
            (m_category == Category::Deleted)   ? "Deleted Games"_i18n :
            (m_category == Category::Backups)   ? "Backups"_i18n : "Saves"_i18n
        );
    }
    App::PlaySoundEffect(SoundEffect_Focus);
    ScanHomebrew(true); // only the tab changed: keep the scanned backup library
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
        if (m_category != Category::Backups) {
            options->Add<SidebarEntryCallback>("Backup"_i18n, [this](){
                PromptSaveTypeOptions(SaveOp::Backup);
            }, "Backup selected saves."_i18n)->SetIcon(ActionIcon::Save);
        }
        options->Add<SidebarEntryCallback>("Restore"_i18n, [this](){
            PromptSaveTypeOptions(SaveOp::Restore);
        }, "Restore selected saves."_i18n)->SetIcon(ActionIcon::Refresh);
        if (m_category != Category::Backups) {
            options->Add<SidebarEntryCallback>("Delete"_i18n, [this](){
                PromptSaveTypeOptions(SaveOp::Delete);
            }, true, "Permanently delete save data for selected games."_i18n)->SetIcon(ActionIcon::Delete);
        }
    }

    SidebarEntryArray::Items layout_items;
    layout_items.push_back("Grid"_i18n);
    layout_items.push_back("HB Menu"_i18n);
    layout_items.push_back("List"_i18n);

    static const int layout_map_inv[] = {
        2, // LayoutType_List -> index 2
        0, // LayoutType_Grid -> index 0
        0, // LayoutType_GridDetail -> index 0
        1, // LayoutType_HbMenu -> index 1
    };
    const auto cur_layout = m_layout.Get();
    const auto cur_idx = (cur_layout >= 0 && cur_layout < 4) ? layout_map_inv[cur_layout] : 0;

    options->Add<SidebarEntryArray>("Layout"_i18n, layout_items, [this](s64& index_out){
        const int layout_map_local[] = {
            grid::LayoutType_Grid,
            grid::LayoutType_HbMenu,
            grid::LayoutType_List,
        };
        const auto new_layout = layout_map_local[std::clamp<s64>(index_out, 0, 2)];
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
        }, "ZIP restores always create a verified SD recovery archive regardless of this setting. RAW container restore is unsupported."_i18n);

        options->Add<SidebarEntryBool>("Compress backup"_i18n, App::GetSaveCompressBackup(), [](bool& v_out){
            App::SetSaveCompressBackup(v_out);
        }, "Save backups as compressed ZIP archives to reduce disk space."_i18n);
    }, "Access advanced backup and restore settings."_i18n);
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

} // namespace sphaira::ui::menu::save
