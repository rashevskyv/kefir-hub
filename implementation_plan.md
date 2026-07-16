# Виправлення видалення перекладів, фільтрація мовних пакетів та автоперемикання мови (v0.13.230)

Цей план впровадження описує зміни для вирішення таких проблем:
1. Помилка `FsError_TargetLocked` при видаленні встановленого перекладу. Ми будемо ігнорувати цю помилку під час видалення, оскільки після видалення консоль все одно перезавантажується, що звільняє файлові дескриптори.
2. Відображення мовних папок у меню встановлення, навіть якщо самого перекладу немає (відсутній файл JSON або він порожній). Ми додамо перевірку наявності JSON-файлу перекладу та його вмісту.
3. Автоматичне перемикання мови інтерфейсу в Sphaira (KefirHub) відповідно до встановленого перекладу (якщо мова підтримується програмою).

## Необхідний розгляд користувачем

> [!NOTE]
> Перемикання мови інтерфейсу Sphaira відбуватиметься без відображення діалогового вікна із запитом на перезапуск програми (Kefir Hub), оскільки після встановлення перекладу консоль автоматично перезавантажується.

## Запитання

Немає.

## Пропоновані зміни

---

### [Component: UI Settings (Translations)]

#### [MODIFY] [settings_translations.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/menus/settings/settings_translations.hpp)
- Жодних змін у цьому файлі не планується.

#### [MODIFY] [settings_translations.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/settings/settings_translations.cpp)
- **RemoveInterfaceTranslation**: Додати ігнорування коду помилки `FsError_TargetLocked` під час видалення файлів (аналогічно до `FsError_PathNotFoundFsDev`).
- **ParseInterfaceTranslations**: Додати перевірку наявності JSON-файлу за допомогою `fs::FileExists` та валідацію на наявність опцій через `ReadInterfaceReplacementOptions`. Якщо переклад не налаштований або відсутній, відповідна папка не додаватиметься до списку.
- **TryAutoSwitchLanguage**: Додати допоміжну функцію, яка реєстронезалежно шукатиме назву перекладу в `LANGUAGE_ITEMS` та викликатиме `App::SetLanguage` без запиту на перезавантаження.
- **InstallInterfaceTranslation**: Додати виклик `TryAutoSwitchLanguage` перед перезавантаженням консолі.

---

### [Component: Core Application settings]

#### [MODIFY] [app.hpp](file:///d:/git/dev/sphaira/sphaira/include/app.hpp)
- Оновити сигнатуру `SetLanguage`:
  ```cpp
  static void SetLanguage(long index, bool prompt_restart = true);
  ```

#### [MODIFY] [app_settings.cpp](file:///d:/git/dev/sphaira/sphaira/source/app_settings.cpp)
- Оновити реалізацію `App::SetLanguage`: показувати вікно запиту перезапуску тільки якщо `prompt_restart == true`.

---

### [Component: CMake Configuration]

#### [MODIFY] [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt)
- Ітерувати версію до `0.13.230`.

## План верифікації

### Автоматичні тести
- Автоматичні тести відсутні.

### Ручна перевірка
1. Зібрати `kefir-hub.nro` через WSL.
2. Перевірити, що список мовних пакетів для встановлення не містить порожніх папок (де відсутні відповідні JSON-файли).
3. Перевірити, що видалення встановленого перекладу не призводить до помилки `FsError_TargetLocked`.
4. Перевірити, що після встановлення українського/португальського перекладу мова інтерфейсу Sphaira перемикається автоматично без зайвих запитів.
