// Game Details → Content tab: tick components with X / Y and delete them (START → Game actions, or A on one row).
#include "ui/menus/game/game_details_internal.hpp"

#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "title_info.hpp"
#include "yati/nx/ncm.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace sphaira::ui::menu::game {

auto DbiDetailsMenu::MarkedCount() const -> size_t {
    return static_cast<size_t>(std::count(m_marked.begin(), m_marked.end(), 1));
}

// X on the Content tab ticks the row under the cursor; Y ticks every row, or clears them when all are ticked.
void DbiDetailsMenu::SyncSelectActions() {
    if (m_tab == Tab::Content) {
        SetAction(Button::X, Action{"Select"_i18n, [this](){
            m_marked.resize(m_components.size(), 0);
            if (m_row_index >= 0 && m_row_index < static_cast<s64>(m_marked.size())) {
                m_marked[m_row_index] ^= 1;
            }
        }});
        SetAction(Button::Y, Action{"Select all"_i18n, [this](){
            m_marked.resize(m_components.size(), 0);
            const u8 value = MarkedCount() == m_marked.size() ? 0 : 1;
            std::fill(m_marked.begin(), m_marked.end(), value);
        }});
    } else if (m_tab == Tab::Saves) {
        SetAction(Button::X, Action{"Create save slot"_i18n, [this](){
            PromptCreateSaveSlot(CurrentEntry().app_id, CurrentEntry().GetName(), m_saves, [this](){ LoadGame(); });
        }});
        RemoveAction(Button::Y);
    } else {
        RemoveAction(Button::X);
        RemoveAction(Button::Y);
    }
}

// the ticked rows, or just the one under the cursor when none is ticked.
auto DbiDetailsMenu::DeleteTargets() const -> std::vector<GameComponentRow> {
    std::vector<GameComponentRow> out;
    for (size_t i = 0; i < m_marked.size() && i < m_components.size(); i++) {
        if (m_marked[i]) {
            out.push_back(m_components[i]);
        }
    }
    if (out.empty() && m_row_index >= 0 && m_row_index < static_cast<s64>(m_components.size())) {
        out.push_back(m_components[m_row_index]);
    }
    return out;
}

void DbiDetailsMenu::ConfirmDeleteComponents(std::vector<GameComponentRow> rows) {
    // game card content has no storage of ours to delete from.
    std::erase_if(rows, [](const GameComponentRow& r) {
        return r.status.storageID != NcmStorageId_SdCard && r.status.storageID != NcmStorageId_BuiltInUser;
    });
    if (rows.empty()) {
        App::Notify("Nothing here can be deleted"_i18n);
        return;
    }

    std::string list;
    for (const auto& r : rows) {
        list += std::string{ncm::GetMetaTypeStr(r.status.meta_type)} + " v" + std::to_string(r.status.version) + "\n";
    }

    App::Push<OptionBox>(
        "Delete these components?"_i18n + "\n\n" + list + "\n" + "The save data stays."_i18n,
        "Back"_i18n, "Delete"_i18n, 0, [this, rows = std::move(rows)](auto op_index){
            if (!op_index || *op_index != 1) {
                return;
            }
            App::Push<ProgressBox>(0, "Deleting"_i18n, "", [rows](ProgressBox* pbox) -> Result {
                for (const auto& r : rows) {
                    R_UNLESS(!pbox->ShouldExit(), pbox->ShouldExitResult());
                    pbox->NewTransfer(ncm::GetMetaTypeStr(r.status.meta_type));
                    R_TRY(title::DeleteComponent(r.status));
                }
                R_SUCCEED();
            }, [this](Result rc){
                if (R_FAILED(rc) && rc != Result_TransferCancelled) {
                    App::PushErrorBox(rc, "Delete failed!"_i18n);
                }
                LoadGame();
            });
        });
}

} // namespace sphaira::ui::menu::game
