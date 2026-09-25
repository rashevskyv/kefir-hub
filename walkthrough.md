# walkthrough.md

Актуальний shipped delivery — **v0.13.880** (2026-09-25).

## v0.13.880 — DBI-бекапи в каталозі

`ReadArchiveSaveMetadata` тепер переносить уже порахований `payload_count` разом із метаданими. Раніше лічильник обнулявся, і `InspectBackupArchive` відкидав DBI ZIP як порожній. На SD виявлено 21 ZIP із payload; ще 9 ZIP містять лише метадані й залишаються відхиленими. Змінено один рядок коду, версію піднято до `0.13.880`.

Gemini повідомив про `git diff --check` PASS; senior перевірив diff, усі гілки перенесення результату та розміри файлів. Збірку й консольний runtime не виконували. Потрібна перевірка зібраної версії в Saves → Backups на Switch. Фоновий `TegraExplorer.bin` не включено.

## v0.13.879 — remaining file-size audit closure

25 remaining first-party code/build/test files понад 600 рядків розділено за відповідальністю, а не за довільними діапазонами. Нові 53 файли покривають account/NAND restore, FS/FTP/MTP/Curl, Haze save scan, title/owo, UI, HBL environment, CMake та path-util security tests. Усі 620 перевірених файлів у `sphaira`, `hbl`, `tests` тепер ≤600 рядків; `sphaira/CMakeLists.txt` має 461. C++ save-restore contract лишився незміненим і, за звітом Gemini, пройшов 40/40. Нові translation units зареєстровані в CMake; окремий security test підхоплює наявний `tests/run.sh` glob.

Gemini повідомив про WSL `tests/run.sh` all green, dead-symbol gate 1033/1033, `git diff --check` і `ReleaseWithInstall` `[100%] Built target sphaira_nro` PASS. Senior незалежно перевірив primary `master`, фактичні file sizes, реєстрацію 32 нових `.cpp/.c`, source-level save/MTP contracts, тестовий diff і staged whitespace; не запускав збірку чи тести повторно. Ручна консольна перевірка save/restore та MTP ще попереду. Дозволений `TegraExplorer.bin` не входить у delivery.

## v0.13.878 — Yati, menu та tests structural split

`yati.cpp` скорочено до 450 рядків; analysis, приватні installer types, worker pipeline і ticket/CNMT metadata отримали окремі units. Provider-heavy UI та решту 32 oversized menu файлів розділено за API/parsing, UI/draw, operations і storage; усі 152 файли `source/ui/menus` (`.cpp/.hpp`) тепер ≤600 рядків. `app.hpp` зменшено до 590: невикористані поля/типи вилучено, `GetAccountList` винесено в `app.cpp` без зміни логіки. `tests/run.sh` запускає всі 18 Python contracts; 11 великих сценарних suite розкладено на `contract_fixtures` без додаткового framework. Усього 150 змінених/нових source/test файлів ≤600; CMake (754) та 22 інші oversized C/C++/test файли не входили до A5–A7.

Gemini повідомив про успішний WSL `ReleaseWithInstall` (`sphaira_nro` і `kefir-hub_nro`) та `tests/run.sh` all green. Senior незалежно перевірив primary `master`, реєстрацію всіх 53 нових `.cpp` у CMake, розміри, `git diff --check`, dead-symbol gate (1033/1033) і 18/18 Python contracts. У `test_save_restore_contract.cpp` звужено source boundary до конкретних функцій, щоб інший overload не міг випадково задовольнити assertions; C++ test після цього точкового тестового редагування не перезапускався. Консольний runtime не перевірено. Фоновий `TegraExplorer.bin` не входить у delivery.

## v0.13.877 — Transfer structural split

`threaded_file_transfer.cpp` зменшено з 1 956 до 263 рядків. Transfer engine/wrappers, archive path validation і preflight, native verification та ZIP I/O винесено в чотири конкретні `.cpp` і три мінімальні приватні headers; усі нові файли мають 17–501 рядок. Public transfer API, ZIP/path admission, cancellation і checked-native-save ordering не змінювали навмисно. CMake явно реєструє нові units, а контрактні тести переведено на нові source locations і версію `.877` без вилучення safety assertions.

