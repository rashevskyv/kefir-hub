# task.md

Версія коду: **v0.13.872**.

## Поточний delivery: v0.13.873 — split Save menu UI/catalog

- [ ] `MAP-MENU-873` — callers/state і природні межі `save_menu.cpp` підтверджені graph + source review.
- [ ] `SPLIT-CATALOG-873` — scan/group/catalog helpers винесено без semantic change.
- [ ] `SPLIT-UI-873` — lifecycle/layout/callback responsibilities лишилися cohesive.
- [ ] `API-873` — public `Menu` API, routes, filters, ordering і dialogs стабільні.
- [ ] `CMAKE-873` — нові implementation units явно зареєстровані.
- [ ] `SIZE-873` — усі нові C/C++ files ≤600 рядків; legacy unit materially reduced.
- [ ] `VERIFY-873` — compiler-free gates і фінальний WSL `ReleaseWithInstall` PASS.
- [ ] `DOCS-BUMP-873` — version/docs синхронізовано.
- [ ] `COMMIT-873` — focused primary-master commit без push.

## Попередній delivery: v0.13.872 — виконано

- [x] `SPLIT-PATHS-872`
- [x] `SPLIT-META-872`
- [x] `SPLIT-DISCOVERY-872`
- [x] `CMAKE-872`
- [x] `CONTRACT-872`
- [x] `SIZE-872`
- [x] `VERIFY-872` — `[100%] Built target sphaira_nro`, 18/18 suites, dead-symbol 981, EN/UK 2549/2549, diff check PASS.
- [x] `DOCS-BUMP-872`
- [x] `COMMIT-872`

## Після v0.13.872

- [ ] `SPLIT-SAVE-OPS`
- [ ] `SPLIT-WEB`
- [ ] `SPLIT-APP`
- [ ] `SPLIT-TRANSFER-YATI`
- [ ] `SPLIT-PROVIDER-UI`
- [ ] `TEST-RUNNER`

Історія завершених checklist-ів доступна через Git і тут не дублюється.
