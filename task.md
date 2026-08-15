# Активні задачі

Актуальний delivery — **v0.13.455**. Завершені задачі збережено в
[`archive/task_v0.13.249-v0.13.430.md`](archive/task_v0.13.249-v0.13.430.md)
та [`archive/task_archive.md`](archive/task_archive.md). Порядок виконання —
у [`plan.md`](plan.md), результат останнього delivery — у
[`walkthrough.md`](walkthrough.md).

## Поточний delivery: v0.13.455 (INI text viewer spacing)

- [x] `INI-VIEW-SPACING-455` — перед вимірюванням ключа INI у `fileview::Menu::DrawText` відновити 18 px, щоб значення починалося після фактично намальованого ключа, а не після вимірювання шрифтом номера рядка (16 px).
- [ ] `INI-VIEW-SPACING-VERIFY-455` — версію піднято до `0.13.455`, living docs і `git diff --check` оновлено, а WSL target `sphaira` пройшов; повний `ReleaseWithInstall` зупиняється на наявній Make-цілі `sphaira/sphaira.elf`, тому ще потрібні відновлення фінального NRO-пакування та короткий Switch smoke-test довгого ключа INI.

## Попередній delivery: v0.13.454 (NSP install diagnostics)

- [x] `NSP-DIAG-HOS-454` — для `MAKERESULT(Module_Libnx, LibnxError_IncompatSysVer)` у `ui::GetResultDescription` відобразити мінімальну версію 4.0.0 (через `version::FormatPacked`), фактичну версію консолі через `hats::getSystemFirmware()` та інструкцію оновити системну прошивку.
- [x] `NSP-DIAG-CORRUPT-454` — додати точні рекомендації повторного копіювання/завантаження для `Result_StreamUnexpectedEof` (передчасний кінець файлу/передачі) та `Result_NspBadMagic` (недійсний NSP, можливе пошкодження); не змінювати загальні FS чи крипто-помилки.
- [x] `NSP-DIAG-I18N-454` — додати локалізовані рядки для всіх 14 мовних файлів у `assets/romfs/i18n/*.json` і перевірити їх коректність за допомогою JSON-парсера.
- [x] `NSP-DIAG-VERIFY-454` — розширити `tests/test_version_compare.cpp` перевіркою пакування 4.0.0, пройти `tests/run.sh` та WSL `ReleaseWithInstall` збірку (`[100%] Built target sphaira_nro`).

## Попередній delivery: v0.13.454 (Homebrew multi-select actions)

- [x] `HOMEBREW-MULTISELECT-454` — у Homebrew додати вибір X, інверсію Y і
  скасування вибору B, використавши чинний спільний рендер позначок для List,
  Grid та HB Menu layouts.
- [x] `HOMEBREW-ACTIONS-454` — контекстне меню вибраних Homebrew показує
  безпечне підтверджене видалення та масові дії `Star` / `Unstar`; тимчасовий
  Kefir Updater stub не можна видаляти чи позначати зіркою.
- [x] `HOMEBREW-VERIFY-454` — підняти версію, оновити living docs, пройти
  `tests/run.sh`, WSL `ReleaseWithInstall` і `git diff --check`; лишити
  Switch smoke-test для X/Y, усіх layouts та масових дій.

## Попередній delivery: v0.13.453 (PFS0/NSP parser hardening)

- [x] `PFS0-EXACT-453` — у спільному `yati::container::Nsp` відхиляти short
  PFS0 header/file-table/string-table read до парсингу.
- [x] `PFS0-BOUNDS-453` — до allocation, pointer arithmetic або seek перевіряти
  parser limits, table/string/data/file arithmetic, name offsets/NUL і відомі
  file/container bounds; не дублювати guards у callers.
- [x] `PFS0-NEGATIVE-453` — додати найменшу host-negative boundary coverage для
  malformed PFS0/NSP metadata без MSP, UI, transport або i18n змін.
- [x] `PFS0-VERIFY-453` — Gemini підтвердив 41 focused check, `tests/run.sh`
  (`all green`, 731/731 dead symbols), WSL `ReleaseWithInstall`
  (`[100%] Built target sphaira_nro`) і `git diff --check`; версію піднято до
  `0.13.453`. Hardware check не потрібний: прийнятий diff не торкається runtime UI/transport.

