#include "ui/menus/users_menu.hpp"

#include "account_user.hpp"
#include "account_playtime.hpp"
#include "app.hpp"
#include "app_paths.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "nand_transfer.hpp"
#include "i18n.hpp"
#include "image.hpp"
#include "log.hpp"
#include "path_util.hpp"
#include "swkbd.hpp"
#include "ui/menus/file_picker.hpp"
#include "ui/menus/save/save_menu_detail.hpp"
#include "ui/menus/save_menu.hpp"
#include "ui/nvg_util.hpp"
#include "ui/hold_confirm_box.hpp"
#include "ui/image_crop.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/sidebar.hpp"
#include "ui/steamgriddb_icon.hpp"
#include "utils/utils.hpp"

#include <switch/applets/psel.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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

struct RestoreBackupMenu final : MenuBase {
    using Callback = std::function<void(std::optional<std::vector<account_user::Pack>>)>;

    struct Entry {
        account_user::Pack pack;
        int image{};
        bool selected{};
        bool avatar_tried{};
    };

    RestoreBackupMenu(std::vector<account_user::Pack> packs, Callback cb)
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
        m_list = std::make_unique<List>(1, 8, Vec4{75.f, 110.f, 1145.f, 560.f}, Vec4{75.f, 110.f, 1130.f, 80.f});
        SetTitleSubHeading("X marks backups. Minus deletes. A restores."_i18n, true);
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
        fs::FsNativeSd sd;
        std::vector<u8> jpeg;
        if (R_FAILED(sd.read_entire_file((e.pack.dir + "/avatar.jpg").c_str(), jpeg)) || jpeg.empty()) {
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
            detail_str += e.pack.has_playtime ? " · play hours"_i18n : " · no play hours"_i18n;
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
    SetTitleSubHeading(item.nickname + "  ·  " + StatusLabel(item), true);
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

    options->Add<SidebarEntryHeader>("BACKUP & RESTORE USER"_i18n);
    if (!m_items.empty()) {
        options->Add<SidebarEntryCallback>("Backup user"_i18n, [this](){
            ConfirmBackup();
        }, true, "Back up name, avatar, Nintendo Account link and this user's play hours to SD."_i18n);
    }
    options->Add<SidebarEntryCallback>("Restore Backup"_i18n, [this](){
        ConfirmRestoreBackup();
    }, true, "Restore backups as new users: name, avatar, Nintendo link and play hours. Other users' hours stay."_i18n);

    options->Add<SidebarEntryHeader>("CONSOLE MOVE"_i18n);
    options->Add<SidebarEntryCallback>("Backup profiles & play hours"_i18n, [this](){
        ConfirmNandBackup();
    }, true, "All profiles on this console plus play hours, same user IDs. For moving to another console. If a save is locked, Hub skips it and leaves a TegraExplorer script."_i18n);
    options->Add<SidebarEntryCallback>("Restore profiles & play hours"_i18n, [this](){
        ConfirmNandRestore();
    }, true, "Write that pack into this console. Hours and profiles here are replaced. If a save is locked, use restore.te in TegraExplorer."_i18n);

    options->Add<SidebarEntryHeader>("NINTENDO ACCOUNT"_i18n);
    if (!m_items.empty()) {
        options->Add<SidebarEntryCallback>("Link Nintendo Account"_i18n, [this](){
            ConfirmLinkNintendoAccount();
        }, true, "Link all currently unlinked profiles to the official Nintendo Account donor. Already linked profiles will not be changed. Console will reboot."_i18n);
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
        [this, uids](auto op) {
            if (!op || *op == 0) {
                return;
            }
            RunBackup(uids, *op == 1);
        });
}

void Menu::ConfirmNandBackup() {
    App::Push<OptionBox>(
        "Copy every profile on this console plus their play hours to SD (same users, with hours). If the system holds a save, Hub skips it and writes a TegraExplorer script instead of killing services. Not the same as Backup user."_i18n,
        "Cancel"_i18n, "Backup"_i18n, 1,
        [this](auto op) {
            if (op && *op == 1) {
                RunNandBackup();
            }
        });
}

void Menu::ConfirmNandRestore() {
    App::Push<OptionBox>(
        "Write a profiles & play hours pack into this console? Users and hours here will be replaced. If a save is locked, use restore.te in TegraExplorer. Back up SYSTEM first. Y selects the pack folder."_i18n,
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

void Menu::ConfirmRestoreBackup() {
    if (m_items.size() >= ACC_USER_LIST_SIZE) {
        App::Push<OptionBox>("The console already has 8 user profiles."_i18n, "OK"_i18n);
        return;
    }
    const auto packs = account_user::ListUserPacks();
    if (packs.empty()) {
        App::Push<OptionBox>("No user backups found under /config/kefir/user_packs."_i18n, "OK"_i18n);
        return;
    }
    App::Push<RestoreBackupMenu>(packs, [this](auto picked) {
        if (!picked || picked->empty()) {
            return;
        }
        const auto available_slots = ACC_USER_LIST_SIZE - m_items.size();
        if (picked->size() > available_slots) {
            App::Push<OptionBox>(
                "Cannot restore: selecting " + std::to_string(picked->size()) +
                " profile(s) would exceed the maximum of 8 users (available: " +
                std::to_string(available_slots) + ")."_i18n, "OK"_i18n);
            return;
        }
        bool any_link = false;
        for (const auto& p : *picked) {
            if (p.link_valid) {
                any_link = true;
                break;
            }
        }
        std::string gate_reason;
        if (any_link && account_link::IsLinkGated(gate_reason)) {
            App::Push<OptionBox>(gate_reason, "OK"_i18n);
            return;
        }
        RunRestoreBackup(std::move(*picked));
    });
}

void Menu::ConfirmDelete() {
    const auto uids = SelectedUids();
    if (uids.empty()) {
        return;
    }
    const auto msg = (uids.size() > 1)
        ? "Delete the selected users? This cannot be undone. Their game saves will also be deleted. Hold A to confirm."_i18n
        : "Delete this user? This cannot be undone. Their game saves will also be deleted. Hold A to confirm."_i18n;
    App::Push<HoldConfirmBox>(msg, [this, uids](bool ok) {
        if (!ok) {
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
        "Link all currently unlinked profiles to the official Nintendo Account donor? Already linked profiles will not be changed. The console will reboot immediately."_i18n,
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
    }, 1, PRIO_PREEMPTIVE, 1024 * 64, false);
}

void Menu::RunNandBackup() {
    auto report = std::make_shared<nand_transfer::Report>();
    App::Push<ProgressBox>(0, "Backup profiles & play hours"_i18n, "Backup profiles & play hours"_i18n,
        [report](auto pbox) -> Result {
            R_TRY(nand_transfer::Export(pbox, *report));
            R_SUCCEED();
        }, [this, report](Result rc) {
            if (R_FAILED(rc) || report->dir.empty()) {
                App::Push<OptionBox>(
                    "Horizon would not open the system saves. Reboot to RCM, open TegraExplorer, run dump.te (also under TegraExplorer/scripts). That dumps play hours without Horizon."_i18n,
                    "OK"_i18n);
                return;
            }
            std::string msg = "Copied to "_i18n + report->dir + ". ";
            if (report->save_0010 && report->save_00F0) {
                msg += "Profiles and play hours are in the pack. On the other console: Restore profiles & play hours."_i18n;
            } else {
                if (report->save_0010) {
                    msg += "Profiles copied. "_i18n;
                } else {
                    msg += "Profiles were locked by the system. "_i18n;
                }
                if (report->save_00F0) {
                    msg += "Play hours copied. "_i18n;
                } else {
                    msg += "Play hours were locked. Reboot to RCM, TegraExplorer → dump.te (or TegraExplorer/scripts/dump.te). "_i18n;
                }
                msg += "On the other console: Restore profiles & play hours, or restore.te if a save stays locked."_i18n;
            }
            App::Push<OptionBox>(msg, "OK"_i18n);
            Refresh();
        }, 1, PRIO_PREEMPTIVE, 1024 * 256, false);
}

void Menu::RunNandRestore(const std::string& dir) {
    if (!nand_transfer::IsPack(dir)) {
        App::Push<OptionBox>(
            "That folder is not a profiles & play hours pack."_i18n,
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
                    ? "That folder is not a profiles & play hours pack."_i18n
                    : "Horizon would not open the system saves to write. Reboot to RCM and run restore.te from the pack (also under TegraExplorer/scripts)."_i18n;
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

void Menu::RunBackup(std::vector<AccountUid> uids, bool overwrite_existing) {
    if (uids.empty()) {
        return;
    }
    auto dirs = std::make_shared<std::vector<std::string>>();
    App::Push<ProgressBox>(0, "Backup user"_i18n, "Backup user"_i18n,
        [uids = std::move(uids), dirs, overwrite_existing](auto pbox) -> Result {
        pbox->NewTransfer("Writing user pack"_i18n);
        R_TRY(account_user::ExportUserPacks(uids, *dirs, overwrite_existing));
        R_SUCCEED();
    }, [this, dirs](Result rc) {
        if (R_FAILED(rc) || dirs->empty()) {
            App::Push<OptionBox>("Could not write the user pack."_i18n, "OK"_i18n);
            return;
        }
        if (dirs->size() == 1) {
            App::Push<OptionBox>("Exported to "_i18n + dirs->front() + "\n" + "Includes this user's play hours for Restore Backup."_i18n + "\n" + "Game saves are backed up separately through Backup saves."_i18n, "OK"_i18n);
        } else {
            App::Push<OptionBox>("Exported " + std::to_string(dirs->size()) + " user profiles to SD."_i18n + "\n" + "Includes this user's play hours for Restore Backup."_i18n + "\n" + "Game saves are backed up separately through Backup saves."_i18n, "OK"_i18n);
        }
        Refresh();
    }, 1, PRIO_PREEMPTIVE, 1024 * 256, false);
}

namespace {

struct RestoreReport {
    u32 profiles_restored{};
    u32 links_restored{};
    u32 unlinked_restored{};
    u32 link_malformed_count{};
    u32 failed_creations{};
    u32 play_hours_applied{};
    u32 play_hours_missing{};
    bool link_apply_failed{};
    bool play_hours_locked{};
};

} // namespace

void Menu::RunRestoreBackup(std::vector<account_user::Pack> picked_packs) {
    auto report = std::make_shared<RestoreReport>();
    App::Push<ProgressBox>(0, "Restore Backup"_i18n, "Restoring profiles..."_i18n,
        [picked_packs = std::move(picked_packs), report](auto pbox) -> Result {
            fs::FsNativeSd sd;
            std::vector<account_link::TargetLink> links_to_apply;
            struct PlayJob {
                AccountUid uid{};
                std::vector<PdmPlayEvent> events;
            };
            std::vector<PlayJob> play_jobs;

            for (size_t i = 0; i < picked_packs.size(); i++) {
                const auto& p = picked_packs[i];
                pbox->NewTransfer("Creating user profile"_i18n);

                std::vector<u8> jpeg;
                sd.read_entire_file((p.dir + "/avatar.jpg").c_str(), jpeg);

                AccountUid new_uid{};
                const std::string name = !p.nickname.empty() ? p.nickname : "User";
                const auto create_rc = account_user::Create(name, new_uid, jpeg);
                if (R_FAILED(create_rc)) {
                    log_write("[USER] Create user failed 0x%X\n", create_rc);
                    report->failed_creations++;
                    continue;
                }
                report->profiles_restored++;

                account_link::LinkPackage pkg;
                const auto link_load_rc = account_link::LoadUserPackLinkPackage(p.dir, pkg);
                if (R_SUCCEEDED(link_load_rc)) {
                    links_to_apply.push_back({new_uid, std::move(pkg)});
                } else if (!sd.DirExists((p.dir + "/baas").c_str())) {
                    report->unlinked_restored++;
                } else {
                    log_write("[USER] Link package invalid in %s (0x%X)\n", p.dir.c_str(), link_load_rc);
                    report->link_malformed_count++;
                    report->unlinked_restored++;
                }

                std::vector<PdmPlayEvent> play_events;
                if (R_SUCCEEDED(account_playtime::LoadPackPlayEvents(p.dir, play_events)) && !play_events.empty()) {
                    play_jobs.push_back({new_uid, std::move(play_events)});
                } else {
                    report->play_hours_missing++;
                }
            }

            if (!links_to_apply.empty()) {
                pbox->NewTransfer("Applying Nintendo Account link"_i18n);
                u32 count = 0;
                const auto apply_rc = account_link::ApplyLinkPackages(links_to_apply, count);
                if (R_SUCCEEDED(apply_rc)) {
                    report->links_restored = count;
                } else {
                    log_write("[USER] ApplyLinkPackages failed 0x%X\n", apply_rc);
                    report->link_apply_failed = true;
                }
            }

            if (!play_jobs.empty()) {
                pbox->NewTransfer("Writing play hours"_i18n);
                for (const auto& job : play_jobs) {
                    const auto play_rc = account_playtime::AppendPlayEventsForUser(job.uid, job.events);
                    if (R_SUCCEEDED(play_rc)) {
                        report->play_hours_applied++;
                    } else {
                        log_write("[USER] play hours append 0x%X\n", play_rc);
                        report->play_hours_locked = true;
                    }
                }
            }

            R_SUCCEED();
        },
        [this, report](Result /*rc*/) {
            if (report->profiles_restored == 0) {
                App::Push<OptionBox>("Could not restore user profiles."_i18n, "OK"_i18n);
                Refresh();
                return;
            }

            std::string msg = "Restored " + std::to_string(report->profiles_restored) + " user profile(s)."_i18n;
            if (report->links_restored > 0) {
                msg += " " + std::to_string(report->links_restored) + " with Nintendo Account link."_i18n;
            }
            if (report->unlinked_restored > 0) {
                msg += " " + std::to_string(report->unlinked_restored) + " profile(s) restored without link."_i18n;
            }
            if (report->link_malformed_count > 0 || report->link_apply_failed) {
                msg += " " + "Nintendo Account link data was invalid or could not be applied."_i18n;
            }
            if (report->play_hours_applied > 0) {
                msg += " " + "Play hours written for "_i18n + std::to_string(report->play_hours_applied) +
                    " profile(s)."_i18n;
            }
            if (report->play_hours_missing > 0) {
                msg += " " + "A pack had no play hours — make a new backup."_i18n;
            }
            if (report->play_hours_locked) {
                msg += " " + "Play hours could not be written (00F0 locked). Close games and retry Restore Backup."_i18n;
            }

            const bool need_reboot = report->links_restored > 0 || report->play_hours_applied > 0;
            if (need_reboot) {
                msg += " " + "Reboot required."_i18n;
                App::Push<OptionBox>(
                    msg,
                    "Later"_i18n, "Reboot"_i18n, 1,
                    [](auto op) {
                        if (op && *op == 1) {
                            utils::requestForcedReboot();
                        }
                    });
            } else {
                App::Push<OptionBox>(msg, "OK"_i18n);
            }
            Refresh();
        }, 1, PRIO_PREEMPTIVE, 1024 * 256, false);
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
