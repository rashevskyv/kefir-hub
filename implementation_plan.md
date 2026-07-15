# UX-покращення Module Manager (id 44, 46) та виправлення сенсорної навігації Settings (id 48)

Цей план описує зміни, спрямовані на покращення досвіду користувача (UX) у меню "Керування модулями" (Module Manager) та виправлення поведінки сенсорного керування у двоколонковому меню налаштувань (Settings).

## Proposed Changes

### [Component: Sphaira (C++)]

---

#### [MODIFY] [app_paths.hpp](file:///d:/git/dev/sphaira/sphaira/include/app_paths.hpp)
- Додати константу для файлу реєстру описів модулів:
  ```cpp
  inline const std::string MODULES = DATA_ROOT + "/modules.json";
  ```

---

#### [MODIFY] [uninstaller_menu.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/menus/uninstaller_menu.hpp)
- Розширити структуру `ModuleItem` полем `description` для збереження локалізованого опису модуля:
  ```cpp
  std::string description;
  ```

---

#### [MODIFY] [uninstaller_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/uninstaller_menu.cpp)
- **Завантаження описів (`LoadModules`)**:
  - Перед скануванням каталогів зчитати файл `/config/kefir/modules.json`.
  - Якщо файл не існує, створити його з дефолтними описами популярних модулів (emuiibo, Mission Control, sys-clk, ldn_mitm, sys-ftpd) англійською та українською мовами.
  - Розпарсити JSON за допомогою `yyjson`, зчитуючи опис для поточної мови (визначається через `App::GetLanguage() == 14 ? "uk" : "en"`) із фолбеком на `"en"`.
  - Під час парсингу `toolbox.json` додавати відповідний опис з реєстру за TID (якщо опису немає — `"No description provided"_i18n`).
- **Візуальні зміни (`Draw` / `UninstallerMenu` constructor)**:
  - Збільшити висоту рядка списку з `66.f` до `82.f` для комфортного розташування нової інформації.
  - Зсунути початок списку вгору до `GetY() + 45.f` (замість `GetY() + 79.f`), щоб перший рядок стояв одразу під легендою.
  - Вирівняти область `nvgScissor` у `Draw()` відповідно до нової геометрії: `list_x = 75.f`, `list_y = GetY() + 45.f`, `list_w = 1070.f`, `list_h = 574.f`, розширивши її на `SELECTION_OUTLINE_PAD` з усіх боків для запобігання обрізанню рамки фокусу.
  - У `Draw()` прибрати виведення старого статусу `StatusText(item)`. Замість цього вивести два окремих статуси праворуч:
    - `Now: On` (зелений) / `Now: Off` (сірий)
    - `After reboot: Enabled` (жовтий) / `After reboot: Disabled` (сірий)
  - Перейменувати суфікс ` - reboot required` на ` - Applies after reboot` під назвою модуля.
- **Оновлення дій (`ToggleSelectedModule` та `UpdateSubheading`)**:
  - У `UpdateSubheading()` встановлювати підзаголовок меню рівним `item.description` вибраного модуля.
  - У `ToggleSelectedModule()` для модулів з `requires_reboot` показувати повідомлення `"This module applies after reboot. Use autostart."_i18n` замість старого `"Use Autostart for reboot-required modules"`.

---

#### [MODIFY] [settings_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/settings_menu.cpp)
- **Геометрія списків**:
  - Передати списку `m_category_list` його реальний viewport `Vec4{76.f, 138.f, 300.f, 448.f}` замість загального `m_pos`.
  - Передати списку `m_item_list` його реальний viewport `Vec4{420.f, 132.f, 780.f, 462.f}` замість загального `m_pos`.
- **Обробка Touch-навігації (`Update`)**:
  - У `Update()` відстежувати зміну фокусу через локальну змінну `focus_changed`. Якщо відбувся клік у межах viewport іншої колонки, перемкнути `m_focus_pane` та виставити `focus_changed = true`.
  - У колбеках `OnUpdate` обох списків виконувати `FireAction(Button::A)` лише у випадку, якщо клік здійснено по вже виділеному елементу і при цьому фокус НЕ змінювався у поточному кадрі (`!focus_changed`). Це запобігає випадковій активації параметрів при першому тапі для переходу в праву колонку.

---

### [Component: i18n (Локалізація)]

#### [MODIFY] [en.json](file:///d:/git/dev/sphaira/assets/romfs/i18n/en.json) та [uk.json](file:///d:/git/dev/sphaira/assets/romfs/i18n/uk.json)
- Додати переклади для нових рядків:
  - `"Now: On"`
  - `"Now: Off"`
  - `"After reboot: Enabled"`
  - `"After reboot: Disabled"`
  - `" - Applies after reboot"`
  - `"This module applies after reboot. Use autostart."`
  - `"No description provided"`

---

## Verification Plan

### Automated Tests
- Автоматизовані тести відсутні.

### Manual Verification
- **WSL-збірка**: Перевірити успішність компіляції NRO (`make` / `cmake`).
- **Тестування на консолі**:
  - Запустити Module Manager. Перевірити, що список починається вище і рамка фокусу не обрізається.
  - Перевірити наявність файлу `/config/kefir/modules.json` на SD-карті та відображення описів у підзаголовку.
  - Змінити стан модуля (A) або автозапуск (Y) та перевірити незалежне оновлення рядків `Now` та `After reboot`.
  - Перевірити тач-навігацію у Settings: кліки по лівій колонці перемикають категорії, перший клік по параметру лише виділяє його, повторний клік — відкриває.
