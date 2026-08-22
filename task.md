# Активні задачі

Актуальний delivery — **v0.13.529**. Завершені задачі збережено в
[`archive/task_v0.13.249-v0.13.430.md`](archive/task_v0.13.249-v0.13.430.md)
та [`archive/task_archive.md`](archive/task_archive.md). Порядок виконання —
у [`plan.md`](plan.md), результат останнього delivery — у
[`walkthrough.md`](walkthrough.md).

## Поточний delivery: v0.13.529 (Forwarder Editor: List Null Pointer Safety & D-Pad Focus Transitions)

- [x] `CRASH-LOG-TRACEBACK-529` — проведено аналіз crash-звітів `01787411683_03db12780bd84000.log`, `01787411672_03db12780bd84000.log` та `01787411496_03db12780bd84000.log` на підключеній SD-картці Switch (диск `F:`). З'ясовано, що у `sphaira` на адресі `PC = sphaira + 0xf08f4` (`sphaira::ui::List::OnUpdateGrid`) відбувався `Data Abort` на нульовій адресі `0x0000000000000000` через розіменування `controller` (`ldr x1, [x24]`), коли `Forwarder Editor` викликав `m_list->OnUpdate(nullptr, ...)` під час фокусу на іконці.
- [x] `LIST-NULL-SAFETY-529` — у `sphaira/source/ui/list.cpp` додано повний захист покажчиків `controller` та `touch` у `OnUpdateGrid`, `OnUpdateHome`, `StepFling` та `OnTouchScroll`, що запобігає збоям при виклику з `nullptr`.
- [x] `FORWARDER-DPAD-FOCUS-529` — у `sphaira/source/ui/forwarder_editor.cpp` реалізовано плавний перехід фокусу за допомогою D-Pad між іконкою та списком налаштувань (натискання `DOWN` або `RIGHT` переводить фокус у список, `LEFT` або `UP` з першого рядка повертає на іконку).
- [x] `TEST-LIST-NULL-SAFETY-529` — створено host unit-тест `tests/test_list_null_safety.cpp` (6 перевірок), що тестує виклики `List::OnUpdate` з `controller=nullptr` та різними станами `TouchInfo`.
- [x] `DOCS-BUMP-529` — версію піднято до `0.13.529` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `plan.md`, `task.md`, `walkthrough.md`, пройдено всі 24 набори unit-тестів у WSL.

## Попередній delivery: v0.13.528 (HBL Loader Fix: Exact NRO Segment Sizing, Applet/Application Mode Detection & Heap Restoration)

- [x] `CRASH-LOG-TRACEBACK-528` — проведено аналіз crash-звітів `01787404697_010000000000100d.log` (Album mode) та `01787404688_03db12780bd84000.log` (Forwarder mode). З'ясовано подвійну проблему: (1) в режимі Альбому лоадер жорстко передавав `AppletType_SystemApplication` замість `AppletType_LibraryApplet`, через що `NX-Activity-Log` намагався виділити 64.5 МБ пам'яті в 32-мегабайтному пулі аплету; (2) у форвардері функція `calculateMaxHeapSize` безумовно віднімала 96 МБ (`size -= 0x6000000`), що урізало купу до ~100 МБ і викликало збій `dc civac` на адресі `0x25fb8f000`.
- [x] `HBL-APPLET-APPLICATION-DETECT-528` — у `hbl/source/main.c` додано `getIsApplication()` (через `svcGetInfo(..., InfoType_IsApplication)` та `pm:shell`) і `getIsAutomaticGameplayRecording()` (через `nsGetApplicationControlData`), що динамічно налаштовує `AppletType_LibraryApplet` або `AppletType_SystemApplication` і віднімає 96 МБ лише за реальної наявності `video_capture == 2`.
- [x] `HBL-NRO-READ-EXACT-528` — у `hbl/source/main.c` реалізовано детерміноване покрокове зчитування NRO: окремо `NroStart` (16 байт), `NroHeader` (112 байт) та дані тіла `rest_size = header->size - 0x80`. Додано явне обнулення пам'яті BSS (`memset`) перед відображенням `svcMapProcessCodeMemory`, усунуто проникнення RomFS у BSS та забезпечено коректне розташування `OverrideHeap`.
- [x] `SPHAIRA-NVEXIT-CLEANUP-528` — у `sphaira/source/main.cpp` у функцію `userAppExit()` додано явний виклик `nvExit()` для чистого звільнення ресурсів драйвера Tegra та відображення відеопам'яті перед ланцюговим запуском наступного NRO.
- [x] `TEST-HBL-NRO-READER-528` — оновлено host unit-тест `tests/test_hbl_nro_reader.cpp` (528,394 перевірки), що симулює NRO із 12 МБ RomFS та перевіряє захист пам'яті BSS, купи та коректність розрахунку розміру пам'яті.
- [x] `DOCS-BUMP-528` — версію піднято до `0.13.528` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `plan.md`, `task.md`, `walkthrough.md`, пройдено всі 23 набори unit-тестів у WSL.

## Попередній delivery: v0.13.527 (Software Menu Visual Separation: Dedicated Bottom Network Section)

- [x] `SOFTWARE-SECTION-HEADER-527` — у `BuildSoftwareItems()` в `settings_menu.cpp` перенесено `Network Downloads` та `Custom Link` у самий кінець списку, додано розділювач `MakeHeader("NETWORK DOWNLOADS")` з лінією розмежування (HR); у `DrawActionListItem` реалізовано рендеринг заголовка секції, а в `SoftwareMenu::SetIndex` — пропуск неінтерактивних рядків через `ResolveItemIndex`.
- [x] `DOCS-BUMP-527` — версію піднято до `0.13.527` у `sphaira/CMakeLists.txt`, оновлено `plan.md`, `task.md`, `walkthrough.md`, пройдено всі 22 набори unit-тестів та успішно зібрано бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.526 (Menu Structure: Network Downloads & Custom Link to Software Menu)

- [x] `UPDATER-CLEANUP-526` — вилучено секцію `OTHER` (`Network Downloads` та `Custom Link`) з `BuildSectionedEntries` у `kefir_menu.cpp`; меню `Updater` сфокусовано виключно на оновленнях `KEFIR` та `FIRMWARE`.
- [x] `SOFTWARE-NETWORK-DOWNLOADS-526` — додано пункти `Network Downloads` (відкриває GitHub Downloader) та `Custom Link` (відкриває Direct Download діалог з підтримкою QR/Web) у `BuildSoftwareItems()` в `settings_menu.cpp`.
- [x] `TOOLS-MENU-DESCRIPTIONS-526` — оновлено описи карток `Updater` («Update Kefir and firmware packages.») та `Software` («App Store, GitHub downloads, DBI and mod utilities.») у `tools_menu.cpp`.
- [x] `DOCS-BUMP-526` — версію піднято до `0.13.526` у `sphaira/CMakeLists.txt`, оновлено `plan.md`, `task.md`, `walkthrough.md`, `README.md`, пройдено всі 22 набори unit-тестів.

## Попередній delivery: v0.13.525 (Graceful download cancellation & universal remote text/URL transfer)

- [x] `APPSTORE-CANCEL-GRACEFUL-525` — усунуто появу аварійного діалогу `SphairaError_AppstoreFailedZipDownload` при перериванні завантаження або видалення в AppStore: додано повернення та обробку `Result_TransferCancelled` із виведенням спокійного інформаційного діалогу.
- [x] `REMOTE-INPUT-MODULE-525` — створено загальний модуль `ui::remote_input` (`sphaira/include/ui/remote_input.hpp`, `sphaira/source/ui/remote_input.cpp`), веб-ендпоінти `/input`, `/remote-input`, `/input/config` та інтерфейс `REMOTE_INPUT_PAGE` для передачі тексту/посилань з телефону або ПК без ручного набору.
- [x] `DIRECT-DOWNLOAD-NRO-ZIP-525` — оновлено `Custom Link` / `Direct Download` у `ghdl.cpp` із підтримкою `PromptTextInput`, прямим завантаженням виконуваних `.nro` файлів у `/switch/` та розпакуванням `.zip` архівів у корінь.
- [x] `DOCS-BUMP-525` — версію піднято до `0.13.525` у `sphaira/CMakeLists.txt`, оновлено `plan.md`, `task.md`, `walkthrough.md`, `README.md`, пройдено всі 22 набори unit-тестів та успішно зібрано бінарник `sphaira_nro` у WSL.

## Поточний delivery: v0.13.524 (USB 3.0 indicator, waiting screen anti-overlap layout & screensaver clean title display)

