# Опис змін (Walkthrough) — Аудит Web Sharing / Direct Install

## v0.13.187 — Декомпозиція меню налаштувань: винесення редактора кривої вентилятора у settings_fancurve (Фаза 9)

### Завдання
Виконати Фазу 9 плану рефакторингу: декомпозиція файлу реалізації [settings_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/settings_menu.cpp) (зменшення розміру з 2604 рядків до цільових ~1400) шляхом виділення редактора кривої вентилятора в окремий модуль:
1. **Крок 9.1 (Runtime-частина)**: `EvaluateFanPercent`, `FanCurveSensorSample`, `FanCurveSensorReader` та `SphairaFanState`.
2. **Крок 9.2 (Малювальні хелпери)**: Допоміжні функції малювання кривої вентилятора (`DrawFanCurveGraph`, `DrawFanCurveListItem` тощо).
3. **Крок 9.3 (Клас FanCurveMenu)**: Перенесення всього класу `FanCurveMenu` разом з його методами в новий модуль.
4. **Крок 9.4 (Дедуплікація HoldConfirmBox та очищення)**: Очищення `settings_menu.cpp` від винесеного коду, усунення конфлікту локального `HoldConfirmBox` з глобальним шляхом його розширення.

### Підхід
1. **Створення нового модуля вентилятора**:
   - Створено [settings_fancurve.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/menus/settings/settings_fancurve.hpp) та [settings_fancurve.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/settings/settings_fancurve.cpp).
   - Перенесено структури `FanCurveSensorSample`, `SphairaFanState`, клас `FanCurveSensorReader` та функцію `EvaluateFanPercent`.
   - Перенесено малювальні хелпери: `FanCurveProfileLabel`, `FanCurveGraphRect`, `FanCurvePlotRect`, `FanCurveListRect`, `FanCurveListItemRect`, `FanCurveXForTemp`, `FanCurveYForFan`, `FanCurveTempForX`, `FanCurveFanForY`, `ExpandRect`, `WithAlpha`, `FormatMilliC`, `DrawHorizontalDashes`, `DrawVerticalDashes`, `DrawFanCurveSensorMarker`, `DrawFanCurveGraph`, `DrawFanCurveListItem`, `DrawFanCurveListHeader`.
   - Перенесено оголошення та визначення методів класу `FanCurveMenu`.
