#include "ui/menus/trash_menu.hpp"
#include "ui/menus/tools_menu.hpp"
#include "ui/menus/grid_menu_base.hpp"
#include "ui/menus/settings/settings_internal.hpp"
#include "ui/menus/save_menu.hpp"
#include "ui/progress_box.hpp"
#include "ui/hold_confirm_box.hpp"
#include "ui/option_box.hpp"
#include "app.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "log.hpp"
#include "trash.hpp"

#include <ctime>
#include <memory>

namespace sphaira::ui::menu::trash_page {
namespace {

constexpr s64 LOW_SPACE = 1024LL * 1024 * 1024; // below 1 GB free the oldest items go

auto KindName(const std::string& kind) -> std::string {
    if (kind == "save") return "Save"_i18n;
    if (kind == "backup") return "Save backup"_i18n;
    if (kind == "profile") return "Profile backup"_i18n;
    if (kind == "user") return "User profile and its saves"_i18n;
    return kind;
}

auto Row(std::string label, std::string description, std::function<void()> action) -> settings::SettingsItem {
    return { std::move(label), std::move(description), {}, std::move(action), settings::SettingsItemKind::Normal };
}

auto LowSpace() -> bool {
    s64 free{};
    return R_SUCCEEDED(fs::FsNativeSd().GetFreeSpace("/", &free)) && free < LOW_SPACE;
}

void Restore(const trash::Item& it, const std::function<void()>& reopen) {
    std::size_t kept = 0;
    const auto moved = trash::Restore(trash::SD, it.id, &kept);
    trash::Journal(trash::SD, kept ? "trash: restored in part" : "trash: restored", it.label, std::string{trash::DIR} + "/" + it.id, std::time(nullptr));
    log_write("[TRASH] restore %s: %zu back, %zu kept\n", it.id.c_str(), moved, kept);
    reopen();
    std::string msg = "Put back where it was. Restore a save from its backup in Saves, a profile in Users."_i18n;
    if (kept) {
        msg = "Some files stay in the Trash: a file with the same name is already in their place."_i18n + "\n\n" + msg;
    }
    App::Push<OptionBox>(msg, "OK"_i18n);
}

} // namespace

void Open() {
    trash::Purge(trash::SD, std::time(nullptr), LowSpace);
    // the page is rebuilt after every change: the old one leaves and a new one is pushed
    auto self = std::make_shared<tools::SystemToolsMenu*>(nullptr);
    const std::function<void()> reopen = [self]() {
        if (*self) {
            (*self)->SetPop();
        }
        Open();
    };

    const auto items = trash::List(trash::SD);
    std::vector<settings::SettingsItem> rows;
    rows.push_back(settings::MakeHeader("Kept for 30 days, or until the card is low on space"_i18n));
    if (items.empty()) {
        rows.push_back(Row("The Trash is empty"_i18n, "Saves, save backups and profiles that the Hub deleted. Restore them or delete them for good."_i18n, nullptr));
    }
    for (const auto& it : items) {
        char date[32]{};
        std::tm tm{};
        localtime_r(&it.when, &tm);
        std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M", &tm);
        const auto desc = KindName(it.kind) + " · " + date + " · " + grid::FormatBytes(it.bytes);
        rows.push_back(Row(it.label, desc, [it, desc, reopen]() {
            App::Push<OptionBox>(it.label + "\n" + desc, "Back"_i18n, "Delete forever"_i18n, "Restore"_i18n, 2, [it, reopen](auto op) {
                if (op && *op == 2) {
                    Restore(it, reopen);
                } else if (op && *op == 1) {
                    App::Push<HoldConfirmBox>("Delete it for good? It cannot be restored afterwards. Hold A to confirm."_i18n, [it, reopen](bool ok) {
                        if (ok) {
                            trash::Remove(trash::SD, it.id);
                            trash::Journal(trash::SD, "trash: deleted for good", it.label, "", std::time(nullptr));
                            reopen();
                        }
                    });
                }
            });
        }));
    }
    if (!items.empty()) {
        rows.push_back(Row("Empty the Trash"_i18n, "", [items, reopen]() {
            App::Push<HoldConfirmBox>("Delete everything in the Trash for good? It cannot be restored afterwards. Hold A to confirm."_i18n, [items, reopen](bool ok) {
                if (!ok) {
                    return;
                }
                for (const auto& it : items) {
                    trash::Remove(trash::SD, it.id);
                    trash::Journal(trash::SD, "trash: deleted for good", it.label, "", std::time(nullptr));
                }
                reopen();
            });
        }));
    }

    auto menu = std::make_unique<tools::SystemToolsMenu>("Trash"_i18n, std::move(rows));
    *self = menu.get();
    App::Push(std::move(menu));
}

auto RemovedUserSavesDeleter() -> decltype(cleanup::Options::delete_user_saves) {
    auto helper = std::make_shared<save::Menu>(MenuFlag_None);
    return [helper](ProgressBox* pbox, const std::vector<FsSaveDataInfo>& infos) -> Result {
        std::vector<save::Entry> entries(infos.size());
        for (size_t i = 0; i < infos.size(); i++) {
            static_cast<FsSaveDataInfo&>(entries[i]) = infos[i];
        }
        const auto now = std::time(nullptr);
        const auto label = "Saves of removed users"_i18n;
        const auto bin = trash::NewItem(trash::SD, "save", label, now);
        trash::Journal(trash::SD, "delete saves of removed users", label, bin, now);
        return helper->DeleteSavesOn(pbox, std::move(entries), bin);
    };
}

void PurgeOnStart() {
    const auto removed = trash::Purge(trash::SD, std::time(nullptr), LowSpace);
    if (removed) {
        log_write("[TRASH] purged %zu\n", removed);
        App::Notify("Trash: old items removed: "_i18n + std::to_string(removed));
    }
}

} // namespace sphaira::ui::menu::trash_page
