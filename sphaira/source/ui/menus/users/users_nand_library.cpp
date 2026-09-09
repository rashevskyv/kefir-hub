#include "ui/menus/users/users_nand_library.hpp"

#include "account/nand_transfer.hpp"
#include "app.hpp"
#include "app_paths.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "image.hpp"
#include "swkbd.hpp"
#include "ui/list.hpp"
#include "ui/menus/install_share.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/sidebar.hpp"
#include "download.hpp"
#include "ui/popup_list.hpp"
#include <yyjson.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sphaira::ui::menu::users {

void PromptNandPackRestore(const std::string& dir, bool save_00F0,
                           std::function<void(const std::string& dir, bool restore_play_hours)> on_restore,
                           std::function<void()> before_restore) {
    std::string msg =
        "Restore this profiles & play hours pack?\n\n"
        "Profiles (0010) always restore as a whole pack.\n\n"_i18n;
    if (save_00F0) {
        msg +=
            "If play hours are restored, this console's play log is replaced by the backup. "
            "Old hours here are lost; hours after restore start from the pack.\n\n"_i18n;
        App::Push<OptionBox>(
            msg,
            "Cancel"_i18n, "Profiles only"_i18n, "Profiles + play hours"_i18n, 2,
            [dir, on_restore, before_restore](auto op) {
                if (!op || *op == 0) {
                    return;
                }
                const bool hours = (*op == 2);
                if (before_restore) {
                    before_restore();
                }
                if (on_restore) {
                    on_restore(dir, hours);
                }
            });
        return;
    }

    msg += "This pack has no play hours (00F0). Profiles will still restore."_i18n;
    App::Push<OptionBox>(
        msg,
        "Cancel"_i18n, "Restore profiles"_i18n, 1,
        [dir, on_restore, before_restore](auto op) {
            if (!op || *op != 1) {
                return;
            }
            if (before_restore) {
                before_restore();
            }
            if (on_restore) {
                on_restore(dir, false);
            }
        });
}

namespace {

auto GetPackDisplayName(const nand_transfer::PackInfo& pack) -> std::string {
    std::string stem = pack.name;
    if (pack.is_archive) {
        constexpr std::string_view kKefirNandExt = ".kefir-nand.zip";
        constexpr std::string_view kZipExt = ".zip";
        if (stem.size() >= kKefirNandExt.size() && stem.ends_with(kKefirNandExt)) {
            stem.resize(stem.size() - kKefirNandExt.size());
        } else if (stem.size() >= kZipExt.size() && stem.ends_with(kZipExt)) {
            stem.resize(stem.size() - kZipExt.size());
        }
    }
    if (stem.size() == 15 && !pack.created_label.empty()) {
        return pack.created_label;
    }
    return stem;
}

struct NandPackDetailMenu final : MenuBase {
    using RestoreCb = std::function<void(const std::string& dir, bool restore_play_hours)>;

    struct Entry {
        nand_transfer::PackUser user;
        int image{};
        bool avatar_tried{};
    };

    NandPackDetailMenu(nand_transfer::PackInfo pack, RestoreCb on_restore)
        : MenuBase{GetPackDisplayName(pack), MenuFlag_None}
        , m_pack{std::move(pack)}
        , m_on_restore{std::move(on_restore)}
    {
        for (auto& u : nand_transfer::ListPackUsers(m_pack.dir)) {
            Entry e;
            e.user = std::move(u);
            m_entries.push_back(std::move(e));
        }

        this->SetActions(
            std::make_pair(Button::A, Action{"Restore"_i18n, [this](){ ConfirmRestore(); }}),
            std::make_pair(Button::B, Action{"Back"_i18n, [this](){ SetPop(); }}),
            std::make_pair(Button::START, Action{"Options"_i18n, [this](){ ShowContextMenu(); }})
        );

        SetTitleSubHeading(
            "+ opens options. A restores this pack."_i18n, true);
        m_list = std::make_unique<List>(2, 4, Vec4{75.f, 110.f, 1145.f, 560.f}, Vec4{75.f, 110.f, 555.f, 110.f}, Vec2{20.f, 15.f});
        UpdateSubHeading();
    }

    ~NandPackDetailMenu() {
        FreeImages();
    }

    auto GetShortTitle() const -> const char* override { return "Pack"; }

    void FreeImages() {
        auto* vg = App::GetVg();
        for (auto& e : m_entries) {
            if (e.image > 0 && vg) {
                nvgDeleteImage(vg, e.image);
                e.image = 0;
            }
        }
    }

    void UpdateSubHeading() {
        std::string sub = std::to_string(m_entries.size()) + " " + "accounts"_i18n;
        sub += m_pack.save_00F0 ? " - play hours yes"_i18n : " - play hours no"_i18n;
        SetSubHeading(sub);
    }

    static constexpr s64 GridSlotToEntry(s64 grid_idx) {
        return (grid_idx % 2) * 4 + (grid_idx / 2);
    }

    static constexpr s64 EntryToGridSlot(s64 entry_idx) {
        return (entry_idx % 4) * 2 + (entry_idx / 4);
    }

