# Актуальний план

Поточний delivery — **v0.13.490**. Завершені плани збережено в
[`archive/plan_v0.13.357-v0.13.430.md`](archive/plan_v0.13.357-v0.13.430.md)
та [`archive/plan_archive.md`](archive/plan_archive.md).

## Поточний delivery: v0.13.490 — Fix cstring include in static logger

Статус: реалізацію виконано та перевірено:
1. **Виправлення компіляції `log.cpp`**:
   - Додано заголовок `<cstring>` у `sphaira/source/log.cpp` для повної підтримки `std::memcpy` у статичному неалокуючому буфері.
2. **Версія та збірка**:
   - Піднято версію до `0.13.490` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `task.md`, `walkthrough.md`.
   - Успішно зібрано `sphaira_nro` у WSL (`[100%] Built target sphaira_nro`).

## Попередній delivery: v0.13.489 — Zero-heap static logging buffer & image load ordering

Статус: реалізацію виконано та перевірено. Повністю усунено алокації кучі у фоновому потоці логування та нормалізовано порядок ініціалізації графіки:
1. **Статичний буфер логування без звернень до heap (`sphaira/source/log.cpp`)**:
   - Замінено динамічний `std::string` та операції `append`/`swap`/`free` на фіксований статичний буфер `g_buffer_data` (64 КБ). Це повністю виключає звернення до `_malloc_r`, `_realloc_r` та `free` під час запису логів з фонових потоків і скидання на диск/сокет, унеможливлюючи пошкодження метаданих чанків кучі (`Data Abort 0x4A8`).
2. **Порядок завантаження ресурсів (`sphaira/source/app.cpp`)**:
   - `InitDefaultImage()` перенесено перед запуском фонових воркерів `ntp::Start()` та `forwarder_auto::StartCheck()`, що гарантує ексклюзивне розкодування системних іконок без конкуренції за пам'ять.
3. **Версія та інтеграція**:
   - Піднято версію до `0.13.489` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `task.md`, `walkthrough.md`.

## Попередній delivery: v0.13.488 — Sysmodule slow SD boot timeout & crash prevention

Статус: реалізацію виконано та перевірено. На основі аналізу патчу SwitchThemeInjector усунено падіння та зависання на повільних microSD картах:
1. **Збільшення тайм-аутів ініціалізації ФС у сисмодулі (`sysmodule/source/main.c`)**:
   - Збільшено ліміт спроб підключення `fsInitialize()` та монтування `fsdevMountSdmc()` зі 100 ітерацій (10 секунд) до 3000 ітерацій (300 секунд / 5 хвилин), що гарантує успішний старт сисмодуля на повільних картах пам'яті.
   - Видалено фатальний аборт `diagAbortWithResult` при помилці `smInitialize()`, що запобігає крашу Atmosphere при затримках сервісів під час завантаження ОС.
2. **Версія та перевірка**:
   - Ітеровано версію програми до `0.13.488` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `task.md`, `walkthrough.md`.

## Попередній delivery: v0.13.487 — SD card FS sync, malloc & NanoVG stability on slow cards / NX-Link handoff

Статус: реалізацію виконано та перевірено. Усунено аварійні падіння (Data Abort 0x4A8 в `_malloc_r`) та збої файлової системи на повільних картах пам'яті під час передачі NRO через NX-Link та запуску:
1. **Фіксація файлової системи та захист від пошкодження microSD (`main.cpp`, `nxlink.cpp`, `nro.cpp`, `log.cpp`)**:
   - Виправлено ім'я монтування пристрою в `fsdevCommitDevice("sdmc")` та `fsdevGetDeviceFileSystem("sdmc")` у `userAppExit()`. Раніше передавався некоректний суфікс `"sdmc:"`, через що системне збереження кешу ФС не викликалося під час виходу з програми.
   - Додано виклики `fsdevCommitDevice("sdmc")` після створення/перейменування файлів у `nxlink.cpp`, перед викликом `envSetNextLoad` у `nro.cpp` (`launch_internal`), а також при записі логів у `log.cpp` (`do_flush`, `log_write_error`).
2. **Усунення race conditions та захист хіпу при логуванні (`log.cpp`)**:
   - Переведено мережеву передачу логів у фоновому потоці з `stdout`/stdio на прямий `send(sock, ...)`. Це усуває конфлікти алокацій у stdio нових потоків newlib, які призводили до пошкодження заголовків чанків heap (`_malloc_r`).
   - Замінено `std::localtime` на реентрабельний `localtime_r` у `log_write_error`.
3. **Безпечне завантаження ресурсів тем та текстур (`app_theme.cpp`)**:
   - У `LoadElementImage` та `LoadElementColour` додано явне створення нуль-термінованих рядків `std::string` перед викликом `nvgCreateImage` та `std::strtoul`, що усуває вихід за межі буфера `std::string_view`.
4. **Версія, збірка та розгортання**:
   - Оновлено версію до `0.13.487` у `CMakeLists.txt`, успішно зібрано `sphaira_nro` у WSL, виконано тести (`tests/run.sh`), бінарник розгорнуто на microSD диск `I:\` (`I:\hbmenu.nro` та `I:\switch\kefir-hub.nro`).

## Попередній delivery: v0.13.486 — Saves menu L/R shoulder button tab navigation

Статус: реалізацію виконано та перевірено. Додано можливість швидкого та безшовного перемикання між категоріями збережень («Встановлені ігри», «Видалені ігри», «Резервні копії») плечовими кнопками L та R:
1. **Реєстрація дій плечових кнопок (`save_menu.cpp`)**:
   - У конструкторі `Menu::Menu` додано дії `Button::L` ("Previous tab"_i18n) та `Button::R` ("Next tab"_i18n) для автономного режиму меню (`!m_app_id_filter`).
   - Кнопки відображаються у футері та підтримують як натискання фізичних кнопок контролера, так і сенсорні натискання по підказках у футері.
2. **Циклічне перемикання категорій (`save_menu.hpp`, `save_menu.cpp`)**:
   - Реалізовано метод `Menu::ChangeCategory(s64 delta)`, який циклічно перемикає категорію за списком `Installed` <-> `Deleted` <-> `Backups`.
   - Реалізовано метод `Menu::SetCategory(Category category)`: змінює `m_category`, оновлює заголовок `SetTitle(...)`, відтворює звуковий ефект зміни фокусу `App::PlaySoundEffect(SoundEffect_Focus)` та перезавантажує список елементів через `ScanHomebrew()`.
3. **Версія, тести та збірка**:
   - Піднято версію до `0.13.486` у `sphaira/CMakeLists.txt`, оновлено `README.md`, успішно скомпільовано ціль `sphaira_nro` у WSL (`[100%] Built target sphaira_nro`), пройдено перевірки host unit tests (`tests/run.sh`) та `git diff --check`.

## Попередній delivery: v0.13.485 — Screensaver display sleep prevention & OLED user brightness retention

Статус: реалізацію виконано та перевірено. Забезпечено надійну роботу скрінсейвера без вимкнення екрана та оптимізовано яскравість для різних типів матриць:
1. **Запобігання вимкненню екрана та авто-сну (`app.hpp`, `screensaver.cpp`)**:
   - Оновлено `App::SetAutoSleepDisabled(bool enable)`: тепер виклик `appletSetMediaPlaybackState(true)` здійснюється обов'язково разом із `appletSetAutoSleepDisabled(true)`. Згідно зі специфікацією HOS, саме `SetMediaPlaybackState` блокує системне приглушення яскравості (dimming) та вимкнення підсвітки екрана через неактивність.
   - Додано виклики `App::SetAutoSleepDisabled(true)` у `Screensaver::Start()` та скидання у `Screensaver::Stop()`.
   - У `Screensaver::Update(...)` додано виклик `appletReportUserIsActive()`, який періодично передає ОС сигнал про активність користувача на рівні HID, унеможливлюючи спрацьовування таймерів очікування HOS.
2. **Збереження яскравості користувача на OLED та зниження на LCD (`app_settings.cpp`, `screensaver.cpp`)**:
   - Реалізовано апаратне визначення типу консолі `App::IsOledModel()` через опитування `splGetConfig(SplConfigItem_HardwareType, &hardware_type)` (значення `5` відповідає моделі Aula / Switch OLED).
   - У `Screensaver::Start()` для OLED-моделей встановлено збереження поточної виставленої користувачем яскравості `m_saved_brightness` (чистий чорний фон скрінсейвера `#000000` вимикає пікселі OLED і споживає 0W, тому годинник і статистика залишаються яскравими та легко читабельними з відстані).
   - Для LCD-моделей (Switch V1, V2, Lite) яскравість знижується до значення `App::GetBlankBrightness() / 100.f`, що заощаджує батарею та усуває засвітку підсвітки в темряві.
   - Збережено можливість ручного регулювання яскравості правим стіком (Up/Down) на будь-якому типі дисплея та обов'язкове відновлення початкової яскравості користувача при виході зі скрінсейвера.
3. **Попередній перегляд скрінсейвера (`screensaver.cpp`)**:
   - У `SaverPreview::Update` додано виклик `m_saver.Update` для коректного оновлення дрейфу, реакції на стіки та запобігання засинанню консолі в режимі прев'ю.
4. **Версія, документація та збірка**:
   - Піднято версію до `0.13.485` у `sphaira/CMakeLists.txt`, синхронізовано `README.md`, успішно зібрано бінарник `sphaira_nro` у WSL (`[100%] Built target sphaira_nro`), пройдено перевірки host unit tests (`tests/run.sh`) та `git diff --check`.

