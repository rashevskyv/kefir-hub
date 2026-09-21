# task.md

Версія коду: **v0.13.870**.

## Поточний delivery: v0.13.871 — dead-code cleanup

- [ ] `DEAD-ADVANCED-871` — видалено недосяжний `App::DisplayAdvancedOptions` і залежний misc factory; живі install/forwarder/dump options незмінні.
- [ ] `DEAD-STREAM-MENU-871` — видалено неінстанційований `stream::Menu`, active-menu pointer/setter/branches; background install і DBI flow збережені.
- [ ] `DEAD-STREAM-FILE-871` — видалено неінстанційований `yati::source::StreamFile`, header/source/CMake entry.
- [ ] `DEAD-WEBDAV-871` — видалено невикористані `GetWebdavUrl/User/Pass`; активний named-location flow незмінний.
- [ ] `SIZE-871` — нових файлів понад 600 рядків немає; oversized legacy files не отримали незалежної нової відповідальності.
- [ ] `VERIFY-STATIC-871` — Gemini виконав scoped static/dead-symbol/diff checks після implementation.
- [ ] `VERIFY-BUILD-871` — лише після окремої команди senior-а Gemini виконав WSL `ReleaseWithInstall` і виправив compile/link errors у межах delivery.
- [ ] `DOCS-BUMP-871` — app version піднято до `0.13.871`, `plan.md` / `task.md` / `walkthrough.md` / `audit.md` синхронізовано.
- [ ] `COMMIT-871` — focused primary-master commit `v0.13.871: remove unreachable legacy UI` створено без push.

## Після v0.13.871

- [ ] `SPLIT-SAVES` — перший behavior-preserving split legacy Save subsystem.
- [ ] `SPLIT-WEB`
- [ ] `SPLIT-APP`
- [ ] `SPLIT-TRANSFER-YATI`
- [ ] `SPLIT-PROVIDER-UI`
- [ ] `TEST-RUNNER`

Історія завершених checklist-ів доступна через Git і тут не дублюється.
