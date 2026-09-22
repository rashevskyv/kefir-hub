# task.md

Версія коду: **v0.13.876**.

## Поточний delivery: Transfers/Yati — scoping

- [ ] `MAP-TRANSFER` — вибрати один pipeline boundary і перевірити callers/safety ordering.
- [ ] `SPLIT-TRANSFER-YATI` — виконати обмежений structural split після окремого handoff.

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

- [ ] `SPLIT-PROVIDER-UI`
- [ ] `TEST-RUNNER`
- [ ] `APP-HEADER-DEBT` — оцінити `app.hpp` (626) як окремий вузький refactor.

Історія завершених checklist-ів доступна через Git і тут не дублюється.
