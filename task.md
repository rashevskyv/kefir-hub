# Активні задачі

Порядок відповідає `plan.md`. Завершені рядки переносяться в архів, а не
видаляються без сліду.

## Поточний delivery: v0.13.284

- [x] FIX-MTP-INSTALL-DEADLOCK-CANCEL-284 — усунення взаємного блокування (deadlock) при скасуванні встановлення (натискання кнопки B або Stop). Додано override `SignalCancel()` у `Stream`, який відразу викликає `Disable()`, що пробуджує потік читання `t_read`, дозволяючи всім потокам коректно завершитися.
- [x] FIX-MTP-INSTALL-VERBOSE-LOGGING-284 — додано детальне логування при переході в очікування на condvar-змінних у `Stream::Push`, `Stream::ReadChunk`, чергах `ThreadData` та при нульових читаннях `ThreadData::Read`, для діагностики точного місця зависання.
- [ ] BUILD-NRO-284 — збірка 0.13.284 та верифікація компіляції (WSL).
- [ ] HW-SMOKE-284 — на Switch+ПК: перевірка скасування встановлення кнопкою B та аналіз логів при можливому повторному зависанні.

## Попередній delivery: v0.13.283

- [x] FIX-MTP-INSTALL-HANG-3PERCENT-283 — усунення зависання встановлення на 3% (шляхом заміни умов `if` на цикли `while` при очікуванні на умовних змінних `condvarWait`). Це запобігає передчасній обробці порожніх/невалідних буферів через spurious wakeups та сигнали wake-all, що виникали при паралельному завершенні попередніх потоків.
- [ ] BUILD-NRO-283 — збірка 0.13.283 та верифікація компіляції (WSL).
- [ ] HW-SMOKE-283 — на Switch+ПК: повторна перевірка інсталяції репаків (1-2-Switch) через USB MTP без зависань на 3% та 4%.

## Попередній delivery: v0.13.282

- [x] FIX-MTP-INSTALL-HANG-4PERCENT-282 — виправлення зависання встановлення на 4% (або наприкінці першого файлу в репаках). Метод `SetReadResult` та `SetDecompressResult` тепер завжди викликають `WakeAllThreads()`, що дозволяє розблокувати downstream-потоки при успішному EOF на потоці читання та дає їм можливість коректно завершити свою роботу.
- [x] MTP-INSTALL-BLOCK-INPUTS-282 — блокування фонових кнопок при активному вікні встановлення. Якщо прогрес-бокс не мінімізований, оновлення активного віджета блокується, залишаючи активними тільки кнопку L3 (згорнути), B (скасувати) та натискання по екрану на кнопку "Stop". При згортанні (L3) всі кнопки знову розблоковуються.
- [x] MTP-INSTALL-STOP-BUTTON-282 — додано відображення та роботу кнопки "Stop" (скасування) на повноекранному прогрес-боксі під час встановлення. Для цього висота віджета `ProgressBox` при `m_detached == true` динамічно збільшена до 360px з перепозиціонуванням елементів.
- [x] MTP-INSTALL-MINI-BADGE-SCALE-282 — динамічне розрахування ширини мінімізованого віджета `DrawMiniBadge` за допомогою вимірювання текстових границь (`gfx::textBounds`), щоб довгі назви файлів з відсотками та швидкістю гарантовано влізали у вікно без виходу за його межі.
- [ ] BUILD-NRO-282 — збірка 0.13.282 та верифікація компіляції (WSL).
- [ ] HW-SMOKE-282 — на Switch+ПК: перевірка встановлення з блокуванням/розблокуванням кнопок, роботою кнопки скасування та динамічним масштабуванням міні-віджета.

## Попередній delivery: v0.13.281

- [x] FIX-MTP-INSTALL-UNEXPECTED-EOF-281 — виправлення помилки передчасного завершення встановлення (Result_StreamUnexpectedEof) для репаків. У `Stream::Read` дозволено успішне завершення читання з меншою кількістю байтів при досягненні EOF, а у `ThreadData::Read` вилучено перевірку на точну відповідність розміру читання, що дозволяє коректно ігнорувати відсутність необов'язкових нульових байтів/паддінгу в кінці перепакованих NSP.
- [ ] BUILD-NRO-281 — збірка 0.13.281 та верифікація компіляції (WSL).
- [ ] HW-SMOKE-281 — на Switch+ПК: повторна перевірка інсталяції репаків (наприклад, 1-2-Switch [UKR+REPACK]) через USB MTP.

