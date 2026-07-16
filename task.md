# Активні задачі

Порядок відповідає `plan.md`. Завершені рядки переносяться в архів, а не
видаляються без сліду.

## Поточний delivery: v0.13.251

- [x] CURL-AUTH-251 — devoptab curl тепер веде переговори про схему
  автентифікації (`CURLAUTH_ANY`, як у download.cpp); сервери з Digest більше
  не відповідають 401 на PROPFIND/GET при перегляді (причина «пусто» у .249 і
  `FsUnknownStdioError` у .250, коли Test Connection проходив)
- [x] NET-ERROR-UX-251 — помилки мережевих джерел показуються звичайним
  діалогом з поясненням (сервер недоступний / лістинг не вдався) замість
  страшного error box з кодом і проханням повідомити про проблему
- [x] BADGE-PERSIST-251 — статус джерела кешується на сесію за URL: бейдж на
  root стає зеленим/червоним після входу або Test Connection (з root і з
  Settings) і не скидається при поверненні на root
- [ ] HW-SMOKE-251 — ручний smoke test на реальній Switch за `tests.md`

## Попередній delivery: v0.13.250

- [x] WEBDAV-LIST-250 — PROPFIND-парсер регістро- і namespace-незалежний
  (`<D:response>`, `<ns0:...>` тощо); не-WebDAV відповіді падають у fallback на
  HTML index; FTP LIST замість NLST дає типи та розміри записів
- [x] SCAN-ERROR-250 — невдалий лістинг змонтованого мережевого джерела показує
  error box і повертає до root замість зеленого стану `Empty...`
- [x] ROOT-SOURCES-250 — з root файлового браузера доступні ті самі дії, що в
  Settings -> Sources: Add network location, Edit Source (SourceEditMenu),
  Test Connection, Rename, Properties, Delete; root-список оновлюється після
  змін через OnFocusGained, а файлові операції (Cut/Copy/Delete/Rename/Zip)
  приховані на root
- [x] ROOT-BADGE-250 — бейдж джерела на root стає зеленим/червоним за
  результатом реальної спроби підключення
- [ ] HW-SMOKE-250 — ручний smoke test на реальній Switch за `tests.md`

## Попередній delivery: v0.13.249

- [x] AUDIT-HISTORY — аудит останніх 5 комітів і всієї історії plan/task/archive
- [x] HIST-HTTP-RETRY — GET-only retry/resume, restart без stale tail, final flush check
- [x] HIST-61A.1 — Game Details Overview і реальні NCM component rows
- [x] HIST-61A.2 — точковий dump поточної гри без успадкування bulk selection
- [x] HIST-61A.3 — DBI-подібні Content/Tickets/Saves, мови, allocated save size і L/R навігація
- [x] HIST-62 — Base/Update/DLC/LayeredFS badges із кешованого NCM summary
- [x] HIST-GAMES-VIEWPORT — Games не малюється поверх header/footer
- [x] HIST-GAMES-STORAGE — розмір вибраної гри підсвічується у NAND/SD bars
- [x] HIST-GAMES-SELECTION — X перемикає одну гру, Y інвертує вибір, B спочатку очищає вибір
- [x] HIST-GAMES-COMMON-ACTIONS — меню показує лише спільно застосовні типи dump для вибраних ігор
- [x] HIST-GAMES-UX-240 — контрастні badges включно з `Без контенту`, виразні вкладки,
  пояснення папки модів Atmosphere та об'єднана легенда L/R і ZL/ZR
- [x] SOURCES-PROTOCOL-EDIT — наявне джерело можна перетворити між SMB/WebDAV/FTP/HTTP;
  URL, порт, поля форми та збережені реквізити узгоджуються з новим протоколом
- [x] HIST-GAMES-BADGES-242 — Base/DLC/Update/LayeredFS показуються вертикально,
  контрастними різнокольоровими лейбочками; порожня гра має червону лейбочку `-`
- [x] HTTP-WEBDAV-CRASH-243 — виправлено use-after-free заголовків PROPFIND/FTP MKD
  і небезпечне блокування при повторному перегляді HTTP/WebDAV джерел
- [x] HIST-GAMES-UNAVAILABLE-244 — metadata-error/application-record без встановленого
  контенту має червоний `-`; Game Options містить прямий show/hide toggle і Layout
- [x] HIST-GAMES-BASE-245 — кожен запис без Base має червоний `-`; `L / R` перемикають
  вкладки деталей, а `ZL / ZR` — попередню/наступну гру
- [x] HIST-HBMENU-TITLE-246 — назва у верхній плашці HB Menu card не виходить за
  рамку; довгий текст прокручується лише на картці під фокусом
- [x] HIST-GAMES-BADGE-SIZE-247 — мінімальна ширина кожного game badge дорівнює `Base`
- [x] HIST-GAMES-STORAGE-SUM-247 — NAND/SD показує точні bytes гри під фокусом або
  суму групового X/Y-виділення разом із пропорційним сегментом
- [x] HIST-INSTALL-NOSLEEP-248 — auto-sleep lock перевіряється через applet service;
  media-playback fallback утримує консоль активною, якщо перевірка не пройшла
- [x] SOURCES-PREFLIGHT-249 — File Browser робить HTTP/WebDAV/FTP probe до mount/scan;
  недоступне джерело показує помилку замість зеленого стану `Empty`
- [x] WEBDAV-SYNC-PREFLIGHT-249 — Test Connection, auto-sync, remote restore і full sync
  використовують суворий WebDAV PROPFIND; HTTP errors більше не вважаються success
- [x] WEBDAV-SCHEME-249 — `webdav://` нормалізується в HTTP, `webdavs://` у HTTPS;
  explicit HTTP джерела не обираються як WebDAV sync targets, missing folders створюються
- [x] HIST-WEB-APPLET.1 — Runtime Mode UX, Title Mode guide і forwarder installer
- [x] HIST-WEB-APPLET.2 — Applet worker profile, listener diagnostics і loopback self-test
- [x] AUDIO-REMOVE — вилучено BGM, UI sounds, audio init, settings та `libpulsar`
- [x] BUILD-RELEASE — Release NRO v0.13.249 зібрано; `git diff --check` пройдено
- [ ] HW-SMOKE-249 — ручний smoke test на реальній Switch за `tests.md`

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
