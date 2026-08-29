#pragma once

#include "ui/menus/grid_menu_base.hpp"
#include "ui/list.hpp"
#include "account_link.hpp"
#include "option.hpp"

#include <memory>
#include <string>
#include <vector>

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

    void ConfirmPrepareExport();
    void ConfirmPrepareApply();
    void ConfirmPrepareLayoutProbe();
    void ConfirmPrepareAccountSaveDump();
    void ConfirmCreate();
    void ConfirmRename();
    void ConfirmChangeAvatar();
    void ConfirmBackup();
    void ConfirmNandBackup();
    void ConfirmNandRestore();
    void ConfirmRestore();
    void ConfirmDelete();
    void RunPrepareExport(const AccountUid& uid);
    void RunPrepareApply(const AccountUid& target_uid, const std::string& pkg_dir);
    void RunPrepareLayoutProbe(const AccountUid& uid);
    void RunPrepareAccountSaveDump();
    void RunRename(const std::string& nickname);
    void RunSetAvatar(std::vector<u8> jpeg);
    void RunBackup(std::vector<save::Entry> picked_saves = {});
    void RunNandBackup();
    void RunNandRestore(const std::string& dir);
    void RunRestore(const std::string& dir);
    void RunDelete(bool backup_account, std::vector<save::Entry> save_backup);

    auto StatusLabel(const account_link::User& u) const -> std::string;
    auto TryLoadAvatar(Item& u) -> bool;

    std::vector<Item> m_items;
    s64 m_index{};
    s64 m_selected_count{};
    std::unique_ptr<List> m_list;
    ScrollingText m_name_scroll{};
    option::OptionLong m_layout{INI_SECTION, "layout", LayoutType::LayoutType_GridDetail};
};

} // namespace sphaira::ui::menu::users
