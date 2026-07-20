# Результати впровадження: Дозволення успішного EOF при коротких читаннях репаків (v0.13.281)

## Опис змін

Виправлено помилку `SphairaError_StreamUnexpectedEof`, яка виникала при встановленні перепакованих NSP-файлів (репаків) через MTP:
1. **Суть проблеми**:
   - Деякі репаки NSP створюються шляхом модифікації оригінальних файлів без повного дотримання вирівнювання за розміром (відсутні необов'язкові байти заповнення/паддінгу в кінці файлу). При цьому в PFS0-заголовку NSP-файлу вказані завеликі розміри для деяких NCA.
   - В оригінальній реалізації `Stream::Read`, якщо потік закінчувався і повертав 0 байтів (`ReadChunk` повернув EOF) до того, як було прочитано весь запитаний обсяг даних, потік викидав фатальну помилку `Result_StreamUnexpectedEof`.
   - Також у `ThreadData::Read` (yati) перевірка `R_UNLESS(size == *bytes_read, Result_YatiInvalidNcaReadSize)` примусово вимагала прочитати саме стільки байтів, скільки вказано в PFS0-заголовку.
   - Через це інсталятор падав перед самим завершенням процесу, хоча всі реальні стиснені/необхідні дані NCA були повністю прочитані й розпаковані.
2. **Вирішення**:
   - Оновлено метод `Stream::Read` у [stream.cpp](file:///d:/git/dev/sphaira/sphaira/source/yati/source/stream.cpp): замість викидання помилки `Result_StreamUnexpectedEof` при досягненні EOF (`bytes_read == 0`) метод просто виходить із циклу читання та повертає успіх з фактично зчитаною кількістю байтів.
   - Оновлено метод `ThreadData::Read` у [yati.cpp](file:///d:/git/dev/sphaira/sphaira/source/yati/yati.cpp): вилучено вимогу про точний збіг розміру зчитування (`Result_YatiInvalidNcaReadSize`).
   - Це дозволяє інсталятору успішно завершити роботу, якщо весь контент NCA був отриманий і розпакований, навіть якщо наприкінці файлу в потоці бракувало кілька сотень байтів неважливого паддінгу. Якщо ж файл дійсно пошкоджено, встановлення все одно перерветься помилкою розпакування ZSTD або запису у `writeFuncInternal`, що гарантує цілісність даних.

## Зроблені зміни

### [Component: Потоковий інсталятор yati]
- **[stream.cpp](file:///d:/git/dev/sphaira/sphaira/source/yati/source/stream.cpp)**:
  - Змінено обробку EOF у `Stream::Read` для повернення успіху при читанні замість викидання помилки `Result_StreamUnexpectedEof`.
- **[yati.cpp](file:///d:/git/dev/sphaira/sphaira/source/yati/yati.cpp)**:
  - Вилучено перевірку на точну відповідність зчитаного обсягу `Result_YatiInvalidNcaReadSize` у методі `ThreadData::Read`.

### [Component: CMake Configuration]
- **[CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt)**:
  - Збільшено версію програми `sphaira_VERSION` до `0.13.281`.

---

# Результати впровадження: Виправлення помилки завершення MTP-інсталяції (v0.13.280)

## Опис змін

Виправлено серйозну помилку у процесі інсталяції ігор через USB MTP, яка виникала наприкінці передачі файлу та призводила до збою інсталяції з помилкою `SphairaError_TransferCancelled`:
1. **Суть проблеми**:
   - При встановленні NSP-файлів через MTP, коли передача файлу з боку ПК завершується, фоновий MTP-сервер деактивує потік інсталяції (`Stream::Disable()`).
   - Після цього інсталятор робить останній запит до потоку (`Stream::ReadChunk()`), щоб прочитати залишок даних або виявити кінець файлу (EOF).
   - Раніше при неактивному потоці та порожньому буфері метод `ReadChunk` передчасно викидав помилку `Result_TransferCancelled` замість того, щоб коректно повернути успішний EOF (0 прочитаних байтів).
   - Через це інсталяція репаків або ігор із додатковим овер-ридом розміру/метаданих падала із помилкою в самому кінці, а встановлені титли не реєструвалися системою.
2. **Вирішення**:
   - Оновлено логіку `Stream::ReadChunk()` в `install_stream_menu_base.cpp`. Тепер при досягненні EOF (`!m_active && m_buffer.empty()`) метод коректно повертає `0` (успіх) та записує `0` у `*bytes_read`. Помилка `Result_TransferCancelled` тепер викидається лише у випадку, якщо користувач дійсно скасував встановлення (`m_token.stop_requested()`).

## Зроблені зміни

### [Component: Базовий інсталятор потоку]
- **[install_stream_menu_base.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/install_stream_menu_base.cpp)**:
  - Змінено обробку умов виходу в `Stream::ReadChunk()`. Успішний EOF повертає 0 з кодом успіху.

### [Component: CMake Configuration]
- **[CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt)**:
  - Збільшено версію програми `sphaira_VERSION` до `0.13.280`.

---

# Результати впровадження: Ручне керування MTP у розділі інструментів (v0.13.279)

## Опис змін

Оскільки автоматичне підхоплення MTP Horizon OS при першому підключенні кабелю може поводитися нестабільно на деяких консолях через внутрішні процеси системи, ми додали можливість ручного запуску/зупинки MTP безпосередньо з інтерфейсу програми:
1. **Вилучення Runtime Mode з швидкого меню**:
   - У меню інструментів (Tools) при відкритті меню з'єднань через Plus (+)/Start прибрано пункт `Runtime Mode` як менш важливий.
2. **Додавання опції Mount MTP / MTP: Active**:
   - На місце Runtime Mode додано нову динамічну опцію швидкого запуску/зупинки MTP.
   - Опція використовує нову функцію `haze::IsRunning()` для перевірки поточного стану фонового сервера MTP:
     - Якщо MTP не запущено, відображається пункт **Mount MTP** (Змонтувати MTP). Натискання на нього запускає MTP-сервер та видає повідомлення "MTP started" (або помилку).
     - Якщо MTP вже запущено, відображається пункт **MTP: Active** (MTP: Активний). Натискання на нього зупиняє MTP-сервер та видає повідомлення "MTP stopped".
3. **Локалізація**:
   - Додано нові українські переклади для статусів та дій MTP до файлу локалізації `uk.json`.
4. **Малювання лейблів жирним шрифтом у бічній панелі**:
   - Оновлено систему рендерингу тексту у класі `SidebarEntryBase` та `ScrollingText`: тепер всі заголовки (лейбли) елементів бічного меню малюються жирним шрифтом (через овер-драв), у тому числі назва опції MTP.

Це дозволяє користувачеві у будь-який момент примусово ініціалізувати MTP-з'єднання з комп'ютером, якщо HOS автоматично не підхопила кабель при підключенні.

## Зроблені зміни

### [Component: Ядро програми та фонові сервіси]
- **[haze_helper.hpp](file:///d:/git/dev/sphaira/sphaira/include/haze_helper.hpp)** / **[haze_helper.cpp](file:///d:/git/dev/sphaira/sphaira/source/haze_helper.cpp)**:
  - Додано та реалізовано публічну функцію `haze::IsRunning()`, яка повертає поточний стан запуску сервера MTP (`g_is_running`).

### [Component: Меню інструментів]
- **[tools_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/tools_menu.cpp)**:
  - Підключено заголовок `haze_helper.hpp`.
  - У методі `DisplayConnectionOptions()` прибрано пункт `Runtime Mode`.
  - Додано логіку перевірки `haze::IsRunning()` та виклики `App::SetMtpEnable()` для динамічного перемикання MTP-з'єднання.

### [Component: Інтерфейс та бічна панель]
- **[scrolling_text.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/scrolling_text.hpp)** / **[scrolling_text.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/scrolling_text.cpp)**:
  - Додано параметр `bold` у функцію `Draw()` для підтримки рендерингу жирного тексту.
- **[sidebar.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/sidebar.hpp)** / **[sidebar.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/sidebar.cpp)**:
  - Додано метод `SetTitle` для зміни заголовків пунктів меню на ходу.
  - Налаштовано малювання всіх заголовків (лейблів) бічного меню жирним шрифтом (включно із заголовком та назвами опцій).

### [Component: Локалізація]
- **[uk.json](file:///d:/git/dev/sphaira/assets/romfs/i18n/uk.json)**:
  - Додано українські переклади для ключів `"MTP: Active"`, `"Mount MTP"`, `"MTP is currently active. Click to disable it."`, `"Start the MTP responder to browse SD card files on PC."`, `"MTP started"`, `"MTP stopped"`, `"Failed to start MTP"`.

### [Component: CMake Configuration]
- **[CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt)**:
  - Збільшено версію програми `sphaira_VERSION` до `0.13.279`.

---

# Результати впровадження: виправлення автозапуску MTP та усунення конфлікту з USB HDD (v0.13.278)

## Опис змін

Реалізовано взаємовиключну роботу режимів USB Host (зовнішні HDD) та USB Device (MTP/USB-інсталяція) для усунення конфлікту та відновлення автоматичного запуску MTP при підключенні кабелю:
1. **Автоматичне вимкнення HDD при старті MTP**:
   - У `haze::Init()` (файл [haze_helper.cpp](file:///d:/git/dev/sphaira/sphaira/source/haze_helper.cpp)) перед ініціалізацією MTP додано виклик `usbHsFsExit()`, якщо сервіс HDD увімкнений.
   - Це повністю звільняє USB-порт консолі, дозволяючи MTP успішно ініціалізуватися.
2. **Перезапуск HDD при зупинці MTP**:
   - У `haze::Exit()` після завершення роботи MTP додано повторну ініціалізацію `usbHsFsInitialize(1)` (з урахуванням налаштування `App::GetWriteProtect()`).
   - У разі помилки запуску MTP в `haze::Init()`, HDD також автоматично перезапускається.
3. **Умовний старт HDD при завантаженні**:
   - У конструкторі `App` (файл [app.cpp](file:///d:/git/dev/sphaira/sphaira/source/app.cpp)) ініціалізація HDD тепер не запускається, якщо увімкнено автозапуск MTP, щоб запобігти миттєвому блокуванню USB-порту.
4. **Вимикання HDD під час USB-інсталяції**:
   - У конструкторах [usb_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/usb_menu.cpp) та [dbi_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/dbi_menu.cpp) додано виклик `usbHsFsExit()` для зупинки HDD під час роботи з USB-кабелем.
   - У деструкторах цих меню HDD перезапускається, якщо MTP не буде повторно активовано.

Це гарантує, що при підключенні кабелю до комп'ютера MTP автоматично і успішно вмикається, як і раніше, а комп'ютер одразу виявляє приставку.

## Зроблені зміни

### [Component: Ядро програми та фонові сервіси]
- **[haze_helper.cpp](file:///d:/git/dev/sphaira/sphaira/source/haze_helper.cpp)**:
  - Підключено заголовок `#include <usbhsfs.h>`.
  - У `Init()` додано виклик `usbHsFsExit()`, а при помилці — відновлення HDD.
  - У `Exit()` додано перезапуск HDD.
- **[app.cpp](file:///d:/git/dev/sphaira/sphaira/source/app.cpp)**:
  - Налаштовано умовний запуск HDD у конструкторі `App::App`.
  - Перенесено ініціалізацію MTP (`haze::Init()`) з конструктора `App::App` до першої ітерації головного циклу `App::Loop()`. Це запобігає збою ініціалізації USB при старті додатка та гарантує стабільне підключення кабелю на запущеному додатку.

### [Component: USB та DBI інсталятори]
- **[usb_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/usb_menu.cpp)**:
  - Підключено заголовок `#include <usbhsfs.h>`.
  - Додано вимкнення HDD на старті та відновлення при виході.
- **[dbi_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/dbi_menu.cpp)**:
  - Підключено заголовок `#include <usbhsfs.h>`.
  - Додано вимкнення HDD на старті та відновлення при виході в конструкторі/деструкторі.

### [Component: CMake Configuration]
- **[CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt)**:
  - Збільшено версію програми `sphaira_VERSION` до `0.13.278`.

---

# Результати впровадження: Примусове перенесення payload.bin та папки bootloader при оновленні Кефіру (v0.13.271)

## Опис змін

Одразу після успішного розпакування завантаженого zip-архіву Кефіру в тимчасову директорію `/kefir` та виконання `Commit()`, додано наступні примусові кроки:
1. **Перенесення `payload.bin`**:
   - Перевіряється наявність файлу `/kefir/payload.bin`.
   - Якщо в корені SD-карти вже існує `/payload.bin`, він видаляється.
   - Новий файл переноситься за допомогою функції `RenameFile` безпосередньо в корінь SD-карти: `/payload.bin`.
2. **Злиття папки `bootloader`**:
   - Реалізовано функцію рекурсивного злиття `MergeDirectory(pbox, fs, src_dir, dst_dir)`:
     - Обходить дерево джерела `/kefir/bootloader`.
     - Для кожної піддиректорії рекурсивно викликає злиття.
     - Для файлів — якщо однойменний файл існує в `/bootloader`, він видаляється, і новий файл переноситься на його місце через `RenameFile`.
     - Це дозволяє оновити всі файли Кефіру та зберегти унікальні файли користувача в директорії `/bootloader` (теми, конфіги, сторонні пейлоади тощо).

Це забезпечує безпечне оновлення завантажувача з повним збереженням налаштувань користувача.

## Зроблені зміни

### [Component: Меню оновлення Кефіру]
- **[kefir_changelog.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/kefir/kefir_changelog.cpp)**:
  - Додано рекурсивну функцію `MergeDirectory`.
  - У функції `DownloadAndInstallKefir` додано логіку перенесення файлу `/kefir/payload.bin` та злиття папки `/kefir/bootloader` з `/bootloader` через `MergeDirectory`.

### [Component: CMake Configuration]
- **[CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt)**:
  - Збільшено версію додатка `sphaira_VERSION` до `0.13.271`.

---

# Результати впровадження: 3 варіанти поведінки наявних ігор + Контекстне меню (+) + Автовибір по A + Виключення обсягу реінсталю (v0.13.269)

## Фідбек користувача

1. **Виключення обсягу ігор при реінсталі**: Якщо гра вже встановлена на приставці, при її реінсталі (перевстановленні) розмір гри не має додаватися до загального Required-хедера та проекції смуг вільного місця SD/NAND, оскільки нове місце при цьому не витрачається.
2. **Контекстне меню в черзі по Plus (+)**: Натискання Plus (+) / Start у `ReviewQueue` повинно відкривати бічну панель (Sidebar) з налаштуваннями встановлення (поведінка для вже наявних ігор, ціль встановлення, резерв вільного місця).
3. **Автоматичний вибір по кнопці A**: Якщо в черзі жоден файл не вибрано галочкою, натискання `A` має автоматично позначати файл под курсором та розпочинати його встановлення.
4. **Запитання про перевстановлення гри**: якщо гра вже встановлена, програма має запитувати користувача чи перевстановити її, або пропускати автоматично.
5. **Усунення зависання при скасуванні**: при натисканні "B" у черзі програма має негайно повертати у файловий менеджер без зависань.

## Зроблені зміни

### [Component: Settings & UI]
- **[app.hpp](file:///d:/git/dev/sphaira/sphaira/include/app.hpp)**:
  - Змінено тип `m_skip_if_already_installed` з `OptionBool` на `OptionLong` з дефолтним значенням `1` (Skip).
  - Додано опцію `m_save_settings_globally` ("Save options globally") з гетерами/сетерами.
- **[settings_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/settings_menu.cpp)**: Переписано налаштування "Skip if already installed" на PopupList (`Reinstall`, `Skip`, `Prompt`) та додано вимикач "Save options globally".
- **[app_display_options.cpp](file:///d:/git/dev/sphaira/sphaira/source/app_display_options.cpp)**: Оновлено бічну панель налаштувань "Install Options".

### [Component: Installer Core]
- **[install_progress.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/install_progress.hpp)**: Додано метод `PromptReinstall(title_name)`.
- **[yati.hpp](file:///d:/git/dev/sphaira/sphaira/include/yati/yati.hpp)**: Оновлено `yati::Config` та `yati::ConfigOverride` (додано `skip_if_already_installed`).
- **[yati.cpp](file:///d:/git/dev/sphaira/sphaira/source/yati/yati.cpp)**: Підтримано виклик `PromptReinstall` при `skip_if_already_installed == 2`.

### [Component: UI Install Queue]
- **[dbi_menu.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/menus/dbi_menu.hpp)**:
  - Додано метод `DisplayQueueOptions()`.
  - Додано змінні session override (`m_session_skip_if_already_installed`, `m_session_install_location`, `m_session_reserve_mb`).
- **[dbi_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/dbi_menu.cpp)**:
  - Додано підключення `#include "title_info.hpp"`.
  - Реалізовано допоміжні функції `GetQueueEntryTitleId()` та `IsTitleAlreadyInstalled()`.
  - У `Draw()` (під час відображення `ReviewQueue` та `Installing`) та `StartInstall()` автоматично перевіряється, чи встановлено гру на приставці (у NAND або SD). Якщо гра вже встановлена, її розмір виключається з обчислення загального обсягу `Required`, а також проекції смуг вільного місця SD та NAND.
  - В `UpdateActions()` додано прив'язку `Button::START` (Plus) → `DisplayQueueOptions()`.
  - В `DisplayQueueOptions()` реалізовано Sidebar із налаштуваннями ("Skip if already installed", "Install location", "Reserve free space"), які змінюють глобальні або session-локальні опції залежно від `Save options globally`.
  - В `StartInstall()`: якщо жодного прапорця в черзі не встановлено, автоматично позначається файл під курсором `m_index` і запускається його встановлення.
  - В `CancelSession()`: при натисканні "B" викликається `SetPop()`, повертаючи у файловий менеджер.

### [Component: Net Transfers]
- **[devoptab_curl_thread.cpp](file:///d:/git/dev/sphaira/sphaira/source/utils/devoptab_curl_thread.cpp)**: Зменшено таймаут `DestroyTransfer` з 5 с до 200 мс для усунення зависань при закритті сокетів.

---

# Результати впровадження: Стилізований журнал черги + жирні лейбли (v0.13.266)

## Фідбек користувача

Журнал: події жирним (початок/кінець), помилки червоним, успіхи зеленим; коли гру
пропущено бо вже встановлена — писати це зеленим і дописати, що поведінку можна
змінити в налаштуваннях. Лейбли статус-рядка ReviewQueue — жирні, значення
звичайні; «системну пам'ять» перейменувати на NAND. (Контекстне меню по Start і
scope налаштувань — спроєктовано на наступний блок, тут ще не реалізовано.)

## Зроблені зміни

### [Component: Installer Core]
- **[install_progress.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/install_progress.hpp)**:
  новий віртуальний хук `OnInstallSkipped()` (default no-op).
- **[yati.cpp](file:///d:/git/dev/sphaira/sphaira/source/yati/yati.cpp)**: у точці
  пропуску вже встановленого тайтла (`InstallInternal`/`InstallInternalStream`)
  викликається `pbox->OnInstallSkipped()`.

### [Component: UI Install Queue]
- **[dbi_menu.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/menus/dbi_menu.hpp)** /
  **[dbi_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/dbi_menu.cpp)**:
  - `m_log` став `std::vector<LogEntry>` з `LogKind` (Normal/Event/Success/
    Warning/Error); `AddLog(text, kind)`.
  - Рендер журналу фарбує за типом (зелений/червоний/бурштиновий) і робить
    faux-bold для подій (over-draw з суб-піксельним зсувом — bold-шрифту нема).
  - `OnInstallSkipped()` ставить `m_current_file_skipped`; обидва install-цикли
    (USB `ThreadFunction`, файл `LocalThreadFunction`) логують «Skipped … already
    installed» (зелене) + рядок-підказку про налаштування, або «Installed».
  - Лейбли ReviewQueue малюються хелпером `draw_kv_row` (жирний лейбл + звичайне
    значення); «System memory free» → «NAND free».
- **[uk.json](file:///d:/git/dev/sphaira/assets/romfs/i18n/uk.json)**: ключі
  «Skipped: », «already installed», «NAND free», рядок-підказка.

### [Component: CMake Configuration]
- **[CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt)**: `sphaira_VERSION` → `0.13.266`.

## Перевірка

Збірка WSL `cmake --preset ReleaseWithInstall`: exit 0, `[100%] Built target
sphaira_nro`; uk.json — валідний JSON; dbi_menu/yati без нових warnings.

## Наступний блок (спроєктовано)

Контекстне меню по Start (`Sidebar`) з локальними override для цього
встановлення + перемикач у Tools ▸ Settings «глобально / локально для сесії».
Потребує розширення `yati::ConfigOverride` і `Yati::Setup` — окремим раундом,
обійти з install-фіксом.
