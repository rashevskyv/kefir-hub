#include "ui/menus/users/users_nand_library.hpp"

#include "account/nand_transfer.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "image.hpp"
#include "ui/list.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sphaira::ui::menu::users {
namespace {

struct NandPackDetailMenu final : MenuBase {
    using RestoreCb = std::function<void(const std::string& dir, bool restore_play_hours)>;

    struct Entry {
        nand_transfer::PackUser user;
        int image{};
        bool avatar_tried{};
    };

    NandPackDetailMenu(nand_transfer::PackInfo pack, RestoreCb on_restore, std::function<void()> on_deleted)
        : MenuBase{"Profiles & play hours pack"_i18n, MenuFlag_None}
        , m_pack{std::move(pack)}
        , m_on_restore{std::move(on_restore)}
        , m_on_deleted{std::move(on_deleted)}
    {
        for (auto& u : nand_transfer::ListPackUsers(m_pack.dir)) {
            Entry e;
            e.user = std::move(u);
            m_entries.push_back(std::move(e));
        }

        this->SetActions(
            std::make_pair(Button::A, Action{"Actions"_i18n, [this](){ PromptAction(); }}),
            std::make_pair(Button::B, Action{"Back"_i18n, [this](){ SetPop(); }}),
            std::make_pair(Button::SELECT, Action{"Delete"_i18n, [this](){ ConfirmDelete(); }})
        );

        SetTitleSubHeading(
            "This pack restores all profiles together. For one user use Restore Backup."_i18n, true);
        m_list = std::make_unique<List>(1, 8, Vec4{75.f, 110.f, 1145.f, 560.f}, Vec4{75.f, 110.f, 1130.f, 80.f});
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
        if (e.user.avatar_path.empty()) {
            return false;
        }
        std::vector<u8> jpeg;
        fs::FsNativeSd sd;
        if (R_FAILED(sd.read_entire_file(e.user.avatar_path.c_str(), jpeg)) || jpeg.empty()) {
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

            const float icon_size = 46.f;
            const float icon_x = v.x + 20.f;
            const float icon_y = v.y + (v.h - icon_size) / 2.f;
            gfx::drawImage(vg, Vec4{icon_x, icon_y, icon_size, icon_size},
                e.image > 0 ? e.image : App::GetDefaultImage(), 4);

            const float text_x = icon_x + icon_size + 14.f;
            gfx::drawTextArgs(vg, text_x, v.y + v.h / 2.f - 11.f, 20.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT),
                "%s", e.user.nickname.c_str());
            gfx::drawTextArgs(vg, text_x, v.y + v.h / 2.f + 13.f, 15.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                theme->GetColour(ThemeEntryID_TEXT_INFO),
                "%s", e.user.uid.c_str());
        });
    }

private:
    void PromptAction() {
        PopupList::Items items;
        items.emplace_back("Restore"_i18n);
        items.emplace_back("Delete pack"_i18n);
        items.emplace_back("Back"_i18n);
        App::Push<PopupList>(m_pack.name, items, [this](auto op_index) {
            if (!op_index) {
                return;
            }
            switch (*op_index) {
                case 0: ConfirmRestore(); break;
                case 1: ConfirmDelete(); break;
                case 2: SetPop(); break;
                default: break;
            }
        });
    }

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

    void ConfirmDelete() {
        App::Push<OptionBox>(
            "Delete this profiles & play hours pack from the SD card?"_i18n,
            "Cancel"_i18n, "Delete"_i18n, 1,
            [this](auto op) {
                if (!op || *op != 1) {
                    return;
                }
                App::Push<ProgressBox>(0, "Delete pack"_i18n, m_pack.name,
                    [dir = m_pack.dir](auto pbox) -> Result {
                        pbox->NewTransfer("Deleting"_i18n);
                        fs::FsNativeSd sd;
                        R_TRY(sd.DeleteDirectoryRecursively(dir.c_str()));
                        R_SUCCEED();
                    }, [this](Result rc) {
                        if (R_FAILED(rc)) {
                            App::Push<OptionBox>("Could not delete the pack."_i18n, "OK"_i18n);
                            return;
                        }
                        auto cb = m_on_deleted;
                        SetPop();
                        if (cb) {
                            cb();
                        }
                    }, 2);
            });
    }

    nand_transfer::PackInfo m_pack;
    RestoreCb m_on_restore;
    std::function<void()> m_on_deleted;
    std::vector<Entry> m_entries;
    s64 m_index{};
    std::unique_ptr<List> m_list;
};