## Попередній delivery: v0.13.484 — NX-Link SD commit, path normalization, buffer bounds & forwarder auto-install stabilization

Статус: реалізацію виконано та перевірено. На основі аналізу дампів аварійних збоїв (Atmosphere Crash Reports) усунено причини падіння пам'яті та крашу процесу при роботі NX-Link та старті застосунку:
1. **Безпечна ініціалізація `m_app_path` (`app.cpp`)**: Виправлено нетермінований рядок шляху виконуваного файлу при старті (`argv0` з `sdmc:/`). Раніше `std::strncpy` копіював байти без завершального `\0`, що призводило до читання пам'яті за межами буфера, битих записів у `playlog.ini` та некоректного обчислення SHA256-хешу. Тепер буфер явно обнуляється та гарантовано термінується `\0`.
2. **Захист від повторного встановлення активного тайтла (`forwarder_auto_install.cpp`)**:
   - Додано перевірку режиму виконання: якщо Sphaira вже працює як встановлений Application (forwarder), потік автоматично завершує перевірку без сканування та встановлення.
   - Реалізовано вилучення Title ID із назви знайденого NSP (`Homebrew menu [03DB12780BD84000]...`) та перевірку через `nsIsAnyApplicationEntityInstalled`. Якщо знайдений тайтл вже встановлено на консолі, фонове встановлення пропускається, що усуває конкурентний перезапис активного тайтла та конфлікти алокацій пам'яті/NCM під час завантаження тем і Deko3D.
   - Додано функцію `StopCheck()` та обробку запиту на зупинку `g_stop_requested` у `SilentInstallProgress`, що забезпечує коректну зупинку фонового потоку в деструкторі `App::~App()`.
3. **Нормалізація шляхів та фіксація файлової системи в `nxlink.cpp`**:
   - Шляхи, передані з хоста через NX-Link, нормалізуються (видаляється префікс `sdmc:`, гарантується початковий `/`), що усуває збої нативних викликів `FsFileSystem` (`0x202` / `0x402`).
   - Додано обов'язкові виклики `fs.Commit()` після запису даних у тимчасовий файл та після фінального `fs.RenameFile`, що гарантує збереження таблиці кластерів FAT32/exFAT на карті пам'яті перед запуском NRO та запобігає падінню файлової системи microSD.
   - Забезпечено безпечну роботу з буфером аргументів `args_buf` із гарантованим нуль-термінатором та `strncpy` для колбеків повідомлень.
   - `SocketWrapper` переведено на move-only семантику з коректним закриттям сокетів без подвійного `close`.
4. **Потокобезпечне логування (`log.cpp`)**: Замінено небезпечний `std::localtime` на реентерабельний `localtime_r` у `log_write_arg_internal`.
5. **Паралельний запуск тестів (`tests/run.sh`)**: Скрипт тестів хоста переведено на паралельну компіляцію та запуск усіх тестових наборів.
6. **Версія та збірка**: Піднято версію до `0.13.484` у `sphaira/CMakeLists.txt`, успішно скомпільовано цільовий бінарник у WSL (`[100%] Built target sphaira_nro`), пройдено всі unit-тести.

## Попередній delivery: v0.13.483 — Install queue list layout bounds fix & auto-advance on X button

Статус: реалізацію виконано та перевірено. Виправлено накладання списку пакунків у черзі встановлення на футер та додано автоматичний крок курсора при виборі пунктів кнопкою X:
1. **Геометрія списку черги інсталяції (`dbi_menu.cpp`)**: Виправлено перекриття списком елементів лінії та кнопок футера (`FOOTER_LINE_Y = 646.f`). Зменшено висоту рядка з 82.f до 78.f, скориговано позицію списку `queue_pos` на `{70.f, GetY() + 63.f, 1140.f, 470.f}` (150.f по осі Y). Тепер 6 рядків списку займають висоту 468.f (край на рівні 618.f, рамка фокусу 622.f), що залишає 24px безпечного відступу до розділювача футера.
2. **Геометрія списків журналу та помилок**: Скориговано висоту `log_pos` з 330.f до 310.f (`m_log_list` на 10 рядків по 30.f = 300.f, `m_error_list` на 5 рядків по 55.f = 275.f), усунувши накладання на футер у режимах `Installing`, `Summary` та `Cancelled`.
3. **Автоматичний перехід курсора при виборі кнопкою X**: Оновлено дію `Button::X` ("Select") для стану `State::ReviewQueue`. При натисканні кнопки X перемикається стан виділення `m_queue[m_index].selected`, а потім, якщо це не останній елемент черги (`m_index + 1 < m_queue.size()`), курсор автоматично переходить на наступний рядок (`m_index++`) із забезпеченням видимості через `m_list->EnsureVisible`. Це повністю відповідає логіці вибору в інших меню програми (`game_menu`, `homebrew`, `filebrowser`, `save_menu`).
4. **Захист ножиць кадрування контенту (`layout.hpp`)**: У `PaddedContentClipY` встановлено обов'язкове обмеження `bottom = std::min(bottom, CONTENT_BOTTOM)` для будь-яких блоків контенту, які починаються нижче лінії заголовка (`y >= HEADER_LINE_Y`), що запобігає малюванню контенту поверх футера.
5. **Версія, тести та збірка**: Піднято версію до `0.13.483` у `CMakeLists.txt`, оновлено документацію, успішно виконано прогін усіх тестів та збірку `sphaira_nro` у WSL.

## Попередній delivery: v0.13.482 — Fully silent background forwarder installation without restart prompt

Статус: реалізацію виконано та перевірено. Переведено автоматичне встановлення форвардера при старті програми у повністю тихий режим без запитів на перезапуск:
1. При старті програми фоновий потік перевіряє наявність встановленого форвардера для Homebrew Menu / Sphaira через `nsIsAnyApplicationEntityInstalled`.
2. Якщо форвардер відсутній, у фоні виконується пошук `Homebrew menu*.nsp` у папці `/Games/` та тихе встановлення через `yati::InstallFromFile`.
3. Повністю видалено діалогові вікна `OptionBox` та пропозиції перезапуску: після завершення встановлення потік тихо фіксує успіх у логах і завершує роботу, не перериваючи та не турбуючи користувача.
4. Очищено невикористовувані заголовні файли в `forwarder_auto_install.cpp`.
5. Піднято версію програми до `0.13.482` у `CMakeLists.txt`, оновлено документацію, успішно виконано збірку в WSL та пройдено всі тести.

## Попередній delivery: v0.13.481 — Install queue package skip fix & USB link resynchronization

Статус: реалізацію виконано та перевірено. Виправлено проблему, коли дія пропуску пакунка кнопкою B у черзі встановлення переривала всю чергу:
1. Усунено розсинхронізацію USB-протоколу: при пропуску пакунка користувачем (`user_skipped`) у `ThreadFunction` активний USB endpoint скасовується, через що хост-застосунок на ПК залишався в середині передачі попереднього файлу. Додано автоматичний виклик `ReestablishUsbLink()`, який повторно проводить handshake та переводить хост у режим очікування нової команди перед переходом до наступного пакунка в черзі.
2. Розширено умови повторних спроб (`attempt`) у `ThreadFunction`: тепер при виникненні помилок протоколу/сесії DBI (`IsDbiSessionError`) відбувається спроба повторного підключення замість миттєвого завершення черги.
3. Оновлено діалог `OptionBox` для кнопки `B` ("Skip package") під час встановлення (`State::Installing`): встановлено дефолтний індекс `1` ("Yes"), що дозволяє користувачеві підтвердити пропуск пакунка кнопками `A` або `+`, або скасувати діалог кнопкою `B`.
4. Синхронізовано `LocalThreadFunction`: включено перевірку `Result_UsbCancelled` та уніфіковано встановлення прапорця `m_cancel_requested` при загальному скасуванні черги.
5. Розширено `test_queue_outcome.cpp` тестом багатопакетної черги з пропуском одного пакунка та успішним встановленням наступного, піднято версію до `0.13.481` у `CMakeLists.txt`, успішно скомпільовано реліз у WSL та пройдено всі unit-тести.

## Попередній delivery: v0.13.480 — Save data deletion mechanism & auto-creation on restore

Статус: реалізацію виконано та перевірено. Додано механізм видалення збережень для встановлених та видалених ігор (а також резервних копій) у меню Tools > Saves, і покращено відновлення сейвів:
1. Додано пункт `"Delete"` до спливаючого списку дій `Save Action` при виборі збереження кнопкою `A` або групи збережень кнопкою `X`, а також у бічне меню `Save Options` (кнопка `+`).
2. Реалізовано бічне меню `Delete Options` з підтримкою фільтрації за обліковими записами (`Accounts`) та типами сейвів (`Save Types`).
3. Додано захисні діалоги підтвердження `OptionBox` із попередженням про незворотність видалення та відображенням іконки гри.
4. Реалізовано метод `Menu::DeleteSaves`:
   - Для встановлених та видалених ігор: видалення сейвів із консолі за допомогою `fsDeleteSaveDataFileSystemBySaveDataSpaceId` та резервного `fsDeleteSaveDataFileSystemBySaveDataAttribute`. Для категорії "Deleted Games" гра повністю зникає зі списку після видалення сейву.
   - Для категорії "Backups": видалення файлів резервних копій (`.zip`, `.disa`) з SD-карти / накопичувача та очищення порожніх каталогів.
