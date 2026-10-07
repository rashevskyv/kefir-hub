# Системні інструменти

Керування системними модулями, крива вентилятора, збережені мережі Wi-Fi і профілі користувачів.

**Де:** [[Tools]] → [[Tools]]

<!-- shot: system-tools-list | Tools submenu: one list under three captions — Diagnostics, Settings, Maintenance -->

<!-- draft
- the list has three captions (v0.14.018): [[Diagnostics]], [[Settings]], [[Maintenance]]; the cursor skips a caption, tapping one does nothing
- the NAND zero-fill item was removed in v0.14.018; [[Fill free SD space with zeros]] stays, now under [[Maintenance]]
-->

| Група | Пункт | Що це |
|---|---|---|
| [[Diagnostics]] | [[System information]] | Консоль, Atmosphère, пам'ять, живлення, батарея, обладнання та ігрова активність. Див. [Системна інформація](#system-information). |
| [[Settings]] | [[Module Manager]] | Запуск, зупинка й автозапуск установлених системних модулів. |
| [[Settings]] | [[Fan curve]] | Криві швидкості вентилятора для портативного режиму й док-станції. |
| [[Settings]] | [[Wi-Fi]] | Збережені мережі Wi-Fi. |
| [[Settings]] | [[Users]] | Профілі користувачів консолі. Див. [Користувачі](users.md). |
| [[Maintenance]] | [[Clean system junk]] | Видаляє залишки. Див. [Очищення системи](#clean-system-junk). |
| [[Maintenance]] | [[Fill free SD space with zeros]] | Заповнює нулями вільне місце на карті пам'яті. Див. [Заповнення вільного місця на SD нулями](#fill-free-sd-space-with-zeros). |
| [[Maintenance]] | [[Remove parental controls]] | Заплановано; показує повідомлення «Coming soon». |

## [[System information]] { #system-information }

**Де:** [[Tools]] → [[Tools]] → [[System information]]

<!-- shot: system-tools-system-info | System information: Console group open, parameter → value rows, other groups closed -->

<!-- draft
- replaces the text report (v0.14.019): no file is written to the memory card any more
- one list of groups: [[Console]], [[Atmosphere]], [[Storage]], [[Power]], [[Battery]], [[Hardware]], [[Play activity]]; the first group is open at start
- press **A** on a group, or tap it, to open or close it; **A** on a row opens or closes the group the row belongs to; **Y** opens all groups or closes all
- each open group shows rows parameter → value; the number on the right of a closed group is its row count
- [[Console]]: firmware version, name and hash; model; hardware type and SoC; retail or development unit; burnt fuses; DRAM id; device id; kiosk; charger HiZ; serial number; console nickname; language; region; parental controls
- [[Serial number]] is the real one. [[Serial number source]] says where it was read: [[System settings]], [[PRODINFO partition]] or [[Backup file]] with the file path (Atmosphère `/atmosphere/automatic_backups`, hekate `/backup`). When the system returns a blank serial (Atmosphère blank_prodinfo, Incognito), the row [[Serial number (system)]] shows that blank value. The number is never computed or guessed; if no source has it, the value is [[Not available]]
- [[Atmosphere]]: version, key generation, target firmware, supported firmware, git commit, RCM bug patched, emuMMC (partition or file), blank PRODINFO, PRODINFO writes allowed, USB 3.0 forced
- [[Storage]]: system memory used and free; microSD used and free, speed mode, user and protected area; microSD CID: maker, OEM id, product, revision, serial, month made
- [[Power]]: charger, charging allowed, enough power, charging now, fast charging, USB charger type, USB power role, every current and voltage limit, HiZ, controller power supply, OTG, power delivery state (firmware 17.0.0 or newer for most rows)
- [[Battery]]: charge, raw charge, health, temperature, voltage; from the fuel gauge: design capacity, full capacity now, remaining capacity, charge cycles, age, current, average current, cell voltage, cell temperature, time to empty
- [[Hardware]]: Bluetooth and Wi-Fi MAC, configuration id, battery lot, serial number from the calibration data
- [[Play activity]]: installed games, total play time, total launches, most played game
- a group whose service cannot be read is not shown
-->

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
- FunControl (модуль вентилятора) запускає Kefir Hub, коли ви відкриваєте [[Fan curve]], і сам його зупиняє.

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

Першого разу Kefir Hub запропонує встановити модуль вентилятора Kefir. З ним нова крива застосовується без перезавантаження,
а на екрані видно показники датчиків у реальному часі. Виберіть [[Install]] (рекомендовано) або [[No]]. Якщо модулю потрібне
перезавантаження, щоб запуститися, Kefir Hub запитає [[Later]] чи [[Reboot]].

### Відредагуйте криву

1. Натисніть **X** ([[Mode]]), щоб перемкнутися між кривою для портативного режиму й для док-станції.
2. Виберіть точку кнопкою **D-pad** вгору/вниз або торкніться її на графіку.
3. Натисніть **A** ([[Edit]]). **D-pad** ліворуч/праворуч змінює температуру, вгору/вниз — швидкість вентилятора. Щоб завершити редагування, натисніть **A** або **B** ([[Done]]).
4. **L** ([[Add Point]]) додає точку; **R** ([[Remove Point]]) видаляє вибрану. У кривій має бути щонайменше дві точки.
5. Натисніть **+** ([[Apply]]), щоб зберегти й застосувати обидві криві.

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

## Очищення системи { #clean-system-junk }

Видаляє те, що лишається після встановлень і видалених ігор.

**Де:** [[Tools]] → [[Tools]] → [[Clean system junk]]

Вимкніть те, що хочете зберегти, і виберіть [[Run selected]]. Повідомлення наприкінці покаже, скільки місця звільнилося.

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
Корисно перед продажем чи передачею карти: видалені дані вже не відновити.

**Де:** [[Tools]] → [[Tools]] → [[Fill free SD space with zeros]]

1. Виберіть пункт і підтвердьте кнопкою [[Fill]].
2. Зачекайте. Смуга показує, скільки вже записано; на великій карті це може тривати довго.
3. **B** скасовує; уже записане місце знову звільняється.

Під час запису Kefir Hub лишає 64 МБ вільними, щоб система могла зберігати свої дані.

## Проблеми

**«Could not start this module.»** Деякі модулі запускаються лише під час завантаження. Увімкніть [[Autostart]] і перезавантажте консоль.

**«Failed to activate fan module.»** Криву збережено, але вона не діє. Утримуйте **A**, щоб перезавантажити консоль і застосувати її, або вона застосується після наступного перезавантаження.

**«No saved Wi-Fi networks».** Спершу додайте мережу в системних налаштуваннях консолі.
