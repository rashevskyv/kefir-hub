# UX-покращення джерел даних та статус-бару (v0.13.220)

Цей план описує зміни, спрямовані на покращення досвіду користувача (UX) при роботі з мережевими джерелами даних (Sources) у файловому менеджері та меню налаштувань, а також виправлення злипання годинника та відсотків заряду в статус-барі.

## Proposed Changes

### [Component: Status Bar Layout]

---

#### [MODIFY] [menu_base.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/menu_base.cpp)
- Збільшити фіксований зсув `start_x` для батареї з `64` до `94` пікселів в обох режимах (зарядка та розрядка):
  - При заряджанні (`charger_type != 0`): `start_x -= 94;`
  - При розряджанні (`charger_type == 0`): `draw(ThemeEntryID_TEXT, 94, "%u\uFE6A", pdata.battery_percetange);`

---

### [Component: Location Core]

---

#### [MODIFY] [location.hpp](file:///d:/git/dev/sphaira/sphaira/include/location.hpp)
- Додати декларацію функції `Remove` для видалення мережевої локації за ім'ям:
  ```cpp
  void Remove(const std::string& name);
  ```

---

#### [MODIFY] [location.cpp](file:///d:/git/dev/sphaira/sphaira/source/location.cpp)
- Реалізувати функцію `Remove` за допомогою `minIni` (`ini_puts` з передачею `nullptr` замість імені ключа та значення для видалення всієї секції):
  ```cpp
  void Remove(const std::string& name) {
      if (name.empty()) return;
      ini_puts(name.c_str(), nullptr, nullptr, location_path);
  }
  ```

---

### [Component: File Browser (UI)]

---

#### [MODIFY] [filebrowser.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/menus/filebrowser.hpp)
- Додати декларацію інтерактивної функції додавання мережевого джерела:
  ```cpp
  void AddNetworkLocationInteractive(std::function<void()> on_success = nullptr);
  ```

---

#### [MODIFY] [filebrowser.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/filebrowser.cpp)
- **Прибрати дублювання монтування**:
  - Видалити пункт `"Mount"` (який викликав `ShowSourcePicker()`) з `DisplayAdvancedOptions()`.
- **Перенести додавання джерела**:
  - Видалити пункт `"Add network location"` з `DisplayAdvancedOptions()`.
  - У `ShowSourcePicker()` додати в кінець списку джерел пункт `"Add network location"_i18n`, який викликає `AddNetworkLocationInteractive` з оновленням меню.
  - Змінити опис на детальний: `"Configure a new network location (supported protocols: SMB, WebDAV, FTP, HTTP)."_i18n`.
- **Оновити пункт Upload**:
  - У `DisplayAdvancedOptions()` перейменувати `"Upload"_i18n` на `"Upload to network location"_i18n` та змінити опис на `"Upload the selected file(s) to a configured network storage."_i18n`.
- **Реалізація `AddNetworkLocationInteractive`**:
  - Реалізувати функцію в кінці файлу з послідовним вибором протоколу (`Samba (SMB)`, `WebDAV`, `FTP`, `HTTP`) та відповідними кроками введення через `swkbd::ShowText` та збереженням через `location::Add`.

---

#### [MODIFY] [filebrowser_ops.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/filebrowser_ops.cpp)
- У методі `UploadFiles()` змінити нотифікацію при відсутності локацій:
  - Було: `App::Notify("No upload locations set!"_i18n);`
  - Стане: `App::Notify("No network locations configured! Add one in Settings."_i18n);`
- Змінити заголовок PopupList вибору локації:
  - Було: `"Select upload location"_i18n`
  - Стане: `"Select network location"_i18n`

---

### [Component: Settings Menu]

---

#### [MODIFY] [settings_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/settings_menu.cpp)
- Додати нову категорію налаштувань `"Sources"_i18n` (під категорією `"Network"_i18n`) з описом `"Manage file sources and network locations."_i18n`.
- Реалізувати функцію `BuildSourcesCategoryItems()`, яка генерує:
  - Пункт `"+ Add network location"_i18n` (опис: `"Configure a new network location (supported protocols: SMB, WebDAV, FTP, HTTP)."_i18n`), що викликає `AddNetworkLocationInteractive()` з оновленням категорій (`menu->OnFocusGained()`).
  - Список наявних джерел із `location::Load()`. При натисканні `A` на джерело показується вікно підтвердження видалення: `"Delete this network location?"_i18n` з кнопками `"No"_i18n`/`"Yes"_i18n`. При виборі `"Yes"_i18n` джерело видаляється через `location::Remove()`, показується нотифікація `"Location deleted successfully!"_i18n` та меню оновлюється.