    void Update(Controller* controller, TouchInfo* touch) override {
        MenuBase::Update(controller, touch);
        if (m_entries.empty()) {
            return;
        }
        const auto old_index = m_index;
        m_list->OnUpdate(controller, touch, m_index, 8, [this](bool touch, auto i) {
            const auto entry_idx = GridSlotToEntry(i);
            if (entry_idx < static_cast<s64>(m_entries.size())) {
                if (touch && m_index == i) {
                    FireAction(Button::A);
                } else {
                    App::PlaySoundEffect(SoundEffect_Focus);
                    m_index = i;
                }
            }
        }, this);
        if (GridSlotToEntry(m_index) >= static_cast<s64>(m_entries.size())) {
            m_index = old_index;
        }
    }

    auto TryLoadAvatar(Entry& e) -> bool {
        if (e.avatar_tried) {
            return false;
        }
        e.avatar_tried = true;
        if (e.user.avatar_path.empty()) {
            return false;
        }
        std::vector<u8> jpeg;
        if (!nand_transfer::ReadPackUserAvatar(m_pack, e.user, jpeg) || jpeg.empty()) {
            return false;
        }
        auto img = ImageLoadFromMemory(jpeg, ImageFlag_JPEG);
        if (img.data.empty()) {
            img = ImageLoadIcon(jpeg);
        }
        if (img.data.empty()) {
            return false;
        }
        e.image = nvgCreateImageRGBA(App::GetVg(), img.w, img.h, 0, img.data.data());
        return true;
    }

    void Draw(NVGcontext* vg, Theme* theme) override {
        MenuBase::Draw(vg, theme);
        if (m_entries.empty()) {
            gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 22.f,
                NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
                theme->GetColour(ThemeEntryID_TEXT_INFO),
                "%s", "No accounts found in this pack"_i18n.c_str());
            return;
        }
        int loaded = 0;
        m_list->Draw(vg, theme, 8, [this, &loaded](auto* vg, auto* theme, Vec4 v, auto i) {
            const auto entry_idx = GridSlotToEntry(i);
            if (entry_idx >= static_cast<s64>(m_entries.size())) {
                return;
            }
            auto& e = m_entries[entry_idx];
            if (loaded < 2 && TryLoadAvatar(e)) {
                loaded++;
            }
            const auto selected = m_index == i;
            if (selected) {
                gfx::drawRectOutline(vg, theme, 4.f, v, 5.f);
            } else {
                DrawElement(v, ThemeEntryID_GRID);
            }

            const float icon_size = 46.f;
            const float icon_x = v.x + 20.f;
            const float icon_y = v.y + (v.h - icon_size) / 2.f;
            gfx::drawImage(vg, Vec4{icon_x, icon_y, icon_size, icon_size},
                e.image > 0 ? e.image : App::GetDefaultImage(), 4);

            const float text_x = icon_x + icon_size + 14.f;
            const float text_w = std::max(0.f, v.x + v.w - text_x - 12.f);
            nvgSave(vg);
            nvgIntersectScissor(vg, text_x, v.y, text_w, v.h);
            gfx::drawTextArgs(vg, text_x, v.y + v.h / 2.f - 11.f, 20.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT),
                "%s", e.user.nickname.c_str());
            gfx::drawTextArgs(vg, text_x, v.y + v.h / 2.f + 13.f, 15.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                theme->GetColour(ThemeEntryID_TEXT_INFO),
                "%s", e.user.uid.c_str());
            nvgRestore(vg);
        });
    }

private:
    void ConfirmRestore() {
        PromptNandPackRestore(m_pack.dir, m_pack.save_00F0, m_on_restore, [this](){
            SetPop();
        });
    }

    void ShowContextMenu() {
        auto options = std::make_unique<Sidebar>(GetPackDisplayName(m_pack), Sidebar::Side::RIGHT);
        ON_SCOPE_EXIT(App::Push(std::move(options)));

        options->Add<SidebarEntryHeader>("ACTIONS"_i18n);

        auto restore_entry = options->Add<SidebarEntryCallback>("Restore"_i18n, [this](){
            ConfirmRestore();
        }, true, "Restore profiles & play hours from this pack to console."_i18n);
        restore_entry->SetIcon(ActionIcon::Save);

        auto restore_remote_entry = options->Add<SidebarEntryCallback>("Restore from another console"_i18n, [this](){
            OpenRemoteNandTransfer(NandLibraryMode::Restore, [this](const std::string& dir, bool hours){
                SetPop();
                if (m_on_restore) {
                    m_on_restore(dir, hours);
                }
            });
        }, true, "Download and restore a backup from another console over Console Transfer."_i18n);
        restore_remote_entry->SetIcon(ActionIcon::Move);

        auto send_entry = options->Add<SidebarEntryCallback>("Send to another console"_i18n, [](){
            menu::StartConsoleTransferShareNandBackups();
        }, true, "Share backup(s) with another console over Console Transfer."_i18n);
        send_entry->SetIcon(ActionIcon::Move);
    }

    nand_transfer::PackInfo m_pack;
    RestoreCb m_on_restore;
    std::vector<Entry> m_entries;
    s64 m_index{};
    std::unique_ptr<List> m_list;
};

struct NandPackLibraryMenu final : MenuBase {
    using RestoreCb = std::function<void(const std::string& dir, bool restore_play_hours)>;

    struct Entry {
        nand_transfer::PackInfo pack;
        bool selected{};
    };

