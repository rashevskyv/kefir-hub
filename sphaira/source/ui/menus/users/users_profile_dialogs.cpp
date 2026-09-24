#include "ui/menus/users/users_internal.hpp"

#include "account/account_user.hpp"
#include "app.hpp"
#include "app_paths.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "image.hpp"
#include "path_util.hpp"
#include "swkbd.hpp"
#include "ui/image_crop.hpp"
#include "ui/list.hpp"
#include "ui/menus/file_picker.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/menus/save_menu.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/steamgriddb_icon.hpp"

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
        m_list->Draw(vg, theme, m_entries.size(), m_index, [this](auto* vg, auto* theme, Vec4 v, auto i) {
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
        m_list->Draw(vg, theme, m_tiles.size(), m_index, [this](auto* vg, auto* theme, Vec4 v, auto i) {
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

void PickSavesForBackup(std::vector<save::Entry> entries, std::function<void(std::optional<std::vector<save::Entry>>)> cb) {
    App::Push<SavePickMenu>(std::move(entries), std::move(cb));
}

void PickAvatar(std::function<void(std::vector<u8>)> cb) {
    App::Push(std::make_unique<AvatarPickMenu>(std::move(cb)));
}

} // namespace sphaira::ui::menu::users
