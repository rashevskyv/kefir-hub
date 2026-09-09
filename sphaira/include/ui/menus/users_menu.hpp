#pragma once

#include "ui/menus/grid_menu_base.hpp"
#include "ui/list.hpp"
#include "account/account_link.hpp"
#include "option.hpp"

#include <memory>
#include <string>
#include <vector>

namespace sphaira::account_user {
struct Pack;
}

namespace sphaira::ui::menu::save {
struct Entry;
}

namespace sphaira::ui::menu::users {

using LayoutType = grid::LayoutType;

struct Menu final : grid::Menu {
    Menu();
    ~Menu();

    auto GetShortTitle() const -> const char* override { return "Users"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

private:
    static constexpr inline const char* INI_SECTION = "users";

    struct Item : account_link::User {
        int image{};
        bool selected{};
        bool avatar_tried{};
    };

    void SetIndex(s64 index);
    void Refresh();
    void FreeImages();
    void OnLayoutChange();
    void ShowContextMenu();
    void ToggleCurrentSelection();
    void InvertSelection();
    void ClearSelection();
    auto SelectedUsers() const -> std::vector<account_link::User>;
    auto SelectedUids(bool all = false) const -> std::vector<AccountUid>;

    void ConfirmLinkNintendoAccount();
    void ConfirmUnlinkNintendoAccount();
    void ConfirmCreate();
    void ConfirmRename();
    void ConfirmChangeAvatar();
    void ConfirmBackup();
    void ConfirmNandBackup();
    void ConfirmNandRestore();
    void ConfirmNandManage();
    void ReceiveNandFromAnotherConsole();
    void RestoreNandFromAnotherConsole();
    void ConfirmRestoreBackup();
    void OpenManageBackups();
    void ConfirmPickedRestorePacks(std::vector<account_user::Pack> picked);
    void RunPrepareRestoreSnapshot(std::vector<account_user::Pack> packs);
    void ConfirmDelete();
    void RunLinkNintendoAccount();
    void RunUnlinkNintendoAccount(std::vector<AccountUid> uids);
    void RunRename(const std::string& nickname);
    void RunSetAvatar(std::vector<u8> jpeg);
    void RunBackup(std::vector<AccountUid> uids, bool overwrite_existing = false);
    void RunNandBackup();
    void RunNandRestore(const std::string& dir, bool restore_play_hours = true);
    void RunRestoreBackup(std::vector<account_user::Pack> picked_packs);
    void RunDelete(std::vector<save::Entry> save_backup);

    auto StatusLabel(const account_link::User& u) const -> std::string;
    auto TryLoadAvatar(Item& u) -> bool;

    std::vector<Item> m_items;
    s64 m_index{};
    s64 m_selected_count{};
    std::unique_ptr<List> m_list;
    ScrollingText m_name_scroll{};
    ScrollingText m_status_scroll{};
    ScrollingText m_uid_scroll{};
    option::OptionLong m_layout{INI_SECTION, "layout", LayoutType::LayoutType_GridDetail};
};

auto OfferPendingRestore() -> bool;
void StartRestoreBackup(std::vector<account_user::Pack> picked_packs);

} // namespace sphaira::ui::menu::users
