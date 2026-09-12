#include "ui/menus/users/users_restore_library.hpp"

#include "account/account_user.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "image.hpp"
#include "ui/list.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sphaira::ui::menu::users {
namespace {

struct RestoreBackupMenu final : MenuBase {
    using Callback = std::function<void(std::optional<std::vector<account_user::Pack>>)>;

    struct Entry {
        account_user::Pack pack;
        int image{};
        bool selected{};
        bool avatar_tried{};
    };

    RestoreBackupMenu(std::vector<account_user::Pack> packs, Callback cb, bool allow_delete = true)
        : MenuBase{"Restore Backup"_i18n, MenuFlag_None}
        , m_cb{std::move(cb)}
    {
        for (auto& p : packs) {
            Entry e;
            e.pack = std::move(p);
            m_entries.push_back(std::move(e));
        }
        this->SetActions(
            std::make_pair(Button::A, Action{"Restore"_i18n, [this](){
                std::vector<account_user::Pack> picked;
                for (const auto& e : m_entries) {
                    if (e.selected) {
                        picked.push_back(e.pack);
                    }
                }
                if (picked.empty() && !m_entries.empty()) {
                    picked.push_back(m_entries[m_index].pack);
                }
                auto cb = m_cb;
                SetPop();
                if (cb) {
                    cb(std::move(picked));
                }
            }}),
            std::make_pair(Button::B, Action{"Cancel"_i18n, [this](){
                auto cb = m_cb;
                SetPop();
                if (cb) {
                    cb(std::nullopt);
                }
            }}),
            std::make_pair(Button::X, Action{"Select"_i18n, [this](){
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
            }}),
            std::make_pair(Button::Y, Action{"Invert"_i18n, [this](){
                m_selected_count = 0;
                for (auto& e : m_entries) {
                    e.selected ^= 1;
                    if (e.selected) {
                        m_selected_count++;
                    }
                }
                UpdateSubHeading();
            }}),
            std::make_pair(Button::SELECT, Action{"Delete"_i18n, [this](){
                ConfirmDeletePacks();
            }})
        );
        if (!allow_delete) {
            this->RemoveAction(Button::SELECT);
            SetTitleSubHeading("X marks backups to restore. A restores the selected profiles."_i18n, true);
        } else {
            SetTitleSubHeading("X marks backups. Minus deletes. A restores."_i18n, true);
        }
        m_list = std::make_unique<List>(1, 8, Vec4{75.f, 110.f, 1145.f, 560.f}, Vec4{75.f, 110.f, 1130.f, 80.f});
        UpdateSubHeading();
    }

    ~RestoreBackupMenu() {
        auto* vg = App::GetVg();
        for (auto& e : m_entries) {
            if (e.image > 0 && vg) {
                nvgDeleteImage(vg, e.image);
                e.image = 0;
            }
        }
    }

    auto GetShortTitle() const -> const char* override { return "Restore"; }

    void UpdateSubHeading() {
        if (m_entries.empty()) {
            SetSubHeading("0");
        } else if (m_selected_count > 0) {
            SetSubHeading(std::to_string(m_selected_count) + " / " + std::to_string(m_entries.size()));
        } else {
            SetSubHeading(std::to_string(m_entries.size()));
        }
    }

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
        m_list->Draw(vg, theme, m_entries.size(), m_index, [this, &loaded](auto* vg, auto* theme, Vec4 v, auto i) {
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

private:
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
    Callback m_cb;
    s64 m_index{};
    s64 m_selected_count{};
    std::unique_ptr<List> m_list;
};

} // namespace

void OpenRestoreLibrary(
    std::vector<account_user::Pack> packs,
    std::function<void(std::optional<std::vector<account_user::Pack>>)> cb,
    bool allow_delete)
{
    App::Push<RestoreBackupMenu>(std::move(packs), std::move(cb), allow_delete);
}

} // namespace sphaira::ui::menu::users
