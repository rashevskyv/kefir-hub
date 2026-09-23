# task.md

Версія коду: **v0.13.877**.

## Поточний delivery: v0.13.877 — Transfer structural split (завершено)

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

- [ ] `SPLIT-YATI`
- [ ] `SPLIT-PROVIDER-UI`
- [ ] `TEST-RUNNER`
- [ ] `APP-HEADER-DEBT` — оцінити `app.hpp` (626) як окремий вузький refactor.

Історія завершених checklist-ів доступна через Git і тут не дублюється.
