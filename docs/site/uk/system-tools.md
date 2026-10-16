# Системні інструменти

Керування системними модулями, крива вентилятора, збережені мережі Wi-Fi і профілі користувачів.

**Де:** [[Tools]] → [[Tools]]

<!-- shot: system-tools-list | Tools submenu: one list under three captions — Diagnostics, Settings, Maintenance -->

| Група | Пункт | Що це |
|---|---|---|
| [[Diagnostics]] | [[System information]] | Консоль, Atmosphère, пам'ять, живлення, батарея, обладнання та ігрова активність. Див. [Системна інформація](#system-information). |
| [[Settings]] | [[Module Manager]] | Запуск, зупинка й автозапуск установлених системних модулів. |
| [[Settings]] | [[Fan curve]] | Криві швидкості вентилятора для портативного режиму й док-станції. |
| [[Settings]] | [[Wi-Fi]] | Збережені мережі Wi-Fi. |
| [[Settings]] | [[Users]] | Профілі користувачів консолі. Див. [Користувачі](users.md). |
| [[Maintenance]] | [[Battery]] | Стан датчика заряду і збереженої копії, висновок, резервна копія. Див. [Батарея](#battery). |
| [[Maintenance]] | [[Clean system junk]] | Видаляє залишки. Див. [Очищення системи](#clean-system-junk). |
| [[Maintenance]] | [[Fill free SD space with zeros]] | Заповнює нулями вільне місце на карті пам'яті. Див. [Заповнення вільного місця на SD нулями](#fill-free-sd-space-with-zeros). |
| [[Maintenance]] | [[Remove parental controls]] | Заплановано; показує повідомлення «Coming soon». |

<!-- draft
- new row under [[Diagnostics]]: [[Check game updates]] ("[[Find installed games that have a newer update.]]"); add it to the table above and give it a short section
- it downloads the list of the latest game updates that Kefir Updater used (switch-cheats-db, about 21 000 games, 3.6 MB) and compares it with the installed version of every game
- the page [[Game updates]] lists only the games with a newer update: name on the left, installed update and newest update on the right, for example v1 → v6 (the update number, as elsewhere in Kefir Hub); the caption on top says how many ([[Updates available: ]])
- games the list does not know (homebrew forwarders, very new or rare games) are not checked; the caption at the bottom counts them ([[Not in the list, not checked: ]])
- with nothing to update: the notice "[[All checked games are up to date]]"
- **B** stops the download; it needs the internet
-->

## [[System information]] { #system-information }

**Де:** [[Tools]] → [[Tools]] → [[System information]]

<!-- shot: system-tools-system-info | System information: Console group open, parameter → value rows, other groups closed -->

- Список груп замінює колишній текстовий звіт (v0.14.019): файл більше не записується на карту пам'яті.
- Групи містять такі пункти: [[Console]], [[Atmosphere]], [[Storage]], [[Power]], [[Battery]], [[Hardware]], [[Play activity]]; перша група відкрита за замовчуванням.
- Натисніть **A** на групі або торкніться її, щоб відкрити чи закрити її. Натискання **A** на рядку відкриває або закриває групу, до якої він належить. **Y** відкриває або закриває всі групи одразу.
- У версії v0.14.030 клавіші **ліворуч** / **праворуч** гортають сторінки рядків (як у файловому браузері); **L** / **R** ([[Previous group]], [[Next group]]) переміщують до попередньої чи наступної групи та відкривають її.
- Кожна відкрита група показує рядки параметр → значення. Число праворуч від закритої групи вказує кількість рядків у ній.
- Зовнішній вигляд відповідає стилю v0.14.027 (у v0.14.029 бічну панель прибрано): група виглядає як заповнена смуга, назва відкритої групи виділена акцентним кольором, а її рядки мають відступ; назви параметрів жирні, а значення — в акцентному кольорі.
- [[Console]]: версія прошивки, назва й хеш; модель; тип апаратного забезпечення та SoC; роздрібна чи розробницька консоль; пропалені запобіжники; ідентифікатор DRAM; ідентифікатор пристрою; кіоск; зарядний пристрій HiZ; серійний номер; псевдонім консолі; мова; регіон; батьківський контроль.
- [[Serial number]] є справжнім серійним номером. [[Serial number source]] вказує джерело його зчитування: [[System settings]], розділ [[PRODINFO partition]] або файл резервної копії (Backup file) із зазначенням шляху (Atmosphère `/atmosphere/automatic_backups`, hekate `/backup`). Якщо система повертає порожній серійний номер (при використанні Atmosphère blank_prodinfo чи Incognito), рядок [[Serial number (system)]] показує це порожнє значення. Номер ніколи не обчислюється і не вгадується; якщо жодне джерело його не містить, відображається [[Not available]].
- [[Atmosphere]]: версія, генерація ключів, цільова прошивка, підтримувана прошивка, коміт git, виправлення помилки RCM, emuMMC (розділ або файл), пустий PRODINFO, дозвіл на запис у PRODINFO, примусове ввімкнення USB 3.0.
- [[Storage]]: використана та вільна системна пам'ять; використана та вільна пам'ять microSD, режим швидкості, користувацька та захищена області; CID microSD: виробник, ідентифікатор OEM, продукт, версія, серійний номер, місяць виготовлення.
- [[Power]]: зарядний пристрій, дозвіл на заряджання, достатня потужність, стан заряджання зараз, швидке заряджання, тип зарядного пристрою USB, роздача живлення USB, усі обмеження струму та напруги, HiZ, живлення контролера, OTG, стан постачання живлення (для більшості рядків потрібна прошивка 17.0.0 або новіша).
- [[Battery]]: заряд, необроблений заряд, стан здоров'я, температура, напруга; дані палиміра (fuel gauge): розрахункова ємність, поточна повна ємність, залишок ємності, цикли заряджання, знос, сила струму, середній струм, напруга комірки, температура комірки, час до розряджання.
- [[Hardware]]: MAC-адреси Bluetooth та Wi-Fi, ідентифікатор конфігурації, партія акумулятора, серійний номер із даних калібрування.
- [[Play activity]]: встановлені ігри, загальний час гри, загальна кількість запусків, гра з найбільшою кількістю годин.
- Група, служба якої не може бути прочитана, не відображається.

## [[Module Manager]]

Показує всі системні модулі, встановлені в `/atmosphere/contents` на карті пам'яті: чи працює модуль зараз і чи запускається він під час завантаження консолі.

**Де:** [[Tools]] → [[Tools]] → [[Module Manager]]

<!-- shot: system-tools-module-manager | Module Manager list: green/grey dots, RAM per running module, "After reboot: Enabled/Disabled", Sysmodule RAM bar at the top -->

У кожному рядку:

- зелена крапка, якщо модуль працює зараз, сіра — якщо ні;
- назва й ідентифікатор програми; «[[Applies after reboot]]», якщо модуль можна запустити лише під час завантаження;
- обсяг пам'яті, який займає модуль, коли працює;
- «[[After reboot: Enabled]]» або «[[After reboot: Disabled]]» — чи запускається модуль під час завантаження консолі.

Смуга вгорі ([[Sysmodule RAM]]) показує, скільки системної пам'яті модулів зайнято і скільки вільно.

### Запустіть або зупиніть модуль

1. Виберіть модуль.
2. Натисніть **A** ([[Toggle]]). Робочий модуль зупиниться, зупинений — запуститься.

Деякі модулі тут перемкнути не можна:

- Модуль із позначкою [[Applies after reboot]] не запускається, поки консоль працює. Увімкніть автозапуск і перезавантажте консоль.
- sys-patch запускається під час завантаження, і запускати його вдруге не треба.

### Увімкніть автозапуск модуля

1. Виберіть модуль.
2. Натисніть **Y** ([[Autostart]]). У правій колонці з'явиться «[[After reboot: Enabled]]» або «[[After reboot: Disabled]]».
3. Зміна подіє під час наступного завантаження.

### Відомості про модуль

Натисніть **−** ([[Info]]): назва, ідентифікатор програми, поточний стан, використання пам'яті, автозапуск і опис (зі сторінки модуля на GitHub, якщо він є).

### Параметри

Натисніть **+** ([[Options]]).

| Параметр | Що робить |
|---|---|
| [[Start]] / [[Stop]] | Те саме, що **A** для вибраного модуля. |
| [[Autostart]] | Те саме, що **Y**. |
| [[Info]] | Те саме, що **−**. |
| [[Filter]] | Показати модулі: [[All]], [[Running]], [[Stopped]], [[Autostart]] або [[Applies after reboot]]. |
| [[Sort]] | Упорядкувати за [[Name]], [[Running]] або [[Autostart]]. |

Натисніть **X** ([[Refresh]]), щоб зчитати стани ще раз.

!!! warning
    Якщо зупинити модуль, від якого залежить система чи запущена гра, вона може аварійно завершитися. Якщо щось перестало працювати, перезавантажте консоль.

## [[Fan curve]]

Задає швидкість вентилятора для кожної температури — окремо для портативного режиму й док-станції.
Крива записується у файл Atmosphère `system_settings.ini`.

**Де:** [[Tools]] → [[Tools]] → [[Fan curve]]

<!-- shot: system-tools-fan-curve | Fan curve screen: graph with points, live temperature, points list, Handheld curve label -->

Поки цей екран відкритий, вентилятором керує сам Kefir Hub. Вентилятор іде за кривою на екрані,
тож кожну зміну ви чуєте одразу. Коли ви виходите з екрана, вентилятором знову керує консоль.

[[Apply]] зберігає обидві криві. Коли ви виходите з екрана, Kefir Hub запускає свій модуль вентилятора з новою кривою.
Модуль працює до наступного перезавантаження. Після перезавантаження консоль сама застосовує збережену криву, і модуль не запускається.
Модуль не лишає файлів на карті microSD.

### Відредагуйте криву

1. Натисніть **X** ([[Mode]]), щоб перемкнутися між кривою для портативного режиму й для док-станції.
2. Виберіть точку кнопкою **D-pad** вгору/вниз або торкніться її на графіку.
3. Натисніть **A** ([[Edit]]). **D-pad** ліворуч/праворуч змінює температуру, вгору/вниз — швидкість вентилятора. Щоб завершити редагування, натисніть **A** або **B** ([[Done]]).
4. **L** ([[Add Point]]) додає точку; **R** ([[Remove Point]]) видаляє вибрану. У кривій має бути щонайменше дві точки.
5. Натисніть **+** ([[Apply]]), щоб зберегти обидві криві. Вони діють і після виходу з екрана.

Точки зберігають порядок: точку не можна пересунути за сусідню.

**Y** вмикає режим [[Bezier]]: ви пересуваєте три контрольні точки (Min, Mid, Max), а крива плавно йде за ними.
Натисніть **Y** ([[Manual Mode]]), щоб повернутися до редагування окремих точок.

Якщо натиснути **B**, коли зміни не застосовано, Kefir Hub запропонує їх скасувати ([[Discard]]).

### Пресети

- **ZL** ([[Load Preset]]) завантажує пресет для поточного режиму: [[Cold console]], [[Quiet]], [[Balanced]], [[Fan off]], [[Fan 100%]] або один із трьох власних пресетів.
- **ZR** ([[Save Preset]]) зберігає поточну криву в одну з трьох власних комірок. Введіть назву.

Власні пресети зберігаються окремо для портативного режиму й док-станції.

!!! warning
    [[Fan off]] тримає вентилятор на 0% за будь-якої температури кривої (10–90 °C), а [[Fan 100%]] — на повній швидкості.
    Якщо граєте з [[Fan off]], стежте за температурою.

## [[Wi-Fi]]

Показує мережі Wi-Fi, збережені на консолі. До них можна під'єднатися, змінити або видалити. Нові мережі додають у системних налаштуваннях консолі.

**Де:** [[Tools]] → [[Tools]] → [[Wi-Fi]]

<!-- shot: system-tools-wifi | Wi-Fi list with two saved networks, one tagged Connected -->

### Під'єднайтеся до збереженої мережі

1. Виберіть мережу.
2. Натисніть **A** ([[Connect]]) і підтвердьте.
3. Результат з'явиться в заголовку, або «Connection timed out».

### Змініть або видаліть мережу

Натисніть **+** ([[Options]]) на мережі:

| Параметр | Що робить |
|---|---|
| [[Connect]] | Під'єднатися до цієї мережі. |
| [[Rename]] | Змінити назву, під якою показано мережу. |
| [[Change password]] | Задати новий пароль Wi-Fi без системних налаштувань. |
| [[Edit SSID]] | Змінити назву мережі (SSID), яку шукає консоль. |
| [[View password & details]] | Показати SSID, тип захисту й збережений пароль. |
| [[Delete network]] | Видалити збережену мережу. |
| [[Turn Wi-Fi Off]] / [[Turn Wi-Fi On]] | Вимкнути чи ввімкнути бездротовий зв'язок. |
| [[Refresh]] | Перечитати збережені мережі. |

!!! warning
    [[View password & details]] показує пароль на екрані відкритим текстом.

### Видаліть кілька мереж

1. Натисніть **X** ([[Select]]) на кожній мережі або **Y** ([[Invert]]), щоб інвертувати вибір.
2. Натисніть **+** → [[Delete selected]] і підтвердьте.

Щоб зняти вибір, натисніть **B**.

## [[Users]]

Створення, перейменування, резервне копіювання й прив'язка профілів користувачів консолі. Див. [Користувачі](users.md).

## Батарея {#battery}

**Де:** [[Tools]] → [[Tools]] → [[Battery]]

<!-- draft
- read only: nothing is written to the fuel gauge chip or to the system settings in this version
- [[Verdict]]: [[No sign of desync on this MMC]], or the signs found: [[Restore flag (MiscCFG bit 15)]] (the chip lost power; HOS writes this MMC's saved copy at the next start), [[Restart flag (Status POR)]], [[Chip vs this MMC's copy]] with the parameters that differ; also [[Battery version]] (CAL0 and set:cal disagree), [[Cycle count]] (Lite: not trusted, charge limit 4.192 V), [[Learned capacity]] below 70 % of design
- "Model: chip / sysMMC|emuMMC": the nine learned parameters (RCOMP0, TempCo, FullCap, FullCapNom, IAvgEmpty, QRTable00..30) as chip / saved copy, hex, capacities also in mAh; a row that differs is marked ≠
- [[Fuel gauge (chip)]]: charge, remaining, full and design capacity, cell and open-circuit voltage, cycles register, MiscCFG / Status
- [[Saved by the system]]: charge cycles and cycle count reliability of the MMC you are in; sysMMC and emuMMC each keep their own copy, so open the page in both
- **X** ([[Back up]]): `/config/kefir/battery/<date>-<sysmmc|emummc>.cal` (24 bytes, the `batt.cal` format of Battery Desync Fix; copy it to `/switch/batt.cal` to use it there) and a `.txt` beside it with all 256 chip registers, the saved copy, battery versions and lot
- why: HOS writes its saved copy into the chip only after the chip lost power, and each MMC has its own copy, so switching MMCs can leave the chip and the copy apart (wrong %, sudden shutdowns); the calibration wizard and the reset come in later versions
-->
<!-- shot: system-tools-battery | Battery page with the verdict and the model rows -->

## Очищення системи { #clean-system-junk }

Видаляє те, що лишається після встановлень і видалених ігор.

**Де:** [[Tools]] → [[Tools]] → [[Clean system junk]]

Список відкривається знизу. Натисніть A на рядку, щоб поставити або зняти галочку, потім виберіть [[Start cleanup]]. Повідомлення наприкінці покаже, скільки місця звільнилося.

| Пункт | Що видаляє |
|---|---|
| [[Old game updates]] | Оновлення, якщо встановлено новіше для тієї самої гри. |
| [[Lost content on the SD card]], [[Lost content in system memory]] | Файли ігор, якими не користується жодна встановлена гра. |
| [[Unfinished installs on the SD card]], [[Unfinished installs in system memory]] | Залишки встановлень, що зупинилися на півдорозі. |
| [[Unused tickets]] | Тікети ігор, яких уже немає на консолі. |
| [[Error reports]] | Звіти про збої в `/atmosphere/erpt_reports`. |
| [[Folders of removed games]] | Папки в `/atmosphere/contents` ігор, яких уже немає на консолі. Системні модулі лишаються. |
| [[Saves of removed users]] | Збереження користувачів, видалених з консолі. Вимкнено за замовчуванням: їх не відновити. |

## Заповнення вільного місця на SD нулями { #fill-free-sd-space-with-zeros }

Записує нулі в місце на карті пам'яті, яке не зайняте файлами. Файли, ігри й збереження лишаються як були.
Корисно перед продажем чи передачею карти: видалені дані вже не відновити. Пункт заповнення NAND нулями було видалено у версії v0.14.018, проте пункт [[Fill free SD space with zeros]] залишається доступним тепер під розділом [[Maintenance]].

**Де:** [[Tools]] → [[Tools]] → [[Fill free SD space with zeros]]

1. Виберіть пункт і підтвердьте кнопкою [[Fill]].
2. Зачекайте. Смуга показує, скільки вже записано; на великій карті це може тривати довго.
3. **B** скасовує; уже записане місце знову звільняється.

Під час запису Kefir Hub лишає 64 МБ вільними, щоб система могла зберігати свої дані.

<!-- draft
- the fill writes one temporary file, /kefir-hub-zero-fill.tmp, that grows until the card is full; nothing is reserved up front
- two steps on screen: "Step 1 of 2: writing zeros" with written / total, speed, time left and a write speed graph with the current ("Now") and the average speed of the whole fill, the time elapsed and the time left at the average speed; "Step 2 of 2: removing the temporary file, do not turn the console off"
- **B** stops within a second; the temporary file is then removed
- **Minus** ([[Screen off]]) shows the screensaver with the step, written / total, current and average speed, time left, time elapsed and the graph; with [[Inactivity timeout]] set it starts by itself; any button brings the progress back
- the figures change once a second
- the console does not sleep and the screen does not dim while the fill runs
- after a crash or a power-off during a fill, Kefir Hub removes the temporary file at its next start
- the card gets real zeros (the file is not encrypted)
-->

## Проблеми

**«Could not start this module.»** Деякі модулі запускаються лише під час завантаження. Увімкніть [[Autostart]] і перезавантажте консоль.

**«Failed to install the fan module.»** Криву збережено, але до наступного перезавантаження вентилятор іде за попередньою кривою. Перезавантажте консоль.

**«No saved Wi-Fi networks».** Спершу додайте мережу в системних налаштуваннях консолі.
