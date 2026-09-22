# task.md

Версія коду: **v0.13.875**.

## Поточний delivery: v0.13.876 — App structural split

- [ ] `MAP-APP-876` — lifecycle/state/callers підтверджені graph + source review.
- [ ] `DEAD-DECL-876` — dead declarations у `app.hpp` перевірені перед видаленням.
- [ ] `SPLIT-APP-876` — cohesive App/settings units виділено без behavior change.
- [ ] `API-CMAKE-876` — public API стабільний; нові units явно зареєстровані.
- [ ] `SIZE-876` — нові C/C++ files ≤600 рядків; legacy units materially reduced.
- [ ] `VERIFY-876` — compiler-free gates і фінальний WSL `ReleaseWithInstall` PASS.
- [ ] `DOCS-BUMP-876` — version/docs синхронізовано.
- [ ] `COMMIT-876` — focused primary-master commit без push.

## Попередній delivery: v0.13.875 — виконано

- [x] `MAP-WEB-875`
- [x] `SPLIT-WEB-875`
- [x] `API-CMAKE-875`
- [x] `SIZE-875`
- [x] `VERIFY-875` — Gemini: `[100%] Built target sphaira_nro`, 18/18 suites, dead-symbol 981, EN/UK 2549/2549, 10/10 page payloads, diff check PASS.
- [x] `DOCS-BUMP-875`
- [x] `COMMIT-875`

## Після v0.13.874

- [ ] `SPLIT-TRANSFER-YATI`
- [ ] `SPLIT-PROVIDER-UI`
- [ ] `TEST-RUNNER`

Історія завершених checklist-ів доступна через Git і тут не дублюється.
