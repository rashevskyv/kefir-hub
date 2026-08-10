# Активні задачі

Актуальний delivery — **v0.13.438**. Завершені задачі збережено в
[`archive/task_v0.13.249-v0.13.430.md`](archive/task_v0.13.249-v0.13.430.md)
та [`archive/task_archive.md`](archive/task_archive.md). Порядок виконання —
у [`plan.md`](plan.md), результат останнього delivery — у
[`walkthrough.md`](walkthrough.md).

## Поточний delivery: v0.13.438 (перемикач USB 3.0)

- [x] `USB3-SETTING-438` — додати в `Tools → Налаштування кефіру` перемикач
  `[usb] usb30_force_enabled`; відсутній ключ вважати увімкненим за
  замовчуванням.
- [x] `USB3-PERSIST-438` — записувати точне `u8!0x1` / `u8!0x0` і синхронно
  commit-ити SD до повідомлення про успіх.
- [x] `USB3-REBOOT-438` — після запису пояснювати, що без перезавантаження
  нічого не зміниться, та пропонувати `Пізніше` або `Перезавантажити`.
- [x] `USB3-DELIVERY-438` — перевірити JSON, зібрати WSL
  `ReleaseWithInstall` і створити focused commit.

## Попередній delivery: v0.13.436 (незалежний скрінсейвер)

- [x] `SAVER-NONBLOCK-436` — прибрати синхронний запис INI та очікування
  `m_mutex` з активного UI-шляху скрінсейвера; рух, яскравість і кадри мають
  залишатися чутливими під час блокуючого запису на SD.
- [x] `SAVER-GRAPH-436` — семплувати R/W-графік кожні 0,5 с без mutex
  інсталятора; за відсутності приросту додавати нульовий семпл, не зупиняючи
  прокрутку графіка.
- [x] `SAVER-FINISHED-436` — після завершення черги ховати графік і показувати
  `Finished` або `Finished with errors` незалежно від маски полів.
- [x] `SAVER-AUTO-436` — додати в налаштування скрінсейвера автозапуск під час
  інсталяції після вибраного періоду бездіяльності; будь-яке введення скидає
  таймер, `Off` лишається типовим значенням.
- [x] `SAVER-PERSIST-436` — зміну яскравості тримати в пам'яті під час запису й
  зберігати один раз лише після завершення/cancel інсталяційного I/O.
- [x] `SAVER-VERIFY-436` — додати одну мінімальну host-перевірку таймера,
  прогнати `tests/run.sh` і WSL `ReleaseWithInstall`.
- [x] `SAVER-DELIVERY-436` — після прийняття підняти версію, оновити living
  docs і створити focused commit без сторонніх змін delivery 0.13.435.

## Поточний delivery: v0.13.437 (Text Viewer / Editor UX)

- [x] `TEXT-OPEN-A-437` — відкривати відомі текстові формати кнопкою `A` у
  read-only viewer до пошуку зовнішніх асоціацій.
- [x] `TEXT-CONTEXT-437` — показувати для текстового файла окремі дії `View` і
  `Edit`, не обмежуючи viewer старим порогом 64 KiB.
- [x] `TEXT-FS-437` — передавати text viewer поточний `fs::Fs*`, дозволити
  перегляд на всіх підтримуваних джерелах і запис лише на реально writable FS;
  image-viewer залишити на чинному незалежному шляху.
- [x] `TEXT-IO-437` — не створювати порожній документ після помилки
  Open/GetSize/Read; показувати фактичний Result і не дозволяти редагування.
- [x] `TEXT-SAVED-STATE-437` — визначати `*` порівнянням із останнім успішно
  збереженим текстом, щоб Undo міг повернути чистий стан, а Save оновлював
  baseline і передбачувано завершував історію.
- [x] `TEXT-SAFE-SAVE-437` — записувати через sibling temp із перевіреним
  rename/restore; після невдалого Save залишати editor відкритим, dirty і з
  усіма змінами в пам'яті.
- [x] `TEXT-GOTO-INSERT-437` — після Go to line одразу показувати затиснутий до
  діапазону рядок; Insert створює рядок тільки після підтвердження keyboard.
- [x] `TEXT-CONTROLS-437` — у View прокручувати текст правим стіком; у Edit
  рухати курсор лівим стіком, правим незалежно прокручувати viewport, а `A`
  відкривати Switch keyboard із повним поточним рядком.
- [x] `INI-UX-437` — підсвічувати секції, коментарі, ключі та значення INI;
  подвійним дотиком до boolean-рядка перемикати `true` / `false` з undo.
- [x] `TEXT-VERIFY-437` — додати мінімальну host-перевірку розпізнавання тексту
  й INI boolean toggle, прогнати host-тести та WSL `ReleaseWithInstall`.