    NandPackLibraryMenu(RestoreCb on_restore, NandLibraryMode mode = NandLibraryMode::Manage)
        : MenuBase{mode == NandLibraryMode::Restore ? "Restore profiles & play hours"_i18n : "Manage Backups"_i18n, MenuFlag_None}
        , m_on_restore{std::move(on_restore)}
        , m_mode{mode}
    {
        if (m_mode == NandLibraryMode::Restore) {
            this->SetActions(
                std::make_pair(Button::A, Action{"Restore"_i18n, [this](){ ConfirmRestoreCurrent(); }}),
                std::make_pair(Button::B, Action{"Back"_i18n, [this](){
                    if (m_selected_count > 0) {
                        ClearSelection();
                    } else {
                        SetPop();
                    }
                }}),
                std::make_pair(Button::X, Action{"Select"_i18n, [this](){ ToggleCurrentSelection(); }}),
                std::make_pair(Button::Y, Action{"Invert"_i18n, [this](){ InvertSelection(); }}),
                std::make_pair(Button::SELECT, Action{"Delete"_i18n, [this](){ ConfirmDelete(); }}),
                std::make_pair(Button::START, Action{"Options"_i18n, [this](){ ShowContextMenu(); }})
            );
            SetTitleSubHeading("+ opens options. A restores selected pack. X marks backups."_i18n, true);
        } else {
            this->SetActions(
                std::make_pair(Button::A, Action{"Open"_i18n, [this](){ OpenCurrent(); }}),
                std::make_pair(Button::B, Action{"Back"_i18n, [this](){
                    if (m_selected_count > 0) {
                        ClearSelection();
                    } else {
                        SetPop();
                    }
                }}),
                std::make_pair(Button::X, Action{"Select"_i18n, [this](){ ToggleCurrentSelection(); }}),
                std::make_pair(Button::Y, Action{"Invert"_i18n, [this](){ InvertSelection(); }}),
                std::make_pair(Button::SELECT, Action{"Delete"_i18n, [this](){ ConfirmDelete(); }}),
                std::make_pair(Button::START, Action{"Options"_i18n, [this](){ ShowContextMenu(); }})
            );
            SetTitleSubHeading("+ opens options. A opens pack details. X marks backups."_i18n, true);
        }
        m_list = std::make_unique<List>(1, 8, Vec4{75.f, 110.f, 1145.f, 560.f}, Vec4{75.f, 110.f, 1130.f, 80.f});
        Refresh();
    }

    auto GetShortTitle() const -> const char* override { return "Packs"; }

    void Refresh() {
        m_entries.clear();
        m_selected_count = 0;
        for (auto& p : nand_transfer::ListPacks()) {
            Entry e;
            e.pack = std::move(p);
            m_entries.push_back(std::move(e));
        }
        if (m_index >= static_cast<s64>(m_entries.size())) {
            m_index = m_entries.empty() ? 0 : static_cast<s64>(m_entries.size()) - 1;
        }
        UpdateSubHeading();
    }

    void UpdateSubHeading() {
        if (m_entries.empty()) {
            SetSubHeading("0");
        } else if (m_selected_count > 0) {
            SetSubHeading(std::to_string(m_selected_count) + " / " + std::to_string(m_entries.size()));
        } else {
            SetSubHeading(std::to_string(m_entries.size()));
        }
    }

    void ToggleCurrentSelection(bool advance = true) {
        if (m_entries.empty() || m_index < 0 || static_cast<size_t>(m_index) >= m_entries.size()) {
            return;
        }
        m_entries[m_index].selected ^= 1;
        m_selected_count += m_entries[m_index].selected ? 1 : -1;
        if (advance && m_index + 1 < static_cast<s64>(m_entries.size())) {
            m_index++;
            m_list->EnsureVisible(m_index, m_entries.size());
        }
        UpdateSubHeading();
    }

    void SelectAll() {
        for (auto& e : m_entries) {
            e.selected = true;
        }
        m_selected_count = static_cast<s64>(m_entries.size());
        UpdateSubHeading();
    }

    void InvertSelection() {
        m_selected_count = 0;
        for (auto& e : m_entries) {
            e.selected ^= 1;
            if (e.selected) {
                m_selected_count++;
            }
        }
        UpdateSubHeading();
    }

    void ClearSelection() {
        for (auto& e : m_entries) {
            e.selected = false;
        }
        m_selected_count = 0;
        UpdateSubHeading();
    }

    void Update(Controller* controller, TouchInfo* touch) override {
        MenuBase::Update(controller, touch);
        if (m_entries.empty()) {
            return;
        }
        m_list->OnUpdate(controller, touch, m_index, m_entries.size(), [this](bool touch, auto i) {
            if (touch && m_index == i) {
                FireAction(Button::A);
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                m_index = i;
            }
        }, this);
    }

