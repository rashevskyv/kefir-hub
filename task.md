# task.md

Версія коду: **v0.13.883**.

## Поточний delivery: v0.13.883 — компактний перший розділювач бекапів (завершено)

- [x] `GRID-883` — прибрано порожній перший ряд у плитковому вигляді Backups; підпис джерела лишився під вкладками.
- [x] `VERIFY-883` — контрактний тест секцій пройшов; перевірено diff, кількість рядків і whitespace; збірка та консольний runtime очікують перевірки.
- [x] `DOCS-BUMP-883` — patch version і delivery-документи синхронізовано.
- [x] `COMMIT-883` — focused commit на primary master без ROMFS binary.

## Попередній delivery: v0.13.882 — походження та папкові бекапи (завершено)

- [x] `MAP-882` — знайдено групування за identity, розділювач live/backup, наявний folder-stage/restore і обмеження створення нового слота.
- [x] `SOURCE-882` — достовірне походження, окремі групи/розділювачі DBI, JKSV, Checkpoint, Kefir Hub та інших джерел.
- [x] `FOLDER-882` — безпечна видимість і restore папкових бекапів JKSV/Checkpoint, включно з перевіркою відсутньої цілі.
- [x] `VERIFY-882` — WSL `ReleaseWithInstall` (`sphaira_nro`), цільові контракти, EN/UK parity, diff і розміри; консольний runtime окремо.
- [x] `DOCS-BUMP-882` — patch version і документи.
- [x] `COMMIT-882` — focused primary-master commit без ROMFS binary.

## Попередній delivery: v0.13.881 — відновлення сейву без встановленої гри (завершено)

- [x] `MAP-881` — знайдено NACP-only gate у `PlanAccountSaveCreation`; ZIP уже несе metadata owner/size.
- [x] `RESTORE-881` — безпечне планування слота з валідних метаданих архіву за відсутності NACP.
- [x] `VERIFY-881` — Gemini: Python contract, EN/UK parity, whitespace; senior: diff/межі/CMake/розміри; консольний runtime окремо.
- [x] `DOCS-BUMP-881` — patch version і delivery-документи.
- [x] `COMMIT-881` — focused commit на primary master без ROMFS binary.

## Попередній delivery: v0.13.880 — DBI-бекапи в каталозі (завершено)

- [x] `MAP-880` — перевірено реальні DBI ZIP на SD, сканер, інспектор архівів і втрату `payload_count`.
- [x] `FIX-880` — збережено кількість payload під час копіювання розібраних метаданих; metadata-only і invalid лишаються відхиленими.
- [x] `VERIFY-880` — Gemini: `git diff --check`; senior: diff, source flow, file-size та primary-master review; без збірки й device test.
- [x] `DOCS-BUMP-880` — версію та delivery-документи синхронізовано.
- [x] `COMMIT-880` — focused primary-master commit без push; ROMFS binary виключено.

## Попередній delivery: v0.13.879 — remaining file-size audit (завершено)

- [x] `SPLIT-879` — 25 remaining oversized first-party targets розділено на когезивні units; 53 нові файли.
- [x] `SIZE-879` — усі 620 перевірених code/build/test файлів у `sphaira`, `hbl`, `tests` ≤600 рядків.
- [x] `VERIFY-879` — Gemini: WSL `tests/run.sh` all green, save-restore C++ 40/40, dead-symbol 1033/1033, `ReleaseWithInstall` і whitespace PASS; senior: diff/CMake/size/source review, без повторної компіляції.
- [x] `DOCS-BUMP-879` — версію й delivery-документи синхронізовано.
- [x] `COMMIT-879` — focused primary-master commit без push; ROMFS binary виключено.

## Попередній delivery: v0.13.878 — File-size audit closure (завершено)

- [x] `MAP-YATI-878` — graph/source/callers перевірені; analysis/planning seam визначено.
- [x] `SPLIT-ANALYSIS-878` — NCZ analysis і вибір носія винесені; senior перевірив diff, compile ще попереду.
- [x] `SPLIT-TYPES-878` — спільні приватні типи інсталятора винесені в `yati_internal.hpp` (387 рядків); compile ще попереду.
- [x] `SPLIT-PIPELINE-878` — worker read/decompress/write винесені в 512-рядковий unit; compile ще попереду.
- [x] `SPLIT-INSTALL-878` — ticket/CNMT і threaded pipeline розділені; senior перевірив структуру, compile ще попереду.
- [x] `SPLIT-PROVIDER-878` — 7 provider-heavy targets розділені на units ≤590; Gemini WSL build PASS, senior structure/CMake review PASS.
- [x] `SPLIT-UI-878` — 32 oversized menu files розділені; всі 152 menu source/header файли ≤600.
- [x] `TEST-RUNNER-878` — 18 Python contracts discoverable в existing runner.
- [x] `SPLIT-TESTS-878` — 11 oversized suites розділені за сценаріями без вилучення safety cases.
- [x] `APP-HEADER-878` — dead declarations прибрані; `GetAccountList` перенесено без зміни логіки.
- [x] `SIZE-878` — 150 змінених/нових source/test файлів ≤600; legacy CMake та 22 інші oversized файли поза цим delivery.
- [x] `VERIFY-878` — Gemini: WSL build і `tests/run.sh` PASS; senior: 18/18 Python, dead-symbol 1033/1033, CMake/розміри/whitespace PASS. C++ test після точкового звуження source boundary не перезапускався.
- [x] `DOCS-BUMP-878` — version/docs синхронізовано.
- [x] `COMMIT-878` — focused primary-master commit без push.

## Попередній delivery: v0.13.877 — Transfer structural split (завершено)

- [x] `MAP-TRANSFER-877` — transfer core і archive safety boundary підтверджені graph + source review.
- [x] `SPLIT-CORE-877` — transfer engine/wrappers винесені в 499-рядковий unit; фінальна збірка ще попереду.
- [x] `SPLIT-ARCHIVE-877` — ZIP/path/preflight/verification розкладені за стабільними межами; збірка ще попереду.
- [x] `SIZE-877` — нові C/C++ files ≤501 рядка, `threaded_file_transfer.cpp` зменшено до 263 рядків.
- [x] `VERIFY-877` — Gemini: WSL `sphaira_nro`, 18/18 Python contracts, dead-symbol 981 і whitespace PASS.
- [x] `DOCS-BUMP-877` — version/docs синхронізовано.
- [x] `COMMIT-877` — focused primary-master commit без push.

## Попередній delivery: v0.13.876 — виконано

- [x] `MAP-APP-876` — lifecycle/state/callers підтверджені graph + source review.
- [x] `DEAD-DECL-876` — кандидати в `app.hpp` перевірені; layout/ініціалізаційні ефекти залишені для окремого delivery.
- [x] `SPLIT-APP-876` — App/settings responsibilities винесені без навмисної зміни поведінки.
- [x] `API-CMAKE-876` — public API стабільний; нові units явно зареєстровані.
- [x] `SIZE-876` — усі нові App files ≤597 рядків; `app.cpp` 523, `app_settings.cpp` 554.
- [x] `VERIFY-876` — Gemini: WSL NRO build, 18/18 contracts, dead-symbol 981, EN/UK 2549/2549 і whitespace PASS.
- [x] `DOCS-BUMP-876` — version/docs синхронізовано.
- [x] `COMMIT-876` — focused primary-master commit без push.

## Подальша черга

Ручний device/runtime тест save/restore та MTP; structural size queue для перевіреної області закрита.

Історія завершених checklist-ів доступна через Git і тут не дублюється.
