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

constexpr u32 NX_SAVE_META_MAGIC = 0x4A4B5356; // JKSV
constexpr u32 NX_SAVE_META_VERSION = 1;
constexpr const char* NX_SAVE_META_NAME = ".nx_save_meta.bin";
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

// https://github.com/J-D-K/JKSV/issues/264#issuecomment-2618962807
struct NXSaveMeta {
    u32 magic{}; // NX_SAVE_META_MAGIC
    u32 version{}; // NX_SAVE_META_VERSION
    FsSaveDataAttribute attr{}; // FsSaveDataExtraData::attr
    u64 owner_id{}; // FsSaveDataExtraData::owner_id
    u64 timestamp{}; // FsSaveDataExtraData::timestamp
    u32 flags{}; // FsSaveDataExtraData::flags
    u32 unk_x54{}; // FsSaveDataExtraData::unk_x54
    s64 data_size{}; // FsSaveDataExtraData::data_size
    s64 journal_size{}; // FsSaveDataExtraData::journal_size
    u64 commit_id{}; // FsSaveDataExtraData::commit_id
    u64 raw_size{}; // FsSaveDataInfo::size
};
static_assert(sizeof(NXSaveMeta) == 128);

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

auto GetSaveFolder(u8 data_type) -> fs::FsPath {
    switch (data_type) {
        case FsSaveDataType_System:     return "Save System";
        case FsSaveDataType_SystemBcat: return "Save System BCAT";
        case FsSaveDataType_Account:    return "Save";
        case FsSaveDataType_Bcat:       return "Save BCAT";
        case FsSaveDataType_Device:     return "Save Device";
        case FsSaveDataType_Temporary:  return "Save Temporary";
        case FsSaveDataType_Cache:      return "Save Cache";
    }
    std::unreachable();
}

auto GetSaveTypeLabel(u8 data_type) -> const char* {
    switch (data_type) {
        case FsSaveDataType_System:     return "System";
        case FsSaveDataType_Account:    return "Account";
        case FsSaveDataType_Bcat:       return "BCAT";
        case FsSaveDataType_Device:     return "Device";
        case FsSaveDataType_Temporary:  return "Temporary";
        case FsSaveDataType_Cache:      return "Cache";
        case FsSaveDataType_SystemBcat: return "System BCAT";
    }
    return "Unknown";
}

auto SaveTypeIndex(u8 data_type) -> size_t {
    for (size_t i = 0; i < SAVE_TYPE_VALUES.size(); i++) {
        if (SAVE_TYPE_VALUES[i] == data_type) {
            return i;
        }
    }
    return 0;
}

auto SaveEntryKey(const FsSaveDataInfo& e) -> std::string {
    char key[0x80];
    std::snprintf(key, sizeof(key), "%u:%u:%016lX:%016lX:%016lX:%016lX:%u:%u",
        e.save_data_space_id, e.save_data_type, e.application_id,
        e.system_save_data_id, e.uid.uid[0], e.uid.uid[1],
        e.save_data_rank, e.save_data_index);
    return key;
}

auto IsSystemLikeSave(u8 data_type) -> bool {
    return data_type == FsSaveDataType_System || data_type == FsSaveDataType_SystemBcat;
}

auto DisplayEntryKey(const Entry& e) -> std::string {
    char key[0x40];
    if (IsSystemLikeSave(e.save_data_type)) {
        std::snprintf(key, sizeof(key), "system:%u:%016lX", e.save_data_type, e.system_save_data_id);
    } else {
        std::snprintf(key, sizeof(key), "app:%016lX", e.application_id);
    }
    return key;
}

auto GetSaveFolder(const Entry& e) {
    return GetSaveFolder(e.save_data_type);
}

