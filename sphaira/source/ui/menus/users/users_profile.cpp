#include "ui/menus/users_menu.hpp"
#include "ui/menus/users/users_internal.hpp"

#include "account/account_link.hpp"
#include "account/account_user.hpp"
#include "app.hpp"
#include "app_paths.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "image.hpp"
#include "path_util.hpp"
#include "swkbd.hpp"
#include "ui/hold_confirm_box.hpp"
#include "ui/image_crop.hpp"
#include "ui/list.hpp"
#include "ui/menus/file_picker.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/menus/save_menu.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/steamgriddb_icon.hpp"
#include "utils/utils.hpp"

#include <switch/applets/psel.h>

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::ui::menu::users {
namespace {

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
        SetTitleSubHeading("X marks games to back up. A backs up the marked saves."_i18n, true);
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

struct AvatarPickMenu final : MenuBase {
    using Callback = std::function<void(std::vector<u8>)>;
    enum class Kind { Existing, FromSd, Sgdb };

    struct Tile {
        Kind kind{Kind::Existing};
        std::vector<u8> jpeg;
        int image{};
        std::string label;
    };

    explicit AvatarPickMenu(Callback cb)
        : MenuBase{"Choose an avatar"_i18n, MenuFlag_None}
        , m_cb{std::move(cb)}
    {
        LoadTiles();
        this->SetActions(
            std::make_pair(Button::A, Action{"Select"_i18n, [this](){ Activate(); }}),
            std::make_pair(Button::B, Action{"Cancel"_i18n, [this](){ SetPop(); }})
        );
        const Vec4 list_pos{40.f, 110.f, 1200.f, 530.f};
        const Vec4 item_pos{70.f, 120.f, 160.f, 160.f};
        m_list = std::make_unique<List>(6, 12, list_pos, item_pos, Vec2{14.f, 14.f});
        SetTitleSubHeading("A selects. SteamGridDB asks for a game name, then lets you choose a matching game."_i18n, true);
        SetSubHeading(std::to_string(m_tiles.size()));
    }

    ~AvatarPickMenu() {
        auto* vg = App::GetVg();
        for (auto& t : m_tiles) {
            if (t.image > 0 && vg) {
                nvgDeleteImage(vg, t.image);
                t.image = 0;
            }
        }
    }

    auto GetShortTitle() const -> const char* override { return "Avatar"; }

    void Update(Controller* controller, TouchInfo* touch) override {
        MenuBase::Update(controller, touch);
        if (m_tiles.empty()) {
            return;
        }
        m_list->OnUpdate(controller, touch, m_index, m_tiles.size(), [this](bool touched, auto i) {
            if (touched && m_index == i) {
                FireAction(Button::A);
            } else {
                App::PlaySoundEffect(SoundEffect_Focus);
                m_index = i;
            }
        }, this);
    }

    void Draw(NVGcontext* vg, Theme* theme) override {
        MenuBase::Draw(vg, theme);
        m_list->Draw(vg, theme, m_tiles.size(), [this](auto* vg, auto* theme, Vec4 v, auto i) {
            const auto& t = m_tiles[i];
            const auto selected = m_index == i;
            if (selected) {
                gfx::drawRectOutline(vg, theme, 4.f, v);
            } else {
                DrawElement(v, ThemeEntryID_GRID);
            }
            if (t.kind == Kind::FromSd) {
                const Vec4 icon_rect{v.x + 16.f, v.y + 12.f, v.w - 32.f, v.h - 48.f};
                DrawElementContain(icon_rect, ThemeEntryID_ICON_FILE);
                gfx::drawTextArgs(vg, v.x + v.w / 2.f, v.y + v.h - 18.f, 14.f,
                    NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
                    theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT),
                    "%s", "From SD"_i18n.c_str());
            } else if (t.kind == Kind::Sgdb) {
                gfx::drawTextArgs(vg, v.x + v.w / 2.f, v.y + v.h / 2.f, 18.f,
                    NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
                    theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT),
                    "SteamGridDB");
            } else {
                const Vec4 inner{v.x + 8.f, v.y + 8.f, v.w - 16.f, v.h - 16.f};
                gfx::drawImage(vg, inner, t.image > 0 ? t.image : App::GetDefaultImage(), 5.f);
            }
        });
    }

private:
    void AddDecodedTile(std::span<const u8> raw) {
        if (raw.empty()) {
            return;
        }
        auto img = ImageLoadFromMemory(raw, ImageFlag_JPEG);
        if (img.data.empty()) {
            img = ImageLoadFromMemory(raw);
        }
        if (img.data.empty() || img.w <= 0 || img.h <= 0) {
            return;
        }
        Tile t;
        t.jpeg = ImageNormalizeAvatar(raw);
        if (t.jpeg.empty()) {
            t.jpeg = ImageNormalizeIcon(raw);
        }
        if (t.jpeg.empty()) {
            t.jpeg.assign(raw.begin(), raw.end());
        }
        t.image = nvgCreateImageRGBA(App::GetVg(), img.w, img.h, 0, img.data.data());
        m_tiles.push_back(std::move(t));
    }

    void LoadTiles() {
        for (const auto& base : App::GetAccountList()) {
            std::vector<u8> jpeg;
            if (R_SUCCEEDED(account_user::LoadImageJpeg(base.uid, jpeg))) {
                AddDecodedTile(jpeg);
            }
        }
        fs::FsNativeSd sd;
        fs::Dir dir;
        const auto extra = paths::DATA_ROOT + "/avatars";
        if (R_SUCCEEDED(sd.OpenDirectory(extra.c_str(), FsDirOpenMode_ReadFiles, &dir))) {
            std::vector<FsDirectoryEntry> ents;
            dir.ReadAll(ents);
            for (const auto& e : ents) {
                if (e.type != FsDirEntryType_File) {
                    continue;
                }
                const auto ext = path::Extension(e.name);
                static constexpr std::string_view kImg[] = {"jpg", "jpeg", "png", "bmp"};
                if (!path::IsAnyOfIC(ext, kImg)) {
                    continue;
                }
                std::vector<u8> raw;
                if (R_SUCCEEDED(sd.read_entire_file((extra + "/" + e.name).c_str(), raw))) {
                    AddDecodedTile(raw);
                }
            }
        }
        m_tiles.push_back(Tile{Kind::FromSd, {}, 0, "From SD"});
        m_tiles.push_back(Tile{Kind::Sgdb, {}, 0, "SteamGridDB"});
    }

    void Activate() {
        if (m_index < 0 || static_cast<size_t>(m_index) >= m_tiles.size()) {
            return;
        }
        auto& t = m_tiles[m_index];
        if (t.kind == Kind::FromSd) {
            OpenSd();
            return;
        }
        if (t.kind == Kind::Sgdb) {
            std::string query;
            if (R_FAILED(swkbd::ShowText(query, "Game or icon name"_i18n.c_str(), nullptr, 1, 64)) || query.empty()) {
                return;
            }
            steamgriddb::ShowIconPicker(query, [this](std::vector<u8> icon) {
                if (icon.empty()) {
                    return;
                }
                auto jpeg = ImageNormalizeIcon(icon);
                if (jpeg.empty()) {
                    jpeg = std::move(icon);
                }
                Finish(std::move(jpeg));
            });
            return;
        }
        Finish(t.jpeg);
    }

    void OpenSd() {
        App::Push<filepicker::Menu>(
            filepicker::Callback{[this](const fs::FsPath& path) -> bool {
                const auto ext = path::Extension(path);
                const auto flags = path::EqualsIC(ext, "jpg") || path::EqualsIC(ext, "jpeg")
                    ? ImageFlag_JPEG : ImageFlag_None;
                auto raw = ImageLoadFromFile(path, flags);
                if (raw.data.empty()) {
                    raw = ImageLoadFromFile(path);
                }
                if (raw.data.empty() || raw.w <= 0 || raw.h <= 0) {
                    App::Push<OptionBox>("Could not read that image."_i18n, "OK"_i18n);
                    return false;
                }
                auto source = path.toString();
                if (const auto slash = source.find_last_of("/\\"); slash != std::string::npos) {
                    source.erase(0, slash + 1);
                }
                image_crop::Show(std::move(raw), std::move(source),
                    [this](std::vector<u8> jpeg, std::string) {
                        if (!jpeg.empty()) {
                            Finish(std::move(jpeg));
                        }
                    });
                return true;
            }},
            std::vector<std::string>{"jpg", "jpeg", "png", "bmp"});
    }

    void Finish(std::vector<u8> jpeg) {
        auto cb = m_cb;
        SetPop();
        if (cb) {
            cb(std::move(jpeg));
        }
    }

    std::vector<Tile> m_tiles;
    Callback m_cb;
    s64 m_index{};
    std::unique_ptr<List> m_list;
};

} // namespace