    void Draw(NVGcontext* vg, Theme* theme) override {
        MenuBase::Draw(vg, theme);
        if (m_entries.empty()) {
            gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 22.f,
                NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
                theme->GetColour(ThemeEntryID_TEXT_INFO),
                "%s", "No profiles & play hours packs found"_i18n.c_str());
            return;
        }
        m_list->Draw(vg, theme, m_entries.size(), [this](auto* vg, auto* theme, Vec4 v, auto i) {
            const auto& e = m_entries[i];
            const auto focused = m_index == i;
            if (focused) {
                gfx::drawRectOutline(vg, theme, 4.f, v, 5.f);
            } else {
                DrawElement(v, ThemeEntryID_GRID);
            }
            if (e.selected) {
                auto tint = theme->GetColour(ThemeEntryID_FOCUS);
                tint.a *= 0.35f;
                gfx::drawRect(vg, v, tint, 5.f);
            }

            gfx::drawCheckbox(vg, theme, v.x + 16.f, v.y + (v.h - gfx::CHECKBOX_SIZE) / 2.f,
                gfx::CHECKBOX_SIZE, e.selected);

            const float text_x = v.x + 50.f;
            const auto title = GetPackDisplayName(e.pack);
            gfx::drawTextArgs(vg, text_x, v.y + v.h / 2.f - 11.f, 20.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                theme->GetColour(focused ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT),
                "%s", title.c_str());

            std::string detail = std::to_string(e.pack.accounts) + " " + "accounts"_i18n;
            detail += e.pack.save_00F0 ? " - play hours"_i18n : " - no play hours"_i18n;
            if (e.pack.save_0010) {
                detail += " - profiles"_i18n;
            }
            if (!e.pack.created_label.empty() && title != e.pack.created_label) {
                detail = e.pack.created_label + " - " + detail;
            }
            gfx::drawTextArgs(vg, text_x, v.y + v.h / 2.f + 13.f, 15.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                theme->GetColour(ThemeEntryID_TEXT_INFO),
                "%s", detail.c_str());
        });
    }

    void OnFocusGained() override {
        MenuBase::OnFocusGained();
        Refresh();
    }

