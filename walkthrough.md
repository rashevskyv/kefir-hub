# Поточний walkthrough

Актуальний delivery — **v0.13.507** (2026-08-21). Попередні
walkthrough збережено в
[`archive/walkthrough_v0.13.357-v0.13.430.md`](archive/walkthrough_v0.13.357-v0.13.430.md)
та [`archive/walkthrough_archive.md`](archive/walkthrough_archive.md).

## v0.13.507 — Shared Homebrew Mutation Policy & Complete Web Success Coverage (UPA-06)

- **Спільна політика виявлення змін у каталозі Homebrew**:
  - У `sphaira/include/path_util.hpp` реалізовано:
    - `path::IsSubpathOf(path, parent)` — перевірка входження підшляху з точним контролем меж компонентів шляху (виключає пастки типу `/switch2` vs `/switch`).
    - `path::IsNroPath(path)` — перевірка розширення `.nro` без урахування регістру символів.
    - `path::PathAffectsHomebrew(path, custom_roots, is_directory)` — централізований аналіз впливу модифікації шляху на каталог додатків.
- **Розширені функції сповіщення в меню Homebrew**:
  - У `sphaira/include/ui/menus/homebrew.hpp` та `sphaira/source/ui/menus/homebrew.cpp` реалізовано `NotifyFileCreated`, `NotifyFileDeleted`, `NotifyDirectoryCreated`, `NotifyDirectoryDeleted`, `NotifyRename`, `NotifyPathChanged`.
  - Сповіщення надсилається лише тоді, коли операція дійсно зачіпає дефолтну або сконфігуровану директорію додатків.
- **Покриття Web сервера**:
  - У `sphaira/source/web.cpp` (`HandleUpload`) замінено прямий `SignalChange()` на перевірку через `NotifyFileCreated(out_path.s)`.
  - У `HandleDelete` додано виклики `NotifyDirectoryDeleted(path)` та `NotifyFileDeleted(path)` після успішного видалення об'єктів.
- **Тести та збірка**:
  - Піднято версію до **`0.13.507`** у `sphaira/CMakeLists.txt`.
  - Оновлено статуси в [**`upstream_audit.md`**](upstream_audit.md) та [**`upstream_implementation_plan.md`**](upstream_implementation_plan.md).
  - Додано 53 нові перевірки в `tests/test_path_util.cpp` (283 checks passed).
  - Пройдено всі 15 наборів host unit-тестів та shape-check у WSL (`tests/run.sh`).
  - Успішно зібрано бінарник `sphaira_nro` у WSL.

## v0.13.506 — Playtime Worker UI-Thread Isolation & Race Elimination (UPA-05)

- **Ліквідація стану гонитви (Data Race) у фоновому потоці статистики часу гри**:
  - У `sphaira/source/ui/menus/game_menu.cpp` функція `Menu::LoadPlaytime()` перед запуском фонового потоку `ProgressBox` створює ізольований знімок `std::vector<u64> app_ids` та виділяє спільний буфер `results` (`std::make_shared<std::vector<PlaytimeResult>>`).
  - Фоновий worker тепер звертається виключно до цього буфера, усунувши небезпечні одночасні читання та мутації `m_entries` паралельно з UI-рендерингом.
- **Детерміноване оновлення інтерфейсу на UI-потоці**:
  - Результати з буфера воркера переносяться в `m_entries` строго в деструкторі `ProgressBox` на UI thread і лише за умови успішного виконання (`R_SUCCEEDED(rc)`).
  - Сортування та оновлення інтерфейсу `m_sort.Set(SortType_PlayTime); SortAndFindLastFile(false)` викликаються один раз після завершення обробки всіх ігор, а не під час кожної ітерації.
- **Тести та збірка**:
  - Піднято версію до **`0.13.506`** у `sphaira/CMakeLists.txt`.
  - Оновлено статуси в [**`upstream_audit.md`**](upstream_audit.md) та [**`upstream_implementation_plan.md`**](upstream_implementation_plan.md).
  - Пройдено всі 15 наборів host unit-тестів та перевірку форми патча libhaze у WSL (`tests/run.sh`).
  - Успішно зібрано бінарник `sphaira_nro` у WSL.

## v0.13.505 — MTP Zero-Byte Upload Support & Patch Shape Verification (UPA-04A)

- **Підтримка завантаження файлів розміром 0 байт через MTP**:
  - У `sphaira/cmake/patch_libhaze.cmake` (секція `SendObject`) умову обчислення розміру файлу `data_header.length > sizeof(PtpUsbBulkContainer)` виправлено на `>= sizeof(PtpUsbBulkContainer)`.
  - Це дозволяє коректно приймати нульовий payload контейнера PTP/MTP та викликати `SetFileSize(0)`, запобігаючи помилковому залишенню фіктивного розміру `4_GB` для порожніх файлів.
- **Ідемпотентність та Shape Check патчу**:
  - Забезпечено повну сумісність скрипта патчування: підтримується вихідний стан, попередньо пропатчений стан та стан з проміжними правками.
  - Додано автономний перевірочний скрипт [**`tests/test_patch_libhaze.sh`**](tests/test_patch_libhaze.sh), що перевіряє застосування патчу, ідемпотентність повторного виклику та детекцію спотворення коду.
- **Тести та збірка**:
  - Піднято версію до **`0.13.505`** у `sphaira/CMakeLists.txt`.
  - Підключено shape-check у [**`tests/run.sh`**](tests/run.sh) (всі 15 suite + shape check зелені).
  - Оновлено статуси в [**`upstream_audit.md`**](upstream_audit.md) та [**`upstream_implementation_plan.md`**](upstream_implementation_plan.md) (статус задачі: `SW-DONE / HW-PENDING`).
  - Успішно зібрано бінарник `sphaira_nro` у WSL.

## v0.13.504 — Centralized GitHub and Direct URL Validation (UPA-03)

- **Централізований парсер та валідатор URL GitHub**:
  - У `sphaira/include/path_util.hpp` додано функцію `path::ParseGitHubRepoUrl(url)`.
  - Вона підтримує схеми `http://` та `https://`, хости `github.com` та `www.github.com`, автоматично відсікає суфікс `.git` та завершальні слеші.
  - Строго перевіряє сегменти `owner` та `repo` на валідність (тільки ASCII букви, цифри, `-`, `_`, `.`), відхиляє credentials (`@`), небезпечні порти (`:port`), фрагменти (`#`), параметри запиту (`?`) та directory traversal (`..`).
- **Валідація прямих та ZIP посилань**:
  - Додано `path::IsValidDirectAssetUrl(url)` та `path::IsValidDirectZipUrl(url)`.
  - Усі некоректні посилання відхиляються до початку мережевих запитів та створення записів.
- **Оновлення меню завантажувача GitHub**:
  - У `sphaira/source/ui/menus/ghdl.cpp` замінено наївне обрізання `entry.url.substr(19)` на `path::ParseGitHubRepoUrl`.
  - Функції `LoadEntriesFromPath`, `Download` та `OpenDirectLinkPrompt` переведено на централізовану валідацію.
- **Тести та збірка**:
  - Піднято версію до **`0.13.504`** у `sphaira/CMakeLists.txt`.
  - Оновлено статуси в [**`upstream_audit.md`**](upstream_audit.md) та [**`upstream_implementation_plan.md`**](upstream_implementation_plan.md).
  - Покрито новими unit-тестами у `tests/test_path_util.cpp` (230 checks passed).
  - Успішно зібрано бінарник `sphaira_nro` у WSL.

## v0.13.503 — GHDL ZIP Type Detection & Safe Non-ZIP Destination (UPA-02B)

- **Комплексне визначення ZIP-архівів**:
  - У `sphaira/include/path_util.hpp` додано функцію `path::IsZipAsset(content_type, filename, url)`.
  - Вона надійно ідентифікує ZIP за вмістом `content_type` (`"application/zip"`, `"application/x-zip-compressed"` тощо), за розширенням назви файлу (`.zip`/`.ZIP`), або за шляхом у URL навіть за наявності параметрів запиту `?token=...` чи фрагментів `#...`.
