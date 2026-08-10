# Активні задачі

Актуальний delivery — **v0.13.436**. Завершені задачі збережено в
[`archive/task_v0.13.249-v0.13.430.md`](archive/task_v0.13.249-v0.13.430.md)
та [`archive/task_archive.md`](archive/task_archive.md). Порядок виконання —
у [`plan.md`](plan.md), результат останнього delivery — у
[`walkthrough.md`](walkthrough.md).

## Поточний delivery: v0.13.436 (незалежний скрінсейвер)

- [x] SAVER-NONBLOCKING-436 — Прибрано блокувальні операції SD та очікування mutex із кадру скрінсейвера; керування і графік не залежать від нульової швидкості запису.
- [x] SAVER-BRIGHTNESS-436 — Яскравість застосовується до підсвітки одразу, а в конфігурацію записується один раз після завершення встановлення.
- [x] SAVER-FINISHED-436 — Після завершення замість графіка показується `Finished` або `Finished with errors`.
- [x] SAVER-TIMEOUT-436 — Додано налаштування автозапуску після бездіяльності: Off, 30 с, 1/2/5/10 хв.
- [x] VERIFY-SAVER-436 — Host-тести, `git diff --check` і збірка `ReleaseWithInstall` у WSL пройшли успішно.

## Поточний delivery: v0.13.431 (File Viewer Build Fix & NxLink Deployment)

- [x] BUILD-FIX-FILE-VIEWER-431 — Додано пропущені заголовні файли `swkbd.hpp` та `ui/popup_list.hpp` у `file_viewer.cpp`, виправлено специфікатор форматування `%ld` для `s64` номера рядка.
- [x] CMAKELISTS-VERSION-BUMP-431 — Піднято версію в `sphaira/CMakeLists.txt` до `0.13.431`.
- [x] VERIFY-NXLINK-SEND-431 — Успішно зібрано `[100%] Built target sphaira_nro` у WSL та відправлено `kefir-hub.nro` (6,081,813 байт) через `make nxlink` на Switch (192.168.50.69).

## Поточні перевірки

- [ ] `HW-SMOKE-430` — обидва стіки у скрінсейвері, межі руху, яскравість,
  швидкість дрейфу та пробудження.
- [ ] `HW-SMOKE-429` — межі storage-блоку, R/W-графік і preview скрінсейвера.
- [ ] `HIST-USB-COMPAT` — DBI backend, Awoo/TinFoil і GoldLeaf v0.10+ на
  реальній Switch.
- [ ] `HW-SMOKE-MTP` — лістинг MTP, reconnect і встановлення NSP з телефона.
- [ ] `HIST-WEB-APPLET` — перевірити Wi-Fi client isolation на реальній
  Switch і точці доступу.

## Черга реалізації

- [ ] `HIST-55` — DBI: один динамічний рядок журналу на пакет.
- [ ] `HIST-56` — DBI: сегментовані NAND/SD-смуги та наочний `ReviewQueue`.
- [ ] `HIST-61B` — Games: dump/verify/read-only mount і безпечні component actions.
- [ ] `HIST-61C` — Games: save integration, ticket details і перевірений DLC unlocker.
- [ ] `HIST-NFS-SFTP` — NFS/SFTP як окремі network sources.
- [~] `HIST-PLAYER` — вбудований player; заморожено до окремого погодження.

## Правило закриття

`[x]` означає пройдену перевірку. Успішна компіляція не закриває hardware-gate
без записаного результату в `tests.md`.