private:
    void OpenCurrent() {
        if (m_entries.empty() || m_index < 0 || static_cast<size_t>(m_index) >= m_entries.size()) {
            return;
        }
        App::Push<NandPackDetailMenu>(
            m_entries[m_index].pack,
            m_on_restore);
    }

    void ConfirmRestoreCurrent() {
        if (m_entries.empty() || m_index < 0 || static_cast<size_t>(m_index) >= m_entries.size()) {
            return;
        }
        const auto& pack = m_entries[m_index].pack;
        PromptNandPackRestore(pack.dir, pack.save_00F0, m_on_restore);
    }

    void ShowContextMenu() {
        const bool has_entries = !m_entries.empty() && m_index >= 0 && static_cast<size_t>(m_index) < m_entries.size();

        std::string title = (m_mode == NandLibraryMode::Restore)
            ? "Restore profiles & play hours"_i18n
            : "Manage Backups"_i18n;
        if (has_entries) {
            title = GetPackDisplayName(m_entries[m_index].pack);
        }

        auto options = std::make_unique<Sidebar>(title, Sidebar::Side::RIGHT);
        ON_SCOPE_EXIT(App::Push(std::move(options)));

        options->Add<SidebarEntryHeader>("ACTIONS"_i18n);

        if (m_mode == NandLibraryMode::Restore) {
            auto restore_entry = options->Add<SidebarEntryCallback>("Restore"_i18n, [this](){
                ConfirmRestoreCurrent();
            }, true, "Restore profiles & play hours to console."_i18n);
            restore_entry->SetIcon(ActionIcon::Save);
            if (!has_entries) {
                restore_entry->Depends([](){ return false; }, "No local backups to restore"_i18n);
            } else if (m_selected_count > 1) {
                restore_entry->Depends([this](){ return m_selected_count <= 1; }, "Cannot restore multiple backups simultaneously"_i18n);
            }

            auto restore_remote_entry = options->Add<SidebarEntryCallback>("Restore from another console"_i18n, [this](){
                OpenRemoteNandTransfer(NandLibraryMode::Restore, m_on_restore, [this](){ Refresh(); });
            }, true, "Download and restore a backup from another console over Console Transfer."_i18n);
            restore_remote_entry->SetIcon(ActionIcon::Move);

            auto open_entry = options->Add<SidebarEntryCallback>("Open"_i18n, [this](){
                OpenCurrent();
            }, true, "Open pack details to inspect accounts."_i18n);
            open_entry->SetIcon(ActionIcon::Folder);
            if (!has_entries) {
                open_entry->Depends([](){ return false; }, "No local backups available"_i18n);
            } else if (m_selected_count > 1) {
                open_entry->Depends([this](){ return m_selected_count <= 1; }, "Cannot open multiple backups"_i18n);
            }
        } else {
            auto open_entry = options->Add<SidebarEntryCallback>("Open"_i18n, [this](){
                OpenCurrent();
            }, true, "Open pack details to inspect or restore accounts."_i18n);
            open_entry->SetIcon(ActionIcon::Folder);
            if (!has_entries) {
                open_entry->Depends([](){ return false; }, "No local backups available"_i18n);
            } else if (m_selected_count > 1) {
                open_entry->Depends([this](){ return m_selected_count <= 1; }, "Cannot open multiple backups"_i18n);
            }

            auto restore_entry = options->Add<SidebarEntryCallback>("Restore"_i18n, [this](){
                ConfirmRestoreCurrent();
            }, true, "Restore profiles & play hours to console."_i18n);
            restore_entry->SetIcon(ActionIcon::Save);
            if (!has_entries) {
                restore_entry->Depends([](){ return false; }, "No local backups to restore"_i18n);
            } else if (m_selected_count > 1) {
                restore_entry->Depends([this](){ return m_selected_count <= 1; }, "Cannot restore multiple backups simultaneously"_i18n);
            }

            auto receive_entry = options->Add<SidebarEntryCallback>("Receive from another console"_i18n, [this](){
                OpenRemoteNandTransfer(NandLibraryMode::Manage, nullptr, [this](){ Refresh(); });
            }, true, "Receive backup(s) from another console over Console Transfer."_i18n);
            receive_entry->SetIcon(ActionIcon::Move);
        }

        auto rename_entry = options->Add<SidebarEntryCallback>("Rename"_i18n, [this](){
            RenameCurrent();
        }, true, "Give this backup a custom name."_i18n);
        rename_entry->SetIcon(ActionIcon::Edit);
        if (!has_entries) {
            rename_entry->Depends([](){ return false; }, "No backup selected"_i18n);
        } else if (m_selected_count > 1) {
            rename_entry->Depends([this](){ return m_selected_count <= 1; }, "Cannot rename multiple backups"_i18n);
        }

        auto delete_entry = options->Add<SidebarEntryCallback>("Delete"_i18n, [this](){
            ConfirmDelete();
        }, true, "Permanently delete the selected backup(s)."_i18n);
        delete_entry->SetIcon(ActionIcon::Delete);
        if (!has_entries) {
            delete_entry->Depends([](){ return false; }, "No backup selected"_i18n);
        }

        auto send_entry = options->Add<SidebarEntryCallback>("Send to another console"_i18n, [](){
            menu::StartConsoleTransferShareNandBackups();
        }, true, "Share backup(s) with another console over Console Transfer."_i18n);
        send_entry->SetIcon(ActionIcon::Move);
        if (!has_entries) {
            send_entry->Depends([](){ return false; }, "No backups available to send"_i18n);
        }

        if (has_entries) {
            options->Add<SidebarEntryHeader>("SELECTION"_i18n);

            const bool is_cur_selected = m_entries[m_index].selected;
            auto toggle_entry = options->Add<SidebarEntryCallback>(is_cur_selected ? "Deselect"_i18n : "Select"_i18n, [this](){
                ToggleCurrentSelection(false);
            }, true, is_cur_selected ? "Uncheck this backup."_i18n : "Mark this backup for batch operations."_i18n);
            toggle_entry->SetIcon(ActionIcon::Toggle);

            auto select_all_entry = options->Add<SidebarEntryCallback>("Select All"_i18n, [this](){
                SelectAll();
            }, true, "Select all backups in the list."_i18n);
            select_all_entry->SetIcon(ActionIcon::Range);

            if (m_selected_count > 0) {
                auto clear_entry = options->Add<SidebarEntryCallback>("Clear selection"_i18n, [this](){
                    ClearSelection();
                }, true, "Deselect all backups."_i18n);
                clear_entry->SetIcon(ActionIcon::Undo);
            }

            auto invert_entry = options->Add<SidebarEntryCallback>("Invert"_i18n, [this](){
                InvertSelection();
            }, true, "Invert current selection."_i18n);
            invert_entry->SetIcon(ActionIcon::Refresh);
        }
    }

    void RenameCurrent() {
        if (m_entries.empty() || m_index < 0 || static_cast<size_t>(m_index) >= m_entries.size()) {
            return;
        }
        RenamePack(m_entries[m_index].pack);
    }

    void RenamePack(const nand_transfer::PackInfo& pack) {
        const auto src_path = pack.dir;
        fs::FsNativeSd sd;
        const bool exists = pack.is_archive ? sd.FileExists(src_path.c_str()) : sd.DirExists(src_path.c_str());
        if (!exists) {
            App::Push<OptionBox>("Backup file does not exist."_i18n, "OK"_i18n);
            return;
        }

        std::string parent_dir;
        if (const auto slash = src_path.find_last_of("/\\"); slash != std::string::npos) {
            parent_dir = src_path.substr(0, slash);
        } else {
            parent_dir = paths::DATA_ROOT + "/nand_transfer";
        }

        std::string name_stem = pack.name;
        std::string ext;
        if (pack.is_archive) {
            constexpr std::string_view kKefirNandExt = ".kefir-nand.zip";
            constexpr std::string_view kZipExt = ".zip";
            if (name_stem.size() >= kKefirNandExt.size() && name_stem.ends_with(kKefirNandExt)) {
                ext = kKefirNandExt;
                name_stem.resize(name_stem.size() - kKefirNandExt.size());
            } else if (name_stem.size() >= kZipExt.size() && name_stem.ends_with(kZipExt)) {
                ext = kZipExt;
                name_stem.resize(name_stem.size() - kZipExt.size());
            }
        }

        std::string input;
        if (R_FAILED(swkbd::ShowText(input, "Rename backup"_i18n.c_str(), name_stem.c_str(), 1, 64)) || input.empty()) {
            return;
        }

        while (!input.empty() && (input.front() == ' ' || input.front() == '\t' || input.front() == '\r' || input.front() == '\n')) {
            input.erase(input.begin());
        }
        while (!input.empty() && (input.back() == ' ' || input.back() == '\t' || input.back() == '\r' || input.back() == '\n')) {
            input.pop_back();
        }
        if (input.empty()) {
            return;
        }

        if (!ext.empty() && input.size() >= ext.size() && input.ends_with(ext)) {
            input.resize(input.size() - ext.size());
        }

        for (auto& c : input) {
            if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
                c = '_';
            }
        }

        while (!input.empty() && (input.front() == ' ' || input.front() == '\t' || input.front() == '\r' || input.front() == '\n')) {
            input.erase(input.begin());
        }
        while (!input.empty() && (input.back() == ' ' || input.back() == '\t' || input.back() == '\r' || input.back() == '\n')) {
            input.pop_back();
        }
        if (input.empty()) {
            return;
        }

        const std::string final_path = parent_dir + "/" + input + ext;
        if (final_path == src_path) {
            return;
        }

        if (sd.FileExists(final_path.c_str()) || sd.DirExists(final_path.c_str())) {
            App::Push<OptionBox>("A backup with that name already exists."_i18n, "OK"_i18n);
            return;
        }

        Result rc;
        if (pack.is_archive) {
            rc = sd.RenameFile(src_path.c_str(), final_path.c_str());
        } else {
            rc = sd.RenameDirectory(src_path.c_str(), final_path.c_str());
        }
        if (R_FAILED(rc)) {
            App::Push<OptionBox>("Could not rename the backup."_i18n, "OK"_i18n);
            return;
        }

        const bool valid = pack.is_archive ? nand_transfer::IsPackArchive(final_path) : nand_transfer::IsPack(final_path);
        if (!valid) {
            if (pack.is_archive) {
                sd.RenameFile(final_path.c_str(), src_path.c_str());
            } else {
                sd.RenameDirectory(final_path.c_str(), src_path.c_str());
            }
            App::Push<OptionBox>("Renamed backup is invalid."_i18n, "OK"_i18n);
            return;
        }

        Refresh();
        for (size_t i = 0; i < m_entries.size(); ++i) {
            if (m_entries[i].pack.dir == final_path) {
                m_index = static_cast<s64>(i);
                m_list->EnsureVisible(m_index, m_entries.size());
                break;
            }
        }
    }

    void ConfirmDelete() {
        if (m_entries.empty()) {
            return;
        }
        std::vector<std::string> dirs;
        std::string progress_name;
        if (m_selected_count > 0) {
            for (const auto& e : m_entries) {
                if (e.selected) {
                    dirs.push_back(e.pack.dir);
                }
            }
            progress_name = std::to_string(dirs.size());
        } else {
            dirs.push_back(m_entries[m_index].pack.dir);
            progress_name = m_entries[m_index].pack.name;
        }
        if (dirs.empty()) {
            return;
        }
        const auto msg = (m_selected_count > 0)
            ? "Delete the selected backups from the SD card?"_i18n
            : "Delete this profiles & play hours pack from the SD card?"_i18n;
        App::Push<OptionBox>(
            msg,
            "Cancel"_i18n, "Delete"_i18n, 1,
            [this, dirs, progress_name](auto op) {
                if (!op || *op != 1) {
                    return;
                }
                App::Push<ProgressBox>(0, "Delete pack"_i18n, progress_name,
                    [dirs](auto pbox) -> Result {
                        pbox->NewTransfer("Deleting"_i18n);
                        fs::FsNativeSd sd;
                        for (const auto& item : dirs) {
                            if (sd.FileExists(item.c_str())) {
                                R_TRY(sd.DeleteFile(item.c_str()));
                            } else if (sd.DirExists(item.c_str())) {
                                R_TRY(sd.DeleteDirectoryRecursively(item.c_str()));
                            }
                        }
                        R_SUCCEED();
                    }, [this](Result rc) {
                        if (R_FAILED(rc)) {
                            App::Push<OptionBox>("Could not delete the pack."_i18n, "OK"_i18n);
                            return;
                        }
                        Refresh();
                        m_selected_count = 0;
                    }, 2);
            });
    }

    std::vector<Entry> m_entries;
    RestoreCb m_on_restore;
    NandLibraryMode m_mode{NandLibraryMode::Manage};
    s64 m_index{};
    s64 m_selected_count{};
    std::unique_ptr<List> m_list;
};

