// Kefir Settings > Nextendo. Rules: nextendo_hosts.hpp / nextendo_plan.hpp, card work: nextendo_apply.cpp.
// Based on Prelude-Nextendo by Nextendo Network (PolyForm Shield 1.0.0). Required Notice: Copyright 2026 Nextendo Network

#include "ui/menus/settings_menu.hpp"
#include "ui/menus/settings/settings_internal.hpp"
#include "ui/menus/settings/settings_kefir.hpp"
#include "ui/menus/settings/settings_translations.hpp"
#include "ui/menus/settings/settings_tweaks.hpp"

#include "app.hpp"
#include "i18n.hpp"
#include "nextendo_apply.hpp"
#include "ui/nvg_util.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace sphaira::ui::menu::settings {

namespace {

using namespace detail;
namespace nx = ::sphaira::nextendo;

auto SupportedFirmwareText() -> std::string {
    std::string out;
    for (const auto& v : nx::kSupportedFirmware) {
        out += (out.empty() ? "" : ", ") + std::to_string(v[0]) + "." + std::to_string(v[1]) + "." + std::to_string(v[2]);
    }
    return out;
}

auto InstalledFirmwareText() -> std::string {
    const auto v = hosversionGet();
    return std::to_string(HOSVER_MAJOR(v)) + "." + std::to_string(HOSVER_MINOR(v)) + "." + std::to_string(HOSVER_MICRO(v));
}

// Replaces the %s of an i18n text.
auto Fill(std::string fmt, const std::string& arg) -> std::string {
    const auto pos = fmt.find("%s");
    if (pos != std::string::npos) {
        fmt.replace(pos, 2, arg);
    }
    return fmt;
}

auto StatusText() -> std::string {
    if (!nx::FirmwareOk()) {
        return "Off (firmware)"_i18n;
    }
    switch (nx::CurrentMode()) {
        case nx::Mode::EmuMMC: return nx::EmuEnabled() ? "On"_i18n : "Off"_i18n;
        case nx::Mode::SemiStock: return nx::SysEnabled() ? "On (sysNAND)"_i18n : "Off"_i18n;
        case nx::Mode::Other: break;
    }
    return "Not in use"_i18n;
}

auto StatusHelp() -> std::string {
    if (!nx::FirmwareOk()) {
        return Fill("Nextendo needs system firmware %s until Kefir Hub is updated. It is switched off on this firmware."_i18n, SupportedFirmwareText());
    }
    if (nx::CurrentMode() == nx::Mode::Other) {
        return "Nextendo works on the emuMMC, or on the sysNAND while Semi-stock is on."_i18n;
    }
    return "Nextendo is set up when Kefir Hub starts and needs no button press."_i18n;
}

// A toggle: store the choice, make the card match, restart only if something was really written.
auto ApplyChoice(void (*store)(bool), bool enabled) -> Result {
    store(enabled);
    bool changed{};
    R_TRY(nx::Reconcile(changed));
    if (changed) {
        RebootAfterSetting();
    }
    R_SUCCEED();
}

auto BuildNextendoItems() -> std::vector<SettingsItem> {
    std::vector<SettingsItem> items;

    items.emplace_back(SettingsItem{
        "Status"_i18n,
        StatusHelp(),
        [](){ return StatusText(); },
        {},
    });

    items.emplace_back(MakeKefirToggle({
        "Nextendo on emuMMC"_i18n,
        "Redirects the online services of supported games to the Nextendo Network while the emuMMC runs. On by default."_i18n,
        [](){ return nx::EmuEnabled(); },
        [](bool enabled){ return ApplyChoice(nx::SetEmuEnabled, enabled); },
        "The console restarts to apply this. Nextendo uses the real PRODINFO of the console inside the emuMMC; Nintendo servers are not reached."_i18n,
        0.5f,
    }));

    // only while Semi-stock is on; the option is switched off when Semi-stock ends
    if (nx::CurrentMode() == nx::Mode::SemiStock) {
        items.emplace_back(MakeKefirToggle({
            "Nextendo on sysNAND (Semi-stock)"_i18n,
            "Uses Nextendo on the original system instead of the official online. Off by default."_i18n,
            [](){ return nx::SysEnabled(); },
            [](bool enabled){ return ApplyChoice(nx::SetSysEnabled, enabled); },
            "Semi-stock is meant for the official Nintendo online. With this on, the Nintendo services go to the Nextendo Network instead, so the eShop and other official online features may stop working.\n\nThere is no PRODINFO protection in Semi-stock. The console restarts to apply this."_i18n,
            3.f,
        }));
    }

    items.emplace_back(SettingsItem{
        "System firmware"_i18n,
        Fill("Nextendo needs the latest system software that this Kefir Hub supports. Supported: %s"_i18n, SupportedFirmwareText()),
        [](){ return InstalledFirmwareText(); },
        {},
    });

    items.emplace_back(SettingsItem{
        "Server"_i18n,
        "Address of the Nextendo server written into the hosts file."_i18n,
        [](){ return nx::ServerIpsInUse().main; },
        {},
    });

    items.emplace_back(SettingsItem{
        "Release"_i18n,
        "Based on Prelude-Nextendo by Nextendo Network, PolyForm Shield License 1.0.0. Required Notice: Copyright 2026 Nextendo Network"_i18n,
        [](){ return std::string{nx::kPinnedTag}; },
        {},
    });

    if (nx::RestartNeeded()) {
        items.emplace_back(SettingsItem{
            "Restart needed"_i18n,
            "Restart the console so that the changes take effect."_i18n,
            [](){ return std::string{"!"}; },
            {},
        });
    }

    return items;
}

} // namespace

NextendoMenu::NextendoMenu() : MenuBase{"Nextendo", MenuFlag_None} {
    m_items = BuildNextendoItems();
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

NextendoMenu::~NextendoMenu() = default;

void NextendoMenu::OnFocusGained() {
    MenuBase::OnFocusGained();
    std::string item_label;
    if (!m_items.empty()) {
        item_label = m_items[m_index].label;
    }
    m_items = BuildNextendoItems();
    auto it = std::find_if(m_items.cbegin(), m_items.cend(), [&](const auto& item) {
        return item.label == item_label;
    });
    SetIndex(it == m_items.cend() ? m_index : std::distance(m_items.cbegin(), it));
}

void NextendoMenu::Update(Controller* controller, TouchInfo* touch) {
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

void NextendoMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);
    m_list->Draw(vg, theme, m_items.size(), m_index, [this](auto* vg, auto* theme, Vec4 v, auto i) {
        DrawActionListItem(vg, theme, v, m_items[i], m_index == i);
    });
}

void NextendoMenu::SetIndex(s64 index) {
    if (m_items.empty()) {
        m_index = 0;
        return;
    }
    m_index = std::clamp<s64>(index, 0, static_cast<s64>(m_items.size() - 1));
    if (!m_index) {
        m_list->SetYoff(0);
    }
    SetTitleSubHeading(m_items[m_index].description, true);
    SetSubHeading("");
}

void NextendoMenu::OnSelect() {
    if (!m_items.empty() && m_items[m_index].action) {
        m_items[m_index].action();
    }
}

} // namespace sphaira::ui::menu::settings