void Menu::ConfirmCreate() {
    if (m_items.size() >= ACC_USER_LIST_SIZE) {
        App::Push<OptionBox>("The console already has 8 user profiles."_i18n, "OK"_i18n);
        return;
    }
    const auto rc = pselShowUserCreator();
    App::ResetTouchAfterApplet();
    if (R_FAILED(rc)) {
        App::PushErrorBox(rc, "Could not open user creator."_i18n);
        return;
    }
    Refresh();
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
    App::Push(std::make_unique<AvatarPickMenu>([this](std::vector<u8> jpeg) {
        if (!jpeg.empty()) {
            RunSetAvatar(std::move(jpeg));
        }
    }));
}

void Menu::ConfirmBackup() {
    const auto uids = SelectedUids();
    if (uids.empty()) {
        return;
    }
    App::Push<OptionBox>(
        "This backup will reboot the console."_i18n,
        "Cancel"_i18n, "Backup"_i18n, 1,
        [this, uids](auto op) {
            if (!op || *op != 1) {
                return;
            }
            const auto packs = account_user::ListUserPacks();
            std::string existing_when;
            u32 existing_users = 0;
            for (const auto& uid : uids) {
                const auto hex = account_link::UidHex(uid);
                for (const auto& p : packs) {
                    if (!p.uid_hex.empty() && p.uid_hex == hex) {
                        existing_users++;
                        if (existing_when.empty()) {
                            existing_when = !p.created_label.empty() ? p.created_label : p.folder_name;
                        }
                        break;
                    }
                }
            }
            if (existing_users == 0) {
                RunBackup(uids, false);
                return;
            }
            std::string msg;
            if (uids.size() == 1) {
                msg = "A backup of this user already exists"_i18n + " (" + existing_when + "). " +
                    "Overwrite it, or keep both?"_i18n;
            } else {
                msg = "One or more of these users already have a backup. Overwrite, or keep both?"_i18n;
            }
            App::Push<OptionBox>(msg, "Cancel"_i18n, "Overwrite"_i18n, "Keep both"_i18n, 1,
                [this, uids](auto op2) {
                    if (!op2 || *op2 == 0) {
                        return;
                    }
                    RunBackup(uids, *op2 == 1);
                });
        });
}