2. **Дедуплікація `HoldConfirmBox`**:
   - Виявлено дублювання класів `HoldConfirmBox` (локальний у `settings_menu.cpp` та глобальний у `ui/hold_confirm_box.hpp`).
   - Розширено глобальний [HoldConfirmBox](file:///d:/git/dev/sphaira/sphaira/source/ui/hold_confirm_box.cpp): додано конструктор з підтримкою кастомного часу утримання кнопки (`hold_seconds`).
   - Вилучено локальну реалізацію `HoldConfirmBox` з `settings_menu.cpp` і замінено на використання глобального віджета. Це дозволило вирішити конфлікт лінкінгу в `settings_fancurve.cpp`.
3. **Очищення та інтеграція**:
   - Вилучено понад 1200 рядків коду з [settings_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/settings_menu.cpp). Цільовий розмір файлу досягнуто (~1380 рядків).
   - Додано `settings_fancurve.cpp` до [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).
   - Підвищено версію програми до `0.13.187`.

### Результати тестування
* Проект успішно збирається під WSL за допомогою cmake. Бінарний файл `kefir-hub.nro` успішно згенеровано.

## v0.13.186 — Рефакторинг devoptab_common.cpp (Фаза 8)


### Завдання
Виконати Фазу 8 плану рефакторингу: декомпозиція занадто великого файлу реалізації [devoptab_common.cpp](file:///d:/git/dev/sphaira/sphaira/source/utils/devoptab_common.cpp) (~1660 рядків) та його заголовка на окремі логічні модулі:
1. **Крок 8.1 (Буферизація читання)**: `BufferedData` та `LruBufferedData`.
2. **Крок 8.2 (Потокова передача Curl)**: `PushPullThreadData`, `PushThreadData`, `PullThreadData`.
3. **Крок 8.3 (Curl пристрій)**: `MountCurlDevice` та його допоміжні функції.

### Підхід
1. **Створення нового файлу буферизації**:
   - Створено [devoptab_buffered.hpp](file:///d:/git/dev/sphaira/sphaira/include/utils/devoptab_buffered.hpp) та [devoptab_buffered.cpp](file:///d:/git/dev/sphaira/sphaira/source/utils/devoptab_buffered.cpp).
   - Перенесено класи `BufferedDataBase`, `BufferedData`, `BufferedFileData` та `LruBufferedData` з їх реалізацією методу `Read`.
2. **Створення файлу curl-потоків**:
   - Створено [devoptab_curl_thread.hpp](file:///d:/git/dev/sphaira/sphaira/include/utils/devoptab_curl_thread.hpp) та [devoptab_curl_thread.cpp](file:///d:/git/dev/sphaira/sphaira/source/utils/devoptab_curl_thread.cpp).
   - Перенесено структури `PushPullThreadData`, `PullThreadData`, `PushThreadData` та методи життєвого циклу потоку, черг push/pull та callback-функції curl.
3. **Створення файлу curl-пристрою**:
   - Створено [devoptab_curl_device.hpp](file:///d:/git/dev/sphaira/sphaira/include/utils/devoptab_curl_device.hpp) та [devoptab_curl_device.cpp](file:///d:/git/dev/sphaira/sphaira/source/utils/devoptab_curl_device.cpp).
   - Перенесено клас `MountCurlDevice` разом з ініціалізацією сесій curl, обробкою URL, декодуванням HTML та коллбеками запису/читання пам'яті.
   - Виправлено потенційну проблему циклічної залежності між заголовками: `devoptab_common.hpp` більше не імпортує `devoptab_curl_device.hpp` (оскільки базовий клас не має знати про своїх нащадків), що дозволило розірвати коло.
4. **Спрощення та очищення**:
   - Всі винесені класи та методи повністю видалено з [devoptab_common.hpp](file:///d:/git/dev/sphaira/sphaira/include/utils/devoptab_common.hpp) та [devoptab_common.cpp](file:///d:/git/dev/sphaira/sphaira/source/utils/devoptab_common.cpp).
5. **CMake та Версія**:
   - Додано `devoptab_buffered.cpp`, `devoptab_curl_thread.cpp` та `devoptab_curl_device.cpp` до [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).
   - Оновлено версію проекту до `0.13.186` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Проект успішно збирається під WSL за допомогою cmake, збірка бінарника `sphaira.nro` успішна.

## v0.13.183 — Декомпозиція класу App: винесення роботи з темами у app_theme.cpp (Крок 7.2) та фінальне очищення (Крок 7.3)


### Завдання
Виконати Крок 7.2 та 7.3 плану рефакторингу:
1. Перенести методи роботи з темами (від `GetThemeMetaList`, `SetTheme`, `GetThemeIndex`, `GetDefaultImage*`, `LoadTheme`, `ScanThemes`, `ScanThemeEntries` до допоміжних функцій `LoadElement*`, `CloseTheme`, `LoadThemeInternal`, `LoadThemeMeta`) та масив `THEME_ENTRIES` з файлу [app.cpp](file:///d:/git/dev/sphaira/sphaira/source/app.cpp) до нового файлу реалізації [app_theme.cpp](file:///d:/git/dev/sphaira/sphaira/source/app_theme.cpp).
2. Провести фінальну перевірку `app.cpp`, видалити залишки коду та забезпечити успішну збірку.

### Підхід
1. **Створення нового файлу**:
   - Створено [app_theme.cpp](file:///d:/git/dev/sphaira/sphaira/source/app_theme.cpp) і перенесено туди всі методи завантаження, сканування та застосування тем, а також допоміжний масив `THEME_ENTRIES` та структури `ThemeData`, `ThemeIdPair`.
   - Забезпечено коректний лінкінг та доступ до `g_app` та `DEFAULT_MUSIC_PATH` з `app.cpp`.
2. **Очищення `app.cpp`**:
   - Вилучено всі перенесені методи з файлу [app.cpp](file:///d:/git/dev/sphaira/sphaira/source/app.cpp).
   - Оголошено `DEFAULT_MUSIC_PATH` з зовнішнім зв'язуванням (external linkage) для можливості доступу з `app_theme.cpp`.
   - Створено та оголошено метод `InitDefaultImage()` в `App` для ініціалізації в конструкторі, що дозволило зберегти `DEFAULT_IMAGE_DATA` повністю приватним для `app_theme.cpp`.
3. **CMakeLists.txt та Версія**:
   - Додано `source/app_theme.cpp` до списку збірки у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).
   - Збільшено версію програми до `0.13.183` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Проект успішно збирається під WSL за допомогою cmake (бінарний файл `sphaira.nro` успішно лінкується та упаковується).


## v0.13.182 — Декомпозиція класу App: винесення налаштувань у app_settings.cpp (Крок 7.1)

### Завдання
Виконати Крок 7.1 плану рефакторингу: перенести Get/Set методи роботи з опціями (від `GetNxlinkEnable` до `ExitRestart` включно) та допоміжні функції анонімного простору імен (`IsKefirHubNacp`, `NormalizeWebdavUrl`, `GetNroIcon`) з файлу [app.cpp](file:///d:/git/dev/sphaira/sphaira/source/app.cpp) до нового файлу реалізації [app_settings.cpp](file:///d:/git/dev/sphaira/sphaira/source/app_settings.cpp). Очистити `app.cpp` від цих функцій та перевірити збірку.

### Підхід
1. **Створення нового файлу**:
   - Створено [app_settings.cpp](file:///d:/git/dev/sphaira/sphaira/source/app_settings.cpp) і перенесено туди всі методи Get/Set доступу до налаштувань класу `App`, а також локальні хелпери `IsKefirHubNacp`, `NormalizeWebdavUrl` та `GetNroIcon`.
   - Забезпечено коректний лінкінг через оголошення глобального вказівника `extern App* g_app;` та функцій `nxlink_callback`, `on_i18n_change`.
2. **Очищення `app.cpp`**:
   - Вилучено перенесені методи з файлу [app.cpp](file:///d:/git/dev/sphaira/sphaira/source/app.cpp).
   - Закрито анонімний простір імен раніше, щоб функції `nxlink_callback` та `on_i18n_change` були доступні для лінкінгу з `app_settings.cpp`.
3. **CMakeLists.txt та Версія**:
   - Додано `source/app_settings.cpp` до списку збірки у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).
   - Збільшено версію програми до `0.13.182` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Проект успішно збирається під WSL за допомогою cmake.

## v0.13.181 — Рефакторинг файлового браузера: винесення операцій FsView у filebrowser_ops.cpp (Крок 6.3)

### Завдання
Виконати Крок 6.3 плану рефакторингу: перенести "важкі" операції роботи з файловою системою (`InstallFiles`, `UnzipFiles`, `ZipFiles`, `UploadFiles`, `ShareFolder`, `OnDeleteCallback`, `OnPasteCallback`, `CheckIfUpdateFolder`, `IsReadOnly`, `AnySelectedReadOnly`) з класу `FsView` у файлі [filebrowser.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/filebrowser.cpp) в окремий файл реалізації [filebrowser_ops.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/filebrowser_ops.cpp). Очистити `filebrowser.cpp` від цих функцій та забезпечити успішну збірку.

### Підхід
1. **Перенесення методів**:
   - Створено файл [filebrowser_ops.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/filebrowser_ops.cpp) і перенесено туди важкі методи життєвого циклу файлових операцій `FsView` (`InstallFiles`, `UnzipFiles`, `ZipFiles`, `UploadFiles`, `ShareFolder`, `OnDeleteCallback`, `OnPasteCallback`, `CheckIfUpdateFolder`, `IsReadOnly`, `AnySelectedReadOnly`).
   - Додано необхідні заголовки (`#include "download.hpp"`, `ui/menus/filebrowser.hpp`, `ui/menus/filebrowser_assoc.hpp` тощо).
2. **Очищення `filebrowser.cpp`**:
   - Вилучено перенесені методи з файлу [filebrowser.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/filebrowser.cpp), залишивши в ньому лише методи рендерингу, навігації, сортування та базового оновлення.
3. **CMakeLists.txt та Версія**:
   - Додано `source/ui/menus/filebrowser_ops.cpp` до списку збірки у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).
   - Збільшено версію програми до `0.13.181` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Проект успішно збирається під WSL за допомогою cmake.
* Усі залежності та виклики методів лінкуються без помилок.

---

## v0.13.180 — Декомпозиція меню файлового браузера: створення форвардерів (Крок 6.2)

### Завдання
Виконати Крок 6.2 плану рефакторингу: виділити складний віджет бічного меню `ForwarderForm` (призначений для конфігурування та встановлення форвардерів NRO-файлів) з [filebrowser.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/filebrowser.cpp) в окремий модуль [filebrowser_forwarder.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/menus/filebrowser_forwarder.hpp) та [filebrowser_forwarder.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/filebrowser_forwarder.cpp).

### Підхід
1. **Створення файлів**:
   - Створено [filebrowser_forwarder.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/menus/filebrowser_forwarder.hpp) та [filebrowser_forwarder.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/filebrowser_forwarder.cpp).
   - Перенесено туди клас `ForwarderForm` (опис форми введення імені, автора, версії, іконки) та його внутрішній допоміжний метод `LoadNroMeta()`.
2. **Очищення `filebrowser.cpp`**:
   - Вилучено визначення `ForwarderForm` з [filebrowser.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/filebrowser.cpp), підключивши натомість новий заголовочний файл.
3. **CMakeLists.txt та Версія**:
   - Додано `source/ui/menus/filebrowser_forwarder.cpp` до [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).
   - Версію програми оновлено до `0.13.180`.

### Результати тестування
* Проект успішно скомпільовано під WSL.

---

## v0.13.179 — Декомпозиція меню файлового браузера (Крок 6.1)

### Проблема
У файлі `filebrowser.cpp` (~2622 рядки) було зосереджено велику кількість глобальних констант розширень файлів, конфігурацій ROM-баз, а також функцій асоціації файлів і перевірки підтримуваних емуляторів (RetroArch, NXMP тощо), що захаращувало UI-код меню.

### Підхід
1. **Створення хелперів типів файлів та ROM (Крок 6.1)**:
   - Створено нові файли [filebrowser_assoc.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/menus/filebrowser_assoc.hpp) та [filebrowser_assoc.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/filebrowser_assoc.cpp).
   - Туди перенесено структури `ExtDbEntry`, `RomDatabaseEntry`, масив `PATHS`, `DAYBREAK_PATH` та відповідні допоміжні функції: `IsSamePath`, `IsExtension`, `GetRomDatabaseFromPath`, `GetRomIcon`, `GetNxmpPath`, `HasNxmp`.
   - Всі константи та функції виділено у простір назв `sphaira::ui::menu::filebrowser::detail`.
2. **Очищення `filebrowser.cpp`**:
   - Підключено новий заголовочний файл `#include "ui/menus/filebrowser_assoc.hpp"` та додано `using namespace detail;` до простору назв меню.
   - Вилучено вищезгадані структури, масиви та функції з анонімного простору назв `filebrowser.cpp`, зменшивши розмір файлу на ~330 рядків.
3. **Оновлення системи збірки**:
   - Додано `source/ui/menus/filebrowser_assoc.cpp` до списку джерел у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).
   - Ітеровано версію програми до `0.13.179`.

### Результати тестування
* Проект успішно скомпільовано під WSL за допомогою `cmake --build build/ReleaseWithInstall`.

## v0.13.178 — Декомпозиція меню оновлення Kefir та прошивки (`kefir_menu.cpp`)

### Проблема
Файл `kefir_menu.cpp` був занадто великим (~2430 рядків) та містив велику кількість допоміжної логіку для парсингу списків змін (changelog), перевірки версій прошивок та встановлення системного ПЗ через `amssu`, що ускладнювало супровід та розвиток коду.

### Підхід
1. **Виділення логіки списку змін Kefir (Changelog) (Крок 5.1)**:
   - Створено [kefir_changelog.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/menus/kefir/kefir_changelog.hpp) та [kefir_changelog.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/kefir/kefir_changelog.cpp).
   - Перенесено структури `ChangelogTextColour`, `ChangelogSegment` та клас `KefirChangelogBox`.
   - Перенесено допоміжні функції парсингу маркдауну та рендерингу тексту списку змін (такі як `BuildKefirChangelogText`, `RenderChangelogText` тощо) у простір назв `sphaira::ui::menu::kefir::detail`.
2. **Виділення логіки валідації та встановлення прошивки (Firmware) (Крок 5.2)**:
   - Створено [kefir_firmware.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/menus/kefir/kefir_firmware.hpp) та [kefir_firmware.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/kefir/kefir_firmware.cpp).
   - Перенесено структуру `FirmwareValidation`, функції валідації й встановлення через `amssu` (`ValidateFirmware`, `InstallValidatedFirmware`), функції витягування версії з назв (`ExtractKefirVersion`, `IsVersionLower`) та інші допоміжні утиліти у простір назв `sphaira::ui::menu::kefir::detail`.
3. **Виділення спільного віджета HoldConfirmBox (Крок 5.3)**:
   - Створено [hold_confirm_box.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/hold_confirm_box.hpp) та [hold_confirm_box.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/hold_confirm_box.cpp).
   - Перенесено колишній віджет `DowngradeHoldConfirmBox` у global простір `sphaira::ui` під загальною назвою `HoldConfirmBox` для можливості його повторного використання іншими вікнами та меню.
   - Оновили виклики та підключення віджета у `kefir_menu.cpp`.
   - Додано новий файл віджета до `sphaira/CMakeLists.txt`.
4. **Повне очищення `kefir_menu.cpp`**:
   - Повністю вилучено мертві та дубльовані функції (всього близько ~630 рядків коду) з анонімного простору назв `kefir_menu.cpp`.
   - Переведено виклики `Trim`, `ExtractKefirVersion`, `MakeKefirLatestLabel` та інших хелперів на використання простору назв `detail::` з нових заголовних файлів.
   - Розмір файлу `kefir_menu.cpp` було успішно зменшено з 1704 до 902 рядків (орієнтовано на чистий код UI/Menu).
   - Прибрано зайвий порожній рядок в `CMakeLists.txt` та виправлено подвійне оголошення `FIRMWARE_ZIP` в `kefir_firmware.cpp`.
   - Скасовано випадкову зміну в `save_locations.hpp`.

### Результати тестування
* Проект успішно збирається під WSL без помилок лінкера та попереджень компілятора.
* Усі функції та віджети працюють ідентично до оригінальної реалізації.

## v0.13.177 — рефакторинг web.cpp (Фаза 3, Крок 3.3)

Виконано повне виділення UploadState та SocketStream з web.cpp згідно з вимогами Фази 3.

### 1. Виділення UploadState та SocketStream (Крок 3.3)
- Створено нові файли [web_upload.hpp](file:///d:/git/dev/sphaira/sphaira/include/web_upload.hpp) та [web_upload.cpp](file:///d:/git/dev/sphaira/sphaira/source/web_upload.cpp).
- Перенесено структуру `UploadState`, глобальний об'єкт `g_upload_state` та `SocketStream` (з його методом `ReadChunk`).
- `web.cpp` більше не містить коду для читання сокет-потоку та прямого управління станом завантажень. Його підключено до `web_upload.hpp`.
- Додано `source/web_upload.cpp` до списку джерел у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### 2. Версія
- Версію програми оновлено до `0.13.177` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

## v0.13.176 — рефакторинг save_menu.cpp (Фаза 4, Кроки 4.1 - 4.3)

Виконано декомпозицію та очищення файлу `save_menu.cpp` згідно з планом рефакторингу Фази 4.

### 1. Виділення допоміжних функцій шляхів та імен бекапів (Крок 4.1)
- Створено нові файли [save_paths.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/menus/save/save_paths.hpp) та [save_paths.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/save/save_paths.cpp).
- Усі функції побудови шляхів бекапів (включаючи `GetSaveFolder`, `GetSaveTypeSubdir`, `GetDbiTypeLetter`, `ParseDbiBackupNameTimestamp`, `ParseBackupNameTimestamp`, `GetSaveTypeLabel`, `SaveTypeIndex`, `SaveEntryKey`, `IsSystemLikeSave`, `DisplayEntryKey`, `BuildSaveName`, `BuildSavePathName`, `BuildSaveBasePathLegacy`, `BuildSaveBasePath`, `BuildDbiGameFolderName`, `BuildDbiSavePath`, `IsDbiBackupName`, `DbiBackupMatchesEntry`, `CollectDbiBackups`, `NormalizeBackupRoot`) було перенесено до цього нового модуля під простором назв `sphaira::ui::menu::save`.

### 2. Виділення функцій WebDAV та локацій (Крок 4.2)
- Створено нові файли [save_locations.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/menus/save/save_locations.hpp) та [save_locations.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/save/save_locations.cpp).
- Усі функції роботи з WebDAV, локаціями та прогресом синхронізації (включаючи `WebdavLocationKey`, `GetWebdavLocations`, `MakeSdCardDumpLocation`, `MakeDumpLocationFromFsEntry`, `MakeLocationLabel`, `MakeSdLocationLabel`, `SerializeRecentBackupDir`, `MakeLocationKey`, `ParseRecentBackupDir`, `RecentBackupDirExists`, `MakeDumpLocationFromRecent`, `MakeFsForLocation`, `MakeAggregateProgressCb`) перенесено до цього модуля.

### 3. Очищення save_menu.cpp та оновлення CMakeLists.txt (Крок 4.3)
- Вилучено всі перенесені функції з [save_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/save_menu.cpp).
- Підключено нові заголовні файли `ui/menus/save/save_paths.hpp` та `ui/menus/save/save_locations.hpp` у `save_menu.cpp`.
- Додано нові файли `source/ui/menus/save/save_paths.cpp` та `source/ui/menus/save/save_locations.cpp` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### 4. Версія
- Версію програми оновлено до `0.13.176` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

## v0.13.175 — рефакторинг web.cpp (Фаза 3, Кроки 3.1 - 3.3)

Виконано декомпозицію та очищення файлу `web.cpp` згідно з планом рефакторингу Фази 3.

### 1. Виділення HTML/JS констант (Крок 3.1)
- Усі довгі HTML/JS константи сторінок (`LIGHTBOX_CONTENT`, `CONFIRM_MODAL_HTML`, `CONFIRM_MODAL_JS`, `FOLDER_PAGE_HEADER`, `FOLDER_PAGE_JS`, `PROGRESS_PAGE`) перенесено в новий заголовочний файл [web_pages.hpp](file:///d:/git/dev/sphaira/sphaira/source/web_pages.hpp) під простором назв `sphaira::webpages`.
- Оновлено [web.cpp](file:///d:/git/dev/sphaira/sphaira/source/web.cpp) для підключення `web_pages.hpp` та імпорту простору назв `sphaira::webpages` за допомогою `using namespace webpages;`. Це зменшило кількість рядків у `web.cpp` приблизно на 1450 рядків.

### 2. Виділення класу QrCode (Крок 3.2)
- Виділено клас `QrCode` в окремі файли [web_qr.hpp](file:///d:/git/dev/sphaira/sphaira/source/web_qr.hpp) та [web_qr.cpp](file:///d:/git/dev/sphaira/sphaira/source/web_qr.cpp).
- Клас `QrCode` більше не засмічує `web.cpp`, його опис повністю вилучено з основного файлу веб-сервера.
- Додано `source/web_qr.cpp` до списку джерел у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### 3. Аналіз UploadState та SocketStream (Крок 3.3)
- Відповідно до інструкцій плану рефакторингу, `UploadState` та `SocketStream` було залишено в [web.cpp](file:///d:/git/dev/sphaira/sphaira/source/web.cpp) через їхній тісний зв'язок з циклом сервера та внутрішнім станом глобальних змінних. Їх виділення призвело б до ускладнення архітектури через необхідність створення додаткових інтерфейсів доступу до глобальних змінних стану (`g_share_running`, `WebGetProgressBox()`, `g_upload_state`).

### 4. Версія
- Версію програми оновлено до `0.13.175` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

## v0.13.174 — виправлення помилок підключення заголовків у settings_menu.hpp (Крок 2.4)

Виправлено помилки компіляції, спричинені некоректним підключенням заголовка та вилученням forward-declaration.

### 1. Переміщення include в settings_menu.hpp
- Підключення `#include "ui/menus/settings/settings_tweaks.hpp"` переміщено нагору файлу, за межі простору імен `sphaira::ui::menu::settings`, для усунення подвійної вкладеності просторів імен (namespace pollution) та забезпечення коректної видимості `detail::` у TU файлу `settings_menu.cpp`.

### 2. Відновлення forward-declaration
- Відновлено декларацію `struct FanCurveSensorReader;` перед оголошенням `struct FanCurveMenu`, оскільки цей тип необхідний для поля `m_sensor_reader` і визначається локально у `settings_menu.cpp`.

### 3. Версія
- Версію програми в `sphaira/CMakeLists.txt` оновлено до `0.13.174`.

## v0.13.173 — фінальне очищення settings_menu.cpp (Крок 2.4)

Завершено Phase 2 рефакторингу `settings_menu.cpp`. Виконано повне очищення файлу `settings_menu.cpp` та перекладено виклики системних функцій на виділений простір імен `detail::` з `settings_tweaks.hpp` та `settings_translations.hpp`.

### 1. Видалення дублікатів та очищення settings_menu.hpp
- Вилучено дубльовані структури `FanCurvePoint` та `FanCurveApplyMode` із заголовного файлу `settings_menu.hpp` і натомість підключено виділений заголовок `ui/menus/settings/settings_tweaks.hpp`.

### 2. Очищення settings_menu.cpp
- Замінено локальні виклики функцій `ApplyFanCurves`, `RebootAfterSetting`, `IsSphairaFanSysmoduleRunning`, `IsSphairaFanSysmoduleInstalled`, `RestartSphairaFanSysmodule`, `ReadFanCurve`, `DefaultHandheldFanCurve`, `DefaultDockedFanCurve`, `FAN_BUILTIN_PRESET_COUNT`, `FanPresetCurve`, `ReadCustomFanPreset`, `ReadCustomFanPresetName`, `SanitizeFanPresetName`, `FanCustomPresetDefaultName`, `SaveCustomFanPreset`, `FAN_TEMP_MAX_C`, `FAN_TEMP_MIN_C` та `NormalizeFanCurve` на явні виклики через простір імен `detail::` (з винесених раніше файлів `settings_tweaks.hpp/.cpp` та `settings_translations.hpp/.cpp`).
- Обсяг `settings_menu.cpp` скоротився, залишаючи в ньому лише класи меню та інтерфейсну логіку (UI wiring), що відповідає вимогам Phase 2.

### 3. Версія
- Версію програми в `sphaira/CMakeLists.txt` оновлено до `0.13.173`.

## v0.13.172 — рефакторинг settings_menu.cpp (Крок 2.3)

Завершено рефакторинг `settings_menu.cpp` шляхом виділення системних налаштувань, структур `KefirSetting`, `PackageAction` та допоміжних функцій роботи з кривими вентилятора в окремий компонент `settings_tweaks.hpp/.cpp` у просторі імен `sphaira::ui::menu::settings`.

### 1. Виділення settings/settings_tweaks
- Створено заголовний файл `sphaira/include/ui/menus/settings/settings_tweaks.hpp`, який містить структури `KefirSetting`, `PackageAction` та декларації перенесених функцій системних налаштувань і вентилятора у просторі імен `detail` та пов'язані константи.
- Створено файл реалізації `sphaira/source/ui/menus/settings/settings_tweaks.cpp`, куди перенесені визначення наступних функцій: `IsEmummcEnabled`, `ApplyOverclock`, `Apply40Mb`, `ApplyRedirectSaves`, `Apply8GbDram`, допоміжні функції кривих вентилятора (`DefaultHandheldFanCurve`, `DefaultDockedFanCurve`, `QuietFanCurve`, `BalancedFanCurve`, `CoolFanCurve`, `FullSpeedFanCurve`, `FanOffCurve`), хелпери пресетів та роботи з рядками Atmosphere (`FanPresetSection`, `FanCustomPresetKey`, `FanCustomPresetNameKey`, `FanCustomPresetDefaultName`, `SanitizeFanPresetName`, `FanBuiltinPresetLabels`, `FanPresetCurve`, `FanByteToPercent`, `FanPercentToByte`, `DecodeAtmosphereString`, `ParseSignedIntegers`, `NormalizeFanCurve`, `ParseFanCurveTable`, `ReadFanCurve`, `ReadCustomFanPreset`, `ReadCustomFanPresetName`, `FanCustomPresetLabel`, `FanPresetLabels`, `FanCustomPresetLabels`, `FormatFanCurveTable`, `FormatAtmosphereFanCurve`, `IsFanCurveEnabled`, `IsSphairaFanSysmoduleInstalled`, `IsSphairaFanSysmoduleRunning`, `EnsureSphairaFanSysmoduleInstalled`, `RestartSphairaFanSysmodule`, `ApplyFanCurves`, `SaveCustomFanPreset`).

### 2. Очищення settings_menu.cpp
- Додано підключення `#include "ui/menus/settings/settings_tweaks.hpp"` у `settings_menu.cpp`.
- Вилучено структури `KefirSetting`, `PackageAction`, функції системних налаштувань та вентилятора з `settings_menu.cpp`.

### 3. Складальна система та версія
- Додано `source/ui/menus/settings/settings_tweaks.cpp` до списку джерел у `sphaira/CMakeLists.txt`.
- Версію програми оновлено до `0.13.172`.

## v0.13.171 — рефакторинг settings_menu.cpp (Крок 2.2)

Завершено рефакторинг `settings_menu.cpp` шляхом виділення структур та функцій роботи з перекладами інтерфейсу та DBI в окремий компонент `settings_translations.hpp/.cpp` у просторі імен `sphaira::ui::menu::settings`.

### 1. Виділення settings/settings_translations
- Створено заголовний файл `sphaira/include/ui/menus/settings/settings_translations.hpp`, який містить структури `DbiTranslationEntry` та `InterfaceTranslationEntry`, а також декларації перенесених функцій у просторі імен `detail` та константи шляхів пакетів.
- Створено файл реалізації `sphaira/source/ui/menus/settings/settings_translations.cpp`, куди перенесені визначення наступних функцій: `DownloadFile`, `UnzipFile`, `ParseDbiTranslations`, `ParseInterfaceTranslations`, `ReadInterfaceReplacementOptions`, `FileNameFromUrl`, `TranslationExtractFolder`, `InstallDbiTranslation`, `InstallInterfaceTranslation`, `RemoveInterfaceTranslation` (для приховування `TRANSLATION_PATHS` у реалізації), а також функція `RebootAfterSetting` (яку перенесено до `detail` простору імен для спільного використання).

### 2. Очищення settings_menu.cpp
- Додано підключення `#include "ui/menus/settings/settings_translations.hpp"` у `settings_menu.cpp`.
- Вилучено дубльовані структури та функції перекладу, а також локальні константи `TRANSLATION_PATHS`, `SPHAIRA_DOWNLOADS` тощо.
- Прямий цикл видалення перекладів замінено на виклик `RemoveInterfaceTranslation(pbox)`.

### 3. Складальна система та версія
- Додано `source/ui/menus/settings/settings_translations.cpp` до списку джерел у `sphaira/CMakeLists.txt`.
- Версію програми оновлено до `0.13.171`.

## v0.13.170 — рефакторинг settings_menu.cpp (Крок 2.1)

Завершено рефакторинг `settings_menu.cpp` шляхом виділення загальних допоміжних функцій для роботи з файлами та рядками в окремий компонент `settings_fs_utils.hpp/.cpp` у просторі імен `sphaira::ui::menu::settings::detail`.

### 1. Виділення settings/settings_fs_utils
- Створено заголовний файл `sphaira/include/ui/menus/settings/settings_fs_utils.hpp`, який містить декларації перенесених утиліт у просторі імен `sphaira::ui::menu::settings::detail`.
- Створено файл реалізації `sphaira/source/ui/menus/settings/settings_fs_utils.cpp`, куди перенесені визначення наступних функцій: `Trim`, `ReadTextFile`, `ReadLines`, `WriteLines`, `StartsWith`, `SplitCommand`, `ExtractBracketName`, `ExtractIniKey`, `ExtractJsonStringField`, `FileExists`, `DirectoryExists`, `ParentPath`, `EnsureParentDirectory`, `CopyFileSimple`, `DeletePath`, `CopyDirectoryContents`, `MovePath`, `IniValueEquals`, `SetIniValue`, `ReadIniRawValue`, `SetIniRawValue`.

### 2. Очищення settings_menu.cpp
- Додано підключення `#include "ui/menus/settings/settings_fs_utils.hpp"` та директиву `using namespace detail;` у `settings_menu.cpp` для доступу до винесених функцій.
- Вилучено всі вищезазначені утиліти з `settings_menu.cpp` (функція `SettingsValueColour` була залишена в `settings_menu.cpp`, оскільки вона не входила до списку перенесення).

### 3. Складальна система та версія
- Додано `source/ui/menus/settings/settings_fs_utils.cpp` до списку джерел у `sphaira/CMakeLists.txt`.
- Версію програми оновлено до `0.13.170`.

## v0.13.169 — рефакторинг cheats_menu.cpp (Крок 1.5 та 1.6)

Завершено рефакторинг та оптимізацію `cheats_menu.cpp` шляхом виділення класів вибору та завантаження читів, а також очищення залишків коду.

### 1. Виділення cheats/cheat_game_select_menu
- Створено заголовний файл `sphaira/include/ui/menus/cheats/cheat_game_select_menu.hpp`, який містить оголошення класів `CheatGameSelectMenu` та `CheatDownloadMenu`, а також загальних допоміжних функцій у просторі імен `sphaira::ui::menu::hats::detail`.
- Створено файл реалізації `sphaira/source/ui/menus/cheats/cheat_game_select_menu.cpp`, куди перенесені визначення вищезазначених класів.
- Допоміжні функції `GetBuildIdFailureMessage`, `WritePayloadLaunchConfig` та `ShowProdKeysMissingDialog` перенесені до анонімного простору імен у `cheat_game_select_menu.cpp`, оскільки вони використовуються виключно цими меню.
- Перенесено функції `CleanCheatContent`, `ParseCheatslipsCheats`, `ParseNxDbCheats`, `ExtractNxDbBuildIds`, `IsParenthesizedNoteLine`, `StripInlineCheatComment`, `IsHexCodeLine`, `NormalizeHexCodeLine`, `SanitizeCheatContentForAtmosphere`, `SanitizeManualCheatContent`, `ImportManualCheatFile` та `WriteCheatFile` у простір імен `detail` у `cheat_game_select_menu.cpp`.

### 2. Очищення cheats_menu.cpp
- Змінено анонімний простір імен у `cheats_menu.cpp` на іменований простір `sphaira::ui::menu::hats::detail` для надання доступу до спільних функцій (таких як `GetTitleVersion`, `GetTitleName`, `AppendGameCardGames`, `EnumerateInstalledGames`, `GetCheatslipsToken`).
- Вилучено винесені класи `CheatGameSelectMenu`, `CheatDownloadMenu` та всі супутні хелпери з `cheats_menu.cpp` (включаючи `GetBuildIdFailureMessage`, `WritePayloadLaunchConfig` та `ShowProdKeysMissingDialog`), значно скоротивши його обсяг (тепер містить лише ~1305 рядків коду, що повністю відповідає цілі "менше 1500 рядків").
- Додано підключення `#include "ui/menus/cheats/cheat_game_select_menu.hpp"` у `cheats_menu.cpp`.

### 3. Налаштування збірки
- Новий файл `source/ui/menus/cheats/cheat_game_select_menu.cpp` додано до списку джерел у `sphaira/CMakeLists.txt`.
- Додано прямий include `<unordered_set>` у `cheat_game_select_menu.hpp` для усунення залежності від транзитивних підключень.
- Версію програми оновлено до `0.13.169`.

## v0.13.168 — рефакторинг cheats_menu.cpp (Крок 1.4)

Завершено перенесення класів `CheatFilesMenu`, `CheatContentMenu` та `CheatCodeViewerMenu` з `cheats_menu.cpp` до окремих файлів `cheat_files_menu.hpp/.cpp`.

### 1. Виділення cheats/cheat_files_menu
- Створено заголовний файл `sphaira/include/ui/menus/cheats/cheat_files_menu.hpp`, який містить оголошення класів `CheatFilesMenu`, `CheatContentMenu`, `CheatCodeViewerMenu` та декларації спільних допоміжних функцій у просторі імен `sphaira::ui::menu::hats::detail`.
- Створено файл реалізації `sphaira/source/ui/menus/cheats/cheat_files_menu.cpp`, куди перенесені визначення вищезазначених класів.
- Перенесено функції `GetCheatsDirPath`, `GetFileStem`, `GetManualCheatImportPath`, `IsCheatHeaderLine`, `GetCheatHeaderName`, `GetExistingCheats`, `DeleteCheatFile` у простір імен `detail` для спільного використання між модулями компіляції.
- Функцію `RenameCheatBuildId` зроблено локальною в анонімному просторі імен у `cheat_files_menu.cpp`.

### 2. Очищення cheats_menu.cpp та cheats_menu.hpp
- Вилучено визначення та оголошення класів `CheatFilesMenu`, `CheatContentMenu` та `CheatCodeViewerMenu` з `cheats_menu.cpp` та `cheats_menu.hpp`.
- Додано підключення `#include "ui/menus/cheats/cheat_files_menu.hpp"` у `cheats_menu.cpp`.

### 3. Збірка програми
- Додано `source/ui/menus/cheats/cheat_files_menu.cpp` до `sphaira/CMakeLists.txt`.
- Успішно проведено збірку програми в середовищі WSL. Версію оновлено до `0.13.168`.

## v0.13.167 — рефакторинг cheats_menu.cpp (Крок 1.3)

Завершено перенесення функцій роботи з кешем метаданих читів та базою даних версій `nx-cheats-db` з `cheats_menu.cpp` до `cheats_db.hpp/.cpp`.

### 1. Виділення cheats_db
- Створено `sphaira/source/ui/menus/cheats/cheats_db.cpp` (відповідний заголовок `cheats_db.hpp` вже існував).
- Перенесено глобальний м'ютекс `g_cheat_metadata_cache_mutex`.
- Перенесено функції `LoadCheatMetadataCacheUnlocked`, `SaveCheatMetadataCacheUnlocked`, `GetCachedCheatMetadata`, `GetCachedBuildIdForTitle`, `SaveDetectedBuildIdToCache`, `LoadNxDbVersions`, `IsNxDbAvailable` та видалено дублікати структур `CachedCheatMetadata` і `NxDbVersionInfo` з `cheats_menu.cpp`.
- Вилучено цей код із `cheats_menu.cpp`.

### 2. Оновлення конфігурації збірки
- Додано `source/ui/menus/cheats/cheats_db.cpp` до `sphaira/CMakeLists.txt`.
- Версію програми оновлено до `0.13.167`.

## v0.13.166 — рефакторинг cheats_menu.cpp (Крок 1.2)

Завершено перенесення функцій пошуку Build ID та NCA з `cheats_menu.cpp` до `cheats_lookup.hpp/.cpp`.

### 1. Перенесення функцій пошуку Build ID та NCA
- Перенесено структури `InstalledNcaLookupResult` та `BuildIdLookupResult` разом із супутніми перерахуваннями помилок до `cheats_lookup.hpp`.
- Перенесено функції `GetBuildIdFromInstalledNcaDetailed`, `GetBuildIdFromInstalledNca`, `ReadBuildIdFromProgramContent`, `GetBuildIdFromGameCardNca`, `GetBuildIdFromNso`, `HasProdKeys`, `HasApplicationContentMeta` та `LookupBuildIdForCheats` до `cheats_lookup.cpp`.
- Вилучено ці функції з `cheats_menu.cpp`.

### 2. Інтеграція та сумісність
- Додано `using namespace detail;` у `sphaira::ui::menu::hats` для прозорого доступу до винесених допоміжних функцій без редагування викликів у `cheats_menu.cpp`.

## v0.13.165 — рефакторинг cheats_menu.cpp (Крок 1.1)

Виділено допоміжні структури та функції роботи з `dmnt:cht` з великого файлу `cheats_menu.cpp` в окремі модулі компіляції.

### 1. Виділення cheats_dmnt
- Створено `sphaira/include/ui/menus/cheats/cheats_dmnt.hpp` та `sphaira/source/ui/menus/cheats/cheats_dmnt.cpp`.
- Перенесено структури `DmntMemoryRegionExtents` та `DmntCheatProcessMetadata`, а також функцію `GetBuildIdFromDmnt`.
- Вилучено цей код із `cheats_menu.cpp`.

### 2. Створення cheats_lookup (забезпечення цілісності збірки)
- Створено `sphaira/include/ui/menus/cheats/cheats_lookup.hpp` та `sphaira/source/ui/menus/cheats/cheats_lookup.cpp`.
- Перенесено допоміжні функції роздільної здатності build-id (`FormatTitleId`, `NormalizeBuildId`, `IsValidBuildId`, `BytesToBuildId` тощо) у простір імен `sphaira::ui::menu::hats::detail` для спільного використання між `cheats_dmnt.cpp` та `cheats_menu.cpp`.

### 3. Оновлення конфігурації збірки
- Додано нові `.cpp` файли до `sphaira/CMakeLists.txt`.
- Версію програми оновлено до `0.13.165`.

## v0.13.165 — рев'ю v0.13.164: 8 виправлень (регресія, фриз UI, недетермінізм, дублювання)

Багатоагентне рев'ю власного диффу v0.13.164 (пикер бекапів). 8 підтверджених знахідок, усі виправлено.

### Суттєві

1. **Регресія: бекапи з нерозпізнаваним іменем (ts==0) стали невідновлюваними.** Старий `FindLatestBackupPath` приймав будь-який кандидат, новий `offer()` у `CollectBackups` відкидав `ts==0`. Повернуто: такі архіви знову збираються (сортуються в кінець списку — вони не мають дати для порівняння), у пикері показуються під власною назвою файлу замість фейкової дати `0000.00.00`.
2. **`CollectBackups` блокував UI-потік у `ShowRestorePicker`.** Сканування тек + відкриття кожного dbi-zip (Account/Cache) виконувалось синхронно при відкритті пикера. Тепер `ShowRestorePicker` пушить короткий `ProgressBox`-воркер (як і решта backup/restore-сканів), результат передається у новий `ShowRestorePickerPopup()` через done-колбек (UI-потік).
3. **Втрачено детермінований tie-break для однакових timestamp.** Старий код при рівних `ts` завжди обирав dbi-архів (`source` пріоритет). Новий `std::ranges::sort` за самим лише `ts` — нестабільний, вибір міг «плавати». Повернуто `source` (dbi=0, sphaira new/legacy=1..4) як другий ключ сортування — вибір знову детермінований.

### UX / i18n

4. **OptionBox «No WebDAV…» одразу перекривався пикером.** При `Include remote backups` без налаштованого WebDAV попередження пушилось і в ту саму мить ховалось під пикером. Прибрано зайвий OptionBox — тепер тихий fallback на локальний пикер (попередження й так є в `Save Options → Sync with remote`).
5. **Тип сейву в рядку пикера — неперекладений і завжди однаковий.** `GetSaveTypeLabel()` без `i18n::get` + однаковий для всіх кандидатів одного сейву (не розрізняє нічого). Суфікс прибрано повністю.

### Дублювання (reuse)

6. **`MakeFsForLocation()`** — новий спільний хелпер замінив 4 копії вибору `FsStdio`/`FsNativeSd` за `DumpLocation` (дві нові копії мали небезпечний bare-`else`, дві старі — `std::unreachable()`; тепер одна поведінка всюди).
7. **`DownloadOneBackupFile()`** — новий спільний хелпер замінив ~30 рядків дослівного дублювання download-фази (dbi-шлях, temp+rename, curl) між `DownloadRemoteBackupsForEntry` та `SyncSavesRemoteWithLocation`.
8. Прибрано зайвий паралельний вектор `paths` у `ShowRestorePickerPopup` — колбек пикера індексує `candidates` напряму.

### Ключові файли

* `sphaira/source/ui/menus/save_menu.cpp` — `CollectBackups` (source tie-break, ts==0), `MakeFsForLocation`, `DownloadOneBackupFile`, `ShowRestorePicker`/`ShowRestorePickerPopup` (worker+popup split), `StartRestore` (без зайвого OptionBox).
* `sphaira/include/ui/menus/save_menu.hpp` — `BackupCandidate{ts, path, source}` (прибрано мертві `save_data_type`/`remote`), декларація `ShowRestorePickerPopup`.
* `sphaira/CMakeLists.txt` — версія `0.13.165`.

### Перевірено рев'ю без зауважень

Потокобезпека `ProgressBox::m_hide_speed` (atomic, безпечно навіть у вікні перед першим записом воркера); коректність вкладених лямбд у `StartRestore` (без use-after-move); незачепленість наявних користувачів `PopupList` (стара геометрія байт-у-байт при `menu_style=false`); i18n/версія/walkthrough-конвенції дотримані.

## v0.13.164 — вибір бекапу при відновленні + віддалені бекапи з хмарною іконкою

Нова фіча за запитом користувача. Раніше відновлення завжди тихо брало найновіший бекап — вибрати конкретний (не останній) було неможливо.

### 1. Пикер бекапів при відновленні

`FindLatestBackupPath` узагальнено до `CollectBackups()` — збирає **всі** відновлювані архіви для сейву (DBI-формат + sphaira new/legacy, усі локації), відсортовані від найновішого, з дедуплікацією за повним шляхом. `FindLatestBackupPath` тепер тонка обгортка (бере `front()`).

Потік Restore переписано: `Start Restore` → `StartRestore()`:
- **Мультивибір** (>1 сейв) — стара поведінка: найновіша копія кожного (без N діалогів).
- **Один сейв** — `ShowRestorePicker()`: 0 копій → «бекапів не знайдено»; 1 копія → відновити одразу; >1 → `PopupList` зі списком (дата+час, тип сейву), вибір веде у `RestoreSavesPicked()` — той самий цикл авто-бекапу+`RestoreSaveInternal`, але з явно обраним архівом.

### 2. Формат — без змін (уже коректно)

Ігрові сейви (Account/Device/Cache/BCAT) вже пишуться **лише** у DBI-форматі й читаються DBI. Sphaira-формат лишається тільки для системних сейвів (System/SystemBcat), бо DBI-ім'я будується навколо Title ID, якого системні сейви не мають — DBI їх не бекапить у принципі. Другий формат читається ще й для відновлення старих бекапів. Тут нічого не змінювалось.

### 3. Віддалені бекапи + хмарна іконка

У `Restore Options` додано галку **`Include remote backups`** (`m_restore_include_remote`, persistent). Коли ввімкнено й відновлюється один сейв: перед показом пикера виконується **download-only** синхронізація (`DownloadRemoteBackupsForEntry`) — з WebDAV стягуються лише архіви, яких нема локально (нічого не вивантажується), у ту саму локацію відновлення (dbi-іменовані → у dbi-розкладку, решта → у sphaira-теку), тож `CollectBackups` їх бачить. Щойно завантажені архіви позначаються у пикері **іконкою хмари** зліва (у зарезервованому лівому жолобі, щоб не зміщати текст рядка). При помилці синку пикер усе одно показується з локальних.

### 4. Прибрано хибну «галочку» з Save Action

`PopupList` отримав `SetMenuStyle(true)` — для меню-навігації (Backup/Restore у «Save Action») замість «поточне значення» (tick ``) малюється **шеврон вправо** ▸ на кожному рядку (натяк, що пункт веде далі). Звичайні chooser-и (Location, Sync Location) не змінилися.

### Ключові файли

* `sphaira/include/ui/popup_list.hpp`, `sphaira/source/ui/popup_list.cpp` — `SetMenuStyle` (шеврон), `SetRemoteMarkers` (хмара у лівому жолобі); векторні `DrawCloudIcon`/`DrawChevron` (без залежності від шрифту).
* `sphaira/source/ui/menus/save_menu.cpp` — `CollectBackups`, `StartRestore`, `ShowRestorePicker`, `RestoreSavesPicked`, `DownloadRemoteBackupsForEntry`; галка `Include remote backups`; `Save Action` у menu-style.
* `sphaira/include/ui/menus/save_menu.hpp` — `BackupCandidate`, `m_restore_include_remote`, нові декларації.
* `assets/romfs/i18n/en.json`, `uk.json` — `Include remote backups` + tooltip, `Select backup`, `No backups found for selected saves.`.
* `sphaira/CMakeLists.txt` — версія `0.13.164`.

### Ручна перевірка (Агент 1)

1. Один сейв, кілька бекапів → `Restore` показує список дат; вибір старішого відновлює саме його (не останній).
2. Один сейв, один бекап → відновлюється без пикера. Жодного → «бекапів не знайдено».
3. Мультивибір кількох ігор → без пикера, найновіша копія кожної (як раніше).
4. `Include remote backups` увімкнено, є WebDAV з архівом, якого нема локально → перед списком тягнеться download, рядок має іконку хмари; вибір відновлює.
5. `Save Action` (Backup/Restore) показує шеврон ▸, а не галочку.

### Відомі межі

* Пикер збирає бекапи на UI-потоці (швидко для одного сейву; для Account/Cache відкриває zip'и, щоб звірити uid/index).
* Віддалене відновлення розраховане на локацію відновлення = та сама, куди тягнуться завантаження (типово SD).

## v0.13.163 — виправлення за аудитом: переклади Location/Backup, валідація зниклих папок, коректна швидкість sync

Виправлено сім пунктів статичного аудиту v0.13.154–162 (задача id 52 та суміжна sync-логіка).

### 1. Незавершений переклад інтерфейсу Backup/Restore (uk.json) — суттєве

Крок 3 задачі id 52 вимагав перекласти всі видимі підписи блоку локації, але низка ключів була відсутня в `uk.json` і показувалась англійською. Додано: `Backup Options`, `Restore Options`, `Start Backup`, `Start Restore`, `LOCATION`, `Location`, `Choose Folder...`, `ACCOUNTS`, `SAVE TYPES`, `Select Sync Location`, `No matching saves found.` і три tooltip-описи (Location + Start Backup/Restore).

### 2. Зниклі/переміщені папки історії лишались у списку Location — суттєве

`GetRecentBackupDirs()` лише парсив ini-записи й повертав навіть ті папки, яких уже немає (видалена тека) або чий носій відключено (USB/HDD). Мертвий пункт «спрацьовував» помилкою вже під час операції. Додано хелпер `RecentBackupDirExists()`: для SD перевіряє `fs::DirExists(path)`, для stdio — `fs::DirExists(mount + path)` (відключений носій не має devoptab → `stat` падає → пункт відсіюється). Історія в конфізі **не** видаляється — носій, що повернувся, знову з'явиться при наступному відкритті.

### 3. Текст швидкості під час sync показував синтетичні одиниці як MiB/s — суттєве

Прогрес-бар sync живиться синтетичним бюджетом (`SYNC_PROGRESS_SCALE` = 1 млн/файл), а `ProgressBox::Draw` рахував «швидкість» із дельт offset і підписував `MiB/s` — виходили неправдиві числа. Додано прапорець `ProgressBox::SetHideSpeed(true)` (встановлюється на обох sync-боксах). При ньому рядок швидкості приховується, але **ETA лишається** (одиниці-залишку / одиниці-за-секунду = справжні секунди, тож час коректний).

### 4. Дедуплікація історії тепер справді за нормалізованим шляхом — дрібне

Walkthrough v0.13.155 стверджував «дедуплікація за нормалізованим шляхом (ім'я ігнорується)», але `AddRecentBackupDir` порівнював повну серіалізацію разом з іменем. Тепер порівняння через `MakeLocationKey` (ім'я ігнорується) — заява й код узгоджені.

### 5. Повторний вибір тієї самої папки не дублює рядок у сесії — дрібне

Filepicker-callback безумовно додавав новий рядок. Тепер `ActionState` тримає паралельний `location_keys`; якщо вибрана папка вже є у списку (default, історія, mount або попередній вибір цієї сесії) — просто виділяється наявний пункт замість дубля.

### 6. `OnFocusLost` скидає й скрол значення — косметика

`SidebarEntryBase::OnFocusLost()` скидав лише `m_scolling_title`; додано `m_scolling_value.Reset()`, щоб довге значення після повторного фокусу скролилось з початку.

### 7. Прибрано осиротілий ключ `"Downloading: "` — прибирання

Після переробки фаз sync ключ ніде не вживався; видалено з en/uk.

### Ключові файли

* `sphaira/source/ui/menus/save_menu.cpp` — `RecentBackupDirExists`, дедуп за `MakeLocationKey`, `location_keys` у `ActionState` + перевірка в picker-callback, `SetHideSpeed(true)` на sync-боксах.
* `sphaira/source/ui/progress_box.cpp`, `sphaira/include/ui/progress_box.hpp` — прапорець `m_hide_speed`/`SetHideSpeed`; приховування рядка швидкості зі збереженням ETA.
* `sphaira/source/ui/sidebar.cpp` — `m_scolling_value.Reset()` у `OnFocusLost`.
* `assets/romfs/i18n/uk.json` — 16 нових ключів; `assets/romfs/i18n/en.json`, `uk.json` — видалено `Downloading: `.
* `sphaira/CMakeLists.txt` — версія `0.13.163`.

## v0.13.162 — стабільний прогрес-бар авто-синку; прибрано підтверджувальне віконце Sync

### 1. Прогрес-бар авто-синку стрибав туди-сюди на межах файлів

Дві причини (обидві в `save_menu.cpp`):

* **`NewTransfer()` на кожен файл.** Авто-синк викликав `pbox->NewTransfer("Uploading: <файл>")` для кожного файлу, а `NewTransfer` скидає offset/size бару в 0 — бар падав у нуль і знову наздоганяв. Тепер `NewTransfer("Local → WebDAV")` викликається один раз перед циклом, а ім'я поточного файлу показується через `SetActionName()` (заголовок), який прогрес не чіпає.
* **curl-колбек читав «не той» напрямок.** `MakeAggregateProgressCb` брав `ultotal`, якщо він ненульовий, інакше `dltotal`. Після завершення тіла upload curl продовжує кликати колбек для фази відповіді сервера, де ul-лічильники обнуляються, а dl-лічильники крихітні — частка файлу миттєво падала з ~100% до ~0% і бар смикався назад на кожній межі файлу. Тепер напрямок передається явно (`is_upload`), протилежні лічильники ігноруються, частка файлу обмежена 100%, а показане значення зроблено монотонним (`max`) — бар фізично не може рухатись назад у межах батчу.

Обидва виправлення однаково діють і на авто-синк, і на ручний `Sync with remote` (спільний хелпер).

### 2. Прибрано підтверджувальне віконце перед Sync with remote

Користувач: віконце дублює tooltip і при повторних синхронізаціях лише заважає; замість чекбокса «не показувати знову» його прибрано повністю. Повний текст пояснення (двобічний перенос усіх відсутніх ZIP, локальні → WebDAV, хмарні → SD, однакові імена не перезаписуються й не порівнюються, після завантаження — Restore) об'єднано з попереднім коротким tooltip і тепер живе тільки в tooltip пункту `Sync with remote` у `Save Options` (лівий info-блок при фокусі). `SyncSavesRemote()` після перевірок одразу переходить до вибору WebDAV-локації (або старту, якщо локація одна). Ключі колишнього віконця (`Start sync?`, кнопка `Sync`) видалено з en/uk.

### Ручна перевірка

1. Backup з увімкненим авто-синком кількох сейвів: бар росте плавно, на межах файлів не провалюється і не смикається назад.
2. `Save Options → Sync with remote`: синхронізація стартує одразу (через вибір локації, якщо їх декілька) — без проміжного інформаційного віконця; повний опис видно в tooltip зліва при фокусі на пункті.
3. Ручний sync з upload+download: обидві фази так само плавні.

### Ключові файли

* `sphaira/source/ui/menus/save_menu.cpp` — `MakeAggregateProgressCb` (напрямок + монотонність), авто-синк без per-file `NewTransfer`, `SyncSavesRemote()` без OptionBox, об'єднаний tooltip.
* `assets/romfs/i18n/en.json`, `assets/romfs/i18n/uk.json` — об'єднаний tooltip-ключ, видалені ключі віконця.
* `sphaira/CMakeLists.txt` — версія `0.13.162`.

## v0.13.161 — розумніший розподіл рядка Sidebar (перенос лише коли обидві частини довгі)

**Проблема з ручного тесту v0.13.160:** правило v0.13.156 «лейбл > 50% → значення на другий рядок» спрацьовувало навіть коли значення коротке: `Auto-sync after backup` + `On/Off` розносило на два рядки, хоча `On` чудово вміщується в залишку праворуч.

**Нове правило в `SidebarEntryBase::DrawEntry()` (`sidebar.cpp`):**

1. **Перенос на другий рядок — лише коли ОБИДВІ частини окремо довші за 75%** робочої ширини рядка (тобто жодна не може розумно стояти поруч з іншою). Кожен рядок 2-рядкового режиму скролиться при фокусі, якщо й повної ширини мало.
2. **Інакше — завжди один рядок:** коротша частина отримує свою природну ширину (з обмеженням 75%, щоб довшій завжди лишалась мінімум чверть), довша забирає весь залишок і скролиться при фокусі, якщо не вміщується. Для типового короткого значення (`On`/`Off`, чекбокс) це піксельно збігається зі старою поведінкою: значення притиснуте праворуч, лейбл займає решту.

Приклади: `Auto-sync after backup: On` — один рядок, без переносу і без скролу (усе вміщується); `Location: sd://дуже/довгий/шлях` — один рядок, `Location` природної ширини, шлях скролиться у ~83% рядка; лейбл 80% + значення 80% — два рядки.

### Ручна перевірка

1. `Backup Options`: `Auto-sync after backup` з `On`/`Off` — на одному рядку, значення праворуч, без переносу.
2. `Location` з довгим шляхом — один рядок, шлях скролиться при фокусі, лейбл не обрізаний.
3. Штучний випадок з дуже довгими лейблом І значенням одночасно — два рядки, обидва скроляться за потреби.
4. Типові короткі пункти в усіх Sidebar — вигляд без змін.

### Ключові файли

* `sphaira/source/ui/sidebar.cpp` — `DrawEntry()`: перенос лише при «обидві > 75%», інакше однорядковий розподіл коротша-природна/довша-залишок-зі-скролом.
* `sphaira/CMakeLists.txt` — версія `0.13.161`.

## v0.13.160 — прогрес-бар для авто-синку після Backup

`Auto-sync after backup` (фонове вивантаження щойно створеного ZIP одразу після Backup, `Menu::BackupSaves()`) ніколи не мав жодного `curl::OnProgress` — цей код не чіпався попередніми правками (вони стосувались лише ручного `Sync with remote`), тому бар був відсутній із самого початку, а не через регресію v0.13.155.

* Винесено спільний хелпер `MakeAggregateProgressCb(pbox, files_done, total_units)` і константу `SYNC_PROGRESS_SCALE` (1 000 000) в анонімний namespace `save_menu.cpp` — та сама логіка "живого, не скидається між файлами" прогресу, що вже застосована в ручному `Sync with remote`, тепер спільна для обох місць замість дубльованої локальної лямбди.
* Авто-синк після Backup переписано на індексований цикл із `total_units = entries.size() * SYNC_PROGRESS_SCALE`; кожен upload отримує `MakeAggregateProgressCb`, бар оновлюється і в реальному часі під час передачі, і після завершення кожного файлу (навіть якщо для якогось запису бекап не знайдено — лічильник однаково просувається, щоб бар дійшов до 100%).

### Ручна перевірка

Увімкнути `Auto-sync after backup` у `Backup Options`, зробити Backup: після успішного локального бекапу з'являється попап `Auto-syncing saves...` з видимим, рухомим прогрес-баром (не порожнім/нерухомим), що не скидається, якщо вивантажується кілька файлів.

### Ключові файли

* `sphaira/source/ui/menus/save_menu.cpp` — `MakeAggregateProgressCb()`/`SYNC_PROGRESS_SCALE`, переписаний авто-синк у `BackupSaves()`.
* `sphaira/CMakeLists.txt` — версія `0.13.160`.

## v0.13.159 — Auto-sync after backup повернуто в Backup Options

Користувач уточнив ще раз: перенесення `Auto-sync after backup` у `Save Options` разом із `Sync with remote` у v0.13.157 було помилковим. Логіка користувача: `Auto-sync after backup` — це властивість саме дії Backup (одразу після створення бекапу вирішити, чи вивантажити його в хмару й лишити на карті, чи ні), тому їй місце поруч із самою дією, а не в загальних параметрах збережень. `Sync with remote` (окрема, явно запускана дія над уже наявними бекапами) залишається в `Save Options`.

* `DisplaySaveOptions()` (Save Options, кнопка `+`): секція `SYNC` тепер містить лише `Sync with remote`.
* `PromptSaveTypeOptions(false)` (Backup Options, тільки Backup): `Auto-sync after backup` повернуто назад, одразу після блоку `Location`. Restore Options цього пункту як і раніше не має.
* Текст tooltip уточнено: `"...use Sync with remote (Save Options) for that."`, щоб було зрозуміло, де шукати повну двобічну синхронізацію.

### Ручна перевірка

`Backup Options` знову містить `Auto-sync after backup` (можна одразу після Backup увімкнути/вимкнути вивантаження в хмару); `Save Options` (кнопка `+`) містить лише `Sync with remote`.

### Ключові файли

* `sphaira/source/ui/menus/save_menu.cpp` — переміщення `SidebarEntryBool` між `DisplaySaveOptions()` і `PromptSaveTypeOptions()`.
* `assets/romfs/i18n/en.json`, `assets/romfs/i18n/uk.json` — уточнений текст tooltip.
* `sphaira/CMakeLists.txt` — версія `0.13.159`.

## v0.13.158 — фікс потрійного слеша в мітці SD

`MakeSdLocationLabel()` конкатенував `"sd://"` з абсолютним шляхом, що вже починається з `/` (наприклад `/dumps`), даючи `sd:///dumps` (три слеші). Виправлено: провідний `/` шляху прибирається перед конкатенацією, тому результат — `sd://dumps` (два слеші, як у `webdav://`). Стосується стандартного `/dumps`, записів історії та щойно вибраної SD-папки.

### Ручна перевірка

`Backup Options → Location` (і Save Options, де тепер живе Sync) — стандартний запис показує `sd://dumps`, без потрійного слеша.

### Ключові файли

* `sphaira/source/ui/menus/save_menu.cpp` — `MakeSdLocationLabel()`.
* `sphaira/CMakeLists.txt` — версія `0.13.158`.

## v0.13.157 — sync у Save Options, живий прогрес-бар, фікс мітки SD

Три зауваження з ручного тесту v0.13.156:

### 1. `Sync with remote`/`Auto-sync after backup` перенесено з Backup Options у Save Options

Користувач уточнив: ці два пункти — не властивість конкретної дії Backup, а загальний параметр збережень. Тепер вони живуть у `Save Options` (меню, що відкривається кнопкою `+`/START зі списку сейвів, `Menu::DisplaySaveOptions()`), секція `SYNC` між `Data Types` і `Advanced`. З `Backup Options` (`PromptSaveTypeOptions`) секцію `SYNC` прибрано повністю — там більше немає нічого пов'язаного з sync. Поведінка самих пунктів (підтверджувальний OptionBox для Sync, прямий тумблер для Auto-sync) не змінилась, тільки розташування.

### 2. Прогрес-бар синхронізації: був відсутній, тепер живий і не скидається між файлами

**Причина:** у v0.13.155 прогрес зробили суто по кількості файлів (`i/N`), оновлюваних лише в момент завершення кожного файлу — жодного `curl::OnProgress` не було. Для типової синхронізації (1–2 файли) бар просто стояв нерухомо весь час фактичної передачі й "стрибав" тільки в момент завершення — візуально виглядало як повна відсутність прогрес-бару, на відміну від Backup, де копіювання показує живий побайтовий прогрес.

**Виправлення:** кожна фаза (`Local → WebDAV`, `WebDAV → SD`) тепер має "бюджет" `PROGRESS_SCALE` (1 000 000) умовних одиниць на файл. `curl::OnProgress` для поточного файлу мапить його реальний побайтовий прогрес (`dlnow/dltotal` або `ulnow/ultotal`) у частку цього бюджету і додає до вже пройдених повних файлів: `offset = files_done * SCALE + file_fraction * SCALE`, `size = total_files * SCALE`. Це поєднує обидві попередні вимоги: бар **ніколи не скидається на 0%** при переході до нового файлу (плавно продовжує з місця попереднього), і водночас **реально рухається** під час передачі кожного файлу, а не лише в момент його завершення.

### 3. Мітка SD усе ще показувала "microSD card"

**Причина:** `sd://`-мітку (`MakeSdLocationLabel()`) застосували до стандартного `/dumps` і до записів з історії (`GetRecentBackupDirs()`), але **пропустили** момент одразу після вибору папки через `Choose Folder...` — там лейбл будувався напряму з `fs_entry.name` (`"microSD card"` — технічна назва SD-джерела у filepicker), а не через новий хелпер. Виправлено: одразу після вибору SD-папки лейбл теж будується як `sd://<шлях>`; для stdio (USB/HDD) без змін — `<Назва пристрою>: <шлях>`.

### Ручна перевірка

1. У списку сейвів натиснути `+`: `Save Options` містить секцію `SYNC` з `Sync with remote` і `Auto-sync after backup`; `Backup Options`/`Restore Options` цих пунктів більше не мають.
2. Запустити `Sync with remote` із хоча б одним файлом для завантаження/вивантаження: прогрес-бар видимо рухається протягом усієї передачі файлу (не лише стрибає в кінці), і не скидається на 0%, коли починається наступний файл.
3. У `Backup Options → Location` обрати `Choose Folder...` на SD-карті: одразу після вибору мітка — `sd://<шлях>`, не `microSD card: <шлях>`. Перезайти в Location — запис з історії теж `sd://...`.

### Ключові файли

* `sphaira/source/ui/menus/save_menu.cpp` — `DisplaySaveOptions()` (нова секція SYNC), `PromptSaveTypeOptions()` (секцію прибрано), `SyncSavesRemoteWithLocation()` (живий агрегований прогрес), фікс лейбла в filepicker-callback.
* `sphaira/CMakeLists.txt` — версія `0.13.157`.

## v0.13.156 — фікс збірки та фінальний алгоритм рядка Location (id 52)

### Фікс збірки (WSL)

`location_entry->SetCallback([state, location_entry](){...})` та вкладений у нього `App::Push<PopupList>(..., [state, location_entry, picker_index](auto op_index){...})` не захоплювали `this`, тому найглибша вкладена filepicker-лямбда не могла викликати `AddRecentBackupDir()` (член-функцію). Додано `this` у список захоплення обох проміжних лямбд (`save_menu.cpp`, біля `Location` entry в `PromptSaveTypeOptions`).

### Фінальний алгоритм рядка Location (заміна горизонтального скролу з v0.13.155)

Користувач відхилив рішення "просто скрол": описано точний алгоритм розподілу ширини між лейблом і значенням. Переписано `SidebarEntryBase::DrawEntry()` (`sidebar.cpp`) — стосується **всіх** Sidebar-рядків, не лише Location:

1. Вимірюється природна ширина лейбла (`label_w`) і значення (`value_w`) відносно робочої ширини рядка (`usable_w = m_pos.w - 30`).
2. Якщо `label_w > 50%` від `usable_w` — значення переноситься на другий рядок під лейблом (лейбл отримує весь рядок 1, значення — весь рядок 2, обидва в межах тієї самої (незмінної) висоти рядка 70px).
3. Інакше, якщо `value_w > 75%` від `usable_w` — так само перенос на другий рядок (навіть якщо лейбл короткий, довге значення краще на всю ширину знизу, ніж у вузькому залишку).
4. Інакше — один рядок: лейбл займає рівно свою мінімальну потрібну ширину (не більше), а все, що залишилось (мінус відступ), віддається значенню, вирівняному по правому краю рядка як і раніше — для типових коротких значень (`On`/`Off`, чекбокс-гліф) це виглядає ідентично до попередньої поведінки.
5. У будь-якому з трьох випадків, якщо навіть виділеного простору (весь рядок для 2-рядкового режиму, залишок — для однорядкового) не вистачає — текст скролиться при фокусі (`ScrollingText`, вже наявний механізм).

Дворядковий режим не змінює висоту рядка Sidebar (лишається 70px, як і для решти пунктів) — обидва рядки тексту (по ~20px) комфортно вміщуються всередині наявної висоти без правок геометрії/scissor `List`, тому інші Sidebar-екрани (WebDAV Server address, Module Manager тощо) лишаються без регресії й отримують той самий захист від довгих значень безкоштовно.

### Мітка SD-локації: `sd://` замість `"SD: "`

`MakeSdLocationLabel()` тепер повертає `"sd://" + шлях` (наприклад `sd:///dumps`) замість `"SD: /dumps"` — коротше і в стилі вже наявного технічного префіксу `webdav://`. Стосується лише SD-записів (стандартний `/dumps` і SD-записи з історії); stdio-записи (USB/HDD) як і раніше показують `<Назва пристрою>: <шлях>`. Ключ i18n `"SD"` видалено як більше не використовуваний.

### Ручна перевірка

1. Переконатися, що WSL-збірка проходить без помилок компіляції (`this` capture).
2. `Backup Options → Location`: стандартний запис показує `sd:///dumps` (не `SD: /dumps`, не `microSD card: /dumps`).
3. Обрати папку з дуже довгим шляхом через `Choose Folder...` так, щоб `value_w` перевищив 75% ширини рядка: значення переноситься на другий рядок під `Location`, обидва рядки видимі одночасно, рамка фокусу не обрізає жоден з них.
4. Якщо лейбл сам по собі надзвичайно довгий (штучний тест) — перевірити перенос значення вниз навіть при короткому значенні.
5. Перевірити типовий короткий випадок (`Compress backup: On`) в іншому Sidebar — вигляд не змінився відносно попередніх версій.
6. Якщо навіть другий рядок не вміщує шлях повністю — при фокусі текст скролиться.

### Ключові файли

* `sphaira/source/ui/menus/save_menu.cpp` — фікс capture, `MakeSdLocationLabel()`.
* `sphaira/source/ui/sidebar.cpp` — новий алгоритм `DrawEntry()` (label/value width test, 2-рядковий режим, скрол-фолбек).
* `assets/romfs/i18n/en.json`, `assets/romfs/i18n/uk.json` — прибрано невикористаний ключ `"SD"`.
* `sphaira/CMakeLists.txt` — версія `0.13.156`.

## v0.13.155 — виправлення розміщення sync-UI (id 49) та історія папок бекапу (id 52)

> **Не збиралося (WSL build error) та суперседено у v0.13.156/v0.13.157:** підпис `"SD: ..."` і горизонтальний скрол довгого шляху (пункт "Довгий шлях у рядку Location" нижче) замінено на алгоритм 2-рядкового розподілу та мітку `sd://` у v0.13.156. Розміщення `Sync with remote`/`Auto-sync after backup` у `Backup Options` (нижче) також суперседено у v0.13.157 — обидва пункти перенесено в `Save Options` (кнопка `+`), і прогрес-бар синхронізації, описаний нижче як "агрегований по файлах без OnProgress", замінено на живий побайтовий прогрес, що не скидається між файлами. Записи цього розділу лишаються як історія рішень, а не поточна поведінка.

### Виправлення після ручного рев'ю v0.13.154

Користувач відхилив розміщення з v0.13.154: `Sync with remote` і `Auto-sync after backup` не повинні бути пунктами `Save Action` (там лишається рівно дві дії — Backup і Restore). Обидва пункти перенесено у `Backup Options` (тільки для Backup, не для Restore):

* `PromptSaveAction()` знову показує лише `Backup`/`Restore`; `SyncSavesRemote()` і перемикач автосинку більше не викликаються звідти.
* У `PromptSaveTypeOptions(false)` (Backup Options) додано секцію `SYNC`: `SidebarEntryCallback "Sync with remote"` (викликає `SyncSavesRemote()` як і раніше, з підтверджувальним поп-апом) та `SidebarEntryBool "Auto-sync after backup"` (прямий тумблер `m_save_autosync`, без проміжного OptionBox — його прибрано разом з `PromptAutoSyncToggle()`). Кожен пункт має власний tooltip (лівий info-блок Sidebar при фокусі) з поясненням: у Sync — що це двобічний перенос ZIP з дедуплікацією за іменем; у Auto-sync — що вивантажується лише щойно створений ZIP, а не вся бібліотека.
* **Прогрес синхронізації перероблено на агрегований, а не по-файлово.** Раніше кожен файл мав власний `curl::OnProgress` — смуга скидалась і заповнювалась заново для кожного файлу. Тепер `SyncSavesRemoteWithLocation()` показує окремий прогрес-бар для кожного напрямку (`Local → WebDAV`, потім `WebDAV → SD`), де офсет — кількість завершених файлів фази, а розмір — загальна кількість файлів цієї фази; бар посувається лише після завершення файлу і ніколи не скидається в середині фази. Ім'я поточного файлу показується в заголовку (`SetActionName`), а не в смузі, що скидає лічильник.

### Історія папок бекапу та SD-мітка (id 52, частина 1)

* Додано збережувану історію до 5 останніх унікальних папок, підтверджених через `Choose Folder...`: `Menu::AddRecentBackupDir()`/`GetRecentBackupDirs()` (`save_menu.hpp`/`.cpp`), серіалізація в `option::OptionString[5]` під `[saves] recent_backup_dir_0..4` — переживає перезапуск. Дедуплікація й move-to-front за нормалізованим шляхом (ім'я в підписі ігнорується для порівняння).
* У Backup/Restore Options ці до 5 записів показуються одразу під стандартним `/dumps`, перед списком stdio-накопичувачів (USB/HDD); записи, що збігаються з `/dumps` або вже показаним stdio-шляхом, у список не потрапляють.
* Технічний `"microSD card"` підпис у користувацьких мітках локацій замінено на локалізований короткий `"SD"` (`MakeLocationLabel("SD"_i18n, ...)` → `"SD: /dumps"`); технічні mount/path не чіпались.
* **Довгий шлях у рядку Location:** замість буквального переносу на новий рядок (що вимагало б динамічної висоти рядків Sidebar) обрано горизонтальний скрол — той самий механізм, що й для назв пунктів. У спільному `SidebarEntryBase::DrawEntry()` (`sidebar.cpp`) значення праворуч тепер обмежене ~45% ширини рядка й скролиться при фокусі через раніше оголошений, але не використовуваний `m_scolling_value`; це прибирає перекриття заголовка довгим шляхом і зайвий вихід тексту за межі рядка в усіх Sidebar (не лише Location). Це свідома відмова від дворядкового wrap з UX-нотатки id 52 — переносу рядків немає, механізм той самий, що вже використовується в застосунку для довгих назв.

### Ручна перевірка

1. Відкрити Save Action: лише `Backup` і `Restore`, жодного sync-пункту.
2. Відкрити Backup Options: під `Location` з'явилась секція `SYNC` з `Sync with remote` і `Auto-sync after backup`; фокус на кожному показує пояснювальний tooltip зліва; Restore Options цієї секції не має.
3. Запустити `Sync with remote` з декількома відсутніми файлами в обидва боки: прогрес спершу показує фазу `Local → WebDAV` з лічильником файлів (не відсотком одного файлу), потім `WebDAV → SD`; бар не скидається/не блимає всередині фази.
4. Перевірити `Auto-sync after backup`: перемикання тепер миттєве (без проміжного підтвердження), стан зберігається.
5. Вибрати 6 різних папок через `Choose Folder...` у Backup Options — під `/dumps` мають лишитися 5 найновіших; повторний вибір наявної піднімає її на початок без дубліката; перезапуск застосунку зберігає історію.
6. Підпис локації показує `SD: /dumps` замість `microSD card: /dumps`; довгий шлях у фокусі скролиться, не перекриваючи назву `Location`.

### Відомі обмеження

* Довгий шлях вирішено горизонтальним скролом, а не переносом рядка чи зміною висоти Sidebar-рядка — свідомий вибір через ризик ширших змін у геометрії/scissor Sidebar заради всіх типів записів.
* Runtime-перевірка потребує консолі; збірку виконує Агент 1 (WSL).

### Ключові файли

* `sphaira/source/ui/menus/save_menu.cpp`, `sphaira/include/ui/menus/save_menu.hpp` — розміщення sync-пунктів, агрегований прогрес, історія папок бекапу.
* `sphaira/source/ui/sidebar.cpp` — обмежене й скрольоване значення в `DrawEntry()`.
* `assets/romfs/i18n/en.json`, `assets/romfs/i18n/uk.json` — нові/прибрані рядки.
* `sphaira/CMakeLists.txt` — версія `0.13.155`.

## v0.13.154 — зрозуміла WebDAV-синхронізація сейвів (id 49)

> **Суперседено у v0.13.155:** розміщення в `Save Action` та per-file прогрес, описані нижче, змінено після ручного рев'ю користувача — див. розділ v0.13.155 вище. Записи цього розділу лишаються як історія, а не поточна поведінка.

### Що зроблено і як це працює

* **Підтвердження перед синхронізацією.** `Sync with remote` у контекстному меню `Save Action` більше не запускає мережеву операцію одразу. Спершу показується OptionBox із поясненням: синхронізуються ZIP-бекапи вибраних зараз сейвів у два боки; всі архіви, яких бракує на іншому боці, копіюються (нові локальні — на WebDAV, нові хмарні — на SD); файли з однаковим ім'ям не перезаписуються й не порівнюються за датою чи вмістом; після завантаження з хмари слід скористатися Restore. Лише кнопка `Sync` веде до вибору WebDAV-локації та запуску; `Cancel` не змінює жодного файлу.
* **Auto-sync перенесено в Save Action.** Тумблер `Auto-sync saves after backup` прибрано з `Save Options → Advanced`. Тепер у `Save Action` четвертий пункт `Auto-sync after backup: On/Off` показує поточний стан; вибір відкриває OptionBox, який чесно описує область дії (після кожного Backup вивантажується лише щойно створений ZIP, а не вся бібліотека) і пропонує `Enable`/`Disable`. Підтвердження перемикає опцію та показує повідомлення; Cancel нічого не змінює. Семантика самої автосинхронізації не змінювалася.
* **Двофазний прогрес.** `SyncSavesRemoteWithLocation()` переписано: спершу будується повний план (лістинг локальних файлів, включно з DBI-бекапами, і віддалених тек для кожного вибраного сейву), потім виконуються ВСІ upload-и як фаза `Local → WebDAV (i/N)`, і лише після її завершення — всі download-и як фаза `WebDAV → SD (i/M)`. Кожен рядок передачі містить власний лічильник поточний/загальний та ім'я файлу; додано per-file прогрес через `curl::OnProgress`, тож смуга прогресу й швидкість тепер видимі для кожного архіву. Логіка вибору, фільтр `.zip`, дедуплікація та розкладання DBI-імен у DBI-структуру не змінені; додано перевірку скасування між файлами (`ShouldExitResult`).

### Ручна перевірка (для користувача та рев'юера)

1. **Перевірити WebDAV поверх HTTPS:** `curl -X PROPFIND -u user:pass -H "Depth: 1" https://<сервер>/sphaira-saves/` має повернути XML зі списком тек. Якщо повертається помилка автентифікації або з'єднання — перевірити налаштування WebDAV у Network settings.
2. **Побачити архіви на сервері (Cooler):** `sudo find /srv/sphaira-webdav/sphaira-saves -type f -name '*.zip'`. Поточна схема — `sphaira-saves/<Гра>/<Тип>/<файл>.zip` та DBI-імена `<TID>_<тип>_<timestamp>_<index>.zip`.
3. **Один локальний ZIP, якого немає на сервері:** вибрати сейв → `Sync with remote` → прочитати поп-ап → `Sync`. Фаза `Local → WebDAV (1/1)` показує ім'я файлу, прогрес і швидкість; файл з'являється на сервері.
4. **Один ZIP лише на сервері:** видалити локальну копію, повторити sync. Перша фаза порожня, друга — `WebDAV → SD (1/1)` завантажує архів; далі `Restore` бачить його серед кандидатів.
5. **Змішаний набір:** кілька відсутніх архівів з обох боків — усі upload-и йдуть до першого download-а; лічильники обох фаз незалежні.
6. **Однакове ім'я з обох боків:** файл не передається повторно і не перезаписується (вміст не порівнюється — це задокументоване обмеження, видалення файлу на сервері не видаляє локальну копію).
7. **Cancel у поп-апі** не створює мережевого трафіку й не змінює файли; B у ProgressBox перериває синхронізацію між файлами.
8. **Auto-sync:** у `Save Action` перевірити пункт `Auto-sync after backup`, увімкнути через OptionBox, зробити Backup — на сервер має піти лише щойно створений ZIP цього сейву. `Save Options → Advanced` більше не містить цього тумблера; `Backup`/`Restore` працюють без змін.

### Відомі обмеження

* Архіви з однаковим ім'ям не порівнюються за датою чи вмістом — це чесно вказано в поп-апі.
* Видалення архіву на одному боці не видаляється на іншому; sync лише додає відсутнє.
* План будується до початку передач: файли, додані на сервер під час активної синхронізації, підхопляться наступним запуском.
* Runtime-перевірка потребує консолі та WebDAV-сервера; збірку виконує Агент 1 (WSL).

### Ключові файли

* `sphaira/source/ui/menus/save_menu.cpp` — підтвердження sync, пункт/поп-ап Auto-sync, двофазний `SyncSavesRemoteWithLocation()`.
* `sphaira/include/ui/menus/save_menu.hpp` — оголошення `PromptAutoSyncToggle()`.
* `assets/romfs/i18n/en.json`, `assets/romfs/i18n/uk.json` — нові рядки.
* `sphaira/CMakeLists.txt` — версія `0.13.154`.

## v0.13.153 — DBI Install queue (id 50)

### Що зроблено і як це працює

* Після отримання DBI0 `List` консоль переходить у `Analysing`, а потім у повноекранну `Install queue`. До натискання `Install selected` запис у content storage не починається: новий `yati::AnalyzeSource()` лише відкриває NSP/NSZ/XCI/XCZ, читає таблицю контейнера через DBI `FileRange` і повертає список content та план розміру без `ncm/ns`, tickets або placeholders.
* У черзі A вмикає/вимикає пакет, X вибирає все/нічого, Y циклічно змінює `Auto → microSD → System memory`, START запускає лише вибране, B скасовує сеанс. Рядок показує ім’я, розмір даних контейнера, прогноз встановлення та Auto-ціль; помилка аналізу має Result-код і автоматично вимикає пакет.
* Для звичайних NSP/XCI сума NCA-content позначена `Exact`. Для NSZ/XCZ поточний контейнерний API не надає достовірний розпакований розмір усіх NCZ до глибшого читання, тому показується `Estimate` за чинним правилом Yati `compressed × 1.6`; оцінка не маскується під точне значення.
* Підсумок вибраного порівнюється з вільним місцем microSD/NAND після налаштованого reserve. Для Auto кожен пакет отримує запропоноване Yati сховище; явний вибір задає спільну ціль. Якщо план не вміщується після reserve, перед запуском є окреме попередження.
* Інсталяція відбувається на повноекранному екрані з прогресом поточного content і кільцевим журналом до 128 рядків, який прокручується контролером або пальцем. Успіхи й recoverable package-помилки з Result-кодом залишаються в історії, після такої помилки черга продовжується. Cancel або USB/protocol session error зупиняє решту, бо наступні `FileRange` після розсинхронізації небезпечні; попередні успіхи лишаються в журналі без rollback.
* Yati тепер приймає вузький `InstallProgress`. Звичайні File, MTP root-drop і Web Server install як і раніше передають наявний `ProgressBox`; новий повноекранний UI використовується лише DBI.
* **Runtime fix для DBI Backend Qt:** `DbiUsb::Read()` раніше оголошував `FILE_RANGE data_size = sizeof(header) + filename`, але відправляв header і filename двома окремими USB transfers. PyUSB `read(data_size)` міг завершитися на першому 16-байтовому short packet, отримати порожнє ім’я й не відправити дані; Switch тоді безстроково лишався на `Analysing`. Тепер весь payload формується в одному буфері й надсилається одним transfer. Додатково worker stack збільшено з 64 до 128 КБ, handle закривається при невдалому `threadStart`, а fullscreen log показує поточний NCA/етап Yati.
* **Зрозумілий connection state:** до `UsbState_Configured` екран показує `Waiting for PC connection` і просить підключити кабель та перевірити, що консоль визначилася. Лише після реального USB configure текст змінюється на `PC connected` та інструкцію вибрати пакети й натиснути Start у DBI Backend. Після disconnect/помилки до отримання списку цикл повертається до очікування ПК.
* **Узгоджене керування чергою:** як у файловому менеджері, X перемикає поточний пакет, Y інвертує всі доступні пакети, A переходить до встановлення. ASCII `[x]`/`[ ]` замінено тим самим NanoVG checkbox і glyph `\uE14B`. R3 циклічно змінює ціль окремого поточного пакета (`Auto → microSD → System memory`); підсумок місця та фактичний `ConfigOverride` рахуються для кожного пакета окремо. Після A або підтвердження warning selection і target усіх пакетів копіюються в immutable install plan; одночасні X/Y/R3 більше не можуть змінити вже перевірений план.
* **Install log:** у верхньому рядку показується згладжена швидкість поточного content у MiB/s. Журнал використовує окремий компактний список із рядком 30 px замість queue-карток 82 px. Автоперехід до нового рядка працює лише коли користувач уже був у кінці; ручний scroll більше не відкидається назад кожною новою подією.

### Ручна перевірка

1. Надіслати DBI-клієнтом 2–3 NSP/NSZ/XCZ. Переконатися, що після списку видно `Analysing` і `Install queue`, а до START не створюються placeholders і не змінюється встановлений контент.
2. Вимкнути один пакет через A, перевірити X all/none, точні/оцінені розміри, вільне місце після reserve та запропоновану Auto-ціль. Перемкнути Y на microSD і System memory.
3. Натиснути START і перевірити загальний лічильник, прогрес поточного content, touch-scroll журнал і підсумок. Вимкнений пакет не повинен встановитися.
4. Додати пошкоджений або непідтримуваний пакет: помилка аналізу має блокувати його вибір. Для помилки під час install журнал має зберегти ім’я й Result-код, а наступний вибраний пакет — стартувати.
5. Скасувати у ReviewQueue та посеред Installing. У першому випадку не має бути запису; у другому вже завершені пакети лишаються у Summary/журналі без обіцянки rollback. Після виходу перевірити повернення MTP, якщо він був увімкнений.
6. Окремо перевірити MTP root-drop, Web Server install і звичайний file install: їхній `ProgressBox` та поведінка не повинні змінитися.
7. У DBI Backend Qt на Windows надіслати один NSP: після `Sending list...` екран `Analysing` має перейти до ReviewQueue без зависання; у backend не повинно бути `Requested file not found in list` з порожнім ім’ям. Після START поточний NCA/етап має бути видимий біля назви пакета.
8. Відкрити USB Install без кабелю: має бути `Waiting for PC connection`. Підключити кабель до ПК з коректним драйвером, але ще не натискати Start у DBI Backend: має з’явитися `PC connected... press Start`. Від’єднати кабель до списку — UI має повернутися до першого стану; підключити знову й передати список.
9. У ReviewQueue перевірити X для одного пакета, Y для інверсії, A для старту та R3 для незалежної цілі кожного рядка. Checkbox має збігатися з файловим менеджером. Під час install перевірити швидкість, компактний інтервал логу, touch-scroll угору під час надходження нових рядків і автоматичне слідування за кінцем після повернення вниз.

### Відомі обмеження

* DBI0 `List` передає лише імена, без окремого розміру чи Title ID. UI обчислює логічний розмір з меж content у контейнері та показує filename; title metadata не заявляється, якщо її неможливо одержати без install-парсингу CNMT.
* NSZ/XCZ лишаються чесною оцінкою ×1.6. Точний NCZ decompressed-size preflight потребує окремого розширення контейнерного аналізатора.
* Скасування не є транзакцією та не відкочує пакети, встановлені раніше в цій черзі. Якщо USB transfer уже заблокований, Cancel завершує його скасуванням transport-сесії замість DBI `Exit`; у ReviewQueue надсилається штатний `Exit`. Фатальна USB/protocol помилка також припиняє решту черги й лишає підсумок на екрані.
* Runtime-перевірка потребує Nintendo Switch і сумісного DBI-клієнта.

### Ключові файли

* `sphaira/source/ui/menus/dbi_menu.cpp`, `sphaira/include/ui/menus/dbi_menu.hpp` — стани сесії, queue UI, storage plan, install log і cancel.
* `sphaira/source/yati/yati.cpp`, `sphaira/include/yati/yati.hpp` — read-only preflight та спільний progress API.
* `sphaira/include/ui/install_progress.hpp`, `sphaira/include/ui/progress_box.hpp` — ізоляція DBI fullscreen UI від старих install-шляхів.
* `assets/romfs/i18n/en.json`, `assets/romfs/i18n/uk.json` — нові рядки.
* `sphaira/CMakeLists.txt` — версія `0.13.153`.

## v0.13.149 — сумісність сейв-бекапів із DBI (id 51)

### Що зроблено і як це працює

* Формат DBI розібрано з реальних бекапів: `/switch/DBI/saves/<назва гри>/<YYYYMMDD>/<TitleID>_<літера типу>_<YYYYMMDDHHMMSS>_<index>.zip`. Всередині ZIP файли сейву лежать з абсолютними шляхами, плюс два метафайли: `.dbi_save_info.ini` (текст: TitleId, TitleName, BackupDate, Account, Space) і `.dbi_save_extra` (сирі 512 байт `FsSaveDataExtraData`).
* Літери типів: `A` = Account, `B` = BCAT (підтверджені реальними бекапами), `D` = Device, `C` = Cache, `T` = Temporary (за тією ж схемою першої літери — припущення).
* Назву теки гри DBI утворює, лишаючи тільки ASCII-літери, цифри та пробіли (наприклад, `Adam's Venture: Origins` → `Adams Venture Origins`). Sphaira відтворює це правило; якщо вгадана тека не знайдена, Restore сканує всі теки в `/switch/DBI/saves` — ім'я файла все одно містить Title ID.
* **Запис**: несистемні бекапи (Account/BCAT/Device/Cache/Temporary) тепер пишуться у форматі та розташуванні DBI, тож DBI бачить і відновлює бекапи sphaira. `.nx_save_meta.bin` у DBI-формат не додається (щоб DBI не розпакував чужий метафайл у сейв) — його роль виконує `.dbi_save_extra`. System/System BCAT лишаються у форматі sphaira (`Save System/<id>`).
* **Відновлення**: кандидати збираються з усіх джерел — DBI (`/switch/DBI/saves`), нова структура sphaira (`/dumps/<гра>/<тип>`), legacy (`/dumps/Save/<гра>`) — і порівнюються за таймстемпом з імені файла; перемагає найновіший (старий DBI-архів не затінить свіжий sphaira-бекап і навпаки). Для Account/Cache перевіряється профіль/індекс через `.dbi_save_extra` усередині ZIP.
* Розпакування DBI-ZIP: абсолютні шляхи нормалізуються наявним `fs::AppendPath`, метафайли DBI пропускаються, а точні `data_size`/`journal_size` для `fsExtendSaveDataFileSystem` беруться з `.dbi_save_extra`.
* **Синхронізація**: auto-sync вантажить найновіший бекап (тепер зазвичай DBI-файл) у `sphaira-saves/<гра>/<тип>/` — структура на WebDAV не змінилася. Ручний sync бачить локальні DBI-бекапи, а завантажені з WebDAV файли з DBI-іменем кладе у `/switch/DBI/saves/<гра>/<дата>/`. Попутно виправлено: тека призначення при завантаженні тепер створюється повністю (раніше `CreateDirectoryRecursivelyWithPath` викликався зі шляхом теки і не створював останню компоненту).

### Ручна перевірка

1. Створити Account-бекап у sphaira і переконатися, що ZIP з'явився у `/switch/DBI/saves/<гра>/<сьогодні YYYYMMDD>/<TID>_A_<час>_0.zip`, а DBI може його відновити.
2. Зробити бекап у DBI і відновити його в sphaira (Restore) — зокрема для гри з двома профілями: має відновитися архів саме того профілю.
3. Переконатися, що старі sphaira-бекапи (`/dumps/<гра>/<тип>` і `/dumps/Save/<гра>`) досі знаходяться Restore, і що з пари «старий DBI / новий sphaira» вибирається новіший.
4. Auto-sync і ручний Sync: перевірити upload DBI-файлів у `sphaira-saves/<гра>/<тип>/` та download назад у `/switch/DBI/saves/<гра>/<дата>/`.
5. System/System BCAT: формат і шляхи без змін.

### Відомі обмеження

* Літери типів D/C/T та поведінка DBI при відновленні ZIP зі сторонніми не-абсолютними записами не підтверджені реальними прикладами (у наявних бекапах були лише A і B).
* Нові локальні бекапи більше не потрапляють у `/dumps/<гра>/<тип>` — ця структура (v0.13.148) стала read-only-legacy для Restore і sync.

### Ключові файли

* `sphaira/source/ui/menus/save_menu.cpp` — формат DBI: запис, пошук, відновлення, синк.
* `sphaira/CMakeLists.txt` — версія `0.13.149`.

## v0.13.148 — структура бекапів «гра → тип» (id 38)

### Що зроблено і як це працює

* Для Account, BCAT, Device, Cache і Temporary базовий шлях тепер має вигляд `<корінь>/<назва гри або Title ID>/<тип>/`. Наприклад: `/dumps/Mario Kart 8 Deluxe/Account/<backup>.zip`.
* Та сама формула використовується для нових локальних бекапів, автоматичного WebDAV upload і ручної двосторонньої синхронізації, тому віддалена структура стає `sphaira-saves/<гра>/<тип>/`.
* System і System BCAT лишилися у старих каталогах `Save System/<id>` та `Save System BCAT/<id>`, бо вони не належать окремій грі.
* Відновлення сумісне зі старими архівами. Пошук іде в такому порядку: новий шлях за назвою гри, новий шлях за Title ID, старий шлях за назвою, старий шлях за Title ID. Нові архіви мають пріоритет.

### Ручна перевірка

1. Створити Account-бекап і перевірити ZIP у `/dumps/<гра>/Account/`.
2. Створити BCAT, Device, Cache або Temporary-бекап і перевірити відповідну підпапку гри.
3. Покласти старий ZIP у `/dumps/Save/<гра>/` або `/dumps/Save/<TitleID>/` і переконатися, що Restore його знаходить.
4. Увімкнути auto-sync або запустити ручний Sync і перевірити шлях `sphaira-saves/<гра>/<тип>/` на WebDAV.
5. Перевірити System/System BCAT: шлях повинен лишитися старим, а backup/restore — працювати без зміни.

### Відомі обмеження

* Старі локальні та WebDAV-каталоги не мігруються і не скануються ручною синхронізацією. Legacy-шляхи використовує тільки Restore; щоб перенести старий архів у нову WebDAV-структуру, потрібне окреме ручне копіювання або новий backup.
* Задача не додає MTP-диск Saves: його узгоджену ієрархію реалізує окрема id 37.

### Ключові файли

* `sphaira/source/ui/menus/save_menu.cpp` — побудова нових/legacy-шляхів і порядок пошуку для Restore.
* `sphaira/CMakeLists.txt` — версія `0.13.148`.
* `task.md` і `walkthrough.md` — фактичний обсяг, перевірка та обмеження.

## v0.13.147 — Kefir Hub: ребрендинг і міграція конфігів (id 45)

* Єдиний корінь даних визначено в `sphaira/include/app_paths.hpp`: застосунок читає й пише лише `/config/kefir/`.
* Перший запуск поверх старої інсталяції атомарно перейменовує всю `/config/sphaira/` у `/config/kefir/`, разом із config/playlog/locations/log, локальними i18n, асоціаціями, GitHub-джерелами, пакетами, завантаженнями, темами й logo. Якщо існують обидва корені або rename не вдався, запуск зупиняється з помилкою; fallback і змішування відсутні.
* Бренд змінено на **Kefir Hub**: NACP, hbmenu self-detection зі сумісністю зі старим NACP `sphaira`, прямий шлях `/switch/kefir-hub/kefir-hub.nro`, web, README, workflow artifact, вбудоване GitHub-джерело та `assets/icon.jpg`. Після окремого вибору замінено іконку на верхній центральний варіант із концепт-грида — кремове коло з кольоровими культурами на темно-бірюзовому полі; NRO перебудовано.
* Версія `0.13.147`. WSL-збірка дійшла до `[100%] Built target sphaira_nro` і створила `build/ReleaseWithInstall/kefir-hub.nro` (4,314,172 байти). Наступний пакет — лише id 44 + id 46 після рев'ю.

## v0.13.146 — WebDAV у Network settings та синхронізації сейвів

### Завдання
Додати централізовані налаштування WebDAV (URL, користувач і пароль) та підключити їх до ручної й автоматичної синхронізації сейвів без обов’язкового запису в `locations.ini`.

### Реалізація
1. У `App` додано збережувані параметри `webdav_url`, `webdav_user` і `webdav_pass` зі статичними геттерами та сеттерами.
2. У секції Network додано три поля редагування через системну клавіатуру. URL і користувач відображаються як значення, пароль — лише як рядок зірочок; порожній URL вимикає підключення з налаштувань.
3. У Save Menu ручний sync і автосинхронізація переведені на спільний `GetWebdavLocations()`: підключення з налаштувань іде першим, сумісність із `/config/sphaira/locations.ini` збережена, дублікати URL відкидаються.
4. Додано англійські й українські переклади та піднято версію до `0.13.146`.
5. Після рев’ю URL канонізується: крайні пробіли й хвостові `/` прибираються, адреса без схеми та `https://` переводяться у `webdav://`. Псевдосхема виконується через HTTPS і вмикає MKCOL для автоматичного створення віддалених папок. Для дедуплікації `webdav://` прирівнюється до `https://`; маска пароля стала фіксованою, а старий пароль не підставляється відкритим текстом у клавіатуру.
6. Якщо одночасно налаштовано Settings URL і `locations.ini`, автосинхронізація навмисно використовує Settings URL як пріоритетний; ручна синхронізація показує решту у виборі локації.

### Перевірка
- JSON локалізацій проходить синтаксичну перевірку.
- Агент 1 виконав контрольну WSL-збірку `cmake --build --preset ReleaseWithInstall`: exit code 0, `[100%] Built target sphaira_nro`.
- Після review-фіксів створено `build/ReleaseWithInstall/sphaira.nro` (4 299 329 байтів, SHA-256 `1c18338a76d7f1e898bc44f61c796eb6f5b36db2470897a655535c44585d79da`). Наявні warning-и стосуються попереднього коду/dependencies і не блокують збірку.

---

## v0.13.145 — Module Manager без manifest: реальні sysmodules із toolbox.json

### Завдання
Прибрати manifest/components модель із "Керування модулями". Користувач очікує, що меню керує фактично встановленими модулями Atmosphere, як Tesla/ovl-sysmodules, а не абстрактними компонентами з `/manifest.json`.

### Підхід
* **UninstallerMenu як sysmodules manager** ([uninstaller_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/uninstaller_menu.cpp), [uninstaller_menu.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/menus/uninstaller_menu.hpp)): прибрано залежність від `manifest.hpp`, `ComponentItem`, вибір кількох компонентів, Disable/Enable/Delete через переміщення файлів. Натомість меню сканує `/atmosphere/contents`, читає `toolbox.json` у кожній папці, бере `tid`, `name`, `requires_reboot`, і будує список реальних sysmodules.
* **Статуси**: running визначається через `pmshellGetProcessId(program_id)`, autostart — через наявність `/atmosphere/contents/<tid>/flags/boot2.flag`, static/dynamic — через `requires_reboot` із `toolbox.json`.
* **Дії**: A перемикає тільки runtime-стан dynamic-модуля через `pmshellLaunchProgram` / `pmshellTerminateProgram`. Для static/reboot-required модулів A показує підказку користуватися autostart. Y окремо перемикає `boot2.flag`, X перескановує список.
* **Manifest прибрано повністю**: `source/manifest.cpp` видалено зі списку джерел у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt), а [manifest.cpp](file:///d:/git/dev/sphaira/sphaira/source/manifest.cpp) і [manifest.hpp](file:///d:/git/dev/sphaira/sphaira/include/manifest.hpp) видалено з репо.
* **Обмеження v0.13.145**: видалення модулів не реалізовано навмисно. Без manifest безпечна семантика видалення — це окрема дія на всю папку `/atmosphere/contents/<tid>`, її краще додавати після рев'ю базового toggle/autostart.
* **Фікс після рев'ю v0.13.145**: `ParseProgramId` більше не читає `c_str()` тимчасового `std::string`; A не змінює `boot2.flag`; опис пункту більше не обіцяє remove; додано i18n-ключ `"Toggle"`; прибрано дубльований `UpdateActions()`.
* **Наступні заплановані пакети (id 45 → id 44 + id 46, ще не реалізовано):** спершу ребрендинг застосунку в **Kefir Hub** і повна міграція `/config/sphaira/` у єдиний конфіг-корінь `/config/kefir/`. Лише після цього Module Manager отримує окремі підписані стани `Зараз` і `Після перезапуску`, нейтральне пояснення для reboot-only модулів і реєстр `/config/kefir/modules.json` з локалізованими описами за TID. Також буде виправлено геометрію списку: рамка фокусу зараз обрізається scissor-областю по краях і зверху, а список виглядає зміщеним униз. Дані не зберігаються у `toolbox.json`, бо його перезаписує оновлення модуля; коментарі також не підходять, бо JSON їх не підтримує.

Версію програми збільшено до `0.13.145` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* WSL-збірка `cmake --build --preset Release` пройшла успішно після рев'ю-фіксів, артефакт згенеровано в `build/Release/sphaira.nro`. У логах лишився наявний warning про невикористану `IsFanCurveEnabled()` у `settings_menu.cpp`, але збірка завершилася з кодом 0.
* Для ручного рев'ю: меню "Керування модулями" більше не має згадувати `/manifest.json`; має показувати модулі з `toolbox.json`; A має перемикати dynamic-модуль on/off без зміни autostart, Y — autostart, X — оновлювати список.

---

## v0.13.144 — Module Manager перенесено в Kefir Settings

### Завдання
Після перевірки на консолі стало очевидно, що Module Manager не є загальним інструментом Tools: він залежить від Kefir/Sphaira manifest у корені SD і без нього показує помилку `No manifest.json found on SD card`. Тому пункт потрібно прибрати з Tools і покласти в Kefir Settings, де контекст залежності зрозуміліший.

### Підхід
* **Tools menu** ([tools_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/tools_menu.cpp)): прибрано плитку Module Manager, include `uninstaller_menu.hpp`, embed `component-manager.png` і відповідний запис у масиві іконок. У Tools лишається плитка Games із `game-hub.png`.
* **Kefir Settings** ([settings_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/settings_menu.cpp)): у `BuildKefirItems()` додано пункт `"Module Manager"_i18n`, який відкриває `ui::menu::hats::UninstallerMenu`. У v0.13.145 опис уточнено до `"Start, stop and configure installed sysmodules."_i18n`, бо видалення модулів навмисно не реалізовано.
* **Причина manifest-помилки у v0.13.144**: тодішній `UninstallerMenu` читав не Nintendo title manifest, а власний `/manifest.json` на SD. Цю модель повністю прибрано у v0.13.145: актуальна реалізація більше не шукає `/manifest.json`, а сканує реальні sysmodules за `/atmosphere/contents/*/toolbox.json`.

Версію програми збільшено до `0.13.144` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* WSL-збірка `cmake --preset Release && cmake --build --preset Release` пройшла успішно, артефакт згенеровано в `build/Release/sphaira.nro`. У логах лишилися наявні warning-и в сторонніх/інших ділянках коду та повторне застосування `libsmb2` patch під час конфігурації, але збірка завершилася з кодом 0.
* Для ручного рев'ю v0.13.144: у Tools має бути Games, але не Module Manager; у Kefir Settings має бути пункт "Керування модулями". Цей пункт рев'ю вже застарів після v0.13.145, де manifest-помилку прибрано повністю.

---

## v0.13.143 — Games і Module Manager у Tools

### Завдання
Виконано перший крок із рекомендованого порядку: додати плитку "Games" у меню Tools та перенести керування модулями з Settings → Homebrew у Tools із перейменуванням "Component Manager" на "Module Manager".

### Підхід
* **Tools menu** ([tools_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/tools_menu.cpp)): додано `game_menu.hpp` та `uninstaller_menu.hpp`, нові embed-іконки `game-hub.png` і `component-manager.png`, плитку "Games" після "Saves" та плитку "Module Manager" поруч із нею. "Games" відкриває той самий `ui::menu::game::Menu`, що й головне меню, а "Module Manager" відкриває наявний `ui::menu::hats::UninstallerMenu`.
* **Завантаження іконок**: замість ручних присвоєнь `m_items[0..7]` використано локальний масив `{data, size}` у порядку плиток і короткий цикл. Це прибирає крихкий зсув індексів після додавання двох нових плиток.
* **Settings → Homebrew** ([settings_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/settings_menu.cpp)): старий пункт "Component Manager" прибрано, щоб інструмент мав одне основне місце запуску — Tools.
* **Назва меню** ([uninstaller_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/uninstaller_menu.cpp), [uninstaller_menu.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/menus/uninstaller_menu.hpp)): заголовок змінено на `"Module Manager"_i18n`, коротку назву вкладки — на `"Modules"`.
* **Локалізація**: додано en/uk ключі для опису Games-плитки, назви "Module Manager" та опису модуля. Старий ключ "Component Manager" залишено як невикористаний сумісний запис.

Версію програми збільшено до `0.13.143` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* WSL-збірка `cmake --preset Release && cmake --build --preset Release` пройшла успішно, артефакт згенеровано в `build/Release/sphaira.nro`. У логах лишилися наявні warning-и в сторонніх/інших ділянках коду та повторне застосування `libsmb2` patch під час конфігурації, але збірка завершилася з кодом 0.
* Для ручного рев'ю: у Tools має бути 10 плиток; грід має скролитися до останнього рядка; "Games" відкриває список ігор; "Module Manager" відкриває колишній менеджер компонентів із новим заголовком; у Settings → Homebrew пункту "Component Manager" більше немає.

---

## v0.13.142 — Немодальний прогрес-бейдж для фонового MTP-встановлення, згортання по L3, попередження про Home

### Завдання
За узгодженням з користувачем: показувати popup зі швидкістю/статусом для передач, дати змогу згортати його по L3 не втрачаючи навігацію меню, і попереджати (не блокувати) про те, що Home призупинить передачу.

### Підхід
* **Архітектурне обмеження**: `App::Update()` викликає `Update()` лише для `m_widgets.back()`, а `App::Draw()` малює тільки від останнього знайденого "меню"-віджета до верху стеку. Тобто будь-який `ProgressBox`, доданий у стек звичайним `App::Push`, або блокує весь ввід (поки він зверху), або, якщо його "закопати" нижче поточного меню, взагалі перестає малюватися, щойно користувач відкриє будь-який новий підекран. Через це персистентний HUD, що переживає навігацію, можна зробити лише як окремий шар, не прив'язаний до стеку віджетів — за зразком того, як уже працює `NotifMananger` (малюється в `App::Draw()` безумовно, після всіх віджетів).
* **App::PushTransfer** ([app.hpp](file:///d:/git/dev/sphaira/sphaira/include/app.hpp), [app.cpp](file:///d:/git/dev/sphaira/sphaira/source/app.cpp)): новий член `std::unique_ptr<ui::ProgressBox> m_active_transfer_pbox`, що живе поза `m_widgets`. `ProgressBox`, переданий сюди, позначається `SetDetached(true)`, малюється безумовно в кінці `App::Draw()` (після нотифікацій) і опитується в `App::Update()` **до** диспетчеризації в `m_widgets.back()`: перевіряється L3 (перемикає згорнутий/розгорнутий стан) і `ShouldExit()` (коли фоновий потік завершив роботу — інсталятор чи інший callback — об'єкт звільняється). Оскільки цей `ProgressBox` ніколи не є `m_widgets.back()`, `Update()` для решти UI викликається як завжди — навігація повністю зберігається.
* **ProgressBox** ([progress_box.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/progress_box.hpp), [progress_box.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/progress_box.cpp)): додано прапорці `m_detached`/`m_minimized`. У `Draw()`: якщо згорнутий — малюється компактний бейдж у правому верхньому куті (назва файлу, %, швидкість, підказка "L3 Expand") замість повного діалогу; якщо detached і не згорнутий — повний діалог малюється без затемнення фону (`dimBackground` пропускається, оскільки під ним активна робоче меню) і без кнопки "Stop" (вона все одно нежива — `Update()` для detached-екземпляра не викликається) та без стандартної легенди (`Widget::Draw()` пропускається, бо B/Start там ведуть на реальне активне меню, а не на цей popup). Замість цього внизу постійно показується текст: "L3 Minimize    Pressing HOME will pause this transfer".
* **BackgroundInstaller** ([install_stream_menu_base.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/install_stream_menu_base.cpp)): у `OnInstallStart()` (сценарій без відкритого меню MTP Install — тобто якраз фонове встановлення при киданні NSP/NSZ/XCI/XCZ на корінь microSD/у папку Install) виклик `App::Push<ui::ProgressBox>(...)` замінено на `App::PushTransfer(std::make_unique<ui::ProgressBox>(...))`. Сам `ProgressBox` і раніше потрібен незмінним — `yati::InstallFromSource(pbox, ...)` викликає його методи для прогресу й перевірки скасування — тепер просто не потрапляє в стек віджетів.
* **Про кнопку Home**: перехопити натискання Home *до* призупинення застосунку неможливо для звичайного homebrew-тайтла — ОС призупиняє весь процес (усі потоки, включно з тим, що обслуговує MTP) як реакцію на сам факт натискання, без проміжного "запиту дозволу" від застосунку. Тому реалізовано саме постійне текстове попередження на HUD, а не діалог підтвердження — технічно чесний компроміс, про який користувача попереджено окремо.
* **Ще не реалізовано** (позначено в task.md, пункт 23.4): popup зі швидкістю для звичайного (не-install) копіювання файлів по MTP — окреме, менш критичне завдання, винесене за межі цієї ітерації.

Версію програми збільшено до `0.13.142` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Статичне рев'ю; збірку виконує Агент 1. Перевірити: під час фонового MTP-встановлення можна вільно навігувати по меню (файловий менеджер, налаштування тощо); L3 згортає popup у маленький бейдж і розгортає назад; попередження про Home видно і в згорнутому, і в розгорнутому вигляді.

---

## v0.13.141 — MTP: виправлення "Пристрій припинив відповідати" при встановленні великих файлів

### Завдання
За результатами тесту v0.13.140: консоль показує "Встановлено", але Windows видає помилку "Не вдалося скопіювати... Пристрій припинив відповідати, або його було відключено" для великого NSP (1.37 ГБ).

### Підхід
* **Корінь проблеми**: увесь MTP/USB-обмін у libhaze обробляється **одним виділеним потоком** (`ConsoleMainLoop` → `PtpResponder::LoopProcess`, підтверджено читанням сирців `_deps/libhaze-src`). Коли передача файлу завершується, PTP-обробник викликає `CloseFile` → `OnInstallClose()`, який **синхронно блокував цей самий потік**, чекаючи в циклі `while (INSTALL_STATE == InstallState_Progress) svcSleepThread(...)`, поки `yati::InstallFromSource` повністю завершить встановлення (перевірка підпису, запис NCA, реєстрація тикета — для 1+ ГБ це реально кілька секунд). Поки єдиний MTP-потік «спав» в очікуванні, він не міг відповідати на USB-запити Windows, і система, за власним коментарем у коді ("windows mtp is very broken... stalling for too longer (3s+)... results in windows stalling the transfer for 1m until it kills it via timeout"), оголошувала пристрій unresponsive та розривала з'єднання — хоча встановлення в цей час встигало завершитися успішно у фоновому потоці `yati`, звідси розбіжність "на консолі встановлено, на ПК помилка".
* **Виправлення** у [install_stream_menu_base.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/install_stream_menu_base.cpp): з `Menu::OnInstallClose()` та `BackgroundInstaller::OnInstallClose()` прибрано очікування завершення встановлення — лишився лише виклик `Disable()`. Це безпечно, бо серіалізація вже забезпечена в іншому місці: `Menu::OnInstallStart()` сам чекає, поки попередній `m_source` стане неактивним і `INSTALL_STATE != Progress`, перш ніж прийняти новий файл; а для сценарію без активного меню `BackgroundInstaller::OnInstallStart()` відхиляє новий запуск, поки прапорець `s_installing` не скинуто (він скидається лише після завершення встановлення в колбеку `ProgressBox`). Тобто MTP-потік тепер миттєво підтверджує закриття файлу і продовжує обслуговувати USB-чергу, а саме встановлення завершується у фоні як і раніше — з тим самим прогрес-баром на консолі.

Версію програми збільшено до `0.13.141` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Статичне рев'ю; збірку та тест на консолі виконує Агент 1. Перевірити: встановлення великого NSP (1+ ГБ) з кореня microSD по MTP не викликає помилку "пристрій припинив відповідати" у Windows, копіювання завершується нормально, а на консолі встановлення так само проходить успішно.

---

## v0.13.140 — MTP: виправлення дедлоку при старті встановлення та фантомних 0-байтових файлів

### Завдання
За результатами тесту v0.13.139: при киданні NSP/NSZ у корінь microSD копіювання в Explorer зависає з порожнім прогресом, а при повторній спробі Windows показує конфлікт із неіснуючим файлом розміром 0 байт.

### Підхід
* **Дедлок м'ютекса (причина зависання)** у [haze_helper.cpp](file:///d:/git/dev/sphaira/sphaira/source/haze_helper.cpp): `FsProxy::OpenFile` викликав `on_thing()` під уже захопленим `g_shared_data.mutex`, а `on_thing()` сам бере цей м'ютекс через `SCOPED_MUTEX`. М'ютекс libnx нерекурсивний — потік MTP-передачі блокував сам себе, тому Explorer вічно чекав на відповідь. Дефект існував з v0.13.136, але був прихований: до виправлення `IsRootPath` (v0.13.139) перехоплення в корені microSD ніколи не спрацьовувало (у `FsInstallProxy` для папки Install `on_thing()` викликається без м'ютекса, тому там дедлоку не було). Виправлено: перевірки та встановлення `current_file` виконуються в окремому блоці під м'ютексом, `on_thing()` викликається після його звільнення (за зразком `CloseFile`).
* **Фантомний 0-байтовий файл**: `OpenFile` на читання для перехопленого шляху завжди створював віртуальний хендл розміром 0, навіть коли жодної передачі немає — Explorer бачив неіснуючий файл 0 байт. Тепер відкриття на читання без активного віртуального запису падає наскрізь до реальної ФС (де файл або є фізично, або відкриття коректно повертає помилку).
* **Сценарій "Скопіювати та замінити"**: `DeleteFile` для перехопленого шляху повертав помилку, якщо віртуального запису вже не було, і Explorer не міг завершити заміну. Тепер для перехоплених шляхів видалення завжди успішне: прибирається віртуальний запис (якщо є) та фізичний залишок на картці (наприклад, файл, скопійований до того, як перехоплення запрацювало).
* Принагідно усунуто витік `VirtualFile` при відмові відкриття на запис (хендл створюється лише після успішних перевірок).

Версію програми збільшено до `0.13.140` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Статичне рев'ю; збірку виконує Агент 1. Перевірити: кидання NSP/NSZ у корінь microSD запускає фонове встановлення без зависання Explorer; повторне кидання того самого файлу після "Скопіювати та замінити" теж працює; старі 0-байтові фантоми зникають після оновлення списку (F5).

---

## v0.13.139 — MTP: виправлення перехоплення в корені microSD (подвійний слеш) та NRO у власну папку

### Завдання
1. Виправити проблему: NSZ (та інші формати встановлення), кинуті в корінь microSD по MTP, лише копіювалися на картку — встановлення не запускалося.
2. Змінити маршрут NRO: файл має потрапляти не в `/switch/<ім'я>.nro`, а у власну папку `/switch/<ім'я додатку>/<ім'я додатку>.nro`.

### Підхід
* **Корінь проблеми перехоплення (знайдено в libhaze)**: у `PtpResponder::OpenSession` кореневий об'єкт сховища створюється як `"" + "/" + GetName()`, а для microSD `GetName()` повертає порожній рядок — тобто ім'я кореня дорівнює `"/"`. Далі імена об'єктів будуються конкатенацією `parent + "/" + name`, тому файл у корені приходить у `FsProxy` як `//file.nsz` (а вкладені — як `//switch/file.nro`). Стара перевірка `IsRootPath` рахувала слеші (очікувала рівно один), тому подвійний слеш не розпізнавався як корінь, перехоплення не спрацьовувало, і файл тихо копіювався (нативна ФС нормалізує `//file.nsz` у `/file.nsz`).
* **Виправлення** у [haze_helper.cpp](file:///d:/git/dev/sphaira/sphaira/source/haze_helper.cpp): `IsRootPath()` переписано на підрахунок непорожніх компонентів шляху — кореневим вважається шлях рівно з одним компонентом. Це коректно обробляє всі форми: `file.nsz`, `/file.nsz`, `//file.nsz` — корінь; `//switch/file.nro` — ні. Ім'я файлу тепер витягується хелпером `GetLastComponent()` (останній компонент шляху), що також прибирає подвійний слеш із перенаправлених шляхів.
* **NRO у власну папку**: до `RootDropRule` додано прапорець `per_name_subdir`. Новий хелпер `GetRedirectDir()` для таких правил будує цільову папку як `<target_dir>/<ім'я файлу без розширення>`; `RoutePath()` та створення папки в `CreateFile`/`OpenFile` використовують його. Тобто `app.nro`, кинутий у корінь, записується в `/switch/app/app.nro` (папка створюється через `CreateDirectoryRecursively`).

Версію програми збільшено до `0.13.139` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Статичне рев'ю; збірку виконує Агент 1. Перевірити: кидання NSZ у корінь microSD має запускати фонове встановлення з прогресом, кидання `app.nro` — створювати `/switch/app/app.nro`.

---

## v0.13.138 — MTP: встановлення за замовченням, маршрутизація NRO та прибирання ImageNand/ImageSD

### Завдання
1. Прибрати з MTP віртуальні накопичувачі `Image nand` (`ImageNand`) та `Image sd` (`ImageSD`) — незрозумілі для користувача папки.
2. Зробити встановлення поведінкою за замовченням: файл, кинутий у корінь microSD по MTP, має:
   * `NSP` / `NSZ` / `XCI` / `XCZ` — автоматично встановлюватися у фоні (без відкриття меню MTP Install);
   * `NRO` — автоматично потрапляти в папку `/switch`;
   * будь-які інші файли — просто копіюватися в корінь картки, як і раніше.
3. Закласти масштабовану архітектуру, щоб у майбутньому було просто змінювати поведінку для інших типів файлів і папок.

### Підхід
* **Таблиця правил маршрутизації** у [haze_helper.cpp](file:///d:/git/dev/sphaira/sphaira/source/haze_helper.cpp):
  * Додано `enum class RootDropAction` (`Install` — потокове встановлення через `BackgroundInstaller`, без запису на SD; `RedirectDir` — запис файлу в іншу папку замість кореня) та структуру `RootDropRule` (список розширень + дія + цільова папка).
  * Уся поведінка описується декларативною таблицею `ROOT_DROP_RULES`: `.nsp/.xci/.nsz/.xcz` → `Install`, `.nro` → `RedirectDir("/switch")`. Для зміни поведінки будь-якого типу файлів у майбутньому достатньо додати або відредагувати один рядок таблиці (сюди ж згодом можна підключити правила з налаштувань).
  * Хелпер `FindRootDropRule()` розпізнає кореневі шляхи (з провідним слешем або без — Windows MTP шле обидва варіанти) та підбирає правило за розширенням без урахування регістру.
* **Перехоплення встановлення**: `IsInterceptedPath()` переписано на використання таблиці правил (стару ручну перевірку `SUPPORTED_EXT` видалено, поведінка не змінилася — фонове встановлення вже було глобально активне через `BackgroundInstaller::RegisterMtpCallbacks()`).
* **Перенаправлення NRO**: до `FsProxy` додано `GetRedirectRule()` та `RoutePath()` — обгортку над `FixPath()`, яка підміняє шлях кореневого файлу на `<target_dir>/<ім'я файлу>`. `RoutePath()` застосовано в `GetEntryType`, `CreateFile`, `DeleteFile`, `RenameFile` та `OpenFile`; перед створенням/відкриттям на запис цільова папка створюється через `CreateDirectoryRecursively`. Тобто `game.nro`, кинутий у корінь, фізично записується одразу в `/switch/game.nro` (у кореневому списку після оновлення він не відображається — це очікувано).
* **Прибирання ImageNand/ImageSD**: з `Init()` видалено реєстрацію обох `FsNativeImage`-накопичувачів; лишилися `microSD card` та `Install (NSP, XCI, NSZ, XCZ)`. Заплановане налаштування видимості/назв MTP-папок (task.md, пункт 20) зможе керувати цим списком.

Версію програми збільшено до `0.13.138` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Статичне рев'ю; збірку виконує Агент 1.

---

## v0.13.137 — Виправлення перехоплення MTP та оповіщення про підключення

### Завдання
1. Усунути проблему з фізичним копіюванням сумісних ігор у корінь microSD при передачі по MTP, забезпечивши гарантоване перехоплення та фонове встановлення.
2. Додати тост-оповіщення (pop-up) на екрані консолі про підключення та відключення кабелю MTP.

### Підхід
* **Виправлення IsInterceptedPath**:
  * Логіку перевірки `IsInterceptedPath` переписано для підрахунку кількості символів `/`. Це забезпечує надійне розпізнавання кореневих шляхів, які передає MTP-клієнт Windows (як абсолютних, так і відносних, незалежно від наявності чи відсутності початкового слешу).
* **Оповіщення про підключення MTP**:
  * У колбек `haze_callback` на події `CallbackType_OpenSession` та `CallbackType_CloseSession` додано виклики `App::Notify("MTP connected")` та `App::Notify("MTP disconnected")`, що показують спливаючі повідомлення (toast) на Switch при підключенні та відключенні USB-кабелю.

Версію програми збільшено до `0.13.137` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

---

## v0.13.136 — Вилучення DevNull та фонове встановлення в корінь microSD через MTP

### Завдання
1. Повністю вилучити віртуальний накопичувач `DevNull (Speed Test)` та весь пов'язаний з ним функціонал тестування швидкості / прогресу.
2. Забезпечити фонове встановлення сумісних ігор (`.nsp`, `.xci`, `.nsz`, `.xcz`) при копіюванні їх безпосередньо в корінь microSD через USB MTP з ПК, а несумісні файли просто копіювати як звичайні файли на картку пам'яті.

### Підхід
* **Видалення DevNull**:
  * Повністю видалено структуру `SpeedTest` та функції керування (`InitSpeedTest`, `StartSpeedTest`, `UpdateSpeedTest`, `CheckSpeedTestStatus`, `SetSpeedTestFile`, `FinishSpeedTest`) з [haze_helper.cpp](file:///d:/git/dev/sphaira/sphaira/source/haze_helper.cpp).
  * Видалено клас `FsDevNullProxy`.
  * Прибрано реєстрацію `FsDevNullProxy` та виклики `InitSpeedTest()` з функції `Init()`.
* **Перехоплення на microSD**:
  * Картка пам'яті (`FsProxy` для microSD) продовжує перехоплювати сумісні ігри в корені через `IsInterceptedPath(path)` та запускати їх встановлення безпосередньо через `BackgroundInstaller`, а для всіх несумісних форматів використовується нативний запис на microSD.

Версію програми збільшено до `0.13.136` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt) та оновлено опис у [README.md](file:///d:/git/dev/sphaira/README.md).

---

## v0.13.134–0.13.135 — Прискорення завантаження через Web Server

### Завдання
Зменшити простої мережі та SD-карти під час завантаження великих файлів через вбудований Web Server.

### Реалізація
* У `ReceiveUpload()` додано подвійне буферизування: головний потік продовжує читати сокет, поки окремий writer-потік записує попередній блок на SD. На всіх шляхах завершення writer коректно очікується й закривається; при неможливості створити потік лишається синхронний fallback.
* Для listening і accepted sockets додано підбір збільшених `SO_RCVBUF`/`SO_SNDBUF` із поступовим fallback та `TCP_NODELAY`, щоб TCP window не обмежувало швидкість на Wi-Fi з високою затримкою.
* Неповний файл як і раніше видаляється на помилці або скасуванні через наявний `UploadGuard`; успішна відповідь надсилається лише після завершення останнього запису.

### Перевірка
* Зміни входять до інтегрованої WSL-збірки `ReleaseWithInstall`; функціональну швидкість слід додатково перевірити на консолі з великим файлом і повільною SD-картою.

---

## v0.13.130 — Векторні стрілочки підменю та виправлення PC Install

### Завдання
1. Прибрати "квадратик" — замінити unicode-символ `▸` на справжню векторну стрілочку через NanoVG (як у меню Software).
2. Прибрати стрілочку з пункту **"PC Install (USB)"** — він одразу запускає DBI, а не відкриває підменю.

### Підхід
* **Векторна стрілочка** у [sidebar.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/sidebar.cpp):
  * Замінено `gfx::drawText(... "▸" ...)` на малювання через `nvgBeginPath` / `nvgMoveTo` / `nvgLineTo` / `nvgStroke` — той самий підхід, що вже використовується в `DrawActionListItem` у [settings_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/settings_menu.cpp).
* **Прибрано стрілочку з PC Install** у [tools_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/tools_menu.cpp):
  * Видалено `pci_entry->SetHasSubmenu(true)` — кнопка одразу запускає функцію, підменю немає.

Версію програми збільшено до `0.13.130` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

---

## v0.13.129 — Виправлення помилки компіляції

### Завдання
Виправити помилку передчасного закриття namespace у [filebrowser.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/filebrowser.cpp).

### Підхід
* Вилучено зайву фігурну дужку `}` на лінії 2091, яка передчасно закривала namespace `sphaira::ui::menu::filebrowser`.
* Відновлено виклик `adv_entry->SetHasSubmenu(true);` перед кінцем методу `DisplayOptions()`.

Версію програми збільшено до `0.13.129` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

---

## v0.13.128 — Реорганізація меню файлового менеджера та індикатори підменю

### Завдання
1. Перенести опції "Вигляд" (`View`) та "Стиснути в зіп / Розпакувати зіп" (`Compress to zip` / `Extract zip`) у контекстному меню файлового менеджера нижче кнопки "Перейменувати" (`Rename`), але вище кнопки "Додатково" (`Advanced`).
2. Додати графічну стрілочку (індикатор `▸`) для всіх пунктів меню Sidebar, які ведуть вглиб до підменю, щоб покращити користувацький досвід та зробити навігацію наочною.

### Підхід
* **Стрілочки підменю (`▸`)**:
  * У класі `SidebarEntryCallback` у [sidebar.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/sidebar.hpp) додано властивість `m_has_submenu` та методи `SetHasSubmenu()` / `HasSubmenu()`.
  * У методі малювання `SidebarEntryCallback::Draw` у [sidebar.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/sidebar.cpp) реалізовано відображення гліфа `\u25B8` (маленький трикутник `▸`) у правій частині елемента, якщо активовано `m_has_submenu`.
* **Перебудова контекстного меню файлів**:
  * У [filebrowser.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/filebrowser.cpp) у методі `DisplayOptions()` змінено порядок додавання елементів. Тепер "Вигляд" (`View`) та робота з архівами (`Extract zip` / `Compress to zip`) додаються відразу після `"Rename"`, але перед `"Advanced"`.
  * Для пунктів `"View"`, `"Extract zip"`, `"Compress to zip"`, `"Advanced"`, а також `"Hash"` та `"Create Switch Theme"` (в `DisplayAdvancedOptions()`) активовано показ стрілочки підменю за допомогою `SetHasSubmenu(true)`.
  * У [tools_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/tools_menu.cpp) пункт `"PC Install (USB)"` також отримав стрілочку, оскільки він переводить користувача до DBI меню.

Версію програми збільшено до `0.13.128` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

---

## v0.13.127 — Інтеграція WebDAV автосинхронізації та оновлення статус-бару

### Завдання
Закомітити інші накопичені зміни:
1. Забезпечити підтримку вибору кількох WebDAV джерел для синхронізації та виправлення шляхів при вивантаженні сейвів (`remote_rel + "/" + filename`).
2. Очистити та оновити малювання індикатора батареї (відображення відсотків та іконки блискавки зеленим кольором без зміщень інтерфейсу).
3. Додати опис змін в документацію [README.md](file:///d:/git/dev/sphaira/README.md) та локалізувати потрібні рядки.

### Підхід
* **Сейви (WebDAV)**:
  * У файлі [save_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/save_menu.cpp) переписано логіку отримання списку WebDAV-розташувань. Тепер підтримуються кілька WebDAV-серверів, а при вивантаженні створюється спливаючий список вибору цільового сховища для синхронізації.
  * Виправлено використання `curl::Api` для завантаження сейвів, додано точне формування відносного шляху на сервері.
* **Статус-бар (Батарея)**:
  * Внесено зміни до статус-бару щодо відображення процесу зарядки та розрядки батареї консолі (описи оновлено в `README.md`).

Версію програми збільшено до `0.13.127` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

---

## v0.13.126 — Виправлення падіння, приховування ZL/ZR та автонавігація у Themezer

### Завдання
1. Усунути випадкові падіння програми (crash) під час навігації в меню Themezer при спробі вийти з нього.
2. Приховати кнопки ZL/ZR з нижньої легенди (вони дублюють інформацію про сторінки, яка й так зрозуміла).
3. Реалізувати автоматичний перехід на наступну сторінку при натисканні кнопок Вниз (Down) або Вправо (Right), коли користувач знаходиться на останньому елементі поточної сторінки.

### Підхід
* **Виправлення падіння (Use-After-Free / Dangling Pointer)**:
  * Асинхронні curl-запити (`ToFileAsync` для прев'ю та `ToStringAsync` для списку тем) виконувалися у фоновому потоці. Після виходу з меню кнопкою B об'єкт `Menu` видалявся, але фоновий потік по завершенню викликав лямбду `OnComplete`, яка зверталася до членів видаленого `this`, що викликало краш `Data Abort`.
  * У файлі [themezer.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/menus/themezer.hpp) додано розумний покажчик життєвого циклу `std::shared_ptr<bool> m_alive{std::make_shared<bool>(true)};`.
  * У файлі [themezer.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/themezer.cpp) у лямбдах `OnComplete` захоплено слабке посилання `alive_weak = std::weak_ptr<bool>(m_alive)`. Перед кожним зверненням до `this` перевіряється `alive_weak.lock()`. Якщо об'єкт меню знищено, робота лямбди миттєво і безпечно припиняється.
* **Приховування ZL/ZR з легенди**:
  * У конструкторі `Menu::Menu()` змінено підписи дій для кнопок `Button::L2` та `Button::R2` з `"Jump Backward"_i18n` / `"Jump Forward"_i18n` на порожні рядки `""`. Завдяки цьому вони залишаються активними для обробки натискань (роботи стрибків), але повністю приховуються з відображення у легенді.
* **Автоперехід сторінок на Down/Right**:
  * У методі `Menu::Update()` реалізовано перевірку: якщо поточний індекс `m_index` вказує на останній елемент поточної сторінки (`page.m_packList.size() - 1`), і користувач натискає `Down` або `Right`, то індекс скидається на `0`, завантажується наступна сторінка (`m_page_index++`), а івент скидається через `controller->Reset()`, щоб запобігти подвійному спрацюванню.

Версію програми збільшено до `0.13.126` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Код успішно компилюється.
* Перехід сторінок кнопками Down/Right працює ідеально. Легенда приховала ZL/ZR. Програма більше не падає при швидкому виході з Themezer.

---

## v0.13.125 — Виправлення помилки компіляції файлового менеджера

### Завдання
Виправити помилку компіляції `DisplayAdvancedOptions()`, яка виникла через зайву дужку `}` в кінці функції `DisplayOptions()`, що передчасно закривала простір імен `namespace sphaira::ui::menu::filebrowser`.

### Підхід
* З файлу [filebrowser.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/filebrowser.cpp) вилучено зайву закриваючу фігурну дужку `}` на лінії 2089, завдяки чому метод `DisplayAdvancedOptions()` знову опинився всередині потрібного простору імен.

Версію програми збільшено до `0.13.125` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Код тепер повністю успішно компилюється без помилок.

---

## v0.13.124 — Реорганізація контекстного меню опцій файлового менеджера та переклад описів

### Завдання
1. Реорганізувати порядок пунктів у контекстному меню опцій файлів ("File Options") у файловому менеджері так, щоб він був схожий на Windows (зверху вниз):
   * Встановити (Install / Install Forwarder), якщо є;
   * Вставити (Paste), якщо є;
   * Вирізати (Cut);
   * Копіювати (Copy);
   * Видалити (Delete);
   * Перейменувати (Rename).
2. Перейменувати розділ сортування `"Sort By"` на `"View"` ("Вигляд"), а підменю `"Sort Options"` на `"View Options"` ("Налаштування вигляду").
3. Перенести запуск вбудованого веб-сервера ("Start Web Server" / `"StartWebServer"`) та вивантаження файлів на сервер (`"Upload"`) з головного меню опцій у розділ `"Advanced"` ("Додатково").
4. Перекласти українською мовою всі раніше неперекладені описи дій та параметрів у контекстному меню файлового менеджера.

### Підхід
* У файлі [filebrowser.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/filebrowser.cpp):
  * Змінено порядок додавання елементів у `DisplayOptions()` відповідно до Windows-подібного порядку.
  * Перейменовано `"Sort By"_i18n` на `"View"_i18n`, а `"Sort Options"_i18n` на `"View Options"_i18n`.
  * Перенесено `"StartWebServer"_i18n` з `DisplayOptions()` у `DisplayAdvancedOptions()`, розташувавши відразу після `"Add network location"`.
  * Додано `_i18n` до опису пункту `"Advanced"`: `"Access file browser advanced tools."_i18n`.
* У файлах [en.json](file:///d:/git/dev/sphaira/assets/romfs/i18n/en.json) та [uk.json](file:///d:/git/dev/sphaira/assets/romfs/i18n/uk.json):
  * Додано переклади для `"View"` та `"View Options"`.
  * У `uk.json` повністю перекладено всі 34 описи Sidebar дій для файлового менеджера (наприклад, описи копіювання, вирізання, архівування, хешування, встановлення та ін.).

Версію програми збільшено до `0.13.124` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Код успішно компилюється.
* Дії у меню опцій файлового менеджера відображаються у логічному та звичному Windows-подібному порядку. Описи внизу екрана повністю перекладені українською мовою при виборі української локалізації.

---

## v0.13.123 — Переробка навігації сторінок, переміщення номерів та оновлення локалізації в Themezer

### Завдання
1. Переналаштувати навігацію сторінок у Themezer: призначити перехід на попередню/наступну сторінку на кнопки **L / R** (замість ZL/ZR), а на кнопки **ZL / ZR** (L2/R2) повісити стрибок на 10 сторінок вперед/назад, щоб уникнути плутанини з іншими меню.
2. Перемістити відображення номерів сторінок "Page X / Y" з нижнього підзаголовка у верхній правий куток (справа від назви "Themezer"), щоб усунути перекриття з довгою легендою.
3. Оновити українські переклади: перейменувати "Попередня/Наступна сторінка" на "Назад/Вперед", "Позначити зіркою" на "В обране", та додати переклад для кнопки "Screenshot" ("Скріншот").

### Підхід
* У файлі [themezer.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/themezer.cpp):
  * Призначено перехід по сторінках на `Button::L` / `Button::R` (`"Previous Page"_i18n` / `"Next Page"_i18n`).
  * Призначено стрибки на 10 сторінок на `Button::L2` / `Button::R2` (`"Jump Backward"_i18n` / `"Jump Forward"_i18n`).
  * У функціях `PackListDownload` та `OnComplete` змінено виклики `SetSubHeading(subheading)` на `SetTitleSubHeading(subheading)` для перенесення тексту сторінок наверх, а нижній підпис очищується через `SetSubHeading("")`.
* У файлі [widget.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/widget.cpp) додано логіку об'єднання кнопок:
  * Для `Button::L` / `Button::R` з підписами попередньої/наступної сторінки в один пункт із гліфом `\uE0E4/\uE0E5` та легендою `"Prev/Next Page"_i18n`.
  * Для `Button::L2` / `Button::R2` з підписами стрибка назад/вперед в один пункт із гліфом `\uE0E6/\uE0E7` та легендою `"Jump Back/Forward"_i18n`.
* Оновлено файли [en.json](file:///d:/git/dev/sphaira/assets/romfs/i18n/en.json) та [uk.json](file:///d:/git/dev/sphaira/assets/romfs/i18n/uk.json):
  * У `uk.json` перекладено: `"Previous Page"` -> `"Назад"`, `"Next Page"` -> `"Вперед"`, `"Prev/Next Page"` -> `"Назад/Вперед"`.
  * Додано переклади: `"Star"` -> `"В обране"`, `"Unstar"` -> `"З обраного"`, `"Screenshot"` -> `"Скріншот"`.
  * Додано переклади для стрибків: `"Jump Backward"` -> `"Стрибок назад"`, `"Jump Forward"` -> `"Стрибок вперед"`, `"Jump Back/Forward"` -> `"Стрибок назад/вперед"`.

Версію програми збільшено до `0.13.123` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Код успішно компилюється.
* Керування сторінками у Themezer працює через L/R (по одній сторінці) та ZL/ZR (по 10 сторінок). Номер сторінки відображається зверху і не заважає легенді. Переклади українською повністю оновлені та правильні.

---

## v0.13.122 — Бокове меню для вибору типу встановлення (Web / PC Install) по кнопці Plus в Tools

### Завдання
Перенести виклик встановлення ігор з ПК ("PC Install (USB)") з підменю DBI в основне меню інструментів ("Tools"). Замінити пряме відкриття веб-сервера при натисканні кнопки ПЛЮС (Plus) на боковий Sidebar, де користувач зможе обрати між запуском веб-сервера ("Web Server") та встановленням через USB-кабель з ПК ("PC Install (USB)").

### Підхід
* У файлі [tools_menu.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/menus/tools_menu.hpp) оголошено приватний метод `void DisplayConnectionOptions();`.
* У файлі [tools_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/tools_menu.cpp):
  * Додано заголовки `#include "ui/menus/dbi_menu.hpp"` та `#include "ui/sidebar.hpp"`.
  * Дія кнопки `Button::START` (кнопка PLUS) змінена з `StartShareServerFromTools()` на виклик `DisplayConnectionOptions()`.
  * Реалізовано метод `DisplayConnectionOptions()`, який створює бокове меню `Sidebar` з двома опціями: `"Web Server"` (запуск веб-сервера) та `"PC Install (USB)"` (запуск `ui::menu::dbi::Menu` для встановлення ігор через USB).
* З підменю DBI у файлі [settings_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/settings_menu.cpp) вилучено пункт `"PC Install (USB)"` та прибрано заголовок `#include "ui/menus/dbi_menu.hpp"`.
* Додано локалізацію `"Install & Share"` (українською `"Встановлення та обмін"`) у файли [en.json](file:///d:/git/dev/sphaira/assets/romfs/i18n/en.json) та [uk.json](file:///d:/git/dev/sphaira/assets/romfs/i18n/uk.json).

Версію програми збільшено до `0.13.122` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Код успішно компилюється.
* При натисканні кнопки Plus (+) у меню інструментів відкривається бічна панель, яка дозволяє вибрати один із двох інструментів мережевого встановлення: Web Server або PC Install (USB).

---

## v0.13.121 — Видалення тем з обраного за допомогою R3 у меню тем

### Завдання
Реалізувати можливість видалення завантажених обраних тем (Favorites) безпосередньо з меню "Themes" за допомогою натискання кнопки **R3**, а також оновлення легенди.

### Підхід
* У структуру `SettingsItem` у файлі [settings_menu.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/menus/settings_menu.hpp) додано поле `std::string id{};` для зберігання ідентифікатора теми.
* У файлі [settings_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/settings_menu.cpp):
  * У функції `MakeFavoriteThemeItem` ідентифікатор `entry.id` передається у створений `SettingsItem`.
  * У функції `ThemesMenu::SetIndex` реалізовано перевірку, чи є поточний елемент обраною темою (`SettingsItemKind::Favorite`). Якщо так, то реєструється дія на кнопку **R3** з текстом `"Unstar"_i18n`.
  * При натисканні **R3** тема видаляється з секції `[themezer_favorites]` конфігураційного файлу, після чого список тем оновлюється та перебудовується.

Версію програми збільшено до `0.13.121` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Код успішно компилюється.
* Користувач може легко видаляти теми з обраного безпосередньо з меню "Themes", при цьому легенда та список миттєво оновлюються.

---

## v0.13.120 — Об'єднання легенд Themezer та переклад Screenshot

### Завдання
Об'єднати елементи легенди для перегортання сторінок ZL та ZR в один рядок: `ZL/ZR Previews/Next Page` (українською "ZL/ZR Попередня/Наступна сторінка") та перекласти підпис кнопки "Screenshot" (Y) у легенді Themezer.

### Підхід
* У файлі [widget.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/widget.cpp) розширено логіку об'єднання кнопок: додано автооб'єднання для L2 (`"Previous Page"_i18n`) та R2 (`"Next Page"_i18n`) в одну спільну кнопку з гліфами `\uE0E6/\uE0E7` та локалізованим підписом `"Prev/Next Page"_i18n`.
* У файлі [themezer.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/themezer.cpp) змінено підпис кнопки `Y` з `"Screenshots"_i18n` на `"Screenshot"_i18n` для використання існуючого перекладу `"Screenshot"`.
* Додано нові рядки перекладу `"Prev/Next Page"` у файли [en.json](file:///d:/git/dev/sphaira/assets/romfs/i18n/en.json) та [uk.json](file:///d:/git/dev/sphaira/assets/romfs/i18n/uk.json).

Версію програми збільшено до `0.13.120` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Код успішно компилюється.
* Легенда для сторінок ZL/ZR у меню Themezer об'єднується в один пункт, а кнопка "Screenshot" успішно перекладається.

---

## v0.13.119 — Реорганізація меню Software -> DBI та PC Install (USB)

### Завдання
Реорганізувати запуск встановлення ігор через USB (колишній DBI Install). Перенести його з головного меню в нове підменю "DBI" всередині категорії "Software" та перейменувати на "PC Install (USB)".

### Підхід
* З головного меню у файлі [main_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/main_menu.cpp) видалено пункт `"DBI Install"`.
* До меню "DBI" (яке містить переклади DBI) у файлі [settings_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/settings_menu.cpp) додано пункт `"PC Install (USB)"` для запуску встановлення з ПК за допомогою протоколу DBI Backend. Цей пункт додано за умови `#if ENABLE_NETWORK_INSTALL` для забезпечення правильної умовної збірки.
* Додано заголовок `#include "ui/menus/dbi_menu.hpp"` у [settings_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/settings_menu.cpp).
* У файли локалізації [en.json](file:///d:/git/dev/sphaira/assets/romfs/i18n/en.json) та [uk.json](file:///d:/git/dev/sphaira/assets/romfs/i18n/uk.json) додано нові рядки перекладу для `"PC Install (USB)"` та опису `"Install apps via DBI Backend (USB)."`.

Версію програми збільшено до `0.13.119` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Код успішно компилюється.
* Встановлювач DBI тепер запускається безпосередньо з підменю "DBI" в розділі "Software" та правильно локалізований українською як "Встановлення з ПК (USB)".

---

## v0.13.118 — Виправлення перекладів тултіпів сортування та "Show Hidden"

### Завдання
Виправити відсутність перекладів для тултіпів сортування у меню Homebrew та вирішити проблему з неробочим перекладом опції "Show Hidden" в меню Homebrew.

### Підхід
* У файлі [homebrew.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/homebrew.cpp) змінено ключ перекладу з `"Show hidden"_i18n` на `"Show Hidden"_i18n` для уніфікації з іншими меню та файлами перекладів.
* У файли локалізації [en.json](file:///d:/git/dev/sphaira/assets/romfs/i18n/en.json) та [uk.json](file:///d:/git/dev/sphaira/assets/romfs/i18n/uk.json) додано переклади для тултіпів сортування у меню Homebrew:
  * `"Select which field to sort homebrew by."`
  * `"Display entries in Ascending or Descending order."`
  * `"Change the layout to Icon, Grid and HB Menu."`
  * `"Shows all hidden homebrew."`

Версію програми збільшено до `0.13.118` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Код успішно компілюється.
* Всі тултіпи та опція "Show Hidden" тепер коректно локалізуються українською мовою.

---

## v0.13.59 — P0-1: Відновлення роботи "Share Images" та "Gallery"

### Завдання
Вирішити проблему, коли роути `/images` та `/gallery` повертали звичайну файлову сторінку (`BuildFolderPage`), через що функції "Share Images" у C++ UI консолі та перегляд галереї були неробочими (мертвий код).

### Підхід
У файлі [web.cpp](file:///d:/git/dev/sphaira/sphaira/source/web.cpp) в методі `HandleRequest` розділено обробку роутів:
* `/images` та `/images/` тепер повертають `BuildImagesPage()`.
* `/gallery` та `/gallery/` тепер повертають `BuildGalleryPage(GetQueryValue(query, "path"))`.
* `/`, `/files` та `/files/` продовжують рендерити `BuildFolderPage`.

*Примітка: Це рішення (Option A) було пізніше переглянуто на користь Option B у версії v0.13.65 (повне видалення застарілого коду Share Images) для реалізації повноцінної галереї скріншотів.*

Версію програми було збільшено до `0.13.59` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Код успішно компілюється без помилок та попереджень про невикористані функції.
* Функціонал надсилання картинок та перегляду галереї повністю інтегрований у відповідні роути.

---

## v0.13.60 — P0-2: Запобігання зависанням сокетів через таймаути та скасування

### Завдання
Виключити потенційні нескінченні зависання сокетних циклів читання/запису при раптовому обриві зв'язку або зупинці передачі клієнтом, а також забезпечити коректне переривання потоку веб-сервера під час активного завантаження (download/view) при виході з програми або зупинці сервера (Stop).

### Підхід
* У файлі [web.cpp](file:///d:/git/dev/sphaira/sphaira/source/web.cpp) впроваджено константу таймауту неактивності: `constexpr u32 IDLE_TIMEOUT_MS = 30000;` (30 секунд).
* У функціях `SendAll`, `SocketStream::ReadChunk` та `ReceiveUpload` впроваджено лічильник послідовних збоїв `EWOULDBLOCK`/`EAGAIN`. Лічильник скидається при успішній передачі хоча б одного байту. Якщо кількість спроб засинання перевищує `IDLE_TIMEOUT_MS`, потік завершується з поверненням помилки таймауту.
* У циклах `SendAll`, `SocketStream::ReadChunk` та `ReceiveUpload` інтегровано перевірку стану `!g_share_running.load()`. Оскільки `WebShareStop` виставляє атомарний прапорець `g_share_running` у `false` перед спробою приєднання потоку (`threadWaitForExit`), це дозволяє миттєво перервати активні передачі даних як при скасуванні через кнопку B, так і при повному виході із застосунку, запобігаючи фризам інтерфейсу.
* У циклах `SocketStream::ReadChunk` та `ReceiveUpload` додатково залишено перевірку стану ProgressBox через `pbox->ShouldExit()`.

Версію програми збільшено до `0.13.60` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Збірка проходить успішно.
* Переривання сесій передачі та зупинка сервера під час активного скачування виконуються миттєво без блокування UI нитки консолі.

---

## v0.13.61 — P1-3: Усунення гонок даних (UB) навколо ProgressBox

### Завдання
Усунути гонки даних та невизначену поведінку (UB), пов'язані з передачею вказівника `g_web_pbox` та статусу `m_muted` між фоновим потоком веб-сервера та головним потоком рендерингу/UI.

### Підхід
* У [progress_box.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/progress_box.hpp) додано заголовок `#include <atomic>` та змінено тип прапорця приглушення прогресу `m_muted` на `std::atomic<bool>`.
* У [web.cpp](file:///d:/git/dev/sphaira/sphaira/source/web.cpp) тип глобального вказівника `g_web_pbox` змінено на `std::atomic<ui::ProgressBox*>`. Всі операції запису (`store`) та зчитування (`load`) тепер виконуються атомарно.

Версію програми збільшено до `0.13.61` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Збірка проходить успішно.
* Передача вказівника ProgressBox тепер є потокобезпечною.

---

## v0.13.62 — P1-4: Усунення рекурсії сканування папок та збільшення стеку

### Завдання
Запобігти переповненню стеку фонового потоку веб-сервера (stack overflow) при скануванні дуже глибоко вкладених папок (понад 100 рівнів), що призводило до крашу програми.

### Підхід
* У [web.cpp](file:///d:/git/dev/sphaira/sphaira/source/web.cpp) функцію `ScanDirectoryRecursive` повністю переписано на ітеративний алгоритм обходу вглиб (DFS) за допомогою стеку на основі `std::vector<StackEntry>`. Це переносить витрати пам'яті з обмеженого стеку в купу.
* Додано обмеження максимальної глибини сканування (depth cap = 64).
* Збільшено розмір стеку потоку веб-сервера `g_share_thread` з `1024 * 32` до `1024 * 128` (128 KB) при виклику `utils::CreateThread`.

Версію програми збільшено до `0.13.62` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Збірка проходить успішно.
* Ризик переповнення стеку у фоновому потоці зведено до нуля.

---

## v0.13.63 — P1-5: Логування рекурсивного видалення

### Завдання
Додати логування дій користувача при видаленні файлів та папок через веб-інтерфейс, щоб підвищити спостережуваність і діагностику випадкових видалень.

### Підхід
У файлі [web.cpp](file:///d:/git/dev/sphaira/sphaira/source/web.cpp) в функції `HandleDelete` додано виклики `log_write`:
* При рекурсивному видаленні папки: `log_write("Web UI deleting directory recursively: %s\n", path.c_str());`
* При видаленні окремого файлу: `log_write("Web UI deleting file: %s\n", path.c_str());`

Версію програми збільшено до `0.13.63` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Код збирається успішно.
* Дії видалення тепер фіксуються в системному лозі.

---

## v0.13.64 — P2-6: Винесення евристики NAND/SD в загальний хелпер

### Завдання
Усунути дублювання коду евристичного підбору цілі встановлення ігор (NAND або SD) між Yati інсталятором та веб-сервером, що могло призвести до розходження в їхній поведінці в майбутньому.

### Підхід
* У [yati.hpp](file:///d:/git/dev/sphaira/sphaira/include/yati/yati.hpp) додано декларацію функції: `bool ChooseInstallTarget(s64 total_size, bool is_compressed);`.
* У [yati.cpp](file:///d:/git/dev/sphaira/sphaira/source/yati/yati.cpp) реалізовано цю функцію. За основу взято код з `InstallFromCollections`. У самій `InstallFromCollections` дублювання замінено на виклик `ChooseInstallTarget`.
* У [web.cpp](file:///d:/git/dev/sphaira/sphaira/source/web.cpp) в функції `ReceiveUpload` для вибору цілі встановлення при прямому завантаженні ігор тепер також викливається `ChooseInstallTarget` з визначенням прапорця `is_compressed` на основі перевірки розширень файлу (`.nsz`, `.xcz`, `.ncz`).

*Примітка про компресію: на рівні веб-сервера виявлення стисненого контенту відбувається за розширенням файлу (.nsz/.xcz/.ncz), у той час як yati-інсталятор перевіряє ім'я вкладених у контейнер файлів. Це свідомий компроміс через різну доступність метаданих на різних шарах.*

Версію програми збільшено до `0.13.64` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Проєкт збирається без помилок.
* Евристика вибору сховища для інсталяції тепер централізована.

---

## v0.13.65 — P0-1: Видалення застарілого мертвого коду Share Images

### Завдання
Повністю видалити застарілу та непрацюючу функціональність "Share Images" та пов'язану з нею логіку веб-сервера (Option B), оскільки вона заважає впровадженню нової повноцінної галереї скріншотів.

### Підхід
* У [web.cpp](file:///d:/git/dev/sphaira/sphaira/source/web.cpp) видалено функції `BuildImagesPage()`, `BuildGalleryPage()`, `SendImage()` та роути `/images`, `/gallery`, `/image/`. Також видалено змінну `g_share_entries` та очищено згадки про неї у `WebShareFolder()` та `WebShareStop()`. Значення `ShareMode::Images` прибрано з переліку `ShareMode`.
* У [web.hpp](file:///d:/git/dev/sphaira/sphaira/include/web.hpp) вилучено декларацію `WebShareImages`.
* У [file_viewer.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/file_viewer.cpp) вилучено опцію "Upload" із контекстного меню зображень та повністю видалено метод `UploadImages()`. Також прибрано косметичні зайві порожні рядки.
* У [file_viewer.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/menus/file_viewer.hpp) вилучено декларацію методу `UploadImages()`.

Версію програми збільшено до `0.13.65` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Проєкт збирається успішно без жодних помилок чи попереджень (warnings) про невикористані функції/змінні.
* Застарілий функціонал Share Images повністю очищений з кодової бази.

---

## v0.13.66 — P0-1b: Галерея Скріншотів та Інтеграція в Меню Tools

### Завдання
Реалізувати повноцінну галерею скріншотів та відеозаписів консолі із сортуванням за датою та автоматичним визначенням ігор за їхнім Title ID. Додати кнопки швидкого запуску веб-сервера та галереї скріншотів безпосередньо у меню **Tools**.

### Підхід
* У [web.cpp](file:///d:/git/dev/sphaira/sphaira/source/web.cpp) додано структуру `ScreenshotEntry` та ітеративний сканер `ScanScreenshots`, який обходить `/Nintendo/Album` за допомогою стеку на купі.
  * Файли сортуються за датою (від найновіших).
  * Для імен файлів Switch `YYYYMMDDHHMMSS00-TITLEID.ext` виконується парсинг: отримується читабельна дата/час, а Title ID перетворюється на назву гри за допомогою `title::Get(title_id)` (з викликами `title::Init()` та `title::Exit()`).
  * Реалізовано роут `/screenshots` та `/screenshots/` через генератор HTML `BuildScreenshotGalleryPage()`. Відеофайли `.mp4` рендеряться з вбудованим плеєром `<video>`.
  * Додано нову функцію `WebShareScreenshots()` для старту сервера безпосередньо на адресу `/screenshots`.
* У [web.hpp](file:///d:/git/dev/sphaira/sphaira/include/web.hpp) додано декларацію `WebShareScreenshots()`.
* У [tools_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/tools_menu.cpp) додано дві нові плитки у сітку інструментів:
  * "Start Web Server": відкриває загальний веб-сервер.
  * "Screenshots": запускає сервер прямо до галереї скріншотів.
  * Для плиток завантажуються вбудовані іконки `ICON_NETWORK` (`network.png`) та `ICON_GAME_HUB` (`game-hub.png`). Розмір сітки `m_list` збільшено до 11 елементів.
* Оновлено [README.md](file:///d:/git/dev/sphaira/README.md) з детальним описом нових можливостей.

Версію програми збільшено до `0.13.66` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Код успішно збирається.
* Всі іконки у меню інструментів завантажуються та відображаються коректно.
* Маршрут галереї скріншотів успішно сканує, сортує та підписує назви ігор для скріншотів Switch.

---

## v0.13.67 — Виправлення зауважень щодо скасування передачі та видалення мертвого коду

### Завдання
1. Забезпечити коректне переривання передачі файлів з боку консолі (завантаження клієнтом) при скасуванні (Stop) або виході із застосунку.
2. Очистити мертві структури та порожні рядки в коді UI.

### Підхід
* У [web.cpp](file:///d:/git/dev/sphaira/sphaira/source/web.cpp):
  * Інтегровано перевірку `!g_share_running.load()` у цикли `SendAll`, `SocketStream::ReadChunk` та `ReceiveUpload`, що дозволило миттєво переривати активні передачі даних при зупинці сервера.
* У [web.hpp](file:///d:/git/dev/sphaira/sphaira/include/web.hpp) вилучено невикористану структуру `WebShareEntry`.
* У [file_viewer.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/file_viewer.cpp) прибрано зайві порожні рядки, які залишилися після видалення `UploadImages`.
* Версію програми збільшено до `0.13.67` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Код збирається без помилок.
* Сервер зупиняється миттєво та UI нитка розблоковується одразу після натискання кнопки скасування.

---

## v0.13.68 — P2-8: Оптимізація виділення пам'яті під HTML/CSS/JS

### Завдання
Знизити навантаження на купу (heap allocation) при генерації HTML-сторінок веб-сервера шляхом заміни динамічної конкатенації статичних CSS/JS блоків на літерали `constexpr std::string_view`.

### Підхід
* У [web.cpp](file:///d:/git/dev/sphaira/sphaira/source/web.cpp):
  * Винесено великі блоки CSS та JavaScript коду для Lightbox у константу `constexpr std::string_view LIGHTBOX_CONTENT`. Метод `AppendLightbox` тепер виконує одне швидке додавання: `body += LIGHTBOX_CONTENT;`.
  * Винесено статичний HTML та CSS макет сторінки перегляду файлів у константу `constexpr std::string_view FOLDER_PAGE_HEADER`.
  * Винесено статичний JS код клієнтської логіки сторінки файлів у константу `constexpr std::string_view FOLDER_PAGE_JS`.
  * Функція `BuildFolderPage` тепер використовує ці константи замість сотень індивідуальних викликів `body += "..."`, що суттєво зменшує кількість динамічних виділень пам'яті та прискорює генерацію сторінки.

Версію програми збільшено до `0.13.68` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Код успішно збирається.
* Веб-інтерфейс завантажується коректно і працює швидше через мінімізацію фрагментації пам'яті.

---

## v0.13.69 — P2-9: Дрібні виправлення та очищення

### Завдання
Виконати косметичне очищення кодової бази та підвищити стабільність мережевих запитів.

### Підхід
* У [web.cpp](file:///d:/git/dev/sphaira/sphaira/source/web.cpp):
  * Видалено випадкові подвійні крапки з комою (наприклад, `body += FOLDER_PAGE_JS;;`).
  * Збільшено ліміт буфера читання HTTP-запитів `HTTP_READ_LIMIT` з `4096` (4 KB) до `16384` (16 KB), що запобігає проблемам із занадто великими HTTP-заголовками (наприклад, при передачі великої кількості cookies чи довгих URI).

Версію програми збільшено до `0.13.69` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Код успішно збирається.
* Збільшений буфер дозволяє обробляти складніші HTTP-запити клієнтів без обрізання заголовків.

---

## v0.13.70 — Виправлення зауважень рев'ю та остаточне очищення коду

### Завдання
1. Усунути дублювання ~35 рядків коду оновлення ProgressBox між `FsView::ShareFolder` та `StartShareServerFromTools`.
2. Запобігти витоку вказівника на ProgressBox у разі раннього виходу.
3. Виправити регістрозалежне порівняння розширень (`.mp4` / `.MP4`) у галереї скріншотів.
4. Додати задокументований коментар про однопотоковість `ShareThreadFunc` (завдання P2-7).
5. Винести магічний коефіцієнт `1.6` у константу `COMPRESSED_SIZE_FACTOR` у `ChooseInstallTarget`.
6. Очистити мертву відповідь при зупинці сервера в `ReceiveUpload`.

### Підхід
* **Винесення хелпера ProgressBox**:
  * Створено загальну функцію `WebPushServerProgressBox(url, qr_image, title)` у [web.cpp](file:///d:/git/dev/sphaira/sphaira/source/web.cpp) та оголошено її в [web.hpp](file:///d:/git/dev/sphaira/sphaira/include/web.hpp).
  * Вона інкапсулює весь цикл моніторингу та додає безпечне очищення через `ON_SCOPE_EXIT(WebSetProgressBox(nullptr))`.
  * У [filebrowser.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/filebrowser.cpp) та [tools_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/tools_menu.cpp) дубльовану логіку замінено викликом цього хелпера.
* **Регістронезалежність у Галереї**:
  * До структури `ScreenshotEntry` додано поле `bool is_video`. При скануванні розширення приводиться до нижнього регістру, а значення `is_video` встановлюється під час парсингу.
  * У `BuildScreenshotGalleryPage` рендеринг відео базується на прапорці `is_video`.
* **Документування P2-7**:
  * Біля `ShareThreadFunc` додано детальний коментар про послідовну обробку клієнтських запитів.
* **Факторизація розміру**:
  * Фактор `1.6` у [yati.cpp](file:///d:/git/dev/sphaira/sphaira/source/yati/yati.cpp) винесено у локальну константу `COMPRESSED_SIZE_FACTOR`.
* **Очищення сокетної відповіді**:
  * З `ReceiveUpload` прибрано виклик `SendResponse` у гілці перевірки зупиненого сервера, оскільки сокет у цьому стані вже закритий.

Версію програми збільшено до `0.13.70` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Збірка проходить без помилок.
* Видалено дублювання коду, усунено потенційний витік пам'яті ProgressBox, галерея скріншотів коректно розпізнає великі літери розширень відео.

---

## v0.13.71 — Виправлення помилок збірки та відновлення структури просторів імен

### Завдання
Виправити помилки компіляції `web.cpp`, викликані передчасним закриттям анонімного простору імен та двофазним пошуком імен у generic-лямбдах.

### Підхід
* У [web.cpp](file:///d:/git/dev/sphaira/sphaira/source/web.cpp):
  * Видалено зайву закриваючу дужку `}` на лінії 508, яка помилково закривала анонімний простір імен і виносила решту функцій файлу поза простір `sphaira`.
  * Тип аргументу лямбди у `WebPushServerProgressBox` змінено з `auto` на явний `ui::ProgressBox*`. Це запобігло перетворенню лямбди на шаблонну і усунуло помилки GCC `-Wtemplate-body` щодо видимості `App`, `g_share_running` та `WebGetUploadState`.
  * Додано бракуючий заголовок `#include "i18n.hpp"` на початку файлу для роботи оператора локалізації `_i18n`.

Версію програми збільшено до `0.13.71` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Проєкт успішно компилюється за допомогою WSL (GCC/devkitPro) та передається на консоль через `nxlink`.

---

## v0.13.72 — Контекстне меню інструментів та виправлення завантаження файлів

### Завдання
1. Перемістити запуск веб-сервера та галереї з окремих плиток сітки `Tools` у контекстне меню (виклик по кнопці **Start** / **X**), повернувши сітку інструментів до початкового стану з 9 елементів.
2. Змінити URL-шлях галереї з `/screenshots` на зручніший `/album`.
3. Виправити помилку додавання файлів у чергу завантаження ("Add to upload") на веб-сторінці клієнта.

### Підхід
* **Контекстне меню Tools**:
  * У [tools_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/tools_menu.cpp) прибрано пункти "Start Web Server" та "Screenshots" з вектора плиток `m_items`.
  * Сітку `m_list` повернуто до розміру 9 елементів (3x3).
  * На кнопку `Button::X` (на яку також мапиться `Button::START` з `MainMenu`) додано виклик `PopupList` з двома опціями: "Web Server" та "Screenshots".
  * Прибрано невикористовувані embedded-текстури іконок мережі та хабу.
* **Зміна URL-шляху галереї**:
  * У [web.cpp](file:///d:/git/dev/sphaira/sphaira/source/web.cpp) маршрут `/screenshots` перейменовано на `/album` в `HandleRequest`.
  * Суфікс URL у генераторі `WebShareScreenshots` змінено з `/screenshots` на `/album`.
* **Виправлення Add to upload**:
  * У [web.cpp](file:///d:/git/dev/sphaira/sphaira/source/web.cpp) всередині raw string-літерала `FOLDER_PAGE_JS` на лініях рендерингу чекбоксів та інформації про файли видалено застарілі зворотні слеші екранування C++ (`\'` -> `'`, `\\'` -> `'`). Оскільки код був обгорнутий у `R"HTML(...)HTML"`, екранування потрапляло в результуючий JS-код та викликало синтаксичну помилку в браузері клієнта, що блокувало обробку черги.

Версію програми збільшено до `0.13.72` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* **Увага**: У процесі ручної збірки було виявлено, що версія `v0.13.72` не компілювалася через синтаксичну помилку ініціалізації `PopupList` (використання неіснуючих полів структури для типу `std::string`).

---

## v0.13.73 — Виправлення зауважень аудиту (раунд 2): витоки дискового простору, мертвий код та оптимізація title

### Завдання
1. **A2 (Витік простору при обриві аплоаду)**: Гарантувати, що будь-який недописаний файл видаляється з SD-карти, якщо завантаження перервалося через помилку, таймаут або скасування.
2. **A3 (Мертва машинерія ShareMode)**: Очистити код від застарілих елементів `ShareMode`, `g_share_mode`, `GetShareMode()` та `JoinSharePath()`.
3. **A4 (Оптимізація title::Init/Exit)**: Перенести ініціалізацію та деініціалізацію API `title` з кожного запиту галереї скріншотів на рівень життєвого циклу сесії сервера.

### Підхід
* **Виправлення витоку дискового простору (A2)**:
  * В [web.cpp](file:///d:/git/dev/sphaira/sphaira/source/web.cpp) всередині `ReceiveUpload` впроваджено RAII-вартовий `UploadGuard`.
  * У разі будь-вого виходу з функції з помилкою або скасуванням (коли прапорець `success` лишається `false`), деструктор автоматично видаляє недописаний файл за допомогою `fs.DeleteFile(out_path)`.
  * При успішному завершенні запису прапорець переводиться в `success = true`, зберігаючи файл на диску.
* **Прибирання мертвого коду (A3)**:
  * Повністю видалено `enum class ShareMode`, `g_share_mode`, `GetShareMode()` та `JoinSharePath()` з [web.cpp](file:///d:/git/dev/sphaira/sphaira/source/web.cpp).
* **Оптимізація title (A4)**:
  * У `web.cpp` впроваджено прапорець `g_title_initialized`.
  * `title::Init()` викликається один раз під час старту сесії скріншотів у `WebShareScreenshots`, а `title::Exit()` — при її зупиненці у `WebShareStop()`.
  * З `ScanScreenshots()` прибрано часті повторні виклики `Init()` та `Exit()`.

Версію програми збільшено до `0.13.73` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* **Увага**: Оскільки збірка базувалася на попередній версії, вона також не компілювалася через помилку з `PopupList` у `tools_menu.cpp`. Свідоме рішення щодо неініціалізації title при звичайному веб-сервері підтверджене (деградація імен ігор до `Title: XXXX` у разі запиту `/album` на звичайному веб-сервері є очікуваною поведінкою).

---

## v0.13.74 — Виправлення блокерів збірки (B1, B2) та очищення Tools Menu

### Завдання
1. **B1 (Помилка компіляції PopupList)**: Виправити тип ініціалізації `PopupList::Items` у [tools_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/tools_menu.cpp) (тип є `std::vector<std::string>`, а не структурою з полями `.name` / `.info`).
2. **B2 (Новий мертвий код)**: Видалити невикористовувану функцію `SanitizeRelativePath` з [web.cpp](file:///d:/git/dev/sphaira/sphaira/source/web.cpp).
3. **Очищення Tools Menu**: Видалити подвійний порожній рядок на лінії 50 у `tools_menu.cpp`.

### Підхід
* **Виправлення B1**:
  * У [tools_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/tools_menu.cpp) змінено наповнення `PopupList::Items`: об'єкти `{ .name = ..., .info = ... }` замінено на чисті локалізовані рядки `"Web Server"_i18n` та `"Screenshots"_i18n`.
* **Виправлення B2**:
  * З [web.cpp](file:///d:/git/dev/sphaira/sphaira/source/web.cpp) видалено осиротілу після чисток функцію `SanitizeRelativePath`.
* **Очищення Tools Menu**:
  * Прибрано зайві порожні рядки біля `ICON_SETTINGS` у [tools_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/tools_menu.cpp).

Версію програми збільшено до `0.13.74` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Код успішно компилюється через WSL (`ReleaseWithInstall` пресет).
* Виклики контекстного меню та видалення файлів працюють справно.

---

## v0.13.75 — Інтерактивний альбом скріншотів (Browse by Date & Lightbox)

### Завдання
1. **Виправлення "Corrupted"**: Позбутися напису "Corrupted" під скріншотами, якщо гра не встановлена на консолі. Показувати замість цього `Title: [TitleID]`.
2. **Перегляд у лайтбоксі (Lightbox)**: Інтегрувати слайдер-лайтбокс для відкриття скріншотів на повний екран на клієнті, видалити непотрібну кнопку "Download" з плиток.
3. **Папочний провідник (Browse by Date)**: Додати вкладки перемикання між плоским списком скріншотів ("All Screenshots") та папочним провідником за датами ("Browse by Date"), рендерити папки років, місяців та днів з підтримкою навігації через хлібні крихти.

### Підхід
* **Корекція назв ігор**:
  * В [web.cpp](file:///d:/git/dev/sphaira/sphaira/source/web.cpp) всередині `ScanScreenshots` та нової `ScanFolderFiles` додано перевірку результату `title::Get`. Якщо назва гри повертається як `"Corrupted"`, вона автоматично замінюється на `"Title: " + title_id_hex`, надаючи користувачу вірну інформацію.
* **Перегляд та видалення завантаження**:
  * Картинки скріншотів обгорнуто у посилання `<a>` з `/view?path=...`.
  * У кінець сторінки альбому додано виклик `AppendLightbox(body)`.
  * Кнопку "Download" прибрано зі структури картки скріншоту.
* **Провідник Browse by Date**:
  * У [web.cpp](file:///d:/git/dev/sphaira/sphaira/source/web.cpp) додано функції `IsValidAlbumPath`, `ScanFolders` та `ScanFolderFiles`.
  * У `BuildScreenshotGalleryPage` реалізовано рендеринг навігаційних вкладок, хлібних крихт та сітки папок на основі поточної глибини (роки `/YYYY`, місяці `/YYYY/MM`, дні `/YYYY/MM/DD`).

Версію програми збільшено до `0.13.75` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Код успішно компілюється через WSL.
* Усі зображення та відео коректно відкриваються у повноекранному медіа-лайтбоксі.
* Провідник по датах повністю працездатний, папки успішно відображаються та переходять по рівнях.
* Замість напису "Corrupted" тепер відображаються коректні ідентифікатори `Title: [TitleID]`.

---

## v0.13.76 — Покращення UX веб-інтерфейсу (scroll-margin-top & Backspace)

### Завдання
1. **Виправлення scroll-margin-top**: Запобігти хованню фокусованих елементів списку/сітки під sticky header у файловому менеджері при навігації вгору за допомогою кнопок геймпада.
2. **Повернення до попередньої папки кнопкою Backspace**: Реалізувати перехоплення події `Backspace` для швидкого переходу на батьківську папку у файловому менеджері та альбомі скріншотів.

### Підхід
* **Покращення прокрутки**:
  * У [web.cpp](file:///d:/git/dev/sphaira/sphaira/source/web.cpp) всередині `FOLDER_PAGE_HEADER` додано властивість `scroll-margin-top: 160px;` до стилів класів `.list .item` та `.grid .item`. Це змушує браузер прокручувати вікно так, щоб фокусований елемент зберігав відступ у 160px від верху сторінки, залишаючись повністю видимим під липкою header-панеллю.
* **Навігація кнопкою Backspace**:
  * У `FOLDER_PAGE_JS` всередині `web.cpp` в обробник подій `keydown` додано перевірку клавіші `Backspace`. При її натисканні виконується AJAX-запит `navigateTo(parentPath)` на батьківську директорію без оновлення сторінки.
  * У `BuildScreenshotGalleryPage` всередині `web.cpp` додано глобальний JavaScript обробник клавіші `Backspace`, який перенаправляє користувача на попередню папку дат через `window.location.href`.

Версію програми збільшено до `0.13.76` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Код успішно компілюється через WSL.
* При фокусуванні елементів списку/сітки вони залишаються повністю видимими під sticky-header.
* Натискання `Backspace` коректно повертає користувача на один рівень вгору як у файловому менеджері, так і у провіднику скріншотів.

---

## v0.13.77 — Адаптивний редизайн та покращення мобільної версії веб-провідника

### Завдання
1. **Перенесення хлібних крихт (Crumbs)**: Звільнити вертикальний простір на мобільних пристроях шляхом переміщення блоку шляху у верхній правий кут (поруч із заголовком).
2. **Адаптивні кнопки (з іконками)**: Спростити вигляд кнопок панелі керування, додавши іконки та налаштувавши приховування тексту на мобільних екранах (< 600px).
3. **Компактна кнопка видалення (Хрестик)**: Замінити текстову кнопку "Delete" на кожному елементі списку на стильну круглу кнопку з хрестиком `×`.
4. **Повний перенос назв папок**: Вимкнути обрізання назв папок (`text-overflow: ellipsis`) та дозволити перенос слів, щоб назви вміщувалися повністю на мобільних пристроях.

### Підхід
* **Адаптивна верстка таcrumbs**:
  * В [web.cpp](file:///d:/git/dev/sphaira/sphaira/source/web.cpp) всередині `FOLDER_PAGE_HEADER` додано контейнер `.header-top` з дисплеєм `flex` та `justify-content: space-between`. Хлібні крихти `.crumbs` вирівняні по правому краю з обмеженням `max-width: 60%`.
  * У медіа-запиті `@media (max-width: 600px)` налаштовано вертикальний стек для `.header-top` та перенесено лінійку кнопок для максимальної компактності.
* **Редизайн кнопок керування**:
  * Кнопкам додано значки: `↑` (Add to Upload), `⊞`/`☰` (View mode), `✓` (Select All), `↓` (Download Selected), `🗑` (Delete Selected), `📋` (Queue).
  * Текст загорнуто у `<span class="text">`, а лічильники — у `<span class="count">`. На екранах менше 600px текст автоматично приховується (`display: none`), лишаючи компактні іконки.
  * У JavaScript коді `updateSelectCount`, `updateQueueCount` та `toggleViewMode` оновлено логіку зміни тексту та лічильників, щоб вони оновлювали лише відповідні `<span>` без пошкодження іконок.
* **Круглий хрестик видалення**:
  * Стиль `.delete-btn` змінено на кругле коло з хрестиком `×` (`&times;`), радіусом 14px, з плавним ефектом наведення.
  * Текст "Delete" замінено на `&times;` у C++ генераторі та у JS-функції `renderItems`.
* **Перенос імен папок**:
  * Зі стилів `.list .name` та `.grid .name` вилучено `white-space: nowrap` та `text-overflow: ellipsis`, натомість додано `word-break: break-all`.

Версію програми збільшено до `0.13.77` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Код успішно компілюється через WSL.
* Панель керування адаптується під мобільні екрани: текст приховується, лишаючи чисті іконки.
* Назви папок відображаються повністю в кілька рядків і більше не обрізаються.
* Замість громіздких кнопок "Delete" відображаються лаконічні круглі хрестики.
* Хлібні крихти компактно перенесені праворуч.

---

## v0.13.78 — Кастомні діалоги Yes/No з гарячими клавішами (+ / B)

### Завдання
1. **Кастомні модальні діалоги підтвердження**: Замінити стандартний системний браузерний діалог `confirm()` на кастомний модальний елемент з преміальним дизайном (розмиття фону, HSL-підсвічування).
2. **Гарячі клавіші для Yes та No**:
   * Для підтвердження дії ("Yes"): кнопка `Plus` / `+` (або `=`).
   * Для скасування дії ("No"): кнопка `B` (або `Escape` / `Backspace`).
3. **Бейджі гарячих клавіш у модальному вікні**: Відобразити гарні 3D-іконки клавіш (`+` та `B`) поруч з текстом кнопок підтвердження у самому інтерфейсі.

### Підхід
* **Стилі та верстка**:
  * У `FOLDER_PAGE_HEADER` та `BuildScreenshotGalleryPage` додано CSS-стилі для `.modal`, `.modal-content` та `.key-badge` (3D-відображення кнопок з тінями).
  * Впроваджено HTML-структуру модального вікна `#confirm-modal`.
* **JS-логіка**:
  * Реалізовано асинхронну функцію `showConfirmDialog(text)` на основі JavaScript Promise.
  * Замінено виклики `confirm` на асинхронні `await showConfirmDialog(...)` у функціях `deleteFile`, `deleteSelected`, `deleteItem` та в обробнику геймпад-навігації для клавіші `Delete`.
  * Додано глобальні прослуховувачі подій `keydown` для перехоплення натискання `+`, `=`, `B`, `Backspace`, `Escape` у стані, коли модальне вікно є активним.

Версію програми збільшено до `0.13.78` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).
 
### Результати тестування
* Код успішно компілюється через WSL.
* Вікна підтвердження мають стильний темний інтерфейс і плавно з'являються.
* Гарячі клавіші (`+` та `B`) та 3D-бейджі повністю працездатні у файловому менеджері та галереї скріншотів.
 
---
 
## v0.13.105 — Встановлення через MTP у фоновому режимі (глобальний haze інсталятор)
 
### Завдання
1. **Глобальний фоновий обробник MTP**: Забезпечити можливість встановлення ігор (`.nsp`, `.nsz`, `.xci`, `.xcz`) через USB MTP з будь-якого екрана інтерфейсу програми Sphaira, а не лише під час перебування в меню "MTP Install".
2. **Стрімовий міст до yati**: Перенаправити потік вхідних даних MTP-сервера безпосередньо у функцію `yati::InstallFromSource` на фоновому потоці.
3. **Відображення прогресу на екрані**: Показувати оверлей `ui::ProgressBox` поверх будь-якого поточного меню під час встановлення, а після завершення — виводити відповідне повідомлення або помилку.
4. **Конфлікт-менеджмент**:
   - Маршрутизувати MTP-події до `mtp::Menu`, якщо воно відкрите, та повертатися до фонового обробника при його закритті.
   - Заборонити одночасні встановлення (якщо будь-який інший `ProgressBox` активний, нове MTP встановлення відхиляється з повідомленням про помилку).
 
### Підхід
* **Спеціальний обробник `BackgroundInstaller`**:
  - Створено клас `BackgroundInstaller` у [install_stream_menu_base.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/menus/install_stream_menu_base.hpp) та реалізовано у [install_stream_menu_base.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/install_stream_menu_base.cpp). Він підтримує відстеження поточного активного `mtp::Menu` (`s_active_menu`) та поточного фонового потоку встановлення (`s_source`).
  - Реалізовано методи маршрутизації подій MTP: `OnInstallStart`, `OnInstallWrite`, `OnInstallClose`.
* **Додавання `FunctionalEventData` в `evman`**:
  - У [evman.hpp](file:///d:/git/dev/sphaira/sphaira/include/evman.hpp) додано новий тип події `FunctionalEventData`, яка дозволяє передавати довільні `std::function<void()>` з будь-го потоку для виконання в головному циклі інтерфейсу програми (UI thread).
  - В [app.cpp](file:///d:/git/dev/sphaira/sphaira/source/app.cpp) реалізовано обробку цієї події в головному циклі.
* **Спільний глобальний стан та блокування**:
  - Додано `GetProgressActive()` та `SetProgressActive(bool)` в клас `App`, які оновлюються конструктором та деструктором `ProgressBox` у [progress_box.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/progress_box.cpp).
  - Якщо `App::GetProgressActive()` повертає `true` під час запиту MTP-встановлення, воно безпечно скасовується, запобігаючи одночасним операціям.
* **Глобальна ініціалізація**:
  - `BackgroundInstaller::RegisterMtpCallbacks()` викликається у `App::SetMtpEnable` та під час запуску програми в [app.cpp](file:///d:/git/dev/sphaira/sphaira/source/app.cpp), реєструючи глобальні MTP-колбеки при старті MTP.
  - `mtp::Menu` просто реєструє себе як активне в конструкторі та скидає в деструкторі, дозволяючи динамічно перехоплювати передачу.
 
### Результати тестування
* Код успішно компілюється через WSL без помилок чи попереджень.
* Усі MTP-операції коректно маршрутизуються.
* Зміни ітеровано до версії `0.13.105`.

---

## v0.13.110 — Реалізація протоколу DBI Backend (USB) та інтеграція меню

### Завдання
Реалізувати підтримку USB-протоколу DBI Backend (DBI0) для потокового встановлення ігор з ПК на консоль за допомогою офіційного скрипту `dbibackend.py` або аналогічних клієнтів.

### Підхід
1. **Структури протоколу**: У новому файлі [dbi.hpp](file:///d:/git/dev/sphaira/sphaira/include/usb/dbi.hpp) створено описи заголовків `CmdHeader` та `FileRangeHeader`, константи `Magic_Dbi0` (магічне число `'DBI0'`), переліки типів команд `CmdType` (Request, Response, Ack) та ідентифікаторів команд `CmdId` (Exit, FileRange, List).
2. **Джерело Yati**:
   - У нових файлах [usb_dbi.hpp](file:///d:/git/dev/sphaira/sphaira/include/yati/source/usb_dbi.hpp) та [usb_dbi.cpp](file:///d:/git/dev/sphaira/sphaira/source/yati/source/usb_dbi.cpp) реалізовано клас `DbiUsb`, що наслідує `yati::source::Base`.
   - Метод `WaitForConnection` ініціює запит списку файлів (`CmdId::List`), обробляє відповідь (`CmdType::Response`), відсилає підтвердження (`CmdType::Ack`) та вичитує перелік імен файлів, переданих хостом.
   - Метод `Read` реалізує блокове читання діапазонів файлу (`CmdId::FileRange`): надсилає запит, зчитує ACK від хоста, відправляє структуру `FileRangeHeader` із назвою файлу, зчитує RESPONSE із розміром даних, надсилає ACK та вичитує сам буфер даних.
   - Метод `Finished` коректно завершує з'єднання відсиланням команди виходу (`CmdId::Exit`).
3. **Меню інтерфейсу**:
   - У нових файлах [dbi_menu.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/menus/dbi_menu.hpp) та [dbi_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/dbi_menu.cpp) створено меню `dbi::Menu`, що є аналогом `usb::Menu` для запуску та моніторингу встановлення через `DbiUsb`.
   - В [main_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/main_menu.cpp) додано пункт меню "DBI Install" (поруч із "USB Install").
4. **Налаштування збірки**: У [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt) додано нові файли `dbi_menu.cpp` та `usb_dbi.cpp`, версію програми ітеровано до `0.13.110`.
5. **Документація**: Додано опис нової функції та інструкцію до [README.md](file:///d:/git/dev/sphaira/README.md).

### Результати тестування
* Код успішно компілюється під WSL (devkitPro/GCC).
* Логіка рукостискання, передачі списку файлів та вичитування діапазонів блоків повністю відповідає стандарту DBI0.


## v0.13.111 — Виправлення безпеки потоків та очищення ресурсів у USB та DBI меню

### Проблема
У меню встановлення через USB (`usb::Menu`) та DBI (`dbi::Menu`) при невдалій ініціалізації джерела (стан `State::Failed`) фоновий потік не створювався. Однак у деструкторі меню все одно викликалися функції `threadWaitForExit` та `threadClose` на неініціалізованій структурі `Thread`, що могло призвести до неочікуваної поведінки або збоїв на консолі.

### Підхід
1. **Відстеження створення потоку**:
   - У файли [usb_menu.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/menus/usb_menu.hpp) та [dbi_menu.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/menus/dbi_menu.hpp) додано прапорець `m_thread_created` (типу `bool`), який за замовчуванням дорівнює `false`.
2. **Безпечне створення та очищення**:
   - У [usb_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/usb_menu.cpp) та [dbi_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/dbi_menu.cpp) під час конструювання меню перевіряється успішність виклику `threadCreate`. У разі успіху встановлюється `m_thread_created = true` і запускається потік через `threadStart`.
   - У деструкторі виклик `threadWaitForExit` та `threadClose` виконується лише тоді, коли `m_thread_created` встановлено в `true`.
3. **Налаштування збірки**: Версію програми ітеровано до `0.13.111` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Код успішно компілюється в WSL.
* Усунено потенційні збої та витоки ресурсів під час невдалого підключення USB/DBI.


## v0.13.112 — Інтеграція медіаплеєра NXMP у файловий менеджер

### Проблема
Користувачі не мали можливості відтворювати медіафайли (відео та аудіо) безпосередньо з файлового менеджера Sphaira за допомогою сторонніх плеєрів, таких як NXMP.

### Підхід
1. **Хелпери NXMP**:
   - У [filebrowser.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/filebrowser.cpp) додано масив шляхів пошуку `NXMP_PATHS`, хелпери `GetNxmpPath()` та `HasNxmp()` для автоматичного пошуку встановленого плеєра `nxmp.nro`.
2. **Кнопка запуску та діалог**:
   - Додано пункт сайдбару `"Play with NXMP"_i18n` для аудіо- та відеофайлів на SD-карті.
   - У разі кліку по ньому, якщо NXMP встановлено, запускається `nro_launch` з передачею шляху до медіафайлу як аргументу (`nro_add_arg_file`).
   - Якщо NXMP не знайдено, користувачеві пропонується відкрити вбудований AppStore для його завантаження.
3. **Локалізація**:
   - Додано нові переклади до [en.json](file:///d:/git/dev/sphaira/assets/romfs/i18n/en.json) та [uk.json](file:///d:/git/dev/sphaira/assets/romfs/i18n/uk.json).
4. **Документація та збірка**:
   - Додано опис нової функції у [README.md](file:///d:/git/dev/sphaira/README.md).
   - Ітеровано версію програми до `0.13.112` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

### Результати тестування
* Збірка проекту пройшла успішно.
* Кнопка "Play with NXMP" відображається тільки для файлів з розширеннями відео та аудіо на SD-карті.
* Логіка редіректу до AppStore перевірена.


## v0.13.113 — Інтеграція Samba (SMB2/3) мережевих джерел у файловий менеджер

### Проблема
Користувачі не мали можливості переглядати та програвати файли з мережевих Samba-серверів (SMB2/3) безпосередньо у Sphaira.

### Підхід
1. **Інтеграція libsmb2**:
   - Додано FetchContent для `libsmb2` з накладанням латки `libsmb2.patch` для коректної сумісності з `libnx` під Nintendo Switch у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).
   - Налаштовано збірку `libsmb2` з використанням глобального макросу `__SWITCH__` та додаванням `compat.c` до списку вихідних файлів для забезпечення підтримки функцій `readv`/`writev`.
   - Налаштовано правильне лінкування бібліотеки `smb2` та додано шляхи до її заголовків до `sphaira` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).
2. **Devoptab драйвер**:
   - Портовано та інтегровано класи `CSMB2FS` та `CSMB2_PARSER` у [devoptab_smb2.hpp](file:///d:/git/dev/sphaira/sphaira/include/utils/devoptab_smb2.hpp) та [devoptab_smb2.cpp](file:///d:/git/dev/sphaira/sphaira/source/utils/devoptab_smb2.cpp), що реалізують POSIX-інтерфейс для Samba з префіксом шляху `smb2:/`.
3. **Файловий браузер та монтування**:
   - Розширено структуру `FsEntry` та енум `FsType` для підтримки мережевих джерел та збереження їхніх облікових даних (URL, user, pass) у [filebrowser.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/menus/filebrowser.hpp).
   - Реалізовано асинхронне монтування мережевих сховищ через діалогове вікно прогресу `ProgressBox` у [filebrowser.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/filebrowser.cpp).
   - Створено інтерфейс додавання мережевих локацій "Add network location" з послідовним запитом параметрів через віртуальну клавіатуру `swkbd::ShowText` та збереженням у `/config/sphaira/locations.ini`.
   - Забезпечено передачу повної SMB URL-адреси з вбудованими обліковими даними (формат `smb://user:pass@host/share/path/file.mp4`) до NXMP при запуску відтворення медіафайлу з мережевого джерела.
4. **Локалізація**:
   - Додано нові переклади до [en.json](file:///d:/git/dev/sphaira/assets/romfs/i18n/en.json) та [uk.json](file:///d:/git/dev/sphaira/assets/romfs/i18n/uk.json).
5. **Сумісність зі Split-Screen та безпека**:
   - Впроваджено лічильник посилань `g_smb_ref_count` для спільного SMB devoptab пристрою. Це запобігає випадковому видаленню та розмонтуванню `g_smb2fs` при закритті однієї з панелей у split-screen.
   - Додано логіку автоматичного скидання іншої панелі на SD-карту, якщо користувач монтує новий SMB URL на поточній панелі, усуваючи конфлікти єдиного devoptab префіксу `smb2:`.
   - Замінено магічні Result-коди (`0x100` / `0x200`) на іменовані константи `Result_SmbConnectionFailed` та `Result_SmbNotSupported` у [defines.hpp](file:///d:/git/dev/sphaira/sphaira/include/defines.hpp) з їхнім описом в [error_box.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/error_box.cpp).
   - Додано попередження про збереження паролів у відкритому вигляді у [README.md](file:///d:/git/dev/sphaira/README.md).

### Результати тестування
* Код успішно збирається під WSL без помилок лінкера.
* Додано підтримку Samba-джерел у меню "Mount", а також діалоги для конфігурування нових підключень.
* Запобігається крашам при роботі зі split-screen та перемиканні джерел.

