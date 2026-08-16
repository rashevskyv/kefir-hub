# Поточний walkthrough

Актуальний delivery — **v0.13.467** (2026-08-16). Попередні
walkthrough збережено в
[`archive/walkthrough_v0.13.357-v0.13.430.md`](archive/walkthrough_v0.13.357-v0.13.430.md)
та [`archive/walkthrough_archive.md`](archive/walkthrough_archive.md).

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
