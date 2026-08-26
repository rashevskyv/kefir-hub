#include "ui/menus/users_menu.hpp"

#include "account_user.hpp"
#include "app.hpp"
#include "app_paths.hpp"
#include "defines.hpp"
#include "nand_transfer.hpp"
#include "i18n.hpp"
#include "image.hpp"
#include "swkbd.hpp"
#include "ui/menus/file_picker.hpp"
#include "ui/menus/save/save_menu_detail.hpp"
#include "ui/menus/save_menu.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/sidebar.hpp"
#include "ui/steamgriddb_icon.hpp"
#include "utils/utils.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace sphaira::ui::menu::users {
namespace {

auto CollectSaves(const std::vector<AccountUid>& uids) -> std::vector<save::Entry> {
    title::Init();
    std::vector<save::Entry> out;
    for (const auto& uid : uids) {
        auto part = save::Menu::ListAccountSaves(uid);
        out.insert(out.end(), part.begin(), part.end());
    }
    for (auto& e : out) {
        save::detail::LoadControlEntry(e);
        e.selected = true;
    }
    title::Exit();
    return out;
}

struct SavePickMenu final : MenuBase {
    using Callback = std::function<void(std::optional<std::vector<save::Entry>>)>;

    SavePickMenu(std::vector<save::Entry> entries, Callback cb)
        : MenuBase{"Backup saves"_i18n, MenuFlag_None}
        , m_entries{std::move(entries)}
        , m_cb{std::move(cb)}
    {
        title::Init();
        this->SetActions(
            std::make_pair(Button::A, Action{"Backup"_i18n, [this](){
                std::vector<save::Entry> picked;
                for (auto& e : m_entries) {
                    if (e.selected) {
                        picked.push_back(e);
                    }
                }
                auto cb = m_cb;
                SetPop();
                cb(std::move(picked));
            }}),
            std::make_pair(Button::B, Action{"Cancel"_i18n, [this](){
                auto cb = m_cb;
                SetPop();
                cb(std::nullopt);
            }}),
            std::make_pair(Button::X, Action{"Select"_i18n, [this](){
                if (m_entries.empty()) {
                    return;
                }
                m_entries[m_index].selected ^= 1;
                if (m_index + 1 < static_cast<s64>(m_entries.size())) {
                    m_index++;
                    m_list->EnsureVisible(m_index, m_entries.size());
                }
            }})
        );
        m_list = std::make_unique<List>(1, 8, Vec4{75.f, 110.f, 1145.f, 560.f}, Vec4{75.f, 110.f, 1130.f, 70.f});
        SetTitleSubHeading("X marks games to back up. A backs up the marked saves, then deletes the user."_i18n, true);
        SetSubHeading(std::to_string(m_entries.size()));
    }

    ~SavePickMenu() {
        title::Exit();
    }

    auto GetShortTitle() const -> const char* override { return "Saves"; }

    void Update(Controller* controller, TouchInfo* touch) override {
        MenuBase::Update(controller, touch);
        if (m_entries.empty()) {
            return;
        }
        m_list->OnUpdate(controller, touch, m_index, m_entries.size(), [this](bool touch, auto i) {
            if (touch && m_index == i) {
                FireAction(Button::X);
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
                "%s", "No saves on this account"_i18n.c_str());
            return;
        }
        m_list->Draw(vg, theme, m_entries.size(), [this](auto* vg, auto* theme, Vec4 v, auto i) {
            const auto& e = m_entries[i];
            const auto selected = m_index == i;
            if (selected) {
                gfx::drawRectOutline(vg, theme, 4.f, v);
            }
            gfx::drawCheckbox(vg, theme, v.x + 16.f, v.y + (v.h - gfx::CHECKBOX_SIZE) / 2.f,
                gfx::CHECKBOX_SIZE, e.selected);
            const char* name = e.GetName();
            if (!name || !name[0]) {
                name = "Unknown";
            }
            gfx::drawTextArgs(vg, v.x + 56.f, v.y + v.h / 2.f, 18.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT),
                "%s", name);
        });
    }

private:
    std::vector<save::Entry> m_entries;
    Callback m_cb;
    s64 m_index{};
    std::unique_ptr<List> m_list;
};

} // namespace