// https://switchbrew.org/wiki/Flash_Filesystem#SystemSaveData
auto GetSystemSaveName(u64 system_save_data_id) -> const char* {
    switch (system_save_data_id) {
        case 0x8000000000000000: return "fs"; break;
        case 0x8000000000000010: return "account"; break;
        case 0x8000000000000011: return "account"; break;
        case 0x8000000000000020: return "nfc"; break;
        case 0x8000000000000030: return "ns"; break;
        case 0x8000000000000031: return "ns"; break;
        case 0x8000000000000040: return "ns"; break;
        case 0x8000000000000041: return "ns"; break;
        case 0x8000000000000043: return "ns"; break;
        case 0x8000000000000044: return "ns"; break;
        case 0x8000000000000045: return "ns"; break;
        case 0x8000000000000046: return "ns"; break;
        case 0x8000000000000047: return "ns"; break;
        case 0x8000000000000048: return "ns"; break;
        case 0x8000000000000049: return "ns"; break;
        case 0x800000000000004A: return "ns"; break;
        case 0x8000000000000050: return "settings"; break;
        case 0x8000000000000051: return "settings"; break;
        case 0x8000000000000052: return "settings"; break;
        case 0x8000000000000053: return "settings"; break;
        case 0x8000000000000054: return "settings"; break;
        case 0x8000000000000060: return "ssl"; break;
        case 0x8000000000000061: return "ssl"; break; // guessing
        case 0x8000000000000070: return "nim"; break;
        case 0x8000000000000071: return "nim"; break;
        case 0x8000000000000072: return "nim"; break;
        case 0x8000000000000073: return "nim"; break;
        case 0x8000000000000074: return "nim"; break;
        case 0x8000000000000075: return "nim"; break;
        case 0x8000000000000076: return "nim"; break;
        case 0x8000000000000077: return "nim"; break;
        case 0x8000000000000078: return "nim"; break;
        case 0x8000000000000080: return "friends"; break;
        case 0x8000000000000081: return "friends"; break;
        case 0x8000000000000082: return "friends"; break;
        case 0x8000000000000090: return "bcat"; break;
        case 0x8000000000000091: return "bcat"; break;
        case 0x8000000000000092: return "bcat"; break;
        case 0x80000000000000A0: return "bcat"; break;
        case 0x80000000000000A1: return "bcat"; break;
        case 0x80000000000000A2: return "bcat"; break;
        case 0x80000000000000B0: return "bsdsockets"; break;
        case 0x80000000000000C1: return "bcat"; break;
        case 0x80000000000000C2: return "bcat"; break;
        case 0x80000000000000D1: return "erpt"; break;
        case 0x80000000000000E0: return "es"; break;
        case 0x80000000000000E1: return "es"; break;
        case 0x80000000000000E2: return "es"; break;
        case 0x80000000000000E3: return "es"; break;
        case 0x80000000000000E4: return "es"; break;
        case 0x80000000000000F0: return "ns"; break;
        case 0x8000000000000100: return "pctl"; break;
        case 0x8000000000000110: return "npns"; break;
        case 0x8000000000000120: return "ncm"; break;
        case 0x8000000000000121: return "ncm"; break;
        case 0x8000000000000122: return "ncm"; break;
        case 0x8000000000000130: return "migration"; break;
        case 0x8000000000000131: return "migration"; break;
        case 0x8000000000000132: return "migration"; break;
        case 0x8000000000000133: return "migration"; break;
        case 0x8000000000000140: return "capsrv"; break;
        case 0x8000000000000150: return "olsc"; break;
        case 0x8000000000000151: return "olsc"; break;
        case 0x8000000000000152: return "olsc"; break;
        case 0x8000000000000153: return "olsc"; break;
        case 0x8000000000000180: return "sdb"; break;
        case 0x8000000000000190: return "glue"; break;
        case 0x8000000000000200: return "bcat"; break;
        case 0x8000000000000210: return "account"; break;
        case 0x8000000000000220: return "erpt"; break;
        case 0x8000000000001010: return "qlaunch"; break;
        case 0x8000000000001011: return "qlaunch"; break;
        case 0x8000000000001020: return "swkbd"; break;
        case 0x8000000000001021: return "swkbd"; break;
        case 0x8000000000001030: return "auth"; break;
        case 0x8000000000001040: return "miiEdit"; break;
        case 0x8000000000001050: return "miiEdit"; break;
        case 0x8000000000001060: return "LibAppletShop"; break;
        case 0x8000000000001061: return "LibAppletShop"; break;
        case 0x8000000000001070: return "LibAppletWeb"; break;
        case 0x8000000000001071: return "LibAppletWeb"; break;
        case 0x8000000000001080: return "LibAppletOff"; break;
        case 0x8000000000001081: return "LibAppletOff"; break;
        case 0x8000000000001090: return "LibAppletLns"; break;
        case 0x8000000000001091: return "LibAppletLns"; break;
        case 0x80000000000010A0: return "LibAppletAuth"; break;
        case 0x80000000000010A1: return "LibAppletAuth"; break;
        case 0x80000000000010B0: return "playerSelect"; break;
        case 0x80000000000010C0: return "myPage"; break;
        case 0x80000000000010E1: return "qlaunch"; break;
        case 0x8000000000001100: return "qlaunch"; break;
        case 0x8000000000002000: return "DevMenu"; break;
        case 0x8000000000002020: return "ns"; break;
        case 0x8000000000010002: return "bcat"; break;
        case 0x8000000000010003: return "bcat"; break;
        case 0x8000000000010004: return "bcat"; break;
        case 0x8000000000010005: return "bcat"; break;
        case 0x8000000000010006: return "bcat"; break;
        case 0x8000000000010007: return "bcat"; break;
    }

    return "Unknown";
}

