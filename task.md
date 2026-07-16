# Активні задачі

Порядок відповідає `plan.md`. Завершені рядки переносяться в архів, а не
видаляються без сліду.

## Поточний delivery: v0.13.239

- [x] AUDIT-HISTORY — аудит останніх 5 комітів і всієї історії plan/task/archive
- [x] HIST-HTTP-RETRY — GET-only retry/resume, restart без stale tail, final flush check
- [x] HIST-61A.1 — Game Details Overview і реальні NCM component rows
- [x] HIST-61A.2 — точковий dump поточної гри без успадкування bulk selection
- [x] HIST-61A.3 — DBI-подібні Content/Tickets/Saves, мови, allocated save size і L/R навігація
- [x] HIST-62 — Base/Update/DLC/LayeredFS badges із кешованого NCM summary
- [x] HIST-GAMES-VIEWPORT — Games не малюється поверх header/footer
- [x] HIST-GAMES-STORAGE — розмір вибраної гри підсвічується у NAND/SD bars
- [x] HIST-WEB-APPLET.1 — Runtime Mode UX, Title Mode guide і forwarder installer
- [x] HIST-WEB-APPLET.2 — Applet worker profile, listener diagnostics і loopback self-test
- [x] AUDIO-REMOVE — вилучено BGM, UI sounds, audio init, settings та `libpulsar`
- [x] BUILD-RELEASE — Release NRO v0.13.239 зібрано; `git diff --check` пройдено
- [ ] HW-SMOKE-239 — ручний smoke test на реальній Switch за `tests.md`

## Черга реалізації

- [ ] HIST-54 — DBI: графік R/W швидкості та загальний progress bar
- [ ] HIST-55 — DBI: динамічний рядок журналу на пакет
- [ ] HIST-56 — DBI: сегментовані NAND/SD bars і наочний ReviewQueue
- [ ] HIST-60 — Games: перенос NAND ↔ SD та сортування за носієм
- [x] HIST-61A — Games: Game Details з Overview/Content/Tickets/Saves
  - [x] Окремий екран по A, Overview, реальні NCM component rows і точковий component dump
  - [x] Видимі partial-load помилки; чесні `Contents folder` і `Save quota`
  - [x] Вкладки Tickets і Saves, повний список мов та allocated save-data size
- [ ] HIST-61B — Games: dump/verify/read-only mount і безпечні component actions
- [ ] HIST-61C — Games: save integration, ticket details і перевірений DLC unlocker
- [x] HIST-62 — Games: badges Base/Update/DLC/LayeredFS
- [ ] HIST-WEB-APPLET — Applet warning, Title Mode guide і встановлення forwarder
  - [x] Applet chooser: start anyway / install forwarder / Title Mode instructions
  - [x] Runtime Mode help, видимі обмеження browser/NSO, listener error log і Applet worker profile
  - [x] Loopback HTTP listener self-test
  - [ ] Ручна діагностика Wi-Fi client isolation на реальній Switch/точці доступу
- [ ] HIST-USB-COMPAT — ручна матриця USB-клієнтів на реальній Switch
- [ ] HIST-NFS-SFTP — підтримка NFS/SFTP як окремих network sources
- [~] HIST-PLAYER — вбудований плеєр; заморожено до окремого погодження

## Поточна задача

`HIST-61B`: integrity verification/read-only mount і безпечні component actions.

## Правило закриття

Позначка `[x]` означає перевірений код або автоматичний test gate. Те, що
потребує реальної Switch, залишається `[ ]` до записаного результату в
`tests.md`; успішна компіляція сама по собі не закриває hardware gate.