- [x] `TEXT-LEGEND-437` — показати в footer керування для View/Edit: стіки,
  редагування рядка, Actions, Options і Back без перевантаження легенди.
- [x] `TEXT-VIEW-LABEL-437` — не називати writable-файл read-only лише через
  режим View; справжній read-only показувати тільки для protected/source FS.
- [x] `INI-TYPED-FLAG-437` — подвійним tap перемикати також Atmosphère-прапорці
  `u8!0x0` / `u8!0x1` із підтримкою Undo та host-тестом.
- [x] `INI-CONTRAST-437` — малювати назви INI-секцій контрастним текстовим
  кольором, а не темним `focus` чорної теми.
- [x] `STABILITY-TRANSFER-437` — звільняти активний transfer progress box до
  очищення UI/NanoVG, щоб вихід із Home не лишав відкладені image destruction.
- [x] `STABILITY-ICONS-437` — ініціалізувати розміри stb image load і визначати
  формат HB-іконки за даними, а не примусово як JPEG.
- [x] `STABILITY-USB-437` — перед `usbDsExit()` вимкнути USBDS і дочекатися
  `Detached`, що прибирає teardown race на HOS 22.5.
- [x] `TEXT-DELIVERY-437` — після прийняття підняти версію, оновити living docs
  і створити focused commit без сторонніх змін delivery 0.13.435.

## Попередній delivery: v0.13.435 (MTP Games і Create repack)

- [x] `NSP-MERGED-CORE-435` — додати спільний потоковий NSP-builder, який
  об'єднує встановлені BASE, UPD і DLC та читає NCA з їхніх фактичних
  NAND/SD/GameCard storage.
- [x] `NSP-MERGED-NAME-435` — називати пакет
  `Назва [TitleID][B+U<version>+<count>DLC].nsp`, опускаючи відсутні частини.
- [x] `MTP-GAMES-LAYOUT-435` — змінити read-only диск на корінь `Merged` і
  `Separate`; у `Separate/<Game [TitleID]>` залишити окремі BASE/UPD/DLC NSP.
- [x] `HOST-VERIFY-435` — додати мінімальні host-тести для імені merged NSP і
  MTP path routing та прогнати `tests/run.sh`.
- [x] `BUILD-VERIFY-435` — послідовно зібрати `sphaira` і `sphaira_nro` у
  чистому WSL verification-каталозі.
- [x] `CMAKELISTS-VERSION-BUMP-435` — підняти версію до `0.13.435` після
  прийняття реалізації.
- [x] `REPACK-SELECT-435` — додати `Create repack` з вибором лише наявних BASE, UPD і DLC.
- [x] `REPACK-BUILD-435` — записувати вибрані незмінені компоненти одним потоковим NSP у `/games`.
- [x] `REPACK-VERIFY-435` — прогнати host-тести та послідовно зібрати `sphaira` і `sphaira_nro`.
- [x] `UI-COSMETICS-435` — прибрати порожній Progress з вебсервера, розвести USB/Applet Mode на DBI-екрані та збільшити відступ перед NAND/SD-розмірами; host-тести й WSL-збірка пройшли.
- [ ] `REPACK-LAYERFS-LATER` — додати LayeredFS лише разом із коректною перебудовою Program NCA,
  хешів і CNMT; не показувати недієву опцію в поточному UI.
- [ ] `HW-MTPGAMES-435` — на Switch перевірити лістинг обох каталогів і
  копіювання merged та separate NSP на ПК.
- [ ] `HW-REPACK-435` — на Switch створити репак з різними комбінаціями BASE/UPD/DLC,
  перевірити файл у `/games` та його встановлення.

## Паралельний запит: TICO launchers

- [x] `TICO-ASSOC` — додати асоціації лише для фактично встановлених
  `/tico/cores/tico-*.nro`, включно з каталогами `sega-cd`, `fbneo`, `naomi`
  та `atomiswave`.
- [x] `TICO-ARGS` — передавати системний slug для Gambatte і Genesis Plus GX
  під час звичайного запуску та у створеному форвардері; іншим ядрам передавати
  ROM першим аргументом.
- [x] `TICO-CHOOSER` — в обох списках вибору показувати й групувати варіанти як
  `RetroArch — <core>` та `TICO — <core>`.
- [x] `TICO-VERIFY` — прогнати host-тести й WSL-збірку.
- [ ] `HW-TICO-433` — на реальній Switch перевірити прямий запуск і форвардер
  для звичайного ядра та ядра із системним аргументом.

## Попередній delivery: v0.13.431 (File Viewer Build Fix & NxLink Deployment)

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
