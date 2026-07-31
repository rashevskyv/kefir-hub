# Kefir Hub — план ручного тестування

Чек-лист усього, що вміє програма. Проходити зверху вниз або блоками.
Позначення: `[ ]` не перевірено, `[x]` ОК, `[!]` баг (додати опис поруч).

**Перед стартом:**
- [ ] Увімкнути логи (Settings → General → Logging), лог у `/config/kefir/log.txt`
- [ ] ⚠️ `log.txt` обнуляється при кожному запуску — знімати лог ДО перезапуску
- [ ] Мати під рукою: microSD з іграми, USB-HDD, картридж (GC), PC у тій же мережі, USB-кабель
- [ ] Перевірити версію в заголовку = очікувана

---

## 1. Запуск і базова навігація

- [ ] Запуск як **applet** (album/фотки) — працює, попереджає про менші буфери
- [ ] Запуск як **application** (title takeover) — працює
- [ ] Запуск з `/hbmenu.nro` і з `/switch/kefir-hub.nro` — обидва шляхи
- [ ] Перевірка оновлення при старті (GitHub API) → нотифікація "Update available"
- [ ] Без інтернету — не падає, показує "No Internet"
- [ ] `L`/`R` перемикають вкладки Apps ↔ Tools
- [ ] `B` виходить, `SELECT` виходить
- [ ] `START` відкриває Options поточного меню
- [ ] Тач-керування: тап по елементу, скрол, тап по вже виділеному = дія
- [ ] Docked / Handheld — обидва режими, індикатор у шапці
- [ ] Годинник, батарея, індикатор мережі в шапці
- [ ] Settings → General → **Restart Kefir Hub**
- [ ] Settings → General → **Exit**
- [ ] "Replace hbmenu on exit" увімкнено → `/hbmenu.nro` замінюється при виході

---

## 2. Вкладка Apps (Homebrew)

- [ ] Список NRO зі `/switch/` — іконки, назви, автори, розміри
- [ ] `A` — запуск homebrew
- [ ] `R3` — Star / Unstar (обране піднімається вгору)
- [ ] `START` → Sort (name/size/updated), Order (asc/desc)
- [ ] `START` → Layout (List / Icon / Grid)
- [ ] `START` → Show Hidden
- [ ] Пошкоджений/нульовий NRO — не крашить
- [ ] Старий пункт "Kefir Updater" показує підказку про перехід у Tools → Updater

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
- [ ] View as text (unfinished) — великі файли, UTF-8
- [ ] Create Switch Theme з картинки (theme creator: Target, Zoom, Theme Name, Author, Generate)

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
- [ ] `START` → Layout, Sort By / Sort / Order
- [ ] **Show unavailable games**, **Hide forwarders**
- [ ] **Info** — деталі тайтла
- [ ] **Launch random game**
- [ ] **List meta records** — список meta-записів
- [ ] **Title cache** тумблер + **Delete title cache**
- [ ] **Refresh**
- [ ] `A` Details → сторінка гри
- [ ] `X` Select / `Y` Invert → масові дії

### 9.1 Дії над грою
- [ ] Launch
- [ ] Open in file browser
- [ ] Content information (список NCA, розміри, типи)
- [ ] Backup and restore (перехід у Saves)
- [ ] Save information
- [ ] **Move to SD** / **Move to NAND** (весь тайтл)
- [ ] **Move component to SD** / **to NAND** (окремий компонент)
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
- [ ] `/status`, `/progress`
- [ ] Вибір shareRoot (сторінка вибору папки) — обмежує доступ
- [ ] Зупинка сервера, повторний старт
- [ ] Некоректний запит (405/404/431) — сервер не падає

### 13.2 MTP
- [ ] Mount MTP → PC бачить пристрій, підпис змінюється на "MTP: Active"
- [ ] Повторне натискання зупиняє MTP
- [ ] Копіювання файлів PC ↔ консоль
- [ ] ⚠️ Файли в корені microSD (шляхи виду `//file.nsz`) обробляються правильно
- [ ] Read-only Saves-драйв: читається, запис заборонений

### 13.3 PC Install (USB) — DBI backend
- [ ] Підключення PC-клієнта, поява черги
- [ ] `X` Select / `Y` Invert / `R3` Package target
- [ ] `A` Install selected
- [ ] `START` → Skip if already installed / Install location
- [ ] "Already installed. Reinstall?" — Yes/No працюють
- [ ] `B` Cancel session / Cancel queue посеред встановлення
- [ ] Summary: лог сесії, `Y` Errors ↔ Session log
- [ ] Після сесії MTP/USB storage повертаються в попередній стан

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
