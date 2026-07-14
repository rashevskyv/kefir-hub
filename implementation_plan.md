# План реалізації: Прогрес MTP, налаштування WebDAV та вибір джерел File Browser

Цей план описує наступні кроки для покращення UX та реалізації невиконаних задач:
1. **id 43**: Попап прогресу для звичайного (не-install) копіювання файлів по MTP.
2. **S7 / id 47**: Окрема папка налаштувань «WebDAV» у Network settings з боковим Sidebar та канонізацією адрес.
3. **S8**: Пункт «Sources» у верхньому контекстному меню File Browser для швидкого перемикання змонтованих дисків/носіїв.

---

## Потрібен відгук користувача

> [!IMPORTANT]
> Будь ласка, підтвердьте, чи збирати програму самостійно через WSL після завершення внесення змін, чи ви виконаєте збірку вручну.

---

## Детальний опис пропонованих змін

### 1. Попап прогресу копіювання MTP (id 43)

#### [MODIFY] [haze_helper.cpp](file:///d:/git/dev/sphaira/sphaira/source/haze_helper.cpp)
- Створити змінні стану UI попапу в анонімному namespace:
  - `Mutex g_mtp_ui_mutex;`
  - `ui::ProgressBox* g_mtp_pbox{nullptr};`
  - `UEvent g_mtp_done_event;`
  - `bool g_mtp_ui_alive{false};`
  - `bool g_mtp_new_transfer{false};`
  - `std::string g_mtp_current_filename;`
- На `CallbackType_ReadBegin` / `CallbackType_WriteBegin`:
  - Перевірити, чи це не install-потік (перевірка під `g_shared_data.mutex`: `in_progress` і збіг імені з `current_file`). Якщо це install, то ігнорувати.
  - Якщо попап неактивний (`g_mtp_ui_alive == false`), надіслати подію `evman::push(FunctionalEventData)` на головний потік.
  - Головний потік створює detached `ProgressBox` з назвою `"Copying via MTP"_i18n`, запускає воркер-потік, який очікує подію `g_mtp_done_event` з тайм-аутом у циклі, та викликає `App::PushTransfer`.
  - При створенні записуємо вказівник у `g_mtp_pbox` та ставимо `g_mtp_ui_alive = true`.
  - Додати done-колбек для очищення: під м'ютексом `g_mtp_pbox = nullptr; g_mtp_ui_alive = false;`.
- На `CallbackType_ReadProgress` / `CallbackType_WriteProgress`:
  - Під м'ютексом `g_mtp_ui_mutex`, якщо `g_mtp_pbox` живий, викликати `g_mtp_pbox->NewTransferForce(filename)` при зміні файлу та `g_mtp_pbox->UpdateTransferForce(offset, size)`.
- На `CallbackType_ReadEnd` / `CallbackType_WriteEnd`:
  - Сигналізувати `g_mtp_done_event`. Щоб уникнути блимання при купі дрібних файлів, воркер чекає ~1.5 с перед тим, як остаточно закрити попап; якщо приходить новий `Begin`, продовжуємо цикл очікування.

---

### 2. Налаштування WebDAV у Network settings (S7 / id 47)

#### [MODIFY] [app.hpp](file:///d:/git/dev/sphaira/sphaira/include/app.hpp)
- Оголосити метод `static void DisplayWebdavOptions(bool left_side = true);`.

#### [MODIFY] [app_display_options.cpp](file:///d:/git/dev/sphaira/sphaira/source/app_display_options.cpp)
- Реалізувати `App::DisplayWebdavOptions(bool left_side)`:
  - Створити `Sidebar` з назвою `"WebDAV Settings"_i18n`.
  - Додати поле `"Server address"_i18n` (використовуючи `SidebarEntryTextBase`). Значення зчитувати з `App::GetWebdavUrl()`, але відрізати префікс `webdav://` для відображення.
  - При зміні адреси (виклик `swkbd::ShowText`):
    - Якщо введено адресу з `http://` — показати нотифікацію помилки `"HTTP protocol is not allowed. Please use HTTPS/WebDAV."_i18n` та відхилити зміну.
    - Якщо адреса починається з `https://` — замінити префікс на `webdav://`.
    - Якщо адреса без схеми — додати префікс `webdav://`.
    - Записати через `App::SetWebdavUrl()`.
  - Додати поле `"Username"_i18n` (зчитування/запис `App::GetWebdavUser()`).
  - Додати поле `"Password"_i18n`. Показувати як маску `********` (якщо не порожній), при редагуванні не передавати початкове значення у клавіатуру. Записувати через `App::SetWebdavPass()`.

#### [MODIFY] [settings_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/settings_menu.cpp)
- У категорії `"Network"` замінити три старі пункти `"WebDAV URL"`, `"WebDAV User"`, `"WebDAV Password"` на один пункт:
  `{ "WebDAV"_i18n, "Configure WebDAV server for save synchronization."_i18n, [](){ return std::string{}; }, [](){ App::DisplayWebdavOptions(false); }, SettingsItemKind::Folder }`

---

### 3. Швидке перемикання джерел File Browser (S8)

#### [MODIFY] [filebrowser.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/menus/filebrowser.hpp)
- Додати у `FsView` приватний метод `void ShowSourcePicker();`.

#### [MODIFY] [filebrowser.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/filebrowser.cpp)
- Реалізувати `FsView::ShowSourcePicker()`:
  - Створити `Sidebar` з заголовком `"Sources"_i18n`.
  - Побудувати список джерел (як це зараз робиться в `DisplayAdvancedOptions`): `location::GetStdio()`, `FS_ENTRIES`, `location::Load()`.
  - Визначити поточне активне джерело та додати префікс `"-> "` до його назви у списку, а також встановити його як `default_index`.
  - При виборі джерела виконувати монтування: `SetFs(root, target_entry)`.
- У `FsView::DisplayOptions()` (контекстне меню файлів):
  - Додати пункт `"Sources"_i18n` (з тултипом `"Quickly switch this pane's file source."_i18n`), який викликає `ShowSourcePicker()`.
- У `FsView::DisplayAdvancedOptions()`:
  - Замінити великий блок додавання `Mount` на простий виклик `ShowSourcePicker()`.

---

### 4. Локалізація (en.json / uk.json)

#### [MODIFY] [en.json](file:///d:/git/dev/sphaira/assets/romfs/i18n/en.json) та [uk.json](file:///d:/git/dev/sphaira/assets/romfs/i18n/uk.json)
- Додати відповідні переклади англійською та українською мовами для нових UI елементів.

---

## План верифікації

### Ручна верифікація (WSL збірка)
- Зібрати проєкт через WSL та перевірити роботу:
  1. Підключити консоль по MTP, скопіювати звичайний файл (наприклад, `.nro` або картинку) у довільну папку на microSD. Перевірити появу попапу прогресу із назвою файлу та швидкістю. Згорнути/розгорнути по L3.
  2. Зайти в Settings -> Network -> WebDAV. Перевірити, що відкривається Sidebar. Ввести адресу без схеми та перевірити автододавання `webdav://`. Ввести `http://...` та перевірити відхилення.
  3. Відкрити File Browser, натиснути Start (Options) -> Sources. Перевірити відображення списку джерел із позначкою `"-> "` на поточному диску. Перемикання джерел має працювати коректно.