- **Безпечна директорія встановлення не-ZIP ассетів**:
  - Раніше для не-ZIP файлів без вказаного `entry.path` використовувався небезпечний шлях `"/"`, що могло призвести до спроби видалення кореня SD-карти або запису файлу в корінь.
  - Тепер за замовчуванням файли (наприклад, `.nro`) встановлюються у `/switch/<sanitized-name>`.
  - Додано валідацію `path::IsSafeFilename`, що відсікає спроби directory traversal (`..`), символи шляху (`/`, `\`, `:`) та керуючі символи.
- **Підтримка нормалізації та директорій**:
  - Якщо `entry.path` є директорією (закінчується на `/`), до неї безпечно додається назва ассету.
  - Усі шляхи нормалізуються через `path::NormalizeAbsoluteSdPath`.
- **Тести та збірка**:
  - Піднято версію до **`0.13.503`** у `sphaira/CMakeLists.txt`.
  - Додано нові unit-тести у `tests/test_path_util.cpp` (перевірено 193 інваріанти).
  - Успішно зібрано бінарник `sphaira_nro` у WSL.

## v0.13.502 — GitHub Downloader Operation Identity, Cancel & Temp Isolation (UPA-02A)

- **Ізоляція та гарантоване очищення тимчасових файлів**:
  - У `sphaira/source/ui/menus/ghdl.cpp` перед початком завантаження файлів (`DownloadApp()`, `DoDirectLinkDownload()`) додано виклик `fs.DeleteFile(temp_file)` та реєстрацію `ON_SCOPE_EXIT(fs.DeleteFile(temp_file))`.
  - Унеможливлено використання застарілих або пошкоджених файлів від перерваних операцій завантаження.
- **Фазові перевірки скасування (Phase Cancellation Gates)**:
  - Додано перевірки стану скасування `pbox->ShouldExit()` перед початком передачі даних через libcurl, безпосередньо після її завершення перед операціями з файловою системою, а також перед викликом розпакування `thread::TransferUnzipAll` та фінального перейменування.
  - У разі скасування повертається детермінований код `Result_TransferCancelled`, що унеможливлює виконання небезпечних мутацій файлової системи після відмови користувача.
- **Захист від помилкових сигналів оновлення Homebrew**:
  - Виклик `homebrew::SignalChange()` перенесено суворо всередину блоку успішного завершення операції `if (R_SUCCEEDED(rc))`.
  - Для коду `Result_TransferCancelled` прибрано показ непотрібних спливаючих вікон про збій мережі `App::PushErrorBox`.
- **Верифікація та тести**:
  - Піднято версію до **`0.13.502`** у `sphaira/CMakeLists.txt`.
  - Оновлено статуси у звітах [**`upstream_audit.md`**](upstream_audit.md) та [**`upstream_implementation_plan.md`**](upstream_implementation_plan.md).
  - Пройдено всі 15 наборів host unit-тестів у WSL (`tests/run.sh`).
  - Успішно скомпільовано цільовий бінарник `sphaira_nro` у WSL.

## v0.13.501 — GitHub Downloader Callback Ownership & Selection Safety (UPA-01)

- **Ліквідація Global Static стану `gh_entries`**:
  - У `sphaira/source/ui/menus/ghdl.cpp` прибрано змінну `static std::vector<GhApiEntry> gh_entries` усередині `DownloadEntries()`, яка призводила до того, що паралельні або послідовні запити завантаження з Github перезаписували спільний масив релізів під час роботи асинхронних попапів.
  - Контейнер переведено на операційно-локальний `std::shared_ptr<std::vector<GhApiEntry>>`, що гарантує ізольоване володіння даними кожної окремої операції завантаження.
- **Усунення Use-After-Free (UAF) та висячих посилань у відкладених Callbacks**:
  - Раніше лямбда `func` захоплювала локальне посилання `&asset_entry` та сирий вказівник `const AssetEntry* ptr` на елементи вектора `entry.assets`, який знищувався після виходу з попереднього виклику `PopupList`. При відкладеному виклику це спричиняло читання невалідної пам'яті.
  - Замінено сирі вказівники на `std::vector<std::optional<AssetEntry>> matched_assets` та значення `asset_entry`, захоплені за значенням (`[entry, asset_entry, matched]`).
- **Захист від виходу за межі діапазону (Out-of-Bounds Guards)**:
  - Додано строгі перевірки валідності індексу `op_index` для вибору релізів (`!op_index || *op_index < 0 || static_cast<size_t>(*op_index) >= gh_entries->size()`) та ассетів (`static_cast<size_t>(*op_index) >= api_assets.size()`).
  - Додано перевірку на порожній список валідних ассетів із показом інформаційного вікна `OptionBox("No downloadable assets found.")` замість спроби відкриття порожнього меню.
- **Верифікація та тести**:
  - Піднято версію до **`0.13.501`** у `sphaira/CMakeLists.txt`.
  - Успішно виконано всі 15 наборів host unit-тестів у WSL (`tests/run.sh`).
  - Успішно скомпільовано цільовий бінарник `sphaira_nro` у WSL.

## v0.13.500 — OverrideHeap Inaccessible Pages Defense & Logger Lifecycle Hardening

- **Діагностика збоїв запуску (Build ID та трейсбек)**:
  - Ідентифіковано причину падіння аллокатора при старті NRO (`PC 0x2c1258: str x1, [x3, #8]` у `_malloc_r`/`_memalign_r`), що зафіксовано у звітах Atmosphère (зокрема `01787234167_03db12780bd84000.log`, Build ID `29AC83A4D4BFAD5E9014FC1C660A598627CDA520`).
  - Збій відбувався при спробі запису заголовка нового top-чанка у пам'ять за адресою `Exception Address = 0x0000000CBAF673F8` під час сканування тем або декодування зображень (виклики-жертви).
- **Причина проблеми (успадковані дірки в OverrideHeap)**:
  - Попередній NRO-процес або потік, створений через `threadCreate(..., stack_mem=nullptr)` без завершального виклику `threadClose()`, залишає вихідні сторінки стеку у стані `Perm_None` з атрибутом `MemAttr_IsBorrowed` (через `svcMapMemory`).
  - Homebrew loader перевикористовує той самий `OverrideHeap` для наступного NRO, у результаті чого стандартна ініціалізація newlib вважала арену неперервною та намагалася виконати запис у недоступну сторінку.
- **Захист через `__libnx_initheap` (пошук найбільшого чистого діапазону)**:
  - Реалізовано strong override `extern "C" void __libnx_initheap(void)` у `sphaira/source/main.cpp`.
  - При активному `envHasHeapOverride()` здійснюється сканування інтервалу пам'яті за допомогою `svcQueryMemory`, обрізка блоків за межами оверрайду, перевірка типу `MemType_Heap`, прав `Perm_Rw` та атрибутів `attr == 0`.
  - Вибирається найбільший неперервний валідний діапазон сторінок, а `fake_heap_start`/`fake_heap_end` встановлюються лише після успішного повного сканування без виділення пам'яті.
  - У разі помилки або відсутності доступної пам'яті виконання детерміновано зупиняється через `diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed))`.
- **Посилення надійності та життєвого циклу логера**:
  - У `sphaira/source/log.cpp` сентінел сокета `nxlink_socket` ініціалізовано значенням `-1`, а перевірки валідності стандартизовано як `>= 0` (дескриптор 0 є валідним).
  - У `log_write_arg_internal` виправлено копіювання рядків: довжина копіювання суворо обмежена фактичним розміром NUL-термінованого буфера `std::strlen(buf)` без виходу за межі `buf[512]`.
  - Життєвий цикл вихідного логера перенесено: `log_nxlink_init()` викликається в `App::App` після `g_app = this` (коли сокети та глобальні C++ об'єкти вже ініціалізовані), а завершення у `App::~App` спочатку відправляє фінальне повідомлення, закриває `log_nxlink_exit()`, а потім зупиняє та закриває потік логування через `log_file_exit()`.
- **Ітерація версії та верифікація**:
  - Піднято версію програми до **`0.13.500`** у `sphaira/CMakeLists.txt`.
  - Успішно виконано збірку у WSL (`ReleaseWithInstall`), згенеровано новий `kefir-hub.nro` та `sphaira.elf`, пройдено всі host-тести.

## v0.13.499 — Tools Menu Layout Reorganization, Software Description Update & 4th Row Expansion

- **Реорганізація порядку іконок меню Tools**:
  - У `sphaira/source/ui/menus/tools_menu.cpp` оновлено структуру меню `Tools` відповідно до нової логічної послідовності:
    - **Рядок 1**: `File Browser` (Файловий браузер), `Games` (Ігри), `Themes` (Теми).
    - **Рядок 2**: `Updater` (Оновлення), `Saves` (Збереження), `Software` (Додаткові програми).
    - **Рядок 3**: `Cheats` (Чити), `Kefir Settings` (Налаштування кефіра), `Settings` (Налаштування).
    - **Рядок 4 (експериментальний)**: `Tools` (Інструменти / Менеджер модулів).
- **Оновлення опису пункту Software («Додаткові програми»)**:
  - Опис замінено на `"Homebrew App Store, DBI and mod utilities."` з урахуванням прямого доступу до каталогу Homebrew App Store.
- **Експериментальне додавання 4-го ряду (пункт Tools)**:
  - Додано 10-й елемент `Tools` з іконкою `advanced-options.png` та описом `"System tools and sysmodule manager."`. Пункт відкриває системний менеджер модулів (`ui::menu::hats::UninstallerMenu`).
  - При переміщенні курсора вниз на 4-й ряд сітка автоматично та плавно прокручується вниз через `List::ScrollDown` з коректним обмеженням через `ScissorContent`.
- **Повна синхронізація локалізацій**:
  - Оновлено словники перекладів для нових ключів у всіх 14 мовних файлах `assets/romfs/i18n/*.json`.
- **Ітерація версії та документація**:
  - Піднято версію програми до **`0.13.499`** у `sphaira/CMakeLists.txt`.
  - Оновлено `README.md`, `plan.md`, `task.md`.

## v0.13.498 — Header Subtitle Top-Row Alignment & Section Title Sizing Fix

- **Перенесення описів елементів на верхній рядок хедера**:
  - У меню `Tools` (`tools_menu.cpp`), `SaveHub` (`save_hub_menu.cpp`), `Settings` (`settings_menu.cpp`), `Uninstaller` (`uninstaller_menu.cpp`), `FTP` (`ftp_menu.cpp`) та `AppStore` (`appstore.cpp`) опис обраного пункту переведено на виклик `SetTitleSubHeading(description, true); SetSubHeading("");`.
  - Довгий опис тепер відображається у верхньому рядку хедера (поруч із версією `v0.13.498`), плавно прокручуючись за потреби та не конфліктуючи із заголовком розділу.
- **Збереження повного кегля заголовка розділу**:
  - Завдяки очищенню нижнього рядка від довгих текстів заголовок розділу («Інструменти» / «Tools», «Налаштування» / «Settings» тощо) більше не стискається до 40% і не прокручується даремно, а відображається чітким повним шрифтом 28px.
- **Ітерація версії та збірка**:
  - Піднято версію програми до **`0.13.498`** у `sphaira/CMakeLists.txt`.
  - Успішно скомпільовано цільовий двійковий файл `kefir-hub.nro` у WSL та пройдено всі unit-тести.

## v0.13.497 — Clean Switch compilation, translation pipeline sync & NX-Link deployment

- **Виправлення сумісності компіляції під Nintendo Switch**:
  - У `sphaira/source/auto_update.cpp` оновлено відкриття файлу на коректну сигнатуру `fs.OpenFile(staging_path, FsOpenMode_Read, &file)` та визначення розміру через `file.GetSize(&file_size)`.
  - У `sphaira/source/ui/about_box.cpp` виправлено обробку жесту тач-скролінгу відповідно до полів структури `TouchInfo` (`touch->is_touching` та `touch->cur.y`).
  - У `sphaira/source/ui/menus/install_stream_menu_base.cpp` виправлено специфікатор формату `log_write` з `%lld` на `%ld` для типу `s64`.
  - У `sphaira/source/ftpsrv_helper.cpp` очищено невикористовувані змінні `user_len` та `pass_len`.
- **Повна синхронізація перекладів (Gemini 3.6 Flash)**:
  - Утилітою `translate.py` виконано 100% переклад усіх відсутніх ключів у всіх 14 мовах без збоїв.
- **Збірка NRO та деплой через NX-Link**:
  - Цільовий бінарник `kefir-hub.nro` успішно скомпільовано у середовищі WSL devkitA64 (`ReleaseWithInstall`).
  - Виконано команду `make nxlink` — оновлений двійковий файл (`6.66 MB`) успішно відправлено на Switch (`192.168.50.69`).
- **Ітерація версії**:
  - Піднято версію програми до **`0.13.497`** у `sphaira/CMakeLists.txt`.

## v0.13.496 — Automatic Silent Update, Background Self-Updating & About Changelog Box

- **Оновлення URL репозиторію на `rashevskyv/kefir-hub`**:
  - Оновлено віддалене посилання git origin на `https://github.com/rashevskyv/kefir-hub.git`.
  - Оновлено URL API релізів у `sphaira/source/ui/menus/main_menu.cpp` та `assets/romfs/github/kefir-hub.json`.
- **Автоматичне тихе фонове оновлення бінарника**:
  - Створено модуль `sphaira/include/auto_update.hpp` та `sphaira/source/auto_update.cpp`.
  - У `MainMenu::MainMenu()` при виявленні новішого релізу на GitHub у фоновому режимі аналізуються всі додані ассети релізу (`kefir-hub.nro`, `sphaira.nro`, `.zip`) та вибирається найбільш відповідний двійковий файл для поточного середовища запуску.
  - Завантаження виконується асинхронно через `curl::Api().ToFileAsync` у кеш `/switch/sphaira/cache/sphaira_update.temp` без блокування інтерфейсу чи підгальмовувань.
  - Після завершення завантаження перевіряється цілісність та розмір файлу (> 1 KiB), після чого виконується безпечна заміна виконуваного файлу за шляхом `App::GetExePath()` (`ResolveInstallDestination`), а також оновлюється `/hbmenu.nro` у разі увімкненої опції заміни hbmenu.
  - Оновлення відбувається на 100% тихо та прозоро для користувача — без спливаючих вікон із запитами завантаження чи повідомлень про необхідність перезапуску; оновлена версія програми запускатиметься автоматично при наступному запуску.
- **Модальне вікно About та перегляд списку змін**:
  - Створено клас `AboutBox` (`sphaira/include/ui/about_box.hpp`, `sphaira/source/ui/about_box.cpp`).
  - Відображає поточну версію програми, посилання на репозиторій та кешований/актуальний список змін (changelog) з релізів GitHub.
  - Підтримує Markdown-форматування, вертикальне прокручування (стіки, D-Pad, L/R тригери для переходу між сторінками, тач-скролінг) та ручне оновлення за кнопкою `X` (Refresh).
  - Додано пункт `About` у меню `Settings → General`.
- **Налаштування та локалізація**:
  - Додано опцію `m_auto_update` (`App::GetAutoUpdateEnable()`, `App::SetAutoUpdateEnable(bool)`).
  - У розділ `Settings → General` додано перемикач `Auto-update` із детальним описом.
  - Додано повні переклади для всіх нових рядків у всі 14 мовних файлів `assets/romfs/i18n/*.json`.
- **Unit-тести та верифікація**:
  - Створено `tests/test_auto_update_asset.cpp` з 8 перевірками точності селектора ассетів.
  - Пройдено всі 15 наборів host unit-тестів паралельно та перевірку dead symbol guard (`tests/run.sh`).
  - Піднято версію програми до `0.13.496` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `plan.md`, `task.md`.

## v0.13.495 — Pixel-balanced split & full-width justified 2-row footer layout

- **Попіксельне балансування рядків дворядкового футера**:
  - У `sphaira/source/ui/widget.cpp` (`Widget::SetupUiButtons`) алгоритм вибору точки розбиття кнопок $k$ переведено на мінімізацію різниці сумарної ширини контенту в пікселях ($|W_{\text{bottom}} - W_{\text{top}}| \to \min$). Це вирівнює візуальне наповнення обох смуг незалежно від того, скільки літер займає кожна конкретна кнопка чи її переклад.
- **Рівномірний розподіл кнопок на всю ширину футера (Justified Spacing)**:
  - Реалізовано функцію `LayoutUiButtonsRowJustified`: простір, що залишається після розміщення кнопок у рядку, рівномірно ділиться між проміжками ($gap = (W_{\text{avail}} - W_{\text{content}}) / (M - 1)$).
  - Крайній правий елемент рядка вирівнюється по правому полю `1220px`, а крайній лівий елемент — точно по лівому полю `30px`, гармонійно заповнюючи весь простір підвалу екрана.
  - Сенсорні зони елементів безшовно розширено на половину проміжку в обидва боки, забезпечуючи 100% покриття тач-екрана у підвалі без сліпих зон.
- **Тестування та документація**:
  - Unit-тест `tests/test_title_scaling.cpp` оновлено для валідації попіксельного балансу та точного розрахунку інтервалів.
  - Піднято версію до `0.13.495` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `plan.md`, `task.md`.
  - Успішно скомпільовано бінарник `sphaira_nro` у WSL та пройдено всі хостові тести.

## v0.13.494 — Unified Prev/Next Image button hint in image viewer footer

- **Об'єднання підказок дій гортання зображень**:
  - У `sphaira/source/ui/menus/file_viewer.cpp` підказки переходу до попереднього та наступного зображень об'єднано в один елемент: `Action{"Prev / Next Image"_i18n, "\uE0ED / \uE0EE", ...}` на `Button::LEFT`.
  - Для `Button::RIGHT` зареєстровано дію з порожнім `m_hint`, що зберігає реакцію на натискання стрілки праворуч на геймпаді / стіку, але не створює зайвого напису у футері.
  - Це зменшує загальну ширину підказок у вівері зображень і дозволяє комфортно вмістити всі дії.
- **Версія та збірка**:
  - Піднято версію до `0.13.494` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `plan.md`, `task.md`.
  - Успішно скомпільовано цільовий бінарник `sphaira_nro` у WSL та виконано повний набір host-тестів.

## v0.13.493 — Image viewer uncluttered header, dynamic title scaling/scrolling & 2-row footer layout

- **Приховування смуг пам'яті NAND/SD у вівері зображень**:
  - У `MenuBase` (`sphaira/include/ui/menus/menu_base.hpp`) додано методи керування видимістю смуг пам'яті `SetShowStorage(bool)` та `ShowStorage()`.
  - У `Menu::LoadImageFile()` та `Menu::LoadCurrentFile()` (`sphaira/source/ui/menus/file_viewer.cpp`) для зображень смуги NAND та SD автоматично вимикаються (`SetShowStorage(false)`), а для текстових файлів вмикаються назад (`SetShowStorage(true)`).
  - При вимкненні смуг пам'яті ліва межа статус-блоку `m_status_left_x` автоматично зміщується вправо до блоку годинника/батареї (`start_x`), надаючи назві файлу зображення весь простір від лівого поля `x = 80` аж до годинника (~950+ пікселів).
- **Динамічне масштабування та плавна прокрутка назв файлів/заголовків**:
  - У `MenuBase::DrawChrome` (`sphaira/source/ui/menus/menu_base.cpp`) реалізовано алгоритм адаптивного підбору кегля: якщо назва перевищує доступну ширину, кегль зменшується пропорційно (базовий розмір 28px, зменшення до 40% / мінімум 16.8px).
  - Якщо навіть при мінімальному кеглі назва файлу перевищує доступний простір, автоматично активується плавний скролінг `m_scroll_title` (`ScrollingText`).
  - При зміні заголовка `SetTitle()` стан прокрутки коректно скидається.
- **Автоматичний перенос кнопок футера на 2 збалансовані рядки**:
  - У `Widget::SetupUiButtons` (`sphaira/source/ui/widget.cpp`) реалізовано автоматичний перехід на 2 рядки, якщо для 1 рядка масштаб падає нижче 0.85 (або кнопки не поміщаються в екран).
  - Алгоритм перебирає можливі точки розбиття $k$ та обирає ту, яка максимізує фінальний масштаб та мінімізує різницю довжин рядків.
  - Нижній рядок (y = 687) містить основні кнопки підтвердження/вибору (A, B, X, Y), верхній рядок (y = 655) — тригери та вторинні дії (ZL/ZR, +, ◀▶).
  - Для обох рядків збережено високі розміри шрифтів (17px кегль, 22px гліфи) та призначено незалежні вертикальні сенсорні зони ([646, 682] та [682, 720]), забезпечуючи точну та зручну роботу тач-управління без обрізання кнопок зліва екрана.
- **Тестування та документація**:
  - Створено `tests/test_title_scaling.cpp` з 17 перевірками динамічного масштабування та розбиття футера на рядки.
  - Піднято версію до `0.13.493` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `plan.md`, `task.md`.
  - Успішно скомпільовано ціль `sphaira_nro` у WSL (`[100%] Built target sphaira_nro`), усі host unit-тести пройдено успішно.

## v0.13.492 — Homebrew App Store restored to Tools > Software

- **Повернення Homebrew App Store у розділ програм**:
  - У `sphaira/source/ui/menus/settings_menu.cpp` пункт `Homebrew App Store` повернуто на першу позицію в меню `Tools → Software` (`BuildSoftwareItems()`).
  - З розділу `Settings → Homebrew` (`BuildCategories()`) видалено ярлик запуску `Homebrew App Store`, завдяки чому в налаштуваннях залишилися виключно параметри конфігурації (`Homebrew Search Paths`, `Forwarders`, `Replace hbmenu on exit`).
- **Версія та валідація**:
  - Піднято версію до `0.13.492` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `plan.md`, `task.md`.
  - Успішно зібрано цільовий бінарник `sphaira_nro` у WSL (`[100%] Built target sphaira_nro`), пройдено повний набір host-тестів (`tests/run.sh`).

## v0.13.491 — Fix flush thread stack overflow

- **Усунення переповнення стеку у фоновому потоці логування**:
  - У `sphaira/source/log.cpp` усунено алокацію локального буфера `char batch[64KB]` на стеку потоку `flush_thread_func`, перенісши його у статичну пам'ять `g_flush_batch`.
  - Збільшено розмір стеку потоку `g_flush_thread` з `0x4000` (16 КБ) до `0x8000` (32 КБ), що усунуло Stack Overflow та миттєвий Data Abort краш при відкритті програми.
- **Версія та збірка**:
  - Піднято версію до `0.13.491` у `sphaira/CMakeLists.txt`, оновлено документацію в `README.md`, `plan.md`, `task.md`.

## v0.13.490 — Fix cstring include in static logger

- **Виправлення заголовків компіляції**:
  - У `sphaira/source/log.cpp` додано `#include <cstring>` для функції `std::memcpy`, що усунуло помилку компіляції в середовищі devkitA64 GCC 14/16.
- **Версія та збірка**:
  - Піднято версію до `0.13.490` у `sphaira/CMakeLists.txt`, оновлено документацію в `README.md`, `plan.md`, `task.md`.
  - Успішно зібрано ціль `sphaira_nro` у WSL (`[100%] Built target sphaira_nro`).

## v0.13.489 — Zero-heap static logging buffer & image load ordering

- **Статичний буфер логування без алокацій у heap**:
  - У `sphaira/source/log.cpp` усунено використання динамічного `std::string g_buffer` на користь фіксованого статичного буфера `g_buffer_data` (64 КБ). Це гарантує, що жоден виклик `log_write` або скидання `do_flush` у фоновому потоці не викликає `malloc`, `realloc` чи `free`, усуваючи взаємне блокування та пошкодження списку вільних чанків кучі newlib.
- **Оптимізація порядку завантаження ресурсів**:
  - У `sphaira/source/app.cpp` виклик `InitDefaultImage()` виконується до запуску фонових потоків `ntp::Start()` та `forwarder_auto::StartCheck()`.
- **Версія та інтеграція**:
  - Піднято версію до `0.13.489` у `sphaira/CMakeLists.txt`, оновлено документацію в `README.md`, `plan.md`, `task.md`.

## v0.13.488 — Sysmodule slow SD boot timeout & crash prevention

- **Збільшення таймаутів ініціалізації ФС для повільних SD-карт**:
  - У `sysmodule/source/main.c` збільшено цикл спроб підключення `fsInitialize()` та `fsdevMountSdmc()` до 3000 ітерацій (300 секунд = 5 хвилин) згідно з практикою усунення затримок старту на повільних картках (відповідно до коміту SwitchThemeInjector).
  - Прибрано `diagAbortWithResult` при помилці ініціалізації SM у фоновому модулі, усуваючи фатальні паніки Atmosphere при старті.
- **Версія та інтеграція**:
  - Піднято версію до `0.13.488` у `sphaira/CMakeLists.txt`, оновлено документацію в `README.md`, `plan.md`, `task.md`.

## v0.13.487 — SD card FS sync, malloc & NanoVG stability on slow cards / NX-Link handoff

- **Синхронізація файлової системи microSD та запобігання пошкодженню на повільних картах**:
  - Виправлено виклики `fsdevCommitDevice("sdmc")` та `fsdevGetDeviceFileSystem("sdmc")` у `userAppExit()` (`sphaira/source/main.cpp`). У libnx назва пристрою реєструється без двокрапки (`"sdmc"`, а не `"sdmc:"`), тому раніше виклик `fsdevGetDeviceFileSystem("sdmc:")` повертав `nullptr` і скидання дискового кешу при виході з програми не спрацьовувало.
  - Додано обов'язковий виклик `fsdevCommitDevice("sdmc")` перед передачею виконання у `envSetNextLoad` (`sphaira/source/nro.cpp` / `launch_internal`), що забезпечує збереження цілісності ФС перед стартом наступного виконуваного файлу.
  - У `sphaira/source/nxlink.cpp` додано виклики `fsdevCommitDevice("sdmc")` після запису та перейменування тимчасового файлу `~`.
  - У `sphaira/source/log.cpp` додано виклики `fsdevCommitDevice("sdmc")` після кожного скидання буфера логів на диск (`do_flush`, `log_write_error`).
- **Усунення race condition у фоновому потоці логування та захист хіпу**:
  - У `sphaira/source/log.cpp` фоновий потік `g_flush_thread` переведено з запису через `std::fwrite(..., stdout)` / `std::fflush(stdout)` на прямий неблокуючий виклик `send(sock, data.data(), data.size(), 0)`. Конкурентний доступ до спільних буферів `stdout` у newlib одночасно з основним потоком спричиняв пошкодження метаданих чанків кучі та аварійне падіння в `_malloc_r` (`Data Abort 0x4A8`).
  - Замінено `std::localtime` на потокобезпечний `localtime_r` у `log_write_error`.
- **Безпечна обробка рядків та алокацій у темах**:
  - У `sphaira/source/app_theme.cpp` (`LoadElementImage`, `LoadElementColour`) додано явне перетворення `std::string_view` на нуль-терміновані рядки `std::string` перед викликами `nvgCreateImage` та `std::strtoul`, що усуває вихід за межі пам'яті.
- **Версія, збірка, тести та розгортання**:
  - Піднято версію до `0.13.487` у `sphaira/CMakeLists.txt`, оновлено документацію в `README.md`, `plan.md`, `task.md`.
  - Успішно скомпільовано ціль `sphaira_nro` у WSL (`[100%] Built target sphaira_nro`), пройдено всі unit-тести в паралельному режимі (`tests/run.sh`).
  - Новий бінарник розгорнуто безпосередньо на змонтовану картку пам'яті `I:\` (`I:\hbmenu.nro` та `I:\switch\kefir-hub.nro`).

## v0.13.486 — Saves menu L/R shoulder button tab navigation

- **Швидке перемикання вкладок/категорій сейвів кнопками L та R**:
  - У конструкторі `Menu::Menu` (`save_menu.cpp`) додано реєстрацію дій `Button::L` ("Previous tab"_i18n) та `Button::R` ("Next tab"_i18n) для автономного режиму роботи меню збережень (`!m_app_id_filter`).
  - Реалізовано метод `Menu::ChangeCategory(s64 delta)` у `save_menu.hpp` та `save_menu.cpp`: забезпечує циклічну навігацію між категоріями `Category::Installed` («Встановлені ігри»), `Category::Deleted` («Видалені ігри») та `Category::Backups` («Резервні копії»).
  - Реалізовано метод `Menu::SetCategory(Category category)`: змінює активну категорію `m_category`, синхронізує заголовок вікна `SetTitle(...)`, відтворює звуковий ефект зміни фокусу `App::PlaySoundEffect(SoundEffect_Focus)` та виконує оновлення списку елементів через `ScanHomebrew()`.
  - Підказки дій відображаються у правому блоці футера з можливістю активації як фізичними плечовими кнопками геймпада, так і сенсорним дотиком на тачскріні Switch.
- **Версія, документація та збірка**:
  - Піднято версію до `0.13.486` у `sphaira/CMakeLists.txt`, оновлено документацію в `README.md`, `plan.md`, `task.md`.
  - Успішно пройдено повний набір host unit-тестів (`tests/run.sh`), перевірку мертвого коду/оголошень (`check_dead_symbols.py`), компіляцію цільового бінарника `sphaira_nro` у WSL та перевірку форматування `git diff --check`.

## v0.13.485 — Screensaver display sleep prevention & OLED user brightness retention

- **Запобігання вимкненню екрана та авто-сну під час скрінсейвера**:
  - У `App::SetAutoSleepDisabled(bool enable)` (`app.hpp`) реалізовано обов'язкову паралельну активацію `appletSetMediaPlaybackState(true)` разом із `appletSetAutoSleepDisabled(true)`. Раніше `SetMediaPlaybackState` спрацьовував лише як fallback при помилці `appletSetAutoSleepDisabled`, через що система Horizon OS дозволяла приглушення екрана (dimming) та вимкнення підсвітки (screen off) таймером простою.
  - У `Screensaver::Start()` та `Screensaver::Stop()` підключено керування блокуванням сну через `App::SetAutoSleepDisabled`.
  - У `Screensaver::Update(...)` додано виклики `appletReportUserIsActive()`, які під час дрейфу екранної заставки імітують активність користувача на рівні драйвера введення HID, запобігаючи переходу в режим сну.
- **Збереження яскравості користувача на OLED та зниження на LCD**:
  - Реалізовано функцію `App::IsOledModel()` (`app_settings.cpp`), яка опитує апаратний конфіг `SplConfigItem_HardwareType` через `splInitialize` / `splGetConfig` (ідентифікатор `5` — ревізія Aula / Switch OLED) та кешує результат.
  - У `Screensaver::Start()`:
    - Для OLED-моделей при старті скрінсейвера за замовчуванням зберігається поточна встановлена користувачем яскравість консолі `m_saved_brightness` (`m_current_brightness = m_saved_brightness`). Чорний колір фону `#000000` повністю вимикає пікселі OLED-матриці та не споживає енергії, тому годинник та показники процесу залишаються чіткими та яскравими.
    - Для LCD-моделей (Switch V1, V2, Lite) яскравість знижується до значення `App::GetBlankBrightness() / 100.f` для економії заряду та усунення світіння матриці в темряві.
    - У режимі `BlankMode::Dim` яскравість знижується на всіх пристроях, а в `BlankMode::BacklightOff` повністю вимикається підсвітка.
    - Збережено можливість тонкого ручного підстроювання яскравості правим стіком під час роботи скрінсейвера та гарантоване відновлення початкової яскравості користувача у `Stop()`.
- **Попередній перегляд скрінсейвера (`screensaver.cpp`)**:
  - У `SaverPreview::Update` підключено виклик `m_saver.Update(controller, touch)` для коректної обробки стіків, дрейфу та оновлення таймера активності.
- **Версія, документація та тести**:
  - Оновлено `README.md` із поясненням поведінки підсвітки для OLED та LCD, піднято версію до `0.13.485` у `sphaira/CMakeLists.txt`, успішно зібрано релізний бінарник `sphaira_nro` у WSL та пройдено всі unit-тести.

## v0.13.484 — NX-Link SD commit, path normalization, buffer bounds & forwarder auto-install stabilization

- **Аналіз краш-дампів Atmosphere та усунення Data Abort (0x4A8)**:
  - Проаналізовано звіти аварійного завершення `F:\atmosphere\crash_reports\01787136448_03db12780bd84000.log` та відновлено повний стек викликів (`_malloc_r` -> `_memalign_r` -> `stbi_zlib_decode_malloc_guesssize_headerflag` -> `InitDefaultImage` -> `App::App`).
  - Виявлено, що збій виникав через пошкодження заголовків пам'яті кучі (heap corruption) внаслідок одночасного фонового встановлення активного тайтла програми модулем `forwarder_auto_install` та нетермінованого читання `m_app_path`.
- **Безпечна ініціалізація `m_app_path` (`app.cpp`)**:
  - У конструкторі `App::App` буфер `m_app_path` тепер явно обнуляється, а копіювання `argv0` (при запуску через HBL з префіксом `sdmc:/`) виконується із жорстким обмеженням довжини та гарантованим нуль-термінатором `\0`.
  - Усунено читання сміття за межами буфера при викликах `GetExePath().toString()` та `ini_putl(GetExePath(), ...)`.
- **Захист від повторного фонового встановлення активного тайтла (`forwarder_auto_install.cpp`)**:
  - Додано перевірку `App::IsApplication()`: якщо застосунок вже виконується як встановлена гра/форвардер, фоновий пошук та встановлення NSP автоматично припиняються.
  - Реалізовано вилучення Title ID із назви файлу NSP у `/Games/` (`ExtractTitleIdFromName`) та перевірку `nsIsAnyApplicationEntityInstalled`. Якщо знайдений тайтл уже встановлено, фонове встановлення тихо пропускається, запобігаючи небезпечному конкурентному перезапису виконуваного процесу.
  - Додано керування життєвим циклом: функція `StopCheck()` та перевірка `g_stop_requested` у `SilentInstallProgress` забезпечують безпечну зупинку фонового потоку в деструкторі `App::~App()`.
- **Нормалізація шляхів та надійне збереження файлів у `nxlink.cpp`**:
  - Шляхи, передані з ПК через NX-Link, автоматично очищуються від префіксів `sdmc:/` та приводяться до нативного формату FS (`/switch/...` або `/...`), усуваючи збої створення файлів (`0x202` / `0x402`).
  - Додано обов'язкові виклики `fs.Commit()` після запису тимчасового файлу та після його перейменування у цільовий NRO, що запобігає пошкодженню та зльоту файлової системи FAT32/exFAT microSD ("падає картка пам'яті").
  - Забезпечено безпеку прийому аргументів запуску `args_buf` із гарантованим завершальним `\0`, довжино-обмежене копіювання в `WriteCallbackFile`, та move-only семантику для `SocketWrapper`.
- **Потокобезпечне логування (`log.cpp`)**:
  - `std::localtime` замінено на `localtime_r` для усунення перегонів потоків при формуванні міток часу.
- **Паралельне тестування та збірка**:
  - Скрипт `tests/run.sh` оновлено для паралельного виконання всіх наборів тестів хоста.
  - Піднято версію до `0.13.484` у `sphaira/CMakeLists.txt`, успішно зібрано бінарник у WSL (`sphaira_nro`), пройдено всі unit-тести.

## v0.13.483 — Install queue list layout bounds fix & auto-advance on X button

- **Виправлення накладання списку черги встановлення на футер**:
  - У `dbi_menu.cpp` скориговано розміри та координати списку `m_list` для режиму перегляду черги (`State::ReviewQueue`): позицію `queue_pos` встановлено в `{70.f, GetY() + 63.f, 1140.f, 470.f}` (150.f по осі Y), висоту рядка зменшено з 82.f до 78.f. Завдяки цьому 6 рядків списку займають висоту 468.f (закінчуються на 618.f, контур фокусу — на 622.f), залишаючи 24px відступу до розділювача футера (`FOOTER_LINE_Y = 646.f`) та повністю усуваючи накладання на кнопки і підзаголовок футера.
  - Скориговано висоту контейнера `log_pos` з 330.f до 310.f (`m_log_list` — 10 рядків по 30.f = 300.f, `m_error_list` — 5 рядків по 55.f = 275.f), що запобігає перекриттю футера під час відображення логів інсталяції та помилок.
- **Автоматичний перехід до наступного пункту при виборі кнопкою X**:
  - Оновлено обробник дії кнопки `X` ("Select") у стані `State::ReviewQueue`: після зміни прапорця виділення `m_queue[m_index].selected` курсор автоматично переходить до наступного пакунка (`m_index++`) та викликає `m_list->EnsureVisible(m_index, m_queue.size())`.
  - Поведінка вибору тепер повністю уніфікована з меню ігор (`game_menu`), хоумбрю (`homebrew`), файловим менеджером (`filebrowser`) та менеджером сейвів (`save_menu`).
- **Посилення глобального захисту ножиць кадрування (`layout.hpp`)**:
  - У функції `PaddedContentClipY` додано гарантоване обмеження `bottom = std::min(bottom, CONTENT_BOTTOM)` для всіх елементів інтерфейсу, що знаходяться нижче шапки (`y >= HEADER_LINE_Y`).

## v0.13.482 — Fully silent background forwarder installation without restart prompt

- **Повністю тихе встановлення форвардера**: Оновлено модуль `forwarder_auto_install.cpp` — повністю прибрано будь-які спливаючі діалогові вікна (`OptionBox`) та запити на перезапуск після фонового встановлення Homebrew Menu форвардера.
- **Прозора фонова робота**: Якщо форвардер відсутній на консолі, фоновий потік самостійно знаходить `Homebrew menu*.nsp` у папці `/Games/`, встановлює його у фоні через Yati, фіксує успіх у логах і тихо завершує виконання без жодних переривань або взаємодії з користувачем.
- **Очищення заголовків**: Видалено невикористовувані заголовні файли (`evman.hpp`, `i18n.hpp`, `ui/option_box.hpp`) з `forwarder_auto_install.cpp`.

## v0.13.481 — Install queue package skip fix & USB link resynchronization

- **Виправлення обриву черги встановлення при пропуску пакунка**: Вирішено проблему, через яку дія кнопки **B** ("Skip package") під час встановлення (`State::Installing`) переривала всю чергу інсталяції замість пропуску одного поточного пакунка.
- **Ресинхронізація USB-з'єднання після пропуску (`ReestablishUsbLink`)**: При скасуванні активного USB-пакунка (`user_skipped`) хост на ПК залишався в середині передачі попереднього блоку даних. Тепер у `ThreadFunction` після пропуску автоматично виконується перепідключення та скидання сесії (`ReestablishUsbLink`), повертаючи хост до очікування команд та дозволяючи безперешкодно встановлювати наступний пакунок `i + 1`.
- **Обробка відновлюваних помилок сесії в циклі повторних спроб**: У `ThreadFunction` розширено перевірку результату `install_rc` у циклі `attempt` — при виникненні помилок сесії DBI (`IsDbiSessionError`) виконується спроба повторного узгодження зв'язку замість аварійного виходу з черги.
- **Інтуїтивне підтвердження в `OptionBox`**: У `UpdateActions` для `State::Installing` встановлено за замовчуванням вибір `"Yes"` (індекс `1`) для запиту `"Skip this package?"`. Це дозволяє швидко підтвердити пропуск кнопками `A` або `+`, або скасувати діалог кнопкою `B` без впливу на процес інсталяції.
- **Синхронізація локальної черги (`LocalThreadFunction`)**: Включено `Result_UsbCancelled` у перевірку `cancelled` та уніфіковано переведення сесії у `State::Cancelled` за наявності запиту на скасування.
- **Тестування та верифікація**: Додано тест багатопакетної черги з пропуском у `test_queue_outcome.cpp`, піднято версію до `0.13.481` у `CMakeLists.txt`, успішно пройдено всі host-тести та скомпільовано цільовий NRO в WSL.

## v0.13.480 — Save data deletion mechanism & auto-creation on restore

- **Механізм видалення збережень у розділі `Tools > Saves`**: Додано дію `"Delete"` до спливаючого списку дій `Save Action` (при натисканні `A` на окремому збереженні або виділеній групі ігор), а також додано блок дій `ACTIONS` (Backup / Restore / Delete) до загального бічного меню `Save Options` (кнопка `+`).
- **Бічне меню `Delete Options` з фільтрацією**: При виборі видалення для живих сейвів відкривається меню параметрів, де користувач може вибрати конкретні облікові записи (`Accounts`) та типи збережень (`Save Types`), для яких необхідно виконати операцію.
- **Діалоги безпечного підтвердження**: Перед видаленням виводиться діалог `OptionBox` із запитом підтвердження та іконкою гри, що виключає випадкове видалення даних.
- **Реалізація операції видалення `Menu::DeleteSaves`**:
  - Для встановлених та видалених ігор: видалення сейвів із консолі за допомогою `fsDeleteSaveDataFileSystemBySaveDataSpaceId` з резервним викликом `fsDeleteSaveDataFileSystemBySaveDataAttribute`. Для залишкових записів у розділі "Deleted Games" гра повністю зникає зі списку після видалення сейву.
  - Для розділу "Backups": видалення файлів резервних копій (`.zip`, `.disa`) з SD-карти / накопичувача з наступним очищенням порожніх папок.
  - Оновлення списку через `ScanHomebrew()` та сповіщення `"Delete successful!"`.
- **Автоматичне створення сейву при відновленні (`Restore`)**: Вирішено проблему відновлення збережень на чистих або клонованих EmuNAND (де файли сейвів відсутні на розділі USER або пошкоджені записи в системній базі даних), а також для щойно встановлених ігор без першого запуску: функція `RestoreSaveInternal` зчитує метадані `NXSaveMeta` або `FsSaveDataExtraData` з архіву та створює файлову систему збереження на консолі через `fsCreateSaveDataFileSystem` перед розпакуванням.

## v0.13.479 — Automatic forwarder check, silent install & title mode restart prompt

- **Автоматична фонова перевірка форвардера при старті**: Реалізовано модуль `forwarder_auto_install.hpp` / `forwarder_auto_install.cpp`, який викликається під час ініціалізації додатку (`forwarder_auto::StartCheck()`) в окремому низькопріоритетному потоці та перевіряє наявність встановленого форвардера Homebrew Menu або Sphaira (перевірка стандартних Title ID `010000000000100D`, `050000000000100D`, а також згенерованих ідентифікаторів Sphaira на основі шляху до виконуваного файлу через `nsIsAnyApplicationEntityInstalled`).
- **Тихе фонове встановлення з `/Games/`**: Якщо форвардер відсутній на консолі, фоновий процес сканує папку `/Games/` на microSD карті на наявність NSP-файлу за маскою `Homebrew menu*.nsp`. Знайдений файл встановлюється без блокування інтерфейсу за допомогою рушія Yati (`yati::InstallFromFile`) та спеціалізованого класу `SilentInstallProgress`.
- **Зворотний зв'язок про Title ID у Yati**: Розширено інтерфейс `ui::InstallProgress` та логіку реєстрації додатків у `yati.cpp` викликом `pbox->OnTitleInstalled(app_id)`, що дає змогу точно зафіксувати встановлений Title ID безпосередньо в момент реєстрації запису в системі.
- **Пропозиція перезапуску в Title Mode**: Після успішного встановлення через чергу подій `evman::push` на головному UI-потоці показується діалогове вікно `OptionBox` `"Homebrew Menu forwarder installed. Restart into Title Mode now?"`. При натисканні `"Restart"` формується системний запит `appletRequestLaunchApplication(target_tid, nullptr)` та виконується коректний вихід з програми `App::Exit()`, що передає керування Horizon OS для запуску встановленого форвардера у повноцінному режимі застосунку (Title Mode).
- **Утиліти та локалізація**: Додано функцію `path::StartsWithIC` у `path_util.hpp` з повним покриттям у наборі тестів `test_path_util.cpp` (162 перевірки). Додано локалізовані рядки до всіх 14 мовних файлів (`assets/romfs/i18n/*.json`).

## v0.13.478 — Theme packages download & instant install prompt

- **Автоматична пропозиція встановлення після завантаження готових пакетів тем**: До цього моменту готові теми (зокрема Mario BG Dark та Switch 2 Theme by alexwak) у списку `Tools -> Themes` використовували `MakePackageAction`, який завантажував та розпаковував zip-архів у `/themes/`, після чого показував лише коротке повідомлення "Done" без діалогу запуску інсталятора тем (хоча теми з Themezer та закріплені теми одразу пропонували встановити).
- **Спільна логіка встановлення `PromptInstallTheme` та `InstallThemePackage`**: У просторі імен `sphaira::ui::menu::themezer` реалізовано:
  1. `PromptInstallTheme`: перевіряє наявність `NXThemesInstaller.nro` на консолі. Якщо файл присутній — показує `OptionBox` `"Theme downloaded, install now?"` та при виборі `"Install"` запускає інсталятор з аргументами шляхів до видобутих `.nxtheme` файлів. Якщо файл відсутній — пропонує завантажити `NXThemesInstaller.nro` з GitHub.
  2. `InstallThemePackage`: завантажує zip-пакет, розпаковує його у `/themes/` через `thread::TransferUnzipAll` з фільтрацією та збереженням шляхів усіх `.nxtheme` файлів, видаляє тимчасовий zip та викликає `PromptInstallTheme`.
- **Уніфікація пунктів меню тем `MakeThemePackageItem`**: У `settings_menu.cpp` для Mario BG Dark та Switch 2 Theme реалізовано створення пунктів через `MakeThemePackageItem`, що додає запит `"Download theme?"`, індикацію процесу завантаження та видобування через `ProgressBox`, а після завершення — негайну пропозицію встановлення.

## v0.13.477 — Game details stat label vertical alignment fix

- **Виправлення вертикального вирівнювання прокручуваних лейблів статистики**: У картці інформації про гру (`Game Details` / `DbiDetailsMenu`) довгі локалізовані лейбли (зокрема "Останній запуск" / "Last played"), довжина яких перевищує 1/3 ширини рядка, малюються через компонент `ScrollingText`. При виклику `m_stat_label_scrolls[...].Draw` передавався параметр вирівнювання `NVG_ALIGN_LEFT` без прапора вертикального вирівнювання. У результаті NanoVG застосовував за замовчуванням вирівнювання за базовою лінією (`NVG_ALIGN_BASELINE`), що зміщувало прокручуваний текст вгору приблизно на 15 пікселів (висоту шрифту) відносно статичних заголовків та власного значення праворуч.
- **Синхронізація вирівнювання з `NVG_ALIGN_TOP`**: Для `m_stat_label_scrolls` додано явний прапор `NVG_ALIGN_LEFT | NVG_ALIGN_TOP`, що забезпечує ідеальне вирівнювання рядка з `gfx::drawTextBold` та значеннями `gfx::drawText`. Також уніфіковано вирівнювання для прокрутки мов `m_language_scroll` (`NVG_ALIGN_LEFT | NVG_ALIGN_TOP`, `y + 1.f`).

## v0.13.476 — UTF-16 to UTF-8 decoding & Cyrillic filename fix for MTP

- **Виправлення декодування імен файлів та папок у `ReadString`**: В оригінальному коді `libhaze` при отриманні імен у форматі UTF-16 виконувалося пряме приведення кожного 16-бітного символу через `static_cast<char>(chr)`. Для кириличних символів (зокрема в системних назвах папок Windows Explorer, таких як "Нова папка" або "Новая папка", де 'Н' = `0x041D`) старший байт відкидався, утворюючи недійсний символ керування `0x1D` замість коректної багатобайтової UTF-8 послідовності (`\xD0\x9D`). Файлова система консолі (FatFs/libnx) відхиляла створення папки з невалідними символами помилкою `FsError_InvalidCharacter`.
- **Повноцінне декодування UTF-16 -> UTF-8**: У `ptp_data_parser.hpp` реалізовано коректний парсер, що перетворює символи UTF-16 у валідний UTF-8 рядок.
- **Повноцінне кодування UTF-8 -> UTF-16**: У `ptp_data_builder.hpp` реалізовано перетворення з UTF-8 у UTF-16 для коректної передачі назв файлів та папок з кирилицею та іншими не-ASCII символами назад у Windows Explorer.

## v0.13.475 — Full read-write support with commit for MTP Saves drive

- **Повноцінний Read-Write режим для MTP-сховища `Saves`**: Віртуальний MTP-диск розшифрованих збережень переведено з режиму read-only у повноцінний режим читання та запису.
- **Підтримка створення, запису та модифікації сейв-файлів**: Реалізовано методи `CreateFile`, `WriteFile`, `SetFileSize`, `OpenFile` (з прапорами запису) та `CloseFile`. При закритті модифікованого файлу автоматично викликається `fs->Commit()` (`fsFsCommit`), що надійно фіксує всі зміни у файловій системі збереження на консолі.
- **Підтримка роботи з піддиректоріями та видаленням**: Додано реалізацію `CreateDirectory`, `DeleteDirectoryRecursively`, `DeleteFile`, `RenameFile` та `RenameDirectory` всередині змонтованих директорій сейвів з обов'язковим комітом змін.
- **Оновлення статусу та вільного місця**: Функція `GetFreeSpace` тепер повертає реальне/доступне вільне місце замість 0, а назву сховища змінено з `Saves (read-only)` на `Saves`.

## v0.13.474 — Full MTP property handling, GetObjectPropDesc & SendObjectPropList fixes

- **Виправлення відмови створення папок у `SendObjectPropList` (0x9808)**: При створенні папок чи файлів Windows Explorer надсилає набір розширених MTP-властивостей (`StorageID`, `ObjectFormat`, `ParentObject`, `PersistentUniqueObjectIdentifier` тощо) перед назвою об'єкта або разом із нею. В оригінальному коді `libhaze` будь-яка властивість, окрім `ObjectFileName`, негайно викликала `R_THROW(haze::ResultUnknownPropertyCode())`, що миттєво обривало створення папки з системною помилкою Windows («Пристрій припинив відповідати, або його було відключено»). Тепер парсер коректно зчитує та пропускає всі підтримувані типи даних (U8, U16, U32, U64, U128, String, масиви) згідно зі стандартом MTP.
- **Виправлення пошкодження дескриптора властивостей у `GetObjectPropDesc`**: Виправлено пропущений `break;` після `PersistentUniqueObjectIdentifier`, через який відповідь дескриптора містила зайві байти від сусіднього `case ObjectSize`.
- **Підтримка запиту всіх властивостей (`property_code == 0`)**: У `GetObjectPropList` додано обробку коду властивості `0` як запиту на повернення всіх підтримуваних властивостей згідно з MTP специфікацією.
- **Підтримка перейменування через `Name` у `SetObjectPropValue`**: Дозволено оновлення назви об'єкта як через `ObjectFileName`, так і через `Name`.

## v0.13.473 — Installed Games save scanning & category listing fix

- **Повний список встановлених ігор у Saves -> Installed Games**: Перероблено формування списку встановлених ігор. Тепер меню гарантовано виявляє та показує абсолютно всі встановлені на консолі ігри (через системні виклики `nsListApplicationRecord` із верифікацією метаданих через `title::GetMetaEntries`, аналогічно розділу Tools -> Games). Якщо гра встановлена, але ще не має створеного сейву на консолі або активний інший обліковий запис, для неї все одно формується плитка гри з іконкою та назвою.
- **Підтримка порожніх станів ("Empty...")**: Для категорій «Видалені ігри» (`Deleted Games`) та «Резервні копії» (`Backups`) у разі відсутності відповідних файлів коректно та чітко відображається напис «Empty...».
- **Відновлення бекапів для нововстановлених ігор**: У `PromptSaveTypeOptions` додано fallback для операції `Restore`, що дозволяє відновити бекап для встановленої гри навіть тоді, коли на консолі ще немає активного живого сейву.

## v0.13.472 — MTP folder and file creation storage_id fix

- **Виправлення створення папок та файлів через MTP**: Виправлено критичну помилку в реалізації MTP-відповідей `libhaze`. При викликах операцій `SendObjectInfo` (0x100C) та `SendObjectPropList` (0x9808) поле `storage_id` у структурі відповіді `PtpNewObjectInfo` помилково заповнювалося дескриптором батьківського об'єкта (`parentobj->GetObjectId()`), замість ідентифікатора сховища (`parentobj->GetStorageId()`). Через цю розбіжність Windows Explorer вважав відповідь пристрою некоректною і показував помилку «Не вдалося створити папку у вказаному місці. Пристрій припинив відповідати, або його було відключено».
- **Патчі libhaze**: Додано автоматичні патчі 5 та 6 до `patch_libhaze.cmake`, які виправляють встановлення коректного `storage_id` для обох MTP/PTP операцій створення об'єктів.
- **Логування**: Виправлено повідомлення логу в `FsProxy::CreateDirectory` у `haze_helper.cpp`.

## v0.13.471 — Save categories hub & custom save backup search paths

- **Початковий хаб вибору категорій збережень (`SaveHubMenu`)**: При переході у розділ **Saves** з меню Tools або Main Menu тепер спочатку відкривається інтерфейс вибору однієї з трьох категорій:
  1. **Installed Games** («Встановлені ігри») — перегляд і керування збереженнями встановлених ігор.
  2. **Deleted Games** («Видалені ігри») — перегляд і керування залишковими сейвами ігор, які вже видалено з консолі.
  3. **Backups** («Резервні копії») — перегляд та відновлення збережених резервних копій.
- **Підтримка режимів категорій у `save::Menu`**: Додано перелік `Category` у конструктор `save::Menu`. При відкритті вибраної категорії сітка автоматично фільтрує лише потрібний тип сейвів, а при натисканні кнопки **B** плавно повертає користувача назад до вибору категорій. Прямий перехід до сейвів гри з Game Details (`app_id_filter != 0`) безпосередньо відкриває збереження цієї гри.
- **Користувацькі папки пошуку резервних копій у Settings**: У налаштуваннях програми додано категорію `Saves -> Save Backup Search Paths` («Шляхи пошуку резервних копій»). Користувач може додати будь-яку папку з карти пам'яті через `filepicker::Menu` або видалити раніше додані папки.
- **Інтеграція користувацьких шляхів у сканування бекапів**: Усі модулі виявлення та відновлення резервних копій (`CollectBackups`, `ReadBackupEntries`, `FindLatestBackupPath`) автоматично сканують стандартні каталоги (`/dumps`, `/switch/DBI/saves`) разом із усіма налаштованими користувацькими папками з конфігурації `[save_backup_paths]`.
- **Локалізація та документація**: Додано повний набір перекладів для EN та UK локалізацій, оновлено README.md та living documentation.

## v0.13.470 — raw DISA save restore, save discovery & MTP USER:/save

- **Відновлення запакованих (сирових DISA/DPFS) сейвів**: Додано підтримку прямого відновлення сирових контейнерів сейвів (`000000000000001e`, `.disa`, `.bin`, вивантажених з NAND/DBI Explorer) у `RestoreSaveInternal`. Контейнери розпізнаються за заголовком `DISF` за зміщенням `0x100` і записуються безпосередньо у відповідний NAND BIS-розділ (`USER` або `SYSTEM`) за шляхом `/save/<save_data_id>` блоками через `thread::Transfer` з прогрес-баром.
- **Розпізнавання та виявлення бекапів**: У `CollectBackups` та `ReadBackupEntries` додано сканування сирових файлів сейвів (`%016lX`, `*.disa`, `*.bin`) у папках бекапів та `/dumps/`, що дозволяє вибирати їх у вікні відновлення та відображати плитки бекапів у меню сейвів.
- **Відновлення сейвів через File Browser**: У меню дій файлу `File Options` додано пункт `Restore save data`. При виборі файлу сейву Sphaira знаходить відповідний слот на консолі або дозволяє вибрати цільову гру/профіль користувача через зручний список `PopupList`.
- **MTP-сховища для прямого доступу до сирових сейвів**: У `haze_helper.cpp` додано сховища `NAND Saves (USER:/save)` та `NAND Saves (SYSTEM:/save)` на базі `FsProxy` з `FsNativeBis`, що надає можливість читати та записувати сирові сейви безпосередньо з ПК через USB за аналогією з DBI MTP Explorer.
- **Налаштування та локалізація**: Додано перемикачі відображення `USER:/save` та `SYSTEM:/save` у налаштування MTP сховищ (`Settings → Network → MTP storages`) з повною підтримкою англійської та української мов.

## v0.13.469 — unified pending UI & updater work

- Збережений WIP інтегровано поверх відновленої mainline без втрати User-Agent,
  installer diagnostics, header layout чи cURL shutdown.
- `gfx::ImageViewport` став спільним механізмом pan/zoom/pinch для Theme Creator і
  forwarder icon crop; crop працює з локальними NRO/PNG/JPEG і нормалізує результат.
- File Viewer має актуальну модель range selection, операції Copy/Cut/Paste/Comment/
  Uncomment, action icons і коректну навігацію на межах.
- Updater повертає фокус на перший елемент, позначає доступні Kefir updates і повторює
  завантаження каталогу лише після повернення мережі.
- File Picker використовує стабільний верхній header slot, збережений з колишньої
  mainline, тому довгі шляхи не конфліктують із нижнім статусним рядком.

## v0.13.468 — cURL shutdown & shared handle serialization

- Синхронні `ToMemory`, `ToFile`, `FromMemory` та `FromFile` використовують один
  mutex для безпечної роботи зі спільним `g_curl_single`.
- `curl::RequestShutdown()` запускається на початку виходу з App, пробуджує черги
  передач і блокує запуск нових операцій після запиту зупинки.
- `curl::Exit()` очищає shared handle лише під захистом цього mutex.

## v0.13.467 — versioned HTTP User-Agent

- У `sphaira/include/defines.hpp` додано єдине спільне визначення версіонованого User-Agent: `constexpr auto APP_USER_AGENT = "Sphaira/" APP_VERSION;`.
- У `sphaira/source/download.cpp` вилучено застарілий літерал `API_AGENT = "TotalJustice"`; функція `SetCommonCurlOptions()` тепер встановлює `CURLOPT_USERAGENT` з `APP_USER_AGENT`.
- У `sphaira/source/utils/devoptab_curl_device.cpp` у спільну функцію `MountCurlDevice::curl_set_common_options()` додано встановлення `CURLOPT_USERAGENT` із `APP_USER_AGENT`, завдяки чому всі операції віддалених curl-mounts (push, pull, open, dir list, range probe, stat, WebDAV, FTP) надсилають коректний User-Agent.
- Політики TLS, перенаправлень, автентифікації, HTTP-семантика, UI, файли локалізації (i18n) та зовнішні залежності не змінювалися.
- Оновлено `upstream_audit.md` (адаптацію upstream `2eabcec` / `3ef698b` позначено як завершену).
- Версію піднято до `0.13.467` у `sphaira/CMakeLists.txt`; перевірки `git diff --check`, перевірка символів та збірка WSL `ReleaseWithInstall` пройшли успішно (`[100%] Built target sphaira_nro`). Очікується ручний Switch remote-mount smoke check.

## v0.13.466 — caller-selected header layout

- `MenuBase::SetTitleSubHeading` тепер приймає `top_row`; місце підзаголовка визначається екраном, а не довжиною поточного рядка.
- Шляхи та довільні назви у File Browser, File Picker, Homebrew, GHDL, Games, Cheats, Save Menu, App Store і IRS завжди використовують верхній широкий слот після версії; за потреби там працює наявний `ScrollingText`.
- Короткі значення — індикатор сторінки Themezer, Title ID гри, параметри фільтра App Store і лічильник `current / total` — лишаються у нижньому рядку.
- Зміна слота чи очищення тексту скидає scroll state, тому попередній зсув не переходить на інший екран.
- Версію піднято до `0.13.466`; host unit tests, WSL `ReleaseWithInstall` та `git diff --check` пройшли успішно.

## v0.13.465 — text editor multi-line editing

- Реалізовано вибір діапазону рядків у режимі редагування тексту (`Edit` mode) через спливаюче вікно Actions (`Select range` / `Clear selection`).
- Під час вибору діапазону підказки футера оновлюються на `A = Finish selection`, `B = Cancel selection`. Активний діапазон підсвічується фокусною смугою теми (`ThemeEntryID_FOCUS`) з прозорістю `alpha = 0.35`, зберігаючи синтаксичне підсвічування та розмір шрифту тексту.
- Додано процесовий буфер обміну рядками (`s_line_clipboard`) для дій `Copy`, `Cut`, `Paste below` та `Delete` між відкритими текстовими редакторами без потреби збереження на диск.
- Дії `Delete` та `Cut` над діапазоном чи поточним рядком гарантують збереження щонайменше одного порожнього рядка в документі.
- Мутуючі операції створюють рівно один Undo-снапшот перед зміною; копіювання (`Copy`) не створює Undo-записів.
- Додано чисті допоміжні функції `CommentIniLine` та `UncommentIniLine` у `sphaira/include/text_helper.hpp` із збереженням ведучих пробілів/табуляцій та повним набором хостових юніт-тестів у `tests/test_text_helper.cpp`.
- Пункти `Undo` та `Redo` додано у спливаючий список дій (`ShowLineActions`), а заголовок списку динамічно відображає `Line actions (line %zu)` або `Line actions (lines %zu - %zu)`.
- Версію піднято до `0.13.465` у `sphaira/CMakeLists.txt`; перед комітом виконано host unit tests, WSL `ReleaseWithInstall` та `git diff --check`.

## v0.13.460 — text editor basics

- У текстовому editor `B` переходить із Edit до View у межах того самого
  документа. Незбережені зміни зберігаються в пам'яті; наступний `B` у View
  показує чинний Save / Discard / Cancel flow, включно зі збереженням writable
  small-file документа.
- Утримання Up або Down в Edit mode зупиняється на першому чи останньому
  рядку. Після відпускання нове натискання зберігає звичний wrap; shared List
  і всі інші меню не змінено.
- Активний INI рядок зберігає той самий розмір і syntax colors, а double-tap
  перемикає isolated `true`/`false`, `u8!0x0`/`u8!0x1` і plain `0`/`1`, не
  торкаючись відступів чи коментарів. Host assertions відхиляють `10`, `01`,
  `0x0`, `0x1` та інші не-булеві числа.
- Read-only viewer використовує узгоджений з Image Viewer zoom: `ZL` +
  D-pad Up/Down, LS або RS Up/Down масштабує текст; без ZL ці inputs
  прокручують viewport, а використаний zoom chord не викликає page up на
  відпускання ZL.
- Версію піднято до `0.13.460`. Gemini пройшов `tests/run.sh` (13 suites,
  743 declarations), WSL `ReleaseWithInstall` (`[100%] Built sphaira_nro`) та
  `git diff --check`. Потрібен Switch smoke-test Edit → View, boundary wrap,
  INI 0/1, ZL zoom і paging.

## v0.13.458 — Homebrew settings & search paths

- Додано окрему категорію `Homebrew` у налаштуваннях одразу після `General`.
- У категорію `Homebrew` перенесено `Forwarders` (з розділу Install),
  `Homebrew App Store` (з розділу Software) та `Replace hbmenu on exit` (з General)
  без дублювання в старих місцях.
- Реалізовано підменю `Homebrew Search Paths`:
  - Додавання користувацьких шляхів на карті microSD через FilePicker (`Add folder`).
  - Відображення списку додаткових шляхів та видалення через діалогове вікно підтвердження (`OptionBox`).
  - Системний шлях за замовчуванням `/switch` залишається незмінним та не дублюється в конфігурації.
  - Список додатків Homebrew миттєво оновлюється при зміні шляхів.
- Оновлено 13 локалізацій (усі крім `ru.json`).
- Версію піднято до `0.13.458`. Пройдено валідацію JSON, host test suite (`tests/run.sh`: 13 suites,
  742 declarations) та `git diff --check`. Потрібен Switch smoke-test керування шляхами та налаштуваннями.

## v0.13.457 — text viewer viewport scrolling

- У read-only text viewer відокремлено поведінку курсора/редагування від
  прокрутки viewport: натискання Up/Down, D-pad та відхилення обох стіків одразу
  зміщують видиму область на один рядок без затримок переміщення фокуса до краю
  екрана.
- Для потокового перегляду (streamed reader великих файлів) буфер рядків
  утримується наперед, а перехід між сторінками відбувається безперервно та
  плавно на межі видимого viewport без повного читання чи індексації файлу в пам'ять.
- Збережено всі елементи керування: гортання сторінок на відпускання L/R (1 сторінка)
  та ZL/ZR (10 сторінок), масштабування L + RS та pinch touch, вертикальний one-finger swipe
  та неклікабельні підказки у footer.
- Версію піднято до `0.13.457`. Пройдено host test suite (`tests/run.sh`: 13 suites,
  742 declarations), WSL `ReleaseWithInstall` (`[100%] Built target sphaira_nro`) та
  `git diff --check`. Потрібен Switch smoke-test плавного прокручування великих файлів.

## v0.13.456 — text viewer pager

- File Browser отримав контекстну дію `View as text` для кожного звичайного
  файла. Пріоритети звичайного відкриття кнопкою A (NRO, installer, image,
  archive, розпізнаний text та file association) не змінено.
- Файли понад 4 MiB відкриваються тільки для читання через лінивий пейджер:
  reader читає 4 KiB chunks, тримає поточну й не більш як три сусідні сторінки,
  byte offsets та номери рядків; увесь файл або попередній 4 MiB preview у RAM
  не завантажується. Некоректний short read або FS error викликає штатний
  `Failed to read file` error box і не може зациклити discovery offset.
- У read-only viewer D-pad та обидва стіки рухаються рядками; `L`/`R` на
  відпускання гортають на сторінку, `ZL`/`ZR` — на десять. `L` з правим стіком
  масштабує текст і не гортає після відпускання L. Пінч масштабує, вертикальний
  one-finger swipe у streamed view змінює сторінку. Footer показує ці дії як
  неклікабельні підказки, тому touch не запускає paging або zoom через кнопку.
- Версію піднято до `0.13.456`. Gemini пройшов `tests/run.sh` (13 suites,
  742 declarations), WSL `ReleaseWithInstall` (`[100%] Built target
  sphaira_nro`) та `git diff --check`. Потрібен Switch smoke-test великого
  тексту, контролерів і жестів.

## v0.13.455 — відступи INI text viewer

- Виправлено накладання значення на довгі ключі INI: після номера рядка NanoVG залишався на 16 px, через що `textBounds` недооцінював ширину ключа, який малюється 18 px.
- Перед вимірюванням ключа явно відновлюється 18 px; parser, кольори, clipping та поведінка редактора не змінені.
- Версію піднято до `0.13.455`; WSL target `sphaira` і `git diff --check` пройшли. Повний `ReleaseWithInstall` окремо зупинився на Make-залежності `sphaira/sphaira.elf` після успішного лінку `sphaira`, тож NRO-пакування слід відновити окремо. На Switch потрібно відкрити довгий ключ у `system_settings.ini` і переконатися, що `=` та значення не перетинають кінець ключа.

## v0.13.454 — діагностичні повідомлення встановлення NSP (NSP install diagnostics)

- **Централізована межа помилок:** Усі діагностичні описи реалізовано виключно на спільній межі `ui::GetResultDescription(Result)` у `sphaira/source/ui/error_box.cpp`. Кожен виклик через `App::PushErrorBox` (File Browser, потік/USB) та черга DBI (`Menu::AddError`) отримують ідентичні локалізовані описи без дублювання в окремих меню.
- **Невідповідність системної прошивки:** Для `MAKERESULT(Module_Libnx, LibnxError_IncompatSysVer)` (який `sphaira/source/yati/nx/es.cpp` повертає для HOS < 4.0.0) виводиться мінімально необхідна версія 4.0.0 (через наявний форматер `version::FormatPacked(4 << 26)`), динамічна фактична версія прошивки з `hats::getSystemFirmware()` та чітка інструкція оновити системну прошивку консолі перед повторною спробою.
- **Діагностика цілісності NSP:**
  - `Result_StreamUnexpectedEof` (коротке читання потоку/файлу) отримує чітке повідомлення про передчасне завершення файлу або передачі з порадою скопіювати чи завантажити файл знову.
  - `Result_NspBadMagic` (невдача валідації структури PFS0/NSP) позначається як недійсний NSP, що може бути пошкодженим або неповним, із пропозицією повторного копіювання/завантаження.
- **Свідомі виключення:** `Result_FsUnknownStdioError`, стандартні файлові помилки FS, помилки ключів, підписів і квитків не класифікуються як пошкоджені файли і зберігають стандартне відображення.
- **Локалізація та тести:** Усі 14 мовних файлів (`assets/romfs/i18n/*.json`) оновлено однаковими ключами та перевірено JSON-парсером. Додано перевірку пакування 4.0.0 у `tests/test_version_compare.cpp` (34/34 checks pass). Успішно пройдено `tests/run.sh` (`all green`, 734 header declarations) та WSL ReleaseWithInstall збірку (`[100%] Built target sphaira_nro`).

## v0.13.454 — масові дії Homebrew

- Homebrew отримав вибір `X`, інверсію `Y` і очищення вибору через `B`;
  `X` переходить до наступного NRO, як у чинному Games flow.
- Рендер повторно використовує `grid::Menu::DrawSelectionMark`: у List
  checkbox показано у лівому gutter, а у Icon/Grid/HB Menu — штатну плиткову
  позначку. Нового механізму для layout або checkbox не додано.
- Homebrew Options працює з вибраними NRO (або з focused, якщо вибору немає),
  показує `Star`, `Unstar` і підтверджене `Delete`. При видаленні `.star`
  sidecar видаляється першим; його помилка зупиняє операцію до видалення NRO.
  Синтетичний Kefir Updater stub виключено з selection і всіх цих дій.
- Версію піднято до `0.13.454`. Gemini пройшов `tests/run.sh` (13 suite,
  734 declarations), WSL `ReleaseWithInstall` (`Built target sphaira_nro`) і
  `git diff --check`. Потрібний Switch smoke-test: X/Y, List/Icon/Grid/HB Menu,
  Star/Unstar і підтверджене видалення кількох NRO.

## v0.13.453 — PFS0/NSP parser hardening

- Спільний `yati::container::Nsp` тепер вимагає exact reads для PFS0 header,
  file table і string table; short input відхиляється до parsing metadata.
- `pfs0.hpp` централізує валідацію on-disk PFS0 metadata: pinned binary layouts,
  caps 65,535 files / 4 MiB string table, checked unsigned arithmetic, safe
  `s64` conversion, bounded NUL-name search і end-of-container checks.
- Найближча `source::Base::GetSize()` abstraction повідомляє відомі bounds для
  file/NCA/buffer readers. Лише `FsError_NotImplemented` означає unknown-size
  stream; помилки size query коректно доходять до caller.
- Додано `tests/test_pfs0_nsp.cpp` з valid case і негативними межами: short
  metadata, hostile allocation fields, invalid/missing-NUL name, offset/size
  overflow і known-size overrun. MSP installer, manifest/staging/rollback, UI,
  web/MTP transport та i18n не змінювалися.
- Gemini фактично пройшов focused parser test (41 checks), `tests/run.sh`
  (`all green`; dead symbols 731/731), WSL `ReleaseWithInstall`
  (`[100%] Built target sphaira_nro`) і `git diff --check`. Версію піднято до
  `0.13.453`; hardware smoke-test не потрібний, бо runtime UI/transport не змінено.

## v0.13.452 — відновлення loader thread affinity перед NRO

- Адаптовано upstream `87c855a5973f6fa5e2bffc818ca5a69fb18d6090` у `hbl/source/main.c`.
- Перед кожним переходом до `nroEntrypointTrampoline()` loader отримує реальну mask процесу через `svcGetInfo(..., InfoType_CoreMask, CUR_PROCESS_HANDLE, 0)` і відновлює її на main thread через `svcSetThreadCoreMask(CUR_THREAD_HANDLE, -1, core_mask)`.
- Невдача будь-якого syscall фатальна через чинний `diagAbortWithResult`; дозволений у NPDM `highest_cpu_id = 3` не використовується як фіксована mask. `hbl.json`, локалізації та UI не змінювалися.
- Піднято `sphaira_VERSION` до `0.13.452`. Gemini підтвердив успішні WSL `ReleaseWithInstall` (`[100%] Built target sphaira_nro`) та `git diff --check`; потрібен Switch smoke-test первинного й повторного запуску NRO.

## v0.13.451 — користувацькі шляхи пошуку NRO (custom NRO search paths)

- **Нормалізація та збереження:** `path::NormalizeAbsoluteSdPath` приймає лише безпечні absolute SD paths; `[homebrew_paths]` читається через `ini_browse`, а `/switch` лишається незмінним default root і не зберігається в конфігурації. Перед створенням `fs::FsPath` відхиляються path length `>= FS_MAX_PATH`, traversal, control bytes, `\\`, `:`, root і `/switch`.
- **Сканування:** `nro_scan_depth(path, entries, 2)` обходить custom root, його дітей та grandchildren, пропускає hidden entries і розпізнає `.nro` без залежності від регістру. NRO results дедуплікуються за canonical path, а порожній Homebrew list безпечний.
- **UI:** File Browser показує add/remove тільки для дозволеного SD-каталогу; видалення має `OptionBox` confirmation, а успішна зміна зберігає конфіг і надсилає `SignalChange()`.
- **Ресурси й перевірка:** `sphaira_romfs_sync` синхронізує змінені ROMFS assets; додано переклади до всіх locale-файлів, крім `ru.json`, який свідомо відкладено до окремого i18n pipeline. Пройдено host tests (`path_util`: 154 checks), JSON validation, WSL `ReleaseWithInstall` і `git diff --check`.

## v0.13.449 — NFS phase 1 (read-only source)

- **Pinned dependency:** статичний `ITotalJustice/libnfs@65f3e11` підключено через `FetchContent`; dependency docs, examples і tests вимкнено.
- **Read-only backend:** новий NFS devoptab використовує спільне володіння `MountNetworkDevice2()`, `nfs_parse_url_dir()` для повного nested export path, RAII cleanup і захисні `EROFS` write callbacks.
- **Безпечний URL boundary:** приймається лише `nfs://hostname-or-IPv4[:port]/export`; credentials, IPv6, query/fragment, literal і encoded traversal, небезпечні separators, control characters та URL довші за 768 символів відхиляються до `FsPath` construction.
- **File Browser і Settings:** NFS доступний у connection flow, source picker, protocol picker і connection test. Усі входи зберігають `FsEntryFlag_ReadOnly`; невалідні saved entries не потрапляють у root browser чи picker, але залишаються доступними для редагування в Settings.
- **Перевірка:** `tests/run.sh` пройшов (`nfs_url`: 194 checks; dead-symbol guard: 731/731), WSL `ReleaseWithInstall` зібрав `sphaira_nro`, `git diff --check` чистий. Ручний smoke test на Switch залишається відкритим.

## v0.13.448 — очищення екранних NTP-сповіщень

- **Прибрано тимчасові progress tooltip-и NTP:** із `sphaira/source/ntp.cpp` вилучено прапорець `SHOW_NTP_PROGRESS_TOOLTIPS` та виклики `App::Notify` з `ReportSyncStage()`. Жодні проміжні діагностичні повідомлення (етапи мережі, DNS, socket, перевірка offset, помилки/fallback) більше не з'являються на екрані як сповіщення.
- **Збережено діагностичне логування:** `ReportSyncStage()` і `ReportSyncFailure()` продовжують повноцінно записувати всі етапи, зміщення та коди Horizon Result у лог як `[NTP] ...`.
- **Єдине сповіщення — локалізоване "Clock synced":**
  - Вилучено нелокалізоване службове сповіщення `NTP: UI clock refreshed`.
  - Єдиним екранним повідомленням від підсистеми NTP залишено `"Clock synced"_i18n`.
  - Сповіщення `"Clock synced"` ставиться в чергу через `evman::push` виключно на шляху прямого успішного оновлення User Clock після виклику `__libnx_init_time()`.
  - Якщо час уже синхронізований (зміщення менше за `MIN_CORRECTION_SECONDS`), `RunSync()` завершується без повідомлень.
  - На fallback-шляху `used_fallback` (увімкнення automatic correction через `set:sys` та процесний offset) сповіщення "Clock synced" не виводиться, оскільки системний User Clock HOS ще не було змінено наживо.
- **Верифікація:**
  - Успішно виконано збірку WSL `ReleaseWithInstall` (`[100%] Built target sphaira_nro`);
  - Перевірено `git diff --check`, піднято версію застосунку до `0.13.448`.

## v0.13.447 — upstream-equivalence hardening: безпечне ZIP extraction

- **Спільна валідація на першому проході архіву:** у `thread::TransferUnzipAll()` (`sphaira/source/threaded_file_transfer.cpp`) додано повну валідацію структури імен, буферних лімітів та розмірів на етапі sizing pass до створення будь-якого каталогу чи файла на диску.
- **Inline path validator:** у `sphaira/include/path_util.hpp` додано helper `path::IsSafeArchiveEntry(std::string_view)`:
  - дозволяє нормальні відносні шляхи (`switch/app/app.nro`) та записи каталогів (`switch/app/`);
  - відхиляє порожні імена (`""`), початковий слеш (`/`), зворотні слеші (`\`), керуючі символи (< 0x20 та DEL 0x7F) і двокрапки (`:`, блокуючи схеми й пристрої на зразок `sdmc:/`);
  - відхиляє компоненти шляху, що точно дорівнюють `.` або `..` на початку, всередині чи наприкінці шляху (запобігаючи directory traversal), зберігаючи валідні файли з крапками (`.config`, `..data`, `file.name`, `.gitignore`).
- **Перевірка довжини та обрізання імен:**
  - перед створенням `fs::FsPath` перевіряється, що `info.size_filename` вміщується в буфер і точно збігається з `std::strlen(name_buf)`; у разі обрізання або невідповідності повертається `FsError_TooLongPath`;
  - перед `fs::AppendPath()` перевіряється сумарна довжина вихідного шляху (`base_path` + розділювач при потребі + `info.size_filename` + NUL) щодо ліміту `sizeof(fs::FsPath)` (0x301 байт) із поверненням `FsError_TooLongPath`.
- **Захист від цілочисельного переповнення:**
  - `ginfo.number_entry` перевіряється перед приведенням до `s64` (значення більше за `std::numeric_limits<s64>::max()` відхиляються з `FsError_InvalidSize`);
  - накопичення `total_size` для `uncompressed_size` контролюється на переповнення `s64` (при перевищенні повертається `FsError_InvalidSize`).
- **Збереження санітизації та поведінки:**
  - збережено чинну роботу `SanitizeZipEntryName()` для безпечних неструктурних символів, що не підтримуються HOS (`*`, `?`, `"`, `<`, `>`, `|`);
  - збережено семантику фільтрів `UnzipAllFilter`, облік прогресу `pbox` і відмову від SD-специфічного free-space check (оскільки helper працює також із save data та іншими ФС).
- **Охоплені виклики:** захист автоматично діє для всіх 11 call sites `TransferUnzipAll` (Appstore, direct-link / GitHub downloads, cheats, firmware, File Browser extraction, save restore, translations).
- **Верифікація:**
  - розширено host unit-тести `tests/test_path_util.cpp` (106 checks passed);
  - успішно пройдено `tests/run.sh` (всі тести зелені, dead-symbol guard 730/730);
  - збірку WSL `ReleaseWithInstall` виконано успішно (`[100%] Built target sphaira_nro`);
  - перевірено `git diff --check`, піднято версію до `0.13.447`.

## v0.13.446 — NTP через системну automatic correction

- Вилучено виклик `DisableAutomaticCorrection()`, який вимикав автоматичну системну корекцію годинника HOS і перешкоджав перенесенню Network Clock у User Clock.
- Збережено спроби запису User Clock та Network Clock через `time:su` і `time:s`. Після спроби оновлення Network Clock виконується повторне зчитування User Clock: якщо воно вже збігається з NTP (до 2 с), система вважає це негайним live-синхроном (очищається процесний offset Sphaira, оновлюється база часу libnx `__libnx_init_time()` і показується локалізоване "Clock synced").
- Якщо User Clock все ще відхиляється (через те, що automatic correction була вимкнена під час завантаження HOS), через `set:sys` зберігається `NetworkSystemClockContext` і вмикається `setsysSetUserSystemClockAutomaticCorrectionEnabled(true)`. При цьому не виводиться "Clock synced" і не стверджується live-зміна системного годинника HOS, а в tooltip/log додається повідомлення: `automatic correction enabled; reboot required to update HOS User Clock`.
- Збережено процесний offset `g_display_offset` Sphaira для негайного показу точного NTP-часу в UI Sphaira до перезавантаження консолі.
- Збірку WSL `ReleaseWithInstall` та `git diff --check` пройдено успішно (`[100%] Built target sphaira_nro`), версію піднято до `0.13.446`. Апаратна перевірка на реальній Switch залишається відкритою.

## v0.13.445 — NTP User Clock через set:sys

- Додано fallback на IPC-сервіс `set:sys`, який виконується після відмови обох live-шляхів `time:su` та `time:s` (як-от з `0x00000274` / `time::ResultNoCapability`).
- При fallback отримується `TimeSteadyClockTimePoint`, обчислюється `TimeSystemClockContext` (`offset = NTP - steady.time_point`), записуються `UserSystemClockContext` та `NetworkSystemClockContext`, а також вимикається automatic correction в `set:sys`.
- Кожен етап `set:sys` та точні помилки `Result` відображаються через тимчасові diagnostic tooltip-и та дублюються у `log.txt`; запис до `errors.txt` виконується лише у разі відмови `set:sys`, що запобігає появі хибних записів про помилки при успішній синхронізації.
- Додано процесний offset `g_display_offset` у `ntp`: при використанні fallback `set:sys` він оновлюється на різницю часу і додається до `std::time(NULL)` у `MenuBase::GetPolledData`, завдяки чому годинник Sphaira і скрінсейвер миттєво показують правильний NTP-час без виклику `__libnx_init_time()` та без зміни файлових/ігрових таймстемпів.
- Після успішного set:sys fallback показується сповіщення `Clock synced`.
- Збірку WSL `ReleaseWithInstall` та `git diff --check` пройдено успішно (`[100%] Built target sphaira_nro`), версію піднято до `0.13.445`. Апаратна перевірка на реальній Switch залишається відкритою (pending hardware verification: миттєве оновлення годинника Sphaira, NTP trace та збереження часу після перезапуску).

## v0.13.444 — видимий NTP diagnostic trace

- Лог з v0.13.443 підтвердив, що `time:su` також повертає `0x00000274`, як і `time:s`; вибір сервісу більше не є невідомою змінною.
- Додано тимчасовий `SHOW_NTP_PROGRESS_TOOLTIPS = true`. `ReportSyncStage` одночасно пише `[NTP]`-рядок у `log.txt` та показує tooltip зліва; `ReportSyncFailure` додає точний Result.
- Трейс охоплює мережу, DNS, UDP socket/send/receive, перевірену NTP-відповідь, читання User Clock й offset, відкриття сервісу та clock-сеансів, вимкнення automatic correction, обидва clock writes, fallback, UI refresh і завершення.
- Збірку WSL `ReleaseWithInstall` успішно пройдено (`[100%] Built target sphaira_nro`); версію застосунку піднято до `0.13.444`. Після апаратного тесту достатньо встановити `SHOW_NTP_PROGRESS_TOOLTIPS` у `false`, щоб вимкнути tooltip-и без зміни логування.

## v0.13.443 — запис NTP-часу через `time:su`

- Лог з фізичної Switch підтвердив: NTP-сервер відповідає, але `time:s` відхиляє запис системного годинника з `0x00000274`; отже, це не мережевий або UI-збій.
- `SetSystemTime()` тепер спочатку використовує `time:su`, призначений для system-user операцій на HOS 9.0.0+, і переходить на `time:s` лише коли User system clock через перший сервіс не записано.
- Вимкнення automatic correction та запис Network system clock залишені best-effort. Успіх синхронізації все ще вимагає запису User system clock, після чого чинний UI callback одразу оновлює базу часу libnx і показує `Clock synced`.
- Якщо обидва сервісні шляхи відмовляють, `errors.txt` зберігає окремі Result для `time:su` і `time:s`.
- `hbl` у поточному Kefirosphere вже має `service_access: ["*"]`; тому патч Atmosphère/Kefirosphere для ACL не потрібен і не додавався.
- Збірку WSL `ReleaseWithInstall` успішно пройдено (`[100%] Built target sphaira_nro`); версію застосунку піднято до `0.13.443`. Потрібна ручна перевірка на Switch.

## v0.13.442 — усунення крашу File Browser при завантаженні асоціацій

- Виправлено краш у `filebrowser::Menu::LoadAssocEntries`, який виникав під час завантаження асоціацій запусків (наприклад, `xrick_libretro_libnx.ini`).
- Причина: при додаванні багатьох елементів у `m_assoc_entries` (`std::vector<FileAssocEntry>`) автоматична реалокація вектора виконувала копіювання великих об'єктів `FileAssocEntry` (із буфером `fs::FsPath` розміром 0x301 байт), викликаючи переповнення / краш у `memset`.
- Рішення: створено функцію `CountAssocEntriesPath`, яка попередньо підраховує кількість валідних `.ini` файлів у каталогах `romfs:/assoc/` та `paths::ASSOC`. Перед додаванням записів викликається `m_assoc_entries.reserve(...)`, що запобігає реалокації вектора під час завантаження.
- Збірку WSL `ReleaseWithInstall` та перевірку `git diff --check` пройдено успішно. Версію застосунку піднято до `0.13.442`.

## v0.13.441 — захист звичайного хрому UI Sphaira

- Змінено порядок малювання віджетів у `App::Draw()` (`sphaira/source/app.cpp`): спочатку малюються активне меню та всі немодальні віджети/панелі/переглядачі, після чого малюється стандартний хром `DrawChrome()` (розділювальні лінії заголовка та футера, статус-бар, заголовок та підказки кнопок).
- Додано метод `IsModal()` до базового класу `Widget` (`sphaira/include/ui/widget.hpp`), який перевизначено у значення `true` для модальних вікон/діалогів (`OptionBox`, `PopupList`, `ProgressBox`, `ErrorBox`, `HoldConfirmBox`, `HoldOkBox`, `KefirChangelogBox`). Модальні діалоги малюються після хрому, зберігаючи затемнення всього екрана через `gfx::dimBackground()`.
- У переглядачі файлів `fileview::Menu` (`sphaira/include/ui/menus/file_viewer.hpp`) змінено `WantsChrome()` на повернення `!m_fullscreen`, що дозволяє звичайному перегляду зображень/файлів використовувати стандартний хром `MenuBase`, при цьому повністю зберігаючи вимкнення хрому в повноекранному режимі.
- Обчислення меж зображення `ImageBounds()` у `sphaira/source/ui/menus/file_viewer.cpp` переведено на константи `layout::ContentBand()`.
- Перевірено збереження поведінки оклюзії бокових панелей `Sidebar`.
- Збірку WSL `ReleaseWithInstall` та перевірку `git diff --check` пройдено успішно. Версію застосунку піднято до `0.13.441`.

## v0.13.440 — інтерактивне керування чергою інсталяції (Skip / Cancel)

- Під час активного встановлення черги (`State::Installing`) змінено прив'язку кнопок: `B` тепер викликає діалог підтвердження пропуску лише поточного пакета (`Skip this package?`), а `X` — діалог підтвердження скасування всієї черги (`Cancel installation queue?`).
- Відхилення діалогу (`No`) або його закриття залишає встановлення активним без жодних переривань.
- Підтвердження пропуску пакета прив'язується до індексу пакета (`active_pkg`), активного в момент натискання `B`. При підтвердженні перевіряється, що сесія все ще встановлює саме цей пакет; якщо пакет устиг завершитися до відповіді, пропуск ігнорується і наступні пакети не перериваються. Сигнал `m_skip_requested` та `m_cancel_event` переривають yati-інсталяцію поточного пакета, записують результат у статистику як `Skipped` (без додавання до error list) та скидаються перед наступним пакетом.
- Підтвердження скасування черги викликає спільний `CancelSession()`, який зупиняє встановлення зі збереженням уже завершених пакетів і переводить сесію у стан `Cancelled`.
- Поведінку повністю уніфіковано між USB-чергою (`ThreadFunction`) та локальними файлами (`LocalThreadFunction`).
- Додано нові рядки локалізації для EN та UK (`en.json`, `uk.json`).
- Додано host unit-тест `tests/test_queue_outcome.cpp`. Всі тести `tests/run.sh`, збірка `ReleaseWithInstall` та `git diff --check` пройшли успішно. Версію застосунку піднято до `0.13.440`.

## v0.13.439 — миттєва NTP-синхронізація

- Перша фонова спроба NTP-синхронізації запускається одразу при старті Sphaira/Kefir Hub без початкової 10-секундної паузи.
- Повторне створення фонового потоку не відбувається: `ntp::Start()` при активному worker будить його через UEvent.
- Запис часу вимагає обов'язкового успіху для User system clock; успіх лише Network system clock не маскує помилку запису User clock. Тимчасовий прапор автоматичної корекції знімається для коректного збереження часу.
- Після реальної корекції часу UI-потік через `evman::push` запускає `__libnx_init_time()`, оновлюючи часову базу процесу libnx, завдяки чому `std::time()` та екранний годинник показують точний час без перезапуску застосунку.
- Локалізоване повідомлення `Clock synced` показується лише після фактичної зміни часу і не з'являється при точному часі, відсутності зв'язку чи помилці.
- Збірку WSL `ReleaseWithInstall` та `git diff --check` успішно пройдено. Версію піднято до `0.13.439`.

## v0.13.438 — перемикач USB 3.0

- У `Tools → Налаштування кефіру` додано `USB 3.0`, який керує
  `[usb] usb30_force_enabled` у Atmosphère `system_settings.ini`.
- Лише точне `u8!0x0` показується як Off; відсутній файл або ключ означає
  типовий On. Перемикання записує канонічне `u8!0x1` / `u8!0x0`.
- Після успішного запису SD синхронно commit-иться, а діалог пояснює, що зміна
  не набуде чинності без перезавантаження, і пропонує `Пізніше` або
  `Перезавантажити`.
- EN/UK JSON перевірено; WSL `ReleaseWithInstall` завершився успішно. Версію
  застосунку піднято до `0.13.438`.

## v0.13.436 — незалежний скрінсейвер

- Активний скрінсейвер більше не чекає mutex інсталятора: prompt і live snapshot
  читаються через `mutexTryLock`, а при зайнятому worker малюється останній
  готовий `SaverInfo`.
- R/W-графік семплується UI-потоком кожні 0,5 с з атомарних лічильників;
  нульовий приріст додає нульову точку, не зупиняючи рух або керування.
- Зміна яскравості одразу застосовується до підсвітки, але INI записується один
  раз лише після завершення install I/O; pending-значення переживає повторні
  Start/Stop.
- Після завершення черги графік замінюється на `Finished` або
  `Finished with errors` незалежно від маски полів скрінсейвера.
- У `Screen off` додано автозапуск після 30 с, 1/2/5/10 хв бездіяльності або
  `Off`. Таймер рахується лише під час `Installing` і скидається введенням,
  wake та prompt.
- Host-тести, dead-symbol guard і WSL `ReleaseWithInstall` пройшли; версію
  застосунку піднято до `0.13.436`.

## v0.13.437 — Text Viewer / Editor UX і стабілізація

- Відомі текстові файли відкриваються кнопкою `A` у View; контекстне меню має
  окремі `View` і `Edit`, причому Edit доступний лише для writable-файлів до
  4 MiB, а великі файли отримують обмежений read-only preview.
- Viewer використовує поточний `fs::Fs*`, коректно відхиляє Open/GetSize/Read
  та short-read помилки й не створює з них порожній editable документ.
- Edit має точний saved baseline, 32 кроки Undo/Redo, Insert/Delete/Join,
  затиснутий Go to line, Save/Discard/Cancel і recovery-save через sibling
  `.tmp.editor` / `.bak.editor` без закриття редактора після помилки.
- Правий стік незалежно й без wrap прокручує viewport; лівий стік/D-pad рухає
  курсор у Edit, а `A` відкриває Switch keyboard із повним поточним рядком.
- Footer розрізняє View/Edit controls, а writable-файл у View більше не
  позначається як read-only.
- INI отримав підсвічування та подвійний touch-toggle окремих `true`/`false` і
  Atmosphère `u8!0x0` / `u8!0x1` із підтримкою Undo. Російські переклади не
  змінювалися.
- Окремими комітами виправлено teardown активного transfer UI, визначення
  формату HB-іконок та очікування USBDS `Detached` перед виходом на HOS 22.5.
- `./tests/run.sh` завершився `all green`; WSL `ReleaseWithInstall` завершився
  успішно. Версію застосунку піднято до `0.13.437`; потрібна ручна перевірка на
  Switch.

## v0.13.435 — MTP Games і Create repack

- Read-only MTP-диск ігор має корінь `Merged` і `Separate`: перший віддає один
  об'єднаний NSP на гру, другий — окремі BASE/UPD/DLC у папці гри.
- Один потоковий builder читає NCA з їх фактичних NAND/SD/GameCard-сховищ,
  прибирає дублікати та формує назви на кшталт `[B+U65536+9DLC].nsp`.
- У `Games → Game Actions` додано `Create repack`: доступні BASE, найновіший UPD
  і всі DLC можна вибрати та записати одним NSP безпосередньо в `/games`.
- LayeredFS не показується, доки не реалізовано перебудову Program NCA, хешів і CNMT.
- Host-тести, JSON-переклади та послідовні WSL-збірки `sphaira`/`sphaira_nro`
  пройшли успішно; версію застосунку піднято до `0.13.435`.
- Вебсервер більше не показує порожню кнопку `Progress`; на DBI-екрані USB-статус
  розташовано над інструкцією, Applet Mode винесено в окремий читабельний блок, а
  між NAND/SD-смужками та значеннями додано відступ.

## v0.13.433 — запуск ROM через ядра TICO

- Додано 17 асоціацій для 13 установлених ядер TICO; відсутні ядра автоматично
  не показуються.
- У файловому менеджері та під час створення форвардера варіанти згруповано як
  `RetroArch — <ядро>` і `TICO — <ядро>`.
- Gambatte та Genesis Plus GX отримують системний аргумент перед шляхом до ROM;
  звичайний запуск, запуск з архіву та форвардер використовують спільну логіку.
- Host-тести й збірка `ReleaseWithInstall` у WSL пройшли успішно.
- Версію застосунку піднято до `0.13.433`.

## v0.13.431 — виправлення збірки File Viewer та відправка через NxLink

- Додано відсутні заголовні файли `swkbd.hpp` та `ui/popup_list.hpp` у `file_viewer.cpp`.
- Виправлено специфікатор форматування `%ld` для `s64` номера рядка при відмальовці списку.
- Успішно проведено компіляцію `[100%] Built target sphaira_nro` у WSL та відправлено `kefir-hub.nro` через `make nxlink` на Switch (`192.168.50.69`).
- Версію застосунку піднято до `0.13.431`.

## v0.13.430 — інтерактивний скрінсейвер

- Лівий аналоговий стік вільно пересуває блок скрінсейвера в межах екрана.
- Правий стік по вертикалі змінює яскравість, по горизонталі — швидкість
  автоматичного дрейфу.
- `Controller` отримав стани обох стіків; `App::Poll()` читає їх через
  `padGetStickPos`.
- Версію застосунку піднято до `0.13.430`.

## Стан перевірки

- [x] Зміни зібрані та зафіксовані в Git.
- [ ] На реальній Switch перевірити v0.13.452: запуск NRO з Homebrew Menu і повторний запуск після `envSetNextLoad()` без крашу або зависання.
- [ ] На реальній Switch перевірити v0.13.445: миттєве оновлення годинника Sphaira, результати NTP trace та збереження часу після перезапуску консолі.
- [ ] На реальній Switch перевірити USB 3.0 On → Off → Later і Off → On →
  Reboot, значення в `system_settings.ini` та фактичну швидкість після reboot.
- [ ] На реальній Switch перевірити `Merged`/`Separate`, копіювання NSP через MTP,
  створення репаку в `/games` для різних комбінацій BASE/UPD/DLC та встановлення результату.
- [ ] На реальній Switch перевірити запуск TICO напряму та через створений
  форвардер для звичайного ядра й ядра із системним аргументом.
- [ ] Перевірити на реальній Switch межі руху, керування яскравістю,
  регулювання дрейфу та пробудження кнопкою.
