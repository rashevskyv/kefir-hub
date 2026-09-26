# audit.md

Версія коду: **v0.13.892**. Дата аудиту: 2026-09-26.

## Поточний стан

- v0.13.892 виправила невідповідність перевантаження `PromptBatchRestoreTargets`, через яку надана збірка v0.13.891 зупинилась. Повторну збірку й Switch runtime ще не виконано.
- v0.13.891 спростила дії Backups: B зі списку користувачів після A → Restore повертає до меню дій; дубль Restore for user прибрано; у + → ACTIONS залишено Restore. Шість цільових Python контрактів і diff check пройшли, NRO та Switch runtime не перевірені.
- v0.13.890 виправила єдину помилку компіляції з наданого логу: заголовок плитки HB Menu більше не звертається до приватного `grid::Menu::m_scroll_name`. Збірку та Switch runtime після правки не запускали.
- v0.13.889 підняла назви HB Menu над послабленим Inner Glow та додала асинхронний fallback іконок за Title ID з перевіркою `ImageLoadIcon`, SD і session кешем. Цільові Python контракти й diff check пройшли за звітом Gemini; NRO та Switch runtime ще не перевірені.
- v0.13.888 прибрала дубль «Іконки» з перемикача макетів Saves; три інші пункти залишено з правильним відображенням на збережені LayoutType. Два цільові Python контракти пройшли; NRO та Switch runtime ще не перевірені.
- v0.13.887 виправила перехід з першого бекапа до останнього через UP/LEFT у макетах зі службовою позицією перед списком. Два цільові Python контракти пройшли; NRO та Switch runtime ще не перевірені.
- v0.13.886 прибрала повторний запит про створення слота при відновленні бекапа; слот створює наявний перевірений restore шлях. Два цільові Python контракти пройшли; NRO та Switch runtime ще не перевірені.
- v0.13.885 прибрала цілий порожній ряд перед наступними джерелами плиткового Backups; залишено 34 px під підпис секції. Два Python контракти пройшли; NRO та Switch runtime ще не перевірені.
- v0.13.884 вирівняла відступи між підписами джерел Backups і плитками та порядок малювання хмаринки над лінією. Два цільові Python контракти пройшли; збірка та runtime на Switch ще не перевірені.
- v0.13.883 прибрала порожній перший ряд у плитковому вигляді Backups і перенесла перший підпис джерела під вкладки. Контрактний тест секцій пройшов; збірка та консольний runtime ще не перевірені.
- v0.13.882 розділяє бекапи за походженням, показує й відновлює папки JKSV/Checkpoint та перевіряє metadata перед створенням слота. WSL `ReleaseWithInstall` завершився `sphaira_nro` після двох компіляційних виправлень; цільові контракти й EN/UK parity пройдено. Runtime на Switch ще не перевірено.
- v0.13.881 додала перевірений archive-metadata fallback для створення Account save slot невстановленої гри. Код увійшов до успішної збірки v0.13.882; консольне відновлення ще не перевірене.
- v0.13.880 виправила втрату `payload_count` для DBI ZIP із метаданими; на SD є 21 архів із payload для перевірки на консолі. Ще 9 metadata-only архівів відсікає чинна safety policy. Build/runtime не запускали.
- v0.13.879 закрила залишковий file-size gap: 25 oversized targets розділено, 53 нові файли; усі 620 перевірених first-party code/build/test файлів у `sphaira`, `hbl`, `tests` ≤600 рядків. `sphaira/CMakeLists.txt` тепер 461.
- Gemini повідомив про WSL host suite (`test_save_restore_contract` 40/40), `sphaira_nro`, dead-symbol 1033/1033 і whitespace PASS; senior перевірив diff/структуру, але не повторював збірку. Консольні save/restore і MTP flows лишаються для ручного тесту.

- v0.13.878 закрила A5–A7: Yati, provider/menu UI та Python contracts розкладені; 150 змінених/нових source/test файлів і всі 152 menu source/header файли ≤600.
- v0.13.877 розклала `threaded_file_transfer.cpp` з 1 956 до 263 рядків; усі нові transfer files ≤501 рядка.
- Transfer core, archive preflight, native verification і ZIP I/O мають окремі concrete units; public header незмінний.
- Gemini: WSL `ReleaseWithInstall` build, 18/18 Python contracts і dead-symbol gate PASS; runtime на консолі не перевірено.
- `app.hpp` зменшено до 590 рядків після перевірки невикористаних declarations; `GetAccountList` перенесено без зміни алгоритму.
- Save build-closure units лишилися 591/597 рядків.
- У `sphaira/source/ui/menus` більше немає `.cpp/.hpp` понад 600. Поза перевіреною областю лишаються табличні дані/згенеровані артефакти; ліміт не накладати на них механічно.
- Graphify є картою зв’язків; source-level перевірка потрібна для кожного нового delivery.
- Активні delivery-документи скорочено; попередня історія лишається в Git без потрійного дублювання.

