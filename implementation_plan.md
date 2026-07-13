# План реалізації: закриття етапу «Сейви» (бекапи, синхронізація, MTP)

Попередній вміст цього файлу (Фази 13/14 — рефакторинг `app.cpp` і дедуплікація) виконано повністю та зафіксовано в `task.md`; файл переписано під новий етап.

Цей план зібрано з трьох джерел:
1. **Аудит поточного коду** модулів сейвів після рефакторингу (Фази 4 і 10): `sphaira/source/ui/menus/save_menu.cpp`, `sphaira/source/ui/menus/save/save_paths.cpp`, `save_locations.cpp`, `save_menu_ops.cpp`, `save_menu_detail.cpp`, а також `sphaira/source/download.cpp` (WebDAV-клієнт). Знайдено реальні дефекти — Частина I.
2. **Невиконані задачі з `task.md`**, що стосуються сейвів: id 31 (передумова), id 37, id 47 — Частина II.
3. **Невиконані пункти з `plan.md` розділ 9** («Підводні камені» та крок 5 «Стійкість»), які ніколи не були перенесені в задачі — увійшли до Частини I (крок S4) та Частини III (чеклист).

**Конвенції для виконавця (обов'язкові для кожного кроку):**
- Нові видимі рядки UI → `_i18n` + ключі в `assets/romfs/i18n/en.json` і `assets/romfs/i18n/uk.json`.
- Після кожного кроку: підняти версію в `sphaira/CMakeLists.txt`, додати розділ у `walkthrough.md`, оновити статус у `task.md`.
- **Збірку виконує Агент 1 у WSL; виконавець збірку сам не запускає.**
- Номери рядків у цьому плані звірені з кодом станом на v0.13.200. Якщо код зсунувся — орієнтуватися на назви функцій, не на номери.

---

## Частина I: Дефекти, знайдені аудитом коду (виправити першими)

### Крок S1: Auto-sync після Backup ігнорує вибрану локацію бекапу — вантажить не той файл

**Статус: виконано у v0.13.201.**

**Проблема.** У `Menu::BackupSaves(entries, location, backup_root)` ([save_menu_ops.cpp:76–147](sphaira/source/ui/menus/save/save_menu_ops.cpp:76)) блок автосинку:
- лямбда завершення захоплює `[this, entries, backup_root]` (рядок 84) — **`location` не захоплюється**;
- всередині жорстко створюється `fs::FsNativeSd sd_fs;` (рядок 98) і викликається `FindLatestBackupPath(&sd_fs, e, backup_root, latest_path)` (рядок 113).

Наслідок: якщо користувач зробив Backup у **stdio-локацію** (USB HDD через Location або Recent-папку), автосинк шукає «найновіший бекап» на **SD-карті** з чужим `backup_root`. Він або мовчки нічого не знайде (`FindLatestBackupPath` → false — файл пропускається без помилки), або, гірше, знайде **старіший** бекап на SD і вивантажить його в хмару, після чого покаже «Auto-sync successfull!».

**Виправлення** (файл [save_menu_ops.cpp](sphaira/source/ui/menus/save/save_menu_ops.cpp)):
1. У лямбду завершення (рядок 84) додати захоплення `location`; так само прокинути `location` у внутрішню лямбду ProgressBox (рядок 94).
2. Замінити `fs::FsNativeSd sd_fs;` на `const auto fs = MakeFsForLocation(location);` (хелпер уже існує — `save_locations.cpp:151`).
3. Усі використання `&sd_fs` у цьому блоці → `fs.get()`. `FindLatestBackupPath` → `CollectBackups` → `CollectDbiBackups` вже приймають `fs::Fs*` і працюють відносно кореня fs, тож ніяких інших змін не потрібно.

**Перевірка:** Backup у stdio-локацію з увімкненим `Auto-sync after backup` вивантажує саме щойно створений ZIP (звірити ім'я файлу з timestamp на WebDAV-сервері); Backup на SD у стандартний `/dumps` — поведінка без змін.

---

### Крок S2: «Sync with remote» жорстко прив'язаний до SD `/dumps` — узгодити поведінку й тексти

**Факт.** `Menu::SyncSavesRemoteWithLocation()` ([save_menu_ops.cpp:957–1097](sphaira/source/ui/menus/save/save_menu_ops.cpp:957)) завжди використовує `fs::FsNativeSd sd_fs;` та `const fs::FsPath backup_root{"/dumps"};` (рядки 967–968). Тобто ручна синхронізація бачить: (а) DBI-бекапи на SD (`/switch/DBI/saves`, через `CollectDbiBackups`), (б) sphaira-структуру лише під `/dumps` на SD. Бекапи, зроблені у вибрану користувачем папку (Recent) або на stdio-носій, у синхронізацію **не потрапляють**, і завантаження з хмари лягають лише на SD.

**Рішення: лишити SD-only за задумом** (синхронізація визначена як «бібліотека на microSD ↔ WebDAV»; тягнути в неї довільні локації — окрема велика задача без запиту користувача), але прибрати неявність:
1. Винести дубльований літерал у спільну константу: `constexpr fs::FsPath DEFAULT_BACKUP_ROOT{"/dumps"};` в [save_paths.hpp](sphaira/include/ui/menus/save/save_paths.hpp) (або поряд у save-модулі). Замінити ним усі три жорсткі входження: `save_menu_ops.cpp:73` (перевантаження `BackupSaves`), `save_menu_ops.cpp:968` (ручний sync), `save_menu.cpp:947` (`default_backup_root` у побудові списку локацій).
2. Доповнити tooltip пункту `Sync with remote` ([save_menu.cpp:468–470](sphaira/source/ui/menus/save_menu.cpp:468)): явно сказати, що синхронізується бібліотека бекапів **на microSD** (стандартна тека `/dumps` і DBI-тека `/switch/DBI/saves`); бекапи, збережені в інші папки/носії, не охоплюються. Оновити ключ у en.json та uk.json (старий текст ключа змінюється — оновити обидві мови синхронно).
3. Додати абзац у `walkthrough.md` із цим самим поясненням для рев'юера.

**Перевірка:** tooltip англійською та українською описує SD-only-межу; поведінка sync не змінилася (регресії немає); grep по `"/dumps"` у save-модулях знаходить лише константу.

---

### Крок S3: Пікер «Location» обіцяє теку, а несистемні бекапи пишуться в `/switch/DBI/saves`

**Факт.** `BackupSaveInternal()` ([save_menu_ops.cpp:749–751](sphaira/source/ui/menus/save/save_menu_ops.cpp:749)): для несистемних сейвів (`dbi_format == true`, тобто всі Account/BCAT/Device/Cache/Temporary) шлях будується як `AppendPath(fs->Root(), BuildDbiSavePath(e, now_tm))` — тека `/switch/DBI/saves/...` на **обраному носії**, а `backup_root` (вибрана папка) ігнорується. `backup_root` реально впливає лише на системні сейви та на сканування при Restore. Це усвідомлене рішення id 51 (сумісність із DBI), його **не змінюємо**. Але UI вводить в оману: tooltip пункту `Location` ([save_menu.cpp:1036](sphaira/source/ui/menus/save_menu.cpp:1036)) каже «Choose the folder where backups will be stored or read from» без застережень.

**Виправлення (тільки тексти, без зміни поведінки):**
1. Переписати tooltip `Location` приблизно так (en, з перекладом в uk): «Choose the storage and folder for backups. Game saves are always written in DBI format to /switch/DBI/saves on the selected storage; the chosen folder is used for system save backups and for finding older backups during Restore.»
2. У `walkthrough.md` додати пояснення цієї схеми (де фізично лежать які бекапи: DBI-тека для ігор, `backup_root` для системних, legacy-читання).
3. i18n: en + uk.

**Перевірка:** прочитавши tooltip, користувач не очікує ігрові бекапи у вибраній папці; Backup/Restore поведінково не змінилися.

---

### Крок S4: Стійкість синхронізації — не обривати весь sync на першому невдалому файлі

**Статус: виконано у v0.13.202.**

**Проблема.** У `SyncSavesRemoteWithLocation()` перший невдалий upload (`R_THROW(Result_SaveSyncFailed)`, [save_menu_ops.cpp:1061–1064](sphaira/source/ui/menus/save/save_menu_ops.cpp:1061)) або download (через `DownloadOneBackupFile`, рядок 1083) миттєво завершує **всю** синхронізацію: решта файлів плану не передається, користувач бачить лише «Sync failed!» без переліку того, що встигло пройти. Пункт 5 розділу 9 `plan.md` («при обриві — продовжити з наступного файлу і в кінці показати список невдалих») ніколи не був реалізований. Захист від битих файлів уже є (download іде в `.temp` + rename — `DownloadOneBackupFile`, рядки 450–465) — його не чіпати.

**Виправлення** (файл [save_menu_ops.cpp](sphaira/source/ui/menus/save/save_menu_ops.cpp)):
1. У фазах upload (рядки 1047–1067) і download (1075–1087): замість `R_THROW` при невдачі конкретного файлу — записати ім'я файлу в локальний `std::vector<std::string> failed;`, за-`log_write`-ити і **продовжити цикл**. Лічильник прогресу інкрементувати як завершений (файл «оброблено», хай і невдало), щоб бар не завис.
2. Скасування користувачем (`pbox->ShouldExitResult()`) залишити як є — воно має перервати все одразу.
3. Після обох фаз: якщо `failed` порожній → `R_SUCCEED()` (нотифікація «Sync successfull!» без змін). Якщо не порожній → повернути `Result_SaveSyncFailed`, але перед цим через `evman`/завершальний колбек показати кількість невдалих: змінити завершальний колбек (рядки 1090–1096) так, щоб при часткових збоях показувати `OptionBox` виду «Sync finished with N failed transfers. See log for details.»_i18n замість голого «Sync failed!». Найпростіший механізм передачі `N` — поле `m_last_sync_failed_count` у `Menu` або захоплений `std::shared_ptr<int>`; вибрати простіше, не тягнути глобальний стан.
4. `DownloadRemoteBackupsForEntry()` (шлях «Include remote backups» перед Restore-пікером) **не чіпати**: там обрив доречний, користувач одразу бачить результат у пікері.
5. i18n: новий рядок повідомлення (en + uk).

**Перевірка:** вимкнути Wi-Fi посеред sync із ≥3 файлами у плані → решта файлів пробується, в кінці показано «N failed», локально немає файлів `.temp`; повторний sync після відновлення мережі докачує лише відсутнє; скасування кнопкою B перериває одразу.

---

## Частина II: Невиконані планові задачі по сейвах (з task.md)

### Крок S5 (task.md id 31): Таблиця сховищ MTP — передумова для диска «Saves»

Не сейв-задача сама по собі, але **обов'язкова основа** для S6 — без неї диск Saves доведеться вшивати в `haze::Init()` дублюванням. Виконувати за описом task.md id 31 (там повні кроки), з поправкою на актуальні координати:
- Реєстрація сховищ тепер: [haze_helper.cpp:905–913](sphaira/source/haze_helper.cpp:905) (`FsProxy` microSD + `FsInstallProxy("install", ...)`, `g_fs_entries` — рядок 858).
- **КРИТИЧНО (без змін із task.md):** перший аргумент (`m_name`) — технічний префікс шляхів; налаштовувати можна лише `display_name`. `m_name` microSD лишається порожнім (кореневі шляхи `//file.nsz` — див. пам'ятку про libhaze).
- Опції `m_mtp_show_sd`/`m_mtp_show_install`/`m_mtp_name_sd`/`m_mtp_name_install` в `app.hpp`, сайдбар «MTP storages» у Network settings, перезапуск MTP через `SetMtpEnable(false)/(true)` — усе за кроками id 31.

**Перевірка:** за описом id 31 у task.md.

### Крок S6 (task.md id 37): Розшифровані сейви як окремий MTP-диск «Saves» (read-only)

Найскладніша задача етапу. Виконувати **лише після S5**. Повний опис — task.md id 37; тут — уточнення до актуального коду, бо task.md посилається на дорефакторингові рядки:
- Базові класи: `FsProxyBase` — [haze_helper.cpp:143](sphaira/source/haze_helper.cpp:143), `FsProxy` — :180, `FsProxyVfs` (повністю віртуальний зразок) — :584, `FsInstallProxy` — :731.
- Перелік сейвів: `Menu::ReadSaveEntries` — тепер [save_menu.cpp:622](sphaira/source/ui/menus/save_menu.cpp:622), `GetFsSaveAttr` — :55, `GetAccountName` — :265.
- Санітизація/побудова імен: `BuildSaveName` та супутні — тепер у [save_paths.cpp:141](sphaira/source/ui/menus/save/save_paths.cpp:141) (модуль `ui/menus/save/save_paths.hpp`), **не** в save_menu.cpp. Для віртуальної ієрархії диска перевикористати саме ці функції, не копіювати.
- Маунт сейва: `fs::FsNativeSave(data_type, space_id, &attr, read_only)` — `fs.hpp` (звірити точні рядки на місці).
- Ієрархія диска: `/<GameName [TitleID]>/<Account <ім'я> | BCAT | Device | Cache>/<файли сейва>` — збігається зі структурою бекапів id 38 («гра → тип»); для назв підпапок типів перевикористати `GetSaveTypeSubdir()` ([save_paths.cpp:39](sphaira/source/ui/menus/save/save_paths.cpp:39)).
- Решта рішень (lazy mount з LRU-кешем ~4, read-only з `R_THROW(FsError_NotImplemented)` на всі write-операції, `MultiThreadTransfer=false`, сканування один раз при Init, опція `m_mtp_show_saves` за замовчуванням **вимкнена**) — без змін із task.md id 37.

**Перевірка:** за описом id 37 у task.md (диск видно у Windows; читання/копіювання на ПК працює; запис дає помилку без крашу; вимкнений тумблер — диска немає; зайнятий сейв валить лише своє піддерево).

### Крок S7 (task.md id 47): Папка «WebDAV» у Network settings + дружній ввід адреси

UX-обгортка конфігурації сейв-синхронізації. Повний опис — task.md id 47; поправка на актуальний код:
- Три `SettingsItem` («WebDAV URL» / «WebDAV User» / «WebDAV Password») тепер починаються з [settings_menu.cpp:1022](sphaira/source/ui/menus/settings_menu.cpp:1022) (task.md посилається на старі ~2369 — не шукати там).
- Ключові вимоги без змін: один пункт-папка `WebDAV` → правий Sidebar; поле називається `Server address` і показується без технічного префікса `webdav://`; канонізація голого домену/`https://`/`webdav://` у `webdav://...`; `http://` — локалізована відмова без зміни попереднього значення; пароль — фіксована маска, initial у клавіатуру не передавати; `GetWebdavLocations()` ([save_locations.cpp:30](sphaira/source/ui/menus/save/save_locations.cpp:30)) і модель даних не змінюються.

**Перевірка:** за описом id 47 у task.md.

---

## Частина III: Наскрізна ручна верифікація сейв-функціоналу (закриття етапу)

Після S1–S4 (мінімум; в ідеалі після всього) прогнати на консолі повний чеклист. Результати зафіксувати у `walkthrough.md`. Тестує користувач/Агент 1, план перевірки готує виконавець:

1. **Backup Account-сейва** (SD, стандартна локація) → ZIP у `/switch/DBI/saves/<гра>/<YYYYMMDD>/<TID>_A_..._<idx>.zip`; бекап видно й відновлюється у самому DBI.
2. **Backup системного сейва** → `/dumps/Save System/<id>/...zip` (стара схема, sphaira-формат із `.nx_save_meta.bin`).
3. **Backup у stdio-локацію (USB/HDD)** з увімкненим auto-sync → вивантажується саме новий файл (перевіряє S1).
4. **Restore:** один сейв із кількома бекапами → пікер дат; вибір старішого відновлює саме його; бекапи з legacy-структур sphaira (обидві) і DBI читаються; мультивибір → найновіший кожного без пікера; бекап зі зламаним іменем (ts==0) видно в кінці списку і він відновлюється.
5. **Include remote backups** → відсутні тягнуться з WebDAV, позначені хмарою, лягають у DBI-структуру (для DBI-імен).
6. **Sync with remote:** локальний-тільки ZIP → upload; віддалений-тільки → download на SD; однакові імена не перезаписуються; дві фази з окремими барами, бар не скидається між файлами.
7. **Обрив мережі посеред sync** → продовження з наступного файлу + підсумок «N failed», без `.temp`-сміття (перевіряє S4).
8. **Спецсимволи:** гра/акаунт із пробілами й кирилицею в назві → повний цикл upload → лістинг → download → Restore проходить (перевірка URL-кодування; `ListWebdav` декодує href — `download.cpp:1292`).
9. **Великий сейв (>1ГБ, напр. Animal Crossing):** бекап іде через файловий шлях (`file_download` — `save_menu_ops.cpp:761`), upload стрімить із файлу (`ReadFileCallback` читає чанками — `download.cpp:438`; підтверджено аудитом, RAM не роздувається) — пункт «Підводні камені» з plan.md §9 закривається цим тестом.
10. **Історія Recent-папок:** 6 виборів → лишаються 5 останніх; повторний вибір — move-to-front без дубліката; відключений USB-носій зникає зі списку, але не з конфігу.
11. **WebDAV-джерела:** Settings-URL і той самий URL у `locations.ini` не дублюються у виборі локації.

---

## Рекомендований порядок виконання

| Порядок | Крок | Розмір | Залежності |
|---|---|---|---|
| 1 | S1 (auto-sync fs) | малий | — |
| 2 | S4 (стійкість sync) | середній | — |
| 3 | S2 + S3 (константа + тексти) | малий | можна одним коммітом |
| 4 | Частина III, пп. 1–11 | тестування | після S1–S4 |
| 5 | S5 (id 31, таблиця MTP) | середній | — |
| 6 | S6 (id 37, диск Saves) | великий | після S5 |
| 7 | S7 (id 47, папка WebDAV) | середній | незалежний |

Кожен крок — окремий комміт із версією та записом у `walkthrough.md`. Реалізовує окремий агент; рев'ю виконує автор цього плану.
