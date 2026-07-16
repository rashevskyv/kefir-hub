# Список завдань: Виправлення перекладів та автоперемикання мови (v0.13.230)

- [x] **Підготовка та налаштування**:
  - [x] Створити план впровадження та список завдань
- [x] **Реалізація змін**:
  - [x] Оновити `App::SetLanguage` у `app.hpp` та `app_settings.cpp` (додати параметр `prompt_restart = true`)
  - [x] Оновити `RemoveInterfaceTranslation` у `settings_translations.cpp` (ігнорувати `FsError_TargetLocked`)
  - [x] Оновити `ParseInterfaceTranslations` у `settings_translations.cpp` (фільтрація відсутніх/порожніх перекладів)
  - [x] Реалізувати `TryAutoSwitchLanguage` та інтегрувати її в `InstallInterfaceTranslation`
  - [x] Ітерувати версію програми до `0.13.230` у `sphaira/CMakeLists.txt`
- [x] **Верифікація**:
  - [x] Компіляція проекту за допомогою `make` у WSL
