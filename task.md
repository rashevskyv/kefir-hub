# task.md

Версія коду: **v0.13.871**.

## Поточний delivery: v0.13.872 — split Save paths and metadata

- [ ] `SPLIT-PATHS-872` — path naming/building лишилося в компактному `save_paths.cpp`.
- [ ] `SPLIT-META-872` — metadata wire decode/read винесено в cohesive unit ≤600 рядків.
- [ ] `SPLIT-DISCOVERY-872` — backup search/discovery та inspection/integrity рознесено за стабільними відповідальностями.
- [ ] `CMAKE-872` — explicit source list оновлено без нового build abstraction.
- [ ] `CONTRACT-872` — save formats, filters, grouping, paths, restore admission і callers не змінилися.
- [ ] `SIZE-872` — усі нові source/test/instruction files ≤600 рядків; legacy unit materially reduced.
- [ ] `VERIFY-872` — 18 suites, dead-symbol, JSON parity, diff check і фінальний WSL `ReleaseWithInstall` PASS.
- [ ] `DOCS-BUMP-872` — version/docs синхронізовано.
- [ ] `COMMIT-872` — focused primary-master commit без push.

## Попередній delivery: v0.13.871 — виконано

- [x] `DEAD-ADVANCED-871`
- [x] `DEAD-STREAM-MENU-871`
- [x] `DEAD-STREAM-FILE-871`
- [x] `DEAD-WEBDAV-871`
- [x] `BUILD-CLOSURE-871` — виправлено accumulated Save compile blockers.
- [x] `VERIFY-871` — `[100%] Built target sphaira_nro`, 18/18 suites, dead-symbol 981, EN/UK 2549/2549, diff check PASS.
- [x] `DOCS-BUMP-871`
- [x] `COMMIT-871`

## Після v0.13.871

- [ ] `SPLIT-SAVE-UI-ACTIONS`
- [ ] `SPLIT-WEB`
- [ ] `SPLIT-APP`
- [ ] `SPLIT-TRANSFER-YATI`
- [ ] `SPLIT-PROVIDER-UI`
- [ ] `TEST-RUNNER`

Історія завершених checklist-ів доступна через Git і тут не дублюється.
