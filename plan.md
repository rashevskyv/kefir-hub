# plan.md

Версія коду: **v0.13.878**.

## Поточний delivery: v0.13.878 — File-size audit closure (завершено)

Одна безперервна Gemini-передача для решти size/checks аудиту: завершити й перевірити Yati split, розкласти provider-heavy UI за API/parsing/storage/UI, пройти 32 oversized `source/ui/menus` файли за когезивними межами, дати 18 Python contracts discoverable запуск у `tests/run.sh`, зменшити 11 oversized test scripts без втрати safety cases, окремо перевірити `app.hpp` на безпечні dead declarations. Не робити механічних поділів заради числа; неподоланні винятки документувати фактично. Один patch-version `0.13.878`, фінальні host contracts + WSL build після всіх змін. Senior перевіряє весь diff, оновлює delivery-документи й комітить; Gemini не чіпає docs/бінарний ROMFS і не комітить.

Результат: 150 змінених/нових source/test файлів ≤600 рядків; усі 152 menu `.cpp/.hpp` ≤600. Yati, provider UI, інші menu units, test fixtures і `app.hpp` пройдені. Gemini повідомив про `tests/run.sh` і WSL `ReleaseWithInstall` PASS; senior повторно виконав 18 Python contracts, dead-symbol gate та перевірив CMake/розміри/whitespace. C++ contract після точкового звуження source boundary окремо не перезапускався. Це закриває A5–A7, а не всі oversized файли репозиторію: 22 інші C/C++/test файли та `sphaira/CMakeLists.txt` (754 рядки) лишаються поза поточним обсягом.

## Попередній delivery: v0.13.877 — Transfer structural split (завершено)

`threaded_file_transfer.cpp` зменшено з 1 956 до 263 рядків без зміни public API. Core, archive preflight, native verification і ZIP I/O винесено в чотири конкретні `.cpp` та три приватні headers (17–501 рядок). `yati.cpp` — окремий наступний delivery; дозволена фонова зміна `TegraExplorer.bin` поза цим комітом.

Gemini повідомив про WSL `ReleaseWithInstall` build `[100%] Built target sphaira_nro`, 18/18 Python contracts, dead-symbol gate (981 declarations) та whitespace PASS. Senior перевірив diff, CMake, version, межі файлів і перенесені source assertions; не запускав тести чи збірку повторно. Консольний runtime ще не перевірено.

## Попередній delivery: v0.13.876 — App structural split

`app.cpp` зменшено з 2 111 до 523 рядків, `app_settings.cpp` — до 554. Startup, loop/frame, UI stack, USB/MTP, network і save settings розкладено у конкретні units ≤597 рядків; public `app.hpp` лишився незмінним (626 рядків, окремий legacy debt). Gemini повідомив про успішний WSL `ReleaseWithInstall`, 18/18 contracts, dead-symbol gate, EN/UK parity і whitespace checks.

## Поза поточним delivery

Інші non-UI files понад 600 рядків не входять в A5–A7 чергу: не розкладати platform adapters, CMake, embedded templates чи табличні дані механічно. Повернутися лише з конкретним maintenance outcome після прийняття поточного delivery.

## Межі

- Один product delivery за раз на primary `master`.
- Нові source/test/instruction files — не більше 600 physical lines.
- Не додавати abstraction або dependency заради самого поділу.
- Не запускати compile/build до завершення всіх code/test edits поточного delivery.
- `assets/romfs/tegra/TegraExplorer.bin` — дозволена фонова зміна, поза delivery.

Історія попередніх delivery доступна через Git; активні документи її не дублюють.