void Menu::ConfirmDelete() {
    const auto uids = SelectedUids();
    if (uids.empty()) {
        return;
    }

    const auto live = App::GetAccountList();
    bool covers_all = !live.empty();
    for (const auto& base : live) {
        bool found = false;
        for (const auto& uid : uids) {
            if (base.uid.uid[0] == uid.uid[0] && base.uid.uid[1] == uid.uid[1]) {
                found = true;
                break;
            }
        }
        if (!found) {
            covers_all = false;
            break;
        }
    }
    if (covers_all) {
        App::Push<OptionBox>("Cannot delete every user profile. Keep at least one."_i18n, "OK"_i18n);
        return;
    }

    const auto msg = (uids.size() > 1)
        ? "Delete the selected users? This cannot be undone. Their game saves will also be deleted. Hold A to confirm."_i18n
        : "Delete this user? This cannot be undone. Their game saves will also be deleted. Hold A to confirm."_i18n;
    App::Push<HoldConfirmBox>(msg, [this, uids](bool ok) {
        if (!ok) {
            return;
        }

        auto ask_saves_then_delete = [this, uids]() {
            auto saves = CollectSaves(uids);
            if (saves.empty()) {
                RunDelete({});
                return;
            }
            App::Push<OptionBox>(
                "Back up game saves for these users? You can pick which games."_i18n,
                "Skip"_i18n, "Choose saves"_i18n, 1,
                [this, saves](auto op) mutable {
                    if (!op) {
                        return;
                    }
                    if (*op == 0) {
                        RunDelete({});
                        return;
                    }
                    App::Push<SavePickMenu>(std::move(saves), [this](auto picked) {
                        if (!picked) {
                            return;
                        }
                        RunDelete(std::move(*picked));
                    });
                });
        };

        App::Push<OptionBox>(
            "Back up all user profiles on this console now (name, avatar, Nintendo link)?"_i18n,
            "Skip"_i18n, "Backup all accounts"_i18n, 1,
            [this, uids, ask_saves_then_delete](auto op) {
                if (!op) {
                    return;
                }
                if (*op != 1) {
                    ask_saves_then_delete();
                    return;
                }

                std::vector<AccountUid> all_uids;
                for (const auto& base : App::GetAccountList()) {
                    all_uids.push_back(base.uid);
                }
                if (all_uids.empty()) {
                    ask_saves_then_delete();
                    return;
                }

                auto dirs = std::make_shared<std::vector<std::string>>();
                App::Push<ProgressBox>(0, "Backup user"_i18n, "Backup user"_i18n,
                    [all_uids = std::move(all_uids), dirs](auto pbox) -> Result {
                        pbox->NewTransfer("Writing account backup"_i18n);
                        R_TRY(account_user::ExportUserPacks(all_uids, *dirs, false, false));
                        R_SUCCEED();
                    }, [ask_saves_then_delete, dirs](Result rc) {
                        if (R_FAILED(rc) || dirs->empty()) {
                            App::Push<OptionBox>("Could not write the account backup."_i18n, "OK"_i18n);
                            return;
                        }
                        ask_saves_then_delete();
                    }, 1, PRIO_PREEMPTIVE, 1024 * 256, false);
            });
    });
}

