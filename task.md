# Список завдань: Покращення джерел та зсув статус-бару (v0.13.220)

- [x] **Статус-бар**:
  - [x] Збільшити фіксований зсув `start_x` для батареї з 64 до 94 в `menu_base.cpp` при зарядці (`charger_type != 0`).
  - [x] Збільшити фіксований зсув `start_x` для батареї з 64 до 94 в `menu_base.cpp` при розрядці (`draw(ThemeEntryID_TEXT, 94, ...)`).

- [x] **Location Core**:
  - [x] Додати декларацію `Remove` в `location.hpp`.
  - [x] Реалізувати `Remove` в `location.cpp`.

- [x] **File Browser (UI)**:
  - [x] Додати декларацію `AddNetworkLocationInteractive` в `filebrowser.hpp`.
  - [x] Реалізувати `AddNetworkLocationInteractive` в `filebrowser.cpp`.
  - [x] Видалити `"Mount"` та `"Add network location"` з `DisplayAdvancedOptions()` у `filebrowser.cpp`.
  - [x] Додати пункт `"Add network location"_i18n` в `ShowSourcePicker()` у `filebrowser.cpp`.
  - [x] Оновити пункт `"Upload"_i18n` на `"Upload to network location"_i18n` в `filebrowser.cpp` та оновити повідомлення/заголовки у `filebrowser_ops.cpp`.

- [x] **Settings Menu (UI)**:
  - [x] Додати категорію `"Sources"_i18n` та реалізувати `BuildSourcesCategoryItems()` у `settings_menu.cpp`.

- [x] **Локалізація**:
  - [x] Додати переклади нових рядків в `en.json`.
  - [x] Додати переклади нових рядків в `uk.json`.

- [x] **Версія**:
  - [x] Оновити версію проекту на `0.13.220` у `sphaira/CMakeLists.txt`.

- [x] **Збірка**:
  - [x] Виконати повну WSL-збірка проекту і перевірити успішність компіляції `kefir-hub.nro`.

# Список завдань: Спільні джерела та виправлення стабільності (v0.13.221)

- [x] **Виправлення крашу**:
  - [x] Усунути Use-After-Free краш в `AddNetworkLocationInteractive` при додаванні локації шляхом перенесення виклику `on_success()` в асинхронну чергу `evman::push`.

- [x] **Спільні джерела (WebDAV saves integration)**:
  - [x] Динамічно додавати WebDAV джерело з налаштувань сейвів (`App::GetWebdavUrl()`) до результату `location::Load()`.
  - [x] Інтегрувати підтримку видалення джерела `"WebDAV (Saves Sync)"` у `BuildSourcesCategoryItems()` в `settings_menu.cpp`, щоб воно очищало налаштування WebDAV для сейвів в `config.ini`.

- [x] **Версія**:
  - [x] Оновити версію проекту на `0.13.221` у `sphaira/CMakeLists.txt`.

- [x] **Збірка**:
  - [x] Перевірити успішність WSL-збірки `kefir-hub.nro`.
