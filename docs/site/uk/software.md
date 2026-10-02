# [[Software]]

Отримуйте homebrew-застосунки: магазин Homebrew App Store, збірки DBI, завантажувачі модів і завантаження за прямим посиланням.

**Де:** [[Tools]] → [[Software]]

<!-- shot: software-list | Software menu: Homebrew App Store, DBI, Ownfoil, UAModDownloader, ModCD, SimpleModDownloader, Custom Link -->

| Пункт | Що це |
|---|---|
| [[Homebrew App Store]] | Перегляд, установлення, оновлення й видалення homebrew-застосунків. |
| [[DBI]] | Завантаження збірок DBI, його конфігу та фанатських перекладів. |
| [[Ownfoil]] | Установлення ігор із власного сервера Ownfoil. Див. [Установлення через мережу](install/network.md). |
| [[UAModDownloader]] | Завантажує застосунок UAModDownloader (українські моди). |
| [[ModCD]] | Завантажує застосунок ModCD (графічні моди ECLIPS). |
| [[SimpleModDownloader]] | Завантажує застосунок SimpleModDownloader (моди для ігор із GameBanana). |
| [[Custom Link]] | Завантажити `.zip` або `.nro` за будь-яким прямим посиланням. |

Для всіх пунктів потрібне з'єднання з інтернетом.

## [[Homebrew App Store]]

Список застосунків береться з Homebrew App Store (fortheusers.org).

**Де:** [[Tools]] → [[Software]] → [[Homebrew App Store]]

<!-- shot: software-appstore-grid | App Store grid with app tiles and status icons -->

Маленька іконка на кожній плитці показує стан застосунку: не встановлено, встановлено, є оновлення або знайдено
на карті пам'яті, але встановлено не через магазин.

### Установіть, оновіть або видаліть застосунок

1. Виберіть застосунок і натисніть **A** ([[Info]]). Відкриється сторінка застосунку з описом, версією, розміром і кількістю завантажень.
2. Доступні дії залежать від стану застосунку:
    - [[Install]] — застосунок не встановлено.
    - [[Update]] — є новіша версія, або застосунок знайдено на карті пам'яті без даних магазину.
    - [[Launch]] — запустити встановлений застосунок.
    - [[Remove]] — видалити файли застосунку («Completely remove …?»).
3. Виберіть дію кнопкою **D-pad** вгору/вниз і натисніть **A**. [[Launch]] і [[Remove]] просять підтвердження.

<!-- shot: software-appstore-entry | App page: icon, description, version/installed/updated lines, Install button -->

На сторінці застосунку:

| Кнопка | Дія |
|---|---|
| **L** | [[Changelog]] / [[Details]] — перемкнутися між списком змін і описом |
| **ZL** | [[Files]] — список файлів, які встановлює застосунок |
| **+** | [[Options]]: [[More by Author]], [[Leave Feedback]], [[Visit Website]] |
| **B** | [[Back]] |

[[Visit Website]] видно, лише якщо в застосунку є сайт і Kefir Hub працює як повноцінний застосунок (а не з Альбому).
<!-- TODO(verify): confirm that App::IsApplication() is false when started from the Album applet, so this wording is right -->

### Знайдіть застосунок

Натисніть **+** ([[AppStore Options]]) у списку.

| Параметр | Що робить | За замовчуванням |
|---|---|---|
| [[Filter]] | Показати одну категорію: [[All]], [[Games]], [[Emulators]], [[Tools]], [[Advanced]], [[Themes]], [[Legacy]], [[Misc]]. | [[All]] |
| [[Sort]] | [[Updated]], [[Downloads]], [[Size]] або [[Alphabetical]]. | [[Updated]] |
| [[Order]] | [[Descending]] або [[Ascending]]. | [[Descending]] |
| [[Layout]] | [[Icon]], [[Grid]] або [[HB Menu]]. | [[Grid]] |
| [[Search]] | Пошук за назвою чи ключовим словом. Щоб вийти з результатів пошуку, натисніть **B**. | — |

[[More by Author]] на сторінці застосунку показує всі застосунки того самого автора. Щоб повернутися до повного списку, натисніть **B**.

## [[DBI]]

