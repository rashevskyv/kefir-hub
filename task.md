# task.md

Версія коду: **v0.13.874**.

## Поточний delivery: v0.13.875 — Web structural split

- [ ] `MAP-WEB-875` — HTTP routes, server/mDNS та UI callers підтверджені graph + source review.
- [ ] `SPLIT-WEB-875` — `web.cpp` і page templates розділено без behavior change.
- [ ] `API-CMAKE-875` — public API стабільний; нові units явно зареєстровані.
- [ ] `SIZE-875` — нові C/C++ files ≤600 рядків; legacy units materially reduced.
- [ ] `VERIFY-875` — compiler-free gates і фінальний WSL `ReleaseWithInstall` PASS.
- [ ] `DOCS-BUMP-875` — version/docs синхронізовано.
- [ ] `COMMIT-875` — focused primary-master commit без push.

## Попередній delivery: v0.13.874 — виконано

- [x] `MAP-OPS-874`
- [x] `SPLIT-OPS-874`
- [x] `SAFETY-874`
- [x] `API-CMAKE-874`
- [x] `SIZE-874`
- [x] `VERIFY-874` — Gemini: `[100%] Built target sphaira_nro`, 18/18 suites, dead-symbol 981, EN/UK 2549/2549, diff check PASS.
- [x] `DOCS-BUMP-874`
- [x] `COMMIT-874`

## Після v0.13.874

- [ ] `SPLIT-APP`
- [ ] `SPLIT-TRANSFER-YATI`
- [ ] `SPLIT-PROVIDER-UI`
- [ ] `TEST-RUNNER`

Історія завершених checklist-ів доступна через Git і тут не дублюється.
