#include "ui/menus/users_menu.hpp"

#include "account/account_link.hpp"
#include "account/account_user.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "image.hpp"
#include "ui/nvg_util.hpp"
#include "ui/sidebar.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace sphaira::ui::menu::users {
namespace {

auto StatusColour(Theme* theme, const account_link::User& u) -> NVGcolor {
    if (!u.linked_known) {
        return theme->GetColour(ThemeEntryID_TEXT_INFO);
    }
    switch (u.kind) {
        case account_link::LinkKind::Official:
        case account_link::LinkKind::Offline:
            return nvgRGBA(80, 200, 120, 255);
        case account_link::LinkKind::None:
        default:
            return nvgRGBA(230, 60, 60, 255);
    }
}

void DrawLinkDot(NVGcontext* vg, const Vec4& image_v, const account_link::User& u) {
    NVGcolor fill = nvgRGBA(160, 160, 160, 255);
    if (u.linked_known) {
        if (u.kind == account_link::LinkKind::Official || u.kind == account_link::LinkKind::Offline) {
            fill = nvgRGBA(80, 200, 120, 255);
        } else {
            fill = nvgRGBA(230, 60, 60, 255);
        }
    }
    const float r = std::min(6.f, image_v.w * 0.08f);
    const float cx = image_v.x + r + 5.f;
    const float cy = image_v.y + r + 5.f;
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, r + 1.6f);
    nvgFillColor(vg, nvgRGBA(0, 0, 0, 190));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, r);
    nvgFillColor(vg, fill);
    nvgFill(vg);
}

} // namespace

Menu::Menu() : grid::Menu{"Users"_i18n, MenuFlag_None} {
    if (m_layout.Get() == LayoutType::LayoutType_HbMenu) {
        m_layout.Set(LayoutType::LayoutType_GridDetail);
    }
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
    m_name_scroll.Reset();
    m_status_scroll.Reset();
    m_uid_scroll.Reset();
    grid::Menu::OnLayoutChange(m_list, m_layout.Get());
    if (m_layout.Get() == LayoutType::LayoutType_Grid) {
        const Vec4 content_pos{40, 97, 1200, 539};
        const Vec2 pad{10, 40};
        const Vec4 v{93, 150, 174, 174};
        m_list = std::make_unique<List>(6, 6*2, content_pos, v, pad);
    }
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
    SetTitleSubHeading(item.nickname + "  В·  " + StatusLabel(item), true);
    SetSubHeading(std::to_string(m_index + 1) + " / " + std::to_string(m_items.size()));
}

