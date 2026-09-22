# walkthrough.md

Актуальний shipped delivery — **v0.13.875** (2026-09-22).

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