void Menu::ConfirmLinkNintendoAccount() {
    std::string gate_reason;
    if (account_link::IsLinkGated(gate_reason)) {
        App::Push<OptionBox>(gate_reason, "OK"_i18n);
        return;
    }

    bool any_needed = false;
    for (const auto& u : m_items) {
        if (u.linked_known && !u.horizon_linked) {
            any_needed = true;
            break;
        }
    }
    if (!any_needed) {
        App::Push<OptionBox>("All user profiles are already linked."_i18n, "OK"_i18n);
        return;
    }

    App::Push<OptionBox>(
        "Link all currently unlinked profiles using built-in Nintendo Account donors? Each unlinked profile receives a different donor. Already linked profiles will not be changed. The console will reboot immediately."_i18n,
        "Cancel"_i18n, "Link and reboot"_i18n, 1,
        [this](auto op) {
            if (op && *op == 1) {
                RunLinkNintendoAccount();
            }
        });
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
        App::Push<OptionBox>(
            "Avatar changed.\n\nThe change will not take effect until the console is rebooted.\n\nReboot now?"_i18n,
            "Later"_i18n, "Reboot"_i18n, 1,
            [](auto op_index) {
                if (op_index && *op_index == 1) {
                    utils::requestForcedReboot();
                }
            });
    }, 1, PRIO_PREEMPTIVE, 1024 * 64, false);
}

void Menu::RunBackup(std::vector<AccountUid> uids, bool overwrite_existing) {
    if (uids.empty()) {
        return;
    }
    auto dirs = std::make_shared<std::vector<std::string>>();
    App::Push<ProgressBox>(0, "Backup user"_i18n, "Backup user"_i18n,
        [uids = std::move(uids), dirs, overwrite_existing](auto pbox) -> Result {
        pbox->NewTransfer("Writing account backup"_i18n);
        R_TRY(account_user::ExportUserPacks(uids, *dirs, overwrite_existing));
        R_SUCCEED();
    }, [dirs](Result rc) {
        const bool terminated = account_link::ConsumeAccountDaemonsTerminated();
        if (R_FAILED(rc) || dirs->empty()) {
            if (terminated) {
                utils::requestForcedReboot();
                return;
            }
            App::Push<OptionBox>("Could not write the account backup."_i18n, "OK"_i18n);
            return;
        }
        utils::requestForcedReboot();
    }, 1, PRIO_PREEMPTIVE, 1024 * 256, false);
}

