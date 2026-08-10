# Kefir Hub — план ручного тестування

Чек-лист усього, що вміє програма. Проходити зверху вниз або блоками.
Позначення: `[ ]` не перевірено, `[x]` ОК, `[!]` баг (додати опис поруч), `[-]` пропущено.

**Правило супроводу:** кожне завдання з новою функцією має додати сюди кроки перевірки й зафіксувати їхній результат до завершення роботи.

**Поточний прохід: v0.13.433; базовий прохід v0.13.397 → виправлення у v0.13.398**

---

## 🐞 Знайдені проблеми

| # | Де | Проблема | Статус |
|---|---|---|---|
| 1 | Updater, Cheats, і решта меню | Немає loop-прокрутки (з останнього пункту не переходить на перший). У Kefir Settings була | ✅ 0.13.398 |
| 2 | Games + Homebrew, макет **Список** | Прокрутка по колу відсутня | ✅ 0.13.398 (та сама причина, що #1) |
| 3 | Загальне | Після проходу Updater → Kefir Settings → Cheats → Games додаток сильно гальмує, потім "роздуплюється" | 🔴 відкрито |
| 4 | Загальне / тач | Тач у якийсь момент вимкнув екран: кнопка HOME не реагує, але блокування консолі працює | 🔴 відкрито |
| 5 | Футер | Суміщені кнопки ("Prev/Next Page", "Previous/Next tab" тощо) не реагують на тач — один тач-таргет на дві дії | ✅ 0.13.398 |
| 6 | Games, макет Список | Написи `[S\|b]` не вирівняні в колонку — їх зсував розмір гри | ✅ 0.13.398 |
| 7 | Games/Saves, макет Список | Галочка виділення посеред рядка, а не зліва від іконки як у файловому менеджері | ✅ 0.13.398 |
| 8 | File Viewer / Theme Creator | "Zoom Up / Down" — це L2 + D-pad, одним тапом не виражається. Тач по підказці не робить нічого | 🟡 потребує рішення |

### Що зроблено у 0.13.398

- **Loop-прокрутка стала стандартом**: `List::m_wrap` тепер `true` за замовчуванням, 11 викликів `SetWrap(true)` видалено. `SetWrap(false)` лишився для списку, який не має крутитись. У `ScrollDown` додано умову «крутиться лише останній ряд», щоб у сітці з неповним останнім рядом крок вниз і далі потрапляв на останній елемент, а не на початок.
- **Tiles-режим Updater'а** мав власну навігацію повз `List` — `MoveTileSelection` тепер теж по колу.
- **Футер більше не зшиває кнопки**: ~110 рядків правил злиття видалено, натомість увесь рядок підказок міряється і масштабується (до 0.6×), якщо не влазить від правого краю до `layout::SIDE_X`. Кожна кнопка знову має власний тач-таргет; висота таргета не зменшується разом з текстом.
- **Права колонка списку** розбита на дві: розмір праворуч у колонці фіксованої ширини, `[...]` праворуч від неї — тож дужки скрізь на одному x.
- **Галочка виділення** — спільний `gfx::drawCheckbox`; у макеті Список рамка з'являється зліва від іконки на кожному рядку, щойно почалось виділення (як у файловому менеджері). Три копії цього малювання (filebrowser, dbi_menu, grid) зведені в одну.

> #8: щоб тач працював, зум треба перевісити на окремі кнопки (напр. ZL = менше, ZR = більше) — це зміна керування, потрібне рішення.
> #3 і #4 не досліджені: обидва потребують лог до перезапуску (`/config/kefir/log.txt` обнуляється при старті).

**Перед стартом:**
- [ ] Увімкнути логи (Settings → General → Logging), лог у `/config/kefir/log.txt`
- [ ] ⚠️ `log.txt` обнуляється при кожному запуску — знімати лог ДО перезапуску
- [ ] Мати під рукою: microSD з іграми, USB-HDD, картридж (GC), PC у тій же мережі, USB-кабель
- [ ] Перевірити версію в заголовку = очікувана

---

## 1. Запуск і базова навігація

- [x] Запуск як **applet** (album/фотки) — працює, попереджає про менші буфери
- [x] Запуск як **application** (title takeover) — працює
- [x] Запуск з `/hbmenu.nro` і з `/switch/kefir-hub.nro` — обидва шляхи
- [-] Перевірка оновлення при старті (GitHub API) → нотифікація "Update available" — поки не чіпаємо
- [x] Без інтернету — не падає, показує "No Internet"
- [x] `L`/`R` перемикають вкладки Apps ↔ Tools
- [x] `B` виходить, `SELECT` виходить
- [x] `START` відкриває Options поточного меню
- [!] Тач-керування: тап по елементу, скрол, тап по вже виділеному = дія — див. проблеми #4, #5, #8
- [-] Docked / Handheld — індикатор у шапці; сенсу в ньому користувач не бачить, під питанням чи потрібен
- [x] Годинник, батарея, індикатор мережі в шапці
- [ ] Settings → General → **Restart Kefir Hub**
- [ ] Settings → General → **Exit**
- [ ] "Replace hbmenu on exit" увімкнено → `/hbmenu.nro` замінюється при виході

### 1.1 Навігація й шапка (v0.13.399–0.13.404)
- [ ] Різкий свайп продовжує прокрутку після відпускання у горизонтальному Homebrew та вертикальних List/Grid; новий дотик одразу зупиняє рух
- [ ] Updater у List і Grid пропускає заголовки секцій/порожні клітинки та переходить по колу з першого доступного запису на останній і назад
- [ ] `X` у Games і Saves позначає поточний рядок, переводить курсор на наступний і тримає його видимим
- [ ] У Settings свайп прокручує саме ту панель, над якою рухається палець
- [ ] Довгі шлях/опис і лічильник у шапці не перекривають один одного, NAND/SD або футер; текст, що не влазить, прокручується

---

## 2. Вкладка Apps (Homebrew)

- [ ] Список NRO зі `/switch/` — іконки, назви, автори, розміри
- [ ] `A` — запуск homebrew
- [ ] `R3` — Star / Unstar (обране піднімається вгору)
- [ ] `START` → Sort (name/size/updated), Order (asc/desc)
- [x] `START` → Layout (List / Icon / Grid) — прокрутка по колу виправлена у 0.13.398
- [ ] `START` → Show Hidden
- [ ] Пошкоджений/нульовий NRO — не крашить
- [ ] Старий пункт "Kefir Updater" показує підказку про перехід у Tools → Updater

### 2.1 NRO і форвардери
- [ ] **Edit name and icon** змінює метадані самого NRO; після повторного сканування нові назва й іконка лишаються, а `.sphaira.tmp`/`.sphaira.bak` не залишаються
- [ ] **Install Forwarder** без `Ask every time` використовує збережені типові параметри; з `Ask every time` відкриває редактор перед встановленням
- [ ] Редактор приймає локальну іконку або SteamGridDB, дає змінити title/author/version і параметри запуску; порожні обов'язкові поля/іконка підсвічуються й блокують створення
- [ ] Отримання SteamGridDB API key через QR і телефон зберігає ключ; невалідний ключ та відсутній результат дають зрозуміле повідомлення

---

## 3. Вкладка Tools (R)

Сітка 3×3. Перевірити, що кожна плитка відкриває правильну сторінку:
- [ ] Updater
- [ ] Kefir Settings
- [ ] Cheats
- [ ] File Browser
- [ ] Saves
- [ ] Games
- [ ] Software
- [ ] Themes
- [ ] Settings
- [ ] `START` → **Install & Share** (Web Server / Mount MTP / PC Install USB / Settings)

---

## 4. Updater (Tools → Updater)

- [ ] Завантаження `nx-links.json`, кеш у `/config/kefir-updater/`
- [ ] Секції KEFIR / FIRMWARE / OTHER
- [ ] Шапка: Current Kefir, Latest Kefir, Current Firmware, Console (модель)
- [ ] `X` — Refresh (перезавантажує список)
- [ ] `START` → Layout: **List** ↔ **Grid** (плитки), перемикання зберігається
- [ ] Навігація по плитках: секції пропускаються, порожні слоти не виділяються

### 4.1 Kefir
- [ ] Встановлення версії Kefir (download → розпакування в `/kefir` → копіювання)
- [ ] `copy_files.txt` обробляється (staged `/kefir/config/kefir-updater/copy_files.txt`)
- [ ] Прогрес-бар з відсотками, скасування посеред завантаження
- [ ] Після встановлення — пропозиція **Reboot / Later**
- [ ] Версія в `/switch/kefir-updater/version` оновилась

### 4.2 Firmware
- [ ] Список прошивок, мітка **DOWNGRADE** червоним для нижчих версій
- [ ] Мітка "непідтримувана" для FW новішої за поточний AMS
- [ ] Завантаження + встановлення прошивки (`ams_su`)
- [ ] **Downgrade fix**: режим `Automatic` — сейв `8000000000000073` видаляється мовчки
- [ ] **Downgrade fix**: режим `Optional (ask)` — питає Yes/No
- [ ] **Downgrade fix**: режим `Off` — не видаляє
- [ ] `START` → **Apply downgrade fix** вручну
- [ ] Попередження про даунгрейд показується один раз, не двічі
- [ ] Після встановлення — Reboot / Later

### 4.3 Install manually (FirmwareManual)
- [ ] Відкриває file browser у режимі **picker**
- [ ] Вибір папки: `START`, верхній рядок "Select current folder", або будь-який файл усередині
- [ ] Встановлення прошивки з локальної папки
- [ ] Валідація вмісту папки (не-прошивка → зрозуміла помилка)

### 4.4 Network / Custom link
- [ ] "Network" відкриває GitHub-релізи (ghdl) — `A` Download
- [ ] Кастомні записи з `/config/kefir/github`
- [ ] "Custom link" — ввід URL з клавіатури, завантаження ZIP і розпакування на SD
- [ ] Невалідний URL → зрозуміла помилка, не краш

### 4.5 Changelog
- [ ] Перегляд changelog Kefir, скрол, кирилиця відображається

---

## 5. Kefir Settings (Tools → Kefir Settings)

- [ ] **Overclock status** — вмикає/вимикає `/atmosphere/kips/kefir.kip`
- [ ] **40MB Memory** — правка `force_40mb_applet` в `atmosphere.ini`
- [ ] **Redirect Emunand saves to SD** — показується ТІЛЬКИ якщо emuMMC активний
- [ ] **8GB DRAM status** — hold-confirm 3 сек, попередження про TegraExplorer
- [ ] Всі тумблери мають hold-confirm і показують реальний стан після повернення

### 5.1 Fan curve
- [ ] Редагування кривої handheld / docked (`Mode`)
- [ ] Add Point / Remove Point / перетягування точки
- [ ] Load Preset / Save Preset
- [ ] Запис у конфіг Atmosphere tskin, перевірити файл
- [ ] Без модуля fan control вхід у Fan curve пропонує встановлення; відмова все одно відкриває редактор
- [ ] Встановлення створює bundled sysmodule і або запускає його одразу, або чесно пропонує reboot; після цього зміни кривої застосовуються наживо

### 5.2 Module Manager (uninstaller)
- [ ] Список встановлених sysmodules
- [ ] `A` Toggle — старт/стоп модуля
- [ ] `X` Autostart — увімкнення автозапуску (`flags/boot2.flag`)
- [ ] `Y` Refresh
- [ ] Видалення модуля

### 5.3 Translate Interface
- [ ] Download / Update language packs (UltraHand zip)
- [ ] Бекап старого `package.ini` створюється
- [ ] Фільтр пакетів по мові/регіону консолі працює
- [ ] Встановлення окремого пакета перекладу

---

## 6. Cheats (Tools → Cheats)

- [ ] Джерело **NxDb** — вибір гри зі списку
- [ ] Джерело **Manual file** — вибір архіву через file picker
- [ ] `Download All` для гри
- [ ] `Select All` / `Toggle` окремих чітів
- [ ] `Preview` / `View Code` — перегляд коду чіта
- [ ] **Installed Cheats** — список встановлених, `Delete`
- [ ] **Fix BID** — виправлення build ID
- [ ] Видалення "осиротілих" чітів (сканування)
- [ ] Очистка кешу чітів
- [ ] CheatSlips логін (якщо використовується)
- [ ] Чіти реально працюють у грі (dmnt)

---

## 7. File Browser (Tools → File Browser)

### 7.1 Навігація
- [ ] Список файлів/папок, іконки за типом, розміри, дати
- [ ] Рядок `..` і "Select current folder" (закріплені зверху) — не ламають виділення/операції
- [ ] `A` Open, `B` Back
- [ ] `X` Select, `Y` Invert — мультивибір
- [ ] Sort (name/size/date), Order, Folders First, Hidden Last, Show Hidden

### 7.2 Файлові операції
- [ ] Copy → Paste (файл, папка, багато об'єктів)
- [ ] Cut → Paste (в межах одного fs і між різними)
- [ ] Delete (файл, порожня папка, папка з вмістом)
- [ ] Rename (файл, папка, кирилиця, пробіли, спецсимволи)
- [ ] Create File / Create Folder
- [ ] Копіювання великого файлу (>4 ГБ) — прогрес, скасування
- [ ] Скасування посеред копіювання не лишає биті файли

### 7.3 Архіви
- [ ] Відкриття ZIP як віртуальної папки (read-only)
- [ ] Extract here / Extract to root / Extract to… / Extract selection
- [ ] Compress to zip / Compress to… (файли + папки)
- [ ] Всередині ZIP: копіювання назовні працює, запис заборонений

### 7.4 Перегляд
- [ ] View Image (jpg/png), Fit Image, Zoom Up/Down, Previous/Next Image
- [ ] Кнопка `A` відкриває `.txt`, `.ini`, `.json`, `.md`, `.log` та інші відомі текстові формати у View; image/ZIP/NRO/install-дії зберігають пріоритет
- [ ] Create Switch Theme з картинки (theme creator: Target, Zoom, Theme Name, Author, Generate)
- [ ] У контекстному меню текстового файла є `View` і `Edit`; `Edit` недоступний для read-only джерела та файла понад 4 MiB
- [ ] У View footer показує `RS → Scroll` і `B → Back`; для writable-файла також `A → Edit`, яке переходить у Edit без повторного читання
- [ ] У View правий стік прокручує viewport без курсора, wrap і затримки при зміні напрямку на межі
- [ ] У Edit footer показує `LS/RS → Cursor / Scroll`, `A → Edit line`, `X → Actions`, `+ → Options`, `B → Back` без дубльованого `Scroll`
- [ ] У Edit лівий стік/D-pad рухає курсор, правий стік незалежно прокручує viewport, `A` відкриває keyboard із повним поточним рядком
- [ ] Edit / Insert line below / Delete / Join with next, включно з видаленням єдиного рядка; Cancel keyboard не змінює модель
- [ ] Undo/Redo повертають точний saved state: `*` з'являється після зміни, зникає після Undo до baseline та після успішного Save
- [ ] Go to line для `1`, середнього, останнього й завеликого номера затискає номер до діапазону та одразу показує рядок
- [ ] Save і повторне відкриття зберігають UTF-8, кирилицю, emoji, порожні/довгі рядки та наявний LF або CRLF
- [ ] Вихід із dirty Edit показує Save / Discard / Cancel; контрольована помилка Save лишає редактор відкритим, dirty і зі змінами в пам'яті
- [ ] `.ini` контрастно підсвічує секції/коментарі/ключі/значення; подвійний tap за 500 ms перемикає лише окремий RHS `true` / `false` або `u8!0x0` / `u8!0x1`, а Undo повертає значення
- [ ] Файл понад 4 MiB відкривається як обмежений read-only preview із `Preview truncated`; Edit не доступний
- [ ] Writable USB/SMB джерело дозволяє Edit/Save; read-only ZIP/HTTP/MTP джерело дозволяє лише View
- [ ] JPG/PNG/BMP/GIF як і раніше відкриваються в image-viewer: gallery, zoom, pan, fullscreen і theme creation без регресій

### 7.5 Хеші
- [ ] Hash → CRC32 / MD5 / SHA1 / SHA256, результат збігається з PC

### 7.6 Mount / Sources
- [ ] Mount → microSD
- [ ] Mount → **USB drive** (HDD), read-only і writable режими
- [ ] Mount → мережева локація (SMB / WebDAV / FTP / HTTP)
- [ ] Unmount
- [ ] **Ignore read only** тумблер
- [ ] Add network location — SMB, WebDAV, FTP, HTTP
- [ ] Edit Source / Rename Source / Delete Source / Properties
- [ ] **Test Connection** — успіх і фейл дають різні зрозумілі повідомлення
- [ ] Навігація по мережевій папці, копіювання з неї і на неї
- [ ] Upload to network location

### 7.7 Встановлення з файлового браузера
- [ ] Install — `.nsp`, `.nsz`, `.xci`, `.xcz`
- [ ] Install кількох вибраних файлів (черга)
- [ ] Install Forwarder (з NRO)
- [ ] Play with NXMP (відео)
- [ ] Асоціації файлів (`assets/romfs/assoc`) — відкриття у відповідному homebrew
- [ ] Split-файли (`.nsp.00`, `.xci.00`) — `Split` дія
- [ ] Для ROM з кількома асоціаціями launcher picker групує `RetroArch — …`, `TICO — …`, інші; вибраний core запускається з правильним шляхом і додатковим аргументом
- [ ] TICO-асоціації відкривають підтримувані 3DS/GC/Wii/PS1/PSP/GB/GBC/GBA/N64/SNES/Saturn/Dreamcast та Sega ROM; Gambatte/Genesis Plus GX отримують свій mode-аргумент
- [ ] ROM forwarder має редаговану назву, необов'язкову version та `Include platform in title`; порожні обов'язкові поля/іконка не дають створити форвардер

### 7.8 Web
- [ ] StartWebServer з файлового браузера

---

## 8. Saves (Tools → Saves)

- [ ] Список сейвів по акаунтах, іконки ігор
- [ ] `START` → Layout / Sort / Order
- [ ] **Accounts** — фільтр по користувачу
- [ ] **Data Types** — Account / Bcat / Device / System / Cache / Temp
- [ ] **Show saves** — фільтр
- [ ] `X` Select, `Y` Invert — масові операції
- [ ] **Backup** сейву (одного, кількох, усіх)
- [ ] **Compress backup** увімкнено/вимкнено — розмір і формат бекапу
- [ ] **Restore** сейву — вибір з кількох бекапів (picker)
- [ ] **Auto backup on restore** — авто-бекап створюється перед відновленням
- [ ] Save information (розмір, дати, journal)
- [ ] Системні сейви (експериментально) — бекап не ламає консоль

### 8.1 Синхронізація
- [ ] Settings → Sources → **Save sync location** (лише WebDAV)
- [ ] Додавання нової WebDAV-локації прямо з цього пункту
- [ ] **Sync with remote** — вивантаження бекапів
- [ ] **Auto-sync after backup**
- [ ] **Include remote backups** — віддалені бекапи видно в picker'і restore
- [ ] Відновлення з віддаленого бекапу (скачується → відновлюється)
- [ ] Обрив мережі посеред синку — зрозуміла помилка, без втрати локальних даних

---

## 9. Games (Tools → Games)

- [ ] Список встановлених ігор, іконки, версії
- [x] `START` → Layout, Sort By / Sort / Order — макет Список переперевірити після 0.13.398
- [ ] **Show unavailable games**, **Hide forwarders**
- [ ] **Info** — деталі тайтла
- [ ] **Launch random game**
- [ ] **List meta records** — список meta-записів
- [ ] **Title cache** тумблер + **Delete title cache**
- [ ] **Refresh**
- [ ] `A` Details → сторінка гри
- [ ] `X` Select / `Y` Invert → масові дії
- [ ] **Search** фільтрує ігри без втрати виділення; **Clear search** повертає повний список
- [ ] Sort By → **Last played** / **Play time** дає стабільний порядок, а недоступна статистика показує повідомлення без крашу
- [ ] Details показує Languages, Mods folder, Play time, Last played, Components, Tickets, Saves і Save quota без накладання довгих значень

### 9.1 Дії над грою
- [ ] Launch
- [ ] Open in file browser
- [ ] Content information (список NCA, розміри, типи)
- [ ] Backup and restore (перехід у Saves)
- [ ] Save information
- [ ] **Move to SD** / **Move to NAND** (весь тайтл)
- [ ] **Move component to SD** / **to NAND** (окремий компонент)
- [ ] Для split-title показуються лише напрямки, де є що переносити; перед Move є точний список компонентів, які буде перенесено/пропущено
- [ ] Під час Move інтерфейс і анімації лишаються плавними, прогрес реально рухається, а червона **Stop** реагує на touch і просить підтвердження
- [ ] Скасування Move до точки перемикання не лишає частковий тайтл на цільовому носії; успішний Move оновлює NAND/SD та запускає гру
- [ ] Масовий Move послідовно показує назву кожної гри й зупиняється по скасуванню без пошкодження вже завершених переносів
- [ ] **Create save**
- [ ] **Create mods folders**
- [ ] **Delete** (гра, оновлення, DLC — окремо і разом)
- [ ] Advanced options

### 9.2 Dump
- [ ] Dump NSP / Dump All / Dump Application / Dump Patch / Dump AddOnContent / Dump DataPatch
- [ ] Dump all components
- [ ] Вибір локації: microSD `/dumps/`, USB S2S, `/dev/null` (speed test), stdio, мережа
- [ ] Опції дампу (див. розділ 12.6) впливають на результат
- [ ] Дамп великої гри — прогрес, швидкість, скасування
- [ ] Дамп перевстановлюється назад без помилок

---

## 10. Software (Tools → Software)

- [ ] **Homebrew App Store** — список, пошук (`Search`), Filter/Sort/Order/Layout
- [ ] Встановлення застосунку з AppStore, оновлення, видалення
- [ ] `Files` — перелік файлів пакета
- [ ] `Details` / `Changelog` / More by Author / Leave Feedback / Visit Website
- [ ] **DBI** → Download DBI translations list
- [ ] DBI → встановлення конкретного фан-перекладу
- [ ] DBI → Russian latest DBI
- [ ] DBI → Reset DBI config (з попередженням і hold-confirm)
- [ ] **UAModDownloader**, **ModCD**, **SimpleModDownloader** — завантаження і запуск

---

## 11. Themes (Tools → Themes)

- [ ] **Themezer** — список тем, сторінки (`Next/Previous Page`, `Page` за номером)
- [ ] Filter: Target (Home Menu / Lock Screen / All Apps / Settings / Player Select / User Page / News)
- [ ] Tags, NSFW, Sort, Order, Search
- [ ] `Screenshot` — прев'ю теми
- [ ] `Star` / `Unstar` — обране з'являється у Themes як окремий пункт
- [ ] Download → тема в `/themes/sphaira`
- [ ] Launch NXthemes_Installer.nro
- [ ] Готові пакети: **Mario BG Dark**, **Switch 2 Theme by alexwak** — завантаження + розпакування в `/themes/`

---

## 12. Settings (Tools → Settings)

Двопанельна сторінка: категорії ліворуч, пункти праворуч. Перевірити навігацію між панелями і вхід/вихід у папки.

### 12.1 General
- [ ] Language — усі мови, включно з Auto; UI перемальовується
- [ ] Text scroll speed
- [ ] 12 Hour Time
- [ ] Clock sync (NTP) — час коригується
- [ ] Logging on/off (попередження про сповільнення)
- [ ] Replace hbmenu on exit
- [ ] Restart / Exit

### 12.2 Appearance
- [ ] Theme — вибір теми зі списку, застосовується миттєво
- [ ] Animated waves
- [ ] Kefir Hub theme options (підпапка)

### 12.3 Network
- [ ] **FTP** (підпапка): Enable, Anonymous, Username, Password, Port (5000)
- [ ] Підключення FileZilla анонімно і з логіном
- [ ] Зміна порту застосовується без перезапуску
- [ ] **MTP** тумблер — вмикання вимикає USB storage (і навпаки)
- [ ] **MTP storages** (підпапка): Show microSD / Show Install folder / Show Saves (read-only)
- [ ] **Show Games (read-only)** перемонтовує активний MTP і додає/прибирає Games drive без перезапуску програми
- [ ] Кастомні імена microSD і Install folder видно на PC
- [ ] **Add folder** — додавання довільної папки як окремого MTP-стораджу
- [ ] Видалення доданої папки
- [ ] ⚠️ Кілька доданих папок монтуються ВСІ, не лише перша
- [ ] **Nxlink** — прийом `.nro` з PC (`nxlink -a <ip> file.nro`)

### 12.4 Sources
- [ ] `+ Add network location` — SMB / WebDAV / FTP / HTTP
- [ ] Перехід у локацію відкриває file browser
- [ ] **USB storage** тумблер — HDD монтується/розмонтовується наживо
- [ ] **USB storage read-only** — запис реально блокується/дозволяється
- [ ] Список підключених накопичувачів оновлюється при вставлянні/вийманні
- [ ] **Save sync location**

### 12.5 Install
- [ ] Enable sysMMC / Enable emuMMC (з попередженням про бан)
- [ ] Install location: SD only / System only / System→SD / SD→System / Automatic
- [ ] Окремі **Reserve NAND** і **Reserve SD** зберігаються після перезапуску та реально виключають носій, якщо після інсталяції резерв буде порушено
- [ ] Allow downgrade
- [ ] Skip if already installed: Reinstall / Skip / Prompt — всі три поведінки
- [ ] Save options globally
- [ ] Boost CPU during transfer — швидкість помітно змінюється
- [ ] Install tickets only
- [ ] Skip base game / Skip game updates / Skip DLC / Skip DLC updates / Skip tickets
- [ ] Skip NCA hash verify / Skip RSA header verify / Skip RSA NPDM verify
- [ ] Ignore origin flag
- [ ] Convert ticket on install / Convert to standard crypto
- [ ] Re-encrypt to master key 0 / Lower required firmware
- [ ] Кожен тумблер справді впливає на встановлення (перевірити хоча б вибірково)

### 12.6 Dump
- [ ] Create nested folder
- [ ] Name XCI folder like the file (`.xci` у назві папки)
- [ ] Trim XCI / Label trimmed XCI
- [ ] USB transfer stream
- [ ] Convert ticket on dump

### 12.7 Forwarders
- [ ] `Ask every time`, Address space (Automatic/36-bit/39-bit), Profile selection, Screenshots, Video capture і svcDebug зберігаються та потрапляють у новий форвардер
- [ ] Video capture недоступний без Screenshots; Automatic svcDebug відповідає версії Atmosphere
- [ ] SteamGridDB API key: Set / Replace / Remove, QR-сторінка недоступна поза активним запитом

### 12.8 Screen off (Minus)
- [ ] Minus в Install queue виконує вибраний режим: Lower brightness / Turn off backlight / Screensaver; пробудження відновлює попередню яскравість/auto-brightness і не скасовує чергу
- [ ] Preview показує реальні Brightness/OLED/вибрані поля; clock, status, package, file, progress, speed, ETA, elapsed, battery, errors і R/W graph вмикаються незалежно
- [ ] У screensaver лівий stick рухає блок, правий Up/Down змінює й зберігає яскравість, правий Left/Right змінює drift speed; stick не закриває screensaver, кнопка або touch закриває

---

## 13. Install & Share (Tools → START)

### 13.1 Web Server
- [ ] Старт у **title mode** — одразу піднімається
- [ ] Старт у **applet mode** — пропонує Start anyway / Install Title Mode forwarder / How to enter Title Mode
- [ ] Install Title Mode forwarder — іконка з'являється в HOME Menu і запускається
- [ ] Без інтернету пункт сірий, натискання пояснює і пропонує підключитись
- [ ] QR-код + URL показуються, адреса відкривається в браузері PC/телефону
- [ ] `/` та `/files` — перегляд папок, навігація
- [ ] `/download` — завантаження файлу на PC
- [ ] `/view` — перегляд файлу в браузері
- [ ] `/upload` (PUT) — завантаження файлу на консоль, прогрес видно на консолі
- [ ] `/delete` (DELETE) — видалення
- [ ] `/list` та `/list-recursive` — JSON
- [ ] `/album` — галерея скріншотів, навігація по папках, скачування
- [ ] `/album` визначає реальну гру через album service, показує правильні назву й Title ID для screenshot/video; невідомий файл лишається `Unknown Game`
- [ ] `/status`, `/progress`
- [ ] Вибір shareRoot (сторінка вибору папки) — обмежує доступ
- [ ] Зупинка сервера, повторний старт
- [ ] Після sleep/applet switch з тим самим IP сервер знову приймає запити за старим URL; при зміні IP або 30 с offline сам зупиняється з правильною причиною
- [ ] Web File Manager має один Root breadcrumb і перехід до Screenshots без дубльованої навігації
- [ ] Некоректний запит (405/404/431) — сервер не падає

### 13.2 MTP
- [ ] Mount MTP → PC бачить пристрій, підпис змінюється на "MTP: Active"
- [ ] Повторне натискання зупиняє MTP
- [ ] Копіювання файлів PC ↔ консоль
- [ ] ⚠️ Файли в корені microSD (шляхи виду `//file.nsz`) обробляються правильно
- [ ] Read-only Saves-драйв: читається, запис заборонений
- [ ] Read-only Games-драйв містить по папці на встановлену гру та окремі NSP для base/update/DLC; копія на PC перевстановлюється й не займає місце на microSD
- [ ] Games-драйв відхиляє create/write/delete/rename, коректно читає файли великими й довільними range-запитами та не показує archived title без контенту
- [ ] Запуск MTP в applet mode показує попередження про обмежену пам'ять і ризик NSZ, але дозволяє продовжити

### 13.3 PC Install (USB) — DBI backend
- [ ] Підключення PC-клієнта, поява черги
- [ ] `X` Select / `Y` Invert / `R3` Package target
- [ ] `A` Install selected
- [ ] `START` → Skip if already installed / Install location
- [ ] "Already installed. Reinstall?" — Yes/No працюють
- [ ] `B` Cancel session / Cancel queue посеред встановлення
- [ ] Summary: лог сесії, `Y` Errors ↔ Session log
- [ ] Після сесії MTP/USB storage повертаються в попередній стан
- [ ] Один пункт PC Install автоматично розпізнає **DBI Backend**, **Awoo/Tinfoil** і **GoldLeaf v0.10+**; для кожного протоколу та сама review/install/summary черга
- [ ] Екран очікування показує USB state і speed; stream mode відхиляється з підказкою вимкнути його, а обрив дає retry/зрозумілу причину замість зависання
- [ ] В applet mode до старту USB-інсталяції постійно видно попередження про NSZ і рекомендацію Title Mode

---

## 14. Встановлення ігор — усі шляхи

Два стеки поверх спільного 3-потокового yati-пайплайну. Перевірити обидва.

### 14.1 File / queue стек
- [ ] Один `.nsp` з microSD
- [ ] Один `.nsz` (стиснутий)
- [ ] `.xci` / `.xcz`
- [ ] Черга з кількох файлів (мультивибір у file browser)
- [ ] Встановлення з USB-HDD
- [ ] Встановлення з мережевої локації (SMB / WebDAV / FTP / HTTP)
- [ ] Встановлення з ZIP (якщо підтримується)
- [ ] Split-файли (`.00`, `.01`, …)

### 14.2 Stream / MTP стек
- [ ] Встановлення через **MTP Install**
- [ ] Встановлення через **FTP Install**
- [ ] Встановлення через **USB Install** (ns-usbloader / fluffy)
- [ ] ⚠️ Прогрес: розмір невідомий → indeterminate, БЕЗ відсотків >100%
- [ ] ⚠️ Наприкінці MTP-встановлення немає "cannot copy" і гра не залишається "стоячою"

### 14.3 Спільне
- [ ] Оновлення (patch), DLC, DLC-оновлення — окремо і разом
- [ ] Встановлення поверх наявного (Reinstall / Skip / Prompt)
- [ ] Даунгрейд версії (з Allow downgrade і без)
- [ ] Встановлення на NAND і на SD згідно з Install location
- [ ] Скасування посеред встановлення — тайтл не лишається битим
- [ ] Помилка ключів / пошкоджений файл → зрозуміла помилка, не краш
- [ ] Мало місця на носії → зрозуміла помилка
- [ ] Встановлений тайтл запускається

### 14.4 Планування черги
- [ ] Review змішаної черги з pinned SD/NAND та Automatic заздалегідь розкладає пакети в порядку черги, враховує окремі резерви й переносить Auto на другий носій лише коли треба
- [ ] NAND/SD у review показують `+this / +all` для поточного пакета; фактична інсталяція використовує саме зафіксований при підтвердженні план
- [ ] Під час інсталяції є прогрес поточного файла й всієї черги, `Overall %`, швидкість та `this file / whole queue` remaining; значення не перевищують 100%
- [ ] GoldLeaf, Awoo/Tinfoil, DBI, local і network шляхи однаково виконують Install location, Reserve NAND/SD та Skip/Reinstall/Prompt

---

## 15. GameCard (Apps → GameCard, якщо доступно)

- [ ] Інформація про вставлений картридж (тайтл, версія, розмір)
- [ ] Гаряча заміна картриджа без виходу з меню
- [ ] `START` → Install options — встановлення вмісту GC
- [ ] `START` → Dump options — Dump XCI, Dump All Bins, Card ID Set, Card UID, Certificate, Initial Data
- [ ] Trim XCI впливає на розмір дампу
- [ ] `Prev` / `Next` — перемикання між тайтлами на картриджі
- [ ] Виймання картриджа посеред дампу → коректна помилка

---

## 16. IRS (Apps → IRS)

- [ ] Зображення з ІЧ-камери правого Joy-Con
- [ ] Controller / Rotation / Colour / Light Target / Gain / Negative Image / Format / Trimming Format
- [ ] External Light Filter
- [ ] Load Default

---

## 17. Мережа і джерела — окремо

- [ ] FTP-сервер: анонімний вхід, вхід з логіном, root-drop routing
- [ ] Один глобальний mount (`App::SetMountedFolder`) спільний для MTP / FTP / HTTP
- [ ] SMB-локація: підключення, навігація, читання, запис
- [ ] WebDAV-локація: те саме + save sync
- [ ] HTTP-локація: читання
- [ ] FTP-локація (як клієнт): читання
- [ ] Втрата Wi-Fi посеред операції → зрозуміла помилка, без зависання
- [ ] Введення WebDAV-адреси через swkbd — ⚠️ не крашить (історичний 0xffe в ams_mitm)

---

## 18. Регресії після merge `ponytail/dead-code-audit`

Комітів 4 (v0.13.383–386). Точкова перевірка того, що вони чіпали:

- [ ] **v0.13.383** (видалення мертвого коду) — загальний прохід по всіх меню, нічого не зникло
- [ ] **v0.13.384** (єдина логіка версій прошивки) — Updater правильно визначає downgrade і непідтримувану FW; `Current Firmware` у шапці збігається з реальною
- [ ] **v0.13.385** (один case-insensitive порівнювач шляхів) — розширення у ВЕЛИКОМУ регістрі (`GAME.NSP`, `X.XCI`, `A.ZIP`) розпізнаються; асоціації файлів працюють; сортування не зламалось
- [ ] **v0.13.386** (обрізаний curl Api + i18n) — усі завантаження живі: Updater, AppStore, Themezer, GitHub, DBI, cheats, переклади; жодного нелокалізованого рядка не з'явилось
- [ ] **v0.13.382** (mount every selected folder) — кілька MTP-папок монтуються всі
- [ ] `tests/test_path_util.cpp` проходить

---

## 19. Стрес і крайні випадки

- [ ] Довга робота (30+ хв) без витоків/сповільнення
- [ ] Дуже довгі імена файлів і глибока вкладеність
- [ ] Кирилиця / японська / емодзі в іменах файлів
- [ ] Папка з 1000+ файлами — швидкість відкриття і скролу
- [ ] Виймання microSD на ходу
- [ ] Виймання USB-HDD посеред копіювання
- [ ] Сон/пробудження консолі посеред операції
- [ ] Дві операції одночасно (напр. MTP + встановлення) — коректно блокується або працює
- [ ] Заповнена microSD
- [ ] Логи вимкнено → нема просідань у гарячих шляхах (MTP/yati)
