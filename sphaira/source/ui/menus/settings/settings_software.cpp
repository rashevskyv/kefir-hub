#include "ui/menus/settings_menu.hpp"
#include "ui/menus/settings/settings_internal.hpp"
#include "ui/menus/settings/settings_kefir.hpp"
#include "ui/menus/settings/settings_fs_utils.hpp"
#include "ui/menus/settings/settings_translations.hpp"
#include "ui/menus/settings/settings_tweaks.hpp"

#include "ui/menus/appstore.hpp"
#include "ui/menus/ghdl.hpp"

#include "app.hpp"
#include "app_paths.hpp"
#include "i18n.hpp"
#include "ui/nvg_util.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace sphaira::ui::menu::settings {
namespace {

using namespace detail;

auto BuildSoftwareItems() -> std::vector<SettingsItem> {
    std::vector<SettingsItem> items;

    items.emplace_back(SettingsItem{
        "Homebrew App Store"_i18n,
        "Download and update homebrew apps."_i18n,
        [](){
            return std::string{};
        },
        [](){
            App::Push<ui::menu::appstore::Menu>(MenuFlag_None);
        },
        SettingsItemKind::Folder,
    });

    items.emplace_back(SettingsItem{
        "DBI"_i18n,
        "DBI installer and translations."_i18n,
        [](){
            return std::string{};
        },
        [](){
            App::Push<DbiMenu>();
        },
        SettingsItemKind::Folder,
    });

    items.emplace_back(MakePackageAction({
        "UAModDownloader"_i18n,
        "Ukrainian mods."_i18n,
        [](auto pbox) -> Result {
            R_TRY(DownloadFile(
                pbox,
                "Downloading UAModDownloader..."_i18n,
                "https://github.com/pro100luk/UAModDownloader/releases/latest/download/UAModDownloader.nro",
                "/switch/UAModDownloader/UAModDownloader_new.nro"
            ));
            R_TRY(MovePath("/switch/UAModDownloader/UAModDownloader_new.nro", "/switch/UAModDownloader/UAModDownloader.nro"));
            R_SUCCEED();
        },
    }));

    items.emplace_back(MakePackageAction({
        "ModCD"_i18n,
        "ECLIPS graphic mods."_i18n,
        [](auto pbox) -> Result {
            R_TRY(DownloadFile(
                pbox,
                "Downloading ModCD..."_i18n,
                "https://github.com/kawaii-flesh/ModCD/releases/latest/download/ModCD.nro",
                "/switch/ModCD/ModCD_new.nro"
            ));
            R_TRY(MovePath("/switch/ModCD/ModCD_new.nro", "/switch/ModCD/ModCD.nro"));
            R_SUCCEED();
        },
    }));

    items.emplace_back(MakePackageAction({
        "SimpleModDownloader"_i18n,
        "Game mods from GameBanana."_i18n,
        [](auto pbox) -> Result {
            R_TRY(DownloadFile(
                pbox,
                "Downloading SimpleModDownloader..."_i18n,
                "https://github.com/PoloNX/SimpleModDownloader/releases/latest/download/SimpleModDownloader.nro",
                "/switch/SimpleModDownloader/SimpleModDownloader_new.nro"
            ));
            R_TRY(MovePath("/switch/SimpleModDownloader/SimpleModDownloader_new.nro", "/switch/SimpleModDownloader/SimpleModDownloader.nro"));
            R_SUCCEED();
        },
    }));

    items.emplace_back(SettingsItem{
        "Custom Link"_i18n,
        "Direct download .zip or .nro from URL (Keyboard or Phone/PC)."_i18n,
        [](){
            return std::string{};
        },
        [](){
            ui::menu::gh::DownloadDirectLink();
        },
        SettingsItemKind::Download,
    });

    return items;
}


} // namespace

SoftwareMenu::SoftwareMenu() : MenuBase{"Software", MenuFlag_None} {
    m_items = BuildSoftwareItems();
    this->SetActions(
        std::make_pair(Button::A, Action{"Open"_i18n, [this](){
            OnSelect();
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }})
    );

    m_list = std::make_unique<List>(1, 7, Vec4{75.f, 132.f, 1145.f, 462.f}, Vec4{75.f, 132.f, 1130.f, 66.f});
    m_list->SetLayout(List::Layout::GRID);
    m_list->SetPageJump(false);
    SetIndex(0);
}

SoftwareMenu::~SoftwareMenu() = default;

void SoftwareMenu::OnFocusGained() {
    MenuBase::OnFocusGained();
    std::string item_label;
    if (!m_items.empty()) {
        item_label = m_items[m_index].label;
    }
    m_items = BuildSoftwareItems();
    auto it = std::find_if(m_items.cbegin(), m_items.cend(), [&](const auto& item) {
        return item.label == item_label;
    });
    SetIndex(it == m_items.cend() ? m_index : std::distance(m_items.cbegin(), it));
}

void SoftwareMenu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);
    m_list->OnUpdate(controller, touch, m_index, m_items.size(), [this](bool touch, auto i) {
        if (touch && m_index == i) {
            FireAction(Button::A);
        } else {
            App::PlaySoundEffect(SoundEffect_Focus);
            SetIndex(i);
        }
    }, this);
}

void SoftwareMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);
    m_list->Draw(vg, theme, m_items.size(), m_index, [this](auto* vg, auto* theme, Vec4 v, auto i) {
        DrawActionListItem(vg, theme, v, m_items[i], m_index == i);
    });
}

void SoftwareMenu::SetIndex(s64 index) {
    if (m_items.empty()) {
        m_index = 0;
        return;
    }
    m_index = ResolveItemIndex(m_items, index, m_index);
    if (!m_index) {
        m_list->SetYoff(0);
    }
    SetTitleSubHeading(m_items[m_index].description, true);
    SetSubHeading("");
}

void SoftwareMenu::OnSelect() {
    if (!m_items.empty() && m_items[m_index].action) {
        m_items[m_index].action();
    }
}


} // namespace sphaira::ui::menu::settings