struct RemoteNandPack {
    std::string name;
    std::string remote_path;
    bool is_archive{};
    s64 size{};
};

void DownloadRemoteNandPacks(
    const std::string& base_url,
    const std::vector<RemoteNandPack>& packs,
    std::function<void(const std::string& last_pack_path)> on_complete)
{
    auto last_path = std::make_shared<std::string>();

    App::Push<ProgressBox>(0, "Downloading profiles & play hours backup..."_i18n, "",
        [base_url, packs, last_path](auto pbox) -> Result {
            fs::FsNativeSd sd;
            const std::string root_dst = paths::DATA_ROOT + "/nand_transfer";
            R_TRY(sd.CreateDirectoryRecursively(root_dst.c_str()));

            std::vector<std::string> temp_files;
            bool ok = false;
            ON_SCOPE_EXIT({
                if (!ok) {
                    for (const auto& f : temp_files) {
                        sd.DeleteFile(f.c_str());
                    }
                }
            });

            for (size_t i = 0; i < packs.size(); ++i) {
                if (pbox->ShouldExit()) return Result_TransferCancelled;
                const auto& pack = packs[i];

                std::string title = (packs.size() > 1)
                    ? ("[" + std::to_string(i + 1) + "/" + std::to_string(packs.size()) + "] " + pack.name)
                    : pack.name;
                pbox->NewTransfer(title);

                if (pack.is_archive) {
                    std::string stem = pack.name;
                    std::string ext = ".zip";
                    if (stem.ends_with(".kefir-nand.zip")) {
                        stem.erase(stem.size() - 15);
                        ext = ".kefir-nand.zip";
                    } else if (stem.ends_with(".zip")) {
                        stem.erase(stem.size() - 4);
                        ext = ".zip";
                    }

                    std::string target = root_dst + "/" + pack.name;
                    int suffix = 1;
                    while (sd.FileExists(target.c_str()) || sd.DirExists(target.c_str())) {
                        target = root_dst + "/" + stem + "_" + std::to_string(suffix++) + ext;
                    }

                    const std::string part_path = target + ".part";
                    temp_files.push_back(part_path);

                    curl::Api dl_api;
                    dl_api.SetOption(curl::Url{base_url + "/download?path=" + curl::EscapeString(pack.remote_path)});
                    dl_api.SetOption(curl::Path{part_path});
                    dl_api.SetOption(curl::OnProgress{pbox->OnDownloadProgressCallback()});
                    const auto dl_res = curl::ToFile(dl_api);
                    if (pbox->ShouldExit()) return Result_TransferCancelled;
                    if (!dl_res.success) return Result_FsInvalidType;

                    R_TRY(sd.RenameFile(part_path.c_str(), target.c_str()));
                    *last_path = target;
                } else {
                    curl::Api list_api;
                    list_api.SetOption(curl::Url{base_url + "/list-recursive?path=" + curl::EscapeString(pack.remote_path)});
                    list_api.SetOption(curl::OnProgress{[pbox](s64, s64, s64, s64) { return !pbox->ShouldExit(); }});
                    const auto list_res = curl::ToMemory(list_api);
                    if (pbox->ShouldExit()) return Result_TransferCancelled;
                    if (!list_res.success || list_res.data.empty()) return Result_FsInvalidType;

                    yyjson_doc* doc = yyjson_read((const char*)list_res.data.data(), list_res.data.size(), 0);
                    if (!doc) return Result_FsInvalidType;
                    ON_SCOPE_EXIT(yyjson_doc_free(doc));

                    yyjson_val* files_arr = yyjson_doc_get_root(doc);
                    if (!yyjson_is_arr(files_arr)) return Result_FsInvalidType;

                    std::string target_dir = root_dst + "/" + pack.name;
                    int suffix = 1;
                    while (sd.DirExists(target_dir.c_str()) || sd.FileExists(target_dir.c_str())) {
                        target_dir = root_dst + "/" + pack.name + "_" + std::to_string(suffix++);
                    }
                    R_TRY(sd.CreateDirectoryRecursively(target_dir.c_str()));

                    std::string root_prefix = pack.remote_path;
                    if (!root_prefix.empty() && root_prefix.back() != '/') root_prefix += '/';

                    size_t f_idx, f_max;
                    yyjson_val* f_item;
                    yyjson_arr_foreach(files_arr, f_idx, f_max, f_item) {
                        if (pbox->ShouldExit()) return Result_TransferCancelled;
                        const char* f_path = yyjson_get_str(yyjson_obj_get(f_item, "path"));
                        if (!f_path || !*f_path) continue;
                        std::string f_str = f_path;
                        if (!f_str.starts_with(root_prefix)) continue;
                        std::string rel = f_str.substr(root_prefix.size());
                        if (rel.empty() || rel.find("..") != std::string::npos) continue;

                        std::string dest_path = target_dir + "/" + rel;
                        R_TRY(sd.CreateDirectoryRecursivelyWithPath(dest_path.c_str()));

                        pbox->NewTransfer(rel);
                        curl::Api dl_api;
                        dl_api.SetOption(curl::Url{base_url + "/download?path=" + curl::EscapeString(f_str)});
                        dl_api.SetOption(curl::Path{dest_path});
                        dl_api.SetOption(curl::OnProgress{pbox->OnDownloadProgressCallback()});
                        const auto dl_res = curl::ToFile(dl_api);
                        if (pbox->ShouldExit()) return Result_TransferCancelled;
                        if (!dl_res.success) return Result_FsInvalidType;
                    }
                    *last_path = target_dir;
                }
            }
            ok = true;
            return Result_Success;
        },
        [on_complete, last_path](Result rc) {
            if (rc == Result_TransferCancelled) return;
            if (R_FAILED(rc) || last_path->empty()) {
                App::Push<OptionBox>("Failed to download backup files from the sending console."_i18n, "OK"_i18n);
                return;
            }
            if (on_complete) {
                on_complete(*last_path);
            }
        }
    );
}

} // namespace