Доказовий baseline-аудит: [`pfs0_nsp_hardening_audit.md`](pfs0_nsp_hardening_audit.md).

## Попередній delivery: v0.13.452 (відновлення loader thread affinity)

- [x] `HBL-AFFINITY-452` — безпосередньо перед кожним `nroEntrypointTrampoline()` отримати фактичну core mask поточного процесу через `svcGetInfo(..., InfoType_CoreMask, CUR_PROCESS_HANDLE, 0)` та відновити її для `CUR_THREAD_HANDLE` через `svcSetThreadCoreMask(..., -1, core_mask)`.
- [x] `HBL-AFFINITY-ERROR-452` — при помилці читання або встановлення affinity завершуватися через чинний `diagAbortWithResult`; не задавати mask `0..3` за `highest_cpu_id` і не змінювати NPDM, UI чи i18n.
- [x] `HBL-AFFINITY-VERIFY-452` — Gemini WSL `ReleaseWithInstall` завершився успішно (`[100%] Built target sphaira_nro`), `git diff --check` пройшов; версію піднято до `0.13.452`.
- [ ] `HW-HBL-AFFINITY-452` — на реальній Switch перевірити початковий і повторний запуск NRO, зокрема повернення через `envSetNextLoad()`, без крашу або зависання.

## Попередній delivery: v0.13.451 (custom NRO search paths)

- [x] `NRO-SEARCH-PATHS-451` — зберігати лише нормалізовані додаткові абсолютні SD-каталоги в `[homebrew_paths]`; `/switch` лишається незмінним default root і не записується.
- [x] `NRO-SEARCH-PATHS-UI-451` — у File Browser для вибраного SD-каталогу (крім `/` і `/switch`) показати add/remove, підтвердити remove, а після успішної зміни надіслати `homebrew::SignalChange()`.
- [x] `NRO-SEARCH-PATHS-SCAN-451` — сканувати `/switch` без зміни чинної семантики й кожен custom root на фіксовану глибину 2; не додавати дублікати NRO за канонічним шляхом.
- [x] `NRO-SEARCH-PATHS-VERIFY-451` — додати найменші host-тести нормалізації/валідації, пройти `tests/run.sh`, WSL `ReleaseWithInstall` і `git diff --check`.
- [x] `VERSION-DOC-COMMIT-451` — піднято версію до `0.13.451`, оновлено living docs і створено сфокусований коміт після senior review.

## Попередній delivery: v0.13.449 (NFS phase 1 — read-only source)

- [x] `NFS-LIBNFS-449` — додати pinned static dependency `ITotalJustice/libnfs@65f3e11` без документації, прикладів і dependency tests.
- [x] `NFS-READONLY-449` — реалізувати NFS devoptab через `MountNetworkDevice2()` з RAII cleanup, `nfs_parse_url_dir()`, серіалізованими операціями та жорсткою read-only семантикою.
- [x] `NFS-URL-449` — додати host-testable validator канонічного `nfs://host[:port]/export`, захист від credentials, traversal, encoded separators і переповнення `FsPath`; 194 NFS checks пройдено.
- [x] `NFS-UI-449` — інтегрувати NFS у File Browser, source picker і Settings, зберегти `FsEntryFlag_ReadOnly` на всіх маршрутах та відсікати невалідні entries до створення `FsEntry`.
- [x] `NFS-VERIFY-449` — успішно пройти `tests/run.sh`, dead-symbol guard, WSL `ReleaseWithInstall` і `git diff --check`.
- [x] `VERSION-DOC-COMMIT-449` — підняти версію до `0.13.449`, оновити living docs і створити сфокусований коміт після senior review.
- [ ] `HW-NFS-449` — на реальній Switch перевірити browse/open/read/copy-from-NFS для nested export і відсутність write actions.

## Попередній delivery: v0.13.448 (очищення NTP-сповіщень)

