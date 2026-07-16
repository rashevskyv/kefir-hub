# План впровадження: Оновлення мовних пакетів, кругова навігація та фільтрація перекладів (v0.13.225)

Цей план описує зміни для покращення роботи з пакетами перекладів інтерфейсу: динамічна зміна назви кнопки завантаження на оновлення, кругова навігація у списках підменю налаштувань, а також розумна фільтрація варіантів перекладу залежно від мови та регіону консолі з виведенням попередження, якщо жоден варіант не підходить.

## Proposed Changes

### [Component: UI Navigation (Cycling/Wrapping lists)]

---

#### [MODIFY] [settings_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/settings_menu.cpp)
- У конструкторах наступних підменю викликати `m_list->SetWrap(true);` після створення `m_list`:
  - `SoftwareMenu::SoftwareMenu()`
  - `DbiMenu::DbiMenu()`
  - `KefirSettingsMenu::KefirSettingsMenu()`
  - `ThemesMenu::ThemesMenu()`
  - `TranslateMenu::TranslateMenu()`
- Це дозволить здійснювати перехід з першого елемента на останній при натисканні кнопки "Вгору", і навпаки при натисканні "Вниз".

---

### [Component: Translations Interface Options & Filtering]

---

#### [MODIFY] [settings_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/settings_menu.cpp)
- У методі `BuildTranslateItems()` перевіряти, чи вже завантажені мовні пакети (наявність файлу `TRANSLATE_PACKAGE` за допомогою `fs::FileExists(TRANSLATE_PACKAGE)`).
  - Якщо пакети завантажені, показувати пункт `"Update language packs"_i18n` з описом `"Update the UltraHand language package list."_i18n` замість `"Download language packs"_i18n` та `"Download the UltraHand language package list."_i18n`.
- При виборі конкретного перекладу у списку:
  - Отримати поточну мову системи (`SetLanguage`) та регіон консолі (`SetRegionCode`) за допомогою libnx API (`setGetSystemLanguage`, `setMakeLanguage`, `setGetRegionCode`).
  - Відфільтрувати список `options` (повернений з `ReadInterfaceReplacementOptions`), залишаючи лише ті варіанти, які підходять до поточної конфігурації приставки.
  - Якщо є хоча б один сумісний варіант:
    - Показувати `PopupList` лише з цими сумісними варіантами.
  - Якщо сумісних варіантів немає:
    - Показувати попередження за допомогою `OptionBox` (з кнопкою "OK"):
      `"To apply this translation, you must select one of the required variations in the console settings:\n"`
      і далі перераховувати всі доступні варіанти перекладу з оригінального списку.

---

### [Component: Translations (I18N JSON files)]

---

#### [MODIFY] Вси файли локалізації в [i18n](file:///d:/git/dev/sphaira/assets/romfs/i18n/)
Додати нові ключі локалізації для всіх 14 мов:
- `"Update language packs"`
- `"Update the UltraHand language package list."`
- `"To apply this translation, you must select one of the required variations in the console settings:\n"`

---

### [Component: Version]

---

#### [MODIFY] [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt)
- Оновити версію проекту на `0.13.225`.

## Verification Plan

### Automated Tests
- Автоматизовані тести відсутні.

### Manual Verification
- **WSL-збірка**: Перевірити успішність компіляції `kefir-hub.nro`.
- **Перевірка на консолі / у емуляторі**:
  - Перевірити, що в меню `Translate Interface` при першому запуску (коли файлу немає) пишеться "Download language packs".
  - Після успішного завантаження текст пункту змінюється на "Update language packs".
  - Перевірити кругову навігацію в підменю `Software`, `DBI`, `Kefir Settings`, `Themes` та `Translate Interface` (перехід вверх з першого пункту на останній).
  - Перевірити фільтрацію варіантів: якщо мова консолі English, а регіон Europe, має відображатися лише "English for Europe Region" (якщо він є).
  - Перевірити поведінку за відсутності сумісних варіантів: має відображатися інформаційне вікно з пропозицією змінити мову/регіон у налаштуваннях приставки.
