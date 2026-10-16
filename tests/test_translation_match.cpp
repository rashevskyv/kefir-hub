// Host test for sphaira/include/ui/menus/settings/translation_match.hpp

#include "ui/menus/settings/translation_match.hpp"

#include <cassert>
#include <cstdio>

using namespace sphaira::ui::menu::settings::translation;

int main() {
    // the reported case: Russian system language; only the ru folder fits, whatever the console region
    assert(Fits("replaces_ru-EU", "ru"));
    assert(!Fits("replaces_en-US", "ru"));
    assert(!Fits("replaces_en-EU", "ru"));

    // English: US and UK are two system languages
    assert(Fits("replaces_en-US", "en-US"));
    assert(!Fits("replaces_en-EU", "en-US"));
    assert(Fits("replaces_en-EU", "en-GB"));

    // the other American variants
    assert(Fits("replaces_es-US", "es-419"));
    assert(Fits("replaces_es-EU", "es"));
    assert(Fits("replaces_fr-US", "fr-CA"));
    assert(Fits("replaces_fr-EU", "fr"));
    assert(Fits("replaces_pt-US", "pt-BR"));
    assert(Fits("replaces_pt-EU", "pt"));
    assert(Fits("replaces_de-EU", "de"));

    // no NX-Translation folder for Japanese or Chinese
    assert(!Fits("replaces_en-US", "ja"));
    assert(!Fits("replaces_en-EU", "zh-Hans"));

    assert(SystemLanguageName("replaces_ru-EU") == "Русский");
    assert(SystemLanguageName("replaces_en-US") == "English (US)");
    assert(SystemLanguageName("replaces_xx-EU").empty());

    // the path is in the system's language, not the Hub's
    assert(LanguageSettingPath("en-GB") == "System Settings → System → Language");
    assert(LanguageSettingPath("ru") == "Системные настройки → Система → Язык");
    assert(LanguageSettingPath("es-419") == LanguageSettingPath("es"));

    std::puts("test_translation_match: ok");
}