5. Покращено функцію `RestoreSaveInternal`: усунуто падіння при відновленні на чистих/відновлених EmuNAND або нових іграх без попереднього сейву. За відсутності файлової системи збереження на консолі вона автоматично створюється через `fsCreateSaveDataFileSystem` на основі метаданих архіву перед розпакуванням файлів.
6. Оновлено файли локалізації (`en.json`, `uk.json`), синхронізовано документацію (`README.md`), піднято версію до `0.13.480`, успішно скомпільовано цільовий `sphaira_nro` у WSL та пройдено всі unit-тести.

## Попередній delivery: v0.13.479 — Automatic forwarder check, silent install & title mode restart prompt

Статус: реалізацію виконано та перевірено. Додано автоматичну фонову перевірку наявності встановленого форвардера при старті програми, тихе встановлення з microSD та пропозицію перезапуску в Title Mode:
1. При запуску Sphaira у фоновому потоці (`forwarder_auto::StartCheck()`) перевіряється, чи встановлено форвардер для Homebrew Menu або Sphaira (перевірка стандартних тайтлів `010000000000100D`, `050000000000100D`, а також згенерованих ідентифікаторів Sphaira на базі виконуваного NRO через `nsIsAnyApplicationEntityInstalled`).
2. Якщо форвардер відсутній, виконується прозорий фоновий пошук NSP-пакета в директорії `/Games/` на microSD карті за маскою `Homebrew menu*.nsp`.
3. Знайдений NSP встановлюється у фоні за допомогою `yati::InstallFromFile` із застосуванням спеціалізованого `SilentInstallProgress` без блокування інтерфейсу та з прапорцем `skip_if_already_installed = 1`.
4. Розширено інтерфейс `ui::InstallProgress` та механізм `yati.cpp` методом `OnTitleInstalled(u64 title_id)` для точного визначення встановленого ідентифікатора тайтла.
5. Після успішного встановлення через `evman::push` на головний UI-потік виводиться діалогове вікно `OptionBox` із запитом `"Homebrew Menu forwarder installed. Restart into Title Mode now?"`. При підтвердженні ("Restart") викликається `appletRequestLaunchApplication(target_tid, nullptr)` та `App::Exit()`.
6. Додано допоміжну функцію `path::StartsWithIC` у `path_util.hpp`, розширено host unit tests (`test_path_util.cpp`, 162 checks), синхронізовано 14 файлів локалізації.
7. Піднято версію до `0.13.479`, успішно виконано збірку в WSL (`sphaira_nro`), пройдено всі тести та `git diff --check`.

## Попередній delivery: v0.13.478 — Theme packages download & instant install prompt

Статус: реалізацію виконано та перевірено. Додано автоматичну пропозицію встановлення для готових пакетів тем (Mario BG Dark, Switch 2 Theme by alexwak) у меню Tools -> Themes:
1. Раніше готові пакети тем завантажувалися через `MakePackageAction`, який лише розпаковував zip у `/themes/` та показував сповіщення "Done", не пропонуючи запуск `NXThemesInstaller` (на відміну від завантаження з Themezer та закріплених тем).
2. Реалізовано спільні функції `PromptInstallTheme` та `InstallThemePackage` у `themezer.hpp` / `themezer.cpp`: під час розпакування zip-архіву автоматично відстежуються шляхи до всіх видобутих `.nxtheme` файлів, після чого показується запит `"Theme downloaded, install now?"`.
3. При підтвердженні встановлення запускається `NXThemesInstaller.nro` з передачею аргументів розпакованих тем (`sdmc:/themes/...`). Якщо інсталятор відсутній на консолі, пропонується його швидке завантаження з GitHub.
4. Додано функцію `MakeThemePackageItem` у `settings_menu.cpp` з попереднім запитом `"Download theme?"`, уніфікуючи поведінку для всіх типів тем.
5. Піднято версію до `0.13.478`, успішно виконано збірку в WSL, пройдено всі тести та `git diff --check`.

## Попередній delivery: v0.13.477 — Game details stat label vertical alignment fix

Статус: реалізацію виконано та перевірено. Виправлено вертикальне зміщення та накладання прокручуваних лейблів статистики гри:
1. При перевищенні довжини локалізованого лейблу (наприклад, "Останній запуск" / "Last played") 1/3 ширини блоку, текст переходить у режим автопрокручування `m_stat_label_scrolls[...].Draw`. Раніше передавалося вирівнювання `NVG_ALIGN_LEFT` без визначення вертикальної площини, через що NanoVG вирівнював текст за базовою лінією (`NVG_ALIGN_BASELINE`) замість верхнього краю (`NVG_ALIGN_TOP`), зміщуючи весь рядок угору (~15px) відносно свого значення.
2. Додано прапорець `NVG_ALIGN_LEFT | NVG_ALIGN_TOP` до `m_stat_label_scrolls` та уніфіковано вирівнювання для `m_language_scroll.Draw` (`NVG_ALIGN_LEFT | NVG_ALIGN_TOP`, `y + 1.f`).
3. Піднято версію до `0.13.477`, успішно виконано збірку в WSL, пройдено всі тести та `git diff --check`.

## Попередній delivery: v0.13.476 — UTF-16 to UTF-8 decoding & Cyrillic filename fix for MTP

Статус: реалізацію виконано та перевірено. Виправлено критичну проблему зі створенням та перейменуванням файлів/папок з кириличними та іншими не-ASCII назвами (зокрема системними іменами "Нова папка" / "Новая папка" у Windows Explorer):
1. В оригінальній бібліотеці `libhaze` функція `ReadString` у `ptp_data_parser.hpp` некоректно виконувала `static_cast<char>(chr)` над UTF-16 символами, відкидаючи старший байт. Для кириличних символів (наприклад 'Н' = `0x041D`) це призводило до перетворення на неприпустимі керуючі символи (0x1D) замість дійсного UTF-8 (`\xD0\x9D`), через що файлова система Switch відхиляла створення папки з помилкою `FsError_InvalidCharacter`.
2. Реалізовано повноцінне декодування UTF-16 у UTF-8 у `ReadString` (`ptp_data_parser.hpp`) та кодування UTF-8 у UTF-16 у `AddString` (`ptp_data_builder.hpp`).
3. Додано секції 7 та 8 у `patch_libhaze.cmake`, піднято версію до `0.13.476`, успішно зібрано бінарник у WSL та пройдено всі тести.

## Попередній delivery: v0.13.475 — Full read-write support with commit for MTP Saves drive

Статус: реалізацію виконано та перевірено. Перетворено віртуальне MTP-сховище `Saves` з режиму read-only у повноцінний read-write режим:
1. Додано підтримку створення та запису файлів (`CreateFile`, `WriteFile`, `SetFileSize`, `OpenFile` з `FsOpenMode_Write`) з автоматичним комітом змін (`fsFsCommit`) у файлову систему збереження при завершенні запису.
2. Додано підтримку створення піддиректорій (`CreateDirectory`), видалення файлів і папок (`DeleteFile`, `DeleteDirectoryRecursively`) та перейменування (`RenameFile`, `RenameDirectory`) всередині змонтованих збережень.
3. Оновлено монтування сейвів у `FsSaveProxy`: тепер відкриття виконується у режимі читання-запису (з безпечним fallback у read-only для захищених системних сейвів).
4. Оновлено `GetFreeSpace` для відображення доступного місця та змінено відображення назви диска з `Saves (read-only)` на `Saves`.

## Попередній delivery: v0.13.474 — Full MTP property handling, GetObjectPropDesc & SendObjectPropList fixes

Статус: реалізацію виконано та перевірено. Виправлено критичні збої та відмови в MTP-обробнику `libhaze`:
1. У `SendObjectPropList` (0x9808) замінено виклик помилки `ResultUnknownPropertyCode` на безпечне вичитування та обробку всіх типів властивостей об'єктів MTP (U8, U16, U32, U64, U128, String, масиви). Раніше будь-яка стандартна властивість від Windows Explorer (наприклад `StorageID`, `ObjectFormat`, `ParentObject`, `PersistentUniqueObjectIdentifier`), що надсилалася перед ім'ям файлу, спричиняла аварійний викид помилки та відмову створення папки.
2. У `GetObjectPropDesc` виправлено пропущений `break;` у switch після властивості `PersistentUniqueObjectIdentifier`, що спричиняло падіння у наступний `case ObjectSize` та надсилання пошкодженого блоку дескриптора властивості.
3. У `GetObjectPropList` додано підтримку `property_code == 0` (запит усіх властивостей згідно з MTP специфікацією).
4. У `SetObjectPropValue` додано підтримку встановлення імені об'єкта через властивість `PtpObjectPropertyCode_Name`.
5. Усі патчі додано в `patch_libhaze.cmake`, піднято версію до `0.13.474`, збірка та тести успішно пройдені.

## Попередній delivery: v0.13.473 — Installed Games save scanning & category listing fix

Статус: реалізацію виконано та перевірено. Виправлено відображення списку в `Saves -> Installed Games`: тепер меню надійно відображає всі встановлені на консолі ігри (аналогічно Tools Games через `nsListApplicationRecord` + `title::GetMetaEntries`), навіть якщо для них ще не було створено сейв на консолі або активний інший обліковий запис. Для кожної гри прив'язуються наявні активні сейви, або створюється запис гри для швидкого створення чи відновлення бекапів. Категорії "Видалені ігри" та "Резервні копії" при відсутності записів коректно показують стан "Empty...". У `PromptSaveTypeOptions` додано можливість відновлення бекапів безпосередньо для встановленої гри без наявності попереднього сейву.

