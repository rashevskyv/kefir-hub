#pragma once

#include "ui/menus/menu_base.hpp"
#include "ui/list.hpp"
#include "account/account_user.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sphaira::ui::menu::users {

struct ManageBackupsMenu final : MenuBase {
    using Callback = std::function<void(std::vector<account_user::Pack>)>;

    struct Entry {
        account_user::Pack pack;
        int image{};
        bool selected{};
        bool avatar_tried{};
    };

    ManageBackupsMenu(Callback on_restore);
    ~ManageBackupsMenu();

    auto GetShortTitle() const -> const char* override { return "Backups"; }

    void FreeImages();
    void Refresh();
    void UpdateSubHeading();
    void ToggleCurrentSelection(bool advance = true);
    void SelectAll();
    void InvertSelection();
    void ClearSelection();

    void Update(Controller* controller, TouchInfo* touch) override;
    auto TryLoadAvatar(Entry& e) -> bool;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

    void PromptAction();
    void RestoreSelected();
    void DuplicateCurrent();
    void RenameCurrent();
    void DuplicatePack(const account_user::Pack& pack);
    void RenamePack(const account_user::Pack& pack);
    void ConfirmDeletePacks();

    std::vector<Entry> m_entries;
    Callback m_on_restore;
    s64 m_index{};
    s64 m_selected_count{};
    std::unique_ptr<List> m_list;
};

void OpenUserLibrary(std::function<void(std::vector<account_user::Pack>)> on_restore);

} // namespace sphaira::ui::menu::users