## Черга — виконувати серійно

### A8 — remaining first-party file-size audit (`v0.13.879`) — DONE

Account/NAND, filesystem і transport adapters, Haze save scan, title/owo, UI, HBL, CMake та path-util test split завершені. Наступний крок — ручний device/runtime тест save/restore та MTP, не новий structural split.

### A1 — dead-code cleanup (`v0.13.871`) — DONE

1. `App::DisplayAdvancedOptions` не має caller-а. Разом із ним мертві `GetMiscMenuEntries`, `MISC_MENU_ENTRIES` і generator.
2. `stream::Menu` не має subclasses/instances. `SetActiveMenu` не має caller-а; `s_active_menu` branches обслуговують лише цей шлях.
3. `yati::source::StreamFile` не інстанціюється.
4. `App::GetWebdavUrl/User/Pass` мають лише declaration + definition; активні callers використовують `GetWebdavUrlName` та `location::Entry`.
5. Фактичний результат: no new dependencies/abstractions, full build PASS.

### A2 — Saves structural split — DONE

`save_paths.cpp` split завершено у v0.13.872; `save_menu.cpp` — у v0.13.873; `save_menu_ops.cpp` — у v0.13.874.

- Backup, ZIP/folder restore, remote sync і deletion винесені зі стабільним `Menu` API та safety ordering.

Не вводити service interfaces/factories. Existing `Menu`/free-function APIs спочатку лишити стабільними.

### A3 — Web — DONE

У v0.13.875 `web.cpp` 538 і `web_pages.hpp` 5 рядків; existing constexpr templates розкладено за сторінками без template engine. Full WSL build і 18/18 contracts PASS за звітом Gemini.

### A4 — App — DONE

У v0.13.876 `app.cpp` 523 і `app_settings.cpp` 554 рядки; public API і shutdown ordering збережені. Gemini повідомив про WSL build та 18/18 contracts PASS. `app.hpp` 626 — окремий борг: `LaunchType` і `m_pop_count` виглядають невикористаними, але member layout та ефекти option-полів треба перевірити перед видаленням; не стискати header механічно.

### A5 — Transfers and installer — DONE

- Transfers — DONE у v0.13.877: `threaded_file_transfer.cpp` 263; core, preflight, verification і ZIP I/O розділено без нових абстракцій.
- `yati.cpp` 450; analysis, threaded pipeline та ticket/CNMT metadata — окремі concrete units ≤512.

### A6 — Provider-heavy UI — DONE

API/parsing/storage відокремлено від UI в `cheat_download_menu.cpp`, `appstore.cpp`, `kefir_menu.cpp`, `download.cpp`, `themezer.cpp`, `cheats_menu.cpp`, `settings_fancurve.cpp`.

Також 32 великі menu targets розкладено за конкретними responsibilities; всі menu source/header files ≤600.

### A7 — Tests/tooling — DONE

Початково 18 Python contract scripts мали 17 483 рядки, 11 були понад 600, а `tests/run.sh` їх не запускав. Suite розділено за scenario boundaries без додаткового framework.

Тепер runner автоматично запускає всі 18 Python contracts, а великі scenario fixtures рознесені до `tests/contract_fixtures/`; senior повторно отримав 18/18 PASS і 1033/1033 dead-symbol declarations. Gemini повідомив про host suite і WSL build PASS. C++ contract після точкового звуження source boundary senior не компілював повторно.

## Не чіпати механічно

- i18n JSON — табличні дані, line cap не застосовувати.
- Platform adapters і CMake після цього delivery не ділити знову без конкретного maintenance outcome.
- Не мінімізувати embedded HTML заради line count.
- Не змішувати behavior change з broad file split.
- Не створювати interfaces з однією реалізацією, registries або speculative config.
- Не запускати compile/build до фінального Gemini follow-up наступного delivery.

## Agent routing

- Save UI/state: `source/ui/menus/save_menu*.cpp`
- Save wire/discovery/mutation: `source/ui/menus/save/`
- Web HTTP/UI: `source/web*.cpp`, `source/web_pages.hpp`
- App lifecycle/settings: `source/app*.cpp`, `include/app.hpp`
- Transfer core/archive: `source/threaded_file_transfer*.cpp`, `include/threaded_file_transfer.hpp`
- Installer: `source/yati/`, `include/yati/`
- MTP filesystem: `source/utils/devoptab_mtp*.cpp`
- Active work: `plan.md`, `task.md`; shipped outcome: `walkthrough.md`; next cut: this file.

Історичні accepted-delivery докази доступні через Git і не є активною чергою.