## Попередній delivery: v0.13.280

- [x] FIX-MTP-INSTALL-EOF-280 — виправлення помилки передчасного скасування встановлення (Result_TransferCancelled) при досягненні EOF у Stream::ReadChunk. Успішний EOF (коли передача закінчена і буфер порожній) тепер повертає 0 байтів з кодом успіху замість викидання помилки скасування.
- [ ] BUILD-NRO-280 — збірка 0.13.280 та верифікація компіляції (WSL).
- [ ] HW-SMOKE-280 — на Switch+ПК: перевірка повної та успішної інсталяції репаків та великих ігор через MTP.

## Попередній delivery: v0.13.279

- [x] MANUAL-MTP-TOGGLE-279 — заміна «Runtime Mode» на опцію «Mount MTP» у бічному меню «Install & Share» розділу інструментів (Tools). Опція відображає поточний стан MTP (MTP: Active або Mount MTP) та дозволяє динамічно перемикати та оновлювати назву на ходу в самому меню при натисканні.
- [x] BOLD-SIDEBAR-LABELS-279 — малювання всіх заголовків (лейблів) бічного меню жирним шрифтом (через овер-драв), у тому числі назв та статусів опцій MTP.
- [ ] BUILD-NRO-279 — збірка 0.13.279 та верифікація компіляції (WSL).
- [ ] HW-SMOKE-279 — на Switch+ПК: ручна перевірка динамічного перемикання та жирних лейблів через розділ інструментів (Tools -> Plus (+) / Start -> Mount MTP / MTP: Active).

## Попередній delivery: v0.13.278

- [x] AUTOSTART-MTP-CONFLICT-278 — усунення конфлікту між USB HDD (usbHsFs) та MTP (libhaze)/USB-інсталяцією. При запуску MTP/USB-інсталяції фоновий сервіс HDD тимчасово деініціалізується, а при виході — перезапускається. Ініціалізацію MTP перенесено у першу ітерацію циклу App::Loop, а у конструкторі App вимкнено запуск HDD, якщо увімкнено MTP.
- [ ] BUILD-NRO-278 — збірка 0.13.278 та верифікація компіляції (WSL).
- [ ] HW-SMOKE-278 — на Switch+ПК: перевірка автозапуску MTP при підключенні кабелю, роботи монтування папки по MTP у файловому менеджері та з'єднання в DBI/USB-меню з увімкненим HDD.

## Попередній delivery: v0.13.277

- [x] MOUNT-FOLDER-MTP-277 — у файловому менеджері дія «Змонтувати по MTP» віддає поточну локацію на ПК: звичайну папку SD (FsNativeSd + base_path у FsProxy), а також ВІРТУАЛЬНІ монтування (вміст компонента `FsNcm`, архів `FsZip`) — бо haze обгортає `fs::Fs`. У haze додано «pinned» сторедж (`MountFs`/`UnmountPinned`/`HasPinned`), base_path у `FsProxyBase::FixPath`, single-thread для virtual, перезапуск MTP. Дія «Демонтувати MTP» зупиняє віддачу. FTP-віддачу віртуального відкладено (ftpsrv працює через stdio-дерево, не бачить наші fs).
- [x] BUILD-NRO-277 — збірка 0.13.277 та верифікація компіляції.
- [ ] HW-SMOKE-277 — на Switch+ПК: «Змонтувати по MTP» з папки SD → видно на ПК і читається/пишеться; те саме для компонента (Game Details → Open in file browser → Mount over MTP) і для .zip; «Демонтувати MTP».

## Попередній delivery: v0.13.276

