# Список завдань: Оновлення мовних пакетів, кругова навігація та фільтрація перекладів (v0.13.225)

- [x] **Кругова навігація у підменю налаштувань**:
  - [x] Додати `m_list->SetWrap(true);` у `SoftwareMenu` в `settings_menu.cpp`.
  - [x] Додати `m_list->SetWrap(true);` у `DbiMenu` в `settings_menu.cpp`.
  - [x] Додати `m_list->SetWrap(true);` у `KefirSettingsMenu` in `settings_menu.cpp`.
  - [x] Додати `m_list->SetWrap(true);` у `ThemesMenu` в `settings_menu.cpp`.
  - [x] Додати `m_list->SetWrap(true);` у `TranslateMenu` в `settings_menu.cpp`.

- [x] **Динамічне оновлення тексту мовних пакетів**:
  - [x] Додати логіку перевірки існування `TRANSLATE_PACKAGE` в `BuildTranslateItems()`.
  - [x] Використовувати `"Update language packs"_i18n` та `"Update the UltraHand language package list."_i18n` при наявності файлу.

- [x] **Фільтрація та перевірка варіантів перекладу**:
  - [x] Реалізувати логіку отримання мови та регіону консолі за допомогою libnx API.
  - [x] Реалізувати функцію `IsOptionApplicable` для фільтрації варіантів перекладу.
  - [x] Додати перевірку результатів фільтрації: якщо варіанти є, показати `PopupList` з ними; якщо немає, показати попередження через `OptionBox` із переліком всіх доступних варіантів.

- [x] **Оновлення локалізації**:
  - [x] Додати нові ключі локалізації в усі JSON файли у `assets/romfs/i18n/`.

- [x] **Версія**:
  - [x] Оновити версію проекту на `0.13.225` у `sphaira/CMakeLists.txt`.

- [x] **Збірка**:
  - [x] Виконати WSL-збірку `kefir-hub.nro`.
