# Що лежить на карті памʼяті

Kefir записує на карту памʼяті ці папки й файли. Ця сторінка пояснює, для чого кожен з них, щоб ви знали, що можна
чіпати. Файли, які консоль або застосунок створює пізніше, позначено *створює консоль*.

!!! tip
    Не видаляйте папку лише тому, що не знаєте її. Спершу знайдіть її тут. Якщо сумніваєтесь, лишіть.

## Корінь карти

| Шлях | Що це |
|---|---|
| `payload.bin` | hekate, меню завантаження. Це пейлоад, який ви надсилаєте чипом, TegraRcmGUI або перезавантаженням з Kefir Hub. |
| `boot.dat`, `boot.ini` | Для чипів типу SX: `boot.ini` каже їм запустити `payload.bin`. Інші чипи та RCM їх не використовують. |
| `hbmenu.nro` | Homebrew Menu, запускається з Альбому. Kefir Hub стає на це місце, коли встановлений як меню. |
| `exosphere.ini` | Низькорівневі налаштування Atmosphère: режим відлагодження та приховування серійного номера (blank PRODINFO) в emuMMC. |
| `install.bat` | Windows-скрипт пакета Kefir: копіює файли на карту та прибирає залишки старих версій. На консолі не використовується. |
| `atmosphere/` | Atmosphère: кастомна прошивка. Див. [atmosphere/](#atmosphere). |
| `bootloader/` | hekate: меню завантаження та його налаштування. Див. [bootloader/](#bootloader). |
| `config/` | Налаштування Kefir, Kefir Hub і системних модулів. Див. [config/](#config). |
| `switch/` | Homebrew-застосунки й оверлеї. Див. [switch/](#switch). |
| `themes/`, `games/`, `warmboot_mariko/` | Див. [інші папки](#other-folders). |
| `emuMMC/` | *Створює консоль.* Образ emuMMC (копія системи) та `emummc.ini`. Ніколи не видаляйте: це ваш emuMMC. |
| `Nintendo/` | *Створює консоль.* Ігри та збереження, які sysMMC тримає на карті. emuMMC тримає свою копію всередині `emuMMC/`. |
| `backup/<серійний номер>/` | *Створює консоль.* Резервні копії NAND і ключів, зроблені в hekate. |
| `dumps/` | *Створює консоль.* Дампи ігор і збережень, зроблені Kefir Hub. |
| `startup.te` | *Створює консоль.* Одноразовий скрипт TegraExplorer, записаний Kefir Hub (перенесення профілів). TegraExplorer виконує і видаляє його. |

## atmosphere/ {#atmosphere}

| Шлях | Що це |
|---|---|
| `package3` | Сам Atmosphère: патчі ядра та всі системні модулі в одному файлі. Його запускає hekate. |
| `stratosphere.romfs` | Частина Atmosphère, постачається разом із `package3`. |
| `reboot_payload.bin` | Пейлоад, який запускає «Reboot to payload». Копія завантажувального пейлоада. |
| `hbl.nsp` | Homebrew Loader: запускає `.nro` з Альбому або замість гри. |
| `hbl_html/` | Файли, які Homebrew Loader використовує при запуску з Альбому. |
| `splash.png` | Картинка під час запуску Atmosphère. |
| `config/override_config.ini` | Яка кнопка і який тайтл запускають Homebrew Loader. |
| `config/system_settings.ini` | Системні перемикачі, які Atmosphère застосовує при завантаженні. [[Kefir Settings]] записує сюди [[40MB Memory]] і [[USB 3.0]]. |
| `config/stratosphere.ini` | Опції власних модулів Atmosphère. |
| `config/system_settings_stock.ini` | Друга копія `system_settings.ini`, яку тримає Kefir. |
| `config_templates/` | Еталонні копії Atmosphère з поясненням кожного ключа. При завантаженні не читаються. |
| `contents/<id>/` | Одна папка на системний модуль або гру. Id гри містить моди (`exefs/`, `romfs/`) і чити. Id модуля містить сам модуль (`exefs.nsp`), `flags/boot2.flag` (запуск при завантаженні) та `toolbox.json` (назва в оверлеї Sysmodules). |
| `exefs_patches/` | Патчі, які Atmosphère застосовує до системних модулів у памʼяті. `disable_ca_verification` дозволяє консолі працювати з серверами з власними сертифікатами; `bluetooth_patches` і `btm_patches` потрібні MissionControl; `fatal_force_extra_info` показує повні дані при фатальній помилці. |
| `nro_patches/` | Те саме для `.nro`. |
| `hosts/default.txt` | Правила hosts, які Atmosphère застосовує до мережі консолі (блокувати або перенаправити домен). У Kefir порожній. |
| `crash_reports/`, `fatal_reports/`, `erpt_reports/` | *Створює консоль.* Звіти після збою. Kefir Hub читає `fatal_reports/` для [Усунення проблем](troubleshooting.md). Можна видаляти. |
| `automatic_backups/` | *Створює консоль.* Резервні копії PRODINFO і ключів BIS, які Atmosphère робить сам. Зберігайте. |

Системні модулі в пакеті Kefir, за назвою папки в `contents/`:

| Папка | Модуль | Що робить |
|---|---|---|
| `00FF46554E43544C` | FunControl | Крива вентилятора. |
| `010000000000bd00` | MissionControl | Сторонні Bluetooth-контролери. |
| `420000000000000B` | sys-patch | Патчі підписів та інші патчі при завантаженні. |
| `420000000007E51A` | nx-ovlloader | Завантажує оверлеї Tesla та Ultrahand. |
| `690000000000000D` | sys-con | USB-контролери. |

## bootloader/ {#bootloader}

| Шлях | Що це |
|---|---|
| `hekate_ipl.ini` | Головні налаштування hekate: пункти завантаження (Atmosphere, Full Stock), автозапуск, затримка, яскравість. `hekate_ipl_.ini` — запасна копія. |
| `ini/` | Додаткові пункти завантаження: `atmostock.ini` (Semi-stock, застарілий), `kefir_updater.ini` (Update Kefir, запускає TegraExplorer), `!kefir_updater.ini` (пункт для автооновлення). |
| `nyx.ini` | Налаштування Nyx, сенсорного інтерфейсу hekate. `nyx.ini_` — запасна копія. |
| `payloads/` | Пейлоади в hekate → Payloads: `fusee.bin` (Atmosphère), `TegraExplorer.bin` (оновлення Kefir та робота з NAND), `Lockpick_RCM.bin` (ключі консолі). |
| `sys/` | Частини hekate: `nyx.bin` (інтерфейс), `res.pak` (його картинки), `emummc.kipm`, `libsys_lp0.bso` (сон), `libsys_minerva.bso` (тренування RAM), `thk.bin`, `l4t/` (Linux і Android). |
| `res/` | Іконки пунктів завантаження. |
| `update.bin` | Сам hekate. hekate запускає цей файл, коли він новіший за надісланий пейлоад. |
| `bootlogo_kefir.bmp`, `updating.bmp` | Картинки під час завантаження та оновлення Kefir. |

## config/ {#config}

| Шлях | Що це |
|---|---|
| `kefir/` | Kefir Hub: налаштування, мережеві розташування, журнали, дані перенесення профілів. Див. [Де зберігаються налаштування](settings.md#where-settings-are-stored). |
| `sphaira/` | Стара назва папки Kefir Hub. Kefir Hub переносить її в `kefir/` при першому запуску. |
| `kefir-updater/` | Оновлювач Kefir: `custom_packs.json`, `hide_tabs.json`, `kefir_updater.ini`. |
| `.skip` | Папки, які оновлювач Kefir пропускає при скануванні карти (`roms`, `retroarch`, `tico`). |
| `oc/` | Файли розгону: sys-clk, `kefir.kip`, оверлей. [[Overclock status]] копіює їх на місце; `oc_bkp/` тримає резервну копію, поки розгін вимкнено. |
| `8gb/` | Скрипти TegraExplorer і файли для [[8GB DRAM status]]. Лише для консолей з припаяними 8 ГБ RAM. |
| `semistock/` | Скрипт TegraExplorer, який скасовує Semi-stock (відновлює `emummc.ini` та `exosphere.ini`). |
| `MissionControl/` | Налаштування MissionControl (Bluetooth-контролери). |
| `sys-con/` | Налаштування sys-con (USB-контролери), один файл на тип контролера. |
| `ultrahand/` | Налаштування, теми та завантаження меню оверлеїв Ultrahand. |
| `sys-clk/` | *Створює консоль.* Профілі sys-clk, коли розгін увімкнено. |
| `redirect.bin` | *Створює консоль* через [[Redirect Emunand saves to SD]]. |

## switch/ {#switch}

| Шлях | Що це |
|---|---|
| `.overlays/` | Оверлеї (відкрити: **L + хрестовина вниз + правий стік**): `ovlmenu.ovl` (меню Ultrahand), `ovlSysmodules.ovl`, `ovlEdiZon.ovl` (чити), `NX-FanControl.ovl`, `sys-patch-overlay.ovl`. |
| `.packages/` | Пакети Ultrahand: Kefir Menu (Settings, Software, Theme, Translate Interface). |
| `kefir-hub.nro` | Kefir Hub. |
| `kefir-updater/` | Оновлювач Kefir: `kefir-updater.nro`, `update.te` (скрипт, який виконує TegraExplorer), `version` (встановлена версія Kefir). |
| `DBI/` | DBI, застосунок-інсталятор. |
| `daybreak/` | Daybreak, оновлювач прошивки від команди Atmosphère. |
| `linkalho/` | Linkalho, привʼязує користувача до акаунта без мережі. |
| `NX-Activity-Log/` | NX-Activity-Log, статистика часу гри. |
| `NxThemesInstaller/` | NXThemesInstaller, встановлює теми HOME-меню. |
| `TorrentShopNX/` | TorrentShopNX. |
| `appstore/.get/` | *Створює консоль.* Що встановив [[Homebrew App Store]]: одна папка на застосунок зі списком його файлів. |
| `sphaira/cache/` | *Створює консоль.* Кеш Kefir Hub: списки магазинів, іконки. Можна видаляти. |

## Інші папки {#other-folders}

| Шлях | Що це |
|---|---|
| `themes/systemPatches/` | Патчі, потрібні NXThemesInstaller для тем HOME-меню, один файл на прошивку. |
| `games/Homebrew menu […].nsp` | Форвардер, який виводить Homebrew Menu на HOME-екран. Встановіть його через Kefir Hub або DBI. |
| `warmboot_mariko/wb_XX.bin` | Прошивка режиму сну для консолей Mariko (V2, Lite, OLED), один файл на діапазон прошивок. Потрібні hekate. |
