#include "app.hpp"
#include "log.hpp"
#include "i18n.hpp"
#include "ui/menus/save_menu.hpp"
#include "ui/menus/save/save_paths.hpp"
#include "ui/menus/save/save_locations.hpp"
#include "save/save_menu_internal.hpp"
#include "ui/layout.hpp"
#include "ui/list.hpp"
#include "yati/nx/ncm.hpp"
#include "yati/nx/nca.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace sphaira::ui::menu::save {

constinit UEvent g_change_uevent;

namespace {

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
        if (touch && DisplayToEntry(disp, g) < 0) {
            return;
        }
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
                // Room for a source label between adjacent backup rows.
                const Vec2 pad{10, m_category == Category::Backups ? 34.f : 10.f};
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

} // namespace sphaira::ui::menu::save