## Попередній delivery: v0.13.472 — MTP folder and file creation storage_id fix

Статус: реалізацію виконано та перевірено. Виправлено критичний баг у протоколі MTP/PTP бібліотеки `libhaze`, через який Windows Explorer не міг створювати нові папки та файли ("Пристрій припинив відповідати, або його було відключено"). У відповідях `SendObjectInfo` (0x100C) та `SendObjectPropList` (0x9808) поле `storage_id` помилково заповнювалося `parentobj->GetObjectId()` (дескриптором об'єкта) замість `parentobj->GetStorageId()` (ідентифікатора сховища). Додано відповідні патчі в `patch_libhaze.cmake` та виправлено логування в `haze_helper.cpp`.

## Попередній delivery: v0.13.471 — Save categories hub & custom save backup search paths

Статус: реалізацію виконано та перевірено. Додано початкове меню категорій (`SaveHubMenu`) при вході у розділ Saves (Tools -> Saves та Main Menu -> Saves) з 3 пунктами: "Встановлені ігри" (Installed Games), "Видалені ігри" (Deleted Games), "Резервні копії" (Backups). Додано підтримку налаштування додаткових користувацьких папок для пошуку резервних копій у Settings -> Saves -> Save Backup Search Paths із вибором папок через `filepicker::Menu`, збереженням у конфігураційний файл `[save_backup_paths]` та автоматичним скануванням цих папок у `CollectBackups` та `ReadBackupEntries`.

## Попередній delivery: v0.13.470 — raw DISA save restore, save discovery & MTP USER:/save

Статус: реалізацію виконано та перевірено. Додано підтримку відновлення запакованих/сирових DISA/DPFS сейвів (монолітні контейнери `000000000000001e`, `.disa`, `.bin`, дампи з DBI Explorer) шляхом прямого блокового запису у відповідний NAND BIS-розділ (`FsBisPartitionId_User` або `FsBisPartitionId_System`) за шляхом `/save/<save_data_id>` із відображенням прогресу. Розширено `CollectBackups` та `ReadBackupEntries` для виявлення сирових сейвів у каталогах бекапів. Додано дію "Restore save data" у File Browser для швидкого відновлення збережень з будь-якого носія (SD, USB HDD, мережа). Додано MTP-сховища `USER:/save` та `SYSTEM:/save` з підтримкою читання й запису.

## Попередній delivery: v0.13.469 — unified pending UI & updater work

Статус: відновлено збережений WIP поверх повної колишньої mainline. Залишено
новішу спільну реалізацію `gfx::ImageViewport` для Theme Creator і forwarder crop,
покращений File Viewer з вибором діапазону та діями над ним, іконки дій у sidebar/
popup/File Browser, а також Updater focus, Kefir update badge і reconnect. File Picker
використовує стабільний верхній слот header з відновленої mainline.

## Попередній delivery: v0.13.468 — cURL shutdown & shared handle serialization

Статус: інтеграцію відновлено поверх `v0.13.467`. Спільний синхронний дескриптор
`g_curl_single` серіалізовано через `g_mutex_single`; `curl::RequestShutdown()`
викликається на початку виходу з App і не дає почати нові transfer після запиту
зупинки. Очищення handle виконується під тим самим mutex.

## Попередній delivery: v0.13.467 — versioned HTTP User-Agent

Статус: реалізацію виконано та перевірено. Замінено застарілий downloader User-Agent `TotalJustice` на єдине спільне джерело `APP_USER_AGENT` (`Sphaira/<APP_VERSION>`) у `defines.hpp` та додано встановлення `CURLOPT_USERAGENT` до `MountCurlDevice::curl_set_common_options()`. Політику TLS, редиректи, автентифікацію, HTTP-семантику, UI, i18n та залежності залишено без змін. Gemini успішно пройшов `git diff --check` та WSL `ReleaseWithInstall` (`[100%] Built target sphaira_nro`). Очікується ручний Switch remote-mount smoke check.

1. Додати єдину константу `APP_USER_AGENT = "Sphaira/" APP_VERSION;` у `sphaira/include/defines.hpp`.
2. Видалити локальний `API_AGENT` у `sphaira/source/download.cpp` та використати `APP_USER_AGENT` у `SetCommonCurlOptions()`.
3. Додати `curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, APP_USER_AGENT)` у `MountCurlDevice::curl_set_common_options()` (`sphaira/source/utils/devoptab_curl_device.cpp`).
4. Оновити `upstream_audit.md` (пункти `2eabcec` / `3ef698b`), підняти версію до `0.13.467` у `sphaira/CMakeLists.txt`, синхронізувати living docs, пройти `git diff --check` та WSL `ReleaseWithInstall`.

## Попередній delivery: v0.13.466 — caller-selected header layout

Статус: реалізацію виконано та перевірено. `MenuBase::SetTitleSubHeading` отримав стабільний параметр `top_row`, тому шлях або довільна назва більше не стрибає між рядками залежно від ширини. Шляхи й назви рендеряться після версії у верхньому широкому слоті, а лічильники та короткі статуси лишаються внизу.

1. Додати `bool top_row = false` до `MenuBase::SetTitleSubHeading` та скидати scroll state при зміні слота або очищенні тексту.
2. Рендерити верхній слот після виміряного `v%s` і до межі fixed status area; лишити `ScrollingText` для переповнення.
3. Передати `top_row = true` лише caller-ам шляхів і довільних назв; залишити компактні summary на нижньому рядку.
4. Підняти версію до `0.13.466`, оновити living docs, пройти host tests, WSL `ReleaseWithInstall` та `git diff --check`.

## Попередній delivery: v0.13.465 — text editor multi-line editing

Статус: реалізацію виконано та перевірено. Реалізовано вибір діапазону рядків у режимі редагування через меню дій (`Select range` / `Clear selection`), під час активного вибору A = `Finish selection`, B = `Cancel selection`. Додано процесовий буфер обміну рядками (`s_line_clipboard`) для дій Copy, Cut, Paste below та Delete над виділеним діапазоном або активним рядком. Реалізовано чисті допоміжні функції `CommentIniLine` та `UncommentIniLine` у `text_helper.hpp` із збереженням відступів та додано пункт `Undo` й `Redo` у спливаючий список дій. Пройдено повний набір host unit tests (`tests/run.sh` - 13 suites, 752 declarations) та `git diff --check`.

1. Додати підтримку вибору діапазону рядків у `fileview::Menu` (`StartRangeSelection`, `FinishRangeSelection`, `CancelRangeSelection`, `ClearRangeSelection`) та оновити підказки кнопок футера (`Finish selection` / `Cancel`).
2. Створити процесовий буфер обміну рядками (`s_line_clipboard`) для `CopySelection`, `CutSelection`, `PasteBelow` та `DeleteLine` (із збереженням щонайменше 1 порожнього рядка).
3. Реалізувати чисті функції `CommentIniLine` та `UncommentIniLine` у `text_helper.hpp` з тестами в `test_text_helper.cpp` та підключити їх до дій `Comment` / `Uncomment`.
4. Оновити спливаюче вікно дій рядка (`ShowLineActions`) для динамічного показу дій над виділеним діапазоном або поточним рядком, включно з `Undo` та `Redo`.
5. Малювати виділення напівпрозорою фокусною смугою з alpha 0.35 (`ThemeEntryID_FOCUS`), зберігаючи синтаксичні кольори тексту.
6. Підняти версію до `0.13.465` у `sphaira/CMakeLists.txt`, оновити living docs, пройти перевірку host tests та `git diff --check`.

## Попередній delivery: v0.13.460 — text editor basics

Статус: реалізацію виконано та перевірено. Реалізовано перехід Edit → View на кнопку B
зі збереженням стану та підтвердженням Save/Discard/Cancel при виході з View; блокування
повторного wrap при утриманні Down/Up на межах документа; стабільний рендеринг виділеного
рядка INI з синтаксичним підсвічуванням та перемикання значень 0 ↔ 1 у `ToggleIniBoolean`.
До цього delivery також увійшов ще не закомічений ZL zoom chord: `ZL` + D-pad
або вертикальний стік масштабує текст і не запускає page up після zoom.
Пройдено host test suite (13 suites, 742 declarations), WSL ReleaseWithInstall
(`[100%] Built target sphaira_nro`) та `git diff --check`.

1. Додати `SwitchToViewMode()`: кнопка B в Edit перемикає у View без закриття меню;
   кнопка B у View показує діалог збереження для зміненого документа.
2. В Edit mode блокувати автоповтор wrap при утриманні кнопки на першому чи останньому
   рядку; скидати блокування при відпусканні кнопки.
3. Зберегти синтаксичні кольори та однаковий розмір шрифту для виділеного рядка INI.
4. Розширити `ToggleIniBoolean()` для перемикання 0 ↔ 1 із тестами в `test_text_helper.cpp`.
5. Підняти версію до `0.13.460`, оновити living docs, пройти тести та збірку.

## Попередній delivery: v0.13.458 — Homebrew settings & search paths

Статус: реалізацію виконано та перевірено. Додано виділену категорію
`Homebrew` у Settings одразу після `General`, куди перенесено `Forwarders`,
`Homebrew App Store` та `Replace hbmenu on exit` без дублювання. Додано
менеджер `Homebrew Search Paths` для додавання лише microSD-папок, збереження
користувацьких шляхів у конфіг, їх перегляду та видалення з підтвердженням,
із негайним оновленням переліку Homebrew (системний шлях `/switch` залишається
незмінним та прихованим від конфігу). Оновлено 13 локалізацій (без `ru.json`).
Пройдено валідацію JSON, host test suite (13 suites, 742 declarations) та
`git diff --check`.

1. Додати окрему категорію `Homebrew` у Settings одразу після `General`.
2. Перенести до нової категорії `Forwarders` (з `Install`), `Homebrew App Store`
   (з `Software`) та `Replace hbmenu on exit` (з `General`).
3. Реалізувати `Homebrew Search Paths`: додавання лише SD-папок через FilePicker,
   збереження користувацьких шляхів у конфіг, показ списку та видалення через
   OptionBox із миттєвим `SignalChange()` / оновленням NRO.
4. Оновити 13 файлів локалізації в `assets/romfs/i18n/*.json` (крім `ru.json`).
   Підняти версію `0.13.457 → 0.13.458`, оновити living docs, перевірити валідність
   JSON, пройти host tests та `git diff --check`.

## Попередній delivery: v0.13.457 — text viewer viewport scrolling

Статус: реалізацію виконано та перевірено. У read-only text viewer Up/Down,
D-pad та обидва стіки зміщують вікно на один рядок одразу без затримок курсора;
streamed reader тримає буфер рядків наперед і плавно переходить між сторінками.
Збережено release-based L/R (сторінка), ZL/ZR (10 сторінок), L + right-stick zoom,
pinch zoom та неклікабельні footer hints. Пройдено host test suite (13 suites,
742 declarations), WSL ReleaseWithInstall (`[100%] Built target sphaira_nro`) та
`git diff --check`.

1. У read-only viewer відокремити cursor/editing semantics від прокрутки
   viewport: Up/Down, лівий і правий стіки зміщують вікно на один рядок одразу.
2. Для streamed reader тримати буфер щонайменше на один viewport попереду,
   обчислювати наступний page offset після одного видимого viewport і плавно
   переходити на нього на межі. Не індексувати весь файл і зберегти bounded
   cache.
3. Зберегти release-based L/R, ZL/ZR, L + right-stick zoom, one-finger swipe,
   pinch zoom і неклікабельні footer hints. Підняти `0.13.456 → 0.13.457`,
   оновити living docs і пройти host suite, WSL build та `git diff --check`.

## Попередній delivery: v0.13.456 — text viewer pager

Статус: реалізацію прийнято після Gemini junior-review. Контекстна дія працює
для кожного звичайного файла, а великі файли читаються ліниво малими
сторінками; коротке або помилкове читання завершує viewer через чинний error
box без повторного discovery того самого offset. Gemini пройшов `tests/run.sh`
(13 suites, 742 declarations), WSL `ReleaseWithInstall` (`sphaira_nro`) і
`git diff --check`. Потрібний Switch smoke-test пейджера та жестів.

1. Додати в File Browser один `View as text` для будь-якого звичайного файла.
   Залишити автоматичний View за known text extension, а інсталяцію, image,
   archive, NRO та file associations не змінювати.
2. Зберегти чинний in-memory editor лише для файлів до 4 MiB. Для більших
   відкрити read-only paged reader: тримати лише поточну і кілька наступних
   сторінок тексту, байтові offsets сторінок та невеликий chunk buffer; не
   читати або не індексувати весь файл наперед.
3. У read-only text view: Up/Down і обидва стіки рухаються рядком; `L`/`R`
   перегортають назад/уперед одну сторінку на release; `ZL`/`ZR` — десять.
   Утриманий `L` + правий стік змінює масштаб без випадкової дії від drift;
   release `L` гортає назад лише якщо L не був modifier. Підтримати pinch zoom
   через фактичний two-touch input. Ніякий paging/zoom footer hint не повинен
   спрацьовувати від touch, але scroll і pinch залишаються touch actions.
4. Змінювати масштаб у практичних межах, перебудовуючи viewport/page rows і
   зберігаючи поточну позицію документа настільки точно, наскільки дозволяє
   потоковий offset. Додати одну host-перевірку page boundaries/line stepping,
   підняти версію `0.13.455 → 0.13.456`, оновити task/plan/walkthrough та
   пройти host suite, WSL build і `git diff --check`.

## Попередній delivery: v0.13.455 — INI text viewer spacing

Статус: локальну причину накладання знайдено в `fileview::Menu::DrawText`: номер рядка виставляє NanoVG на 16 px, після чого ключ вимірювався цим самим розміром, але малювався в 18 px. Перед `gfx::textBounds` ключа відновлено 18 px, тому початок `=` і значення відповідає фактично намальованій ширині ключа.

1. Зберегти чинні кольори, INI parser, clipping і компонування gutter без нових UI-механізмів.
2. Встановити 18 px лише перед вимірюванням `key_str` у shared INI draw path.
3. Підняти версію `0.13.454 → 0.13.455`, оновити living docs і пройти `git diff --check`; target WSL `sphaira` успішний, але повний `ReleaseWithInstall` окремо блокується відсутньою Make-ціллю `sphaira/sphaira.elf` під час NRO-пакування.
4. На Switch відкрити `system_settings.ini` з ключем на кшталт `enable_send_rights_usage_status_request`; його значення має починатися після ключа без накладання.

## Попередній delivery: v0.13.454 — NSP install diagnostics

Статус: реалізацію виконано та перевірено. Діагностичні повідомлення встановлення NSP та перевірка версії HOS інтегровані в єдину спільну точку опису помилок `ui::GetResultDescription(Result)`. Gemini успішно виконав host checks (`test_version_compare` 34 checks, `tests/run.sh` all green), JSON parser валідацію всіх 14 мов і WSL `ReleaseWithInstall` (`[100%] Built target sphaira_nro`).

1. У `sphaira/source/ui/error_box.cpp` додати описи для `MAKERESULT(Module_Libnx, LibnxError_IncompatSysVer)`, `Result_StreamUnexpectedEof` та `Result_NspBadMagic`.
2. Для HOS несумісності відображати мінімальну версію 4.0.0 через `version::FormatPacked`, динамічну встановлену версію через `hats::getSystemFirmware()` та рекомендацію оновити системну прошивку.
3. Для `Result_StreamUnexpectedEof` та `Result_NspBadMagic` додати чіткі інструкції повторного копіювання/завантаження, не зачіпаючи стандартні файлові чи криптографічні помилки.
4. Додати локалізацію 3 ключів до всіх 14 JSON-файлів у `assets/romfs/i18n/` та перевірити їх валідність.
5. Розширити `tests/test_version_compare.cpp` перевіркою форматування 4.0.0 і підтвердити збірку в WSL.

## Попередній delivery: v0.13.454 — Homebrew multi-select actions

Статус: реалізацію прийнято після Gemini junior-review. Shared grid renderer
залишився єдиним джерелом візуальної семантики selection: List малює checkbox
у боковому gutter, а tile layouts — overlay. Gemini пройшов `tests/run.sh`
(усі 13 suite green, 734 header declarations), WSL `ReleaseWithInstall`
(`Built target sphaira_nro`) і `git diff --check`; лишився Switch smoke-test.

1. Повторно використати в `homebrew::Menu` чинну поведінку Games: `X` змінює
   вибір поточного NRO та переходить до наступного, `Y` інвертує вибір, `B`
   очищує вибір до виходу; не створювати окремий checkbox/layout механізм.
2. Передати `selected` до `grid::Menu::DrawEntry` і використати
   `DrawSelectionMark`, який уже малює checkbox у боковому gutter для List та
   позначку/overlay для плиткових макетів.
3. У Homebrew Options показувати число targets і масові `Star`/`Unstar` лише
   коли відповідна операція має роботу. `Delete` мусить вимагати підтвердження,
   обробляти кожен результат і після успіху перезчитувати список. Не діяти на
   синтетичному Kefir Updater stub.
4. Підняти версію `0.13.453 → 0.13.454`, оновити task/plan/walkthrough,
   додати найменшу потрібну перевірку, пройти host suite, WSL build і
   `git diff --check`; вручну перевірити X/Y, List/Grid/HB Menu та всі три
   контекстні дії на Switch.

## Попередній delivery: v0.13.453 — PFS0/NSP parser hardening

Статус: реалізацію прийнято після ручного Gemini junior-review циклу. Деталі
baseline-доказів і межі scope — у
[`pfs0_nsp_hardening_audit.md`](pfs0_nsp_hardening_audit.md). Зафіксовано exact
metadata reads, limits, checked arithmetic, bounded names і known-size bounds у
спільному PFS0 parser; невідомі streams лишаються підтриманими через штатний
`FsError_NotImplemented` size result.

1. `Nsp::GetCollections()` вимагає exact header/file-table/string-table reads,
   перевіряє всі metadata-derived allocation, offsets і `CollectionEntry` до їх
   publication, не змінюючи чинний chunk-aggregation `source::Stream::Read()`.
2. `pfs0.hpp` зберігає binary-layout asserts, caps `0xFFFF` files / 4 MiB
   string table, checked arithmetic, bounded NUL search і parsed known-size
   ends. Common source `GetSize()` передає file/NCA/buffer capacity у parser;
   лише `FsError_NotImplemented` означає unknown-size stream.
3. `tests/test_pfs0_nsp.cpp` покриває valid layout, short reads, hostile
   allocations, invalid/missing-NUL names, overflow і known-size overrun.
4. Gemini фактично виконав focused test (41 checks), `tests/run.sh` (`all green`),
   WSL `ReleaseWithInstall` (`[100%] Built target sphaira_nro`) і
   `git diff --check`. Senior review охопив parser, всі GetSize adapters і
   PFS0/NCA callers.
5. Версію піднято `0.13.452 → 0.13.453`; зміна parser/test/document-only, тому
   Switch hardware/manual check не потрібний.

## Попередній delivery: v0.13.452 — відновлення loader thread affinity перед NRO

Статус: реалізацію, senior review і програмну верифікацію завершено. У `loadNro()` безпосередньо перед trampoline відновлюється фактична process core mask: `svcGetInfo(InfoType_CoreMask, CUR_PROCESS_HANDLE)` → `svcSetThreadCoreMask(CUR_THREAD_HANDLE, -1, core_mask)`. Будь-яка помилка проходить через `diagAbortWithResult`; `highest_cpu_id = 3` не перетворюється на жорстку mask. Gemini успішно виконав WSL `ReleaseWithInstall` (`[100%] Built target sphaira_nro`) і `git diff --check`; версію піднято до `0.13.452`. Залишається лише апаратний smoke-test.

1. Перевірити на Switch старт NRO з Homebrew Menu та повернення/перезапуск через `envSetNextLoad()`.
2. Очікуваний результат: NRO запускається і повторно запускається без зависання, крашу або зміни UI/CPU-налаштувань.
3. Якщо запуск переривається, зафіксувати точний Horizon Result з abort screen; це симптом для наступного bounded fix.

## 0. v0.13.451 — custom NRO search paths

Статус: реалізацію та всі перевірки завершено. `/switch` лишається незмінним default root; додаткові native-SD roots зберігаються в `[homebrew_paths]`, валідні absolute paths нормалізуються та дедуплікуються, а сканування custom roots має глибину 2. Пройдено `tests/run.sh` (154 checks у `path_util`), JSON validation, WSL `ReleaseWithInstall` і `git diff --check`. Російську локаль свідомо виключено з цього delivery до окремого i18n pipeline.

1. Повторно використано `minIni`, event-based Homebrew refresh і `path_util.hpp`; не додано subsystem, dependency, network filesystem або конфігурацію для `/switch`.
2. `NormalizeSearchPath` відхиляє не-absolute, `.`/`..`, backslash, `:`, control bytes, root, `/switch` і довжину `>= FS_MAX_PATH` до будь-якого створення `fs::FsPath`; неіснуючі roots пропускаються під час scan.
3. File Browser дозволяє add/remove лише для дозволеного SD-каталогу; remove має підтвердження, а успішна зміна конфігу надсилає `homebrew::SignalChange()`.
4. `/switch` сканується чинним `nro_scan`, кожен custom root — `nro_scan_depth(..., 2)`; NRO entries дедуплікуються за canonical path, а empty Homebrew list безпечний.

## 0.1. v0.13.449 — NFS phase 1 (read-only source)

Статус: програмну реалізацію та senior review завершено; host suite (`nfs_url`: 194 checks), dead-symbol guard, WSL `ReleaseWithInstall` і `git diff --check` пройдено 2026-08-14. Апаратна перевірка на реальній Switch залишається відкритою.

1. Підключено статичний `ITotalJustice/libnfs@65f3e11` через `FetchContent`; dependency documentation, examples і tests вимкнено.
2. Додано read-only `devoptab_nfs.cpp`, що належить спільному `MountNetworkDevice2()`, використовує `nfs_parse_url_dir()`, RAII cleanup та повертає `EROFS` для мутацій.
3. Додано host-testable NFS URL validator із canonical lowercase scheme, hostname/IPv4 і port validation, збереженням nested export path, лімітом `FsPath`, відхиленням credentials, traversal, query/fragment, IPv6 та небезпечного percent encoding.
4. NFS підключено до File Browser, source picker і Settings; на кожному маршруті збережено read-only flag, а невалідні saved URLs відсіюються до копіювання у фіксовані `FsPath`.
5. Оновлено англійську та українську локалізації, додано 194 host checks і завершено software verification. Наступний крок — browse/read/copy-from-NFS smoke test на Switch.

## 0.2. v0.13.448 — очищення екранних NTP-сповіщень

Статус: реалізацію завершено; прибрано тимчасові діагностичні tooltip-и та нелокалізоване сповіщення UI refresh; збережено повне логування `[NTP]` та єдине локалізоване сповіщення "Clock synced" для фактично оновленого User Clock; пройдено WSL `ReleaseWithInstall`, `git diff --check`, оновлено living docs.

1. У `sphaira/source/ntp.cpp` вилучено `SHOW_NTP_PROGRESS_TOOLTIPS` та виклик `App::Notify` із `ReportSyncStage()`, зберігши запис усіх етапів і результатів у `[NTP]` лог.
2. Прибрано нелокалізоване сповіщення `App::Notify("NTP: UI clock refreshed", ...)` з блоку оновлення UI.
3. Збережено виклик локалізованого `App::Notify("Clock synced"_i18n)` як єдиного екранного сповіщення, що чергується в UI-потоці через `evman::push` виключно після успішного live-запису User Clock та `__libnx_init_time()`.
4. Гарантовано відсутність сповіщень на шляху, коли зміщення менше за `MIN_CORRECTION_SECONDS` (час уже точний), та на fallback-шляху `used_fallback` (коли увімкнено automatic correction і діє процесний offset).
5. Піднято `sphaira_VERSION` до `0.13.448`, оновлено `task.md`, `plan.md`, `walkthrough.md`.

## 0.3. v0.13.447 — upstream-equivalence hardening: безпечне ZIP extraction

Статус: реалізацію, валідатор і тести завершено; пройдено `tests/run.sh` (106 checks у `path_util`), WSL `ReleaseWithInstall` (`[100%] Built target sphaira_nro`), `git diff --check`, враховано senior review (захист `number_entry` overflow та оновлення коментаря санітизації), піднято версію до `0.13.447` і створено сфокусований коміт.

1. Досліджено всі 11 викликів `thread::TransferUnzipAll()` та виправлено root cause у спільній функції, захистивши всі операції розпакування (Appstore, direct-link/GitHub downloads, cheats, firmware, File Browser, save restore, translations).
2. Додано inline helper `path::IsSafeArchiveEntry(std::string_view)` у `sphaira/include/path_util.hpp`, який валідує відносні шляхи й каталоги, відхиляє порожні імена, початковий `/`, backslash `\`, керуючі символи (< 0x20, DEL 0x7F), `:` (захист від device/scheme) та `.`/`..` компоненти шляху, зберігаючи валідні файли з крапками (`.config`, `..data`, `file.name`).
3. У першому проході `thread::TransferUnzipAll()` додано перевірку `info.size_filename` на відповідність буферу та `strlen`, валідацію `path::IsSafeArchiveEntry()`, перевірку сумарної довжини шляху призначення з `base_path` на ліміт `sizeof(fs::FsPath)`, а також захист від переповнення `s64` для сумарного `uncompressed_size` та `ginfo.number_entry`.
4. Збережено чинну HOS character sanitization для безпечних неструктурних символів (`*`, `?`, `"`, `<`, `>`, `|`), чинні filter callbacks і progress semantics; не додавалося SD-специфічних перевірок вільного місця у спільний helper.
5. Розширено host-тести `tests/test_path_util.cpp`, пройдено `tests/run.sh`, WSL `ReleaseWithInstall` та `git diff --check`.
6. Піднято `sphaira_VERSION` з `0.13.446` до `0.13.447`, оновлено `task.md`, `plan.md`, `walkthrough.md`, `upstream_audit.md` і створено один сфокусований коміт.

## 0.1. v0.13.446 — NTP через системну automatic correction

Статус: реалізацію та програмні перевірки завершено (WSL `ReleaseWithInstall` та `git diff --check` успішно пройдено 2026-08-13, версію піднято до v0.13.446). Апаратна перевірка на реальній Switch залишається відкритою.

1. Вилучено некоректний виклик `DisableAutomaticCorrection()`.
2. Залишено спроби запису User Clock та Network Clock через `time:su` і `time:s`. Після спроб запису виконується повторне зчитування User Clock: якщо час збігається з NTP (до 2 с), це вважається негайним live-синхроном (скидається process offset, оновлюється libnx time, виводиться "Clock synced").
3. Якщо User Clock все ще відхиляється, через `set:sys` зберігається `NetworkSystemClockContext` та вмикається `setsysSetUserSystemClockAutomaticCorrectionEnabled(true)`.
4. У tooltip/log для fallback шляху виводиться `automatic correction enabled; reboot required to update HOS User Clock`, при цьому не показується "Clock synced" і не стверджується live-зміна HOS User Clock. Process offset Sphaira зберігається для миттєвого відображення часу в додатку.
5. Пройдено WSL `ReleaseWithInstall`, `git diff --check`, піднято версію до `0.13.446`, оновлено living docs.

## 0.1. v0.13.445 — NTP User Clock через set:sys

Статус: реалізацію та програмні перевірки завершено (WSL `ReleaseWithInstall` та `git diff --check` успішно пройдено 2026-08-13, версію піднято до v0.13.445). Апаратна перевірка на реальній Switch залишається відкритою (hardware verification remains pending: миттєве оновлення годинника Sphaira, NTP trace та збереження часу після перезапуску).

1. У `SetSystemTime()` зберегти чинні спроби `time:su` і `time:s` для live User Clock; після їхньої відмови спробувати штатний `set:sys` IPC: отримати standard steady-clock time point, утворити `TimeSystemClockContext { NTP - steady, steady }`, записати User і Network context та вимкнути automatic correction у `set:sys`. Запис у `errors.txt` виконувати лише при відмові `set:sys`, щоб успішний fallback не залишав хибних записів про помилки.
2. Не вважати `set:sys` live-успіхом без перевірки: оновити часовий display Sphaira на NTP offset одразу в поточному процесі, а persisted context лишити джерелом правильного часу після перезавантаження HOS.
3. Залишити `SHOW_NTP_PROGRESS_TOOLTIPS = true` і показати відкриття `set:sys`, зчитування steady clock, запис кожного context, результат automatic-correction та підсумок.
4. Пройти WSL `ReleaseWithInstall` і `git diff --check`, підняти версію до `0.13.445`, оновити living docs і виконати ручну перевірку на Switch.

## 0.1. v0.13.444 — видимий NTP diagnostic trace

Статус: реалізацію завершено; WSL ReleaseWithInstall пройшов 2026-08-13, версію піднято до v0.13.444. Потрібна ручна перевірка на Switch.

1. Лог v0.13.443 показав, що NTP-відповідь отримано, але обидва шляхи `time:su` та `time:s` відхилили User system clock з `0x00000274`.
2. Додати тимчасовий `ReportSyncStage`: він записує `[NTP]`-рядок і thread-safe tooltip зліва. Прапор `SHOW_NTP_PROGRESS_TOOLTIPS` залишити `true` до завершення апаратної діагностики.
3. Покрити tooltip-ами кожен етап: мережу, DNS, socket/send/receive, валідну відповідь, читання й offset User Clock, кожну операцію `time:su` і `time:s`, fallback, UI refresh і фінальний Result.
4. Пройдено WSL `ReleaseWithInstall` та `git diff --check`; версію піднято до `0.13.444`.

## 0.1. v0.13.443 — запис NTP-часу через `time:su`

Статус: реалізацію завершено; WSL ReleaseWithInstall пройшов 2026-08-13, версію піднято до v0.13.443. Потрібна ручна перевірка на Switch.

1. Логи з HOS 20.5.0 показали, що NTP-відповідь надходить, але `time:s` відхиляє і вимкнення automatic correction, і запис User system clock з `0x00000274`.
2. `SetSystemTimeWithService` виконує чинну спробу запису для одного сервісу; `SetSystemTime` спершу викликає її для `time:su`, а потім для `time:s` лише якщо User system clock не було записано.
3. Невдале вимкнення automatic correction лишається best-effort. Успіх визначає лише запис User system clock; Network system clock лишається best-effort.
4. За повної невдачі в `errors.txt` записуються Result обох сервісів, що робить наступний апаратний тест діагностичним.
5. Пройдено WSL `ReleaseWithInstall` та `git diff --check`; версію піднято до `0.13.443`.

## 0.1. v0.13.442 — усунення крашу File Browser при завантаженні асоціацій

Статус: реалізацію завершено; WSL ReleaseWithInstall пройшов 2026-08-13, версію піднято до v0.13.442.

1. Причина крашу: під час додавання багатьох асоціацій запусків зростання `std::vector<FileAssocEntry>` викликало реалокацію вектора й копіювання великих об'єктів `FileAssocEntry` (кожен з яких містить 0x301-байтний буфер `fs::FsPath`), що призводило до переповнення стеку / крашу в `memset`.
2. Виправлення: додати static-функцію `CountAssocEntriesPath` і перед додаванням асоціацій обчислити максимальну кількість `.ini` файлів-кандидатів у `romfs:/assoc/` та `paths::ASSOC`, після чого підготувати ємність вектора через `m_assoc_entries.reserve(...)`.
3. Успішно виконано збірку WSL `ReleaseWithInstall` та перевірено `git diff --check`. Версію піднято до `0.13.442`.

## 0.1. v0.13.441 — захист звичайного хрому UI Sphaira

Статус: реалізацію завершено; WSL ReleaseWithInstall пройшов 2026-08-13, версію піднято до v0.13.441.

1. У `App::Draw()` (`sphaira/source/app.cpp`) перенесено виклик `DrawChrome()` після відмальовки всіх немодальних віджетів і контенту, щоб елементи звичайного контенту не перекривали лінії заголовка та футера.
2. Додано метод `IsModal()` у `Widget` та перевизначено для модальних діалогів (`OptionBox`, `PopupList`, `ProgressBox`, `ErrorBox`, `HoldConfirmBox`, `HoldOkBox`, `KefirChangelogBox`), щоб вони малювалися поверху хрому та залишали ефект затемнення екрана.
3. Оновлено `WantsChrome()` у `fileview::Menu` (`file_viewer.hpp`), щоб повертати `!m_fullscreen`, вмикаючи стандартний хром у неповноекранному перегляді та вимикаючи у повноекранному.
4. Оновлено `ImageBounds()` у `file_viewer.cpp` з використанням констант `layout::ContentBand()`.
5. Пройдено збірку WSL `ReleaseWithInstall` та `git diff --check`, піднято версію до `0.13.441`.

## 0.1. v0.13.440 — інтерактивне керування чергою інсталяції (Skip / Cancel)

Статус: реалізацію та тести завершено; WSL ReleaseWithInstall пройшов 2026-08-12, версію піднято до v0.13.440.

1. У стані `Installing` призначити кнопку `B` на пропуск поточного пакета (`Skip package`), а `X` — на скасування всієї черги (`Cancel queue`).
2. Обидві дії показують явний діалог підтвердження через `App::Push<OptionBox>` із варіантами `No` (типовий) та `Yes`.
3. Підтвердження пропуску (`B` -> `Yes`) перериває встановлення лише поточного пакета через `m_skip_requested` та `m_cancel_event`, записує пакет як `Skipped` у статистиці та підсумку (без помилки в error list), після чого автоматично скидає сигнал переривання й переходить до наступного пакета черги.
4. Підтвердження скасування (`X` -> `Yes`) викликає `CancelSession()`, перериває інсталяцію зі збереженням уже встановлених пакетів і завершує сеанс.
5. Логіка уніфікована та працює ідентично для обох режимів черги: USB (`ThreadFunction`) та локальних файлів (`LocalThreadFunction`).
6. Додано переклади EN/UK для нових текстів підтверджень, додано host unit-тест `test_queue_outcome.cpp`, пройдено всі перевірки та піднято версію до `0.13.440`.

## 0.1. v0.13.439 — миттєва NTP-синхронізація

Статус: реалізацію та senior-review завершено; WSL ReleaseWithInstall пройшов 2026-08-10, версію піднято до v0.13.439. Залишилася ручна перевірка на Switch.

1. Залишити один фоновий worker і чинні NTP fallback-сервери. Першу спробу
   виконувати без стартової 10-секундної паузи; `Start()` для вже активного
   worker має лише розбудити його, а не створювати другий thread.
2. Після отримання NTP часу нічого не робити при різниці меншій за чинні 2 с.
   Якщо корекція потрібна, вимкнути live automatic-correction flag і записати
   user clock; успіх network clock не може маскувати помилку user clock.
3. Після успішного запису через чинний thread-safe `evman::FunctionalEventData`
   перейти на UI-потік, повторно ініціалізувати часову базу libnx і показати
   локалізований `Clock synced`. Це має одразу оновити всі чинні виклики
   `std::time()` без окремого offset-cache або змін у кожному caller.
4. Не показувати toast для вже точного годинника, відсутньої мережі чи помилки;
   зберегти чинний retry/backoff і діагностичні логи.
5. Перевірити WSL `ReleaseWithInstall`, підняти версію до `0.13.439`, оновити
   living docs і створити focused commit.

## 0.1. v0.13.438 — перемикач USB 3.0

Статус: реалізацію та senior-review завершено; EN/UK JSON і WSL
`ReleaseWithInstall` пройшли 2026-08-10, версію піднято до `v0.13.438`.
Залишилася ручна перевірка на Switch.

1. У `Tools → Налаштування кефіру` показувати один `USB 3.0` On/Off-рядок.
   Лише точне `u8!0x0` означає Off; відсутній файл або ключ означає типовий On.
2. Після успішного запису `[usb] usb30_force_enabled` commit-ити SD до показу
   діалогу. Помилка запису показує чинний error box і не пропонує reboot.
3. Повідомити, що зміна набуде чинності лише після перезавантаження, та дати
   вибір `Пізніше` / `Перезавантажити` через чинний forced-reboot шлях.
4. Перевірити EN/UK JSON, WSL `ReleaseWithInstall`, підняти версію до
   `0.13.438`, оновити living docs і створити focused commit.

## 0.2. v0.13.436 — незалежний скрінсейвер

Статус: реалізацію та senior-review завершено; host-тести й WSL
`ReleaseWithInstall` пройшли 2026-08-10. Відкрита лише ручна перевірка на Switch.

1. Не створювати окремий render thread: NanoVG/deko3d і HID лишаються в UI
   thread. Натомість зробити активний шлях неблокуючим: семплер графіка не
   потребує `m_mutex`, а prompt/snapshot читаються через `mutexTryLock` із
   поверненням останнього готового `SaverInfo`, якщо worker коротко зайнятий.
2. Прибрати `App::SetBlankBrightness()` з кожного кадру правого стіка. Тримати
   нове значення локально, одразу застосовувати його через `lbl*`, а INI
   записувати один раз тільки після виходу worker зі стану `Installing`; preview
   без активного запису може зберегти значення при закритті.
3. Залишити графік UI-власністю й додавати семпл кожні 0,5 с з атомарних
   `m_total_read`/`m_total_write`. Нульовий приріст є валідним нульовим семплом,
   а не причиною зупинити ані графік, ані скрінсейвер.
4. Додати в `SaverInfo` явний finished-стан. У `Summary` графік не малювати, а
   на його місці незалежно від `saver_fields` показувати локалізоване
   `Finished` / `Finished with errors`.
5. Додати одну INI-опцію timeout у секцію чинних screen-off налаштувань:
   `Off` за замовчуванням і короткий набір практичних preset-ів. Таймер працює
   лише у `State::Installing`, скидається будь-якою кнопкою, touch або рухом
   стіка та після ручного/автоматичного wake.
6. Виділити лише мінімальну чисту timeout-перевірку, потрібну host-тесту; не
   додавати scheduler, thread class чи залежність. Прогнати host-тести та відому
   WSL-збірку.
7. Після senior review підняти версію за чинною схемою, оновити
   `task.md`/`plan.md`/`walkthrough.md` і створити focused commit лише з
   screensaver delivery, зберігши всі наявні незакомічені зміни інших задач.

## 0.3. v0.13.437 — Text Viewer / Editor UX

Статус: реалізацію та corrective senior-review завершено; host-тести й WSL
`ReleaseWithInstall` пройшли 2026-08-10, версію піднято до `v0.13.437`.
Відкрита лише повторна ручна перевірка на Switch.

1. Додати одну спільну перевірку відомих текстових форматів і викликати її з
   головної дії `A` та контекстного меню File Browser. Спеціальні типи
   (`nro`, install, image, zip) залишити пріоритетними.
2. Передати text viewer неволодіючий `fs::Fs*` поточного `FsView` і окремий
   writable-прапорець. Нижній File Browser живе довше за pushed viewer, тому
   pointer безпечний; image-viewer і його `FsNativeSd` не зливати з цим шляхом.
3. Обробляти Open/GetSize/Read як одну fallible операцію: при будь-якій помилці
   не створювати порожній editable buffer, а відкласти показ Result до першого
   Update, коли viewer уже лежить у стеку UI.
4. Зберігати точний baseline останнього успішного Save. Після кожної зміни,
   Undo і Redo обчислювати dirty як `BuildText() != saved_text`; успішний Save
   оновлює baseline і очищає історію.
5. Не писати поверх оригіналу: створити sibling temp, повністю записати його,
   перейменувати оригінал у recovery backup, temp — в оригінал і відновити
   backup при помилці. Невдалий Save повертає failure, не закриває editor і не
   губить buffer; read-only джерела взагалі не отримують write actions.
6. Go to line затискає номер до фактичного діапазону та викликає
   `List::EnsureVisible`. Insert спочатку відкриває keyboard і додає новий
   рядок лише після підтвердження, як у перевіреному upstream UX.
7. Розділити File Viewer на явні `View` і `Edit`: View не має курсора та не
   змінює файл; Edit зберігає чинні undo/redo/save і редагування всього рядка
   через Switch keyboard.
8. Правий стік прокручує viewport незалежно. Лівий стік і D-pad рухають курсор
   лише в Edit; `A` одразу викликає keyboard для вибраного рядка без
   проміжного popup.
9. Для INI мінімально підсвітити section/comment/key/value. Подвійний touch tap
   по рядку з boolean RHS безпечно перемикає лише окремий токен `true` або
   `false`, не чіпаючи коментарі чи частини інших слів, і створює undo snapshot.
10. Залишити обмеження редагування великих файлів, але дозволити їх перегляд без
   безконтрольного читання всього файла в RAM. Додати одну невелику host-перевірку
   чистої логіки розпізнавання/toggle.
11. Після review прогнати host-тести та відому WSL-збірку, підняти версію за
   чинною схемою, оновити `task.md`/`plan.md`/`walkthrough.md` і зробити focused
   commit тільки з цієї функції.
12. Окремими атомарними комітами зафіксувати незалежні стабілізаційні виправлення:
    teardown transfer UI, auto-detect формату HB-іконок і USBDS detach на HOS 22.5.

## 1. MTP Games: merged NSP core

1. Залишити `BuildNspEntries` канонічним шляхом окремого дампу, але прибрати
   припущення, що всі NCA одного `NspEntry` лежать в одному storage.
2. Додати один merged-builder для BASE, останнього встановленого UPD і всіх
   встановлених DLC. DataPatch не включати до merged-пакета без окремо
   погодженої семантики назви.
3. Не дублювати однакові NCA та rights ID; PFS0 має містити всі потрібні NCA,
   CNMT, ticket і certificate та читатися потоком без тимчасового файла.
4. Формувати ім'я `Назва [TitleID][B+U65536+9DLC].nsp`. Відсутні складові
   опускати: `[B]`, `[B+U65536]`, `[B+9DLC]`.
5. Залишити чисте форматування суфікса доступним host-тесту без Switch SDK.

## 2. MTP Games: структура диска

1. Корінь read-only диска містить лише `Merged` і `Separate`.
2. `Merged` містить по одному об'єднаному NSP на встановлену гру.
3. `Separate/<Game [TitleID]>` містить наявні окремі BASE/UPD/DLC NSP через
   чинний `BuildNspEntries`.
4. Усі write/create/delete/rename операції залишаються забороненими; відкриті
   transfer handles мають переживати очищення кешу.
5. Не торкатися наявних незакомічених змін у forwarder-editor і `tests/run.sh`.

## Паралельний запит: TICO launchers

1. Повторно використати чинний механізм `assets/romfs/assoc/*.ini`: окремі
   TICO-асоціації завантажуються лише коли відповідний NRO реально існує у
   `/tico/cores`.
2. Додати одне необов’язкове поле фіксованого аргументу асоціації. Воно потрібне
   лише Gambatte (`gb`, `gbc`) і Genesis Plus GX (`genesis`, `master-system`,
   `game-gear`, `sega-cd`) та має однаково працювати для запуску і форвардера.
3. Один формат підпису використати в обох меню: спочатку RetroArch, потім TICO,
   усередині — назви ядер. Не змінювати загальний `PopupList` і не додавати
   залежностей.
4. Розпізнавати TICO-назви каталогів `sega-cd`, `fbneo`, `naomi`, `naomi2` та
   `atomiswave`; розширення брати з установлених ядер і чинних RetroArch INI.
5. Не перезаписувати незакомічені зміни delivery `0.13.432`, особливо у
   `filebrowser_forwarder.cpp`, `plan.md`, `task.md` та версії.

Реалізовано у `v0.13.433`: 17 конфігурацій покривають 13 установлених ядер,
спільний шлях аргументів працює для запуску, архівів і форвардерів; host-тести
та WSL-збірка пройдені. Залишилась апаратна перевірка на Switch.

## 3. Create repack — окремий етап після MTP

1. Додати `Create repack` у `Tools → Games → Game Actions`.
2. В окремому sidebar-вікні показувати лише фактично встановлені BASE, UPD і DLC;
   доступні компоненти за замовчуванням увімкнені, порожній вибір не запускає запис.
3. Розширити чинний merged NSP-builder прапорами вибору та повторно використати
   `NspSource` і `dump::Dump`, без нового формату чи проміжних файлів.
4. Результат записувати одним NSP у `/games` із погодженою схемою назви.
5. LayeredFS винести в наступний етап: опцію не показувати, доки немає коректної
   перебудови Program NCA, хешів і CNMT.

## 4. Верифікація і delivery

### UI-косметика

Прибрати порожнє посилання Progress з обох шапок вебсервера; на DBI-екрані USB-стан показувати над інструкцією, а Applet Mode — окремим вузьким текстовим блоком; перед NAND/SD-значеннями лишити видимий відступ. Host-тести та WSL-збірку пройдено.
1. Прогнати host-тести у WSL.
2. Зібрати `cmake --build --preset ReleaseWithInstall` у WSL.
3. Підняти версію до `0.13.435`, оновити `task.md`, `plan.md`,
   `walkthrough.md` і створити focused commit лише з прийнятими змінами.
4. Залишити hardware-gate відкритим до копіювання обох типів NSP на ПК і
   перевірки встановлення на Switch.

## 5. Закрити hardware-gates останніх delivery

1. Перевірити керування скрінсейвером у `v0.13.430`: обидва стіки, межі
   екрана, збереження яскравості та пробудження.
2. Перевірити чергу встановлення й скрінсейвер у `v0.13.429`: проєкцію
   NAND/SD без перекриття хедера, R/W-графік і preview.
3. Пройти USB-матрицю: DBI backend, Awoo/TinFoil і GoldLeaf v0.10+.
4. Повторити MTP smoke-test: лістинг телефона, перепідключення кабелю та
   встановлення NSP.

Результати ручних перевірок записувати в `tests.md`.

## 6. Наступні функціональні задачі

1. DBI UI: динамічний рядок журналу та наочний `ReviewQueue` із
   сегментованими NAND/SD-смугами.
2. Games: dump/verify/read-only mount, save integration і ticket details.
3. Network sources: NFS read-only завершено у `v0.13.449`; SFTP — лише після окремого погодження протоколу й UX.

Вбудований player залишається замороженим до окремого рішення.

## Правило завершення

Задача закривається після автоматичної перевірки збірки; hardware-задача —
лише після результату з реальної Switch у `tests.md`.