- [x] CONTENT-OPEN-IN-BROWSER-276 — у Game Details дія «Показати файли» замінена на «Відкрити у файловому браузері»: компонент (Application/Update/AddOn) монтується як read-only virtual fs `fs::FsNcm` (перелік NCA через ncm-запит як у BuildContentEntry, читання через `ncmContentStorageReadContentIdFile`, тримає `title::Init/Exit` для життя NCM-стореджів). Додано `FsType::Content` + поля identity у `FsEntry`, ctor `Menu(flags, entry, path)` (запуск браузера на конкретному mount через делегування + `view->SetFs`). Copy з такого mount переживає навігацію (owned FsNcm у SelectedStash).
- [x] INFO-DIALOG-1BTN-fix — (перенесено з 275, уточнення) інфо-діалоги — одна кнопка.
- [x] BUILD-NRO-276 — збірка 0.13.276 та верифікація компіляції.
- [ ] HW-SMOKE-276 — на Switch: A на компоненті Content → «Відкрити у файловому браузері» → лістинг NCA, copy NCA і paste в SD; «R» на NCA (read-only); вихід із mount.

### Наступні
- [ ] MOUNT-FOLDER-FTP — віддача звичайної папки SD по FTP (увімкнути FTP; папка вже доступна за шляхом). Віддача віртуальних монтувань по FTP — велика окрема робота (переписати ftpsrv на `fs::Fs`).

## Попередній delivery: v0.13.275

- [x] ARCHIVE-PASTE-FIX-275 — виправлено «скопіював із zip, а вставити не можу»: буфер копіювання (`SelectedStash`) тепер ВОЛОДІЄ власною read-only fs (свіжий `FsZip`), тож копія переживає вихід з архіву; `SetFs` не чистить такий буфер; paste читає з owned fs. Copy в архіві → B → куди завгодно → Paste працює. Paste лишається заблокованим для read-only призначення.
- [x] READONLY-MARKER-275 — read-only записи у файловому менеджері позначаються червоним чіпом «R» на кутику іконки (весь вміст архіву, захищені системні шляхи); записи для запису — без позначки.
- [x] INFO-DIALOG-1BTN-275 — інфо-діалоги Save info / Content info / Ticket info більше не мають двох однакових кнопок (Back+OK) — тепер одна кнопка OK.
- [x] BUILD-NRO-275 — збірка 0.13.275 та верифікація компіляції.
- [ ] HW-SMOKE-275 — на Switch: copy з zip → вихід → paste в інше місце; «R» на read-only записах; одна кнопка в інфо-діалогах.

## Попередній delivery: v0.13.274

- [x] GAMEDETAIL-CONTENT-ROWS-274 — рядки вкладки Content перероблено в стилі DBI: бейдж місця `[SD]`/`[NAND]`, тип (Application/Update/AddOn), читабельна версія (Application показує NACP display_version «1.0.4», апдейти/DLC — сирий `v…`), per-component Title ID, розмір і лічильники contents/rights другим рядком.
- [x] GAMEDETAIL-DRILLDOWN-274 — по A у вкладці Content дія «Показати файли» відкриває перелік NCA-файлів компонента (тип · id · розмір) через `BuildContentEntry` + `PopupList`.
- [ ] GAMEDETAIL-CONTENT-ACTIONS-274 — (TODO) DBI-контекстне меню компонента: Verify integrity, Delete, Move to NAND, Reset required version, Force language, Open content via MTP (на базі read-only virtual fs), Generate DLC unlocker.
- [ ] GAMEDETAIL-SAVES-ACTIONS-274 — (TODO) меню сейвів: детальна інфо-панель (Id/Space/Type/Account/Size/Index/Rank/Owner/Timestamp/D.Size/J.Size/Commit Id), create (вибір акаунта), resize, backup, delete, transfer WebDAV/FTP.
- [ ] GAMEDETAIL-INFO-PANEL-274 — (TODO) інфо-панель: play time (total/first/last) через pdm, місце встановлення, forced language; SDK/BID потребують читання NCA-хедера.
- [x] GAMEDETAIL-TABS-273 — вкладки Game Details перероблено на справжні вкладки: заокруглені лише верхні кути, рівний низ, активна вкладка і тіло-панель одного тону (`ThemeEntryID_POPUP`), зелена акцент-смужка зверху активної; всі кольори з теми (працює в світлій/темній). Додано хелпер `gfx::drawRectVarying`.
- [x] GAMEDETAIL-BOLD-273 — лейбли Title ID / Version / Languages / Atmosphere mods folder / NAND / SD / Components / Tickets / Saves / Save quota тепер жирні (faux-bold через `gfx::drawTextBold`), значення — звичайним тоном у вирівняній колонці.
- [x] GAMEDETAIL-FOOTER-273 — футер об'єднав пари: `L/R` → «Попередня/наступна вкладка», `L2/R2` → «Попередня/наступна гра» (merge-правила у widget.cpp).
- [x] ARCHIVE-BROWSE-273 — у файловому менеджері можна заходити всередину .zip: read-only віртуальна fs `fs::FsZip` (індекс central directory, синтез папок, decompress-to-memory), інтегрована через новий virtual-branch у `fs::File`/`fs::Dir`. Кнопка A на .zip → «Переглянути архів» (+ launch, якщо є assoc). B у корені архіву повертає у теку з .zip.
- [x] ARCHIVE-EXTRACT-273 — усередині архіву опція «Видобути вибране» копіює вибрані файли/папки з архіву в теку, де лежить .zip (через get_collections + ProgressBox::CopyFile). Compress приховано для read-only архіву.
- [x] BUILD-NRO-274 — збірка випуску 0.13.274 та верифікація компіляції (WSL, Release). Один NRO покриває вкладки/жирні лейбли/футер + архіви + DBI content-рядки + drill-down.
- [ ] HW-SMOKE-274 — на Switch: вкладки і жирні лейбли Game Details, об'єднаний футер; DBI-рядки Content ([SD]/[NAND], Title ID, версія, розмір) і «Показати файли»; вхід у .zip, лістинг вкладеної структури, «Видобути вибране» поруч із .zip; коректний вихід по B.