DBI — окремий застосунок-інсталятор. Це меню лише завантажує DBI та його файли; як установлювати ігри через DBI, описано в розділі
[Установлення через USB](install/usb.md).

**Де:** [[Tools]] → [[Software]] → [[DBI]]

| Пункт | Що робить |
|---|---|
| [[Download DBI translations list]] / [[Update DBI translations list]] | Завантажує список фанатських перекладів DBI. Після цього переклади з'являються під лінією. |
| [[Russian latest DBI]] | Завантажує найновішу російську збірку DBI в `/switch/DBI/DBI.nro`. |
| [[Reset DBI config]] | Замінює ваш конфіг DBI (`/switch/DBI/dbi.config`) стандартним конфігом Kefir. Утримуйте **A**, щоб підтвердити. |
| *переклад* | Завантажує DBI з цим фанатським перекладом у `/switch/DBI/`. |

!!! warning
    [[Russian latest DBI]] і кожен переклад замінюють `/switch/DBI/DBI.nro`. [[Reset DBI config]] перезаписує ваші налаштування DBI.

## Завантажувачі модів

[[UAModDownloader]], [[ModCD]] і [[SimpleModDownloader]] завантажують найновішу версію відповідного застосунку з GitHub.

1. Виберіть пункт і натисніть **A**.
2. Коли завантаження завершиться, з'явиться «Done». Застосунок зберігається в `/switch/<назва>/<назва>.nro` і з'являється в списку homebrew.
3. Запустіть застосунок зі списку homebrew ([Homebrew](homebrew.md)), щоб завантажувати моди.

Якщо вибрати пункт знову, застосунок оновиться до найновішої версії.

## Завантажте за посиланням

[[Custom Link]] завантажує архів `.zip` або застосунок `.nro` за прямим посиланням.

1. Виберіть [[Custom Link]] і натисніть **A**.
2. Виберіть, як ввести посилання: [[Manual (Keyboard)]] або [[From Phone / PC]].
    - З [[From Phone / PC]] відскануйте QR-код телефоном або відкрийте показану адресу на комп'ютері в тій самій мережі й надішліть посилання звідти.
3. Введіть посилання. Воно має починатися з `http` і закінчуватися на `.zip` або `.nro`.
4. Якщо файл більший за 20 МБ, Kefir Hub попередить, що великі файли можуть спричинити проблеми. Виберіть [[Force]], щоб усе одно завантажити.
5. Що далі, залежить від файлу:
    - **`.nro`:** зберігається в `/switch/<назва>/<назва>.nro`. Kefir Hub запропонує його запустити.
    - **`.zip`:** зберігається в `/downloads/`, і відкривається вікно [[Extract Options]].

<!-- shot: software-direct-link | Direct Download input: choice between Manual (Keyboard) and From Phone / PC -->

У вікні [[Extract Options]]:

- Якщо в архіві один застосунок, виберіть [[Install the app to /switch]].
- Інакше позначте потрібні файли (**X** вибирає, **Y** інвертує вибір) і виберіть, куди їх покласти:
  [[Extract files to /downloads]], у нову папку з назвою архіву або [[Extract files to...]], щоб вибрати папку.
- Після розпакування Kefir Hub запитає, чи видалити завантажений `.zip`, і може запропонувати запустити застосунок або відкрити папку у [Файловому менеджері](file-browser.md).

<!-- shot: software-extract-options | Extract Options window with a file tree and the extract choices -->

## Проблеми

**«This isn't a direct link to a .zip or .nro file.»** Посилання веде на вебсторінку, а не на файл. Виберіть [[Edit URL]] і виправте його.

**«Couldn't download that file.»** Адреса неправильна або сервер не відповів. Виберіть [[Edit URL]] і спробуйте знову.

**«Failed to download application».** Не вдалося зв'язатися із сервером App Store. Перевірте з'єднання з інтернетом і спробуйте знову.

**Застосунок показано встановленим, хоча його файлів уже немає.** Для застосунків без одного файлу запуску (наприклад, системних модулів) App Store покладається на власний запис про встановлення. Виберіть [[Remove]], а потім знову [[Install]].
<!-- TODO(verify): does Remove succeed when the files are already gone? -->