auto Menu::StatusLabel(const account_link::User& u) const -> std::string {
    if (!u.linked_known) {
        return "Link status unavailable"_i18n;
    }
    switch (u.kind) {
        case account_link::LinkKind::Official:
        case account_link::LinkKind::Offline:
            return "Linked"_i18n;
        case account_link::LinkKind::None:
        default:
            return "Not linked"_i18n;
    }
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
        const auto i = (m_index >= 0 && static_cast<size_t>(m_index) < m_items.size()) ? m_index : 0;
        out.push_back(m_items[i]);
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
        Vec4 image_v;
        if (layout == LayoutType::LayoutType_List) {
            const auto selected = m_index == i;
            if (!selected) {
                DrawElement(v, ThemeEntryID_GRID);
            } else {
                gfx::drawRectOutline(vg, theme, 4.f, v, 5.f);
            }
            if (item.selected) {
                auto tint = theme->GetColour(ThemeEntryID_FOCUS);
                tint.a *= 0.35f;
                gfx::drawRect(vg, v, tint, 5.f);
            }
            const float icon_size = 46.f;
            const float icon_x = v.x + 10.f;
            const float icon_y = v.y + (v.h - icon_size) / 2.f;
            gfx::drawImage(vg, Vec4{icon_x, icon_y, icon_size, icon_size},
                item.image ?: App::GetDefaultImage(), 4);
            image_v = Vec4{icon_x, icon_y, icon_size, icon_size};
            const float text_x = icon_x + icon_size + 14.f;
            float status_w = 0.f;
            if (!status.empty()) {
                float bounds[4]{};
                gfx::textBounds(vg, 0, 0, bounds, status.c_str());
                status_w = bounds[2] - bounds[0] + 20.f;
                gfx::drawText(vg, v.x + v.w - 15.f, v.y + v.h / 2.f, 16.f,
                    StatusColour(theme, item), status.c_str(),
                    NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            }
            nvgSave(vg);
            nvgIntersectScissor(vg, text_x, v.y, v.w - (text_x - v.x) - 15.f - status_w, v.h);
            gfx::drawTextArgs(vg, text_x, v.y + v.h / 2.f - 11.f, 20.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT),
                "%s", item.nickname.c_str());
            gfx::drawTextArgs(vg, text_x, v.y + v.h / 2.f + 13.f, 15.f,
                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                theme->GetColour(ThemeEntryID_TEXT_INFO),
                "%s", item.uid_hex.c_str());
            nvgRestore(vg);
        } else if (layout == LayoutType::LayoutType_Grid) {
            const auto selected = m_index == i;
            if (!selected) {
                DrawElement(v, ThemeEntryID_GRID);
                nvgSave(vg);
                nvgIntersectScissor(vg, v.x + 4.f, v.y - 28.f, v.w - 8.f, 26.f);
                gfx::drawTextArgs(vg, v.x + v.w / 2.f, v.y - 14.f, 15.f,
                    NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
                    theme->GetColour(ThemeEntryID_TEXT),
                    "%s", item.nickname.c_str());
                nvgRestore(vg);
            } else {
                gfx::drawRectOutline(vg, theme, 4.f, v, 5.f);
                gfx::drawAppLable(vg, theme, m_name_scroll, v.x, v.y, v.w, item.nickname.c_str());
            }
            image_v = v;
            gfx::drawImage(vg, image_v, item.image ?: App::GetDefaultImage(), 5);
            DrawLinkDot(vg, image_v, item);
        } else if (layout == LayoutType::LayoutType_GridDetail) {
            const auto selected = m_index == i;
            auto text_id = ThemeEntryID_TEXT;
            if (selected) {
                text_id = ThemeEntryID_TEXT_SELECTED;
                gfx::drawRectOutline(vg, theme, 4.f, v, 5.f);
            } else {
                DrawElement(v, ThemeEntryID_GRID);
            }

            image_v = v;
            image_v.x += 20;
            image_v.y += 20;
            image_v.w = 115;
            image_v.h = 115;

            const auto text_off = 148;
            const auto text_x = v.x + text_off;
            const auto text_clip_w = v.w - 30.f - text_off;
            const float font_size = 18;
            m_name_scroll.Draw(vg, selected, text_x, v.y + 45, text_clip_w, font_size, NVG_ALIGN_LEFT, theme->GetColour(text_id), item.nickname.c_str());
            m_status_scroll.Draw(vg, selected, text_x, v.y + 80, text_clip_w, font_size, NVG_ALIGN_LEFT, StatusColour(theme, item), status.c_str());
            m_uid_scroll.Draw(vg, selected, text_x, v.y + 115, text_clip_w, font_size, NVG_ALIGN_LEFT, theme->GetColour(ThemeEntryID_TEXT_INFO), item.uid_hex.c_str());

            gfx::drawImage(vg, image_v, item.image ?: App::GetDefaultImage(), 5);
        } else {
            image_v = DrawEntry(vg, theme, layout, v, m_index == i, item.image,
                item.nickname.c_str(), status.c_str(), item.uid_hex.c_str(), item.selected);
        }
        DrawSelectionMark(vg, theme, layout, v, image_v, item.selected, m_selected_count > 0);
    });

    if (m_layout.Get() == LayoutType::LayoutType_Grid && m_index >= 0 && m_index < static_cast<s64>(m_items.size())) {
        const auto& cur = m_items[m_index];
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, 580.f, 16.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "ID: %s", cur.uid_hex.c_str());
    }
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
        }, true, "Pick an existing profile avatar, an SD image, or SteamGridDB."_i18n);
        options->Add<SidebarEntryCallback>("Delete user"_i18n, [this](){
            ConfirmDelete();
        }, true, "Remove the profile after a hold confirm. You can back up first. Saves are deleted after."_i18n);
    }

    options->Add<SidebarEntryHeader>("CONSOLE MOVE"_i18n);
    options->Add<SidebarEntryCallback>("Backup profiles & play hours"_i18n, [this](){
        ConfirmNandBackup();
    }, true, "All profiles on this console plus play hours, same user IDs. For moving to another console. If a save is locked, Hub skips it and leaves a TegraExplorer script."_i18n);
    options->Add<SidebarEntryCallback>("Restore profiles & play hours"_i18n, [this](){
        ConfirmNandRestore();
    }, true, "Write that pack into this console via TegraExplorer. Hours and profiles here are replaced. Back up SYSTEM first."_i18n);
    options->Add<SidebarEntryCallback>("Manage Backups"_i18n, [this](){
        ConfirmNandRestore();
    }, true, "Browse profiles & play hours packs under /config/kefir/nand_transfer."_i18n);

    options->Add<SidebarEntryHeader>("NINTENDO ACCOUNT"_i18n);
    if (!m_items.empty()) {
        options->Add<SidebarEntryCallback>("Link Nintendo Account"_i18n, [this](){
            ConfirmLinkNintendoAccount();
        }, true, "Link all currently unlinked profiles using built-in Nintendo Account donors. Each unlinked profile receives a different donor. Already linked profiles will not be changed. Console will reboot."_i18n);
        options->Add<SidebarEntryCallback>("Unlink Nintendo Account"_i18n, [this](){
            ConfirmUnlinkNintendoAccount();
        }, true, "Remove Nintendo Account link data from selected profiles, or from all linked profiles if none are selected. Console will reboot."_i18n);
    }

    options->Add<SidebarEntryHeader>("VIEW"_i18n);
    SidebarEntryArray::Items layout_items;
    layout_items.push_back("List"_i18n);
    layout_items.push_back("Icon"_i18n);
    layout_items.push_back("Grid"_i18n);
    auto layout_index = m_layout.Get();
    if (layout_index > LayoutType::LayoutType_GridDetail) {
        layout_index = LayoutType::LayoutType_GridDetail;
    }
    options->Add<SidebarEntryArray>("Layout"_i18n, layout_items, [this](s64& index_out){
        m_layout.Set(index_out);
        OnLayoutChange();
    }, layout_index, "Choose how user profiles are displayed."_i18n);
}

} // namespace sphaira::ui::menu::users