- [x] `NTP-NOTIF-CLEANUP-448` — вимкнути тимчасові екранні підказки діагностичного трейсу NTP; `ReportSyncStage()` зберігає запис у `[NTP]` лог без виклику `App::Notify`.
- [x] `NTP-TOAST-FILTER-448` — прибрати нелокалізоване сповіщення `NTP: UI clock refreshed`; єдиним екранним повідомленням залишається локалізоване `Clock synced`, яке ставиться в чергу виключно після успішного live-запису User Clock та оновлення libnx time.
- [x] `NTP-NO-NOTIF-PATHS-448` — гарантувати відсутність сповіщень для шляхів без зміни User Clock (коли час уже точний або коли увімкнено лише системну automatic correction із процесним offset).
- [x] `VERSION-DOC-COMMIT-448` — підняти версію до `0.13.448`, оновити living docs (`task.md`, `plan.md`, `walkthrough.md`).

Наступний незалежний крок після цього delivery: default icon для iconless NRO forwarder.

## Попередній delivery: v0.13.447 (upstream-equivalence hardening)

- [x] `UPSTREAM-AUDIT-DOC-447` — зберегти живий аудит змін після upstream `1.0.2` у [`upstream_audit.md`](upstream_audit.md).
- [x] `ZIP-PATH-SAFETY-447` — у спільному `thread::TransferUnzipAll()` до створення будь-якого output path відхиляти абсолютні, traversal, керуючі, обрізані та наддовгі archive entry names; не допустити overflow сумарного uncompressed size та entry count.
- [x] `ZIP-PATH-CHECK-447` — додати host unit-тести для `path::IsSafeArchiveEntry()`, успішно пройти `tests/run.sh`, WSL `ReleaseWithInstall` та `git diff --check`.
- [x] `VERSION-DOC-COMMIT-447` — підняти версію до `0.13.447`, оновити living docs і створити сфокусований коміт після senior review.

Наступний незалежний крок після цього delivery: default icon для iconless NRO forwarder.

## Поточний delivery: v0.13.446 (NTP через системну automatic correction)

- [x] `NTP-AUTOCORRECT-446` — після NTP-запису Network Clock увімкнути `UserSystemClockAutomaticCorrectionEnabled` через `set:sys`, а не вимикати його; це єдиний штатний шлях HOS для перенесення Network Clock у User Clock.
- [x] `NTP-AUTOCORRECT-TRACE-446` — tooltip/log однозначно вказує ввімкнення automatic correction та те, що якщо воно було вимкнене на старті HOS, системний User Clock застосує його після reboot; повторно зчитує User Clock після оновлення Network Clock для виявлення негайного live-синхрону.
- [x] `NTP-AUTOCORRECT-VERIFY-446` — успішно виконано WSL `ReleaseWithInstall`, `git diff --check`, піднято версію до `0.13.446`.
- [ ] `HW-NTP-AUTOCORRECT-446` — перевірити на Switch негайний результат за вже ввімкненої опції automatic correction та збереження часу після reboot, коли опція була вимкнена на старті.

## Попередній delivery: v0.13.445 (NTP User Clock через set:sys)

- [x] `NTP-SETSYS-445` — після відмови запису live User Clock через `time:su` та `time:s` (як-от з `time::ResultNoCapability` / `0x00000274`) виконувати fallback та записувати `UserSystemClockContext` через `set:sys`, обчислений від поточного standard steady clock і NTP timestamp.
- [x] `NTP-SETSYS-RUNTIME-445` — одразу показувати коригований час у Sphaira через процесний offset; запис у `errors.txt` виконувати лише при відмові `set:sys`, щоб успішний fallback не залишав хибних записів про помилки.
- [x] `NTP-SETSYS-TRACE-445` — залишити ввімкнені diagnostic tooltip-и й додати до них усі `set:sys` етапи та точні Result.
- [x] `NTP-SETSYS-VERIFY-445` — виконати WSL `ReleaseWithInstall`, `git diff --check` та підняти версію до `0.13.445`.
- [ ] `HW-NTP-SETSYS-445` — перевірити на реальній Switch: миттєве оновлення годинника Sphaira, результати NTP trace та збереження часу після перезапуску консолі.

## Попередній delivery: v0.13.444 (видимий NTP diagnostic trace)