Menu::Menu() : grid::Menu{"Users"_i18n, MenuFlag_None} {
    this->SetActions(
        std::make_pair(Button::A, Action{"Options"_i18n, [this](){ ShowContextMenu(); }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            if (m_selected_count) {
                ClearSelection();
            } else {
                SetPop();
            }
        }}),
        std::make_pair(Button::X, Action{"Select"_i18n, [this](){ ToggleCurrentSelection(); }}),
        std::make_pair(Button::Y, Action{"Invert"_i18n, [this](){ InvertSelection(); }}),
        std::make_pair(Button::START, Action{"Options"_i18n, [this](){ ShowContextMenu(); }})
    );
    OnLayoutChange();
}

Menu::~Menu() {
    FreeImages();
}

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();
    if (m_items.empty()) {
        Refresh();
    }
}

void Menu::FreeImages() {
    auto* vg = App::GetVg();
    for (auto& u : m_items) {
        if (u.image) {
            nvgDeleteImage(vg, u.image);
            u.image = 0;
        }
    }
}

void Menu::OnLayoutChange() {
    m_index = 0;
    grid::Menu::OnLayoutChange(m_list, m_layout.Get());
    SetIndex(0);
}

void Menu::Refresh() {
    FreeImages();
    const auto listed = account_link::ListUsers();
    m_items.clear();
    m_selected_count = 0;
    for (auto& u : listed) {
        Item item;
        static_cast<account_link::User&>(item) = std::move(u);
        m_items.push_back(std::move(item));
    }
    SetIndex(m_index);
}

void Menu::SetIndex(s64 index) {
    if (m_items.empty()) {
        m_index = 0;
        SetTitleSubHeading("No user profiles"_i18n, true);
        SetSubHeading("");
        return;
    }
    m_index = std::clamp<s64>(index, 0, static_cast<s64>(m_items.size() - 1));
    if (!m_index) {
        m_list->SetYoff(0);
    }
    const auto& item = m_items[m_index];
    SetTitleSubHeading(item.nickname + "  ·  " + StatusLabel(item), true);
    SetSubHeading(std::to_string(m_index + 1) + " / " + std::to_string(m_items.size()));
}

auto Menu::StatusLabel(const account_link::User& u) const -> std::string {
    if (!u.linked_known) {
        return {};
    }
    if (u.kind == account_link::LinkKind::Official) {
        return "Linked"_i18n;
    }
    if (u.kind == account_link::LinkKind::Offline) {
        return "Offline stub"_i18n;
    }
    return "Local"_i18n;
}

auto Menu::TryLoadAvatar(Item& u) -> bool {
    if (u.avatar_tried) {
        return false;
    }
    u.avatar_tried = true;
    std::vector<u8> jpeg;
    if (R_FAILED(account_user::LoadImageJpeg(u.uid, jpeg)) || jpeg.empty()) {
        return false;
    }
    auto img = ImageLoadFromMemory(jpeg, ImageFlag_JPEG);
    if (img.data.empty()) {
        img = ImageLoadIcon(jpeg);
    }
    if (img.data.empty()) {
        return false;
    }
    u.image = nvgCreateImageRGBA(App::GetVg(), img.w, img.h, 0, img.data.data());
    return true;
}

void Menu::ToggleCurrentSelection() {
    if (m_items.empty()) {
        return;
    }
    auto& item = m_items[m_index];
    item.selected ^= 1;
    m_selected_count += item.selected ? 1 : -1;
    if (m_index + 1 < static_cast<s64>(m_items.size())) {
        SetIndex(m_index + 1);
        m_list->EnsureVisible(m_index, m_items.size());
    }
}

void Menu::InvertSelection() {
    m_selected_count = 0;
    for (auto& item : m_items) {
        item.selected ^= 1;
        if (item.selected) {
            m_selected_count++;
        }
    }
}

void Menu::ClearSelection() {
    for (auto& item : m_items) {
        item.selected = false;
    }
    m_selected_count = 0;
}

auto Menu::SelectedUsers() const -> std::vector<account_link::User> {
    std::vector<account_link::User> out;
    for (const auto& u : m_items) {
        if (u.selected) {
            out.push_back(u);
        }
    }
    if (out.empty() && !m_items.empty()) {
        out.push_back(m_items[m_index]);
    }
    return out;
}

