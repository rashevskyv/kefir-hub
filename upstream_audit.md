# Аудит upstream Sphaira після 1.0.2

Це живий документ для постійного посилання під час перенесення корисних змін з
upstream [`NaGaa95/sphaira`](https://github.com/NaGaa95/sphaira).

> Політика локалізації: upstream-зміни перекладів не переносимо. Локалі ведуться
> окремим локальним i18n pipeline; функціональні задачі не змінюють i18n-файли.

- Базовий upstream-коміт: `eeac5ff8fcffaf57d88b91d05d704e6fb0c75dba` (`1.0.2`).
- Перевірений upstream HEAD: `ff87305cc01f35f2afe692898cc9c3e7dd05ad85` (`1.0.5`, 2026-08-14).
- Локальний стан під час первинного аудиту: `5b4c34deede4a85182c7ee2698ff59b851974d93` (`0.13.446`).
- Upstream-діапазон: 29 комітів після базового `1.0.2`.
- Прямих patch-equivalent комітів у нашій історії немає: наявні збіги реалізовано незалежно.

Посилання: [baseline](https://github.com/NaGaa95/sphaira/commit/eeac5ff8fcffaf57d88b91d05d704e6fb0c75dba),
[upstream HEAD](https://github.com/NaGaa95/sphaira/commit/ff87305cc01f35f2afe692898cc9c3e7dd05ad85),
[повне порівняння](https://github.com/NaGaa95/sphaira/compare/eeac5ff8fcffaf57d88b91d05d704e6fb0c75dba...ff87305cc01f35f2afe692898cc9c3e7dd05ad85).

## Підсумок

| Стан | Актуальний зміст |
|---|---|
| Уже впроваджено після первинного аудиту | ZIP hardening, read-only NFS, NRO icon hardening/default fallback, custom NRO paths, PFS0/NSP hardening, NSP install diagnostics |
| Частковий перетин | Наявна локальна база є, але upstream має окрему поведінку або іншу UX-модель |
| Залишилося запланувати | FTP refresh, English export fallback, Update/DLC checker |
| Окрема продуктова потреба | screen-off extension, MSP installer, play-stats toggle, custom repository UI, 3/4-core UX |

## Уже є у власній реалізації

### Сортування ігор за видавцем — не гірше

Локально є `SortType_Publisher`, стабільна вторинна логіка порівняння і відповідний
пункт меню у `sphaira/include/ui/menus/game_menu.hpp` та
`sphaira/source/ui/menus/game_menu.cpp`. Переносити `6f8a886` не потрібно.

### Назви ігор для Atmosphère title ID — локально ширше

Локальний File Browser підставляє назви встановлених ігор для Atmosphère title ID,
додатково знає назви HATS/sysmodule і виконує частину роботи асинхронно. Це покриває
`16ada3e` і має ширший fallback-набір.

### Статус Applet Mode — інший компактний UI

Upstream перемістив напис `Applet Mode` вище мережевого статусу. Локально той самий
стан показується компактним індикатором `[A]` у загальному header. Функціональна
інформація не втрачена; копіювати upstream-розкладку не потрібно.

### Актуальний libnx NACP layout — локально стійкіше

Локально є compile-time adapter у `sphaira/include/nacp_util.hpp` і CMake-патч для
`libnxtc`, тому старий і новий layout підтримуються централізовано. Це краще за
точкові заміни з `d84d475`.

## Частковий перетин і рішення

### Заголовки меню (`d9c3939`)

Локальний header суттєво перероблений: є чотири незалежні слоти та прокрутка
підзаголовків. Основний title поки малюється без marquee. Це не функціональна
регресія, але довгі заголовки можуть обрізатися; можна додати через наявний
`ScrollingText` без перенесення upstream-layout. Низький пріоритет.

### `1.0.3` (`0882439`)

Upstream-коміт змішаний. Haze-код у нас має інший API, release JSON є fork-specific.
Корисний залишок — кілька ROM alias (`SuperGrafx`, `Family Computer Disk`,
`FBNeo Neo Geo`), яких локально не знайдено. Додавати лише за реальним запитом на
ці платформи.

### 3/4 CPU cores для forwarder (`204fdde`, `05279db`)

Локальний `hbl/hbl.json` задає `highest_cpu_id = 3`, тобто глобально дозволяє всі
чотири ядра. Upstream за замовчуванням залишає три ядра і дає керований opt-in до
чотирьох з affinity-aware relaunch та попередженнями. Наш варіант простіший, але
менш керований і потенційно конфліктує з системними потоками. Не переносити великий
механізм без окремої продуктової вимоги; якщо потрібна безпечніша модель, реалізувати
малий вибір 3/4 cores у наявному forwarder editor.

### Direct-link ZIP (`69f362d`) — захист шляхів та розмірів вирішено централізовано

Локально є пряме завантаження ZIP через Network Downloads та поле `direct_url` у
JSON. Upstream додатково перевіряє HTTP(S) URL, кожний archive path, суму
uncompressed sizes і вільне місце на SD.

У `v0.13.447` валідацію archive path, захист від обрізання/наддовгих імен, перевірку
сумарної довжини шляху призначення, перевірку кількості записів (`number_entry`) та
захист від переповнення `uncompressed_size` вирішено централізовано у спільному
`thread::TransferUnzipAll()`, що автоматично захистило всі 11 точок виклику.

SD-специфічну перевірку вільного місця перед розпакуванням наразі не реалізовано у
спільній функції, оскільки `TransferUnzipAll` працює не лише з SD, а й зі сховищами
сейвів та іншими не-SD файловими системами.

### Iconless/oversized NRO icon (`2e27148`, `5b0779d`) — завершено у `v0.13.450`

`v0.13.450` централізував безпечну нормалізацію NRO-іконок: обмежив decoded
dimensions/pixels, перевірив множення розмірів і буфери resize/JPEG conversion,
приймає до `1024×1024` і downscale-ить до `256×256`. Homebrew, forwarder і
SteamGridDB використовують ту саму policy; порожня або невалідна іконка тепер
fallback-иться до default image. Це одночасно закриває upstream `2e27148` і
`5b0779d`.

### Custom repositories (`2d8b9ba`)

Локально довільні JSON-репозиторії вже завантажуються з `/config/kefir/github/`.
Upstream додає UI add/remove, нормалізацію URL, deduplication і валідацію. Базовий
функціонал у нас є; повний UI не потрібен без запиту. Валідацію URL варто додати
разом із наступним переглядом GitHub menu.

### FTP refresh (`af4c64c`)

Локально `SignalChange()` викликається після GitHub/Appstore/Web/MTP/background
install. Raw FTP upload/delete у `/switch/*.nro` не має completion callback, тому
відкритий Homebrew список може лишитися застарілим. Upstream callback корисний, але
наша вимога ширша: це має бути **один спільний completion path** для кожного
зовнішнього джерела. Після успішного create/delete/rename/move `.nro` у
спостережуваному Homebrew-каталозі він має сигналізувати про зміну каталогу незалежно
від того, чи операція прийшла з FTP, Web, MTP, SMB/NFS/WebDAV або іншого source.
Не переносити FTP-специфічний callback; знайти централізовану точку мутації та
додавати сигнал лише після успішного завершення операції. Високий пріоритет.

### Required system version (`9e4c46a`)

Локальний інсталятор обнуляє required system version під час формування метаданих,
тобто виправляє значення до запису. Upstream виправляє окремий post-install reset у
meta DB, якого в нас немає. Наш поточний шлях не гірший; переносити нічого.

### Локалізовані export filenames (`db06528`)

Локально назва береться з поточної локалі, ASCII-санітизується і fallback-иться до
Title ID. Upstream має додатковий fallback на англійську назву та опцію збереження
Unicode. Англійський fallback корисний, якщо локалізована назва після санітизації
порожня; Unicode toggle не потрібен без окремої вимоги.

### Async playtime (`ec854b0`)

Локально вже є last played, total playtime, cache, сортування і ручне фонове bulk
оновлення через `ProgressBox`. Upstream тихо оновлює обрану гру у worker і має
per-user cache. Наш шлях прозоріший, але дорожчий для користувача; це UX-відмінність,
не дефект. Переносити лише якщо потрібне автоматичне оновлення без дії користувача.

### Play statistics toggle (`8c6e0e8`, merge `c19e5a3`)

Сортування за last played/playtime локально вже є. Немає перемикача, що повністю
вимикає PDM/playlog I/O та ховає ці сорти. Додати лише якщо вимірювання покаже
помітну затримку або користувачі просять приватний/легкий режим.

## Нові функції, яких локально немає

1. [x] Custom NRO search paths (`5b02f65`) — завершено у `v0.13.451` з повною адаптацією під локальний File Browser (`IsParentEntry`), збереженням `/switch` як незмінного першого кореня, глибиною сканування 2, дедуплікацією `NroEntry`, захистом від розіменування порожнього списку, 13 мовними файлами (без `ru.json` до окремого i18n pipeline) та таргет `sphaira_romfs_sync`.
2. [x] Повний oversized NRO icon hardening/default fallback (`2e27148`, `5b0779d`) —
   завершено у `v0.13.450`.
3. Update/DLC checker (`bc90664`) — немає каталогу `nx-versions` і порівняння
   встановлених base/update/DLC версій.
4. MSP mod packages (`400c514`) — немає `.msp`, manifest/staging/rollback та
   Atmosphère payload install.
5. [x] Loader thread affinity before NRO launch (`87c855a`) — завершено у `v0.13.452`:
   `hbl/source/main.c` читає фактичну mask процесу через `svcGetInfo(InfoType_CoreMask)`
   і відновлює її для main thread через `svcSetThreadCoreMask(..., -1, core_mask)` прямо
   перед trampoline. `highest_cpu_id = 3` лишається лише NPDM-дозволом, без hard-coded mask.

Окремо від MSP parser lessons з `400c514` адаптовано у `v0.13.453`: спільний
PFS0/NSP parser отримав exact reads, container bounds для known-size readers,
checked arithmetic, allocation limits, string-table/name validation і host
negative coverage. Продуктова MSP частина не переносилася.

## Не застосовується або службове

- `dadce0f`: лише typo.
- `81f8b4b`: лише README/Discord.
- [x] `e00ac7c`: NFS URL parsing відновлено у `v0.13.449` через `nfs_parse_url_dir()` з regression coverage для nested export path; NFS додано як read-only source.
- `fac197d`: version bump `1.0.4`.
- `c19e5a3`: merge, функціонально дублює play-stats patch.
- Частина `05279db`: потрібна лише для відсутнього локально affinity relaunch.
- `f4efedf`: корекція трьох іспанських Homebrew-рядків. Не переносити згідно з
  політикою окремого локального i18n pipeline.
- `ff87305`: лише upstream version bump до `1.0.5`.

## Оновлення upstream `1.0.5` (2026-08-14)

Після попередньо перевіреного `c19e5a3` upstream додав чотири commits:

1. `a55b54e` — ручне `Screen Off` у `ProgressBox` для file install, USB install,
   stream install, GameCard install і dump. Локально вже є багаторежимний
   `Screensaver`/backlight-off, ручне керування кнопкою Minus та inactivity timeout,
   але вони прив'язані до install queue. Це **частковий перетин**: не переносити
   upstream global LBL lifecycle поруч із наявним `Screensaver`; за окремою потребою
   розширити існуючий механізм на ці long-running `ProgressBox` flows.
2. `f4efedf` — іспанські рядки; див. вище, **не застосовується**.
3. `87c855a` — відновлює main-thread process core mask безпосередньо перед NRO
   trampoline. Адаптовано у `v0.13.452` без зміни `hbl.json`: використовується фактична
   process mask, а не дозволений діапазон ядер `0..3`.
4. `ff87305` — лише version bump, **не застосовується**.

## Черга перенесення

### Негайно, малими незалежними кроками

1. [x] Спільний ZIP path/size hardening у `thread::TransferUnzipAll()` — завершено у `0.13.447`.
2. [x] Default icon для iconless NRO та decoded-size hardening — завершено у `0.13.450`.
3. [x] Loader thread affinity before NRO launch (`87c855a`) — завершено у `0.13.452`.
4. [x] PFS0/NSP parser bounds/exact-read hardening — завершено у `v0.13.453`.

### Після цього

5. Спільний Homebrew catalog mutation callback для всіх зовнішніх джерел; FTP
   (`af4c64c`) — лише один із callers.
6. Англійський fallback для export filename.
7. [x] Custom NRO search paths — завершено у `0.13.451`.
8. Update/DLC checker.
9. Розширення наявного screensaver/backlight-off на інші довгі `ProgressBox` flows
   (за потребою; не прямий перенос `a55b54e`).

### Лише за окремою потребою

Play-stats toggle/worker, UI керування custom repositories, affinity-aware 3/4-core
модель, MSP package installer.

## Стратегія інтеграції

Прямий cherry-pick не рекомендований: локально перероблені header/status UI,
File Browser, exports, FTP VFS, MTP, NACP compatibility, branding/config paths;
NFS відновлено як read-only source у `v0.13.449`, `libusbdvd` замінено на `libusbhsfs`. Беремо лише перевірену поведінку
і вбудовуємо її у спільні локальні точки, найменшим diff без нових залежностей.
