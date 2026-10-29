# Homebrew

Вкладка [[Homebrew]] показує homebrew-застосунки (файли `.nro`) на карті пам'яті. Тут їх можна запускати,
сортувати, позначати як обрані, видаляти або додавати до HOME Menu окремою іконкою.

**Де:** перша вкладка головного екрана. З [[Tools]] натисніть **L** або **−**.

<!-- shot: homebrew-grid | Homebrew tab in the default Grid layout, one app selected, path shown in the header -->

## Які застосунки показано
- кожен `.nro` у `/switch/` та в його підпапках на один рівень нижче (наприклад, `/switch/ftpd/ftpd.nro`);
- кожен `.nro` у ваших власних папках пошуку, до двох рівнів углиб (див. [Додати папку пошуку](#add-a-search-folder)).

Файли й папки, назва яких починається з крапки, пропускаються. Список оновлюється сам, коли ви додаєте чи
видаляєте застосунок у [Файловому браузері](file-browser.md).

- Підтека `/switch/` з кількома файлами `.nro` показує їх усі, навіть якщо один із них названо як тека. Раніше показувався лише `<тека>/<тека>.nro`, решта була прихована.
- Картка показує в другому рядку імʼя файлу застосунку (наприклад `vktest.nro`) замість автора. Так можна відрізнити збірки одного застосунку з однаковою назвою. Розкладка в стилі hbmenu і раніше показує імена файлів.

## Запуск застосунку
1. Виберіть застосунок. У заголовку видно його шлях і позицію в списку.
2. Натисніть **A** ([[Launch]]). Kefir Hub закриється, і застосунок запуститься.

Коли ви вийдете із застосунку, знову відкриється Homebrew Menu (`/hbmenu.nro`).

Якщо серед застосунків є другий файл Kefir Hub (коли Kefir Hub — ваше Homebrew Menu), **A** питає [[Keep]] / [[Launch]] / [[Delete]]. Типово вибрано [[Launch]], тож налагоджувальна копія все одно запускається.

## Сортування й вигляд
1. Натисніть **+** ([[Options]]).
2. Змініть [[Sort]], [[Order]], [[Layout]] або [[Show Hidden]]. Список оновиться одразу.

<!-- shot: homebrew-options | Homebrew options panel: Sort, Order, Layout, Show Hidden, then THIS HOMEBREW section -->

| Опція | Що робить | Типово |
|---|---|---|
| [[Sort]] | [[Updated]]: спершу ті, що запускали чи змінювали останніми. [[Alphabetical]]: за назвою. [[Size]]: за розміром файлу. Варіанти `(Star)` ставлять обрані застосунки першими | [[Alphabetical (Star)]] |
| [[Order]] | [[Descending]] або [[Ascending]]. Діє для [[Updated]] і [[Size]]; [[Alphabetical]] завжди від А до Я | [[Descending]] |
| [[Layout]] | [[List]]: по рядку на застосунок, з версією та розміром файлу. [[Icon]]: шість іконок у рядку, назву видно лише для вибраної. [[Grid]]: три плитки в рядку з іконкою, назвою, автором і версією. [[HB Menu]]: один ряд іконок, яким рухаються **Ліворуч** / **Праворуч**, і велика інформаційна панель вибраного застосунку, як у класичному Homebrew Menu | [[Grid]] |
| [[Show Hidden]] | Показувати також приховані застосунки | Вимкнено |

## Обрані застосунки
Перед назвою обраного застосунку стоїть ★, і він тримається вгорі списку.
Обране працює лише тоді, коли [[Sort]] — один із варіантів `(Star)`.

- Натисніть **L3** на застосунку, щоб виконати [[Star]] або [[Unstar]].
- Або натисніть **+** і виберіть [[Star]] чи [[Unstar]]. Так можна позначити й кілька вибраних застосунків.

## Вибір кількох застосунків
1. Натисніть **X** ([[Select]]) на кожному застосунку. На ньому з'явиться позначка, а курсор перейде до наступного.
2. Натисніть **Y** ([[Invert]]), щоб вибрати все невибране і зняти вибір з вибраного.
3. Натисніть **+**. Панель покаже [[SELECTED HOMEBREW]] з кількістю, і дії застосуються до всіх них.

Щоб зняти вибір із застосунку, натисніть на ньому **X** ще раз. Натискання **B**, коли вибрано застосунки, знімає вибір з усіх них, а малюнки позначок зникають; водночас Kefir Hub залишається відкритим (на відміну від натискання **B** без вибраних застосунків, яке одразу закриває застосунок).

Коли вибрано кілька застосунків, **+** відкриває параметри з [[SELECTED HOMEBREW]] угорі, над параметрами сортування й вигляду. Коли нічого не вибрано, параметри вигляду йдуть першими, а [[THIS HOMEBREW]] після них.

## Видалення застосунку

- [[Delete]] для одного застосунку дивиться й на його теку. Якщо застосунок лежить у `/switch/<тека>/` і більше `.nro` там немає, питання показує теку та її розмір з варіантами [[Only the app]] і [[App and folder]]. Тека запущеного Kefir Hub не пропонується ніколи.
- Іконка HOME (форвардер), створена Kefir Hub для цього застосунку, видаляється разом із ним. Питання про це говорить: [[The HOME icon that starts it is deleted too.]]
- Коли вибрано кілька застосунків, питання лишається старим: видаляються лише файли `.nro`.
1. Виберіть застосунок або кілька.
2. Натисніть **+** і виберіть [[Delete]].
3. Підтвердьте кнопкою [[Delete]].

Kefir Hub видаляє файл `.nro`. Папка застосунку та інші його файли (налаштування, збереження самого застосунку)
лишаються на карті; за бажання видаліть їх у [Файловому браузері](file-browser.md).

!!! warning
    Видалення не можна скасувати.

## Зміна назви та іконки застосунку
Ця дія переписує назву та іконку, що зберігаються всередині файлу `.nro`, тож нові побачать усі лаунчери.

1. Виберіть один застосунок (інші не мають бути вибрані).
2. Натисніть **+** і виберіть [[Edit name and icon]].
3. Змініть [[App Title]]. Натисніть **A** на іконці або торкніться її, щоб вибрати нову:
   [[Local File]] (зображення з карти пам'яті) або [[SteamGridDB]] (пошук в інтернеті).
4. Виберіть [[Save Changes]].

- [[SteamGridDB]] пропонує кожну статичну картинку PNG/JPEG для гри: спершу іконки, потім обкладинки будь-якого розміру, hero-зображення й логотипи. Неквадратна картинка обрізається до центрального квадрата, без розтягування.
- У списку знайдених ігор **Y** ([[Search]]) відкриває клавіатуру з поточним текстом ([[Search SteamGridDB]]) і шукає знову.
- Якщо гра не знайдена, Kefir Hub каже [[No game by this name on SteamGridDB]] і одразу відкриває клавіатуру. Гра без картинок дає [[No pictures for this game on SteamGridDB]].
- Вибрана картинка відкривається в редакторі обрізання (масштаб, переміщення; **A** [[Apply]]), тож іконкою може стати лише її частина.
- Одночасно завантажується до 10 картинок. Якщо їх більше, спершу завантажується 5, а [[Load more]] додає ще по 5 (якщо лишилося б 1–3, вони завантажуються разом з попередньою партією).
- Знайдені ігри й завантажені картинки зберігаються до перезапуску Kefir Hub: повторний вхід не завантажує їх знову.

<!-- shot: homebrew-edit-name-icon | Edit name and icon screen with title field and icon preview -->

## Теки
<!-- draft
- folders in Homebrew are virtual: they only group apps in this list; no file on the SD card moves (many apps look for their files at a fixed place under /switch)
- folders show first on the top level, with the number of apps in them; there are no folders inside folders
- one app can be in several folders; the top level ([[Homebrew]]) counts as a folder too, so an app can stay on the top level and be in a folder at the same time
- **A** ([[Open]]) goes into a folder; **B** goes back to the top level
- make a folder: **+** on the top level → [[New folder]], type the name
- on an app (or the apps selected with **X**/**Y**), **+** gives [[Copy to folder]] (the app stays where it is and shows in the chosen folder too) and [[Move to folder]] (the app leaves the folder on screen); the list of folders also has [[Homebrew]] (the top level) and [[New folder]]
- inside a folder, **+** on an app also gives [[Remove from this folder]]; an app that is left in no folder goes back to the top level
- **+** on a folder row gives the THIS FOLDER section: [[Open]], [[Rename]], [[Edit name and icon]], [[Remove icon]] (only when the folder has an icon), [[Delete]]
- [[Edit name and icon]] opens the same editor as for an app, without the forwarder options; without a picture the row shows small icons of the apps in the folder: one app nearly fills the picture, 2 to 4 apps in 2x2, 5 or more in 3x3 (the first 9); an empty folder shows a folder shape
- [[Delete]] on a folder asks first and removes only the folder: its apps stay in Homebrew
- the folders are kept in /config/kefir/homebrew_folders.txt, folder pictures in /config/kefir/homebrew_folders/
-->
<!-- shot: homebrew-folders | Homebrew tab, top level: folder rows "Demo Tools" (icons of its two apps) and "Emulators" (its own icon) before the apps -->
<!-- shot: homebrew-folder-options | Homebrew options panel on a folder row: THIS FOLDER section with Open, Rename, Edit name and icon, Remove icon, Delete -->
<!-- shot: homebrew-copy-to-folder | Copy to folder list for an app: Demo Tools, Emulators, New folder -->

## Передати програму на іншу консоль
Натисніть **+** і виберіть [[Send to another console]]. Hub запакує всю теку програми (наприклад, `/switch/<програма>`) в один `.zip` і запропонує її по Wi-Fi. `.nro`, що лежить просто в `/switch`, надсилається окремо. На іншій консолі відкрийте [[Console Transfer]] → [[Receive files]]. Див. [Перенести файли й папки](console-transfer.md#move-files-and-folders). Консоль-відправник показує смугу для кожного файлу під час передачі.

## Додати застосунок до HOME Menu (форвардер)
Форвардер — це іконка в HOME Menu, яка запускає homebrew напряму, без Homebrew Menu.
Застосунок стартує в режимі тайтла, з усією пам'яттю.

!!! warning
    Форвардер встановлюється як гра. Встановлення будь-чого, крім офіційного контенту, може призвести до бану
    консолі, коли вона вийде в мережу.

1. Виберіть один застосунок (інші не мають бути вибрані).
2. Натисніть **+** і виберіть [[Install Forwarder]] у розділі [[FORWARDER]].
3. На sysMMC Kefir Hub спершу показує попередження про бан. Виберіть [[Install]], щоб продовжити. Див. [Попередження про бан на sysMMC](install/index.md#ban-warning).
4. Якщо ввімкнено [[Ask every time]], відкриється редактор форвардера: змініть назву, автора, версію чи іконку й виберіть [[Create Forwarder]]. Якщо вимкнено, форвардер створиться одразу з типовими опціями.
5. Поверніться в HOME Menu. Там з'явиться нова іконка.

<!-- draft
- [[Address space]] / [[Address Space]] has a new value [[42-bit]] (the widest; DBI calls it "42 Bit"); it needs firmware 23.0.0 or newer: on an older firmware a 42-bit forwarder does not start
- [[Automatic]] now means 42-bit on firmware 23.0.0 and newer and 39-bit before; the default stays [[Automatic]]
- in the forwarder editor the value cycles 42-bit → 39-bit → 36-bit → 32-bit → 32-bit (no alias) → back to 42-bit (or 39-bit on an older firmware)
-->
| Опція | Що робить | Типово |
|---|---|---|
| [[Ask every time]] | Відкривати редактор для кожного форвардера замість типових значень | Вимкнено |
| [[Forwarder options]] | Типові значення для нових форвардерів. Також у [[Settings]] → [[Homebrew]] → [[Forwarders]] | див. нижче |
| [[Profile Selection]] | Питати, який профіль користувача взяти під час запуску форвардера | Вимкнено |
| [[Address Space]] | Розкладка пам'яті для застосунку: 39-bit, 36-bit, 32-bit або 32-bit (no alias). Міняйте лише за потреби застосунку: 36-bit для деяких старих застосунків, 32-бітні варіанти для застосунків на кшталт Wine-NX чи Box64, яким потрібна фіксована низька база образу | 39-bit |
| [[CPU Cores]] | 3 або 4 ядра процесора для застосунку. Четверте ядро спільне із системою і може спричиняти підгальмовування | 3 ядра |
| [[Screenshots]] | Дозволити знімки екрана, поки працює застосунок | Увімкнено |
| [[Video Capture]] | Дозволити запис відео, поки працює застосунок. Потребує [[Screenshots]] | Увімкнено |
| [[svcDebug]] | Дозвіл налагодження для застосунку | [[Automatic]] |

Форвардер для будь-якого `.nro` або ROM гри можна зробити й у [Файловому браузері](file-browser.md#make-a-forwarder).

## Додати папку пошуку
Kefir Hub може показувати застосунки з інших папок, крім `/switch/`.

1. Перейдіть до [[Tools]] → [[Settings]] → [[Homebrew]] → [[Homebrew Search Paths]].
2. Виберіть [[Add folder]] і вкажіть папку на карті пам'яті.

Або у [Файловому браузері](file-browser.md) наведіть курсор на папку (нічого не вибираючи), натисніть **+**,
виберіть [[Advanced]], а потім [[Add to Homebrew Search Paths]].

Щоб прибрати папку пошуку, виберіть її в [[Homebrew Search Paths]] або скористайтеся пунктом
[[Delete Homebrew Search Paths]] у Файловому браузері.

## Інше в панелі опцій
Панель опцій вкладки [[Homebrew]] має також пункти розділу [[Install & Share]] ([[Web Server]], [[Mount MTP]],
[[PC Install (USB)]], [[Ownfoil]]) і розділу налаштувань ([[Install Title Mode forwarder]], [[Settings]]).
Див. [Встановлення ігор](install/index.md) і [Спільний доступ](sharing.md).

## Проблеми
**Застосунку немає в списку.** Перевірте, що файл має розширення `.nro` і лежить у `/switch/` або на одну папку
нижче. Для глибших папок потрібна [папка пошуку](#add-a-search-folder). Назви, що починаються з крапки, пропускаються.

**У застосунку немає іконки.** Kefir Hub показує лише іконки розміром 256 × 256 пікселів. Замість інших видно
типове зображення.

**У списку є «Kefir Updater» з версією «Removed».** Kefir Updater замінено на [[Updater]] у [[Tools]].
Натисніть **A** на цьому пункті, прочитайте повідомлення й утримуйте **A** на OK п'ять секунд. Пункт зникне.

**Застосунок прихований, і я не можу його показати.** Увімкніть [[Show Hidden]]. У Kefir Hub немає команди, щоб
приховати чи показати застосунок. Приховані застосунки позначено у `/config/kefir/playlog.ini`: рядок `hidden`
під шляхом застосунку. Видаліть цей рядок, щоб застосунок знову з'явився.