- [x] `NTP-TRACE-444` — на лівій стороні екрана тимчасово показуються tooltip-и кожного етапу: мережа, DNS, UDP, відповідь NTP, offset, усі операції `time:su`/`time:s`, UI refresh та підсумок.
- [x] `NTP-TRACE-LOG-444` — кожен видимий етап дублюється як `[NTP] ...` у `log.txt`; відмова містить точний Horizon Result.
- [x] `NTP-TRACE-VERIFY-444` — успішно виконано WSL `ReleaseWithInstall` (`[100%] Built target sphaira_nro`), перевірено `git diff --check`, піднято версію до `0.13.444`.

## Попередній delivery: v0.13.443 (запис NTP-часу через `time:su`)

- [x] `NTP-TIME-SU-443` — `SetSystemTime()` спершу виконує повний запис через `time:su`; `time:s` лишається fallback лише після невдачі запису User system clock.
- [x] `NTP-TIME-LOG-443` — якщо обидва сервіси відмовляють, `errors.txt` містить окремі Result для `time:su` та `time:s`.
- [x] `NTP-TIME-VERIFY-443` — успішно виконано WSL `ReleaseWithInstall` (`[100%] Built target sphaira_nro`), перевірено `git diff --check`, піднято версію до `0.13.443`.

## Попередній delivery: v0.13.442 (усунення крашу File Browser при завантаженні асоціацій)

