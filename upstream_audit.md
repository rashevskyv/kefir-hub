# Аудит upstream Sphaira після 1.0.2

Це живий документ для постійного посилання під час перенесення корисних змін з
upstream [`NaGaa95/sphaira`](https://github.com/NaGaa95/sphaira).

- Базовий upstream-коміт: `eeac5ff8fcffaf57d88b91d05d704e6fb0c75dba` (`1.0.2`).
- Перевірений upstream HEAD: `c19e5a3ac893b3aafe3f229cc2bffe70493ae111` (2026-08-14).
- Локальний стан під час первинного аудиту: `5b4c34deede4a85182c7ee2698ff59b851974d93` (`0.13.446`).
- Upstream-діапазон: 25 комітів, 81 змінений файл, приблизно `+4172/-455`.
- Прямих patch-equivalent комітів у нашій історії немає: наявні збіги реалізовано незалежно.

Посилання: [baseline](https://github.com/NaGaa95/sphaira/commit/eeac5ff8fcffaf57d88b91d05d704e6fb0c75dba),
[upstream HEAD](https://github.com/NaGaa95/sphaira/commit/c19e5a3ac893b3aafe3f229cc2bffe70493ae111),
[повне порівняння](https://github.com/NaGaa95/sphaira/compare/eeac5ff8fcffaf57d88b91d05d704e6fb0c75dba...c19e5a3ac893b3aafe3f229cc2bffe70493ae111).

## Підсумок

| Стан | Кількість | Значення |
|---|---:|---|
| Є у власній реалізації | 4 | Функціонал уже покрито; пряме перенесення не потрібне |
| Частковий перетин | 11 | База є, але upstream має окремі корисні деталі або іншу UX-модель |
| Відсутнє | 4 | Це справді нові для нас функції |
| Не застосовується / службове | 6 | Версії, typo/docs, merge або код для відсутньої підсистеми |

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

### Iconless NRO (`2e27148`) — UI покрито, forwarder ні

Homebrew UI уже показує `App::GetDefaultImage()` замість порожнього texture handle.
Але `App::Install()` нормалізує лише непорожню `config.icon`, а forwarder editor
вважає порожню іконку помилкою. Upstream правильно підставляє
`App::GetDefaultImageData()` перед нормалізацією. Це мале і корисне перенесення;
виконати після ZIP hardening.

### Custom repositories (`2d8b9ba`)

Локально довільні JSON-репозиторії вже завантажуються з `/config/kefir/github/`.
Upstream додає UI add/remove, нормалізацію URL, deduplication і валідацію. Базовий
функціонал у нас є; повний UI не потрібен без запиту. Валідацію URL варто додати
разом із наступним переглядом GitHub menu.

### FTP refresh (`af4c64c`)

Локально `SignalChange()` викликається після GitHub/Appstore/Web/MTP/background
install. Raw FTP upload/delete у `/switch/*.nro` не має completion callback, тому
відкритий Homebrew список може лишитися застарілим. Upstream callback корисний, але
його треба адаптувати до нашого patched `ftpsrv`, а не cherry-pick. Середній пріоритет.

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
2. Повний oversized NRO icon hardening (`5b0779d`) — compressed blob обмежено 1 MiB,
   але decoded dimensions/pixel multiplication не мають upstream-лімітів і downscale.
3. Update/DLC checker (`bc90664`) — немає каталогу `nx-versions` і порівняння
   встановлених base/update/DLC версій.
4. MSP mod packages (`400c514`) — немає `.msp`, manifest/staging/rollback та
   Atmosphère payload install.

Окремо від MSP корисне parser hardening з `400c514`: поточний PFS0/NSP parser треба
перевірити на exact reads, container bounds, overflow, string table і name offsets.

## Не застосовується або службове

- `dadce0f`: лише typo.
- `81f8b4b`: лише README/Discord.
- [x] `e00ac7c`: NFS URL parsing відновлено у `v0.13.449` через `nfs_parse_url_dir()` з regression coverage для nested export path.
- `fac197d`: version bump `1.0.4`.
- `c19e5a3`: merge, функціонально дублює play-stats patch.
- Частина `05279db`: потрібна лише для відсутнього локально affinity relaunch.

## Черга перенесення

### Негайно, малими незалежними кроками

1. [x] Спільний ZIP path/size hardening у `thread::TransferUnzipAll()` — завершено у `0.13.447`.
2. Default icon для iconless NRO під час створення forwarder.
3. Decoded-size hardening для NRO icons.
4. PFS0/NSP parser bounds/exact-read hardening.

### Після цього

5. FTP completion callback для оновлення Homebrew list.
6. Англійський fallback для export filename.
7. [x] Custom NRO search paths — завершено у `0.13.451`.
8. Update/DLC checker.

### Лише за окремою потребою

Play-stats toggle/worker, UI керування custom repositories, affinity-aware 3/4-core
модель, MSP package installer.

## Стратегія інтеграції

Прямий cherry-pick не рекомендований: локально перероблені header/status UI,
File Browser, exports, FTP VFS, MTP, NACP compatibility, branding/config paths;
NFS відновлено як read-only source у `v0.13.449`, `libusbdvd` замінено на `libusbhsfs`. Беремо лише перевірену поведінку
і вбудовуємо її у спільні локальні точки, найменшим diff без нових залежностей.
