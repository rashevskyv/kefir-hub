# task.md

Версія коду: **v0.13.873**.

## Поточний delivery: v0.13.874 — split Save operations

- [ ] `MAP-OPS-874` — operation/caller boundaries підтверджені graph + source review.
- [ ] `SPLIT-OPS-874` — backup, restore, sync і deletion винесено без behavior change.
- [ ] `SAFETY-874` — admission, recovery, mutation і cancellation ordering збережено.
- [ ] `API-CMAKE-874` — public API стабільний; нові units явно зареєстровані.
- [ ] `SIZE-874` — усі нові C/C++ files ≤600 рядків; legacy unit materially reduced.
- [ ] `VERIFY-874` — compiler-free gates і фінальний WSL `ReleaseWithInstall` PASS.
- [ ] `DOCS-BUMP-874` — version/docs синхронізовано.
- [ ] `COMMIT-874` — focused primary-master commit без push.

## Попередній delivery: v0.13.873 — виконано

- [x] `MAP-MENU-873`
- [x] `SPLIT-CATALOG-873`
- [x] `SPLIT-UI-873`
- [x] `API-873`
- [x] `CMAKE-873`
- [x] `SIZE-873`
- [x] `VERIFY-873` — Gemini: `[100%] Built target sphaira_nro`, 18/18 suites, dead-symbol 981, EN/UK 2549/2549, diff check PASS.
- [x] `DOCS-BUMP-873`
- [x] `COMMIT-873`

## Після v0.13.873

- [ ] `SPLIT-WEB`
- [ ] `SPLIT-APP`
- [ ] `SPLIT-TRANSFER-YATI`
- [ ] `SPLIT-PROVIDER-UI`
- [ ] `TEST-RUNNER`

Історія завершених checklist-ів доступна через Git і тут не дублюється.