void FakeNacpEntryForSystem(Entry& e) {
    e.status = title::NacpLoadStatus::Loaded;

    // fake the nacp entry
    std::snprintf(e.lang.name, sizeof(e.lang.name), "%s | %016lX", GetSystemSaveName(e.system_save_data_id), e.system_save_data_id);
    std::strcpy(e.lang.author, "Nintendo");
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

void LoadControlEntry(Entry& e, bool force_image_load = false) {
    if (e.status != title::NacpLoadStatus::Loaded) {
        if (IsSystemLikeSave(e.save_data_type)) {
            FakeNacpEntryForSystem(e);
        } else {
            LoadResultIntoEntry(e, title::Get(e.application_id));
        }
    }

    if (force_image_load && e.status == title::NacpLoadStatus::Loaded) {
        LoadControlImage(e, title::Get(e.application_id));
    }
}

struct HashStr {
    char str[0x21];
};

HashStr hexIdToStr(auto id) {
    HashStr str{};
    const auto id_lower = std::byteswap(*(u64*)id.c);
    const auto id_upper = std::byteswap(*(u64*)(id.c + 0x8));
    std::snprintf(str.str, 0x21, "%016lx%016lx", id_lower, id_upper);
    return str;
}

auto BuildSaveName(const Entry& e) -> fs::FsPath {
    fs::FsPath name_buf = e.GetName();
    title::utilsReplaceIllegalCharacters(name_buf, true);
    return name_buf;
}

auto BuildSaveBasePath(const Entry& e, bool force_id_path, const fs::FsPath& backup_root) -> fs::FsPath {
    fs::FsPath name;
    if (e.save_data_type == FsSaveDataType_System || e.save_data_type == FsSaveDataType_SystemBcat) {
        std::snprintf(name, sizeof(name), "%016lX", e.system_save_data_id);
    } else if (force_id_path || !strcasecmp(e.GetName(), "corrupted")) {
        std::snprintf(name, sizeof(name), "%016lX", e.application_id);
    } else {
        name = BuildSaveName(e);
    }

    return fs::AppendPath(fs::AppendPath(backup_root, GetSaveFolder(e)), name);
}

auto MakeSdCardDumpLocation() -> dump::DumpLocation {
    dump::DumpLocation location{};
    location.entry = {dump::DumpLocationType_SdCard, 0};
    return location;
}

auto MakeDumpLocationFromFsEntry(const filebrowser::FsEntry& fs_entry) -> dump::DumpLocation {
    dump::DumpLocation location{};
    if (fs_entry.type == filebrowser::FsType::Stdio) {
        location.entry = {dump::DumpLocationType_Stdio, 0};

        location::StdioEntry stdio{};
        stdio.mount = fs_entry.root.s;
        stdio.name = fs_entry.name.s;
        location.stdio.emplace_back(std::move(stdio));
    } else {
        location.entry = {dump::DumpLocationType_SdCard, 0};
    }

    return location;
}

auto NormalizeBackupRoot(const fs::FsPath& path, const filebrowser::FsEntry& fs_entry) -> fs::FsPath {
    auto out = path.toString();
    const auto root = fs_entry.root.toString();

    if (fs_entry.type == filebrowser::FsType::Stdio && out.starts_with(root)) {
        out.erase(0, root.size());
    }

    if (out.empty()) {
        out = "/";
    } else if (out.front() != '/') {
        out.insert(out.begin(), '/');
    }

    return out;
}

auto MakeLocationLabel(const std::string& name, const fs::FsPath& backup_root) -> std::string {
    return name + ": " + backup_root.toString();
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

    options->Add<SidebarEntryCallback>("Advanced"_i18n, [this](){
        auto options = std::make_unique<Sidebar>("Advanced Options"_i18n, Sidebar::Side::RIGHT);
        ON_SCOPE_EXIT(App::Push(std::move(options)));

        options->Add<SidebarEntryBool>("Auto backup on restore"_i18n, m_auto_backup_on_restore.Get(), [this](bool& v_out){
            m_auto_backup_on_restore.Set(v_out);
        }, "Automatically create a backup before restoring a save."_i18n);

        options->Add<SidebarEntryBool>("Compress backup"_i18n, m_compress_save_backup.Get(), [this](bool& v_out){
            m_compress_save_backup.Set(v_out);
        }, "Save backups as compressed ZIP archives to reduce disk space."_i18n);

        options->Add<SidebarEntryBool>("Auto-sync saves after backup"_i18n, m_save_autosync.Get(), [this](bool& v_out){
            m_save_autosync.Set(v_out);
        }, "Automatically upload backups to remote WebDAV after local backup."_i18n);
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

    m_list->Draw(vg, theme, m_entries.size(), [this, &image_load_count](auto* vg, auto* theme, auto v, auto pos) {
        const auto& [x, y, w, h] = v;
        auto& e = m_entries[pos];

        if (e.status == title::NacpLoadStatus::None) {
            if (!IsSystemLikeSave(e.save_data_type)) {
                title::PushAsync(e.application_id);
                e.status = title::NacpLoadStatus::Progress;
            } else {
                FakeNacpEntryForSystem(e);
            }
        } else if (e.status == title::NacpLoadStatus::Progress) {
            LoadResultIntoEntry(e, title::GetAsync(e.application_id));
        }

        // lazy load image
        if (image_load_count < image_load_max) {
            if (LoadControlImage(e, title::GetAsync(e.application_id))) {
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
            gfx::drawTextArgs(vg, image_v.x + image_v.w / 2, image_v.y + image_v.w / 2, 20, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT), GetSystemSaveName(e.system_save_data_id));
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
    items.emplace_back("Sync with remote"_i18n);

    App::Push<PopupList>("Save Action"_i18n, items, [this](auto op_index) {
        if (!op_index) {
            return;
        }

        if (*op_index == 2) {
            SyncSavesRemote();
        } else {
            PromptSaveTypeOptions(*op_index == 1);
        }
    });
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

    const fs::FsPath default_backup_root{"/dumps"};
    state->locations.emplace_back(MakeSdCardDumpLocation());
    state->location_base_paths.emplace_back(default_backup_root);
    state->location_items.emplace_back(MakeLocationLabel("microSD card"_i18n, default_backup_root));

    const auto stdio_locations = location::GetStdio(true);
    for (s32 i = 0; i < static_cast<s32>(stdio_locations.size()); i++) {
        dump::DumpLocation location{};
        location.entry = {dump::DumpLocationType_Stdio, i};
        location.stdio = stdio_locations;

        fs::FsPath backup_root = default_backup_root;
        if (!stdio_locations[i].dump_path.empty()) {
            backup_root = stdio_locations[i].dump_path;
        }
        state->locations.emplace_back(std::move(location));
        state->location_base_paths.emplace_back(backup_root);
        state->location_items.emplace_back(MakeLocationLabel(stdio_locations[i].name, backup_root));
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
            RestoreSaves(entries, location, backup_root);
        } else {
            BackupSaves(entries, location, backup_root);
        }
    }, restore ? "Begin restoring saves from the selected location."_i18n : "Begin backing up saves to the selected location."_i18n);

    options->Add<SidebarEntryHeader>("LOCATION"_i18n);
    auto* location_entry = options->Add<SidebarEntryTextBase>("Location"_i18n, state->location_items[state->location_index], [](){}, "Choose the folder where backups will be stored or read from."_i18n);
    location_entry->SetCallback([state, location_entry]() {
        auto items = state->location_items;
        const auto picker_index = static_cast<s64>(items.size());
        items.emplace_back("Choose Folder..."_i18n);

        App::Push<PopupList>("Location"_i18n, items, [state, location_entry, picker_index](auto op_index) {
            if (!op_index) {
                return;
            }

            if (*op_index != picker_index) {
                state->location_index = *op_index;
                location_entry->SetValue(state->location_items[state->location_index]);
                return;
            }

            App::Push<filepicker::Menu>(
                filepicker::LocationCallback{[state, location_entry](const fs::FsPath& path, const filebrowser::FsEntry& fs_entry) -> bool {
                    const auto backup_root = NormalizeBackupRoot(path, fs_entry);
                    const auto label = MakeLocationLabel(fs_entry.name.toString(), backup_root);

                    state->locations.emplace_back(MakeDumpLocationFromFsEntry(fs_entry));
                    state->location_base_paths.emplace_back(backup_root);
                    state->location_items.emplace_back(label);
                    state->location_index = static_cast<s64>(state->location_items.size() - 1);
                    location_entry->SetValue(label);
                    return true;
                }},
                std::vector<std::string>{},
                fs::FsPath{},
                true
            );
        }, state->location_index);
    });

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

void Menu::BackupSaves(std::vector<std::reference_wrapper<Entry>>& entries) {
    std::vector<Entry> copy;
    for (const auto& e : entries) {
        copy.emplace_back(e.get());
    }
    BackupSaves(std::move(copy));
}

void Menu::BackupSaves(std::vector<Entry> entries) {
    BackupSaves(std::move(entries), MakeSdCardDumpLocation(), "/dumps");
}

void Menu::BackupSaves(std::vector<Entry> entries, const dump::DumpLocation& location, const fs::FsPath& backup_root) {
    App::Push<ProgressBox>(0, "Backup"_i18n, "", [this, entries, location, backup_root](auto pbox) mutable -> Result {
        for (auto& e : entries) {
            // the entry may not have loaded yet.
            LoadControlEntry(e);
            R_TRY(BackupSaveInternal(pbox, location, e, m_compress_save_backup.Get(), false, backup_root));
        }
        R_SUCCEED();
    }, [this, entries, backup_root](Result rc){
        App::PushErrorBox(rc, "Backup failed!"_i18n);

        if (R_SUCCEEDED(rc)) {
            App::Notify("Backup successfull!"_i18n);

            if (m_save_autosync.Get()) {
                std::vector<location::Entry> webdav_locations;
                for (const auto& loc : location::Load()) {
                    if (loc.url.starts_with("webdav://") || loc.url.starts_with("http://") || loc.url.starts_with("https://")) {
                        webdav_locations.push_back(loc);
                    }
                }
                if (!webdav_locations.empty()) {
                    const auto& loc = webdav_locations.front();
                    App::Push<ProgressBox>(0, "Auto-syncing saves..."_i18n, "", [this, entries, loc, backup_root](auto pbox) mutable -> Result {
                        fs::FsNativeSd sd_fs;
                        for (auto& e : entries) {
                            LoadControlEntry(e);
                            fs::FsPath latest_path;
                            if (FindLatestBackupPath(&sd_fs, e, backup_root, latest_path)) {
                                std::string latest_path_str = latest_path.toString();
                                size_t last_slash = latest_path_str.find_last_of('/');
                                std::string filename = (last_slash != std::string::npos) ? latest_path_str.substr(last_slash + 1) : latest_path_str;
                                const auto remote_rel = "sphaira-saves/" + BuildSaveBasePath(e, false, "").toString();
                                pbox->NewTransfer("Uploading: "_i18n + filename);
                                
                                curl::Api api(CURL_LOCATION_TO_API(loc));
                                api.SetUpload(true);
                                api.SetOption(curl::Path{latest_path});
                                api.SetOption(curl::UploadInfo{remote_rel + "/" + filename});

                                auto res = curl::FromFile(api);
                                if (!res.success) {
                                    log_write("[SYNC] auto-sync failed to upload: %s\n", filename.c_str());
                                    R_THROW(Result_SaveSyncFailed);
                                }
                            }
                        }
                        R_SUCCEED();
                    }, [](Result rc){
                        if (R_FAILED(rc)) {
                            App::PushErrorBox(rc, "Auto-sync failed!"_i18n);
                        } else {
                            App::Notify("Auto-sync successfull!"_i18n);
                        }
                    });
                }
            }
        }
    });
}

bool Menu::FindLatestBackupPath(fs::Fs* fs, const Entry& e, const fs::FsPath& backup_root, fs::FsPath& path_out) const {
    filebrowser::FsDirCollection collections[2]{};
    for (auto i = 0; i < std::size(collections); i++) {
        const auto save_path = fs::AppendPath(fs->Root(), BuildSaveBasePath(e, i != 0, backup_root));
        filebrowser::FsView::get_collection(fs, save_path, "", collections[i], true, false, false);
        std::ranges::reverse(collections[i].files);
    }

    for (const auto& collection : collections) {
        for (const auto& p : collection.files) {
            const auto view = std::string_view{p.name};
            if (!view.ends_with(".zip")) {
                continue;
            }

            path_out = fs::AppendPath(collection.path, p.name);
            return true;
        }
    }

    return false;
}

void Menu::RestoreSaves(std::vector<Entry> entries) {
    RestoreSaves(std::move(entries), MakeSdCardDumpLocation(), "/dumps");
}

void Menu::RestoreSaves(std::vector<Entry> entries, const dump::DumpLocation& location, const fs::FsPath& backup_root) {
    auto restored = std::make_shared<size_t>(0);
    auto skipped = std::make_shared<size_t>(0);

    App::Push<ProgressBox>(0, "Restore"_i18n, "", [this, entries, location, backup_root, restored, skipped](auto pbox) mutable -> Result {
        std::unique_ptr<fs::Fs> fs;
        if (location.entry.type == dump::DumpLocationType_Stdio) {
            fs = std::make_unique<fs::FsStdio>(true, location.stdio[location.entry.index].mount);
        } else if (location.entry.type == dump::DumpLocationType_SdCard) {
            fs = std::make_unique<fs::FsNativeSd>();
        } else {
            std::unreachable();
        }

        for (auto& e : entries) {
            LoadControlEntry(e);

            fs::FsPath file_path;
            if (!FindLatestBackupPath(fs.get(), e, backup_root, file_path)) {
                (*skipped)++;
                continue;
            }

            if (m_auto_backup_on_restore.Get()) {
                pbox->SetActionName("Auto backup"_i18n);
                R_TRY(BackupSaveInternal(pbox, location, e, m_compress_save_backup.Get(), true, backup_root));
            }

            pbox->SetActionName("Restore"_i18n);
            R_TRY(RestoreSaveInternal(pbox, e, file_path));
            (*restored)++;
        }

        R_SUCCEED();
    }, [restored, skipped](Result rc){
        App::PushErrorBox(rc, "Restore failed!"_i18n);

        if (R_SUCCEEDED(rc)) {
            if (*restored) {
                App::Notify("Restore successfull!"_i18n);
            } else {
                App::Push<OptionBox>("No backups found for selected saves."_i18n, "OK"_i18n);
            }

            if (*skipped) {
                App::Notify(std::to_string(*skipped) + " saves skipped");
            }
        }
    });
}

auto Menu::BuildSavePath(const Entry& e, bool is_auto, const fs::FsPath& backup_root) const -> fs::FsPath {
    const auto t = std::time(NULL);
    const auto tm = std::localtime(&t);
    const auto base = BuildSaveBasePath(e, false, backup_root);

    char time[64];
    std::snprintf(time, sizeof(time), "%u.%02u.%02u @ %02u.%02u.%02u", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);

    fs::FsPath path;
    if (e.save_data_type == FsSaveDataType_Account) {
        const auto account = GetAccountName(e.uid);

        fs::FsPath name_buf;
        if (is_auto) {
            std::snprintf(name_buf, sizeof(name_buf), "AUTO - %s", account.c_str());
        } else {
            std::snprintf(name_buf, sizeof(name_buf), "%s", account.c_str());
        }

        title::utilsReplaceIllegalCharacters(name_buf, true);
        std::snprintf(path, sizeof(path), "%s/%s - %s.zip", base.s, name_buf.s, time);
    } else {
        std::snprintf(path, sizeof(path), "%s/%s.zip", base.s, time);
    }

    return path;
}

Result Menu::RestoreSaveInternal(ProgressBox* pbox, const Entry& e, const fs::FsPath& path) const {
    pbox->SetTitle(e.GetName());
    if (e.image) {
        pbox->SetImage(e.image);
    } else if (auto data = title::Get(e.application_id); data && !data->icon.empty()) {
        pbox->SetImageDataConst(data->icon);
    } else {
        pbox->SetImage(0);
    }

    const auto save_data_space_id = (FsSaveDataSpaceId)e.save_data_space_id;

    // try and get the journal and data size.
    FsSaveDataExtraData extra{};
    R_TRY(fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(&extra, sizeof(extra), save_data_space_id, e.save_data_id));

    log_write("restoring save: %s\n", path.s);
    zlib_filefunc64_def file_func;
    mz::FileFuncStdio(&file_func);

    auto zfile = unzOpen2_64(path, &file_func);
    R_UNLESS(zfile, Result_UnzOpen2_64);
    ON_SCOPE_EXIT(unzClose(zfile));
    log_write("opened zip\n");

    std::optional<NXSaveMeta> meta{};

    // get manifest
    if (UNZ_END_OF_LIST_OF_FILE != unzLocateFile(zfile, NX_SAVE_META_NAME, 0)) {
        log_write("found meta file\n");
        if (UNZ_OK == unzOpenCurrentFile(zfile)) {
            log_write("opened meta file\n");
            ON_SCOPE_EXIT(unzCloseCurrentFile(zfile));

            NXSaveMeta temp_meta;
            const auto len = unzReadCurrentFile(zfile, &temp_meta, sizeof(temp_meta));
            if (len == sizeof(temp_meta) && temp_meta.magic == NX_SAVE_META_MAGIC && temp_meta.version == NX_SAVE_META_VERSION) {
                meta = temp_meta;
                log_write("loaded meta!\n");
            }
        }
    }

    if (meta.has_value()) {
        log_write("extending save file\n");
        R_TRY(fsExtendSaveDataFileSystem(save_data_space_id, e.save_data_id, meta->data_size, meta->journal_size));
        log_write("extended save file\n");
    } else {
        log_write("doing manual meta parse\n");
        s64 total_size{};

        // todo:: manually calculate / guess the save size.
        unz_global_info64 ginfo;
        R_UNLESS(UNZ_OK == unzGetGlobalInfo64(zfile, &ginfo), Result_UnzGetGlobalInfo64);
        R_UNLESS(UNZ_OK == unzGoToFirstFile(zfile), Result_UnzGoToFirstFile);

        for (s64 i = 0; i < ginfo.number_entry; i++) {
            R_TRY(pbox->ShouldExitResult());

            if (i > 0) {
                R_UNLESS(UNZ_OK == unzGoToNextFile(zfile), Result_UnzGoToNextFile);
            }

            R_UNLESS(UNZ_OK == unzOpenCurrentFile(zfile), Result_UnzOpenCurrentFile);
            ON_SCOPE_EXIT(unzCloseCurrentFile(zfile));

            unz_file_info64 info;
            fs::FsPath name;
            R_UNLESS(UNZ_OK == unzGetCurrentFileInfo64(zfile, &info, name, sizeof(name), 0, 0, 0, 0), Result_UnzGetCurrentFileInfo64);

            if (name == NX_SAVE_META_NAME) {
                continue;
            }
            total_size += info.uncompressed_size;
        }

        // TODO: untested, should work tho.
        const auto rounded_size = total_size + (total_size % extra.journal_size);
        log_write("extendeing manual meta parse\n");
        R_TRY(fsExtendSaveDataFileSystem(save_data_space_id, e.save_data_id, rounded_size, extra.journal_size));
        log_write("extended manual meta parse\n");
    }

    FsSaveDataAttribute attr{};
    attr.application_id = e.application_id;
    attr.uid = e.uid;
    attr.system_save_data_id = e.system_save_data_id;
    attr.save_data_type = e.save_data_type;
    attr.save_data_rank = e.save_data_rank;
    attr.save_data_index = e.save_data_index;

    // try and open the save file system.
    fs::FsNativeSave save_fs{(FsSaveDataType)e.save_data_type, save_data_space_id, &attr, false};
    R_TRY(save_fs.GetFsOpenResult());

    // delete all files in save.
    filebrowser::FsDirCollections collections;
    R_TRY(filebrowser::FsView::get_collections(&save_fs, "/", "", collections));
    R_TRY(filebrowser::FsView::DeleteAllCollections(pbox, &save_fs, collections));

    log_write("opened save file\n");
    // restore save data from zip.
    pbox->NewTransfer("Restoring save..."_i18n);
    R_TRY(thread::TransferUnzipAll(pbox, zfile, &save_fs, "/", [&](const fs::FsPath& name, fs::FsPath& path) -> bool {
        // skip restoring the meta file.
        if (name == NX_SAVE_META_NAME) {
            log_write("skipping meta\n");
            return false;
        }

        // restore everything else.
        log_write("restoring: %s\n", path.s);
        return true;
    }));

    log_write("finished save backup\n");
    R_SUCCEED();
}

Result Menu::BackupSaveInternal(ProgressBox* pbox, const dump::DumpLocation& location, const Entry& e, bool compressed, bool is_auto, const fs::FsPath& backup_root) const {
    std::unique_ptr<fs::Fs> fs;
    if (location.entry.type == dump::DumpLocationType_Stdio) {
        fs = std::make_unique<fs::FsStdio>(true, location.stdio[location.entry.index].mount);
    } else if (location.entry.type == dump::DumpLocationType_SdCard) {
        fs = std::make_unique<fs::FsNativeSd>();
    } else {
        std::unreachable();
    }

    pbox->SetTitle(e.GetName());
    if (e.image) {
        pbox->SetImage(e.image);
    } else if (auto data = title::Get(e.application_id); data && !data->icon.empty()) {
        pbox->SetImageDataConst(data->icon);
    } else {
        pbox->SetImage(0);
    }

    const auto save_data_space_id = (FsSaveDataSpaceId)e.save_data_space_id;

    // try and get the journal and data size.
    FsSaveDataExtraData extra{};
    R_TRY(fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(&extra, sizeof(extra), save_data_space_id, e.save_data_id));

    FsSaveDataAttribute attr{};
    attr.application_id = e.application_id;
    attr.uid = e.uid;
    attr.system_save_data_id = e.system_save_data_id;
    attr.save_data_type = e.save_data_type;
    attr.save_data_rank = e.save_data_rank;
    attr.save_data_index = e.save_data_index;

    // try and open the save file system
    fs::FsNativeSave save_fs{(FsSaveDataType)e.save_data_type, save_data_space_id, &attr, true};
    R_TRY(save_fs.GetFsOpenResult());

    // get a list of collections.
    filebrowser::FsDirCollections collections;
    R_TRY(filebrowser::FsView::get_collections(&save_fs, "/", "", collections));

    // the save file may be empty, this isn't an error, but we exit early.
    R_UNLESS(!collections.empty(), 0x0);

    const auto t = (time_t)extra.timestamp;
    const auto tm = std::localtime(&t);

    // pre-calculate the time rather than calculate it in the loop.
    zip_fileinfo zip_info_default{};
    zip_info_default.tmz_date.tm_sec = tm->tm_sec;
    zip_info_default.tmz_date.tm_min = tm->tm_min;
    zip_info_default.tmz_date.tm_hour = tm->tm_hour;
    zip_info_default.tmz_date.tm_mday = tm->tm_mday;
    zip_info_default.tmz_date.tm_mon = tm->tm_mon;
    zip_info_default.tmz_date.tm_year = tm->tm_year;

    const auto path = fs::AppendPath(fs->Root(), BuildSavePath(e, is_auto, backup_root));
    const auto temp_path = path + ".temp";

    fs->CreateDirectoryRecursivelyWithPath(temp_path);
    ON_SCOPE_EXIT(fs->DeleteFile(temp_path));

    // zip to memory if less than 1GB and not applet mode.
    // TODO: use my mmz code from ftpsrv to stream zip creation.
    // this will allow for zipping to memory and flushing every X bytes
    // such as flushing every 8MB.
    const auto file_download = App::IsApplet() || e.size >= 1024ULL * 1024ULL * 1024ULL;

    mz::MzMem mz_mem{};
    zlib_filefunc64_def file_func;
    if (!file_download) {
        mz::FileFuncMem(&mz_mem, &file_func);
    } else {
        mz::FileFuncStdio(&file_func);
    }

    {
        auto zfile = zipOpen2_64(temp_path, APPEND_STATUS_CREATE, nullptr, &file_func);
        R_UNLESS(zfile, Result_ZipOpen2_64);
        ON_SCOPE_EXIT(zipClose(zfile, "sphaira v" APP_VERSION_HASH));

        // add save meta.
        {
            const NXSaveMeta meta{
                .magic = NX_SAVE_META_MAGIC,
                .version = NX_SAVE_META_VERSION,
                .attr = extra.attr,
                .owner_id = extra.owner_id,
                .timestamp = extra.timestamp,
                .flags = extra.flags,
                .unk_x54 = extra.unk_x54,
                .data_size = extra.data_size,
                .journal_size = extra.journal_size,
                .commit_id = extra.commit_id,
                .raw_size = e.size,
            };

            R_UNLESS(ZIP_OK == zipOpenNewFileInZip(zfile, NX_SAVE_META_NAME, &zip_info_default, NULL, 0, NULL, 0, NULL, Z_DEFLATED, Z_NO_COMPRESSION), Result_ZipOpenNewFileInZip);
            ON_SCOPE_EXIT(zipCloseFileInZip(zfile));
            R_UNLESS(ZIP_OK == zipWriteInFileInZip(zfile, &meta, sizeof(meta)), Result_ZipWriteInFileInZip);
        }

        const auto zip_add = [&](const fs::FsPath& file_path) -> Result {
            const char* file_name_in_zip = file_path.s;

            // strip root path (/ or ums0:)
            if (!std::strncmp(file_name_in_zip, save_fs.Root(), std::strlen(save_fs.Root()))) {
                file_name_in_zip += std::strlen(save_fs.Root());
            }

            // root paths are banned in zips, they will warn when extracting otherwise.
            while (file_name_in_zip[0] == '/') {
                file_name_in_zip++;
            }

            pbox->NewTransfer(file_name_in_zip);

            const auto level = compressed ? Z_DEFAULT_COMPRESSION : Z_NO_COMPRESSION;
            if (ZIP_OK != zipOpenNewFileInZip(zfile, file_name_in_zip, &zip_info_default, NULL, 0, NULL, 0, NULL, Z_DEFLATED, level)) {
                log_write("failed to add zip for %s\n", file_path.s);
                R_THROW(Result_ZipOpenNewFileInZip);
            }
            ON_SCOPE_EXIT(zipCloseFileInZip(zfile));

            return thread::TransferZip(pbox, zfile, &save_fs, file_path);
        };

        // loop through every save file and store to zip.
        for (const auto& collection : collections) {
            for (const auto& file : collection.files) {
                const auto file_path = fs::AppendPath(collection.path, file.name);
                R_TRY(zip_add(file_path));
            }
        }
    }

    // if we dumped the save to ram, flush the data to file.
    const auto is_file_based_emummc = App::IsFileBaseEmummc();
    if (!file_download) {
        pbox->NewTransfer("Flushing zip to file");
        R_TRY(fs->CreateFile(temp_path, mz_mem.buf.size(), 0));

        fs::File file;
        R_TRY(fs->OpenFile(temp_path, FsOpenMode_Write, &file));

        R_TRY(thread::Transfer(pbox, mz_mem.buf.size(),
            [&](void* data, s64 off, s64 size, u64* bytes_read) -> Result {
                size = std::min<s64>(size, mz_mem.buf.size() - off);
                std::memcpy(data, mz_mem.buf.data() + off, size);
                *bytes_read = size;
                R_SUCCEED();
            },
            [&](const void* data, s64 off, s64 size) -> Result {
                const auto rc = file.Write(off, data, size, FsWriteOption_None);
                if (is_file_based_emummc) {
                    svcSleepThread(2e+6); // 2ms
                }
                return rc;
            }
        ));
    }

    fs->DeleteFile(path);
    R_TRY(fs->RenameFile(temp_path, path));

    R_SUCCEED();
}

void Menu::SyncSavesRemote() {
    std::vector<location::Entry> webdav_locations;
    for (const auto& loc : location::Load()) {
        if (loc.url.starts_with("webdav://") || loc.url.starts_with("http://") || loc.url.starts_with("https://")) {
            webdav_locations.push_back(loc);
        }
    }

    if (webdav_locations.empty()) {
        App::Push<OptionBox>("No WebDAV network location configured for sync. Add one in settings."_i18n, "OK"_i18n);
        return;
    }

    const auto seeds = GetSelectedEntries();
    if (seeds.empty()) {
        App::Push<OptionBox>("No saves selected for sync."_i18n, "OK"_i18n);
        return;
    }

    if (webdav_locations.size() == 1) {
        SyncSavesRemoteWithLocation(webdav_locations.front());
    } else {
        PopupList::Items items;
        for (const auto& loc : webdav_locations) {
            items.emplace_back(loc.name);
        }
        App::Push<PopupList>("Select Sync Location"_i18n, items, [this, webdav_locations](auto op_index) {
            if (op_index) {
                SyncSavesRemoteWithLocation(webdav_locations[*op_index]);
            }
        });
    }
}

void Menu::SyncSavesRemoteWithLocation(const location::Entry& loc) {
    const auto seeds = GetSelectedEntries();
    if (seeds.empty()) {
        return;
    }

    App::Push<ProgressBox>(0, "Syncing saves..."_i18n, "", [this, seeds, loc](auto pbox) mutable -> Result {
        fs::FsNativeSd sd_fs;
        const fs::FsPath backup_root{"/dumps"};

        for (const auto& e : seeds) {
            pbox->SetTitle(e.GetName());
            if (e.image) {
                pbox->SetImage(e.image);
            } else if (auto data = title::Get(e.application_id); data && !data->icon.empty()) {
                pbox->SetImageDataConst(data->icon);
            } else {
                pbox->SetImage(0);
            }

            const auto local_base = BuildSaveBasePath(e, false, backup_root);
            const auto local_path = fs::AppendPath(sd_fs.Root(), local_base);

            filebrowser::FsDirCollection local_col{};
            filebrowser::FsView::get_collection(&sd_fs, local_path, "", local_col, true, false, false);

            const auto remote_rel = "sphaira-saves/" + BuildSaveBasePath(e, false, "").toString();
            pbox->NewTransfer("Listing remote files..."_i18n);
            const auto remote_files = curl::ListWebdav(loc.url, loc.user, loc.pass, remote_rel, loc.bearer, loc.pub_key, loc.priv_key, loc.port);

            std::vector<std::string> to_upload;
            for (const auto& f : local_col.files) {
                std::string fname(f.name);
                if (!fname.ends_with(".zip")) continue;
                if (std::ranges::find(remote_files, fname) == remote_files.end()) {
                    to_upload.push_back(fname);
                }
            }

            std::vector<std::string> to_download;
            for (const auto& f : remote_files) {
                if (!f.ends_with(".zip")) continue;
                bool found = false;
                for (const auto& lf : local_col.files) {
                    if (std::string(lf.name) == f) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    to_download.push_back(f);
                }
            }

            for (const auto& f : to_upload) {
                pbox->NewTransfer("Uploading: "_i18n + f);
                const auto local_file = fs::AppendPath(local_path, f);

                curl::Api api(CURL_LOCATION_TO_API(loc));
                api.SetUpload(true);
                api.SetOption(curl::Path{local_file});
                api.SetOption(curl::UploadInfo{remote_rel + "/" + f});

                auto res = curl::FromFile(api);
                if (!res.success) {
                    log_write("[SYNC] failed to upload: %s\n", f.c_str());
                    R_THROW(Result_SaveSyncFailed);
                }
            }

            for (const auto& f : to_download) {
                pbox->NewTransfer("Downloading: "_i18n + f);
                const auto local_file = fs::AppendPath(local_path, f);
                sd_fs.CreateDirectoryRecursivelyWithPath(local_path);

                const auto temp_file = local_file + ".temp";

                curl::Api api(CURL_LOCATION_TO_API(loc));
                api.SetOption(curl::Url{loc.url + "/" + remote_rel + "/" + f});
                api.SetOption(curl::Path{temp_file});

                auto res = curl::ToFile(api);
                if (!res.success) {
                    log_write("[SYNC] failed to download: %s\n", f.c_str());
                    sd_fs.DeleteFile(temp_file);
                    R_THROW(Result_SaveSyncFailed);
                }

                sd_fs.DeleteFile(local_file);
                R_TRY(sd_fs.RenameFile(temp_file, local_file));
            }
        }

        R_SUCCEED();
    }, [](Result rc){
        if (R_FAILED(rc)) {
            App::PushErrorBox(rc, "Sync failed!"_i18n);
        } else {
            App::Notify("Sync successfull!"_i18n);
        }
    });
}

} // namespace sphaira::ui::menu::save