Gemini повідомив про WSL `cmake --build --preset ReleaseWithInstall --parallel 4` з exit 0 та `[100%] Built target sphaira_nro`, 18/18 Python contracts, dead-symbol gate (981 declarations) і `git diff --check` PASS. Senior перевірив фактичний diff, CMake/version і ліміт рядків, але не запускав збірку чи тести повторно. C++ contract і консольний runtime окремо не підтверджено; дозволений `TegraExplorer.bin` лишився поза delivery.

## v0.13.876 — App structural split

`app.cpp` зменшено з 2 111 до 523 рядків, `app_settings.cpp` — до 554. Startup і callbacks, loop/frame, widget stack, USB/auto-MTP, MTP/network/save settings винесено у конкретні source units; `FrameBufferSize` має один приватний header. Усі нові App-файли мають 14–597 рядків. Public `app.hpp` і layout `App` не змінено; header лишається legacy debt на 626 рядків. Деструктор та install-session admission залишено в `app.cpp` для збереження shutdown contract.

Під час фінальної збірки Gemini виправив назву applet performance getter і залишив реалізацію `nanovg_dk.h` лише в одному translation unit. За його звітом WSL `ReleaseWithInstall` завершився exit 0 з `[100%] Built target sphaira_nro`; 18/18 Python contracts після оновлення 11 version allowlists, dead-symbol gate (981 declarations), EN/UK 2549/2549 key parity та whitespace checks пройшли. Senior перевірив фактичний diff, CMake/version і розміри, але не запускав збірку або тести повторно. Device/runtime перевірка не виконувалася; дозволений `TegraExplorer.bin` лишився поза delivery.

## v0.13.875 — Web structural split

`web.cpp` зменшено з 2 381 до 538 рядків; `web_pages.hpp` — з 1 010 до 5. Десять наявних HTML/CSS/JS raw-string payloads без зміни вмісту рознесено за modal, folder і remote headers. mDNS responder, mount/FS helpers, upload handlers, file routes та HTTP router перенесено у п’ять конкретних implementation units із приватними headers; усі нові файли мають 9–576 рядків. Public Web API, route ordering і server shutdown залишені стабільними.

Gemini повідомив про WSL `ReleaseWithInstall` exit 0 з `[100%] Built target sphaira_nro`, 18/18 Python contracts після корекції 11 version allowlists і shutdown source-location assertion, dead-symbol gate (981 declarations), EN/UK 2549/2549 parity, 10/10 byte-identical page payloads та `git diff --check` PASS. Senior перевірив diff, version/CMake, file-size gate і тестові assertions; повторної збірки чи device/browser runtime test не проводив. Дозволений `TegraExplorer.bin` лишився поза delivery.

## v0.13.874 — Save operations split

`save_menu_ops.cpp` зменшено з 2 696 до 301 рядка. Backup publication і ZIP writer, ZIP/folder restore, remote sync та deletion перенесено у шість concrete implementation units і чотири приватні headers (9–544 рядки). CMake явно реєструє нові `.cpp`; public `Menu` API та save formats не змінені. Контрактні тести перенаправлено на нові source locations без вилучення safety-перевірок.

Gemini під час фінальної збірки прибрав неіснуючий `types.hpp` include із `save_remote_sync.hpp` і повідомив про WSL `ReleaseWithInstall` exit 0 з `[100%] Built target sphaira_nro`, 18/18 Python contracts, dead-symbol gate (981 declarations), EN/UK 2549/2549 parity та `git diff --check` PASS. Senior перевірив diff, version/CMake, ліміт рядків і safety ordering; повторної збірки чи device/runtime test не проводив. Дозволений `TegraExplorer.bin` лишився поза delivery.

## v0.13.873 — Save menu UI/catalog split

`save_menu.cpp` зменшено з 2 788 до 308 рядків. Draw/layout, filter UI, operation options, installed-save scan, restore-target lookup, backup catalog і action prompts рознесено у сім конкретних implementation units (166–573 рядки). Спільні tab geometry, save-type label і change event винесено у 28-рядковий private header; public `save_menu.hpp` і class state не змінені. CMake має явні source entries; source-location assertions у contracts оновлено без зміни safety models.

