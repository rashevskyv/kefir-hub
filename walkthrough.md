# Результати впровадження: Виправлення перекладів, фільтрація та автоперемикання мови (v0.13.230)

Усі заплановані зміни було успішно реалізовано.

## Зроблені зміни

### [Component: Core Application settings]
- **[app.hpp](file:///d:/git/dev/sphaira/sphaira/include/app.hpp)**:
  - Оновлено сигнатуру функції `SetLanguage`: додано параметр `bool prompt_restart = true`, щоб мати можливість перемикати мову інтерфейсу без виведення діалогового вікна із запитом на перезапуск програми.
- **[app_settings.cpp](file:///d:/git/dev/sphaira/sphaira/source/app_settings.cpp)**:
  - Оновлено реалізацію `App::SetLanguage` для підтримки прапорця `prompt_restart`. Якщо він встановлений у `false`, запит на перезапуск не показується.

### [Component: UI Settings (Translations)]
- **[settings_translations.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/settings/settings_translations.cpp)**:
  - **RemoveInterfaceTranslation**: Додано ігнорування коду помилки `FsError_TargetLocked`. Це дозволяє уникнути критичної помилки при видаленні заблокованих системою файлів перекладу, оскільки після видалення консоль все одно перезавантажується (що зніме блокування з усіх файлів).
  - **ParseInterfaceTranslations**: Тепер перед додаванням перекладу до списку доступних виконується перевірка наявності відповідного JSON-файлу за допомогою `fs::FileExists` та його перевірка за допомогою `ReadInterfaceReplacementOptions`. Якщо файл відсутній або не містить опцій заміни, ця мовна папка взагалі не буде виведена в меню.
  - **TryAutoSwitchLanguage**: Додано нову функцію, яка виконує реєстронезалежний пошук назви перекладу серед підтримуваних мов Sphaira (`LANGUAGE_ITEMS`) і, у разі збігу, викликає `App::SetLanguage(index, false)`.
  - **InstallInterfaceTranslation**: Додано виклик `TryAutoSwitchLanguage(entry.name)` безпосередньо перед перезавантаженням консолі.

### [Component: CMake Configuration]
- **[CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt)**:
  - Версію проекту оновлено до `0.13.230`.

## Результати перевірки

Код було успішно змінено та підготовлено для компіляції.