---

### [Component: Localization]

---

#### [MODIFY] [en.json](file:///d:/git/dev/sphaira/assets/romfs/i18n/en.json) та [uk.json](file:///d:/git/dev/sphaira/assets/romfs/i18n/uk.json)
- Додати переклади для всіх нових рядків:
  - `"Sources"` -> `"Джерела"` / `"Sources"`
  - `"Manage file sources and network locations."` -> `"Керування джерелами файлів та мережевими локаціями."` / `"Manage file sources and network locations."`
  - `"+ Add network location"` -> `"+ Додати мережеве джерело"` / `"+ Add network location"`
  - `"Delete this network location?"` -> `"Видалити це мережеве джерело?"` / `"Delete this network location?"`
  - `"Location deleted successfully!"` -> `"Джерело успішно видалено!"` / `"Location deleted successfully!"`
  - `"Upload to network location"` -> `"Завантажити в мережеве джерело"` / `"Upload to network location"`
  - `"Upload the selected file(s) to a configured network storage."` -> `"Завантажити вибрані файли у налаштоване мережеве джерело."` / `"Upload the selected file(s) to a configured network storage."`
  - `"No network locations configured! Add one in Settings."` -> `"Не налаштовано жодного мережевого джерела! Додайте його в налаштуваннях."` / `"No network locations configured! Add one in Settings."`
  - `"Select network location"` -> `"Виберіть мережеве джерело"` / `"Select network location"`
  - `"Configure a new network location (supported protocols: SMB, WebDAV, FTP, HTTP)."` -> `"Додати мережеве джерело. Підтримувані протоколи: SMB, WebDAV, FTP, HTTP."` / `"Configure a new network location (supported protocols: SMB, WebDAV, FTP, HTTP)."`
  - `"Select Protocol"` -> `"Виберіть протокол"` / `"Select Protocol"`

---

### [Component: Version]

---

#### [MODIFY] [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt)
- Підвищити версію проекту `sphaira_VERSION` до `0.13.220`.

---

# Спільні джерела та покращення стабільності (v0.13.221)

## Proposed Changes

### [Component: Location Core]

---

#### [MODIFY] [location.cpp](file:///d:/git/dev/sphaira/sphaira/source/location.cpp)
- У методі `Load()` після зчитування локацій з `locations.ini` перевіряти `App::GetWebdavUrl()`. Якщо налаштування WebDAV для сейвів не порожні, додавати віртуальний запис із назвою `"WebDAV (Saves Sync)"`, якщо такий URL ще не існує в списку джерел.

---

### [Component: File Browser (UI)]

---

#### [MODIFY] [filebrowser.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/filebrowser.cpp)
- Імпортувати `"evman.hpp"`.
- У методі `AddNetworkLocationInteractive()` замінити прямі виклики `on_success()` на асинхронні за допомогою `evman::push(evman::FunctionalEventData{[on_success](){ on_success(); }});` для запобігання Use-After-Free крашу `PopupList` на реальній консолі.

---

### [Component: Settings Menu]

---

#### [MODIFY] [settings_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/settings_menu.cpp)
- У методі `BuildSourcesCategoryItems()` при виборі видалення перевіряти, чи дорівнює назва джерела `"WebDAV (Saves Sync)"`. Якщо так, замість виклику `location::Remove(loc.name)` скидати налаштування WebDAV сейвів (`App::SetWebdavUrl("")`, `App::SetWebdavUser("")`, `App::SetWebdavPass("")`), показувати нотифікацію `"Location deleted successfully!"` та оновлювати меню.

---

### [Component: Version]

---

#### [MODIFY] [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt)
- Підвищити версію проекту `sphaira_VERSION` до `0.13.221`.

## Verification Plan (Виконано)

### Automated Tests
- Автоматизовані тести відсутні.