struct NandPackLibraryMenu final : MenuBase {
    using RestoreCb = std::function<void(const std::string& dir, bool restore_play_hours)>;

    struct Entry {
        nand_transfer::PackInfo pack;
    };

    NandPackLibraryMenu(RestoreCb on_restore)
        : MenuBase{"Backup profiles & play hours"_i18n, MenuFlag_None}
        , m_on_restore{std::move(on_restore)}
    {
        this->SetActions(
            std::make_pair(Button::A, Action{"Open"_i18n, [this](){ OpenCurrent(); }}),
            std::make_pair(Button::B, Action{"Back"_i18n, [this](){ SetPop(); }}),
            std::make_pair(Button::SELECT, Action{"Delete"_i18n, [this](){ ConfirmDelete(); }})
        );
        SetTitleSubHeading("A opens pack details. Minus deletes the pack folder."_i18n, true);
        m_list = std::make_unique<List>(1, 8, Vec4{75.f, 110.f, 1145.f, 560.f}, Vec4{75.f, 110.f, 1130.f, 80.f});
        Refresh();
    }

    auto GetShortTitle() const -> const char* override { return "Packs"; }

    void Refresh() {
        m_entries.clear();
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
        SetSubHeading(std::to_string(m_entries.size()));
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
            const auto selected = m_index == i;
            if (selected) {
                gfx::drawRectOutline(vg, theme, 4.f, v, 5.f);
            } else {
                DrawElement(v, ThemeEntryID_GRID);
            }

            const float text_x = v.x + 20.f;
            gfx::drawTextArgs(vg, text_x, v.y + v.h / 2.f - 11.f, 20.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT),
                "%s", e.pack.name.c_str());

            std::string detail = std::to_string(e.pack.accounts) + " " + "accounts"_i18n;
            detail += e.pack.save_00F0 ? " - play hours"_i18n : " - no play hours"_i18n;
            if (e.pack.save_0010) {
                detail += " - profiles"_i18n;
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
            m_on_restore,
            [this]() { Refresh(); });
    }

    void ConfirmDelete() {
        if (m_entries.empty()) {
            return;
        }
        const auto dir = m_entries[m_index].pack.dir;
        const auto name = m_entries[m_index].pack.name;
        App::Push<OptionBox>(
            "Delete this profiles & play hours pack from the SD card?"_i18n,
            "Cancel"_i18n, "Delete"_i18n, 1,
            [this, dir, name](auto op) {
                if (!op || *op != 1) {
                    return;
                }
                App::Push<ProgressBox>(0, "Delete pack"_i18n, name,
                    [dir](auto pbox) -> Result {
                        pbox->NewTransfer("Deleting"_i18n);
                        fs::FsNativeSd sd;
                        R_TRY(sd.DeleteDirectoryRecursively(dir.c_str()));
                        R_SUCCEED();
                    }, [this](Result rc) {
                        if (R_FAILED(rc)) {
                            App::Push<OptionBox>("Could not delete the pack."_i18n, "OK"_i18n);
                            return;
                        }
                        Refresh();
                    }, 2);
            });
    }

    std::vector<Entry> m_entries;
    RestoreCb m_on_restore;
    s64 m_index{};
    std::unique_ptr<List> m_list;
};

} // namespace

void OpenNandPackLibrary(std::function<void(const std::string& dir, bool restore_play_hours)> on_restore) {
    App::Push<NandPackLibraryMenu>(std::move(on_restore));
}

} // namespace sphaira::ui::menu::users