## Попередній delivery: v0.13.272

- [ ] FORCE-COPY-PAYLOAD-BOOTLOADER-272 — примусове копіювання payload.bin та папки bootloader з /kefir в корінь SD-карти після розпакування.
- [x] BUILD-NRO-272 — збірка випуску 0.13.272 та верифікація компіляції.

## Попередній delivery: v0.13.270

- [x] EXCLUDE-REINSTALL-SIZE-269 — під час розрахунку необхідного місця у черзі (поля Required, microSD free, NAND free та проекція смуг пам'яті) ігри, що вже встановлені на приставці, виключаються із підрахунку, оскільки при їх перевстановленні (реінсталі) вільне місце на приставці додатково не витрачається.
- [x] CTX-MENU-269 — по кнопці Plus (+) / Start у ReviewQueue відкривається бічне контекстне меню (Sidebar) з налаштуваннями встановлення (поведінка наявних ігор, ціль, резерв).
- [x] AUTO-SELECT-A-269 — якщо в черзі ReviewQueue не вибрано жодного прапорця, натискання A автоматично позначає та встановлює файл під курсором.
- [x] SETTINGS-SCOPE-269 — додано опцію "Save options globally" у налаштування: від неї залежить, чи контекстне меню черги змінює глобальні налаштування, чи session-окремі override.
- [x] REINSTALL-BEHAVIOR-269 — реалізовано 3 варіанти поведінки для вже наявних тайтлів (Reinstall / Skip / Prompt) у налаштуваннях (як PopupList) та під час встановлення. Для Prompt показується діалог підтвердження. Додано підтримку `skip_if_already_installed` в `ConfigOverride`.
- [x] CANCELLATION-HANG-269 — виправлено зависання при виході / скасуванні встановлення через curl-потоки. Таймаут `DestroyTransfer` обмежено 200 мс, кнопка B у `CancelSession` негайно повертає користувача у файловий менеджер через `SetPop()`.
- [ ] HW-SMOKE-269 — на Switch: перевірити виключення обсягу реінстальованих ігор, меню по Plus (+), авто-вибір по A, Prompt під час встановлення та швидкий вихід по B.

## Попередній delivery: v0.13.266

- [x] LOG-STYLE-266 — журнал черги встановлення тепер стилізований: події (Starting/Queue finished/Cancellation) — жирним (faux-bold через over-draw, бо bold-шрифт не завантажений); помилки — червоним; успіхи — зеленим; скасування — бурштиновим.
- [x] LOG-SKIPPED-266 — коли yati пропускає вже встановлений тайтл, він кличе новий хук `InstallProgress::OnInstallSkipped()`; черга пише зелене «Пропущено: X — вже встановлено» + рядок-підказку, що поведінку змінюють у налаштуваннях «Skip if already installed». Хук у обох install-циклах (USB/файл).
- [x] LABELS-BOLD-266 — лейбли статус-рядка ReviewQueue (Targets/Selected/Required/microSD free/NAND free/Reserve) — жирні, значення звичайні; «System memory free» → «NAND free». i18n-ключі додано в uk.json.
- [ ] HW-SMOKE-266 — на Switch: перевірити кольори/жирність журналу, зелений «пропущено» з підказкою, жирні лейбли.

## Попередній delivery: v0.13.265

- [x] GRAPH-BLEND-265 — лінії R (червона) і W (синя) малюються в режимі
  `NVG_LIGHTER` (адитивне змішування): на перетинах кольори складаються у
  яскравий мікс, видно, де вони сходяться, а не де одна ховає іншу.
- [x] HEADER-AVG-265 — верхній рядок показує СЕРЕДНЮ швидкість запису за вікном
  історії (~48 с), а не поточну; ETA («Remaining») теж рахується від цієї
  середньої, тому не стрибає. Поточну (миттєву) швидкість видно на графіку
  (readout R/W справа).
- [ ] HW-SMOKE-265 — на Switch: перевірити мікс кольорів на перетині, стабільний
  Speed/Remaining зверху; за графіком визначити «стелю» (Write чи Read) для
  наступної атаки на реальне вузьке місце (встановлення все ще повільне —
  ймовірно decompress/write у yati, не мережа).

## Попередній delivery: v0.13.264

- [x] GRAPH-READABLE-264 — переписано R/W-графік на екрані Installing:
  горизонтальна сітка з підписами розмірності (MiB/s), «кругла» авто-шкала
  (крок 1/2/5×10ⁿ, підлога 1 MiB/s), scissor-кліп у межах графіка (піки більше
  не вилазять), а цифровий readout R/W усереднений за останні ~2 с, щоб не
  блимав 0 під час простою читання.
- [x] READAHEAD-DIALBACK-264 — `MAX_BUFFER_SIZE` 16→8 MiB: 16 не дало виграшу
  для стиснених (ncz) інсталяцій і лише подовжило порожні проміжки читання;
  вузьке місце там downstream (decompress/write), не мережа. `CURLOPT_BUFFERSIZE`
  256 KiB лишається (нешкідливо-корисно, допомагає нестисненим .nsp).
- [ ] HW-SMOKE-264 — на Switch: HTTP-встановлення; graph має читатись (сітка,
  видимі R і W, без overflow). За новим скріном визначити, яка лінія «стеля»
  (Write чи Read) — це вкаже справжнє вузьке місце для наступної оптимізації.

## Попередній delivery: v0.13.263

- [x] HTTP-READAHEAD-263 — оптимізовано пропускну здатність HTTP-встановлення
  («гребінка» рідких читань). Причина: 4 MiB hand-off буфер між curl-потоком і
  читачем + блокуючий write-callback → коли буфер повний, curl перестає читати
  сокет, TCP-вікно закривається, передача йде ривками. Зміни: `MAX_BUFFER_SIZE`
  4→16 MiB (більше read-ahead, сокет не стопориться під час downstream-столів);
  `CURLOPT_BUFFERSIZE` 64→256 KiB (curl читає сокет більшими шматками). Черга
  вже рухалась (фікс 262) — тут суто швидкість.
- [ ] HW-SMOKE-263 — на реальній Switch: встановлення з HTTP; порівняти графік
  R/W (має бути щільніший/рівніший) і Remaining проти v0.13.262.

## Попередній delivery: v0.13.262

- [x] INSTALL-MUTEX-262 — усунено пошкодження власності HOS-м'ютексів у спільному
  yati-пайплайні: `WakeAllThreads()` більше не чіпає `read_mutex`/`write_mutex`
  (лише будить condvar-и), а `SetReadResult`/`SetDecompressResult`/`SetWriteResult`
  не захоплюють м'ютекси. Раніше воркер у `Set*Result` тримав обидва м'ютекси і
  кликав `WakeAllThreads()`, тоді як оркестратор у cleanup-циклі теж кликав
  `WakeAllThreads()` без володіння ними → `mutexUnlock` чужого м'ютекса →
  зависання черги встановлення та краш при примусовому закритті. Регресія з
  v0.13.259. Стосується всіх джерел, що йдуть через чергу (SD/HTTP/USB-DBI).
- [x] INSTALL-LIVENESS-262 — `IsAnyRunning()` рахує `decompress_running` замість
  `decompress_result` (латентна помилка: cleanup-цикл міг завершитись, поки потік
  ще працює, або крутитись зайве після його виходу з помилкою).
- [x] BUILD-FIX-262 — `app_display_options.cpp` більше не намагається обгорнути
  `m_skip_if_already_installed` (тепер `OptionLong` 0/1/2) у `SidebarEntryBool`:
  замінено на 3-позиційний `SidebarEntryArray` (Reinstall/Skip/Prompt), як у
  `settings_menu.cpp`. Робоче дерево знову компілюється.
- [x] BUILD-VERIFY-262 — інкрементальна збірка `cmake --preset ReleaseWithInstall`
  у WSL проходит: `[100%] Built target sphaira_nro`, exit 0 (yati.o та
  app_display_options.o компілюються чисто).
- [ ] HW-SMOKE-262 — ручний smoke test на реальній Switch: встановлення з SD,
  HTTP/WebDAV та USB-DBI через чергу; перевірити, що черга рухається і завершується.
  Якщо все ще зависає — зняти `log.txt` одразу після зависання, не перезапускати
  застосунок (наступний запуск обнуляє лог через O_TRUNC).

## Попередній delivery: v0.13.261

- [x] INSTALL-DEADLOCK-261 — виправлено взаємоблокування (lock inversion deadlock) між потоком встановлення та curl-потоком шляхом переходу з fopen/fclose на POSIX open/write/close в log_write
- [ ] HW-SMOKE-261 — ручний smoke test встановлення/скасування з мережі на реальній Switch

## Попередній delivery: v0.13.260

- [x] INSTALL-LOGS-260 — додано детальне діагностичне логування (verbose logging) в yati та devoptab для виявлення точного місця зависання встановлення
- [ ] HW-SMOKE-260 — ручний аналіз файлу log.txt на консолі після спроби встановлення та скасування

## Попередній delivery: v0.13.259

- [x] INSTALL-DEADLOCK-259 — виправлено взаємоблокування потоків декомпресії та запису yati при помилках чи скасуванні встановлення (виклики WakeAllThreads())
- [ ] HW-SMOKE-259 — ручний smoke test встановлення/скасування встановлення з мережевого джерела на реальній Switch

## Попередній delivery: v0.13.258

- [x] INSTALL-BYPASS-258 — обхід мережевого аналізу (AnalyzeSource) для віддалених джерел (HTTP/WebDAV) у черзі встановлення для уникнення зависань
- [ ] HW-SMOKE-258 — ручний smoke test встановлення з мережевого джерела на реальній Switch

## Попередній delivery: v0.13.253

- [x] INSTALL-HANG-253 — спільні curl-хендли devoptab (diropen/lstat/unlink/
  mkdir/rename) захищені м'ютексом: конкурентний доступ metadata-воркера,
  UI-скану та інсталятора більше не може завісити передачу; не-черговий шлях
  інсталяції теж викликає PauseRemoteMetadata
- [x] BUILD-PRESET-253 — збірка перейшла на пресет ReleaseWithInstall
  (ENABLE_NETWORK_INSTALL=ON, як у build.sh): черга встановлення з deferred
  analysis для мережевих джерел активна
- [x] HIST-54 (частково) — графік швидкостей R/W на екрані Installing: червона
  лінія читання, синя — запису; ліворуч мітки R/W, праворуч поточні швидкості;
  дані з нових read/write офсетів yati (UpdateInstallReadWrite)
- [ ] HW-SMOKE-253 — ручний smoke test на реальній Switch за `tests.md`

## Попередній delivery: v0.13.252

- [x] CURL-REDIRECT-AUTH-252 — devoptab і download.cpp шлють credentials після
  редиректів (`CURLOPT_UNRESTRICTED_AUTH`): джерело `http://`/`webdav://`, яке
  301-ить на https, більше не отримує 401 (перевірено проти
  dav.customfw.xyz: PROPFIND через http давав 401 після 301, з
  location-trusted — 207; парсер верифіковано офлайн на реальній відповіді
  Apache mod_dav з `<D:response>` — 4 папки розпізнано)
- [x] FB-WRAP-252 — список файлового браузера зациклений: вниз на останньому
  записі переходить на перший і навпаки
- [x] META-PRIORITY-252 — метадані віддалених джерел вантажаться від курсора:
  видимий екран, потім ±1 екран, потім решта; розміри файлів мають пріоритет
  над лічильниками папок (той самий регіон)
- [ ] HW-SMOKE-252 — ручний smoke test на реальній Switch за `tests.md`

## Попередній delivery: v0.13.251

- [x] CURL-AUTH-251 — devoptab curl тепер веде переговори про схему
  автентифікації (`CURLAUTH_ANY`, як у download.cpp); сервери з Digest більше
  не відповідають 401 на PROPFIND/GET при перегляді (причина «пусто» у .249 і
  `FsUnknownStdioError` у .250, коли Test Connection проходив)
- [x] NET-ERROR-UX-251 — помилки мережевих джерел показуються звичайним
  діалогом з поясненням (сервер недоступний / лістинг не вдався) замість
  страшного error box з кодом і проханням повідомити про проблему
- [x] BADGE-PERSIST-251 — статус джерела кешується на сесію за URL: бейдж на
  root стає зеленим/червоним після входу або Test Connection (з root і з
  Settings) і не скидається при поверненні на root
- [ ] HW-SMOKE-251 — ручний smoke test на реальній Switch за `tests.md`

## Попередній delivery: v0.13.250

- [x] WEBDAV-LIST-250 — PROPFIND-парсер регістро- і namespace-незалежний
  (`<D:response>`, `<ns0:...>` тощо); не-WebDAV відповіді падають у fallback на
  HTML index; FTP LIST замість NLST дає типи та розміри записів
- [x] SCAN-ERROR-250 — невдалий лістинг змонтованого мережевого джерела показує
  error box і повертає до root замість зеленого стану `Empty...`
- [x] ROOT-SOURCES-250 — з root файлового браузера доступні ті самі дії, що в
  Settings -> Sources: Add network location, Edit Source (SourceEditMenu),
  Test Connection, Rename, Properties, Delete; root-список оновлюється після
  змін через OnFocusGained, а файлові операції (Cut/Copy/Delete/Rename/Zip)
  приховані на root
- [x] ROOT-BADGE-250 — бейдж джерела на root стає зеленим/червоним за
  результатом реальної спроби підключення
- [ ] HW-SMOKE-250 — ручний smoke test на реальній Switch за `tests.md`

## Попередній delivery: v0.13.249

- [x] AUDIT-HISTORY — аудит останніх 5 комітів і всієї історії plan/task/archive
- [x] HIST-HTTP-RETRY — GET-only retry/resume, restart без stale tail, final flush check
- [x] HIST-61A.1 — Game Details Overview і реальні NCM component rows
- [x] HIST-61A.2 — точковий dump поточної гри без успадкування bulk selection
- [x] HIST-61A.3 — DBI-подібні Content/Tickets/Saves, мови, allocated save size і L/R навігація
- [x] HIST-62 — Base/Update/DLC/LayeredFS badges із кешованого NCM summary
- [x] HIST-GAMES-VIEWPORT — Games не малюється поверх header/footer
- [x] HIST-GAMES-STORAGE — розмір вибраної гри підсвічується у NAND/SD bars
- [x] HIST-GAMES-SELECTION — X перемикає одну гру, Y інвертує вибір, B спочатку очищає вибір
- [x] HIST-GAMES-COMMON-ACTIONS — меню показує лише спільно застосовні типи dump для вибраних ігор
- [x] HIST-GAMES-UX-240 — контрастні badges включно з `Без контенту`, виразні вкладки,
  пояснення папки модів Atmosphere та об'єднана легенда L/R і ZL/ZR
- [x] SOURCES-PROTOCOL-EDIT — наявне джерело можна перетворити між SMB/WebDAV/FTP/HTTP;
  URL, порт, поля форми та збережені реквізити узгоджуються з новим протоколом
- [x] HIST-GAMES-BADGES-242 — Base/DLC/Update/LayeredFS показуються вертикально,
  контрастними різнокольоровими лейбочками; порожня гра має червону лейбочку `-`
- [x] HTTP-WEBDAV-CRASH-243 — виправлено use-after-free заголовків PROPFIND/FTP MKD
  і небезпечне блокування при повторному перегляді HTTP/WebDAV джерела
- [x] HIST-GAMES-UNAVAILABLE-244 — metadata-error/application-record без встановленого
  контенту має червоний `-`; Game Options містить прямий show/hide toggle і Layout
- [x] HIST-GAMES-BASE-245 — кожен запис без Base має червоний `-`; `L / R` перемикають
  вкладки деталей, а `ZL / ZR` — попередню/наступну гру
- [x] HIST-HBMENU-TITLE-246 — назва у верхній плашці HB Menu card не виходить за
  рамку; довгий текст прокручується лише на картці під фокусом
- [x] HIST-GAMES-BADGE-SIZE-247 — мінімальна ширина кожного game badge дорівнює `Base`
- [x] HIST-GAMES-STORAGE-SUM-247 — NAND/SD показує точні bytes гри під фокусом або
  суму групового X/Y-виділення разом із пропорційним сегментом
- [x] HIST-INSTALL-NOSLEEP-248 — auto-sleep lock перевіряється через applet service;
  media-playback fallback утримує консоль активною, якщо перевірка не пройшла
- [x] SOURCES-PREFLIGHT-249 — File Browser робить HTTP/WebDAV/FTP probe до mount/scan;
  недоступне джерело показує помилку замість зеленого стану `Empty`
- [x] WEBDAV-SYNC-PREFLIGHT-249 — Test Connection, auto-sync, remote restore і full sync
  використовують суворий WebDAV PROPFIND; HTTP errors більше не вважаються success
- [x] WEBDAV-SCHEME-249 — `webdav://` normalizes to HTTP, `webdavs://` to HTTPS;
  explicit HTTP джерела не обираються як WebDAV sync targets, missing folders створюються
- [x] HIST-WEB-APPLET.1 — Runtime Mode UX, Title Mode guide і forwarder installer
- [x] HIST-WEB-APPLET.2 — Applet worker profile, listener diagnostics і loopback self-test
- [x] AUDIO-REMOVE — вилучено BGM, UI sounds, audio init, settings та `libpulsar`
- [x] BUILD-RELEASE — Release NRO v0.13.249 зібрано; `git diff --check` пройдено
- [ ] HW-SMOKE-249 — ручний smoke test на реальній Switch за `tests.md`

## Черга реалізації

- [ ] HIST-54 — DBI: графік R/W швидкості та загальний progress bar
- [ ] HIST-55 — DBI: динамічний рядок журналу на пакет
- [ ] HIST-56 — DBI: сегментовані NAND/SD bars і наочний ReviewQueue
- [ ] HIST-60 — Games: перенос NAND ↔ SD та сортування за носієм
- [x] HIST-61A — Games: Game Details з Overview/Content/Tickets/Saves
  - [x] Окремий екран по A, Overview, реальні NCM component rows і точковий component dump
  - [x] Видимі partial-load помилки; чесні `Contents folder` і `Save quota`
  - [x] Вкладки Tickets і Saves, повний список мов та allocated save-data size
- [ ] HIST-61B — Games: dump/verify/read-only mount і безпечні component actions
- [ ] HIST-61C — Games: save integration, ticket details і перевірений DLC unlocker
- [x] HIST-62 — Games: badges Base/Update/DLC/LayeredFS
- [ ] HIST-WEB-APPLET — Applet warning, Title Mode guide і встановлення forwarder
  - [x] Applet chooser: start anyway / install forwarder / Title Mode instructions
  - [x] Runtime Mode help, видимі обмеження browser/NSO, listener error log і Applet worker profile
  - [x] Loopback HTTP listener self-test
  - [ ] Ручна діагностика Wi-Fi client isolation на реальній Switch/точці доступу
- [ ] HIST-USB-COMPAT — ручна матриця USB-клієнтів на реальній Switch
- [ ] HIST-NFS-SFTP — підтримка NFS/SFTP як окремих network sources
- [~] HIST-PLAYER — вбудований плеєр; заморожено до окремого погодження

## Поточна задача

Примусове копіювання `payload.bin` та папки `bootloader` при оновленні Кефіру.

## Правило закриття

Позначка `[x]` означає перевірений код або автоматичний test gate. Те, що
потребує реальної Switch, залишається `[ ]` до записаного результату в
`tests.md`; успішна компіляція сама по собі не закриває hardware gate.