- [x] `USB3-INDICATOR-524` — реалізовано векторну іконку тризуба USB `gfx::drawUsbIcon` у `sphaira/include/ui/nvg_util.hpp` та `sphaira/source/ui/nvg_util.cpp`; додано зчитування конфігурації `usb30_force_enabled` з `system_settings.ini` та апаратного лінку `usbDsGetSpeed`; додано відображення бейджа `[ USB 3.0 ]` у глобальному хедері `MenuBase::DrawChrome` поруч із MTP/FTP над смугами пам'яті; на екрані очікування черги встановлення `dbi_menu.cpp` додано статусний бейдж з іконкою USB та деталізацією швидкості (SuperSpeed 5 Gbps / High Speed 480 Mbps).
- [x] `DBI-WAITING-ANTI-OVERLAP-524` — оновлено розмітку екрана очікування в `dbi_menu.cpp`: основні інструкції розміщено на `y = 250.f`, реалізовано динамічний розрахунок вертикальних меж тексту через `nvgTextBoxBounds`; попередження про Applet Mode винесено в окрему виділену плашку строго під основним текстом (`std::max(bounds[3] + 35.f, 470.f)`), що повністю усуває взаємне налізання написів.
- [x] `SCREENSAVER-CLEAN-TITLE-524` — у `dbi_menu.cpp` (`ComputeSaverInfo` та живий перегляд) вилучено додавання технічних хешів/імен NCA/NCZ файлів (`.nca`/`.ncz`), забезпечено вивід чистої назви гри (`m_current_title` з фолбеком на ім'я файлу пакету) та збереження службових статусів оновлення БД; у `screensaver.cpp` розширено ширину треку до 840 пікселів та реалізовано адаптивне зменшення шрифту з лівим прив'язуванням для довгих назв, що виключає обрізання початку назви гри.
- [x] `STORAGE-FONT-SEPARATION-524` — зменшено розмір шрифту `storage_font` для підписів `NAND`, `SD` та чисел розміру з 19.05px до 15.5px, а позицію бейджів/версії `badge_y` піднято до 17.f, що забезпечило чіткий вертикальний проміжок і повністю усунуло налізання тексту пам'яті на верхній рядок системної версії/Kefir.
- [x] `HEADER-EMUNAND-BADGE-3WAY-524` — створено статусний бейдж режиму NAND із підтримкою адаптивного розгортання: при наявності вільного простору (без USB 3.0) відображається повний напис `EmuNAND` (зелений) або `SysNAND` (сірий), а при браку місця (наприклад з активним USB 3.0) бейдж компактно згортається до літери `E` / `S`; усунуто дублювання `|E`/`|S` наприкінці системного рядка версії в `hats_version.cpp`; сформовано два окремі блоки: Блок 1 (бейджі MTP, FTP, USB 3.0, EmuNAND/E) та Блок 2 (системний рядок версії Кефіру та ОС); реалізовано 3-сторонній симетричний розподіл з абсолютно рівними інтервалами між краями сховища та між самими блоками ($M = (W_{span} - (W_1 + W_2)) / 3$).
- [x] `DOCS-BUMP-524` — створено unit-тести `tests/test_screensaver_title.cpp` (11 checks), `tests/test_usb3_indicator.cpp` (12 checks) та оновлено `tests/test_header_service_indicators.cpp` (31 checks), версію піднято до `0.13.524` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `plan.md`, `task.md`, `walkthrough.md`, пройдено всі 22 набори host unit-тестів та успішно скомпільовано цільовий бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.523 (Header Kefir version, System OS Firmware & EmuNAND/SysNAND indicator)

- [x] `HEADER-SYS-VERSION-523` — додано `getKefirVersion()` та `getSystemVersionString()` до `sphaira::hats` (`sphaira/include/hats_version.hpp`, `sphaira/source/hats_version.cpp`); сформовано системний рядок версії з точним форматуванням як у налаштуваннях Horizon OS (`<Kefir> · <FW>|AMS <AMS>|<E/S>`, наприклад `Kefir 802 · 19.0.1|AMS 1.8.0|E`); у `MenuBase::DrawChrome` розміщено системний рядок у верхньому рядку сховища з правим вирівнюванням по осі `storage_right` на одній висоті з бейджами MTP та FTP.
- [x] `DOCS-BUMP-523` — оновлено `tests/test_header_service_indicators.cpp` (28 checks), версію піднято до `0.13.523` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `plan.md`, `task.md`, `walkthrough.md`, пройдено всі 20 наборів host unit-тестів у WSL.

## Попередній delivery: v0.13.522 (Exact NAND-edge boundary calculation & conditional anti-overlap marquee)

- [x] `HEADER-NET-NAND-BOUNDS-522` — перенесено розрахунок геометрії пам'яті перед відмальовуванням рядка мережі в `MenuBase::DrawChrome`; реалізовано динамічний розрахунок правого краю тексту NAND (`nand_right = value_x + nand_val_w`), встановлено ліву межу слота мережі на `net_left = nand_right + 12.f`; скролінг активується виключно при наявності реального перекриття (нахльосту) на область NAND, а при вільному розміщенні рядок лишається статичним з правим вирівнюванням.
- [x] `DOCS-BUMP-522` — оновлено `tests/test_header_network_layout.cpp` (28 checks), версію піднято до `0.13.522` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `plan.md`, `task.md`, `walkthrough.md`, пройдено всі 20 наборів host unit-тестів та успішно зібрано бінарник `sphaira_nro` у WSL.

## Поточний delivery: v0.13.521 (Header MTP and FTP demarcated service badges over storage bars)

- [x] `HEADER-SERVICE-INDICATORS-521` — додано поля `mtp_running` та `ftp_running` до структури `PolledData` у `MenuBase`; реалізовано щосекундне опитування активності служб через `sphaira::haze::IsRunning()` та `sphaira::ftpsrv::IsRunning()`; у `MenuBase::DrawChrome` розміщено відокремлені бейджі `MTP` та `FTP` (`[ ● MTP ]  [ ● FTP ]`) безпосередньо над графічними смугами накопичувачів NAND/SD (починаючи від `bar_x`) всередині блоку сховища з динамічним колірним кодуванням (яскраво-зелений індикатор та фон при активній службі, сірий колір теми `TEXT_INFO` при неактивній).
- [x] `DOCS-BUMP-521` — створено unit-тест `tests/test_header_service_indicators.cpp` (23 checks), версію піднято до `0.13.521` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `plan.md`, `task.md`, `walkthrough.md`, пройдено всі 20 наборів host unit-тестів.

## Попередній delivery: v0.13.520 (Header network SSID & IP anti-overlap scrolling marquee)

- [x] `HEADER-NET-MARQUEE-520` — інтегровано `ScrollingText m_scroll_network` у `MenuBase` (`sphaira/include/ui/menus/menu_base.hpp`, `sphaira/source/ui/menus/menu_base.cpp`); обчислено динамічні межі виділеного слота (`[start_x, bar_right]`), що усуває налізання довгих назв точок доступу Wi-Fi (SSID) та IP-адрес на індикатори пам'яті NAND/SD; реалізовано плавний біжучий рядок зі збереженням естетичного правого вирівнювання для коротких назв.
- [x] `DOCS-BUMP-520` — створено unit-тест `tests/test_header_network_layout.cpp` (25 checks), версію піднято до `0.13.520` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `plan.md`, `task.md`, `walkthrough.md`, пройдено всі 19 наборів host unit-тестів та успішно зібрано бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.519 (AppStore EntryMenu layout anti-overlap & instant launch state transition)

- [x] `APPSTORE-LAYOUT-FOOTER-519` — усунуто налізання кнопок дій на футер у `EntryMenu::Draw`: кнопки зафіксовано вище футера на 16 пікселів (`bottom_y = 630.f`), скориговано розмір шрифту та інтервали блоку метаданих, що забезпечило понад 80 пікселів вільного простору.
- [x] `APPSTORE-STATE-TRANSITION-519` — реалізовано детерміноване оновлення `installed_version` у колбеках `install`/`uninstall` та свіже оновлення при відкритті `EntryMenu`, завдяки чому після встановлення кнопка «Оновити» миттєво змінюється на «Запустити» та відображається `installed: Nightly`.
- [x] `DOCS-BUMP-519` — версію піднято до `0.13.519` у `sphaira/CMakeLists.txt`, оновлено `plan.md`, `task.md`, `walkthrough.md`, пройдено всі host unit-тести та успішно зібрано бінарник `sphaira_nro` у WSL з розгортанням на `F:`.

## Попередній delivery: v0.13.518 (RetroArch 7z PhysFS stream extractor & Nightly MD5 bypass)

- [x] `RETROARCH-7Z-EXTRACTOR-518` — підключено `libphysfs.a` у `sphaira/CMakeLists.txt`, реалізовано рекурсивний потоковий розпакувальник `ExtractPhysfsArchive` у `sphaira/source/ui/menus/appstore.cpp` для `.7z` архівів з автоматичною генерацією метаданих `info.json` (`Nightly`), усунуто невідповідну перевірку MD5 хешу для динамічних білдів RetroArch Nightly.
- [x] `DOCS-BUMP-518` — версію піднято до `0.13.518` у `sphaira/CMakeLists.txt`, оновлено `plan.md`, `task.md`, `walkthrough.md`, пройдено всі host unit-тести та успішно зібрано бінарник `sphaira_nro` у WSL з розгортанням на `F:`.

## Попередній delivery: v0.13.517 (AppStore installed version display, clean network handover & LibRetro Nightly resolver)

- [x] `APPSTORE-INSTALLED-VER-517` — у структуру `Entry` додано поле `installed_version`, реалізовано зчитування версії з `info.json` та з NACP бінарника (`nacp_util::GetDisplayVersion`); у меню додатка `EntryMenu::Draw` виведено відображення `installed: <версія>` з підсвічуванням кольором теми при наявності оновлення.
- [x] `RETROARCH-NIGHTLY-RESOLVER-517` — створено допоміжний модуль `sphaira/include/ui/menus/appstore_util.hpp` з функціями `IsRetroArchPackageName` та `ResolveAppstoreZipUrl`; перенаправлено завантаження `RetroNX`/`RetroArch` на офіційний збірник LibRetro Nightly (`RetroArch.7z`); у меню опцій для застарілих версій RetroArch кнопка `Launch` блокується і пропонується дія `Update`.
- [x] `NET-SHUTDOWN-ORDER-517` — у `sphaira/source/main.cpp` виправлено порядок деініціалізації сервісів: `socketExit()` тепер викликається строго перед `nifmExit()`, додано 50 мс паузу перед `appletUnlockExit()` для повного очищення мережевих дескрипторів ядра Horizon OS перед ланцюговим запуском NRO (`envSetNextLoad`).
- [x] `DOCS-BUMP-517` — версію піднято до `0.13.517` у `sphaira/CMakeLists.txt`, створено unit-тест `tests/test_appstore_util.cpp`, оновлено `plan.md`, `task.md`, `walkthrough.md`, пройдено всі host unit-тести та успішно зібрано бінарник `sphaira_nro` у WSL з копіюванням на `F:`.

## Попередній delivery: v0.13.516 (AppStore EntryMenu launch confirmation guard)

- [x] `APPSTORE-LAUNCH-CONFIRM-516` — додано обов'язкове діалогове вікно підтвердження (`OptionBox`) для дії `Launch` у меню детальної інформації про додаток (`EntryMenu` у `sphaira/source/ui/menus/appstore.cpp`), що усуває раптовий та випадковий запуск сторонніх `.nro` при відкритті карток встановлених додатків у магазині; оновлено та розгорнуто скомпільований бінарник `kefir-hub.nro` безпосередньо на карту пам'яті `F:`.
- [x] `DOCS-BUMP-516` — версію піднято до `0.13.516` у `sphaira/CMakeLists.txt`, оновлено `upstream_audit.md`, `plan.md`, `task.md`, `walkthrough.md`, пройдено всі host unit-тести та успішно зібрано бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.515 (UPA-13: Confirmed ROM database compatibility aliases)

- [x] `UPA-13-ROM-ALIASES-515` — оновлено таблицю співставлення баз даних ROM у [**`sphaira/include/ui/menus/filebrowser_assoc.hpp`**](sphaira/include/ui/menus/filebrowser_assoc.hpp): додано підтримку `NEC - PC Engine SuperGrafx` (папки `supergrafx`, `pce-sg`, `pcesg`), `Nintendo - Family Computer Disk System` поряд із `Nintendo - Famicom Disk System` (папка `fds`), та базовий `SNK - Neo Geo` поряд із `Pocket`/`Pocket Color`/`CD` (папка `neogeo`); розширено unit-тести [**`tests/test_tico_assoc.cpp`**](tests/test_tico_assoc.cpp) (20 checks passed).
- [x] `DOCS-BUMP-515` — версію піднято до `0.13.515` у `sphaira/CMakeLists.txt`, оновлено `upstream_audit.md`, `upstream_implementation_plan.md`, `plan.md`, `task.md`, `walkthrough.md`, пройдено всі host unit-тести та успішно зібрано бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.514 (UPA-11: GameCard theme roles & safe storage ratio)

- [x] `UPA-11-GC-BARS-514` — реалізовано модуль [**`sphaira/include/storage_ratio.hpp`**](sphaira/include/storage_ratio.hpp) із безпечним обчисленням коефіцієнтів та доступного обсягу пам'яті (захист від ділення на нуль, `total <= 0`, `free < 0` та `free > total`); оновлено відображення смуг пам'яті в `Menu::Draw` (`sphaira/source/ui/menus/gc_menu.cpp`), додано роль теми `ThemeEntryID_PROGRESSBAR_BACKGROUND`; додано обнулення змінних перед запитом у `UpdateStorageSize`; створено unit-тести [**`tests/test_storage_ratio.cpp`**](tests/test_storage_ratio.cpp) (14 checks passed).
- [x] `DOCS-BUMP-514` — версію піднято до `0.13.514` у `sphaira/CMakeLists.txt`, оновлено `upstream_audit.md`, `upstream_implementation_plan.md`, `plan.md`, `task.md`, `walkthrough.md`, пройдено всі host unit-тести та успішно зібрано бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.513 (UPA-10B: Localized UTF-8 MTP display names)

- [x] `UPA-10B-MTP-DISPLAY-NAME-513` — додано безпечну валідацію, санітизацію та транкацію UTF-8 рядків (`SanitizeUtf8TitleName`, `TruncateUtf8`, `ResolveMtpDisplayTitleName`, `FormatMtpGameDirName`) у `sphaira/include/title_export_name.hpp`; підключено `FormatMtpGameDirName` у `BuildGameDirName` (`sphaira/source/haze_helper.cpp`), що гарантує збереження символів кирилиці (українські/європейські назви), CJK, емодзі без обрізання посеред багатобайтового символу; розширено unit-тести `tests/test_title_export_name.cpp` (42 checks passed).
- [x] `DOCS-BUMP-513` — версію піднято до `0.13.513` у `sphaira/CMakeLists.txt`, оновлено `upstream_audit.md`, `upstream_implementation_plan.md`, `plan.md`, `task.md`, `walkthrough.md`, пройдено всі host unit-тести та успішно зібрано бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.512 (UPA-10A: Tested usable-title core & ASCII-safe NSP export helper)

- [x] `UPA-10A-EXPORT-NAME-512` — реалізовано модуль [**`sphaira/include/title_export_name.hpp`**](sphaira/include/title_export_name.hpp) з чіткою ієрархією кандидатів (American English -> British English -> Localized -> Title ID hex), семантичною валідацією придатності (`IsUsableTitleName`) та безпечною транкацією; інтегровано хелпер у звичайний (`BuildNspPath`) та об'єднаний (`BuildMergedNspEntry`) експорт NSP у `sphaira/source/title_nsp.cpp`; додано unit-тести [**`tests/test_title_export_name.cpp`**](tests/test_title_export_name.cpp) (24 checks passed).
- [x] `DOCS-BUMP-512` — версію піднято до `0.13.512` у `sphaira/CMakeLists.txt`, оновлено `upstream_audit.md`, `upstream_implementation_plan.md`, `plan.md`, `task.md`, `walkthrough.md`, пройдено всі host unit-тести та успішно зібрано бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.511 (UPA-09: Forwarder editor touch/controller focus matrix)

- [x] `UPA-09-FWD-FOCUS-511` — виправлено обробку подій сенсорного екрану та геймпада в `ForwarderEditor` (`sphaira/source/ui/forwarder_editor.cpp`): усунуто ранній `return` при `m_icon_focused`, додано виклик `m_list->OnUpdate(nullptr, touch, ...)` при фокусі на іконці, що забезпечує коректний скролінг та перемикання фокусу на елементи списку при натисканні/дотику без хибних або подвійних спрацьовувань.
- [x] `DOCS-BUMP-511` — версію піднято до `0.13.511` у `sphaira/CMakeLists.txt`, оновлено `upstream_audit.md`, `upstream_implementation_plan.md`, `plan.md`, `task.md`, `walkthrough.md`, пройдено всі host unit-тести та успішно зібрано бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.510 (UPA-08A & UPA-08B: Raw FTP mutation adapter & discovery gate)

- [x] `UPA-08A-HB-FTP-DISCOVERY-510` — зафіксовано архітектурні точки інтеграції, безпеку потоків (`g_thread` worker -> atomic event signal) та патч-маркери у ftpsrv.
- [x] `UPA-08B-HB-FTP-510` — розширено `sphaira/cmake/patch_ftpsrv.cmake` секцією 4 (`vfs_nx.h`, `vfs_nx_fs.h`, `vfs_nx_fs.c`) з підтримкою C ABI хуків для подій мутацій (`vfs_nx_set_mutation_callback`); підключено обробник `FtpMutationCallback` у `sphaira/source/ftpsrv_helper.cpp` до спільної політики Homebrew (`NotifyFileCreated`, `NotifyFileDeleted`, `NotifyDirectoryCreated`, `NotifyDirectoryDeleted`, `NotifyRename`); додано перевірочний скрипт `tests/test_patch_ftpsrv.sh` у `tests/run.sh`.
- [x] `DOCS-BUMP-510` — версію піднято до `0.13.510` у `sphaira/CMakeLists.txt`, оновлено `upstream_audit.md`, `upstream_implementation_plan.md`, `plan.md`, `task.md`, `walkthrough.md`, пройдено всі host unit-тести та успішно зібрано бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.509 (UPA-07B: MTP delete/rename/directory operations mutation coverage)

- [x] `UPA-07B-HB-MTP-MUTATIONS-509` — покрито всі операції мутацій файлів та директорій у MTP VFS (`sphaira/source/haze_helper.cpp`: `DeleteFile`, `RenameFile`, `CreateDirectory`, `DeleteDirectoryRecursively`, `RenameDirectory`); усі виклики перевіряють результат (`R_SUCCEEDED(rc)`), роблять `Commit()` та сповіщають `homebrew` через спільну політику (`NotifyFileDeleted`, `NotifyRename`, `NotifyDirectoryCreated`, `NotifyDirectoryDeleted`).
- [x] `DOCS-BUMP-509` — версію піднято до `0.13.509` у `sphaira/CMakeLists.txt`, оновлено `upstream_audit.md`, `upstream_implementation_plan.md`, `plan.md`, `task.md`, `walkthrough.md`, пройдено всі host unit-тести та успішно зібрано бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.508 (UPA-07A: MTP upload/final-close shared mutation policy integration)

- [x] `UPA-07A-HB-MTP-UPLOAD-508` — інтегровано спільну політику мутацій Homebrew у MTP VFS (`sphaira/source/haze_helper.cpp`): замінено прапорець `m_notify_homebrew` на точний облік відкритих на запис файлів `m_open_write_files`; сповіщення `ui::menu::homebrew::NotifyFileCreated(written_path)` надсилається детерміновано один раз під час успішного `CloseFile()` для будь-яких записів у дефолтний `/switch`, редиректи або кастомні search roots.
- [x] `DOCS-BUMP-508` — версію піднято до `0.13.508` у `sphaira/CMakeLists.txt`, оновлено `upstream_audit.md`, `upstream_implementation_plan.md`, `plan.md`, `task.md`, `walkthrough.md`, пройдено всі host unit-тести та успішно зібрано бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.507 (UPA-06: Shared homebrew mutation policy & complete Web success coverage)

- [x] `UPA-06-HB-POLICY-WEB-507` — реалізовано спільну політику мутацій Homebrew (`path::IsSubpathOf`, `path::IsNroPath`, `path::PathAffectsHomebrew` у `sphaira/include/path_util.hpp`); додано безпечні функції сповіщення (`NotifyFileCreated`, `NotifyFileDeleted`, `NotifyDirectoryCreated`, `NotifyDirectoryDeleted`, `NotifyRename`, `NotifyPathChanged` у `sphaira/include/ui/menus/homebrew.hpp` та `sphaira/source/ui/menus/homebrew.cpp`); підключено спільну політику у Web server (`sphaira/source/web.cpp`: `HandleUpload` та `HandleDelete`); додано 53 нові перевірки в unit-тести `tests/test_path_util.cpp` (283 checks passed).
- [x] `DOCS-BUMP-507` — версію піднято до `0.13.507` у `sphaira/CMakeLists.txt`, оновлено `upstream_audit.md`, `upstream_implementation_plan.md`, `plan.md`, `task.md`, `walkthrough.md`, пройдено всі host unit-тести та успішно зібрано бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.506 (UPA-05: Playtime worker UI-thread isolation & race elimination)

- [x] `UPA-05-PLAYTIME-RACE-506` — ліквідовано стан гонитви (data race) у `Menu::LoadPlaytime()` (`sphaira/source/ui/menus/game_menu.cpp`): фоновий потік `ProgressBox` тепер працює виключно зі знімком ідентифікаторів та окремим буфером результатів без прямого доступу до `m_entries`; результати застосовуються суворо на UI thread після завершення потоку та лише за умови успіху (`R_SUCCEEDED(rc)`).
- [x] `DOCS-BUMP-506` — версію піднято до `0.13.506` у `sphaira/CMakeLists.txt`, оновлено `upstream_audit.md`, `upstream_implementation_plan.md`, `plan.md`, `task.md`, `walkthrough.md`, пройдено всі host unit-тести та успішно зібрано бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.505 (UPA-04A: MTP zero-byte upload support & patch shape verification)

- [x] `UPA-04A-MTP-ZERO-505` — виправлено умову розрахунку розміру файлу під час MTP upload (`SendObject`) у `sphaira/cmake/patch_libhaze.cmake` на `data_header.length >= sizeof(PtpUsbBulkContainer)`, що забезпечує коректне встановлення нульового розміру файлів без залишення dummy `4_GB`; збережено всі локальні розширення та ідемпотентність; додано автоматичний runnable fixture check `tests/test_patch_libhaze.sh` у `tests/run.sh`.
- [x] `DOCS-BUMP-505` — версію піднято до `0.13.505` у `sphaira/CMakeLists.txt`, оновлено `upstream_audit.md`, `upstream_implementation_plan.md`, `plan.md`, `task.md`, `walkthrough.md`, пройдено всі host unit-тести та успішно зібрано бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.504 (UPA-03: Centralized GitHub and direct URL validation)

- [x] `UPA-03-GHDL-URL-504` — реалізовано строгу централізовану валідацію GitHub репозиторіїв `path::ParseGitHubRepoUrl` (підтримка http/https, www, stripping `.git` та trailing slashes, заборона userinfo/port/query/fragment/traversal); додано `path::IsValidDirectAssetUrl` та `path::IsValidDirectZipUrl`; оновлено завантажувач у `sphaira/source/ui/menus/ghdl.cpp` (`LoadEntriesFromPath`, `Download`, `OpenDirectLinkPrompt`); покрито повним набором unit-тестів у `tests/test_path_util.cpp` (230 checks passed).
- [x] `DOCS-BUMP-504` — версію піднято до `0.13.504` у `sphaira/CMakeLists.txt`, оновлено `upstream_audit.md`, `upstream_implementation_plan.md`, `plan.md`, `task.md`, `walkthrough.md`, пройдено всі host unit-тести та успішно зібрано бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.503 (UPA-02B: GHDL ZIP type detection & safe non-ZIP destination)

- [x] `UPA-02B-GHDL-DESTINATION-503` — додано комплексне визначення ZIP-архівів через `path::IsZipAsset` (перевірка `content_type`, суфікса імені файлу та шляху URL без query-параметрів); для не-ZIP ассетів без явного шляху призначено безпечну директорію `/switch/<sanitized-name>` замість кореня `/`; додано валідацію `path::IsSafeFilename` та нормалізацію шляхів через `path::NormalizeAbsoluteSdPath`; покрито новими unit-тестами у `tests/test_path_util.cpp` (193 checks passed).
- [x] `DOCS-BUMP-503` — версію піднято до `0.13.503` у `sphaira/CMakeLists.txt`, оновлено `upstream_audit.md`, `upstream_implementation_plan.md`, `plan.md`, `task.md`, `walkthrough.md`, пройдено всі host unit-тести та успішно зібрано бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.502 (UPA-02A: GitHub downloader operation identity, cancel & temp isolation)

- [x] `UPA-02A-GHDL-FINALIZE-502` — реалізовано строгий контроль стану скасування (`pbox->ShouldExit()` / `Result_TransferCancelled`) на всіх фазах завантаження (`DownloadApp()`, `DownloadReleaseJsonJson()`, `DoDirectLinkDownload()`); застарілі тимчасові файли `ghdl.temp` та `direct_link.zip` детерміновано видаляються перед початком та на виході через `ON_SCOPE_EXIT`; нотифікація `homebrew::SignalChange()` викликається суворо після успішного завершення операції (`R_SUCCEEDED(rc)`).
- [x] `DOCS-BUMP-502` — версію піднято до `0.13.502` у `sphaira/CMakeLists.txt`, оновлено `upstream_audit.md`, `upstream_implementation_plan.md`, `plan.md`, `task.md`, `walkthrough.md`, пройдено всі host unit-тести та успішно зібрано цільовий бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.501 (UPA-01: GitHub downloader callback ownership & selection safety)

- [x] `UPA-01-GHDL-OWNERSHIP-501` — усунено global `static std::vector<GhApiEntry>` у `DownloadEntries()` (`sphaira/source/ui/menus/ghdl.cpp`), замінено на операційно-локальний `std::shared_ptr`; ліквідовано UAF / висячі посилання (`&asset_entry` та `const AssetEntry*` на тимчасові вектори) шляхом переходу на `std::optional<AssetEntry>` та захоплення за значенням; додано строгі перевірки меж `op_index` для вибору релізів та ассетів.
- [x] `DOCS-BUMP-501` — версію піднято до `0.13.501` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `plan.md`, `task.md`, `walkthrough.md`, пройдено всі unit-тести та перевірено збірку бінарника `sphaira_nro` у WSL.

## Попередній delivery: v0.13.500 (NexLink / DBI return crash)

- [x] `CRASH-TRACEBACK-500` — знайдено crash-report на змонтованій microSD (`F:\atmosphere\crash_reports\01787234167_03db12780bd84000.log`), підтверджено точний збіг Build ID з актуальним WSL ELF та відновлено стек `_malloc_r → _memalign_r → opendir → App::ScanThemes → App::App`.
- [x] `NRO-LIFECYCLE-ROOT-CAUSE-500` — додано strong `__libnx_initheap()`: він відсікає успадковані `Perm_None` / `IsBorrowed` сторінки й передає newlib найбільший суцільний `Heap + RW + attr == 0` діапазон; outbound logger thread перенесено в межі життя `App`, socket sentinel виправлено на `-1`, а копіювання повідомлення обмежено фактичним `buf[512]`.
- [x] `REGRESSION-VERIFY-500` — host-тести й WSL ReleaseWithInstall-збірка пройдені; новий ELF має Build ID `93E0BD21BD490A235A75C52D4DE6ECBC243D0879`; апаратне тестування прийнято.
- [x] `DOCS-BUMP-COMMIT-500` — версію піднято до `0.13.500`, результат зафіксовано у walkthrough і task-документах; створено сфокусований коміт.

## Попередній delivery: v0.13.499 (Tools Menu Layout Reorganization, Software Description Update & 4th Row Expansion)

- [x] `TOOLS-MENU-REORDER-499` — реорганізовано порядок пунктів у меню Tools: 1-й ряд (File Browser, Games, Themes), 2-й ряд (Updater, Saves, Software), 3-й ряд (Cheats, Kefir Settings, Settings), 4-й ряд (Tools).
- [x] `SOFTWARE-DESC-UPDATE-499` — оновлено опис розділу Software на `"Homebrew App Store, DBI and mod utilities."` з підтримкою Homebrew App Store.
- [x] `TOOLS-ROW4-EXPANSION-499` — експериментально додано пункт Tools у 4-й ряд з іконкою `advanced-options.png` та відкриттям менеджера модулів (`UninstallerMenu`) з підтримкою вертикального скролінгу сітки.
- [x] `I18N-SYNC-499` — синхронізовано переклади для нових ключів у всіх 14 мовних файлах `assets/romfs/i18n/*.json`.
- [x] `DOCS-BUMP-499` — піднято версію до `0.13.499` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `plan.md`, `task.md`, `walkthrough.md`.

## Попередній delivery: v0.13.498 (Header Subtitle Top-Row Alignment & Section Title Sizing Fix)

- [x] `HEADER-SUBTITLE-TOPROW-498` — перенесено виведення довгих описів елементів меню (Tools, Settings, SaveHub, Uninstaller, FTP, AppStore) з нижнього рядка підзаголовка у верхній рядок хедера поруч з версією програми через `SetTitleSubHeading(..., true)`.
- [x] `HEADER-TITLE-FULLSIZE-498` — завдяки перенесенню опису у верхній рядок звільнено нижній рядок хедера: назви розділів («Інструменти» / «Tools», «Налаштування» / «Settings» тощо) зберігають повний великий кегль 28px без стискання до 40% та непотрібної прокрутки.
- [x] `DOCS-BUMP-498` — піднято версію до `0.13.498` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `plan.md`, `task.md`, `walkthrough.md`, скомпільовано цільовий бінарник у WSL та перевірено unit-тести.

## Попередній delivery: v0.13.497 (Clean Switch compilation, translation pipeline sync & NX-Link deployment)

- [x] `SWITCH-COMPILATION-FIXES-497` — виправлено помилки та попередження компіляції під Nintendo Switch (`fs.OpenFile(staging_path, FsOpenMode_Read, &file)`, `TouchInfo` члени у `AboutBox::Update`, специфікатор `%ld` для `s64`).
- [x] `I18N-TRANSLATE-SYNC-497` — виконано повну синхронізацію та переклад усіх мовних файлів через Gemini 3.6 Flash (0 відсутніх ключів).
- [x] `NRO-BUILD-NXLINK-497` — скомпільовано цільовий двійковий файл `kefir-hub.nro` у WSL та успішно відправлено на Switch через NX-Link (`192.168.50.69`).
- [x] `DOCS-BUMP-497` — піднято версію до `0.13.497` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `plan.md`, `task.md`, `walkthrough.md`.

## Попередній delivery: v0.13.496 (Automatic Silent Update, Background Self-Updating & About Changelog Box)

- [x] `SETTINGS-AUTO-UPDATE-OPT-496` — додано опцію `m_auto_update` (`App::GetAutoUpdateEnable()`, `App::SetAutoUpdateEnable(bool)`) та пункт налаштування `Auto-update` у `Settings → General` з перекладами у всіх 14 мовних файлах `assets/romfs/i18n/*.json`.
- [x] `MAIN-SILENT-UPDATE-FLOW-496` — оновлено URL репозиторію на `rashevskyv/kefir-hub`, реалізовано 100% тихий фоновий асинхронний пайплайн завантаження релізного ассету (`.nro` / `.zip`) у `MainMenu::MainMenu()` та безпечну атомарну заміну файлу `App::GetExePath()` і `/hbmenu.nro` без попапів чи переривань користувача.
- [x] `ABOUT-CHANGELOG-BOX-496` — створено модальне вікно `AboutBox` (`ui/about_box.hpp`, `ui/about_box.cpp`) з переглядом поточної версії, лінком на репозиторій та кешованим/живим списком змін релізу, додано пункт `About` у `Settings → General`.
- [x] `TESTS-AUTO-UPDATE-496` — створено unit-тест `tests/test_auto_update_asset.cpp` для валідації вибору ассетів та порівняння версій.
- [x] `DOCS-BUILD-VERIFY-496` — піднято версію до `0.13.496` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `task.md`, `walkthrough.md`, пройдено всі 15 наборів host unit-тестів у WSL.

## Попередній delivery: v0.13.495 (Pixel-balanced split & full-width justified 2-row footer layout)

- [x] `FOOTER-PIXEL-BALANCE-SPLIT-495` — оновлено алгоритм вибору точки розбиття $k$ у `Widget::SetupUiButtons` (`sphaira/source/ui/widget.cpp`) для мінімізації різниці між сумарною кількістю зайнятих пікселів контенту верхнього та нижнього рядків ($|W_{\text{bottom}} - W_{\text{top}}|$).
- [x] `FOOTER-JUSTIFIED-FULLWIDTH-495` — реалізовано `LayoutUiButtonsRowJustified`, який динамічно розподіляє вільний простір між елементами рядка ($gap = (W_{\text{avail}} - W_{\text{content}}) / (M - 1)$), завдяки чому обидва рядки повністю займають усю ширину футера (від `30px` до `1220px`), а сенсорні зони безшовно покривають весь екран.
- [x] `TESTS-JUSTIFIED-SPLIT-495` — оновлено unit-тест `tests/test_title_scaling.cpp` для перевірки попіксельного балансування рядків і точного приземлення крайнього лівого елемента на координату `30.f`.
- [x] `DOCS-BUILD-VERIFY-495` — піднято версію до `0.13.495` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `plan.md`, `task.md`, `walkthrough.md`, скомпільовано цільовий бінарник `sphaira_nro` у WSL та пройдено всі unit-тести.

## Попередній delivery: v0.13.494 (Unified Prev/Next Image button hint in image viewer footer)

- [x] `IMAGE-SLASH-NAV-HINT-494` — об'єднано окремі підказки «Попереднє зображення» та «Наступне зображення» у вівері (`file_viewer.cpp`) в єдиний компактний елемент `Action{"Prev / Next Image"_i18n, "\uE0ED / \uE0EE", ...}` для `Button::LEFT`, а для `Button::RIGHT` зареєстровано приховану дію (для збереження обробки події переходу вперед контролером), оптимізувавши ширину футера.
- [x] `DOCS-BUILD-VERIFY-494` — піднято версію до `0.13.494` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `plan.md`, `task.md`, `walkthrough.md`, скомпільовано цільовий бінарник `sphaira_nro` у WSL та перевірено тести.

## Попередній delivery: v0.13.493 (Image viewer uncluttered header, dynamic title scaling/scrolling & 2-row footer layout)

- [x] `IMAGE-HEADER-NO-STORAGE-493` — додано прапорець `m_show_storage` та методи `SetShowStorage(bool)` / `ShowStorage()` у `MenuBase` (`menu_base.hpp`), реалізовано приховування панелей пам'яті NAND/SD у вівері зображень (`file_viewer.cpp`) зі звільненням усього простору від лівого краю (`x=80`) до годинника під назву файлу.
- [x] `TITLE-ADAPTIVE-SCALE-SCROLL-493` — реалізовано динамічне масштабування шрифту назви файлу та заголовків меню в `MenuBase::DrawChrome` (`menu_base.cpp`) (базовий 28px, зменшення до 40% / 16.8px пропорційно вільному місцю) з автоматичним переходом у плавну прокрутку `m_scroll_title` (`ScrollingText`), якщо текст перевищує ширину навіть при мінімальному кеглі.
- [x] `FOOTER-2ROW-AUTO-LAYOUT-493` — реалізовано автоматичний перехід легенди футера на 2 збалансовані рядки у `Widget::SetupUiButtons` (`widget.cpp`) при падінні масштабу 1 рядка нижче 0.85 (нижній рядок — основні кнопки дій, верхній рядок — тригери та вторинні дії) зі збереженням великого шрифту та незалежних сенсорних хітбоксів для обох рядків.
- [x] `TESTS-TITLE-FOOTER-493` — створено `tests/test_title_scaling.cpp` для повної перевірки математики масштабування та алгоритму оптимального розбиття 2-рядкового футера.
- [x] `DOCS-BUILD-VERIFY-493` — піднято версію до `0.13.493` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `plan.md`, `task.md`, `walkthrough.md`, скомпільовано цільовий бінарник `sphaira_nro` у WSL та успішно пройдено всі unit-тести.

## Попередній delivery: v0.13.492 (Homebrew App Store restored to Tools > Software)

- [x] `RESTORE-APPSTORE-SOFTWARE-492` — повернуто `Homebrew App Store` у список `BuildSoftwareItems()` (`sphaira/source/ui/menus/settings_menu.cpp`) як перший пункт розділу `Tools → Software`.
- [x] `CLEANUP-SETTINGS-HB-492` — прибрано ярлик `Homebrew App Store` із `Settings → Homebrew` (`BuildCategories()`), залишивши в налаштуваннях виключно параметри конфігурації.
- [x] `DOCS-BUILD-VERIFY-492` — піднято версію до `0.13.492` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `plan.md`, `task.md`, `walkthrough.md`, скомпільовано цільовий бінарник `sphaira_nro` у WSL та пройдено тести.

## Попередній delivery: v0.13.491 (Fix flush thread stack overflow)

- [x] `LOG-STACK-OVERFLOW-FIX-491` — перенесено масив `batch` зі стеку функції `flush_thread_func` у статичну змінну `g_flush_batch` та збільшено стек потоку до `0x8000`.
- [x] `BUILD-VERIFY-491` — піднято версію до `0.13.491` у `sphaira/CMakeLists.txt`, оновлено документацію, успішно зібрано `sphaira_nro` у WSL.

## Попередній delivery: v0.13.490 (Fix cstring include in static logger)

- [x] `LOG-CSTRING-INCLUDE-490` — додано `#include <cstring>` у `sphaira/source/log.cpp` для `std::memcpy`.
- [x] `BUILD-VERIFY-490` — піднято версію до `0.13.490` у `sphaira/CMakeLists.txt`, оновлено документацію, успішно зібрано `sphaira_nro` у WSL.

## Попередній delivery: v0.13.489 (Zero-heap static logging buffer & image load ordering)

- [x] `LOG-STATIC-BUFFER-489` — реалізовано 64 КБ статичний неалокуючий буфер у `sphaira/source/log.cpp`, повністю вилучивши виклики `malloc`/`realloc`/`free` з фонового та робочих потоків логування.
- [x] `INIT-DEFAULT-IMG-ORDER-489` — перенесено `InitDefaultImage()` перед запуском `ntp::Start()` та `forwarder_auto::StartCheck()` у `App::App` (`sphaira/source/app.cpp`).
- [x] `ZERO-HEAP-DOCS-VERIFY-489` — піднято версію до `0.13.489` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `plan.md`, `task.md`, `walkthrough.md`.

## Попередній delivery: v0.13.488 (Sysmodule slow SD boot timeout & crash prevention)

- [x] `SYSMODULE-TIMEOUTS-488` — збільшено таймаути ініціалізації FS та монтування SD у `sysmodule/source/main.c` з 10 секунд до 300 секунд (5 хвилин), видалено `diagAbortWithResult` при помилці `smInitialize()`.
- [x] `SYSMODULE-DOCS-VERIFY-488` — піднято версію до `0.13.488` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `plan.md`, `task.md`, `walkthrough.md`.

## Попередній delivery: v0.13.487 (SD card FS sync, malloc & NanoVG stability on slow cards / NX-Link handoff)

- [x] `FSDEV-COMMIT-SDMC-487` — виправлено назву монтування ФС на `"sdmc"` (без двокрапки) у `fsdevCommitDevice("sdmc")` та `fsdevGetDeviceFileSystem("sdmc")` у `userAppExit()` (`main.cpp`), гарантуючи скидання кешу запису microSD при завершенні та перезапуску NRO.
- [x] `NXLINK-SD-FLUSH-487` — додано виклики `fsdevCommitDevice("sdmc")` у `nxlink.cpp` після запису та перейменування переданого NRO-файлу перед запуском.
- [x] `NRO-LAUNCH-COMMIT-487` — додано виклик `fsdevCommitDevice("sdmc")` у `nro.cpp` (`launch_internal`) перед передачею керування до `envSetNextLoad`, що унеможливлює пошкодження файлової системи та падіння повільних карт пам'яті.
- [x] `LOG-SOCKET-SEND-487` — переведено передачу мережевих логів у фоновому потоці `log.cpp` на прямий системний виклик `send(sock, ...)`, усунувши конкурентні звернення до `stdout`/stdio та взаємне псування заголовків чанків пам'яті (`_malloc_r`).
- [x] `LOG-WRITE-COMMIT-487` — додано `fsdevCommitDevice("sdmc")` після запису логів та помилок у `log.cpp`, замінено `std::localtime` на реентрабельний `localtime_r` у `log_write_error`.
- [x] `THEME-STRING-TERMINATE-487` — усунено вихід за межі рядків при викликах `nvgCreateImage` та `std::strtoul` у `app_theme.cpp` шляхом створення нуль-термінованих `std::string` із `std::string_view`.
- [x] `BUILD-VERIFY-DEPLOY-487` — піднято версію до `0.13.487` у `CMakeLists.txt`, оновлено документацію, успішно зібрано `sphaira_nro` у WSL, виконано тести та оновлено бінарники на карті `I:\` (`I:\hbmenu.nro`, `I:\switch\kefir-hub.nro`).

## Попередній delivery: v0.13.486 (Saves menu L/R shoulder button tab navigation)

- [x] `SAVE-LR-ACTIONS-486` — додано реєстрацію дій `Button::L` ("Previous tab") та `Button::R` ("Next tab") у конструкторі `Menu::Menu` (`save_menu.cpp`) для автономного режиму перегляду сейвів (`!m_app_id_filter`).
- [x] `SAVE-CATEGORY-CYCLE-486` — реалізовано методи `Menu::ChangeCategory(s64 delta)` та `Menu::SetCategory(Category category)` (`save_menu.hpp`, `save_menu.cpp`) з циклічною навігацією між категоріями `Installed Games` <-> `Deleted Games` <-> `Backups`, оновленням заголовка `SetTitle(...)`, відтворенням звуку фокусу та рескануванням `ScanHomebrew()`.
- [x] `SAVE-LR-DOCS-VERIFY-486` — піднято версію до `0.13.486` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `plan.md`, `task.md`, `walkthrough.md`, успішно скомпільовано цільовий бінарник `sphaira_nro` у WSL та пройдено всі unit-тести.

## Попередній delivery: v0.13.485 (Screensaver display sleep prevention & OLED user brightness retention)

- [x] `SAVER-NO-SLEEP-485` — увімкнено гарантовану заборону переходу в авто-сон та гасіння дисплея (`App::SetAutoSleepDisabled(true)` та `appletSetMediaPlaybackState(true)`) під час роботи скрінсейвера `Screensaver::Start()` з відновленням у `Stop()`.
- [x] `SAVER-IDLE-REPORT-485` — додано виклики `appletReportUserIsActive()` у `Screensaver::Update` для постійного скидання системних таймерів неактивності HOS під час дрейфу екранної заставки.
- [x] `OLED-HARDWARE-DETECT-485` — реалізовано метод `App::IsOledModel()` в `app_settings.cpp` з опитуванням апаратного типу `SplConfigItem_HardwareType` (значення 5 / Aula для Nintendo Switch OLED).
- [x] `SAVER-OLED-BRIGHTNESS-485` — у `Screensaver::Start()` для консолей Switch OLED встановлено автоматичне збереження поточної яскравості користувача `m_saved_brightness` (оскільки чистий чорний фон не споживає струму на OLED і не вимагає приглушення, зберігаючи чіткість годинника та статистики), тоді як для LCD-моделей яскравість знижується для економії заряду та усунення світіння матриці.
- [x] `SAVER-PREVIEW-UPDATE-485` — додано виклик `m_saver.Update` у `SaverPreview::Update` для коректної обробки активності та дрейфу в режимі попереднього перегляду.
- [x] `SAVER-DOCS-VERIFY-485` — піднято версію до `0.13.485` у `sphaira/CMakeLists.txt`, синхронізовано `README.md`, `plan.md`, `task.md`, `walkthrough.md`, успішно зібрано бінарник у WSL (`sphaira_nro`) та пройдено всі тести.

## Попередній delivery: v0.13.484 (NX-Link SD commit, path normalization, buffer bounds & forwarder auto-install stabilization)

- [x] `APP-PATH-TERMINATION-484` — виправлено ініціалізацію та відсутність термінатора `\0` для `m_app_path` у `App::App` (`app.cpp`) при старті через HBL (`argv0` з префіксом `sdmc:/`), усунувши вихід за межі буфера при читаннях `GetExePath()`.
- [x] `FWD-ACTIVE-TITLE-GUARD-484` — додано перевірку `App::IsApplication()` та вилучення Title ID із назви NSP у `forwarder_auto_install.cpp` з перевіркою `nsIsAnyApplicationEntityInstalled`, що запобігає спробам перевстановлення активного тайтла програми під час її роботи.
- [x] `FWD-THREAD-LIFECYCLE-484` — додано функцію `StopCheck()` та перевірку `g_stop_requested` у `SilentInstallProgress`, що гарантує коректне та безпечне завершення фонового потоку інсталяції в деструкторі `App::~App()`.
- [x] `NXLINK-PATH-NORMALIZE-484` — додано обов'язкову нормалізацію шляхів файлів, отриманих через NX-Link у `nxlink.cpp` (видалення `sdmc:`, забезпечення нативного абсолютного шляху від кореня SD).
- [x] `NXLINK-FS-COMMIT-484` — додано виклики `fs.Commit()` після запису та перейменування тимчасового файлу в `nxlink.cpp`, що забезпечує атомарність та збереження FAT32/exFAT метаданих перед запуском NRO та запобігає падінню карти пам'яті.
- [x] `NXLINK-BUFFER-SAFETY-484` — забезпечено безпеку буфера аргументів `args_buf` із гарантованим завершальним `\0`, безпечний `strncpy` у `WriteCallbackFile`, та переведено `SocketWrapper` на move-only семантику.
- [x] `LOG-LOCALTIME-REENTRANT-484` — замінено `std::localtime` на `localtime_r` у `log.cpp` для потокобезпечного формування міток часу в логах.
- [x] `TESTS-PARALLEL-VERIFY-484` — переведено `tests/run.sh` на паралельний запуск, піднято версію до `0.13.484` у `sphaira/CMakeLists.txt`, успішно зібрано бінарник `sphaira_nro` у WSL та пройдено всі unit-тести.

## Попередній delivery: v0.13.483 (Install queue list layout bounds fix & auto-advance on X button)

- [x] `QUEUE-LAYOUT-BOUNDS-483` — скориговано розміри та позиціонування списку `m_list` у `dbi_menu.cpp`: зменшено висоту рядка до 78.f, скориговано `queue_pos` до `{70.f, GetY() + 63.f, 1140.f, 470.f}`, усунувши вилізання елементів списку та рамки виділення на розділювач та кнопки футера (`FOOTER_LINE_Y = 646.f`), а також оптимізовано розміри `log_pos` (310.f) для списків логу та помилок.
- [x] `QUEUE-SELECT-AUTO-ADVANCE-483` — додано автоматичний перехід курсора до наступного пункту (`m_index++`) та виклик `m_list->EnsureVisible` при натисканні кнопки `X` у черзі встановлення (`ReviewQueue`), уніфікувавши UX вибору елементів з `game_menu`, `homebrew`, `filebrowser` та `save_menu`.
- [x] `LAYOUT-SCISSOR-GUARD-483` — посилено `PaddedContentClipY` у `layout.hpp`: для будь-якого контенту, розташованого нижче лінії шапки (`y >= HEADER_LINE_Y`), гарантовано встановлюється обмеження нижньої межі ножиць до `CONTENT_BOTTOM` (646.f).
- [x] `QUEUE-DOCS-VERIFY-483` — піднято версію до `0.13.483` у `CMakeLists.txt`, оновлено `README.md`, `plan.md`, `task.md`, `walkthrough.md`, успішно пройдено всі unit-тести та скомпільовано реліз `sphaira_nro` у WSL.

## Попередній delivery: v0.13.482 (Fully silent background forwarder installation without restart prompt)

- [x] `FWD-SILENT-NO-PROMPT-482` — оновлено `forwarder_auto_install.cpp`: повністю прибрано спливаючі діалогові вікна `OptionBox` та запити на перезапуск після встановлення форвардера; процес виконується абсолютно прозоро та мовчки у фоні.
- [x] `FWD-CLEANUP-INCLUDES-482` — очищено невикористовувані заголовні файли (`evman.hpp`, `i18n.hpp`, `ui/option_box.hpp`) у `forwarder_auto_install.cpp`.
- [x] `FWD-SILENT-DOCS-VERIFY-482` — піднято версію програми до `0.13.482` у `CMakeLists.txt`, оновлено `README.md`, `plan.md`, `task.md`, `walkthrough.md`, успішно виконано збірку цілі `sphaira_nro` у WSL та пройдено всі тести.

## Попередній delivery: v0.13.481 (Install queue package skip fix & USB link resynchronization)

- [x] `QUEUE-SKIP-USB-RESYNC-481` — реалізовано обов'язкову ресинхронізацію USB-з'єднання (`ReestablishUsbLink()`) у `ThreadFunction` після пропуску пакунка користувачем (`user_skipped`), що запобігає збою протоколу та аварійному скасуванню наступних пакунків черги.
- [x] `QUEUE-SKIP-RETRY-EXT-481` — розширено умови циклу повторних спроб у `ThreadFunction` для автоматичного відновлення зв'язку при відновлюваних помилках протоколу/сесії DBI (`IsDbiSessionError`).
- [x] `QUEUE-SKIP-PROMPT-DEFAULT-481` — встановлено за замовчуванням вибір `"Yes"` (індекс 1) у діалозі `OptionBox` для дії кнопки `B` ("Skip package") під час встановлення, уніфікуючи поведінку з `ProgressBox`.
- [x] `QUEUE-SKIP-LOCAL-THREAD-481` — оновлено `LocalThreadFunction`: включено `Result_UsbCancelled` у перевірку `cancelled` та уніфіковано встановлення прапорця `m_cancel_requested` при виході.
- [x] `QUEUE-SKIP-TESTS-DOCS-481` — розширено `test_queue_outcome.cpp` новим тестом послідовного пропуску та продовження встановлення наступних пакунків, піднято версію до `0.13.481` у `CMakeLists.txt`, успішно пройдено всі unit-тести та скомпільовано реліз у WSL.

## Попередній delivery: v0.13.480 (Save data deletion mechanism & auto-creation on restore)

- [x] `SAVE-DELETE-ACTION-480` — додано дію `"Delete"` у `PromptSaveAction` (натискання `A`), бічне меню `Save Options` та розширено `PromptSaveTypeOptions` з підтримкою `SaveOp::Delete`.
- [x] `SAVE-DELETE-OPS-480` — реалізовано `Menu::DeleteSaves` для видалення сейвів із консолі (`fsDeleteSaveDataFileSystemBySaveDataSpaceId`, `fsDeleteSaveDataFileSystemBySaveDataAttribute`) та резервних копій з SD із захисними діалогами підтвердження `OptionBox` та відображенням прогресу у `ProgressBox`.
- [x] `SAVE-RESTORE-AUTO-CREATE-480` — покращено `Menu::RestoreSaveInternal`: додано автоматичне створення файлової системи збереження (`fsCreateSaveDataFileSystem`) з метаданих архіву при відновленні на чистих/клонованих EmuNAND або нових іграх без попереднього сейву на консолі.
- [x] `SAVE-I18N-DOCS-480` — оновлено мовні файли (`en.json`, `uk.json`), синхронізовано `README.md`, піднято версію до `0.13.480` у `CMakeLists.txt`, успішно зібрано `sphaira_nro` у WSL та перевірено всі тести.

## Попередній delivery: v0.13.479 (Automatic forwarder check, silent install & title mode restart prompt)

- [x] `FWD-CHECK-INSTALLED-479` — додано `forwarder_auto_install.hpp` та `forwarder_auto_install.cpp` з фоновою перевіркою встановлених тайтлів (`010000000000100D`, `050000000000100D`, та хешованих ідентифікаторів Sphaira) через `nsIsAnyApplicationEntityInstalled`.
- [x] `FWD-SILENT-INSTALL-479` — реалізовано автоматичне фонове сканування папки `/Games/` на SD карті на наявність `Homebrew menu*.nsp`, тихе встановлення через `yati::InstallFromFile` з використанням `SilentInstallProgress`, а також додано метод `OnTitleInstalled` у `ui::InstallProgress` та `yati.cpp`.
- [x] `FWD-RESTART-PROMPT-479` — додано повідомлення через `evman::push` з діалогом `OptionBox` про перезапуск у Title Mode із встановленого форвардера через `appletRequestLaunchApplication(target_tid, nullptr)` та `App::Exit()`.
- [x] `FWD-PATH-UTIL-TESTS-479` — додано `path::StartsWithIC` у `path_util.hpp`, розширено host-тести `test_path_util.cpp` та оновлено 14 мовних файлів локалізації.
- [x] `FWD-VERIFY-DOCS-479` — піднято версію до `0.13.479`, оновлено README.md, plan.md, task.md, walkthrough.md, успішно скомпільовано проєкт у WSL та пройдено всі тести.

## Попередній delivery: v0.13.478 (Theme packages download & instant install prompt)

- [x] `THEME-PKG-EXTRACT-478` — реалізовано `InstallThemePackage` та `PromptInstallTheme` у `themezer.cpp` із фільтрацією `.nxtheme` під час розпакування zip та збереженням шляхів для подальшої передачі інсталятору.
- [x] `THEME-PKG-MENU-ITEM-478` — додано `MakeThemePackageItem` у `settings_menu.cpp` з діалогом `"Download theme?"`, завантаженням через `ProgressBox` та інтеграцією готових пакетів `Mario BG Dark` і `Switch 2 Theme by alexwak`.
- [x] `THEME-PKG-DOCS-VERIFY-478` — піднято версію до `0.13.478`, оновлено README.md, TESTPLAN.md, living docs, виконано компіляцію у WSL та перевірено всі тести.

## Попередній delivery: v0.13.477 (Game details stat label vertical alignment fix)

- [x] `STAT-LABEL-ALIGN-477` — виправлено вирівнювання прокручуваних лейблів статистики у `game_menu.cpp`: додано `NVG_ALIGN_LEFT | NVG_ALIGN_TOP` для `m_stat_label_scrolls` та `m_language_scroll`.
- [x] `STAT-LABEL-VERIFY-477` — піднято версію до `0.13.477`, виконано збірку в WSL, пройдено тести та `git diff --check`.

## Попередній delivery: v0.13.476 (UTF-16 to UTF-8 decoding & Cyrillic filename fix for MTP)

- [x] `MTP-UTF16-DECODE-476` — реалізовано правильне багатобайтове декодування UTF-16 у UTF-8 для `ReadString` у `ptp_data_parser.hpp` (замість відсікання старших байтів через `static_cast<char>`).
- [x] `MTP-UTF8-ENCODE-476` — реалізовано коректне кодування UTF-8 у UTF-16 для `AddString` у `ptp_data_builder.hpp` для правильної передачі імен файлів/папок з кирилицею у Windows Explorer.
- [x] `MTP-PATCHES-7-8-476` — додано патчі до `patch_libhaze.cmake` з повною перевіркою ідемпотентності.
- [x] `MTP-VERIFY-476` — піднято версію до `0.13.476`, виконано збірку в WSL, пройдено тести та `git diff --check`.

## Попередній delivery: v0.13.475 (Full read-write support with commit for MTP Saves drive)

- [x] `MTP-SAVES-RW-475` — переведено `FsSaveProxy` у повноцінний режим читання-запису (відкриття `FsNativeSave` з `read_only = false` та безпечним fallback).
- [x] `MTP-SAVES-FILE-OPS-475` — реалізовано `CreateFile`, `WriteFile`, `SetFileSize`, `DeleteFile`, `RenameFile` з автоматичним викликом `Commit()`.
- [x] `MTP-SAVES-DIR-OPS-475` — реалізовано `CreateDirectory`, `DeleteDirectoryRecursively`, `RenameDirectory` всередині збережень з автоматичним `Commit()`.
- [x] `MTP-SAVES-FREESPACE-NAME-475` — оновлено `GetFreeSpace` та назву MTP-сховища на `Saves`.
- [x] `MTP-VERIFY-475` — піднято версію до `0.13.475`, зібрано цільовий NRO у WSL, пройдено тести та `git diff --check`.

## Попередній delivery: v0.13.474 (Full MTP property handling, GetObjectPropDesc & SendObjectPropList fixes)

- [x] `MTP-SEND-PROP-LIST-474` — виправлено парсер властивостей у `SendObjectPropList` (`ptp_responder_mtp_operations.cpp`), додано повну підтримку читання й пропуску всіх типів властивостей MTP (замість викиду виключення `ResultUnknownPropertyCode`).
- [x] `MTP-PROP-DESC-BREAK-474` — виправлено пропущений `break;` після `PtpObjectPropertyCode_PersistentUniqueObjectIdentifier` у `GetObjectPropDesc`.
- [x] `MTP-PROP-LIST-ZERO-474` — додано підтримку запиту `property_code == 0` (всі властивості) у `GetObjectPropList` та `ShouldIncludeProperty`.
- [x] `MTP-SET-PROP-NAME-474` — додано підтримку `PtpObjectPropertyCode_Name` у `SetObjectPropValue`.
- [x] `MTP-VERIFY-474` — піднято версію до `0.13.474`, виконано збірку в WSL, пройдено тести та `git diff --check`.

## Попередній delivery: v0.13.473 (Installed Games save scanning & category listing fix)

- [x] `SAVE-BUILD-INSTALLED-473` — оновлено `BuildInstalledAppIds` для перевірки `title::GetMetaEntries` та формування списку встановлених ігор `m_installed_apps`.
- [x] `SAVE-INSTALLED-SCAN-473` — оновлено `ScanHomebrew` для відображення всіх встановлених ігор у `Category::Installed` з прив'язкою активних сейвів або створенням слотів.
- [x] `SAVE-EMPTY-STATES-473` — перевірено та забезпечено коректне відображення стану "Empty..." для порожніх списків у "Видалені ігри" та "Резервні копії".
- [x] `SAVE-RESTORE-NEW-GAME-473` — додано fallback у `PromptSaveTypeOptions` для операції Restore, що дозволяє відновлювати бекапи до встановленої гри навіть до створення першого сейву на консолі.
- [x] `SAVE-VERIFY-473` — піднято версію до `0.13.473`, успішно зібрано бінарник у WSL, пройдено тести та `git diff --check`.

## Попередній delivery: v0.13.472 (MTP folder and file creation storage_id fix)

- [x] `MTP-PTP-STORAGE-ID-472` — виправлено помилку в `SendObjectInfo` (`ptp_responder_ptp_operations.cpp`), де `storage_id` повертався як `parentobj->GetObjectId()` замість `parentobj->GetStorageId()`.
- [x] `MTP-PROP-STORAGE-ID-472` — виправлено помилку в `SendObjectPropList` (`ptp_responder_mtp_operations.cpp`), де `storage_id` повертався як `parentobj->GetObjectId()` замість `parentobj->GetStorageId()`.
- [x] `MTP-LOG-DIR-472` — виправлено помилку логування в `FsProxy::CreateDirectory` у `haze_helper.cpp`.
- [x] `MTP-VERIFY-472` — піднято версію до `0.13.472`, перевірено автоматичне накладання патчів у `patch_libhaze.cmake`, виконано успішну збірку в WSL, пройдено тести та `git diff --check`.

## Попередній delivery: v0.13.471 (Save categories hub & custom save backup search paths)

- [x] `SAVE-HUB-MENU-471` — реалізовано `SaveHubMenu` для початкового вибору категорій збережень (Installed Games, Deleted Games, Backups) при переході з Tools та Main Menu.
- [x] `SAVE-CATEGORY-MODE-471` — додано `Category` enum та відповідну фільтрацію категорій у `save::Menu` із поверненням на B до хабу.
- [x] `SAVE-SEARCH-PATHS-OPS-471` — додано функції `GetBackupSearchPaths`, `AddBackupSearchPath`, `RemoveBackupSearchPath` у `save_paths` зі збереженням у `[save_backup_paths]`.
- [x] `SAVE-CUSTOM-PATHS-SCAN-471` — розширено `CollectBackups` та `ReadBackupEntries` для пошуку резервних копій у налаштованих користувацьких папках.
- [x] `SETTINGS-SAVE-PATHS-471` — додано розділ `Saves -> Save Backup Search Paths` у `Settings` із вибором папок через `filepicker::Menu` та видаленням.
- [x] `SAVE-I18N-DOCS-471` — додано переклади для EN та UK локалізацій, оновлено README.md, plan.md, task.md, walkthrough.md та піднято версію до `0.13.471`.
- [x] `VERIFY-471` — пройдено host unit tests, WSL `ReleaseWithInstall` та `git diff --check`.

## Попередній delivery: v0.13.470 (raw DISA save restore, save discovery & MTP USER:/save)

- [x] `FS-NATIVE-BIS-470` — додано структуру `FsNativeBis final : FsNative` для відкриття BIS-розділів через `fsOpenBisFileSystem`.
- [x] `SAVE-DISA-CHECK-470` — додано `IsDisaSaveFile` (перевірка `DISF` за зміщенням 0x100) та `IsRawSaveCandidate` у `save_paths`.
- [x] `SAVE-RESTORE-RAW-470` — реалізовано пряме відновлення сирових контейнерів DISA у `RestoreSaveInternal` (`save_menu_ops.cpp`) через запис у `/save/<save_data_id>` на BIS-розділах `USER`/`SYSTEM`.
- [x] `SAVE-DISCOVERY-RAW-470` — розширено `CollectBackups` та `ReadBackupEntries` для виявлення сирових файлів сейвів `%016lX`, `*.disa`, `*.bin`.
- [x] `FB-SAVE-RESTORE-470` — додано `RestoreSaveFile` та дію `Restore save data` у File Browser для відновлення сейвів з будь-яких джерел.
- [x] `MTP-RAW-SAVES-470` — додано MTP-сховища `USER:/save` та `SYSTEM:/save` у `haze_helper.cpp`, налаштування у Settings та переклади локалізацій.
- [x] `VERIFY-470` — піднято версію до `0.13.470`, пройдено всі хостові юніт-тести, WSL `ReleaseWithInstall` та `git diff --check`.

## Попередній delivery: v0.13.469 (unified pending UI & updater work)

- [x] `RECOVER-UI-VIEWPORT-469` — відновлено shared `ImageViewport`, forwarder icon crop та інтеграцію Theme Creator.
- [x] `RECOVER-FILE-VIEWER-469` — відновлено актуальний Select Range, multi-line actions і керування текстовим viewer/editor.
- [x] `RECOVER-ACTION-ICONS-469` — відновлено іконки стандартних дій у popup, sidebar та File Browser.
- [x] `RECOVER-UPDATER-469` — відновлено focus, Kefir update badge і reconnect після повернення мережі.
- [x] `RECOVER-HEADER-469` — File Picker використовує `top_row` header slot з колишньої mainline.

## Попередній delivery: v0.13.468 (cURL shutdown & shared handle serialization)

- [x] `CURL-SERIALIZE-468` — серіалізовано доступ до `g_curl_single` у всіх синхронних cURL-операціях.
- [x] `CURL-EARLY-SHUTDOWN-468` — додано ранній `curl::RequestShutdown()` у шлях виходу з App.
- [x] `CURL-EXIT-GUARD-468` — очищення `g_curl_single` захищене тим самим mutex.

## Попередній delivery: v0.13.467 (versioned HTTP User-Agent)

- [x] `HTTP-USER-AGENT-DEF-467` — додати єдину спільну константу `APP_USER_AGENT = "Sphaira/" APP_VERSION;` у `sphaira/include/defines.hpp`.
- [x] `HTTP-USER-AGENT-DOWNLOAD-467` — видалити застарілий `API_AGENT = "TotalJustice"` у `sphaira/source/download.cpp` та перевести `SetCommonCurlOptions` на `APP_USER_AGENT`.
- [x] `HTTP-USER-AGENT-MOUNT-467` — додати встановлення `CURLOPT_USERAGENT` із `APP_USER_AGENT` до `MountCurlDevice::curl_set_common_options()` у `sphaira/source/utils/devoptab_curl_device.cpp`.
- [x] `HTTP-USER-AGENT-AUDIT-467` — оновити `upstream_audit.md` для позначення завершеними upstream комітів `2eabcec` та `3ef698b`.
- [x] `HTTP-USER-AGENT-VERIFY-467` — підняти версію до `0.13.467` у `sphaira/CMakeLists.txt`, оновити living docs, перевірити відсутність застарілих символів, успішно пройти `git diff --check` та WSL `ReleaseWithInstall`.
- [ ] `HW-HTTP-USER-AGENT-467` — перевірити на реальній Switch завантаження через downloader та доступ до віддалених curl-mounts (HTTP/WebDAV/FTP).

## Попередній delivery: v0.13.466 (caller-selected header layout)

- [x] `HEADER-CALLER-PLACEMENT-466` — додати `bool top_row = false` до `SetTitleSubHeading`, зберігати вибраний слот і скидати scroll state при зміні слота або очищенні.
- [x] `HEADER-DRAW-CHROME-466` — малювати довільний підзаголовок у верхньому широкому слоті після версії або, для compact summary, на звичному нижньому рядку без автоматичного стрибка.
- [x] `HEADER-CALLERS-MIGRATION-466` — перенести шляхи та назви у File Browser, Homebrew, GHDL, Games, Cheats, Save Menu, App Store та IRS на верхній рядок; Themezer, Title ID і App Store filter summary лишити внизу.
- [x] `HEADER-VERIFY-466` — підняти версію до `0.13.466`, оновити living docs, пройти host tests, WSL `ReleaseWithInstall` та `git diff --check`.

## Попередній delivery: v0.13.465 (text editor multi-line editing)

- [x] `TEXT-RANGE-SELECTION-465` — вибір діапазону рядків у режимі Edit через Actions popup (`Select range` / `Clear selection`), під час вибору A = `Finish selection`, B = `Cancel selection`; активний діапазон підсвічується напівпрозорою focus-смугою (alpha 0.35).
- [x] `TEXT-LINE-CLIPBOARD-465` — міжфайловий локальний буфер обміну рядками (`s_line_clipboard`) для дій Copy, Cut, Paste below та Delete над виділеним діапазоном або поточним рядком; видалення завжди зберігає щонайменше 1 порожній рядок; 1 undo snapshot для мутуючих операцій (0 для Copy).
- [x] `TEXT-INI-COMMENT-465` — коментування (`Comment`) та розкоментування (`Uncomment`) рядка або виділеного діапазону для INI файлів із збереженням пробілів/табуляцій; чисті функції `CommentIniLine` та `UncommentIniLine` у `text_helper.hpp` з хостовими юніт-тестами.
- [x] `TEXT-ACTIONS-UNDO-REDO-465` — інтеграція пунктів `Undo` та `Redo` у спливаюче вікно Actions; динамічний заголовок дій `Line actions (line %zu)` або `Line actions (lines %zu - %zu)`.
- [x] `TEXT-VERIFY-465` — підняти версію до `0.13.465` у `sphaira/CMakeLists.txt`, оновити living docs (`task.md`, `plan.md`, `walkthrough.md`), перевірити host unit tests, WSL ReleaseWithInstall та `git diff --check`.

## Попередній delivery: v0.13.460 (text editor basics)

- [x] `TEXT-EDIT-VIEW-BACK-460` — кнопка B у режимі редагування повертає до режиму
  перегляду (View) для того самого відкритого документа зі збереженням рядків,
  позиції скролу, dirty стану та прав редагування; B у View запитує підтвердження
  Save/Discard/Cancel для зміненого документа.
- [x] `TEXT-EDIT-WRAP-GATE-460` — утримання Down на останньому рядку або Up на
  першому рядку зупиняється на межі без зацикленого повторення; новий фізичний
  натиск виконує перехід (wrap); звичайний скрол List для інших меню не змінено.
- [x] `TEXT-INI-SYNTAX-TOGGLE-460` — виділений рядок у редакторі INI зберігає синтаксичні
  кольори та розмір шрифту; розширено `ToggleIniBoolean` для підтримки перемикання 0 ↔ 1
  із збереженням коментарів/пробілів та валідацією багатозначних чисел.
- [x] `TEXT-ZL-ZOOM-460` — у read-only text viewer `ZL` + D-pad або вертикальний
  стік масштабує текст, а відпускання `ZL` не гортає після zoom chord; без
  modifier вертикальні inputs лишаються viewport scrolling.
- [x] `TEXT-EDIT-VERIFY-460` — підняти версію до 0.13.460, додати хостові юніт-тести
  для 0/1 boolean toggle, пройти host tests, WSL ReleaseWithInstall та `git diff --check`.

## Попередній delivery: v0.13.458 (Homebrew settings & search paths)

- [x] `HB-SETTINGS-CAT-458` — додати окрему категорію `Homebrew` у ліве меню
  Settings одразу після `General`, перенести туди `Forwarders` (з Install),
  `Homebrew App Store` (з Software) та `Replace hbmenu on exit` (з General) без
  дублювання.
- [x] `HB-SEARCH-PATHS-458` — додати менеджер `Homebrew Search Paths` у
  Settings: вибір тільки SD-папок через FilePicker, збереження у конфіг, список з
  видаленням через діалог підтвердження та миттєве оновлення списку Homebrew. Шлях
  `/switch` залишається незмінним, системним та не дублюється в конфігу custom paths.
- [x] `HB-RELEASE-VERIFY-458` — оновити 13 локалізацій (без `ru.json`),
  підняти версію до 0.13.458, оновити living docs, перевірити валідність JSON,
  пройти host tests, WSL ReleaseWithInstall та `git diff --check`; лишити Switch
  smoke-test.

## Попередній delivery: v0.13.457 (text viewer viewport scrolling)

- [x] `TEXT-VIEWPORT-SCROLL-457` — у read-only text viewer Up/Down і стіки
  одразу зміщують видиму область на один рядок без послідовного руху фокуса до
  межі viewport; для streamed файла безперервно переходять між буферними
  сторінками, не читаючи весь файл у пам'ять.
- [x] `TEXT-VIEWPORT-VERIFY-457` — зберегти L/R, ZL/ZR, L + right-stick і
  touch controls, підняти версію, оновити living docs, пройти host tests, WSL
  ReleaseWithInstall і `git diff --check`; лишити Switch smoke-test.

## Попередній delivery: v0.13.456 (text viewer pager)

- [x] `TEXT-OPEN-AS-456` — у File Browser додати для кожного файла контекстну
  дію `View as text`, незалежно від розширення, без зміни пріоритету A-дій
  (NRO/install/image/archive/association).
- [x] `TEXT-STREAM-456` — для великих файлів read-only viewer не завантажує
  весь текст або 4 MiB preview у RAM: читає поточну сторінку та малий bounded
  cache кількох наступних сторінок, з line-wise scrolling і без Edit.
- [x] `TEXT-PAGER-456` — `L`/`R` гортають по сторінці на release, `ZL`/`ZR`
  — на 10 сторінок; D-pad/стік прокручують рядками; `L` + правий стік змінює
  масштаб, а pinch змінює масштаб touch-ом. Footer показує керування, але
  paging/zoom hints не клікабельні.
- [x] `TEXT-VERIFY-456` — підняти версію, оновити living docs, додати
  найменшу host-перевірку pager logic, пройти `tests/run.sh`, WSL
  `ReleaseWithInstall` і `git diff --check`; лишити Switch smoke-test.

## Попередній delivery: v0.13.455 (INI text viewer spacing)

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
