# Системні інструменти

Керування системними модулями, крива вентилятора, збережені мережі Wi-Fi і профілі користувачів.

**Де:** [[Tools]] → [[Tools]]

<!-- shot: system-tools-list | Tools submenu: one list under three captions — Diagnostics, Settings, Maintenance -->

| Група | Пункт | Що це |
|---|---|---|
| [[Diagnostics]] | [[Check game updates]] | Знаходить установлені ігри з новішим оновленням. |
| [[Diagnostics]] | [[System information]] | Консоль, Atmosphère, пам'ять, живлення, батарея, обладнання та ігрова активність. Див. [Системна інформація](#system-information). |
| [[Settings]] | [[Module Manager]] | Запуск, зупинка й автозапуск установлених системних модулів. |
| [[Settings]] | [[Fan curve]] | Криві швидкості вентилятора для портативного режиму й док-станції. |
| [[Settings]] | [[Wi-Fi]] | Збережені мережі Wi-Fi. |
| [[Settings]] | [[Users]] | Профілі користувачів консолі. Див. [Користувачі](users.md). |
| [[Maintenance]] | [[Battery]] | Стан датчика заряду і збереженої копії, висновок, резервна копія. Див. [Батарея](#battery). |
| [[Maintenance]] | [[Clean system junk]] | Видаляє залишки. Див. [Очищення системи](#clean-system-junk). |
| [[Maintenance]] | [[Fill free SD space with zeros]] | Заповнює нулями вільне місце на карті пам'яті. Див. [Заповнення вільного місця на SD нулями](#fill-free-sd-space-with-zeros). |
| [[Maintenance]] | [[Remove parental controls]] | Заплановано; показує повідомлення «Coming soon». |

## [[Check game updates]] { #check-game-updates }

**Де:** [[Tools]] → [[Tools]] → [[Check game updates]]

Hub завантажує список останніх оновлень ігор, який використовував Kefir Updater (switch-cheats-db, близько 21 000 ігор, 3,6 МБ), і порівнює його зі встановленою версією кожної гри. Потрібен інтернет. **B** зупиняє завантаження.

Сторінка [[Game updates]] показує лише ігри з новішим оновленням: ліворуч назва, праворуч встановлене й найновіше оновлення, наприклад v1 → v6 (номер оновлення, як і в решті Kefir Hub). Підпис угорі каже, скільки їх ([[Updates available: ]]).

Ігри, яких немає у списку (форвардери homebrew, дуже нові або рідкісні ігри), не перевіряються. Підпис унизу рахує їх ([[Not in the list, not checked: ]]). Якщо оновлювати нічого, зʼявляється повідомлення [[All checked games are up to date]].

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

Лише читання: у цій версії нічого не записується ні в мікросхему лічильника заряду, ні в системні налаштування.

- [[Verdict]]: [[No sign of desync on this MMC]] або знайдені ознаки: [[Restore flag (MiscCFG bit 15)]] (мікросхема втратила живлення, HOS запише збережену копію цього MMC при наступному запуску), [[Restart flag (Status POR)]], [[Chip vs this MMC's copy]] з параметрами, що відрізняються. Також [[Battery version]] (CAL0 і set:cal не збігаються), [[Cycle count]] (Lite: не довіряють, ліміт заряду 4,192 В) і [[Learned capacity]] менше ніж 70 % від проєктної.
- Перший рядок [[Verdict]]: [[Full capacity / design]] у мА·год і відсотках. Якщо ємність більш ніж на 10 % менша за проєктну, рядок оранжевий і вважається десинхронізацією («— desync, the saved copy too»).
- «Model: chip / sysMMC|emuMMC»: дев'ять навчених параметрів (RCOMP0, TempCo, FullCap, FullCapNom, IAvgEmpty, QRTable00..30) як мікросхема / збережена копія, у hex, ємності також у мА·год. Рядок, що відрізняється, позначено ≠.
- [[Fuel gauge (chip)]]: заряд, залишок, повна й проєктна ємність, напруга елемента й розімкнутого кола, регістр циклів, MiscCFG / Status.
- [[Saved by the system]]: цикли заряду й надійність лічильника циклів для MMC, у якому ви зараз. sysMMC і emuMMC мають кожен свою копію, тож відкрийте сторінку в обох.
- [[Charge: shown / by voltage]]: відсоток, який показує консоль, і відсоток за напругою елемента. Якщо від батареї вони різняться на 10 і більше пунктів (консоль у звичайному використанні в нормі), відсоток хибний. Під час заряджання це лише підказка. [[Charge on the console screen]] — відсоток, який показує HOS.
- [[Charge cycles: this / other MMC]] оранжевий, коли два MMC рахують цикли з різницею в один і більше (Lite: ліміт напруги заряду залежить від лічильника).
- Кожне відкриття зберігає копію цього MMC (`/config/kefir/battery/<mmc>-copy.cal`). Коли сторінку відкрито один раз в іншому MMC, вона також показує [[Chip vs other MMC's copy]]. До того показується [[Other MMC]]: [[Not checked: boot it and open this page once]]. [[No sign of desync found]] зʼявляється лише тоді, коли інший MMC перевірено, а відсоток прочитано від батареї.
- **X** ([[Back up]]): файл `/config/kefir/battery/<дата>-<sysmmc|emummc>.cal` (24 байти, формат `batt.cal` з Battery Desync Fix; скопіюйте його в `/switch/batt.cal`, щоб використати там) і поруч `.txt` з усіма 256 регістрами мікросхеми, збереженою копією, версіями батареї та партією.
- **A** на рядку показує, що він означає. **A** на заголовку розділу відкриває або закриває його.
- **+** ([[Calibrate]]) відкриває той самий майстер, що й [[Tools]] → [[Tools]] → [[Battery calibration]] (див. цей розділ): його перший крок — скидання до заводської моделі. [[Actions]] на сторінці кроків відкриває [[Back up the chip's values]], [[Align the charge to the voltage]], [[Restore from the last backup]], [[Match the other MMC]] (без запису в мікросхему: копія цього MMC бере модель мікросхеми і більший лічильник циклів із двох MMC) та [[Battery type (PRODINFO)]].

Після скидання не давайте батареї розрядитися повністю в іншому MMC, поки не виконано обидва цикли. Відкрийте там сторінку один раз і виберіть [[Match the other MMC]].

Навіщо це: HOS записує свою збережену копію в мікросхему лише після втрати нею живлення, а кожен MMC має власну копію. Тому перемикання між MMC може розвести мікросхему і копію (хибний відсоток, раптові вимкнення).
Сторінка розряджання вмикає екранну заставку після часу простою, заданого для екрана встановлення (дві хвилини, якщо його вимкнено), щоб панель не вигоряла. Це чорна сторінка з показниками, що повільно зміщуються: цикл, смуга й відсоток батареї, потужність (Вт, мА) і температура, енергія (Вт·год), час від батареї та час до вимкнення (на зарядці — відсоток заряду).

Будь-яка кнопка або дотик будить екран (це натискання більше нічого не робить). Навантаження й вимірювання тривають позаду. Стік зміщує показники та задає яскравість і швидкість зміщення, як на екрані встановлення.

- Перехід через hekate в інший MMC (0.14.163) більше не крок майстра: [[5. Transfer to the other systems]] сам записує калібрування туди при наступному запуску Hub у тій системі (див. [[Battery calibration]]).
- [[Battery size]]: коли навчена ємність перевищує проєктну більш ніж на 30 %, [[Verdict]] каже, що вбудовано більшу батарею ([[of the design capacity: a larger battery is built in]]). Для типу батареї LiPo додається [[The battery type is LiPo (4.2 V): set it to LiHV (4.32 V) to use the whole battery]]. Це не ознака десинхронізації.
- На сторінці розряджання, коли батарея розрядилась: [[Keep the screen on while it charges. After a red battery, leave it on the charger for about 3 minutes before you switch it on.]]
- Порада зі спільнотного посібника: повний цикл раз на місяць зберігає ємність. Падіння приблизно на 300 мА·год у використанні повертається звичайним циклом розряджання й заряджання.
- **X** ([[Actions]]) → [[Battery type (PRODINFO)]] задає байт версії батареї в PRODINFO: [[LiHV (4.32 V): for a larger battery]] (версія 2) або [[Back to the original]]. Перед записом резервна копія PRODINFO (`/config/kefir/battery/prodinfo-<час>.bin`) читається назад і порівнюється. Змінюються лише блок батареї та хеш тіла. Результат перевіряється, і якщо перевірка не пройшла, старі значення повертаються. PRODINFO, що не проходить власні хеші чи CRC, не чіпається ніколи.
- Atmosphère блокує запис у PRODINFO. Коли блокування ввімкнено, Kefir Hub вмикає `allow_writing_to_cal_sysmmc=1` (exosphere.ini) і `enable_hbl_bis_write` (system_settings.ini), перезапускає консоль, сам завершує зміну при наступному запуску й знову вимикає обидва параметри. В emuMMC запис може бути відхилено (тоді перезапустіть у sysMMC). Зміна стосується PRODINFO тієї системи, де її виконано.
- Після зміни запустіть майстер калібрування (скидання, два повні цикли). Сторінка батареї тоді покаже тип [[Type]] LiHV (4.32 V) і повну ємність більшої батареї.

<!-- shot: system-tools-battery | Battery page with the verdict and the model rows -->

### [[Storage manager]] { #storage-manager }

[[Storage manager]] (Tools) показує кільце того, що заповнює карту microSD: [[Saves and album]], [[Atmosphère and Kefir]], [[Homebrew]], [[Dumps and backups]], [[Game files (NSP, XCI)]], [[Emulators and their games]], [[Other files]] і [[Free space]], з розміром і відсотком кожної частини. У центрі показано використане й загальне місце. Памʼять системи (використано / всього) записана під списком.

- **Up** / **Down** вибирають частину, **A** відкриває її картку: що це, чи можна це видалити, де воно лежить і чому існує.
- Теки підраховуються при відкритті сторінки (смуга прогресу показує крок). emuMMC на розділі лежить поза файловою системою карти і в кільці не показаний.
- Карту показано цілою: рядок під списком читається «microSD: total = N in the file system + M outside it». emuMMC на розділі — частина поза файловою системою, тож жодна тека її не показує.
- emuMMC — це один контейнер: у списку є рядок [[emuMMC]] із сумою, а під ним з відступом його частини: [[emuMMC partition]] або [[emuMMC files]] (сам emuMMC) і [[Games in the emuMMC]]. Вибір рядка підсвічує всі його частини в кільці. **A** на ньому пояснює, що таке emuMMC.
- **Y** ([[What is an emuMMC?]]) відкриває пояснення про emuMMC з будь-якого місця сторінки.
- [[Game files (NSP, XCI)]] рахує лише `/games`. [[Emulators and their games]] рахує емулятори (RetroArch, Tico) і теки з ROM: `/roms`, `/ROMs`, `/retroarch`, `/RetroArch`, `/tico`, `/Tico`.

### [[Web browser]] { #web-browser }

[[Web browser]] (Tools, група Settings) — сторінка з [[Enter an address]] і [[Favourites]]. Адресу можна ввести на клавіатурі консолі або надіслати з телефона чи комп'ютера (сторінка з QR-кодом, як в інших текстових полях; «https://» додається, якщо його бракує). [[Add to favourites]] стоїть над списком.

- На закладці **A** відкриває її у власному браузері консолі (той самий аплет, що запускає BrowseNX). **X** ([[Select]]) вибирає її, **Y** ([[Invert]]) інвертує позначки, як у файловому менеджері.
- **+** ([[Options]]) пропонує [[Open]], [[Add to favourites]] і [[Remove]] для позначених закладок або для тієї, що під курсором, якщо жодної не позначено.
- Закладки зберігаються в `/config/kefir/web_favorites.txt`.
- Потрібна повна памʼять, тож запускайте Kefir Hub з гри (утримуйте **R** під час запуску).
- На прошивках від 20.0.0 до 22.5.0 системний браузер просить оновити систему і не відкриває жодної сторінки. Тому рядок лише каже, що потрібна прошивка 23.0.1 або новіша ([[The browser needs firmware 23.0.1 or newer. Update the firmware in the Updater first.]]).

## Battery calibration {#battery-calibration}

<!-- draft
- five steps: [[1. Reset the gauge]], [[2. Cycle 1: drain, the zero, charge]], [[3. Cycle 2: drain, charge]], [[4. Report]], [[5. Transfer to the other systems]]; only the next step is active ([[Active]], [[Locked]], [[Done]]); a finished step is drawn in green with a tick in place of its number, and the cursor steps over it (the report and the transfer stay open once done); a road map at the foot of every page of the wizard shows the five steps, the ones done filled, the current one ringed
- [[Start calibration]] first makes a backup of the gauge (the toast says the file name under /config/kefir/battery/), then opens the steps
- step 1, [[Reset the gauge]]: done at once, at any charge level, no charging first (the row says [[next: backup, then the factory model; no charging needed]]); the Hub makes a backup and writes the factory battery model; what the gauge learned before is dropped; then cycle 1 begins with the drain
- step 2, cycle 1: take the charger out, [[Start the drain]]; the Hub drains at full load until the console switches itself off; the drain log (`/config/kefir/battery/calib-log.csv`, a line a second) keeps the voltage; after a switch-off, switch the console on again: without the charger the drain goes on in the tail mode (minimum load and brightness) until it does not start at all; when a low battery only puts the console to sleep (the lock screen) and you wake it, the Hub counts that as the cut-off too: a flag line `S` goes into the log, the toast says [[The console slept: the drain goes on slowly]], and the run goes to the tail mode at once, so the last percent comes out at rest and the cut-off voltage is honest
- the zero: when the console was switched on again (with or without the charger) and the Hub opened, the Hub takes the last voltage before the gap in the log and writes it into the gauge as the zero, by itself, no question (a backup first): the chip's empty-voltage threshold, and the charge counters set to what came in since the switch-on, so the shown percent is 0 % or near it; the page says [[The console switched off at]] X V, [[This is the empty battery.]], [[Written into the gauge: this voltage is now 0 %.]]; a cut-off outside 3.00 - 3.50 V is refused ([[The cut-off voltage is outside 3.00 - 3.50 V: not a zero.]]) and the drain is run again
- then the charge: the Hub opens the charge page (not the list of steps) and says which cycle it is and what to do: plug the charger in, wait for 100 %; the cycle counts by itself from the log (the console cut off, then the charge saw 100 %), even if the Hub is opened later with the charger already out
- step 3, cycle 2: the same drain and charge, no zero
- the cycle rows say where the cycle is: [[next: take the charger out and start the drain]], [[the drain is going on]], [[the drain is done. Plug the charger in and wait for 100 %: the cycle then counts by itself]]; cycle 1 shows [[the zero]] with its voltage once it is written
- step 4, [[Calibration report]]: [[The zero (empty battery)]], [[Learned capacity]] against design, the [[Verdict]], the gauge before each cut-off (per cycle, from the log)
- step 5, [[Transfer to the other systems]]: the calibration is noted in a ledger on the SD (`/config/kefir/battery/ledger.txt`: where it was made, a number, the battery model); at the next start of the Hub in another system (sysMMC, another emuMMC; a file-based one is told by its folder, a partition one by its start sector) that has an older number and the same battery model, the Hub writes it there by itself and shows [[Battery calibration was transferred from]] ... [[to]] ...
- the state is kept on the SD card (`/config/kefir/battery/calib.txt`: the step, the zero, the drain in progress; `calibration.txt`: the reset), so the Hub can be closed and the console used between the steps
- [[Actions]] opens the backup, [[Align the charge to the voltage]] (a manual action now), the restore, the match and the battery type
-->

**Where:** [[Tools]] → [[Tools]] → [[Battery calibration]]

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

Заповнення записує один тимчасовий файл `/kefir-hub-zero-fill.tmp`, який росте, доки карта не заповниться. Наперед нічого не резервується.

- На екрані два кроки. «Step 1 of 2: writing zeros» показує записано / всього, швидкість, час, що лишився, і графік швидкості запису з поточною («Now») та середньою швидкістю всього заповнення, час, що минув, і час, що лишився при середній швидкості. «Step 2 of 2: removing the temporary file, do not turn the console off».
- **B** зупиняє заповнення протягом секунди, після чого тимчасовий файл видаляється.
- **Minus** ([[Screen off]]) показує заставку з кроком, записано / всього, поточною й середньою швидкістю, часом, що лишився, часом, що минув, і графіком. Якщо задано [[Inactivity timeout]], заставка запускається сама. Будь-яка кнопка повертає показ прогресу.
- Цифри оновлюються раз на секунду.
- Під час заповнення консоль не засинає, а екран не тьмяніє.
- Після збою або вимкнення живлення під час заповнення Kefir Hub видаляє тимчасовий файл при наступному запуску.
- Карта отримує справжні нулі (файл не шифрується).

## Проблеми

**«Could not start this module.»** Деякі модулі запускаються лише під час завантаження. Увімкніть [[Autostart]] і перезавантажте консоль.

**«Failed to install the fan module.»** Криву збережено, але до наступного перезавантаження вентилятор іде за попередньою кривою. Перезавантажте консоль.

**«No saved Wi-Fi networks».** Спершу додайте мережу в системних налаштуваннях консолі.
