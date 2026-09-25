# plan.md

Версія коду: **v0.13.881**.

## Поточний delivery: v0.13.881 — відновлення сейву без встановленої гри (завершено)

Після перевірки архіву створення Account save slot для локального користувача використовує валідні owner ID і вирівняні розміри з ZIP, коли NACP гри недоступний. Перевіряються title ID, тип, ранг, індекс, підтвердження перед створенням і сам створений слот; архів без потрібних метаданих відхиляється. Інспекцію архіву винесено в `save_slot_admission.cpp`, щоб не перевищувати 600 рядків. Версія `0.13.881`.

Gemini повідомив про PASS локального `test_save_slot_backend_contract.py`, EN/UK parity (2550 keys) і `git diff --check`; senior перевірив diff, межі метаданих, CMake, розміри та primary `master`. Збірку й runtime не виконували. На Switch потрібно перевірити відновлення з DBI ZIP для невстановленої гри та користувача, якого обрано в UI. ROMFS binary не входить у delivery.

Наступний окремий серійний delivery: розділювачі походження в Backups. Власні DBI-сумісні архіви Kefir Hub потрібно відрізняти за наявним ZIP-коментарем `sphaira v`, а сторонні джерела позначати лише тоді, коли їх походження можна визначити достовірно.

## Попередній delivery: v0.13.880 — DBI-бекапи в каталозі (завершено)

У `ReadArchiveSaveMetadata` збережено `payload_count` під час перенесення розібраних метаданих у результат. Це прибирає хибне відхилення DBI ZIP із файлами сейву в каталозі Backups. На змонтованій SD-картці знайдено 30 DBI ZIP: 21 із payload і 9 лише з метаданими; останні залишаються відхиленими чинною перевіркою безпеки. Зміна охоплює один рядок коду та patch version `0.13.880`.

Gemini виконав `git diff --check`; senior перевірив diff, гілки присвоєння результату, розміри файлів і стан основного `master`. Компіляцію та runtime на консолі не виконували за політикою звичайних змін. Наступна перевірка: зібрати нову версію та відкрити Saves → Backups на Switch. Фоновий `TegraExplorer.bin` поза delivery.

## Попередній delivery: v0.13.879 — repo-wide file-size closure (завершено)

Решту 25 first-party файлів понад 600 рядків розділено за конкретними обов’язками: account/restore і NAND, FS/FTP/MTP/Curl adapters, Haze save scan, title/owo, UI primitives, HBL environment, path-util security tests і CMake. Нові 53 файли та всі 620 перевірених code/build/test файлів у `sphaira`, `hbl`, `tests` мають ≤600 фізичних рядків. Публічні контракти та перевірки save-restore лишилися в обсязі; `test_save_restore_contract` тепер пройшов заявлені Gemini 40/40. Версія збільшена один раз до `0.13.879`.

Gemini повідомив про WSL `tests/run.sh` all green, dead-symbol 1033/1033, `git diff --check` і `ReleaseWithInstall` (`sphaira_nro`) PASS. Senior перевірив первинний `master`, ліміт розміру, CMake-реєстрацію нових translation units, source-level межі save/MTP та тестові зміни; збірку й тести повторно не запускав. Device/runtime save-flow ще потребує ручної перевірки. `TegraExplorer.bin` — дозволена фонова зміна, не частина delivery.

## Попередній delivery: v0.13.878 — File-size audit closure (завершено)

Одна безперервна Gemini-передача для решти size/checks аудиту: завершити й перевірити Yati split, розкласти provider-heavy UI за API/parsing/storage/UI, пройти 32 oversized `source/ui/menus` файли за когезивними межами, дати 18 Python contracts discoverable запуск у `tests/run.sh`, зменшити 11 oversized test scripts без втрати safety cases, окремо перевірити `app.hpp` на безпечні dead declarations. Не робити механічних поділів заради числа; неподоланні винятки документувати фактично. Один patch-version `0.13.878`, фінальні host contracts + WSL build після всіх змін. Senior перевіряє весь diff, оновлює delivery-документи й комітить; Gemini не чіпає docs/бінарний ROMFS і не комітить.

Результат: 150 змінених/нових source/test файлів ≤600 рядків; усі 152 menu `.cpp/.hpp` ≤600. Yati, provider UI, інші menu units, test fixtures і `app.hpp` пройдені. Gemini повідомив про `tests/run.sh` і WSL `ReleaseWithInstall` PASS; senior повторно виконав 18 Python contracts, dead-symbol gate та перевірив CMake/розміри/whitespace. C++ contract після точкового звуження source boundary окремо не перезапускався. Це закриває A5–A7, а не всі oversized файли репозиторію: 22 інші C/C++/test файли та `sphaira/CMakeLists.txt` (754 рядки) лишаються поза поточним обсягом.

## Попередній delivery: v0.13.877 — Transfer structural split (завершено)

`threaded_file_transfer.cpp` зменшено з 1 956 до 263 рядків без зміни public API. Core, archive preflight, native verification і ZIP I/O винесено в чотири конкретні `.cpp` та три приватні headers (17–501 рядок). `yati.cpp` — окремий наступний delivery; дозволена фонова зміна `TegraExplorer.bin` поза цим комітом.

Gemini повідомив про WSL `ReleaseWithInstall` build `[100%] Built target sphaira_nro`, 18/18 Python contracts, dead-symbol gate (981 declarations) та whitespace PASS. Senior перевірив diff, CMake, version, межі файлів і перенесені source assertions; не запускав тести чи збірку повторно. Консольний runtime ще не перевірено.

## Попередній delivery: v0.13.876 — App structural split

`app.cpp` зменшено з 2 111 до 523 рядків, `app_settings.cpp` — до 554. Startup, loop/frame, UI stack, USB/MTP, network і save settings розкладено у конкретні units ≤597 рядків; public `app.hpp` лишився незмінним (626 рядків, окремий legacy debt). Gemini повідомив про успішний WSL `ReleaseWithInstall`, 18/18 contracts, dead-symbol gate, EN/UK parity і whitespace checks.

## Поза поточним delivery

Device/runtime перевірка save/restore і MTP. Ліміт 600 рядків застосовано до first-party code/build/test файлів у `sphaira`, `hbl`, `tests`, але не до табличних даних і згенерованих артефактів.

## Межі

- Один product delivery за раз на primary `master`.
- Нові source/test/instruction files — не більше 600 physical lines.
- Не додавати abstraction або dependency заради самого поділу.
- Не запускати compile/build до завершення всіх code/test edits поточного delivery.
- `assets/romfs/tegra/TegraExplorer.bin` — дозволена фонова зміна, поза delivery.

Історія попередніх delivery доступна через Git; активні документи її не дублюють.
