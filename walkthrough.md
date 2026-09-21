# walkthrough.md

Актуальний shipped delivery — **v0.13.872** (2026-09-21).

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