Gemini повідомив про WSL `ReleaseWithInstall` exit 0 з `[100%] Built target sphaira_nro`, 18/18 repository contract suites, dead-symbol gate (981 declarations), EN/UK exact 2549/2549 parity і `git diff --check` PASS. Senior перевірив diff, CMake/version, private include paths і line cap; повторної збірки чи runtime/device test не проводив. Дозволений `TegraExplorer.bin` лишився поза delivery.

## v0.13.872 — Save paths structural split

`save_paths.cpp` зменшено з 1 794 до 463 рядків без зміни public `save_paths.hpp`. Exact installed-save discovery винесено в `save_discovery.cpp`; bounded JKSV/Sphaira/DBI decoders — у `save_metadata_decoders.cpp`; archive metadata admission — у `save_archive_metadata.cpp`; backup inspection, grouping та ZIP integrity — у `save_backup_inspection.cpp`. Спільні concrete helpers живуть у приватному `save_internal.hpp`; нових interfaces, factories або dependencies немає.

Усі нові C/C++ units мають 84–555 рядків. Під час фінальної збірки додано лише прямий `log.hpp`, якого бракувало після перенесення discovery. WSL `ReleaseWithInstall` завершився `[100%] Built target sphaira_nro`; 18/18 repository contract suites, dead-symbol gate (981 declarations), EN/UK exact 2549/2549 parity та `git diff --check` пройшли. Device/runtime перевірка не виконувалася; дозволений `TegraExplorer.bin` лишився поза delivery.

## v0.13.871 — dead-code cleanup and build closure

Видалено unreachable `App::DisplayAdvancedOptions` і misc-menu factory, неінстанційований install `stream::Menu` разом із active-menu callback route, тестовий `yati::source::StreamFile` та невикористані `GetWebdavUrl/User/Pass`. Живі install/forwarder/dump options, `stream::Stream`, background MTP/FTP installer, DBI session і named WebDAV locations збережені. Product cleanup прибрав приблизно 550 рядків без нової абстракції або dependency.

Фінальний build відкрив накопичені compile blockers попередніх Save deliveries. Виправлено назву `GetFsOpenResult`, поле `UnzipPayloadSummary::file_bytes`, точне порівняння `FsSaveDataInfo` з attribute, явний `FsSaveDataSpaceId` cast, зайвий `title.hpp` include і неіснуючий `ShouldCancel`; обидва Save units лишилися 592/598 рядків.

WSL `ReleaseWithInstall` завершився `[100%] Built target sphaira_nro`. Усі 18 compiler-free contract suites, dead-symbol gate (981 declarations), EN/UK JSON parse та exact 2549/2549 key parity, `git diff --check` пройшли. Device/runtime перевірка не виконувалася; дозволений `TegraExplorer.bin` лишився поза delivery.

## v0.13.870 — Game Tools save-slot manager

Selected-game `Saves` використовує shared authoritative discovery і зберігає повний `FsSaveDataInfo`. Рядки показують nickname/type, space, rank, index, save ID та allocated/data/journal sizes без Account UID. Exact extra-data read failure видимий і блокує grow без remap або fallback.

Create обмежено installed-title Account/User/Primary/index-0 та explicit nickname-only local user. NACP defaults і aligned +16/+64 MiB presets проходять plan → confirm → worker → `CreateSaveDataChecked`. Grow використовує retained exact identity через `ExtendSaveDataChecked`; cancellation, invalid size і no-op не доходять до IPC. Verified success або post-IPC uncertainty запускають authoritative refresh.

Реалізація: game-side helper/header/test — 372/36/477 рядків. Перевірено 18 compiler-free Python suites, dead-symbol gate (983 declarations), EN/UK JSON parse та exact 2549-key parity, `git diff --check`: PASS. Compile/WSL/NRO/libnx IPC/Switch hardware не запускалися. Дозволена фонова зміна `TegraExplorer.bin` не входила в delivery і не валідувалася.

Попередня історія delivery доступна через Git (`git log` / `git show`) і не дублюється в активному контексті агента.
