# Рефакторинг-план 2: аудит результатів фаз 1–8 та наступні кроки

Дата аудиту: 2026-07-13. Аудит проведено по стану `master` (HEAD = `3973de5`).
Це продовження `REFACTOR_PLAN.md`. Той план виконано повністю (всі 8 фаз позначені
`[x]`, збірка `sphaira.nro` підтверджена у walkthrough для v0.13.186).

---

## Частина I — Результати аудиту фаз 1–8

### I.1 Загальний висновок

Рефакторинг пройшов **успішно**:

- Усі нові TU присутні в `sphaira/CMakeLists.txt` (перевірено всі 23 нові файли).
- Проєкт збирається після кожної фази (підтвердження в `walkthrough.md`,
  остання збірка — v0.13.186, Фаза 8).
- Дві регресії, які виникали під час роботи, були помічені та виправлені
  окремими коммітами: `3192dbf` (втрачені хелпери після екстракції cheats) та
  `6212c27` (зламана логіка delete у paste-cut гілці filebrowser). Зараз слідів
  подібних втрат не знайдено.

### I.2 Перевірка move-only цілісності останніх коммітів

Порівняння мультимножин видалених/доданих рядків у двох останніх коммітах
(вони, ймовірно, ще не проходили рев'ю):

- `6fee90f` (app → app_settings/app_theme): єдина зміна поза переносом —
  `DEFAULT_MUSIC_PATH` промоутнуто до `extern const` (описано в повідомленні
  комміту, легітимно).
- `3973de5` (devoptab split): два **безпечні** перейменування локальних змінних
  у перенесеному коді: `bytes_read` → `read_bytes` (локальна змінна тінювала
  (shadowing) out-параметр `u64* bytes_read` — виправлення тіні, поведінка
  ідентична) та `curl` → `curl_handle` у `devoptab_curl_device.cpp`. Логіка
  обох `Read`-циклів (`BufferedData::Read`, `LruBufferedData::Read`) збережена
  дослівно — звірено рядок у рядок.

**Висновок:** відхилення від move-only мінімальні й поведінково нейтральні.
Рев'юеру достатньо разово прогнати `git diff --color-moved=dimmed-zebra` по
`6fee90f` і `3973de5`, знаючи про ці два перейменування.

### I.3 Розміри файлів проти цілей плану

| Фаза | Файл | Ціль | Зараз | Статус |
|---|---|---|---|---|
| 1 | `cheats_menu.cpp` | <1500 | 1112 | ✅ |
| 2 | `settings_menu.cpp` | <1800 | **2604** | ❌ (лишився fan-curve редактор) |
| 3 | `web.cpp` | — | 1739 | ⚠️ є очевидні шви |
| 4 | `save_menu.cpp` | <1800 | **2064** | ❌ (лишився backup/restore engine) |
| 5 | `kefir_menu.cpp` | — | 902 | ✅ |
| 6 | `filebrowser.cpp` | — | 1337 | ✅ |
| 7 | `app.cpp` | <1200 | **1346** | ⚠️ трохи вище цілі |
| 8 | `devoptab_common.cpp` | — | 745 | ✅ |

Побічний ефект Фази 1: новий файл `cheats/cheat_game_select_menu.cpp` має
**2169 рядків** — він тепер другий за розміром у проєкті і сам потребує поділу.

### I.4 Знайдені дублікати (наслідок екстракцій + історичний копіпаст)

1. **`HoldConfirmBox` існує двічі.** Спільний `ui/hold_confirm_box.{hpp,cpp}`
   (створений у кроці 5.3, використовується `kefir_menu.cpp:959`) і локальна
   майже ідентична копія в `settings_menu.cpp:106–199`. Єдина відмінність —
   локальна версія приймає параметр `hold_seconds`, спільна має фіксовану
   тривалість.
2. **`HashStr` + `hexIdToStr` визначені 5 разів.** Канонічна типізована версія —
   `utils/utils.{hpp,cpp}` (overload'и для `FsRightsId`, `NcmRightsId`,
   `NcmContentId`; `nca.cpp` вже викликає `utils::hexIdToStr`). Локальні
   шаблонні клони: `yati/yati.cpp:393`, `ui/menus/save_menu.cpp:254`,
   `ui/menus/gc_menu.cpp:217`, `ui/menus/game_menu.cpp:225`.
3. **`Trim` визначено двічі** у нових detail-неймспейсах:
   `settings/settings_fs_utils.cpp:16` і `kefir/kefir_firmware.cpp:23`
   (+ `TrimAsciiWhitespace` там само).
4. **`settings_fs_utils` дублює `fs.cpp`/`fs.hpp`:** `FileExists`,
   `DirectoryExists` та інші існують і як `fs::FileExists` тощо. Це відома
   відкладена задача з кроку 2.1 старого плану («Do NOT try to deduplicate…
   separate future task») — час її запланувати.
5. **URL/HTML-декодування двічі:** `web.cpp` (`UrlDecode`, `HtmlEscape`) та
   `devoptab_curl_device.cpp` (`url_decode`, `html_decode`). Низький пріоритет.

### I.5 Статус AUDIT_PLAN.md (web-підсистема)

Усі P0/P1 позначені RESOLVED і підтверджуються кодом: у `web.cpp` тепер є
`IDLE_TIMEOUT_MS`, `g_web_pbox` — `std::atomic`, воркер-пул
(`SHARE_WORKER_COUNT = 3`, тобто P2-7 теж закритий), `HTTP_READ_LIMIT`
піднято до 16384 (P2-9 частково закритий). P2-8 закрито Фазою 3 (web_pages.hpp).
**Залишилось відкритим тільки P2-6b** — налаштування стратегії вибору цілі
інсталяції (Auto NAND-first / Always SD / Always NAND). Перенесено в беклог
цього плану (це фіча, не рефакторинг).

---

## Частина II — План наступних робіт (Фази 9–14)

### Ground rules (успадковані з REFACTOR_PLAN.md, діють без змін)

1. **Move-only** для фаз 9–13: різати і переносити, не перейменовувати, не
   «покращувати», не виправляти баги мовчки. Фаза 14 — виняток (дедуплікація,
   там зміни по суті, але без зміни поведінки).
2. Один крок = один комміт, формат `refactor(<area>): extract <what> into <file>`.
3. Кожен новий `.cpp` — у `sphaira/CMakeLists.txt`; кожен новий header —
   `#pragma once`. Збірку виконує Агент 1 / користувач; самому не збирати і
   не питати про збірку.
4. Анонімні хелпери, потрібні кільком TU, — у named `detail` namespace нового
   спільного header/cpp; хелпери одного TU лишаються в anonymous namespace.
5. Після кожної фази — STOP і чекати рев'ю.
6. Номери рядків нижче — підказки зі стану HEAD `3973de5`; довіряти іменам
   символів, не номерам.
7. Після кожного кроку оновлювати `walkthrough.md` (як і раніше) та відмічати
   `[x]` у цьому файлі.

---

### Фаза 9 — `ui/menus/settings_menu.cpp` (2604 → ціль ~1400)

Найбільший виграш: весь редактор кривої вентилятора живе тут одним блоком.

| Крок | Новий файл | Що переносити |
|---|---|---|
| [x] 9.1 | `ui/menus/settings/settings_fancurve.hpp/.cpp` | Runtime-частина: `EvaluateFanPercent` (~791), `FanCurveSensorSample` (~812), `FanCurveSensorReader` (~818), `SphairaFanState` (~830). УВАГА: перевірити, чи ці типи використовуються також пунктами SettingsMenu (прев'ю кривої) — якщо так, вони йдуть у `settings::detail` спільного header'а; якщо тільки FanCurveMenu — в anonymous namespace нового .cpp. |
| [x] 9.2 | той самий `settings_fancurve.cpp` | Малювальні хелпери fan-curve (~1431–1810): `FanCurveProfileLabel`, `FanCurveGraphRect`, `FanCurvePlotRect`, `FanCurveListRect`, `FanCurveListItemRect`, `FanCurveXForTemp*`, `FanCurveYForFan`, `FanCurveTempForX`, `FanCurveFanForY`, `ExpandRect`, `WithAlpha`, `FormatMilliC`, `DrawHorizontalDashes`, `DrawVerticalDashes`, `DrawFanCurveSensorMarker`, `DrawFanCurveGraph`, `DrawFanCurveListItem`, `DrawFanCurveListHeader`. Хелпер `DrawActionListItem` (~1821) — перевірити: якщо він для списку налаштувань, а не fan-curve, він ЛИШАЄТЬСЯ в settings_menu.cpp. |
| [x] 9.3 | той самий | Клас `FanCurveMenu` цілком: оголошення переїжджає з `include/ui/menus/settings_menu.hpp` (~рядки 140–180) до `include/ui/menus/settings/settings_fancurve.hpp`; усі визначення методів `FanCurveMenu::*` (~1873–2500) — у новий .cpp. `settings_menu.cpp`/`.hpp` підключають новий header там, де FanCurveMenu пушиться. |
| [x] 9.4 | leftover check | `settings_menu.cpp` має лишити: `SettingsMenu`, побудову списків пунктів (`Build*Items`, `Make*`), `HoldConfirmBox` (до Фази 14.1). Ціль: ~1300–1400 рядків. |

### Фаза 10 — `ui/menus/save_menu.cpp` (2064 → ціль ~1100)

Патерн «другий TU того самого класу», як уже зроблено для
`filebrowser_ops.cpp`. Розділяємо UI-навігацію та backup/restore-двигун.

| Крок | Новий файл | Що переносити |
|---|---|---|
| [x] 10.1 | `ui/menus/save/save_menu_ops.cpp` | Тіла методів `Menu::` (клас НЕ чіпати, оголошення лишаються в `save_menu.hpp`): `BackupSaves` (усі 3 перевантаження, ~1376–1460), `CollectBackups` (~1461), `FindLatestBackupPath` (~1527), `RestoreSaves` (обидва, ~1537–1584), `StartRestore` (~1585), `ShowRestorePicker` (~1646), `ShowRestorePickerPopup` (~1669), `RestoreSavesPicked` (~1722), `DownloadRemoteBackupsForEntry` (~1781), `BuildSavePath` (~1840), `RestoreSaveInternal` (~1868), `BackupSaveInternal` (~2005), `SyncSavesRemote` (~2238), `SyncSavesRemoteWithLocation` (~2269). Плюс anon-хелпер `DownloadOneBackupFile` (~1750) — він використовується тільки цим блоком, переїжджає в anonymous namespace нового TU. |
| [x] 10.2 | leftover check | У `save_menu.cpp` лишаються: сканування entries, сортування, `Update`/`Draw`/`SetIndex`, Display*-меню, `PromptSaveAction`/`PromptSaveTypeOptions`. Перевірити, які anon-хелпери початку файлу (`GetFsSaveAttr`, `LoadControlEntry`, `hexIdToStr`, …) потрібні обом TU — такі перенести в `save::detail` (наприклад, у вже наявний `save/save_paths.hpp` НЕ пхати; створити мінімальний `save/save_menu_detail.hpp` тільки якщо реально треба). Ціль: ~1100 рядків. |

### Фаза 11 — `ui/menus/cheats/cheat_game_select_menu.cpp` (2169 → два файли)

Файл містить два класи; `CheatDownloadMenu` — це ~2/3 файлу.

| Крок | Новий файл | Що переносити |
|---|---|---|
| [x] 11.1 | `ui/menus/cheats/cheat_download_menu.hpp/.cpp` | Клас `CheatDownloadMenu` цілком: оголошення з `cheat_game_select_menu.hpp` + всі визначення (~690–2169): `Update`, `Draw`, `OnFocusGained`, `SetIndex`, `RunBuildIdDiagnostics`, `FetchCheats`, `FetchCheatsFromNxDb`, `FetchKefirBuildIdFromVersionMap`, `FetchCheatsFileAndExtractBuildIds`, `FetchKefirCheatsFromGithub`, `FetchNxDbCheatsFromGithub`, `CacheNxDbCheatFile`, `FetchCheatsFromApi`, `DownloadCheats`, `DeleteCheat`, `ShowExistingCheats`, `PreviewCheat`. |
| [x] 11.2 | розбір anon-хелперів | Хелпери початку файлу (~49–275, напр. `LoadGameControlImage`) розкласти: використовується одним класом → anonymous namespace його TU; обома → `hats::detail` (у `cheats_db.hpp` або новий маленький header). |
| [x] 11.3 | leftover check | `cheat_game_select_menu.cpp` лишає тільки `CheatGameSelectMenu` (+ `ScanGames`). Очікувано ~700 рядків; `cheat_download_menu.cpp` ~1400. |

### Фаза 12 — `web.cpp` (1739 → ціль ~1000)

| Крок | Новий файл | Що переносити |
|---|---|---|
| [ ] 12.1 | `source/web_http.hpp/.cpp` | HTTP/строкові примітиви з anon namespace (~68–390 + 1049): `PathExtension`, `ExtensionEquals`, `ContentTypeForPath`, `HtmlEscape`, `UrlEncode`, `HexValue`, `UrlDecode`, `GetQueryValue`, `SplitPathAndQuery`, `CanonicalizeAbsolutePath`, `SanitizeFileName`, `SendAll`, `SendString`, `SendResponse`, `ReadHttpRequest`, `HeaderValue`, `JsonEscape`. Namespace `sphaira::web::detail`. Константи `HTTP_READ_LIMIT`, `HTTP_FILE_CHUNK`, `IDLE_TIMEOUT_MS` — туди ж, якщо їх потребують обидва TU. Перевірити, чи `web_upload.cpp` теж потребує щось із цього (тоді header вже готовий для нього). |
| [ ] 12.2 | `source/web_screenshots.hpp/.cpp` | Галерея скріншотів (~1184–1600): `ScreenshotEntry`, `TryParseScreenshotEntry`, `CompareScreenshotEntries`, `ScanScreenshots`, `IsValidAlbumPath`, `ScanFolders`, `ScanFolderFiles`, `BuildScreenshotGalleryPage`. Експортувати мінімум — ймовірно, достатньо однієї функції `BuildScreenshotGalleryPage(query)`. |
| [ ] 12.3 | leftover check | `web.cpp` лишає: роутинг (`HandleRequest`, `Handle*`), сервер (`ShareThreadFunc`, `StartShareServer`, `TuneShareSocket`), публічний API (`WebShow`, `WebShareFolder`, `WebShareStop`, …), `ReceiveUpload`. Ціль: ~1000–1100 рядків. |

### Фаза 13 (опційна) — `app.cpp` (1346 → ~900)

Ціль <1200 з Фази 7 майже досягнута; робити тільки якщо рев'юер вважає за
доцільне після фаз 9–12.

| Крок | Новий файл | Що переносити |
|---|---|---|
| [ ] 13.1 | `source/app_display_options.cpp` | Другий TU класу `App`: `DisplayThemeOptions` (~1015), `DisplayNetworkOptions` (~1057), `DisplayMiscOptions` (~1061), `DisplayAdvancedOptions` (~1112), `DisplayInstallOptions` (~1212), `DisplayDumpOptions` (~1319), `ShowEnableInstallPrompt` (~1350), `ShowEnableInstallPromptOption` (~1369). |

### Фаза 14 — Дедуплікація (НЕ move-only; поведінка не повинна змінитись)

Виконувати ПІСЛЯ фаз 9–12, коли файли «всядуться». Один крок = один комміт.

| Крок | Задача | Деталі |
|---|---|---|
| [ ] 14.1 | Один `HoldConfirmBox` | Розширити `ui/hold_confirm_box.{hpp,cpp}` параметром `float hold_seconds` з дефолтом, що дорівнює поточній фіксованій тривалості спільної версії (звірити з .cpp — щоб виклик у `kefir_menu.cpp:959` не змінив поведінку). Видалити локальний клас із `settings_menu.cpp:106–199`, переключити 4 call-site'и (`settings_menu.cpp:270, 338, 652, 2492`) на `sphaira::ui::HoldConfirmBox`, зберігши їхні поточні тривалості. Звірити візуальні відмінності Draw двох версій (шрифти/розкладка ледь відрізняються) — якщо відрізняються, взяти за основу версію з settings (вона новіша й адаптивна), і перевірити, що kefir-екран виглядає прийнятно. |
| [ ] 14.2 | Один `hexIdToStr` | Замінити локальні `HashStr`/`hexIdToStr` у `yati/yati.cpp:393`, `save_menu.cpp:254`, `gc_menu.cpp:217`, `game_menu.cpp:225` на `utils::hexIdToStr` (`utils/utils.hpp`). ПЕРЕД заміною перевірити фактичні типи аргументів у кожному call-site: overload'и utils покривають `FsRightsId`/`NcmRightsId`/`NcmContentId`; якщо десь передається інший тип (напр. `u64` title id) — додати відповідний overload в utils, НЕ шаблон. |
| [ ] 14.3 | Один `Trim` | Порівняти тіла `settings::detail::Trim` і `kefir::detail::Trim`/`TrimAsciiWhitespace` (семантика може відрізнятись — які символи тримаються). Якщо ідентичні — винести в один спільний хелпер (пропозиція: `utils/utils.hpp`, `namespace utils`), обидва detail-неймспейси переключити. Якщо різні — залишити, додати коментар-пояснення відмінності. |
| [ ] 14.4 | `settings_fs_utils` vs `fs.cpp` | Відкладена задача з кроку 2.1 старого плану. Для кожного хелпера з `settings/settings_fs_utils.cpp` знайти еквівалент у `fs.hpp`/`fs.cpp` (`fs::FileExists`, `fs::DirExists`, копіювання, видалення, move). Мапу «хелпер → еквівалент/нема еквівалента» спершу показати рев'юеру, потім заміняти по одній групі за комміт. Хелпери без еквівалента (`ExtractIniKey`, `SplitCommand`, `ExtractJsonStringField`, …) лишаються. Це найризикованіший крок фази — семантика (робота через stdio vs FsNative, обробка помилок) може відрізнятись; при сумніві НЕ заміняти, а зафіксувати відмінність у коментарі. |
| [ ] 14.5 | (опційно) URL/HTML decode | `web.cpp` `UrlDecode`/`HtmlEscape` vs `devoptab_curl_device.cpp` `url_decode`/`html_decode`. Порівняти семантику; об'єднувати тільки якщо ідентичні та якщо після Фази 12 з'явився природний спільний header (`web_http.hpp` не годиться для devoptab — тоді пропустити). Низький пріоритет. |

---

## Частина III — Беклог (за межами цього плану)

Переоцінені кандидати (розміри на 2026-07-13):

- `yati/yati.cpp` — **1400** (у старому плані значився як 1700; уже схуд).
  Досі найризикованіший, потребує окремого плану.
- `ui/menus/appstore.cpp` — 1244.
- `ui/menus/themezer.cpp` — 1218.
- `download.cpp` — 1070.
- **P2-6b з AUDIT_PLAN.md** (фіча, не рефакторинг): налаштування стратегії
  цілі інсталяції (Auto NAND-first / Always SD / Always NAND) поверх хелпера
  `ChooseInstallTarget` з P2-6.

---

## Частина IV — Чекліст перевірки кожного кроку (для рев'юера)

- `git diff --stat`: тільки перенесені рядки, CMakeLists, include'и.
- `git diff --color-moved=dimmed-zebra`: перенесений код ідентичний
  (для Фази 14 — замість цього перевірка поведінкової еквівалентності).
- Жоден символ не визначений двічі; anon-namespace функції не викликаються
  з інших TU.
- Збірка проходить (Агент 1 / користувач).
- Рекомендований порядок: 9 → 10 → 11 → 12 → 14 → (13 за рішенням рев'юера).