- [x] `ASSOC-CRASH-442` — виправлено краш File Browser при завантаженні асоціацій (причина: реалокація `std::vector<FileAssocEntry>` під час `emplace_back` копіювала великі об'єкти `FileAssocEntry` з `fs::FsPath`).
- [x] `ASSOC-RESERVE-442` — додано `CountAssocEntriesPath` та попереднє резервування ємності вектора `m_assoc_entries.reserve()` на сумарну кількість кандидатних `.ini` файлів із `romfs:/assoc/` та `paths::ASSOC` перед завантаженням.
- [x] `ASSOC-VERIFY-442` — успішно виконано збірку WSL `ReleaseWithInstall` (`[100%] Built target sphaira_nro`), перевірено `git diff --check`, піднято версію до `0.13.442`.

## Попередній delivery: v0.13.441 (захист звичайного хрому UI Sphaira)

- [x] `CHROME-ORDER-441` — змінено порядок малювання в `App::Draw()` (`sphaira/source/app.cpp`), щоб стандартний хром заголовка та футера малювався після немодальних віджетів і контенту, гарантуючи, що елементи контенту не перекривають розділювачі та статус-бар.
- [x] `CHROME-MODAL-441` — додано властивість `IsModal()` до класу `Widget` та перевизначено її для модальних вікон (`OptionBox`, `PopupList`, `ProgressBox`, `ErrorBox`, `HoldConfirmBox`, `HoldOkBox`, `KefirChangelogBox`), зберегши їх малювання та затемнення поверху хрому.
- [x] `CHROME-VIEWER-441` — оновлено `fileview::Menu::WantsChrome()` для повернення `!m_fullscreen`, що забезпечує використання стандартного хрому у звичайному режимі перегляду файлів/зображень та відмову від нього в повноекранному режимі.
- [x] `CHROME-LAYOUT-441` — виправлено обчислення `ImageBounds()` у `file_viewer.cpp` з використанням констант `layout::ContentBand()`.
- [x] `CHROME-VERIFY-441` — успішно виконано збірку WSL `ReleaseWithInstall` та `git diff --check`, піднято версію до `0.13.441`.

## Попередній delivery: v0.13.440 (інтерактивне керування чергою інсталяції)

- [x] `QUEUE-SKIP-440` — кнопка `B` під час активної інсталяції запитує підтвердження, перериває поточний пакет через `m_skip_requested` / `m_cancel_event` і скидає прапор для продовження черги.
- [x] `QUEUE-CANCEL-440` — кнопка `X` під час активної інсталяції запитує підтвердження та перериває весь сеанс через спільний `CancelSession()` зі збереженням завершених встановлень.
- [x] `QUEUE-STATS-440` — пропущений користувачем пакет враховується як `Skipped` у статистиці та підсумку без створення запису помилки та без скасування всієї сесії.
- [x] `QUEUE-UNIFY-440` — уніфіковано поведінку для обох потоків черги: USB (`ThreadFunction`) та локальних файлів (`LocalThreadFunction`).
- [x] `QUEUE-I18N-440` — додано EN та UK локалізації для нових дій та діалогів підтвердження (`Skip package`, `Skip this package?`, `Cancel installation queue?`).
- [x] `QUEUE-VERIFY-440` — додано host-тест `test_queue_outcome.cpp`, успішно виконано `tests/run.sh`, WSL `ReleaseWithInstall` та `git diff --check`.
- [x] `QUEUE-DELIVERY-440` — оновлено `README.md`, піднято версію до `0.13.440`, оновлено living docs та створено focused commit.

## Попередній delivery: v0.13.439 (миттєва NTP-синхронізація)

- [x] `NTP-IMMEDIATE-439` — першу фонову NTP-спробу запускати одразу після
  входу в Sphaira/Kefir Hub; повторне ввімкнення опції має будити чинний worker.
- [x] `NTP-SET-439` — коригувати user clock незалежно від системного перемикача
  автоматичної синхронізації та не звітувати про успіх, якщо user clock не
  записано.
- [x] `NTP-RUNTIME-439` — після реальної корекції оновити часову базу поточного
  процесу на UI-потоці, щоб годинник і `std::time()` не чекали перезапуску.
- [x] `NTP-TOAST-439` — показати локалізований `Clock synced` лише коли час
  справді було змінено; для вже точного часу нічого не показувати.
- [x] `NTP-DELIVERY-439` — пройти WSL `ReleaseWithInstall`, підняти версію до
  `0.13.439`, оновити living docs і створити focused commit після senior-review.

## Попередній delivery: v0.13.438 (перемикач USB 3.0)

- [x] `USB3-SETTING-438` — додати в `Tools → Налаштування кефіру` перемикач
  `[usb] usb30_force_enabled`; відсутній ключ вважати увімкненим за
  замовчуванням.
- [x] `USB3-PERSIST-438` — записувати точне `u8!0x1` / `u8!0x0` і синхронно
  commit-ити SD до повідомлення про успіх.
- [x] `USB3-REBOOT-438` — після запису пояснювати, що без перезавантаження
  нічого не зміниться, та пропонувати `Пізніше` або `Перезавантажити`.
- [x] `USB3-DELIVERY-438` — перевірити JSON, зібрати WSL
  `ReleaseWithInstall` і створити focused commit.

## Попередній delivery: v0.13.436 (незалежний скрінсейвер)

- [x] `SAVER-NONBLOCK-436` — прибрати синхронний запис INI та очікування
  `m_mutex` з активного UI-шляху скрінсейвера; рух, яскравість і кадри мають
  залишатися чутливими під час блокуючого запису на SD.
- [x] `SAVER-GRAPH-436` — семплувати R/W-графік кожні 0,5 с без mutex
  інсталятора; за відсутності приросту додавати нульовий семпл, не зупиняючи
  прокрутку графіка.
- [x] `SAVER-FINISHED-436` — після завершення черги ховати графік і показувати
  `Finished` або `Finished with errors` незалежно від маски полів.
- [x] `SAVER-AUTO-436` — додати в налаштування скрінсейвера автозапуск під час
  інсталяції після вибраного періоду бездіяльності; будь-яке введення скидає
  таймер, `Off` лишається типовим значенням.
- [x] `SAVER-PERSIST-436` — зміну яскравості тримати в пам'яті під час запису й
  зберігати один раз лише після завершення/cancel інсталяційного I/O.
- [x] `SAVER-VERIFY-436` — додати одну мінімальну host-перевірку таймера,
  прогнати `tests/run.sh` і WSL `ReleaseWithInstall`.
- [x] `SAVER-DELIVERY-436` — після прийняття підняти версію, оновити living
  docs і створити focused commit без сторонніх змін delivery 0.13.435.

## Поточний delivery: v0.13.437 (Text Viewer / Editor UX)

- [x] `TEXT-OPEN-A-437` — відкривати відомі текстові формати кнопкою `A` у
  read-only viewer до пошуку зовнішніх асоціацій.
- [x] `TEXT-CONTEXT-437` — показувати для текстового файла окремі дії `View` і
  `Edit`, не обмежуючи viewer старим порогом 64 KiB.
- [x] `TEXT-FS-437` — передавати text viewer поточний `fs::Fs*`, дозволити
  перегляд на всіх підтримуваних джерелах і запис лише на реально writable FS;
  image-viewer залишити на чинному незалежному шляху.
- [x] `TEXT-IO-437` — не створювати порожній документ після помилки
  Open/GetSize/Read; показувати фактичний Result і не дозволяти редагування.
- [x] `TEXT-SAVED-STATE-437` — визначати `*` порівнянням із останнім успішно
  збереженим текстом, щоб Undo міг повернути чистий стан, а Save оновлював
  baseline і передбачувано завершував історію.
- [x] `TEXT-SAFE-SAVE-437` — записувати через sibling temp із перевіреним
  rename/restore; після невдалого Save залишати editor відкритим, dirty і з
  усіма змінами в пам'яті.
- [x] `TEXT-GOTO-INSERT-437` — після Go to line одразу показувати затиснутий до
  діапазону рядок; Insert створює рядок тільки після підтвердження keyboard.
- [x] `TEXT-CONTROLS-437` — у View прокручувати текст правим стіком; у Edit
  рухати курсор лівим стіком, правим незалежно прокручувати viewport, а `A`
  відкривати Switch keyboard із повним поточним рядком.
- [x] `INI-UX-437` — підсвічувати секції, коментарі, ключі та значення INI;
  подвійним дотиком до boolean-рядка перемикати `true` / `false` з undo.
- [x] `TEXT-VERIFY-437` — додати мінімальну host-перевірку розпізнавання тексту
  й INI boolean toggle, прогнати host-тести та WSL `ReleaseWithInstall`.
- [x] `TEXT-LEGEND-437` — показати в footer керування для View/Edit: стіки,
  редагування рядка, Actions, Options і Back без перевантаження легенди.
- [x] `TEXT-VIEW-LABEL-437` — не називати writable-файл read-only лише через
  режим View; справжній read-only показувати тільки для protected/source FS.
- [x] `INI-TYPED-FLAG-437` — подвійним tap перемикати також Atmosphère-прапорці
  `u8!0x0` / `u8!0x1` із підтримкою Undo та host-тестом.
- [x] `INI-CONTRAST-437` — малювати назви INI-секцій контрастним текстовим
  кольором, а не темним `focus` чорної теми.
- [x] `STABILITY-TRANSFER-437` — звільняти активний transfer progress box до
  очищення UI/NanoVG, щоб вихід із Home не лишав відкладені image destruction.
- [x] `STABILITY-ICONS-437` — ініціалізувати розміри stb image load і визначати
  формат HB-іконки за даними, а не примусово як JPEG.
- [x] `STABILITY-USB-437` — перед `usbDsExit()` вимкнути USBDS і дочекатися
  `Detached`, що прибирає teardown race на HOS 22.5.
- [x] `TEXT-DELIVERY-437` — після прийняття підняти версію, оновити living docs
  і створити focused commit без сторонніх змін delivery 0.13.435.

## Попередній delivery: v0.13.435 (MTP Games і Create repack)

- [x] `NSP-MERGED-CORE-435` — додати спільний потоковий NSP-builder, який
  об'єднує встановлені BASE, UPD і DLC та читає NCA з їхніх фактичних
  NAND/SD/GameCard storage.
- [x] `NSP-MERGED-NAME-435` — називати пакет
  `Назва [TitleID][B+U<version>+<count>DLC].nsp`, опускаючи відсутні частини.
- [x] `MTP-GAMES-LAYOUT-435` — змінити read-only диск на корінь `Merged` і
  `Separate`; у `Separate/<Game [TitleID]>` залишити окремі BASE/UPD/DLC NSP.
- [x] `HOST-VERIFY-435` — додати мінімальні host-тести для імені merged NSP і
  MTP path routing та прогнати `tests/run.sh`.
- [x] `BUILD-VERIFY-435` — послідовно зібрати `sphaira` і `sphaira_nro` у
  чистому WSL verification-каталозі.
- [x] `CMAKELISTS-VERSION-BUMP-435` — підняти версію до `0.13.435` після
  прийняття реалізації.
- [x] `REPACK-SELECT-435` — додати `Create repack` з вибором лише наявних BASE, UPD і DLC.
- [x] `REPACK-BUILD-435` — записувати вибрані незмінені компоненти одним потоковим NSP у `/games`.
- [x] `REPACK-VERIFY-435` — прогнати host-тести та послідовно зібрати `sphaira` і `sphaira_nro`.
- [x] `UI-COSMETICS-435` — прибрати порожній Progress з вебсервера, розвести USB/Applet Mode на DBI-екрані та збільшити відступ перед NAND/SD-розмірами; host-тести й WSL-збірка пройшли.
- [ ] `REPACK-LAYERFS-LATER` — додати LayeredFS лише разом із коректною перебудовою Program NCA,
  хешів і CNMT; не показувати недієву опцію в поточному UI.
- [ ] `HW-MTPGAMES-435` — на Switch перевірити лістинг обох каталогів і
  копіювання merged та separate NSP на ПК.
- [ ] `HW-REPACK-435` — на Switch створити репак з різними комбінаціями BASE/UPD/DLC,
  перевірити файл у `/games` та його встановлення.

## Паралельний запит: TICO launchers

- [x] `TICO-ASSOC` — додати асоціації лише для фактично встановлених
  `/tico/cores/tico-*.nro`, включно з каталогами `sega-cd`, `fbneo`, `naomi`
  та `atomiswave`.
- [x] `TICO-ARGS` — передавати системний slug для Gambatte і Genesis Plus GX
  під час звичайного запуску та у створеному форвардері; іншим ядрам передавати
  ROM першим аргументом.
- [x] `TICO-CHOOSER` — в обох списках вибору показувати й групувати варіанти як
  `RetroArch — <core>` та `TICO — <core>`.
- [x] `TICO-VERIFY` — прогнати host-тести й WSL-збірку.
- [ ] `HW-TICO-433` — на реальній Switch перевірити прямий запуск і форвардер
  для звичайного ядра та ядра із системним аргументом.

## Попередній delivery: v0.13.431 (File Viewer Build Fix & NxLink Deployment)

- [x] BUILD-FIX-FILE-VIEWER-431 — Додано пропущені заголовні файли `swkbd.hpp` та `ui/popup_list.hpp` у `file_viewer.cpp`, виправлено специфікатор форматування `%ld` для `s64` номера рядка.
- [x] CMAKELISTS-VERSION-BUMP-431 — Піднято версію в `sphaira/CMakeLists.txt` до `0.13.431`.
- [x] VERIFY-NXLINK-SEND-431 — Успішно зібрано `[100%] Built target sphaira_nro` у WSL та відправлено `kefir-hub.nro` (6,081,813 байт) через `make nxlink` на Switch (192.168.50.69).

## Поточні перевірки

- [ ] `HW-NTP-SETSYS-445` — миттєве оновлення годинника Sphaira, результати NTP trace та збереження часу після перезапуску консолі.
- [ ] `HW-SMOKE-430` — обидва стіки у скрінсейвері, межі руху, яскравість,
  швидкість дрейфу та пробудження.
- [ ] `HW-SMOKE-429` — межі storage-блоку, R/W-графік і preview скрінсейвера.
- [ ] `HIST-USB-COMPAT` — DBI backend, Awoo/TinFoil і GoldLeaf v0.10+ на
  реальній Switch.
- [ ] `HW-SMOKE-MTP` — лістинг MTP, reconnect і встановлення NSP з телефона.
- [ ] `HIST-WEB-APPLET` — перевірити Wi-Fi client isolation на реальній
  Switch і точці доступу.

## Черга реалізації

- [ ] `HIST-55` — DBI: один динамічний рядок журналу на пакет.
- [ ] `HIST-56` — DBI: сегментовані NAND/SD-смуги та наочний `ReviewQueue`.
- [ ] `HIST-61B` — Games: dump/verify/read-only mount і безпечні component actions.
- [ ] `HIST-61C` — Games: save integration, ticket details і перевірений DLC unlocker.
- [x] `HIST-NFS-449` — NFS як read-only network source реалізовано у `v0.13.449`.
- [ ] `HIST-SFTP` — SFTP як окреме network source.
- [~] `HIST-PLAYER` — вбудований player; заморожено до окремого погодження.

## Правило закриття

`[x]` означає пройдену перевірку. Успішна компіляція не закриває hardware-gate
без записаного результату в `tests.md`.
