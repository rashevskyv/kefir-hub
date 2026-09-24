#include "ui/menus/users/users_manage_internal.hpp"

#include "app.hpp"
#include "i18n.hpp"
#include "image.hpp"
#include "ui/nvg_util.hpp"

#include <algorithm>

namespace sphaira::ui::menu::users {

ManageBackupsMenu::ManageBackupsMenu(Callback on_restore)
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

ManageBackupsMenu::~ManageBackupsMenu() {
    FreeImages();
}

void ManageBackupsMenu::FreeImages() {
    auto* vg = App::GetVg();
    for (auto& e : m_entries) {
        if (e.image > 0 && vg) {
            nvgDeleteImage(vg, e.image);
            e.image = 0;
        }
    }
}

void ManageBackupsMenu::Refresh() {
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

void ManageBackupsMenu::UpdateSubHeading() {
    if (m_entries.empty()) {
        SetSubHeading("0");
    } else if (m_selected_count > 0) {
        SetSubHeading(std::to_string(m_selected_count) + " / " + std::to_string(m_entries.size()));
    } else {
        SetSubHeading(std::to_string(m_entries.size()));
    }
}

void ManageBackupsMenu::ToggleCurrentSelection(bool advance) {
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

void ManageBackupsMenu::SelectAll() {
    for (auto& e : m_entries) {
        e.selected = true;
    }
    m_selected_count = static_cast<s64>(m_entries.size());
    UpdateSubHeading();
}

void ManageBackupsMenu::InvertSelection() {
    m_selected_count = 0;
    for (auto& e : m_entries) {
        e.selected ^= 1;
        if (e.selected) {
            m_selected_count++;
        }
    }
    UpdateSubHeading();
}

void ManageBackupsMenu::ClearSelection() {
    for (auto& e : m_entries) {
        e.selected = false;
    }
    m_selected_count = 0;
    UpdateSubHeading();
}

void ManageBackupsMenu::Update(Controller* controller, TouchInfo* touch) {
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

auto ManageBackupsMenu::TryLoadAvatar(Entry& e) -> bool {
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

void ManageBackupsMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);
    if (m_entries.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 22.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "%s", "No Backup user packs found"_i18n.c_str());
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
        detail_str += e.pack.has_playtime ? " - play hours"_i18n : " - no play hours"_i18n;
        gfx::drawTextArgs(vg, text_x, v.y + v.h / 2.f + 13.f, 15.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "%s", detail_str.c_str());
        nvgRestore(vg);
    });
}

void ManageBackupsMenu::OnFocusGained() {
    MenuBase::OnFocusGained();
    Refresh();
}

void OpenUserLibrary(std::function<void(std::vector<account_user::Pack>)> on_restore) {
    App::Push<ManageBackupsMenu>(std::move(on_restore));
}

} // namespace sphaira::ui::menu::users