void Menu::RunDelete(std::vector<save::Entry> save_backup) {
    const auto uids = SelectedUids();
    if (uids.empty()) {
        return;
    }

    const auto live = App::GetAccountList();
    bool covers_all = !live.empty();
    for (const auto& base : live) {
        bool found = false;
        for (const auto& uid : uids) {
            if (base.uid.uid[0] == uid.uid[0] && base.uid.uid[1] == uid.uid[1]) {
                found = true;
                break;
            }
        }
        if (!found) {
            covers_all = false;
            break;
        }
    }
    if (covers_all) {
        App::Push<OptionBox>("Cannot delete every user profile. Keep at least one."_i18n, "OK"_i18n);
        return;
    }

    std::string title = "Delete user"_i18n;
    if (uids.size() == 1) {
        auto nick = LiveNameForUid(uids.front());
        if (!nick.empty()) {
            title = std::move(nick);
        }
    }

    auto all_saves = CollectSaves(uids);
    auto helper = std::make_shared<save::Menu>(MenuFlag_None);
    App::Push<ProgressBox>(0, "Deleting"_i18n, title,
        [uids, save_backup = std::move(save_backup), all_saves = std::move(all_saves), helper](auto pbox) mutable -> Result {
            if (!save_backup.empty()) {
                pbox->NewTransfer("Backing up saves"_i18n);
                R_TRY(helper->BackupSavesOn(pbox, save_backup));
            }
            for (const auto& uid : uids) {
                auto nick = LiveNameForUid(uid);
                if (nick.empty()) {
                    nick = account_link::UidHex(uid);
                }
                pbox->NewTransfer(nick);
                R_TRY(account_user::Delete(uid));
            }
            if (!all_saves.empty()) {
                pbox->NewTransfer("Deleting saves"_i18n);
                R_TRY(helper->DeleteSavesOn(pbox, all_saves));
            }
            R_SUCCEED();
        }, [this](Result rc) {
            if (R_FAILED(rc)) {
                log_write("[USER] RunDelete failed 0x%X\n", rc);
                App::Push<OptionBox>("Could not delete the user."_i18n, "OK"_i18n);
                Refresh();
                return;
            }
            App::Push<OptionBox>("User deleted."_i18n, "OK"_i18n);
            Refresh();
        }, 1, PRIO_PREEMPTIVE, 1024 * 256, false);
}

void Menu::RunLinkNintendoAccount() {
    App::Push<ProgressBox>(0, "Link Nintendo Account"_i18n, "Linking account..."_i18n,
        [](auto pbox) -> Result {
            pbox->NewTransfer("Applying Nintendo Account link"_i18n);
            u32 count = 0;
            R_TRY(account_link::LinkAllFromRomfsDonor(count));
            R_SUCCEED();
        },
        [this](Result rc) {
            if (R_FAILED(rc)) {
                if (account_link::ConsumeAccountDaemonsTerminated()) {
                    utils::requestForcedReboot();
                    return;
                }
                App::Push<OptionBox>("Failed to link Nintendo Account."_i18n, "OK"_i18n);
            } else {
                utils::requestForcedReboot();
            }
        }, 1, PRIO_PREEMPTIVE, 1024 * 128, false);
}

void Menu::ConfirmUnlinkNintendoAccount() {
    std::string gate_reason;
    if (account_link::IsLinkGated(gate_reason)) {
        App::Push<OptionBox>(gate_reason, "OK"_i18n);
        return;
    }

    std::vector<AccountUid> targets;
    if (m_selected_count > 0) {
        for (const auto& u : m_items) {
            if (u.selected && u.linked_known && u.horizon_linked) {
                targets.push_back(u.uid);
            }
        }
    } else {
        for (const auto& u : m_items) {
            if (u.linked_known && u.horizon_linked) {
                targets.push_back(u.uid);
            }
        }
    }

    if (targets.empty()) {
        App::Push<OptionBox>("No linked profiles to unlink."_i18n, "OK"_i18n);
        return;
    }

    const auto msg = (m_selected_count > 0)
        ? "Unlink Nintendo Account from the selected linked profiles? Link data is removed from the system save. The console will reboot immediately."_i18n
        : "Unlink Nintendo Account from all linked profiles? Link data is removed from the system save. The console will reboot immediately."_i18n;

    App::Push<OptionBox>(
        msg,
        "Cancel"_i18n, "Unlink and reboot"_i18n, 1,
        [this, targets = std::move(targets)](auto op) mutable {
            if (op && *op == 1) {
                RunUnlinkNintendoAccount(std::move(targets));
            }
        });
}

void Menu::RunUnlinkNintendoAccount(std::vector<AccountUid> uids) {
    App::Push<ProgressBox>(0, "Unlink Nintendo Account"_i18n, "Unlinking account..."_i18n,
        [uids = std::move(uids)](auto pbox) -> Result {
            pbox->NewTransfer("Removing Nintendo Account link"_i18n);
            u32 count = 0;
            R_TRY(account_link::UnlinkLinkedProfiles(uids, count));
            R_SUCCEED();
        },
        [](Result rc) {
            if (R_FAILED(rc)) {
                App::Push<OptionBox>("Failed to unlink Nintendo Account."_i18n, "OK"_i18n);
            } else {
                utils::requestForcedReboot();
            }
        }, 1, PRIO_PREEMPTIVE, 1024 * 128, false);
}

} // namespace sphaira::ui::menu::users