### Manual Verification (Готово до тестування на консолі)
- **WSL-збірка**: Виконано успішно. Проект скомпільовано без помилок лінкера.
- **Тестування на консолі**:
  - У налаштуваннях `Tools -> Settings -> WebDAV` додати WebDAV сервер.
  - Перевірити, що джерело `"WebDAV (Saves Sync)"` з'явилося у `Tools -> Settings -> Sources` та у списку завантаження файлового менеджера (`Upload to network location`).
  - У `Tools -> Settings -> Sources` вибрати `"WebDAV (Saves Sync)"`, натиснути `A` та видалити його. Перевірити, що налаштування `WebDAV` у `Tools -> Settings -> WebDAV` очистилися.
  - Додати WebDAV, FTP або HTTP джерело і переконатися, що програма більше не крашиться (Атмосфера не вилітає після додавання).

---

# Єдине джерело істини для WebDAV сейвів (v0.13.222)

## Proposed Changes

### [Component: Location Core]

---

#### [MODIFY] [location.cpp](file:///d:/git/dev/sphaira/sphaira/source/location.cpp)
- Відкотити динамічне додавання `"WebDAV (Saves Sync)"` з `App::GetWebdavUrl()`, оскільки джерело істини тепер виключно в `locations.ini`. Метод `Load()` повертає тільки зчитані з файлу локації.

---

### [Component: Saves Sync]

---

#### [MODIFY] [save_locations.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/save/save_locations.cpp)
- Оновити метод `GetWebdavLocations()`: прибрати завантаження `settings_url` з `App::GetWebdavUrl()`. Залишити тільки фільтрацію локацій з `location::Load()`, які починаються зі схем `webdav://`, `http://`, `https://`.

#### [MODIFY] [save_menu_ops.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/save/save_menu_ops.cpp)
- У методі `BackupSaves()` (під час автосинхронізації) замість безумовного вибору першої локації (`webdav_locations.front()`) шукати локацію за ім'ям, збереженим в `App::GetWebdavUrl()`. Якщо локацію не знайдено, але список не порожній, використовувати `webdav_locations.front()`.

---

### [Component: Settings Menu]

---

#### [MODIFY] [app_display_options.cpp](file:///d:/git/dev/sphaira/sphaira/source/app_display_options.cpp)
- Імпортувати `"ui/menus/save/save_locations.hpp"`.
- Переписати `DisplayWebdavOptions()`: замість полів введення сервера, юзера та пароля показувати один вибір `Sync Location`, де користувач вибирає одну з налаштованих у `Sources` WebDAV локацій (або `None`). При виборі зберігати назву локації в `App::SetWebdavUrl(name)`.

#### [MODIFY] [settings_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/settings_menu.cpp)
- Оновити `BuildSourcesCategoryItems()`: при видаленні джерела, назва якого збігається з поточною вибраною локацією автосинхронізації (`App::GetWebdavUrl()`), очищати її (`App::SetWebdavUrl("")`).

---

### [Component: Version]

---

#### [MODIFY] [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt)
- Підвищити версію проекту `sphaira_VERSION` до `0.13.222`.

## Verification Plan (Виконано)

### Automated Tests
- Автоматизовані тести відсутні.

### Manual Verification (Готово до тестування на консолі)
- **WSL-збірка**: Виконано успішно. Проект скомпільовано без помилок лінкера.
- **Тестування на консолі**:
  - У `Tools -> Settings -> Sources` додати WebDAV джерело (наприклад, з назвою `"Nextcloud"`).
  - Перейти в `Tools -> Settings -> WebDAV` (WebDAV Settings).
  - Переконатися, що замість полів введення там з'явився пункт `"Sync Location"` зі значенням за замовчуванням `"None"`.
  - Натиснути на нього, перевірити, що відкрився PopupList з варіантами `"None"` та `"Nextcloud"`. Вибрати `"Nextcloud"`.
  - Переконатися, що налаштування збереглося.
  - Запустити бекап сейву в меню сейвів при активованій опції `Auto-sync saves after backup`. Переконатися, що сейви успішно автосинхронізуються на джерело `"Nextcloud"`.
  - Видалити джерело `"Nextcloud"` у `Settings -> Sources` та перевірити, що в `WebDAV Settings` активна локація змінилася на `"None"`.
