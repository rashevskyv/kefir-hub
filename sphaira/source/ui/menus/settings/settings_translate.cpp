#include "ui/menus/settings_menu.hpp"
#include "ui/menus/settings/settings_internal.hpp"
#include "ui/menus/settings/settings_kefir.hpp"
#include "ui/menus/settings/settings_fs_utils.hpp"
#include "ui/menus/settings/settings_translations.hpp"
#include "ui/menus/settings/settings_tweaks.hpp"

#include "app.hpp"
#include "app_paths.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "ui/hold_confirm_box.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/progress_box.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sphaira::ui::menu::settings {
namespace {

using namespace detail;

bool IsOptionApplicable(const std::pair<std::string, std::string>& option, SetLanguage console_lang, SetRegion console_region) {
    std::string label = option.first;
    std::string dir = option.second;
    std::transform(label.begin(), label.end(), label.begin(), [](unsigned char c){ return std::tolower(c); });
    std::transform(dir.begin(), dir.end(), dir.begin(), [](unsigned char c){ return std::tolower(c); });
    std::string opt = label + " " + dir;

    // Detect language of the option
    bool opt_is_english = (opt.find("english") != std::string::npos || opt.find("en-") != std::string::npos || opt == "en" || opt.find(" en") != std::string::npos);
    bool opt_is_russian = (opt.find("russian") != std::string::npos || opt.find("ru-") != std::string::npos || opt == "ru" || opt.find(" ru") != std::string::npos);
    bool opt_is_french = (opt.find("french") != std::string::npos || opt.find("fr-") != std::string::npos || opt == "fr" || opt.find(" fr") != std::string::npos);
    bool opt_is_german = (opt.find("german") != std::string::npos || opt.find("de-") != std::string::npos || opt == "de" || opt.find(" de") != std::string::npos);
    bool opt_is_italian = (opt.find("italian") != std::string::npos || opt.find("it-") != std::string::npos || opt == "it" || opt.find(" it") != std::string::npos);
    bool opt_is_spanish = (opt.find("spanish") != std::string::npos || opt.find("es-") != std::string::npos || opt == "es" || opt.find(" es") != std::string::npos);
    bool opt_is_chinese = (opt.find("chinese") != std::string::npos || opt.find("zh-") != std::string::npos || opt == "zh" || opt.find(" zh") != std::string::npos);
    bool opt_is_korean = (opt.find("korean") != std::string::npos || opt.find("ko-") != std::string::npos || opt == "ko" || opt.find(" ko") != std::string::npos);
    bool opt_is_dutch = (opt.find("dutch") != std::string::npos || opt.find("nl-") != std::string::npos || opt == "nl" || opt.find(" nl") != std::string::npos);
    bool opt_is_portuguese = (opt.find("portuguese") != std::string::npos || opt.find("pt-") != std::string::npos || opt == "pt" || opt.find(" pt") != std::string::npos);
    bool opt_is_japanese = (opt.find("japanese") != std::string::npos || opt.find("ja-") != std::string::npos || opt == "ja" || opt.find(" ja") != std::string::npos);

    // Check if console language matches
    bool lang_matches = false;
    if (opt_is_english && (console_lang == SetLanguage_ENUS || console_lang == SetLanguage_ENGB)) lang_matches = true;
    else if (opt_is_russian && console_lang == SetLanguage_RU) lang_matches = true;
    else if (opt_is_french && (console_lang == SetLanguage_FR || console_lang == SetLanguage_FRCA)) lang_matches = true;
    else if (opt_is_german && console_lang == SetLanguage_DE) lang_matches = true;
    else if (opt_is_italian && console_lang == SetLanguage_IT) lang_matches = true;
    else if (opt_is_spanish && (console_lang == SetLanguage_ES || console_lang == SetLanguage_ES419)) lang_matches = true;
    else if (opt_is_chinese && (console_lang == SetLanguage_ZHCN || console_lang == SetLanguage_ZHTW || console_lang == SetLanguage_ZHHANS || console_lang == SetLanguage_ZHHANT)) lang_matches = true;
    else if (opt_is_korean && console_lang == SetLanguage_KO) lang_matches = true;
    else if (opt_is_dutch && console_lang == SetLanguage_NL) lang_matches = true;
    else if (opt_is_portuguese && (console_lang == SetLanguage_PT || console_lang == SetLanguage_PTBR)) lang_matches = true;
    else if (opt_is_japanese && console_lang == SetLanguage_JA) lang_matches = true;
    else if (!opt_is_english && !opt_is_russian && !opt_is_french && !opt_is_german && !opt_is_italian && 
             !opt_is_spanish && !opt_is_chinese && !opt_is_korean && !opt_is_dutch && !opt_is_portuguese && !opt_is_japanese) {
        lang_matches = true;
    }

    // Detect region of the option
    bool opt_is_usa = (opt.find("american") != std::string::npos || opt.find("usa") != std::string::npos || opt.find("-us") != std::string::npos || opt.find("419") != std::string::npos || opt.find("-br") != std::string::npos);
    bool opt_is_eur = (opt.find("europe") != std::string::npos || opt.find("eur") != std::string::npos || opt.find("-gb") != std::string::npos || opt.find("british") != std::string::npos);
    bool opt_is_jpn = (opt.find("japan") != std::string::npos || opt.find("jpn") != std::string::npos || opt.find("-jp") != std::string::npos);
    bool opt_is_aus = (opt.find("australia") != std::string::npos || opt.find("aus") != std::string::npos);
    bool opt_is_chn = (opt.find("china") != std::string::npos || opt.find("chn") != std::string::npos || opt.find("-cn") != std::string::npos);
    bool opt_is_kor = (opt.find("korea") != std::string::npos || opt.find("kor") != std::string::npos || opt.find("-kr") != std::string::npos);
    bool opt_is_twn = (opt.find("taiwan") != std::string::npos || opt.find("twn") != std::string::npos || opt.find("-tw") != std::string::npos);

    // Check if console region matches
    bool region_matches = false;
    if (opt_is_usa && console_region == SetRegion_USA) region_matches = true;
    else if (opt_is_eur && console_region == SetRegion_EUR) region_matches = true;
    else if (opt_is_jpn && console_region == SetRegion_JPN) region_matches = true;
    else if (opt_is_aus && (console_region == SetRegion_AUS || console_region == SetRegion_EUR)) region_matches = true;
    else if (opt_is_chn && console_region == SetRegion_CHN) region_matches = true;
    else if (opt_is_kor && console_region == SetRegion_HTK) region_matches = true;
    else if (opt_is_twn && console_region == SetRegion_HTK) region_matches = true;
    else if (!opt_is_usa && !opt_is_eur && !opt_is_jpn && !opt_is_aus && !opt_is_chn && !opt_is_kor && !opt_is_twn) {
        region_matches = true;
    }

    return lang_matches && region_matches;
}

auto BuildTranslateItems() -> std::vector<SettingsItem> {
    std::vector<SettingsItem> items;

    const bool downloaded = fs::FileExists(TRANSLATE_PACKAGE);

    items.emplace_back(MakePackageAction({
        downloaded ? "Update language packs"_i18n : "Download language packs"_i18n,
        downloaded ? "Update the UltraHand language package list."_i18n : "Download the UltraHand language package list."_i18n,
        [downloaded](auto pbox) -> Result {
            R_TRY(DownloadFile(
                pbox,
                downloaded ? "Updating language packs..."_i18n : "Downloading language packs..."_i18n,
                "https://github.com/rashevskyv/switch-translations-mirrors/raw/main/lang_packs_ultra.zip",
                paths::DOWNLOADS + "/lang_packs.zip"
            ));
            R_TRY(MovePath(TRANSLATE_PACKAGE, TRANSLATE_PACKAGE_BACKUP));
            R_TRY(DeletePath(TRANSLATE_PACKAGE_DIR));
            fs::FsNativeSd fs;
            R_TRY(fs.CreateDirectoryRecursively(TRANSLATE_PACKAGE_DIR));
            if (fs::FileExists(TRANSLATE_PACKAGE_BACKUP)) {
                R_TRY(CopyFileSimple(TRANSLATE_PACKAGE_BACKUP, std::string{TRANSLATE_PACKAGE_DIR} + "/package.ini.bkp"));
            }
            R_TRY(UnzipFile(pbox, paths::DOWNLOADS + "/lang_packs.zip", TRANSLATE_PACKAGE_DIR));
            R_TRY(DeletePath(paths::DOWNLOADS + "/lang_packs.zip"));
            R_SUCCEED();
        },
    }));

    items.emplace_back(MakePackageAction({
        "Remove installed translation"_i18n,
        "Delete installed interface translations and reboot."_i18n,
        [](auto pbox) -> Result {
            return RemoveInterfaceTranslation(pbox);
        },
        true,
        "This removes installed system interface translation files and reboots the console."_i18n,
        0.5f,
    }));

    for (const auto& entry : ParseInterfaceTranslations(TRANSLATE_PACKAGE)) {
        items.emplace_back(SettingsItem{
            entry.name,
            "Install interface translation."_i18n,
            [](){
                return std::string{};
            },
            [entry](){
                const auto options = ReadInterfaceReplacementOptions(entry);
                if (options.empty()) {
                    App::PushErrorBox(Result_FsEmpty, "No replacement languages found"_i18n);
                    return;
                }

                u64 languageCode{};
                SetLanguage console_lang = SetLanguage_ENGB;
                if (R_SUCCEEDED(setGetSystemLanguage(&languageCode))) {
                    setMakeLanguage(languageCode, &console_lang);
                }
                SetRegion console_region = SetRegion_EUR;
                setGetRegionCode(&console_region);

                std::vector<std::pair<std::string, std::string>> applicable_options;
                for (const auto& opt : options) {
                    if (IsOptionApplicable(opt, console_lang, console_region)) {
                        applicable_options.push_back(opt);
                    }
                }

                if (applicable_options.empty()) {
                    std::string msg = "To apply this translation, you must select one of the required variations in the console settings:\n"_i18n;
                    for (const auto& opt : options) {
                        msg += "- " + opt.first + "\n";
                    }
                    App::Push<OptionBox>(msg, "OK"_i18n);
                    return;
                }

                PopupList::Items labels;
                labels.reserve(applicable_options.size());
                for (const auto& [label, dir] : applicable_options) {
                    labels.push_back(label);
                }

                App::Push<PopupList>(
                    "Replace language"_i18n,
                    labels,
                    [entry, applicable_options](auto index){
                        if (!index) {
                            return;
                        }

                        const auto dir = applicable_options[*index].second;
                        App::Push<HoldConfirmBox>(
                            "This will replace the selected system interface language and reboot the console."_i18n,
                            0.5f,
                            [entry, dir](bool confirmed){
                                if (!confirmed) {
                                    return;
                                }

                                App::Push<ProgressBox>(
                                    0,
                                    "Installing"_i18n,
                                    entry.name,
                                    [entry, dir](auto pbox) -> Result {
                                        return InstallInterfaceTranslation(pbox, entry, dir);
                                    },
                                    [](Result rc){
                                        if (R_SUCCEEDED(rc)) {
                                            return;
                                        }

                                        if (rc == Result_TranslationRemoveExistingFailed) {
                                            App::Push<OptionBox>(
                                                "The installed translation could not be replaced.\nRemove it and reboot the console?\nAfter the reboot, install the translation again."_i18n,
                                                "Cancel"_i18n, "Remove and reboot"_i18n, 1,
                                                [](auto op_index){
                                                    if (op_index && *op_index) {
                                                        App::Push<ProgressBox>(
                                                            0,
                                                            "Removing"_i18n,
                                                            "",
                                                            [](auto pbox) -> Result {
                                                                return RemoveInterfaceTranslationAndReboot(pbox);
                                                            },
                                                            [](Result remove_rc){
                                                                if (R_FAILED(remove_rc)) {
                                                                    App::PushErrorBox(remove_rc, "Failed to remove translation"_i18n);
                                                                }
                                                            }
                                                        );
                                                    }
                                                }
                                            );
                                            return;
                                        }

                                        App::PushErrorBox(rc, "Failed to install translation"_i18n);
                                    }
                                );
                            }
                        );
                    }
                );
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
    m_list->Draw(vg, theme, m_items.size(), [this](auto* vg, auto* theme, Vec4 v, auto i) {
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
