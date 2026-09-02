#include "ui/menus/users_menu.hpp"

#include "account/account_user.hpp"
#include "app.hpp"
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
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::ui::menu::users {
namespace {

struct ManageBackupsMenu final : MenuBase {
    using Callback = std::function<void(std::vector<account_user::Pack>)>;

    struct Entry {
        account_user::Pack pack;
        int image{};
        bool selected{};
        bool avatar_tried{};
    };

    ManageBackupsMenu(Callback on_restore)
        : MenuBase{"Manage Backups"_i18n, MenuFlag_None}
        , m_on_restore{std::move(on_restore)}
    {
        this->SetActions(
            std::make_pair(Button::A, Action{"Actions"_i18n, [this](){
                PromptAction();
            }}),
            std::make_pair(Button::B, Action{"Back"_i18n, [this](){
                if (m_selected_count > 0) {
                    ClearSelection();
                } else {
                    SetPop();
                }
            }}),
            std::make_pair(Button::X, Action{"Select"_i18n, [this](){
                ToggleCurrentSelection();
            }}),
            std::make_pair(Button::Y, Action{"Invert"_i18n, [this](){
                InvertSelection();
            }}),
            std::make_pair(Button::SELECT, Action{"Delete"_i18n, [this](){
                ConfirmDeletePacks();
            }}),
            std::make_pair(Button::START, Action{"Options"_i18n, [this](){
                PromptAction();
            }})
        );

        SetTitleSubHeading("A opens actions. X marks backups. Minus deletes."_i18n, true);
        m_list = std::make_unique<List>(1, 8, Vec4{75.f, 110.f, 1145.f, 560.f}, Vec4{75.f, 110.f, 1130.f, 80.f});
        Refresh();
    }

    ~ManageBackupsMenu() {
        FreeImages();
    }

    auto GetShortTitle() const -> const char* override { return "Backups"; }

    void FreeImages() {
        auto* vg = App::GetVg();
        for (auto& e : m_entries) {
            if (e.image > 0 && vg) {
                nvgDeleteImage(vg, e.image);
                e.image = 0;
            }
        }
    }

    void Refresh() {
        FreeImages();
        m_entries.clear();
        m_selected_count = 0;
        const auto packs = account_user::ListUserPacks();
        for (auto& p : packs) {
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

    auto TryLoadAvatar(Entry& e) -> bool {
        if (e.avatar_tried) {
            return false;
        }
        e.avatar_tried = true;
        std::vector<u8> jpeg;
        if (!account_user::ReadPackAvatar(e.pack, jpeg) || jpeg.empty()) {
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
                "%s", "No user backups found"_i18n.c_str());
            return;
        }
        int loaded = 0;
        m_list->Draw(vg, theme, m_entries.size(), [this, &loaded](auto* vg, auto* theme, Vec4 v, auto i) {
            auto& e = m_entries[i];
            if (loaded < 2 && TryLoadAvatar(e)) {
                loaded++;
            }
            const auto selected = m_index == i;
            if (selected) {
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

            const float icon_size = 46.f;
            const float icon_x = v.x + 50.f;
            const float icon_y = v.y + (v.h - icon_size) / 2.f;
            gfx::drawImage(vg, Vec4{icon_x, icon_y, icon_size, icon_size},
                e.image > 0 ? e.image : App::GetDefaultImage(), 4);

            const float text_x = icon_x + icon_size + 14.f;
            const auto link_status_str = e.pack.link_valid ? "Linked"_i18n : "Local"_i18n;
            const auto link_color = e.pack.link_valid ? nvgRGBA(80, 200, 120, 255) : theme->GetColour(ThemeEntryID_TEXT_INFO);

            float bounds[4]{};
            gfx::textBounds(vg, 0, 0, bounds, link_status_str.c_str());
            const float status_w = bounds[2] - bounds[0] + 20.f;
            gfx::drawText(vg, v.x + v.w - 15.f, v.y + v.h / 2.f, 16.f,
                link_color, link_status_str.c_str(),
                NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);

            nvgSave(vg);
            nvgIntersectScissor(vg, text_x, v.y, v.w - (text_x - v.x) - 15.f - status_w, v.h);
            gfx::drawTextArgs(vg, text_x, v.y + v.h / 2.f - 11.f, 20.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT),
                "%s", e.pack.nickname.c_str());

            std::string detail_str = !e.pack.created_label.empty() ? e.pack.created_label : e.pack.folder_name;
            detail_str += e.pack.has_playtime ? " В· play hours"_i18n : " В· no play hours"_i18n;
            gfx::drawTextArgs(vg, text_x, v.y + v.h / 2.f + 13.f, 15.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                theme->GetColour(ThemeEntryID_TEXT_INFO),
                "%s", detail_str.c_str());
            nvgRestore(vg);
        });
    }

    void OnFocusGained() override {
        MenuBase::OnFocusGained();
        Refresh();
    }

private:
    void PromptAction() {
        if (m_entries.empty()) {
            return;
        }

        const auto& cur = m_entries[m_index].pack;
        PopupList::Items items;
        items.emplace_back("Restore"_i18n);
        items.emplace_back("Duplicate"_i18n);
        items.emplace_back("Rename"_i18n);
        items.emplace_back("Delete"_i18n);
        items.emplace_back("Send to another console"_i18n);

        auto popup = std::make_unique<PopupList>(cur.nickname, items, [this](auto op_index) {
            if (!op_index) {
                return;
            }
            switch (*op_index) {
                case 0:
                    RestoreSelected();
                    break;
                case 1:
                    DuplicateCurrent();
                    break;
                case 2:
                    RenameCurrent();
                    break;
                case 3:
                    ConfirmDeletePacks();
                    break;
                case 4:
                    menu::StartConsoleTransferShareUserBackups();
                    break;
            }
        });
        popup->SetMenuStyle(true);
        App::Push(std::move(popup));
    }

    void RestoreSelected() {
        if (m_entries.empty()) {
            return;
        }
        std::vector<account_user::Pack> picked;
        if (m_selected_count > 0) {
            for (const auto& e : m_entries) {
                if (e.selected) {
                    picked.push_back(e.pack);
                }
            }
        } else {
            picked.push_back(m_entries[m_index].pack);
        }
        if (picked.empty()) {
            return;
        }
        auto cb = m_on_restore;
        SetPop();
        if (cb) {
            cb(std::move(picked));
        }
    }

    void DuplicateCurrent() {
        if (m_entries.empty() || m_index < 0 || static_cast<size_t>(m_index) >= m_entries.size()) {
            return;
        }
        DuplicatePack(m_entries[m_index].pack);
    }

    void RenameCurrent() {
        if (m_entries.empty() || m_index < 0 || static_cast<size_t>(m_index) >= m_entries.size()) {
            return;
        }
        RenamePack(m_entries[m_index].pack);
    }

    void DuplicatePack(const account_user::Pack& pack) {
        if (!pack.is_archive) {
            App::Push<OptionBox>("Duplicating legacy directory backups is not supported."_i18n, "OK"_i18n);
            return;
        }

        const auto src_path = pack.dir;
        fs::FsNativeSd sd;
        if (!sd.FileExists(src_path.c_str())) {
            App::Push<OptionBox>("Backup file does not exist."_i18n, "OK"_i18n);
            return;
        }

        std::string parent_dir = account_user::GetUserPacksRoot();
        if (const auto slash = src_path.find_last_of("/\\"); slash != std::string::npos) {
            parent_dir = src_path.substr(0, slash);
        }

        std::string name_stem = pack.folder_name;
        constexpr std::string_view zip_ext = ".kefir-user.zip";
        if (name_stem.size() >= zip_ext.size() && name_stem.ends_with(zip_ext)) {
            name_stem.resize(name_stem.size() - zip_ext.size());
        }

        std::string final_path = parent_dir + "/" + name_stem + "_copy" + std::string(zip_ext);
        int suffix = 1;
        while (sd.FileExists(final_path.c_str()) || sd.DirExists(final_path.c_str())) {
            final_path = parent_dir + "/" + name_stem + "_copy_" + std::to_string(suffix++) + std::string(zip_ext);
        }

        const std::string part_path = final_path + ".part";
        if (sd.FileExists(part_path.c_str())) {
            sd.DeleteFile(part_path.c_str());
        }

        App::Push<ProgressBox>(
            0,
            "Duplicate backup"_i18n,
            name_stem,
            [src_path, part_path, final_path](auto pbox) -> Result {
                pbox->NewTransfer("Duplicating backup..."_i18n);
                fs::FsNativeSd sd;
                bool success = false;
                bool renamed = false;
                ON_SCOPE_EXIT(
                    if (!success) {
                        sd.DeleteFile(part_path.c_str());
                        if (renamed) {
                            sd.DeleteFile(final_path.c_str());
                        }
                    }
                );

                auto rc = pbox->CopyFile(src_path, part_path);
                if (R_FAILED(rc)) {
                    return rc;
                }
                if (pbox->ShouldExit()) {
                    return Result_TransferCancelled;
                }

                rc = sd.RenameFile(part_path.c_str(), final_path.c_str());
                if (R_FAILED(rc)) {
                    return rc;
                }
                renamed = true;

                const auto check_pack = account_user::FindUserPack(final_path);
                if (check_pack.dir.empty()) {
                    return Result_FsInvalidType;
                }

                success = true;
                R_SUCCEED();
            },
            [this](Result rc) {
                if (rc == Result_TransferCancelled) {
                    return;
                }
                if (R_FAILED(rc)) {
                    App::Push<OptionBox>("Could not duplicate the backup."_i18n, "OK"_i18n);
                    return;
                }
                Refresh();
            },
            1, PRIO_PREEMPTIVE, 1024 * 128, false
        );
    }

    void RenamePack(const account_user::Pack& pack) {
        if (!pack.is_archive) {
            App::Push<OptionBox>("Renaming legacy directory backups is not supported."_i18n, "OK"_i18n);
            return;
        }

        const auto src_path = pack.dir;
        fs::FsNativeSd sd;
        if (!sd.FileExists(src_path.c_str())) {
            App::Push<OptionBox>("Backup file does not exist."_i18n, "OK"_i18n);
            return;
        }

        std::string parent_dir = account_user::GetUserPacksRoot();
        if (const auto slash = src_path.find_last_of("/\\"); slash != std::string::npos) {
            parent_dir = src_path.substr(0, slash);
        }

        std::string name_stem = pack.folder_name;
        constexpr std::string_view zip_ext = ".kefir-user.zip";
        if (name_stem.size() >= zip_ext.size() && name_stem.ends_with(zip_ext)) {
            name_stem.resize(name_stem.size() - zip_ext.size());
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

        if (input.size() >= zip_ext.size() && input.ends_with(zip_ext)) {
            input.resize(input.size() - zip_ext.size());
        }

        for (auto& c : input) {
            if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
                c = '_';
            }
        }

        const std::string final_path = parent_dir + "/" + input + std::string(zip_ext);
        if (final_path == src_path) {
            return;
        }

        if (sd.FileExists(final_path.c_str()) || sd.DirExists(final_path.c_str())) {
            App::Push<OptionBox>("A backup with that name already exists."_i18n, "OK"_i18n);
            return;
        }

        if (R_FAILED(sd.RenameFile(src_path.c_str(), final_path.c_str()))) {
            App::Push<OptionBox>("Could not rename the backup."_i18n, "OK"_i18n);
            return;
        }

        const auto check_pack = account_user::FindUserPack(final_path);
        if (check_pack.dir.empty()) {
            sd.RenameFile(final_path.c_str(), src_path.c_str());
            App::Push<OptionBox>("Renamed backup is invalid."_i18n, "OK"_i18n);
            return;
        }

        Refresh();
    }

    void ConfirmDeletePacks() {
        if (m_entries.empty()) {
            return;
        }
        std::vector<s64> idxs;
        if (m_selected_count > 0) {
            for (s64 i = 0; i < static_cast<s64>(m_entries.size()); i++) {
                if (m_entries[i].selected) {
                    idxs.push_back(i);
                }
            }
        } else {
            idxs.push_back(m_index);
        }
        const auto msg = (idxs.size() > 1)
            ? "Delete the selected backups from the SD card?"_i18n
            : "Delete this backup from the SD card?"_i18n;
        App::Push<OptionBox>(msg, "Cancel"_i18n, "Delete"_i18n, 1, [this, idxs](auto op) {
            if (!op || *op != 1) {
                return;
            }
            for (auto it = idxs.rbegin(); it != idxs.rend(); ++it) {
                const auto i = *it;
                if (i < 0 || static_cast<size_t>(i) >= m_entries.size()) {
                    continue;
                }
                if (R_FAILED(account_user::DeleteUserPack(m_entries[i].pack.dir))) {
                    App::Push<OptionBox>("Could not delete the backup."_i18n, "OK"_i18n);
                    return;
                }
                if (m_entries[i].image > 0) {
                    nvgDeleteImage(App::GetVg(), m_entries[i].image);
                }
                if (m_entries[i].selected) {
                    m_selected_count--;
                }
                m_entries.erase(m_entries.begin() + i);
            }
            if (m_index >= static_cast<s64>(m_entries.size())) {
                m_index = m_entries.empty() ? 0 : static_cast<s64>(m_entries.size()) - 1;
            }
            UpdateSubHeading();
        });
    }

    std::vector<Entry> m_entries;
    Callback m_on_restore;
    s64 m_index{};
    s64 m_selected_count{};
    std::unique_ptr<List> m_list;
};

} // namespace

void Menu::OpenManageBackups() {
    App::Push<ManageBackupsMenu>([this](std::vector<account_user::Pack> picked) {
        ConfirmPickedRestorePacks(std::move(picked));
    });
}

} // namespace sphaira::ui::menu::users
