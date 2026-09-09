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
#include "ui/menus/menu_base.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/sidebar.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sphaira::ui::menu::users {
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
            std::make_pair(Button::B, Action{"Back"_i18n, [this](){ SetPop(); }})
        );

        SetTitleSubHeading(
            "This pack restores all profiles together. Play hours may be restored with them."_i18n, true);
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
        std::string msg =
            "Restore this profiles & play hours pack?\n\n"
            "Profiles (0010) always restore as a whole pack.\n\n"_i18n;
        if (m_pack.save_00F0) {
            msg +=
                "If play hours are restored, this console's play log is replaced by the backup. "
                "Old hours here are lost; hours after restore start from the pack.\n\n"_i18n;
            App::Push<OptionBox>(
                msg,
                "Cancel"_i18n, "Profiles only"_i18n, "Profiles + play hours"_i18n, 2,
                [this](auto op) {
                    if (!op || *op == 0) {
                        return;
                    }
                    const bool hours = (*op == 2);
                    auto cb = m_on_restore;
                    const auto dir = m_pack.dir;
                    SetPop();
                    if (cb) {
                        cb(dir, hours);
                    }
                });
            return;
        }

        msg += "This pack has no play hours (00F0). Profiles will still restore."_i18n;
        App::Push<OptionBox>(
            msg,
            "Cancel"_i18n, "Restore profiles"_i18n, 1,
            [this](auto op) {
                if (!op || *op != 1) {
                    return;
                }
                auto cb = m_on_restore;
                const auto dir = m_pack.dir;
                SetPop();
                if (cb) {
                    cb(dir, false);
                }
            });
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

    NandPackLibraryMenu(RestoreCb on_restore)
        : MenuBase{"Manage Backups"_i18n, MenuFlag_None}
        , m_on_restore{std::move(on_restore)}
    {
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

    void ToggleCurrentSelection() {
        if (m_entries.empty()) {
            return;
        }
        m_entries[m_index].selected ^= 1;
        m_selected_count += m_entries[m_index].selected ? 1 : -1;
        if (m_index + 1 < static_cast<s64>(m_entries.size())) {
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
        if (m_entries.empty()) {
            return;
        }
        App::Push<NandPackDetailMenu>(
            m_entries[m_index].pack,
            m_on_restore);
    }

    void ShowContextMenu() {
        if (m_entries.empty()) {
            return;
        }

        std::string title = "Manage Backups"_i18n;
        if (m_index >= 0 && static_cast<size_t>(m_index) < m_entries.size()) {
            title = GetPackDisplayName(m_entries[m_index].pack);
        }

        auto options = std::make_unique<Sidebar>(title, Sidebar::Side::RIGHT);
        ON_SCOPE_EXIT(App::Push(std::move(options)));

        options->Add<SidebarEntryHeader>("ACTIONS"_i18n);

        options->Add<SidebarEntryCallback>("Open"_i18n, [this](){
            OpenCurrent();
        }, true, "Open pack details to inspect or restore accounts."_i18n);

        auto rename_entry = options->Add<SidebarEntryCallback>("Rename"_i18n, [this](){
            RenameCurrent();
        }, true, "Give this backup a custom name."_i18n);
        rename_entry->SetIcon(ActionIcon::Edit);
        if (m_selected_count > 1) {
            rename_entry->Depends([this](){ return m_selected_count <= 1; }, "Cannot rename multiple backups"_i18n);
        }

        auto delete_entry = options->Add<SidebarEntryCallback>("Delete"_i18n, [this](){
            ConfirmDelete();
        }, true, "Permanently delete the selected backup(s)."_i18n);
        delete_entry->SetIcon(ActionIcon::Delete);

        options->Add<SidebarEntryHeader>("SELECTION"_i18n);

        options->Add<SidebarEntryCallback>("Select All"_i18n, [this](){
            SelectAll();
        }, true, "Select all backups in the list."_i18n);

        if (m_selected_count > 0) {
            options->Add<SidebarEntryCallback>("Clear selection"_i18n, [this](){
                ClearSelection();
            }, true, "Deselect all backups."_i18n);
        }

        options->Add<SidebarEntryCallback>("Invert"_i18n, [this](){
            InvertSelection();
        }, true, "Invert current selection."_i18n);
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
    s64 m_index{};
    s64 m_selected_count{};
    std::unique_ptr<List> m_list;
};

} // namespace

void OpenNandPackLibrary(std::function<void(const std::string& dir, bool restore_play_hours)> on_restore) {
    App::Push<NandPackLibraryMenu>(std::move(on_restore));
}

} // namespace sphaira::ui::menu::users
