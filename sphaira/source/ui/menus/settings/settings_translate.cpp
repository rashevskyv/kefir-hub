#include "ui/menus/settings_menu.hpp"
#include "ui/menus/settings/settings_internal.hpp"
#include "ui/menus/settings/settings_kefir.hpp"
#include "ui/menus/settings/settings_fs_utils.hpp"
#include "ui/menus/settings/settings_translations.hpp"
#include "ui/menus/settings/translation_match.hpp"
#include "ui/menus/settings/translation_policy.hpp"
#include "ui/menus/settings/settings_tweaks.hpp"
#include "hats_version.hpp"
#include "utils/utils.hpp"

#include "app.hpp"
#include "app_paths.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "ui/hold_confirm_box.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sphaira::ui::menu::settings {
namespace {

using namespace detail;

// "en-US", "ru", "es-419", ...: the language the system menus are shown in
auto SystemLanguageCode() -> std::string {
    u64 code{};
    if (R_FAILED(setGetSystemLanguage(&code))) {
        return {};
    }
    char s[sizeof(code) + 1]{};
    std::memcpy(s, &code, sizeof(code));
    return s;
}

// Replaces the first %s in fmt with each arg in turn.
auto Fill(std::string fmt, std::initializer_list<std::string> args) -> std::string {
    size_t from = 0;
    for (const auto& a : args) {
        const auto pos = fmt.find("%s", from);
        if (pos == std::string::npos) {
            break;
        }
        fmt.replace(pos, 2, a);
        from = pos + a.size();
    }
    return fmt;
}

auto InstallTranslation(const InterfaceTranslationEntry& entry, const std::string& dir) {
    App::Push<ProgressBox>(
        0,
        "Installing"_i18n,
        entry.name,
        [entry, dir](auto pbox) -> Result {
            return InstallInterfaceTranslation(pbox, entry, dir);
        },
        [](Result rc){
            if (R_FAILED(rc)) {
                App::PushErrorBox(rc, "Failed to install translation"_i18n);
            }
        }
    );
}

// The translation goes over the system language the console uses now; when it has no folder for that
// language, say which system languages it replaces and where the setting is, in the system's own words.
auto OpenTranslation(const InterfaceTranslationEntry& entry, const FirmwareCompatibility& compat) {
    const auto code = SystemLanguageCode();
    const auto it = std::find_if(entry.replacements.cbegin(), entry.replacements.cend(), [&](const auto& r) {
        return translation::Fits(r.second, code);
    });

    if (it == entry.replacements.cend()) {
        std::string langs;
        for (const auto& [label, dir] : entry.replacements) {
            const auto name = translation::SystemLanguageName(dir);
            langs += "\n• " + (name.empty() ? label : name);
        }
        App::Push<OptionBox>(
            Fill("%s replaces only these system languages:%s\n\nSet one of them in %s, then select %s again."_i18n,
                 {entry.name, langs, translation::LanguageSettingPath(code), entry.name}),
            "OK"_i18n
        );
        return;
    }

    const auto dir = it->second;
    auto msg = Fill("%s replaces %s. The console reboots after the install."_i18n,
                    {entry.name, translation::SystemLanguageName(dir).empty() ? it->first : translation::SystemLanguageName(dir)});
    if (entry.warning_required) {
        const auto target_fw = ExtractFirmwareFromTag(compat.target_tag);
        msg += "\n\n" + Fill("This translation was made for firmware %s; some system text can be missing or untranslated."_i18n,
                             {target_fw.empty() ? std::string{"?"} : target_fw});
    }
    App::Push<OptionBox>(
        msg,
        "Cancel"_i18n, "Install"_i18n, 1,
        [entry, dir](auto op_index){
            if (op_index && *op_index == 1) {
                InstallTranslation(entry, dir);
            }
        }
    );
}

auto FormatUnavailableMsg(const std::string& fw) -> std::string {
    std::string msg = "Translations are not available for firmware %s."_i18n;
    if (const auto pos = msg.find("%s"); pos != std::string::npos) {
        msg.replace(pos, 2, fw);
    } else {
        msg += " (" + fw + ")";
    }
    return msg;
}

auto MakeRemoveTranslationItem() -> SettingsItem {
    return {
        "Remove installed translation"_i18n,
        "Delete installed interface translation files."_i18n,
        [](){
            return HasInstalledTranslation() ? "Installed"_i18n : "";
        },
        [](){
            if (!HasInstalledTranslation()) {
                App::Push<OptionBox>(
                    "No installed translation files were found."_i18n,
                    "OK"_i18n
                );
                return;
            }

            App::Push<HoldConfirmBox>(
                "This removes installed system interface translation files.\n\nHold A to continue."_i18n,
                0.5f,
                [](bool confirmed) {
                    if (!confirmed) {
                        return;
                    }

                    App::Push<ProgressBox>(
                        0,
                        "Removing"_i18n,
                        "Interface Translation"_i18n,
                        [](auto pbox) -> Result {
                            return RemoveInterfaceTranslationAndReboot(pbox);
                        },
                        [](Result rc) {
                            if (R_FAILED(rc)) {
                                App::PushErrorBox(rc, "Failed to remove translation"_i18n);
                            }
                        }
                    );
                }
            );
        },
        SettingsItemKind::Download,
    };
}

auto BuildTranslateItems() -> std::vector<SettingsItem> {
    std::vector<SettingsItem> items;

    const std::string fw = hats::getSystemFirmware();
    SetRegion console_region = SetRegion_EUR;
    setGetRegionCode(&console_region);
    const auto compat = ResolveFirmwareCompatibility(fw, console_region == SetRegion_CHN);
    if (!compat.available) {
        const auto msg = FormatUnavailableMsg(fw);
        items.emplace_back(SettingsItem{
            "Console Firmware: "_i18n + fw,
            msg,
            [](){ return "Unsupported"_i18n; },
            [msg]() {
                App::Push<OptionBox>(msg, "OK"_i18n);
            },
            SettingsItemKind::Normal,
        });

        items.emplace_back(MakeRemoveTranslationItem());

        return items;
    }

    auto cached_entries = LoadTranslationsCache(TRANSLATIONS_CACHE_PATH, compat.target_tag);
    for (auto& entry : cached_entries) {
        entry.warning_required = compat.warning_required;
    }
    const bool has_cache = !cached_entries.empty();

    items.emplace_back(MakePackageAction({
        has_cache ? "Refresh translations"_i18n : "Load translations"_i18n,
        "Firmware: "_i18n + fw,
        [compat, fw](auto pbox) -> Result {
            return FetchAndCacheTranslations(pbox, compat.target_tag, compat.metadata_tag, fw, compat.warning_required);
        },
    }));

    items.emplace_back(MakeRemoveTranslationItem());

    for (const auto& entry : cached_entries) {
        items.emplace_back(SettingsItem{
            entry.name,
            {},
            {},
            [entry, compat](){
                OpenTranslation(entry, compat);
            },
            SettingsItemKind::Folder,
        });
    }

    return items;
}


} // namespace

