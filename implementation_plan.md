# План реалізації: Дедуплікація (Фаза 14) та Рефакторинг `app.cpp` (Фаза 13)

Цей план описує кроки з дедуплікації коду та рефакторингу основного класу застосунку `App` у проекті Sphaira (Kefir Hub). Він базується на аудиті результатів рефакторингу, описаному в [REFACTOR_PLAN2.md](file:///d:/git/dev/sphaira/REFACTOR_PLAN2.md).

Мета роботи — зменшити розмір файлу `app.cpp` (шляхом винесення методів налаштувань відображення) та видалити дублікати функцій і класів, не змінюючи при цьому поведінку програми.

---

## Пропоновані зміни

Зміни розбито на окремі логічні фази та кроки. Кожен крок виконуватиметься окремим коммітом з оновленням версії програми та описом у `walkthrough.md`.

---

## Частина I: Рефакторинг `app.cpp` (Фаза 13)

### Крок 13.1: Виділення методів відображення налаштувань у новий файл [ВИКОНАНО у v0.13.194]
Ми виділимо методи налаштувань відображення (бічні панелі опцій) з `app.cpp` в окремий файл реалізації `app_display_options.cpp`.

#### [NEW] [app_display_options.cpp](file:///d:/git/dev/sphaira/sphaira/source/app_display_options.cpp)
- Створити новий файл реалізації.
- Перенести визначення наступних методів класу `App`:
  - `DisplayThemeOptions`
  - `DisplayNetworkOptions` (порожня заглушка)
  - `DisplayMiscOptions`
  - `DisplayAdvancedOptions`
  - `DisplayInstallOptions`
  - `DisplayDumpOptions`
  - `ShowEnableInstallPrompt`
  - `ShowEnableInstallPromptOption`
- Підключити необхідні заголовні файли (`app.hpp`, `i18n.hpp`, `swkbd.hpp` та відповідні меню).

#### [MODIFY] [app.cpp](file:///d:/git/dev/sphaira/sphaira/source/app.cpp)
- Вилучити визначення перенесених методів.

#### [MODIFY] [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt)
- Додати `source/app_display_options.cpp` до списку джерел `add_executable(sphaira)`.

---

## Частина II: Дедуплікація (Фаза 14)

### Крок 14.1: Оновлення статусу `HoldConfirmBox`
*Цей крок фактично вже виконаний у попередній сесії коммітом `bd31eae` (стилі та тривалість розділено, локальний клас видалено).*
- Ми позначимо цей крок як виконаний у плані та оновимо `task.md`.

### Крок 14.2: Дедуплікація `hexIdToStr` та `HashStr` [ВИКОНАНО у v0.13.195]
Буде видалено 4 локальні копії структури `HashStr` та функції `hexIdToStr` і замінено їх на єдину реалізацію з `utils/utils.hpp`.

#### [MODIFY] [save_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/save_menu.cpp)
- Видалити локальні `struct HashStr` та `hexIdToStr`.

#### [MODIFY] [gc_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/gc_menu.cpp)
- Видалити локальні `struct HashStr` та `hexIdToStr`.
- Додати `#include "utils/utils.hpp"`.
- Замінити виклики `hexIdToStr` на `utils::hexIdToStr`.

#### [MODIFY] [game_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/game_menu.cpp)
- Видалити локальні `struct HashStr` та `hexIdToStr`.
- Додати `#include "utils/utils.hpp"`.
- Замінити виклики `hexIdToStr` на `utils::hexIdToStr`.

#### [MODIFY] [yati.cpp](file:///d:/git/dev/sphaira/sphaira/source/yati/yati.cpp)
- Видалити локальні `struct HashStr` та `hexIdToStr`.
- Додати `#include "utils/utils.hpp"`.
- Замінити виклики `hexIdToStr` на `utils::hexIdToStr`.

### Крок 14.3: Об'єднання функцій `Trim` [ВИКОНАНО у v0.13.196]
Винесення спільних утилят обрізання пробілів у `utils/utils.hpp`.

#### [MODIFY] [utils.hpp](file:///d:/git/dev/sphaira/sphaira/include/utils/utils.hpp)
- Оголосити `std::string Trim(std::string str);` та `std::string TrimAsciiWhitespace(std::string str);` у просторі назв `sphaira::utils`.

#### [MODIFY] [utils.cpp](file:///d:/git/dev/sphaira/sphaira/source/utils/utils.cpp)
- Додати реалізацію обох функцій `Trim` (зі зняттям лапок) та `TrimAsciiWhitespace` (лише символи пробілів).

#### [MODIFY] [settings_fs_utils.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/menus/settings/settings_fs_utils.hpp)
- Видалити оголошення `Trim`.

#### [MODIFY] [settings_fs_utils.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/settings/settings_fs_utils.cpp)
- Видалити локальну реалізацію `Trim`.
- Додати `using sphaira::utils::Trim;` або викликати `utils::Trim` напряму.

#### [MODIFY] [kefir_firmware.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/menus/kefir/kefir_firmware.hpp)
- Видалити оголошення `Trim` та `TrimAsciiWhitespace`.

#### [MODIFY] [kefir_firmware.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/kefir/kefir_firmware.cpp)
- Видалити локальні реалізації обох функцій.
- Додати `#include "utils/utils.hpp"`.
- Замінити виклики на `sphaira::utils::TrimAsciiWhitespace` та `sphaira::utils::Trim` відповідно.

### Крок 14.4: Порівняння `settings_fs_utils` та `fs.cpp`
> [!NOTE]
> Локальні функції stdio-копіювання та видалення (`CopyFileSimple`, `DeletePath`, `CopyDirectoryContents`, `MovePath`) вирішено **залишити** в `settings_fs_utils.cpp` для стабільності роботи з конфігами через stdio.
> Проте, ми замінимо перевірку існування теки:

#### [MODIFY] [settings_fs_utils.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/settings/settings_fs_utils.cpp)
- Замінити реалізацію `DirectoryExists(const char* path)` на прямий виклик `fs::DirExists(path)` з `fs.hpp`.

### Крок 14.5: URL/HTML декодування
> [!NOTE]
> Після детального аналізу вирішено **пропустити** цей крок. `UrlDecode` у `web_http.cpp` реалізована вручну для веб-сервера без залежностей, тоді як `MountCurlDevice::url_decode` використовує `curl_unescape` з libcurl. Ми додамо коментарі у код, які пояснюють цю різницю.

---

## План верифікації

### Автоматичні тести (Збірка проекту)
Після кожного кроку виконуватиметься збірка проекту в середовищі WSL:
```bash
make
```
або за допомогою скриптів збірки:
```bash
./build.sh
```

### Ручна перевірка
- Перевірка успішності створення файлу `kefir-hub.nro` після завершення кожної фази.
- Контроль за тим, що ніякі функції не визначені двічі та поведінка інтерфейсу (включаючи `HoldConfirmBox`) не змінилася.