auto Menu::SelectedUids(bool all) const -> std::vector<AccountUid> {
    std::vector<AccountUid> uids;
    if (all) {
        for (const auto& u : m_items) {
            uids.push_back(u.uid);
        }
        return uids;
    }
    for (const auto& u : SelectedUsers()) {
        uids.push_back(u.uid);
    }
    return uids;
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);
    if (m_items.empty()) {
        return;
    }
    m_list->OnUpdate(controller, touch, m_index, m_items.size(), [this](bool touch, auto i) {
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

    if (m_items.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "%s", "No user profiles"_i18n.c_str());
        return;
    }

    int loaded{};
    m_list->Draw(vg, theme, m_items.size(), [this, &loaded](auto* vg, auto* theme, Vec4 v, auto i) {
        auto& item = m_items[i];
        if (loaded < 2 && TryLoadAvatar(item)) {
            loaded++;
        }
        const auto layout = m_layout.Get();
        const auto status = StatusLabel(item);
        const auto image_v = DrawEntry(vg, theme, layout, v, m_index == i, item.image,
            item.nickname.c_str(), status.c_str(), item.uid_hex.c_str(), item.selected);
        DrawSelectionMark(vg, theme, layout, v, image_v, item.selected, m_selected_count > 0);
    });
}

void Menu::ShowContextMenu() {
    auto options = std::make_unique<Sidebar>(
        m_items.empty() ? "Users"_i18n : m_items[m_index].nickname, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    options->Add<SidebarEntryHeader>("PROFILE"_i18n);
    options->Add<SidebarEntryCallback>("Create user"_i18n, [this](){
        ConfirmCreate();
    }, true, "Add a new local profile (up to 8)."_i18n);

    if (!m_items.empty()) {
        options->Add<SidebarEntryCallback>("Rename"_i18n, [this](){
            ConfirmRename();
        }, true, "Change this profile's display name."_i18n);
        options->Add<SidebarEntryCallback>("Change avatar"_i18n, [this](){
            ConfirmChangeAvatar();
        }, true, "Set a JPEG avatar from the SD card or SteamGridDB."_i18n);
        options->Add<SidebarEntryCallback>("Backup user"_i18n, [this](){
            ConfirmBackup();
        }, true, "Write name, avatar and Nintendo link to SD as a user pack."_i18n);
        options->Add<SidebarEntryCallback>("Delete user"_i18n, [this](){
            ConfirmDelete();
        }, true, "Remove the profile. You can back up the account and saves first. Saves are deleted after."_i18n);
    }
    options->Add<SidebarEntryCallback>("Restore user pack"_i18n, [this](){
        ConfirmRestore();
    }, true, "Create a new profile from a pack (name, avatar, link). New UID — play hours are not copied. Use Backup profiles & play hours to keep hours."_i18n);

    options->Add<SidebarEntryHeader>("CONSOLE MOVE"_i18n);
    options->Add<SidebarEntryCallback>("Backup profiles & play hours"_i18n, [this](){
        ConfirmNandBackup();
    }, true, "Decrypt account (0010) and play log (00F0) to SD. Restore on the other console with Restore profiles & play hours. Horizon encrypts with that console's keys. Destination hours are replaced."_i18n);
    options->Add<SidebarEntryCallback>("Restore profiles & play hours"_i18n, [this](){
        ConfirmNandRestore();
    }, true, "Write a decrypted pack into this console's system saves. Horizon encrypts with this console's keys. Hours and profiles on this NAND are replaced."_i18n);

    options->Add<SidebarEntryHeader>("NINTENDO ACCOUNT"_i18n);
    if (!m_items.empty()) {
        options->Add<SidebarEntryCallback>("Import official link"_i18n, [this](){
            ConfirmImport(false);
        }, true, "Copy baas/nas (including Nintendo tokens) from a sysNAND dump onto this profile."_i18n);
        options->Add<SidebarEntryCallback>("Unlink Nintendo Account"_i18n, [this](){
            ConfirmUnlink(false);
        }, true, "Remove the Nintendo Account data from this profile."_i18n);
        options->Add<SidebarEntryCallback>("Offline stub (Linkalho)"_i18n, [this](){
            ConfirmOffline(false);
        }, true, "Write fake baas/nas IDs with no Nintendo tokens. Games may retry Nintendo servers."_i18n);
    }
    options->Add<SidebarEntryCallback>("Export account save"_i18n, [this](){
        ConfirmExport();
    }, true, "Write this NAND's baas/nas to SD so you can import them on emuNAND."_i18n);
    options->Add<SidebarEntryCallback>("Import official link to all"_i18n, [this](){
        ConfirmImport(true);
    }, true, "Graft the dumped Nintendo Account onto every profile."_i18n);
    options->Add<SidebarEntryCallback>("Unlink all"_i18n, [this](){
        ConfirmUnlink(true);
    }, true, "Remove Nintendo Account data from every profile."_i18n);

    options->Add<SidebarEntryHeader>("VIEW"_i18n);
    SidebarEntryArray::Items layout_items;
    layout_items.push_back("List"_i18n);
    layout_items.push_back("Icon"_i18n);
    layout_items.push_back("Grid"_i18n);
    layout_items.push_back("HB Menu"_i18n);
    options->Add<SidebarEntryArray>("Layout"_i18n, layout_items, [this](s64& index_out){
        m_layout.Set(index_out);
        OnLayoutChange();
    }, m_layout.Get(), "Choose how user profiles are displayed."_i18n);
}

void Menu::ConfirmCreate() {
    if (m_items.size() >= ACC_USER_LIST_SIZE) {
        App::Push<OptionBox>("The console already has 8 user profiles."_i18n, "OK"_i18n);
        return;
    }
    std::string name;
    if (R_FAILED(swkbd::ShowText(name, "New user"_i18n.c_str(), nullptr, 1, 31)) || name.empty()) {
        return;
    }
    RunCreate(name);
}

void Menu::ConfirmRename() {
    if (m_items.empty()) {
        return;
    }
    std::string name;
    if (R_FAILED(swkbd::ShowText(name, "Rename"_i18n.c_str(), m_items[m_index].nickname.c_str(), 1, 31)) || name.empty()) {
        return;
    }
    RunRename(name);
}

void Menu::ConfirmChangeAvatar() {
    if (m_items.empty()) {
        return;
    }
    App::Push<OptionBox>(
        "Choose an avatar source."_i18n,
        "SD image"_i18n, "SteamGridDB"_i18n, 0,
        [this](auto op) {
            if (!op) {
                return;
            }
            if (*op == 0) {
                App::Push<filepicker::Menu>(
                    filepicker::Callback{[this](const fs::FsPath& path) -> bool {
                        std::vector<u8> jpeg;
                        fs::FsNativeSd sd;
                        std::vector<u8> file;
                        if (R_SUCCEEDED(sd.read_entire_file(path, file))) {
                            jpeg = ImageNormalizeIcon(file);
                        }
                        if (jpeg.empty()) {
                            App::Push<OptionBox>("Could not read that image."_i18n, "OK"_i18n);
                            return true;
                        }
                        RunSetAvatar(std::move(jpeg));
                        return true;
                    }},
                    std::vector<std::string>{"jpg", "jpeg", "png", "bmp"});
                return;
            }
            steamgriddb::ShowIconPicker(m_items[m_index].nickname, [this](std::vector<u8> icon) {
                if (icon.empty()) {
                    return;
                }
                auto jpeg = ImageNormalizeIcon(icon);
                if (jpeg.empty()) {
                    jpeg = std::move(icon);
                }
                RunSetAvatar(std::move(jpeg));
            });
        });
}

void Menu::ConfirmBackup() {
    const auto uids = SelectedUids();
    if (uids.empty()) {
        return;
    }
    App::Push<OptionBox>(
        "Write a user pack (name, avatar, Nintendo link) to SD?"_i18n,
        "Cancel"_i18n, "Backup"_i18n, 1,
        [this](auto op) {
            if (op && *op == 1) {
                RunBackup();
            }
        });
}

void Menu::ConfirmNandBackup() {
    App::Push<OptionBox>(
        "Decrypt this NAND's profiles (0010) and play hours (00F0) to SD. On the destination open Restore profiles & play hours and pick the pack. Horizon encrypts with that console's keys. Destination hours and profiles will be replaced. Back up SYSTEM first. emuNAND recommended."_i18n,
        "Cancel"_i18n, "Backup"_i18n, 1,
        [this](auto op) {
            if (op && *op == 1) {
                RunNandBackup();
            }
        });
}

void Menu::ConfirmNandRestore() {
    App::Push<OptionBox>(
        "Write a Kefir pack (0010/00F0) into this NAND? Hours and profiles here will be replaced. Back up SYSTEM first. emuNAND recommended. Y selects the pack folder."_i18n,
        "Cancel"_i18n, "Choose folder"_i18n, 1,
        [this](auto op) {
            if (!op || *op != 1) {
                return;
            }
            App::Push<filepicker::Menu>(
                filepicker::LocationCallback{[this](const fs::FsPath& path, const filebrowser::FsEntry&) -> bool {
                    RunNandRestore(path.toString());
                    return true;
                }},
                std::vector<std::string>{},
                fs::FsPath{paths::DATA_ROOT + "/nand_transfer"},
                true);
        });
}

void Menu::ConfirmRestore() {
    App::Push<OptionBox>(
        "Pick a user pack folder (profile.json + avatar.jpg). A new local profile is created (new UID, no play hours). Y selects the folder."_i18n,
        "Cancel"_i18n, "Choose folder"_i18n, 1,
        [this](auto op) {
            if (!op || *op != 1) {
                return;
            }
            App::Push<filepicker::Menu>(
                filepicker::LocationCallback{[this](const fs::FsPath& path, const filebrowser::FsEntry&) -> bool {
                    RunRestore(path.toString());
                    return true;
                }},
                std::vector<std::string>{},
                fs::FsPath{paths::DATA_ROOT + "/user_packs"},
                true);
        });
}

void Menu::ConfirmDelete() {
    const auto uids = SelectedUids();
    if (uids.empty()) {
        return;
    }
    App::Push<OptionBox>(
        "Delete the selected user(s)? Game saves for those users will be deleted afterwards."_i18n,
        "Cancel"_i18n, "Delete"_i18n, 0,
        [this, uids](auto op) {
            if (!op || *op != 1) {
                return;
            }
            App::Push<OptionBox>(
                "Back up the user profile (name, avatar, Nintendo link) first?"_i18n,
                "Skip"_i18n, "Backup account"_i18n, 1,
                [this, uids](auto op) {
                    if (!op) {
                        return;
                    }
                    const bool backup_account = *op == 1;
                    auto saves = CollectSaves(uids);
                    if (saves.empty()) {
                        RunDelete(backup_account, {});
                        return;
                    }
                    App::Push<OptionBox>(
                        "Back up game saves for these users? You can pick which games."_i18n,
                        "Skip"_i18n, "Choose saves"_i18n, 1,
                        [this, backup_account, saves](auto op) mutable {
                            if (!op) {
                                return;
                            }
                            if (*op == 0) {
                                RunDelete(backup_account, {});
                                return;
                            }
                            App::Push<SavePickMenu>(std::move(saves), [this, backup_account](auto picked) {
                                if (!picked) {
                                    return;
                                }
                                RunDelete(backup_account, std::move(*picked));
                            });
                        });
                });
        });
}

void Menu::ConfirmImport(bool all) {
    App::Push<OptionBox>(
        "Pick a folder dumped from a console that was linked officially (usually sysNAND). It must contain baas/ and nas/, including id.token and refresh.token. Y selects the folder. This is the method that does not keep retrying Nintendo servers."_i18n,
        "Cancel"_i18n, "Choose folder"_i18n, 1,
        [this, all](auto op) {
            if (!op || *op != 1) {
                return;
            }
            App::Push<filepicker::Menu>(
                filepicker::LocationCallback{[this, all](const fs::FsPath& path, const filebrowser::FsEntry&) -> bool {
                    RunImport(all, path.toString());
                    return true;
                }},
                std::vector<std::string>{},
                fs::FsPath{},
                true);
        });
}

void Menu::ConfirmOffline(bool all) {
    App::Push<OptionBox>(
        "Offline stub writes random Nintendo IDs with no real tokens (same as Linkalho). Horizon then retries Nintendo servers, which shows up as Please wait / airplane-mode nags. Prefer Import official link. A reboot is required."_i18n,
        "Cancel"_i18n, "Write stub"_i18n, 0,
        [this, all](auto op) {
            if (op && *op == 1) {
                RunOffline(all);
            }
        });
}

void Menu::ConfirmUnlink(bool all) {
    App::Push<OptionBox>(
        "Remove the Nintendo Account from the selected profile(s)? A reboot is required."_i18n,
        "Cancel"_i18n, "Unlink"_i18n, 1,
        [this, all](auto op) {
            if (op && *op == 1) {
                RunUnlink(all);
            }
        });
}

void Menu::ConfirmExport() {
    App::Push<OptionBox>(
        "Export baas/ and nas/ from this NAND to SD. On a clean sysNAND with a real Nintendo Account, export here, then import that folder on emuNAND."_i18n,
        "Cancel"_i18n, "Export"_i18n, 1,
        [this](auto op) {
            if (op && *op == 1) {
                RunExport();
            }
        });
}

void Menu::RunCreate(const std::string& nickname) {
    App::Push<ProgressBox>(0, "Create user"_i18n, nickname, [nickname](auto pbox) -> Result {
        pbox->NewTransfer("Creating user"_i18n);
        AccountUid uid{};
        R_TRY(account_user::Create(nickname, uid));
        R_SUCCEED();
    }, [this](Result rc) {
        if (R_FAILED(rc)) {
            App::Push<OptionBox>("Could not create the user."_i18n, "OK"_i18n);
            return;
        }
        Refresh();
    }, 1, PRIO_PREEMPTIVE, 1024 * 64, false);
}

void Menu::RunRename(const std::string& nickname) {
    if (m_items.empty()) {
        return;
    }
    const auto uid = m_items[m_index].uid;
    App::Push<ProgressBox>(0, "Rename"_i18n, nickname, [uid, nickname](auto pbox) -> Result {
        pbox->NewTransfer("Renaming user"_i18n);
        R_TRY(account_user::Rename(uid, nickname));
        R_SUCCEED();
    }, [this](Result rc) {
        if (R_FAILED(rc)) {
            App::Push<OptionBox>("Could not rename the user."_i18n, "OK"_i18n);
            return;
        }
        Refresh();
    }, 1, PRIO_PREEMPTIVE, 1024 * 64, false);
}

void Menu::RunSetAvatar(std::vector<u8> jpeg) {
    if (m_items.empty()) {
        return;
    }
    const auto uid = m_items[m_index].uid;
    App::Push<ProgressBox>(0, "Change avatar"_i18n, "Change avatar"_i18n, [uid, jpeg = std::move(jpeg)](auto pbox) -> Result {
        pbox->NewTransfer("Writing avatar"_i18n);
        R_TRY(account_user::SetImageJpeg(uid, jpeg));
        R_SUCCEED();
    }, [this](Result rc) {
        if (R_FAILED(rc)) {
            App::Push<OptionBox>("Could not change the avatar."_i18n, "OK"_i18n);
            return;
        }
        Refresh();
    }, 1, PRIO_PREEMPTIVE, 1024 * 64, false);
}

void Menu::RunNandBackup() {
    auto report = std::make_shared<nand_transfer::Report>();
    App::Push<ProgressBox>(0, "Backup profiles & play hours"_i18n, "Backup profiles & play hours"_i18n,
        [report](auto pbox) -> Result {
            pbox->NewTransfer("Decrypting system saves"_i18n);
            R_TRY(nand_transfer::Export(pbox, *report));
            R_SUCCEED();
        }, [this, report](Result rc) {
            if (R_FAILED(rc) || report->dir.empty()) {
                App::Push<OptionBox>(
                    "Could not dump system saves. Close games and other homebrew, then try again."_i18n,
                    "OK"_i18n);
                return;
            }
            std::string msg = "Wrote decrypted saves to "_i18n + report->dir + ". ";
            if (!report->save_00F0) {
                msg += "Play hours (00F0) could not be opened. Close games and retry. "_i18n;
            }
            if (!report->save_0010) {
                msg += "Account save (0010) could not be opened. "_i18n;
            }
            msg += "Copy this folder to the destination SD, then Restore profiles & play hours there. Reboot this console."_i18n;
            App::Push<OptionBox>(
                msg,
                "Later"_i18n, "Reboot"_i18n, 1,
                [](auto op) {
                    if (op && *op == 1) {
                        utils::requestForcedReboot();
                    }
                });
            Refresh();
        }, 1, PRIO_PREEMPTIVE, 1024 * 256, false);
}

void Menu::RunNandRestore(const std::string& dir) {
    if (!nand_transfer::IsPack(dir)) {
        App::Push<OptionBox>(
            "That folder is not a profiles & play hours pack (needs 80000000000000F0 or 8000000000000010)."_i18n,
            "OK"_i18n);
        return;
    }
    auto report = std::make_shared<nand_transfer::Report>();
    App::Push<ProgressBox>(0, "Restore profiles & play hours"_i18n, "Restore profiles & play hours"_i18n,
        [dir, report](auto pbox) -> Result {
            pbox->NewTransfer("Writing system saves"_i18n);
            R_TRY(nand_transfer::Import(pbox, dir, *report));
            R_SUCCEED();
        }, [this, report](Result rc) {
            if (R_FAILED(rc)) {
                const auto msg = (rc == Result_FsInvalidType)
                    ? "That folder is not a profiles & play hours pack (needs 80000000000000F0 or 8000000000000010)."_i18n
                    : "Could not write system saves. Close games and other homebrew, then try again. If play hours stay locked, TegraExplorer restore.te in the pack is the fallback."_i18n;
                App::Push<OptionBox>(msg, "OK"_i18n);
                return;
            }
            std::string msg = "Wrote pack into this NAND. Reboot required. "_i18n;
            if (!report->save_00F0) {
                msg += "Play hours (00F0) could not be opened. Close games and retry, or use restore.te. "_i18n;
            }
            if (!report->save_0010) {
                msg += "Account save (0010) could not be opened. "_i18n;
            }
            App::Push<OptionBox>(
                msg,
                "Later"_i18n, "Reboot"_i18n, 1,
                [](auto op) {
                    if (op && *op == 1) {
                        utils::requestForcedReboot();
                    }
                });
            Refresh();
        }, 1, PRIO_PREEMPTIVE, 1024 * 256, false);
}

void Menu::RunBackup() {
    const auto uids = SelectedUids();
    if (uids.empty()) {
        return;
    }
    auto dirs = std::make_shared<std::vector<std::string>>();
    App::Push<ProgressBox>(0, "Backup user"_i18n, "Backup user"_i18n, [uids, dirs](auto pbox) -> Result {
        pbox->NewTransfer("Writing user pack"_i18n);
        R_TRY(account_user::ExportUserPacks(uids, *dirs));
        R_SUCCEED();
    }, [this, dirs](Result rc) {
        if (R_FAILED(rc) || dirs->empty()) {
            App::Push<OptionBox>("Could not write the user pack."_i18n, "OK"_i18n);
            return;
        }
        App::Push<OptionBox>("Exported to "_i18n + dirs->front(), "OK"_i18n);
        Refresh();
    }, 1, PRIO_PREEMPTIVE, 1024 * 128, false);
}

void Menu::RunRestore(const std::string& dir) {
    if (m_items.size() >= ACC_USER_LIST_SIZE) {
        App::Push<OptionBox>("The console already has 8 user profiles."_i18n, "OK"_i18n);
        return;
    }
    App::Push<ProgressBox>(0, "Restore user pack"_i18n, "Restore user pack"_i18n, [dir](auto pbox) -> Result {
        pbox->NewTransfer("Creating user from pack"_i18n);
        AccountUid uid{};
        R_TRY(account_user::ImportUserPack(dir, uid));
        R_SUCCEED();
    }, [this](Result rc) {
        if (R_FAILED(rc)) {
            const auto msg = (rc == Result_FsInvalidType)
                ? "That folder is not a user pack (needs profile.json or avatar.jpg)."_i18n
                : "Could not restore the user pack."_i18n;
            App::Push<OptionBox>(msg, "OK"_i18n);
            return;
        }
        App::Push<OptionBox>(
            "User restored. Reboot if the Nintendo link does not show yet."_i18n,
            "Later"_i18n, "Reboot"_i18n, 1,
            [](auto op) {
                if (op && *op == 1) {
                    utils::requestForcedReboot();
                }
            });
        Refresh();
    }, 1, PRIO_PREEMPTIVE, 1024 * 128, false);
}

void Menu::RunDelete(bool backup_account, std::vector<save::Entry> save_backup) {
    const auto uids = SelectedUids();
    if (uids.empty()) {
        return;
    }
    auto all_saves = CollectSaves(uids);
    auto helper = std::make_shared<save::Menu>(MenuFlag_None);
    App::Push<ProgressBox>(0, "Delete user"_i18n, "Delete user"_i18n,
        [uids, backup_account, save_backup = std::move(save_backup), all_saves = std::move(all_saves), helper](auto pbox) mutable -> Result {
            if (backup_account) {
                pbox->NewTransfer("Backing up account"_i18n);
                std::vector<std::string> dirs;
                R_TRY(account_user::ExportUserPacks(uids, dirs));
            }
            if (!save_backup.empty()) {
                pbox->NewTransfer("Backing up saves"_i18n);
                R_TRY(helper->BackupSavesOn(pbox, save_backup));
            }
            pbox->NewTransfer("Deleting users"_i18n);
            for (const auto& uid : uids) {
                account_link::UnlinkUsers({uid});
                R_TRY(account_user::Delete(uid));
            }
            if (!all_saves.empty()) {
                pbox->NewTransfer("Deleting saves"_i18n);
                R_TRY(helper->DeleteSavesOn(pbox, all_saves));
            }
            R_SUCCEED();
        }, [this](Result rc) {
            if (R_FAILED(rc)) {
                App::Push<OptionBox>("Could not delete the user."_i18n, "OK"_i18n);
                Refresh();
                return;
            }
            App::Push<OptionBox>("User deleted."_i18n, "OK"_i18n);
            Refresh();
        }, 1, PRIO_PREEMPTIVE, 1024 * 128, false);
}

void Menu::RunUnlink(bool all) {
    const auto uids = SelectedUids(all);
    if (uids.empty()) {
        return;
    }
    App::Push<ProgressBox>(0, "Unlink Nintendo Account"_i18n, "Unlink Nintendo Account"_i18n, [uids](auto pbox) -> Result {
        pbox->NewTransfer("Updating account save"_i18n);
        R_TRY(account_link::UnlinkUsers(uids));
        R_SUCCEED();
    }, [this](Result rc) {
        if (R_FAILED(rc)) {
            App::Push<OptionBox>(
                "Could not update the account save. Close other homebrew and try again."_i18n,
                "OK"_i18n);
            return;
        }
        App::Push<OptionBox>(
            "Account data updated. Reboot for the change to apply."_i18n,
            "Later"_i18n, "Reboot"_i18n, 1,
            [](auto op) {
                if (op && *op == 1) {
                    utils::requestForcedReboot();
                }
            });
        Refresh();
    }, 1, PRIO_PREEMPTIVE, 1024 * 128, false);
}

void Menu::RunOffline(bool all) {
    const auto uids = SelectedUids(all);
    if (uids.empty()) {
        return;
    }
    App::Push<ProgressBox>(0, "Offline stub (Linkalho)"_i18n, "Offline stub (Linkalho)"_i18n, [uids](auto pbox) -> Result {
        pbox->NewTransfer("Writing account save"_i18n);
        R_TRY(account_link::LinkUsers(uids));
        R_SUCCEED();
    }, [this](Result rc) {
        if (R_FAILED(rc)) {
            App::Push<OptionBox>(
                "Could not update the account save. Close other homebrew and try again."_i18n,
                "OK"_i18n);
            return;
        }
        App::Push<OptionBox>(
            "Offline stub written. Games that only check for a linked account may work, but Horizon can keep retrying Nintendo. Reboot required."_i18n,
            "Later"_i18n, "Reboot"_i18n, 1,
            [](auto op) {
                if (op && *op == 1) {
                    utils::requestForcedReboot();
                }
            });
        Refresh();
    }, 1, PRIO_PREEMPTIVE, 1024 * 128, false);
}

void Menu::RunImport(bool all, const std::string& dump_dir) {
    const auto uids = SelectedUids(all);
    if (uids.empty()) {
        return;
    }
    auto had_tokens = std::make_shared<bool>(false);
    App::Push<ProgressBox>(0, "Import official link"_i18n, "Import official link"_i18n,
        [uids, dump_dir, had_tokens](auto pbox) -> Result {
            pbox->NewTransfer("Importing account save"_i18n);
            R_TRY(account_link::ImportOfficialLink(uids, dump_dir, *had_tokens));
            R_SUCCEED();
        }, [this, had_tokens](Result rc) {
            if (R_FAILED(rc)) {
                const auto msg = (rc == Result_FsInvalidType)
                    ? "No baas/nas dump in that folder. Dump system save 8000000000000010 (JKSV or TegraExplorer) from a console that was linked officially."_i18n
                    : "Could not update the account save. Close other homebrew and try again."_i18n;
                App::Push<OptionBox>(msg, "OK"_i18n);
                return;
            }
            const auto msg = *had_tokens
                ? "Official Nintendo tokens imported. Reboot for the change to apply. Keep emuNAND off Nintendo servers (dns.mitm / prodinfo blank)."_i18n
                : "baas/nas imported, but this dump has no id.token / refresh.token. Horizon can still retry Nintendo servers. Prefer a dump from an officially linked sysNAND."_i18n;
            App::Push<OptionBox>(
                msg,
                "Later"_i18n, "Reboot"_i18n, 1,
                [](auto op) {
                    if (op && *op == 1) {
                        utils::requestForcedReboot();
                    }
                });
            Refresh();
        }, 1, PRIO_PREEMPTIVE, 1024 * 128, false);
}

void Menu::RunExport() {
    auto out_dir = std::make_shared<std::string>();
    App::Push<ProgressBox>(0, "Export account save"_i18n, "Export account save"_i18n,
        [out_dir](auto pbox) -> Result {
            pbox->NewTransfer("Exporting account save"_i18n);
            R_TRY(account_link::ExportAccountSave(*out_dir));
            R_SUCCEED();
        }, [this, out_dir](Result rc) {
            if (R_FAILED(rc)) {
                const auto msg = (rc == Result_FsEmpty)
                    ? "Nothing to export. This NAND has no baas/nas account data."_i18n
                    : "Could not export the account save. Close other homebrew and try again."_i18n;
                App::Push<OptionBox>(msg, "OK"_i18n);
                return;
            }
            App::Push<OptionBox>("Exported to "_i18n + *out_dir, "OK"_i18n);
            Refresh();
        }, 1, PRIO_PREEMPTIVE, 1024 * 128, false);
}

} // namespace sphaira::ui::menu::users