TranslateMenu::TranslateMenu() : MenuBase{"Translate Interface"_i18n, MenuFlag_None} {
    m_items = BuildTranslateItems();
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
    m_list->SetSectionGap(1, 33.f);
    SetIndex(0);
}

TranslateMenu::~TranslateMenu() = default;

void TranslateMenu::OnFocusGained() {
    MenuBase::OnFocusGained();
    std::string item_label;
    if (!m_items.empty()) {
        item_label = m_items[m_index].label;
    }
    m_items = BuildTranslateItems();
    auto it = std::find_if(m_items.cbegin(), m_items.cend(), [&](const auto& item) {
        return item.label == item_label;
    });
    SetIndex(it == m_items.cend() ? m_index : std::distance(m_items.cbegin(), it));
}

void TranslateMenu::Update(Controller* controller, TouchInfo* touch) {
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

void TranslateMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);
    m_list->Draw(vg, theme, m_items.size(), m_index, [this](auto* vg, auto* theme, Vec4 v, auto i) {
        DrawActionListItem(vg, theme, v, m_items[i], m_index == i);
    });
}

void TranslateMenu::SetIndex(s64 index) {
    if (m_items.empty()) {
        m_index = 0;
        return;
    }
    m_index = std::clamp<s64>(index, 0, static_cast<s64>(m_items.size() - 1));
    if (!m_index) {
        m_list->SetYoff(0);
    } else {
        m_list->EnsureVisible(m_index, m_items.size());
    }
    SetTitleSubHeading(m_items[m_index].description, true);
    SetSubHeading("");
}

void TranslateMenu::OnSelect() {
    if (!m_items.empty() && m_items[m_index].action) {
        m_items[m_index].action();
    }
}


} // namespace sphaira::ui::menu::settings