void OpenRemoteNandTransfer(
    NandLibraryMode mode,
    std::function<void(const std::string& dir, bool restore_play_hours)> on_restore,
    std::function<void()> on_refresh)
{
    ConnectConsoleTransfer([mode, on_restore, on_refresh](const std::string& base_url) {
        auto remote_packs = std::make_shared<std::vector<RemoteNandPack>>();
        App::Push<ProgressBox>(0, "Fetching backup list..."_i18n, "",
            [base_url, remote_packs](auto pbox) -> Result {
                curl::Api api;
                api.SetOption(curl::Url{base_url + "/list"});
                api.SetOption(curl::OnProgress{[pbox](s64, s64, s64, s64) { return !pbox->ShouldExit(); }});
                const auto res = curl::ToMemory(api);
                if (pbox->ShouldExit()) return Result_TransferCancelled;
                if (!res.success || res.data.empty()) return Result_FsInvalidType;

                yyjson_doc* doc = yyjson_read((const char*)res.data.data(), res.data.size(), 0);
                if (!doc) return Result_FsInvalidType;
                ON_SCOPE_EXIT(yyjson_doc_free(doc));

                yyjson_val* root = yyjson_doc_get_root(doc);
                yyjson_val* path_val = yyjson_obj_get(root, "path");
                std::string root_path = yyjson_get_str(path_val) ? yyjson_get_str(path_val) : "";
                if (!root_path.empty() && root_path.back() != '/') root_path += '/';

                yyjson_val* entries = yyjson_obj_get(root, "entries");
                if (!yyjson_is_arr(entries)) return Result_FsInvalidType;

                size_t idx, max;
                yyjson_val* item;
                yyjson_arr_foreach(entries, idx, max, item) {
                    const char* name = yyjson_get_str(yyjson_obj_get(item, "name"));
                    if (!name || !*name || std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0) continue;
                    std::string_view sv{name};
                    if (sv.starts_with(".") || sv.starts_with("_") || sv.ends_with(".part") || sv.ends_with(".te")) continue;

                    int type = yyjson_get_int(yyjson_obj_get(item, "type"));
                    s64 size = (s64)yyjson_get_sint(yyjson_obj_get(item, "size"));
                    bool is_dir = (type == (int)FsDirEntryType_Dir);
                    bool is_archive = (type == (int)FsDirEntryType_File) && (sv.ends_with(".kefir-nand.zip") || sv.ends_with(".zip"));

                    if (is_dir || is_archive) {
                        remote_packs->push_back({
                            .name = name,
                            .remote_path = root_path + name,
                            .is_archive = is_archive,
                            .size = is_archive ? size : 0,
                        });
                    }
                }
                std::sort(remote_packs->begin(), remote_packs->end(), [](const auto& a, const auto& b) { return a.name > b.name; });
                return Result_Success;
            },
            [base_url, remote_packs, mode, on_restore, on_refresh](Result rc) {
                if (rc == Result_TransferCancelled) return;
                if (R_FAILED(rc) || remote_packs->empty()) {
                    App::Push<OptionBox>("No profiles & play hours backups found on the sending console."_i18n, "OK"_i18n);
                    return;
                }

                PopupList::Items items;
                if (mode == NandLibraryMode::Manage && remote_packs->size() > 1) {
                    items.push_back("[Receive All Backups]"_i18n);
                }
                for (const auto& p : *remote_packs) {
                    std::string label = p.name;
                    if (p.is_archive && p.size > 0) {
                        char sz[32]{};
                        if (p.size >= 1024 * 1024) {
                            std::snprintf(sz, sizeof(sz), " (%.1f MiB)", (double)p.size / (1024.0 * 1024.0));
                        } else {
                            std::snprintf(sz, sizeof(sz), " (%lld KiB)", (long long)(p.size / 1024));
                        }
                        label += sz;
                    }
                    items.push_back(std::move(label));
                }

                const std::string title = (mode == NandLibraryMode::Restore)
                    ? "Restore from another console"_i18n
                    : "Receive from another console"_i18n;

                auto popup = std::make_unique<PopupList>(title, items, [base_url, remote_packs, mode, on_restore, on_refresh](std::optional<s64> op) {
                    if (!op || *op < 0) return;
                    const size_t choice = static_cast<size_t>(*op);

                    std::vector<RemoteNandPack> to_download;
                    const bool has_all = (mode == NandLibraryMode::Manage && remote_packs->size() > 1);
                    if (has_all && choice == 0) {
                        to_download = *remote_packs;
                    } else {
                        const size_t pack_idx = has_all ? (choice - 1) : choice;
                        if (pack_idx < remote_packs->size()) {
                            to_download.push_back((*remote_packs)[pack_idx]);
                        }
                    }
                    if (to_download.empty()) return;

                    DownloadRemoteNandPacks(base_url, to_download, [mode, on_restore, on_refresh](const std::string& last_path) {
                        if (mode == NandLibraryMode::Restore) {
                            bool save_00f0 = false;
                            for (const auto& p : nand_transfer::ListPacks()) {
                                if (p.dir == last_path) {
                                    save_00f0 = p.save_00F0;
                                    break;
                                }
                            }
                            PromptNandPackRestore(last_path, save_00f0, on_restore);
                        } else {
                            App::Push<OptionBox>("Backup received successfully."_i18n, "OK"_i18n);
                            if (on_refresh) on_refresh();
                        }
                    });
                });
                popup->SetRemoteMarkers(std::vector<bool>(items.size(), true));
                App::Push(std::move(popup));
            }
        );
    });
}

void OpenNandPackLibrary(
    std::function<void(const std::string& dir, bool restore_play_hours)> on_restore,
    NandLibraryMode mode)
{
    App::Push<NandPackLibraryMenu>(std::move(on_restore), mode);
}

} // namespace sphaira::ui::menu::users
