# Аудит upstream Sphaira після 1.0.2

Останнє оновлення: **2026-08-20**. Це канонічний живий звіт для послідовного
перенесення корисної поведінки з upstream
[`NaGaa95/sphaira`](https://github.com/NaGaa95/sphaira) у локальну Sphaira.

> Головне правило: обираємо не новішу реалізацію, а кращу — надійнішу,
> простішу, сумісну з локальною архітектурою та з ширшим фактичним покриттям.
> Прямий cherry-pick не є метою.

> Політика локалізації: upstream translation/i18n diffs не переносимо.
> Нові локальні функціональні ключі, якщо вони знадобляться, проходять наш
> окремий translation pipeline.

Виконуваний порядок робіт, залежності, acceptance matrix і правила для
junior-виконавця винесено в
[`upstream_implementation_plan.md`](upstream_implementation_plan.md). Цей файл
лишається джерелом фактів і порівняння; companion plan відповідає на питання
«що, у якому порядку і як саме робити».

## 1. Перевірений знімок

| Поле | Значення |
|---|---|
| Базовий upstream-коміт | [`eeac5ff8`](https://github.com/NaGaa95/sphaira/commit/eeac5ff8fcffaf57d88b91d05d704e6fb0c75dba), upstream `1.0.2` |
| HEAD попередньої редакції звіту | [`11806729`](https://github.com/NaGaa95/sphaira/commit/1180672984021559718f339255bdbe7df71b0f64), upstream `1.0.6`, 2026-08-15 |
| Актуальний upstream HEAD | [`eacb54b3`](https://github.com/NaGaa95/sphaira/commit/eacb54b35548ff99057744bd94f56d67c3449fed), 2026-08-17 |
| Повний upstream-діапазон | [48 комітів після `1.0.2`](https://github.com/NaGaa95/sphaira/compare/eeac5ff8fcffaf57d88b91d05d704e6fb0c75dba...eacb54b35548ff99057744bd94f56d67c3449fed) |
| Поточний локальний HEAD | `f7b10f3e406d3c427c563eb1b58cac24dc61ba8d`, `master` |
| Поточна локальна версія | `0.13.514` |
| Checkout | `D:\git\dev\sphaira`, primary checkout |
| Стан перед створенням звіту | чистий `master`; аудит був read-only до зміни цього Markdown-файлу |

GitHub compare API підтвердив `ahead_by = 48` і merge base, що точно дорівнює
`eeac5ff8`. Локальний remote ref для цього fork застарілий, тому джерелом
upstream-інвентарю був канонічний GitHub repository/API, а локальний код
перевірявся без fetch, worktree чи зміни гілки.

Із 48 комітів:

- 32 містять окрему функціональну, bugfix або змішану поведінку;
- 9 є translation/docs/version-only і не створюють функціональної задачі;
- 7 є merge-only та не додають поведінки понад уже враховані parent-коміти.

## 2. Як читати статуси

| Статус | Значення |
|---|---|
| `DONE-LOCAL` | Локальна реалізація повністю покриває upstream і є кращою або не гіршою |
| `DONE-SW / HW-PENDING` | Код і host/build verification є, але потрібне приймання на реальній Switch |
| `PARTIAL` | Є спільна база, але частина корисної поведінки відсутня |
| `MISSING-ENG` | Підтверджена технічна прогалина або дефект; продуктового рішення не потребує |
| `PRODUCT` | Відсутня або відмінна продуктова функція; реалізовувати лише окремою великою задачею |
| `EXCLUDED` | Переклад, branding/docs, version bump або merge-only |

Hardware acceptance не змішується з software implementation: наприклад, NFS
не є «відсутнім», але його реальний smoke-test усе ще відкритий.

## 3. Підсумок стану

| Категорія | Актуальний результат |
|---|---|
| Локально повністю покрито або перевершено | Publisher sort; Atmosphère title labels; Applet indicator; NACP layout adapter; adaptive title scaling/marquee; NRO icon hardening/default icon; custom NRO paths; required-HOS lowering; shared ZIP path/size hardening; PFS0/NSP hardening; versioned HTTP User-Agent; ширший Game Details/Content UI |
| Software complete, hardware pending | Read-only NFS; loader main-thread affinity restoration |
| Підтверджені невиконані engineering fixes | GitHub downloader lifetime/cancellation/destination; zero-byte MTP + responder version; playtime UI data race; forwarder touch focus; cross-source Homebrew mutation completion; English/export-name fallback; GameCard storage-bar theme/ratio guards |
| Часткові robustness gaps | Direct ZIP HTTP(S)/free-space preflight; custom repository normalization/validation; кілька ROM aliases |
| Повністю відсутні продуктові функції | Update/DLC catalog + filter; MTP folder installation; MSP installer; play-statistics disable toggle; керована 3/4-core модель |
| Локальна система краща, але coverage можна розширити продуктово | Screensaver/backlight-off для решти довгих `ProgressBox` flows; автоматичний selected-title playtime worker |
| Навмисно не переносимо | Upstream translation diffs, release bumps, merge-only, upstream branding/docs |

Старий звіт був правильним як історичний зріз до локального `v0.13.467`, але
мав застарілі висновки: головний title уже має marquee, Game Content локально
не відсутній, blocker з dirty `forwarder_editor.cpp` зник, а NFS не можна
класифікувати як службову зміну.

## 4. Нові upstream-коміти після попереднього звіту

Після `11806729` з'явилося 7 комітів; лише 3 несуть нову поведінку.

| Коміт | Upstream-зміна | Фактичний локальний стан | Рішення |
|---|---|---|---|
| [`d2e95b26`](https://github.com/NaGaa95/sphaira/commit/d2e95b262405b5fc32e6c4c6ad0852c9db68224b) | Zero-byte MTP upload, resize перед EOF, `Sphaira/<version> (HOS/<fw>)` у DeviceInfo | У pinned `libhaze@0be1523` resize уже відбувається до zero-read break. Але у нашому `patch_libhaze.cmake` умова досі `data_header.length > sizeof(PtpUsbBulkContainer)`, тож zero-byte transfer лишається dummy `4_GB`; DeviceInfo показує лише HOS | `MISSING-ENG`, best-of-both: додати тільки `>=` і version string у чинний idempotent patch pipeline. Не додавати другий raw patch і не переносити непотрібний buffer hunk |
| [`4392a66f`](https://github.com/NaGaa95/sphaira/commit/4392a66fe33340087bfb9f488975d4a682cde6a9) | Portuguese translation | Окремий translation pipeline | `EXCLUDED` |
| [`69542088`](https://github.com/NaGaa95/sphaira/commit/695420881734a9925facba90d2566188a31ee7d0) | Правильні NACP English slots `0/1`, English/current fallback, localized display name, UTF-8-safe truncation | Локальний NACP v2 mapping уже правильний і централізований, але export helper відсутній. NSP/Merged NSP одразу переходять до Title ID лише для буквально порожньої назви; MTP Games ASCII-санітизує display name і обрізає bytes напряму | `PARTIAL`, best-of-both: залишити `nacp_util`, додати shared usable/export-name helper; NSP лишити ASCII-safe, MTP display name — локалізованим із UTF-8-safe truncation і Title-ID suffix |
| [`a66eeb87`](https://github.com/NaGaa95/sphaira/commit/a66eeb87a30045993d200b03c917c9e1539f8a6d) | Merge PR #348 | Немає окремої поведінки понад `69542088` | `EXCLUDED` |
| [`e16c0e18`](https://github.com/NaGaa95/sphaira/commit/e16c0e18f9e1ec3e4c8155491412de132441f394) | Storage-bar track використовує `ThemeEntryID_PROGRESSBAR_BACKGROUND` | У `gc_menu.cpp` лишилися точні два проблемні `ThemeEntryID_BACKGROUND`. Локальний `MenuBase` уже інший і не має upstream-дефекту: neutral track, threshold fills, highlight/projection та `total > 0` guard | `MISSING-ENG`: змінити тільки GameCard bars; upstream `MenuBase` не переносити. Разом додати clamp/zero-total guard, бо `UpdateStorageSize()` result зараз ігнорується |
| [`aaf7d614`](https://github.com/NaGaa95/sphaira/commit/aaf7d614c2923d4ee876437fb72af19077dbbbe7) | Merge branch | Немає окремої поведінки | `EXCLUDED` |
| [`eacb54b3`](https://github.com/NaGaa95/sphaira/commit/eacb54b35548ff99057744bd94f56d67c3449fed) | Merge PR #352, актуальний HEAD | Немає окремої поведінки понад `e16c0e18` | `EXCLUDED` |

## 5. Рекомендована послідовна черга

Завдання виконуються лише по одному в чистому `master`. Якщо наступні задачі
торкаються того самого файла, друга стартує лише після завершення й commit першої.

### P0 — підтверджені correctness/lifecycle дефекти

1. **GitHub downloader correctness, без продуктового repo UI.**
   Виправити callback ownership у `ghdl.cpp`, прибрати global static release
   vector, перевіряти selection bounds/empty assets, не продовжувати install
   після cancel, визначати ZIP також за extension, давати безпечний
   `/switch/<asset-name>` fallback для non-ZIP та централізовано валідовувати
   GitHub/direct HTTP(S) URLs. Upstream value copies, `optional<AssetEntry>` і
   operation-owned `shared_ptr<vector<...>>` кращі; пряме копіювання repo UI не
   потрібне.

2. **Zero-byte MTP upload і responder version (`d2e95b26`).**
   Розширити існуючий shape-checked `patch_libhaze.cmake`; зберегти всі локальні
   progress, signed-size, storage-ID, MTP property та UTF conversion fixes.
   Перевірити configure/build, MTP upload порожнього файла і DeviceInfo на
   реальній Switch.

3. **Playtime bulk-refresh data race (`ec854b0` overlap).**
   `ProgressBox` callback працює на worker thread, але локальний `LoadPlaytime()`
   напряму змінює `m_entries`, який UI може читати під modal box. Мінімальне
   рішення: worker формує окремий result buffer, а done callback застосовує його
   до `m_entries` на UI thread. Повний upstream selected-title worker не потрібен
   для виправлення цього дефекту.

4. **Спільний completion path для Homebrew catalog mutations (`af4c64c`).**
   Один локальний predicate/notifier має визначати успішну create/delete/rename/
   move операцію `.nro` у `/switch` або configured custom Homebrew roots.
   Transport adapters викликають його тільки після успіху: raw FTP, Web upload/
   delete та MTP upload/delete/rename. NFS read-only нічого не сигналізує.
   Upstream FTP callback — потрібний adapter, але не повне рішення.

### P1 — малі незалежні виправлення

5. **Forwarder editor touch/controller focus (`23f3ca6`, `1476905`).**
   Дозволити touch правого list, коли `m_icon_focused == true`; controller RIGHT
   лише переводить фокус і завершує поточну controller-подію; touch activation
   очищує icon focus. Зберегти локальний picker/crop UI.

6. **Shared English/usable export-name helper (`db06528`, `69542088`).**
   English slots — тільки NACP `0/1` через `nacp_util`; рядок лише з `_`, пробілів
   або крапок є непридатним. Застосувати до звичайного/merged NSP та MTP Games,
   не додаючи Unicode toggle без продуктової потреби.

7. **GameCard storage bars (`e16c0e18`).**
   Два dedicated theme-role replacements плюс safe ratio. Не чіпати багатшу
   локальну реалізацію header bars.

8. **Direct ZIP completion (`69f362d`).**
   Після URL/lifecycle task додати destination-aware free-space preflight у
   SD-specific caller. Не переносити його в generic `TransferUnzipAll()`, який
   також працює з non-SD filesystems та overwrite scenarios.

9. **ROM database aliases із `0882439`.**
   Додати `SuperGrafx`, `Family Computer Disk` alias і base `SNK - Neo Geo`,
   якщо ці каталоги реально потрібні. Це low-risk compatibility data, не нова
   архітектура.

### Acceptance, не нова реалізація

10. **NFS hardware acceptance:** browse/open/read/copy-from-NFS для nested export
    і відсутність write actions (`HW-NFS-449`).

11. **HBL affinity hardware acceptance:** первинний та повторний NRO launch,
    включно з `envSetNextLoad()` (`HW-HBL-AFFINITY-452`).

### PRODUCT — тільки окремими великими задачами

12. MTP folder installation.
13. Update/DLC catalog checker та missing/stale filter.
14. Керована 3/4-core forwarder model.
15. MSP package installer.
16. Play-statistics privacy/disable toggle та, за потреби, selected-title worker.
17. Custom repository add/remove UI.
18. Розширення локального Screensaver на всі довгі `ProgressBox` flows.

## 6. Детальне порівняння реалізацій

### 6.1 Локальна реалізація краща — нічого не переносити

| Область / upstream | Локальний доказ | Чому локальна краща |
|---|---|---|
| Shared ZIP / direct-link archive safety (`69f362d`) | `thread::TransferUnzipAll()`, `path::IsSafeArchiveEntry()`, host tests; локальний `v0.13.447` | Entry-count, exact filename length, traversal, joined path, `s64` size overflow перевіряються централізовано до створення файлів для всіх callers, не лише GitHub menu |
| NFS parsing (`e00ac7c`) | `utils/devoptab_nfs.cpp`, `utils/nfs_url.hpp`, `tests/test_nfs_url.cpp`; `v0.13.449` | Read-only semantics, `nfs_parse_url_dir()`, URL/path limits, credentials/traversal rejection, RAII та host regression coverage; upstream мав вузький parser fix |
| NRO icon (`2e27148`, `5b0779d`) | `ImageNormalizeIcon()`/`ImageGetDefaultIcon()`, `v0.13.450` | Checked dimensions/pixels/allocations, downscale до `256×256`, одна policy для Homebrew/forwarder/SteamGridDB і default fallback |
| Custom NRO paths (`5b02f65`) | `nro.cpp`, `homebrew.cpp`, `v0.13.451` + build fix `v0.13.464` | `/switch` лишається обов'язковим root, normalized custom roots, depth 2 scan, dedup та empty-list guards |
| NACP layout (`d84d475`) | `include/nacp_util.hpp` та local libnxtc compatibility patch | Один compile-time adapter для old/new libnx layout кращий за точкові заміни; його треба лише доповнити export semantics |
| Required system version (`9e4c46a`) | Yati змінює value до запису, зберігає original для warning | Не потрібний post-install rewrite вже записаної meta DB; option та diagnostics збережені |
| PFS0/NSP parser lessons із MSP (`400c514`) | `yati/container/pfs0.hpp`, `nsp.cpp`, negative tests; `v0.13.453` | Exact reads, caps, checked arithmetic, container bounds, string/name validation у shared parser. Майбутній MSP має reuse його |
| HTTP User-Agent (`2eabcec`) | `APP_USER_AGENT`, downloader і curl mounts; `v0.13.467` | Одна константа покриває всі HTTP/WebDAV/FTP curl paths. MTP version є окремим протоколом і не скасовує цей результат |
| Loader affinity (`87c855a`) | `hbl/source/main.c`, `v0.13.452` | Читається фактична process mask і відновлюється безпосередньо перед trampoline; hard-coded mask не використовується |
| Publisher sort (`6f8a886`) | `SortType_Publisher`, author → name tie-break | Deterministic локальний comparator повністю покриває поведінку |
| Header marquee (`d9c3939`) | `MenuBase::DrawChrome`, `v0.13.493–0.13.498` | Adaptive scale до 60%, потім marquee, caller-selected header slots і occlusion handling; старий статус `PARTIAL` застарів |
| Atmosphère title labels (`16ada3e`) | `filebrowser.cpp`, локальний `v0.13.397` | Крім installed-title lookup є HATS/sysmodule labels та async resolution |
| Applet Mode (`fdcd5a1`) | `[A]` у shared header | Та сама інформація займає менше місця й відповідає локальному layout |
| Game content shortcut (`a78e92e`) | `DbiDetailsMenu`, Content/Tickets/Saves tabs | Локальний UI показує base/update/DLC, size/storage/rights і дає browse/dump/move; `Y` корисно лишається invert multi-selection |

NFS і loader affinity тут означають **software implementation complete**, а не
закритий hardware acceptance.

### 6.2 Best-of-both — upstream має поведінку, яку варто адаптувати

| Область | Сильна локальна частина | Сильна upstream частина | Остаточне рішення |
|---|---|---|---|
| GitHub/custom repositories (`2d8b9ba`) | Локальні JSON catalogs, fork paths, shared hardened unzip | Safe URL parsing/normalization, operation-owned release data, value-owned asset config, selection/empty guards, cancellation, safe non-ZIP destination | Негайно взяти correctness/lifecycle, але не UI add/remove. Persistence UI — окремий `PRODUCT` task |
| MTP zero-byte/version (`d2e95b26`) | Значно ширший idempotent libhaze patch pipeline; resize-before-zero вже є | Correct `>=` transfer size і versioned DeviceInfo | Додати два відсутні hunks у наш pipeline, без другого patch mechanism |
| NACP/export (`db06528`, `69542088`) | Central layout adapter, local ASCII-safe export policy, mandatory Title-ID suffix | Correct English slots, usable-name predicate, localized display/export separation, UTF-8 truncation | Shared local helper; ASCII NSP export, localized MTP display, deterministic Title-ID fallback |
| Homebrew mutations (`af4c64c`) | Деякі GitHub/AppStore/Web/MTP callers уже сигналять; custom roots існують | ftpsrv success callbacks для upload/delete | Shared local path policy + thin success adapters для кожного writable transport |
| Forwarder focus (`23f3ca6`, `1476905`) | Значно ширший icon picker/crop editor | Правильна touch/controller focus matrix | Взяти input behavior, не upstream UI |
| Screensaver (`a55b54e`) | Dim/BacklightOff/Screensaver, OLED/brightness restore, timeout, refcounted no-sleep | Ширше підключення до install/dump progress flows | Якщо погоджено продуктово — reuse локальний `Screensaver` у generic flows; не заводити другу LBL lifecycle систему |
| Playtime (`ec854b0`, `8c6e0e8`) | Explicit bulk refresh, progress, console-wide fallback, sort/cache | Worker-owned results, mutex/lifecycle та UI-thread apply для selected-title updates | Спочатку мінімально усунути data race через result buffer. Silent worker/toggle — лише після UX/privacy рішення |
| 3/4 cores (`204fdde`, `05279db`) | Простий NPDM permission і правильний final loader mask restore | Default-three + explicit warned four-core opt-in концептуально безпечніший | Не переносити relaunch/NCA patching частинами. Проєктувати окремо з rollback та hardware matrix |
| MTP folder install (`3f8303d`) | Багато локальних MTP extensions: progress totals, storage IDs, properties, Unicode, Saves RW | Case-insensitive path tree, directory operations, session reset, safe handle ownership | Upstream design кращий за local flat VFS, але його треба адаптувати до current API після zero-byte fix, не cherry-pick |
| GameCard bar (`e16c0e18`) | Багатший main-header storage UI | Dedicated theme role у старому GameCard UI | Взяти два theme changes і додати local ratio guard; main header не замінювати |

### 6.3 Часткова реалізація або окремий продукт

| Функція | Що є | Що відсутнє | Статус |
|---|---|---|---|
| Direct ZIP (`69f362d`) | Manual/JSON direct URL, shared safe unzip | Strict HTTP(S) validation для всіх entry paths та destination-aware free-space preflight | `PARTIAL`; correctness у P0/P1 |
| Custom repos UI (`2d8b9ba`) | File-based repos з `/config/kefir/github/` | Add/remove UI та transactional persistence | `PRODUCT`; correctness fixes не відкладати разом з UI |
| Async selected playtime (`ec854b0`) | Cached stats і manual bulk refresh | Silent per-selection worker | `PRODUCT`; data race — `MISSING-ENG` окремо |
| Play-stat toggle (`8c6e0e8`, merge `c19e5a3`) | Playtime sorts та PDM use | Setting, що вимикає PDM/cache UI | `PRODUCT` privacy/performance choice |
| Screen-off coverage (`a55b54e`) | Сильніший локальний Screensaver у DBI/install queue | Generic file/USB/stream/GameCard/dump `ProgressBox` coverage | `PRODUCT`, reuse existing mechanism |
| Update/DLC (`bc90664`, `ccd290e`) | Installed content details локально багаті | Remote `nx-versions` catalog, availability comparison, missing/stale filter | `PRODUCT`, повністю відсутнє |
| MSP (`400c514`) | Shared parser hardening перенесено | `.msp` manifest/staging/rollback/Atmosphère install | `PRODUCT`, повністю відсутнє |
| MTP folder install (`3f8303d`) | Flat Install drive та багато protocol fixes | Nested folders/path tree/directory ops/session reset | `PRODUCT`, повністю відсутнє |
| ROM aliases (`0882439`) | Частина databases уже є в assoc files | Три directory/database aliases у central map | `PARTIAL`, низький ризик/пріоритет |

## 7. Підтверджені локальні ризики, знайдені під час overlap review

Ці пункти важливіші за просте «є/немає», бо показують, де upstream реалізація
безпечніша за нашу поточну.

1. **Dangling references у GitHub asset callbacks.** У `ghdl.cpp` відкладений
   `func` захоплює `asset_entry` за посиланням, а `ptr` вказує всередину іншої
   closure-owned копії `entry.assets`. Після закінчення popup callback обидва
   можуть стати dangling. Upstream value copies усувають root cause.

2. **Global static releases state.** `static std::vector<GhApiEntry> gh_entries`
   спільний для всіх операцій і popup callbacks. Повторний/reentrant запуск може
   перезаписати активний список. Дані мають належати одній download operation.

3. **Cancel може використати старий temp-файл.** `DownloadApp()` пропускає curl,
   якщо `ShouldExit()` уже true, але все одно переходить до extract/rename. Після
   curl також немає cancellation gate.

4. **Non-ZIP без explicit path намагається встановитися у `/`.** Без `entry.path`
   `root_path` лишається `/`; безпечний default — перевірений
   `/switch/<asset-name>`.

5. **Playtime UI data race.** `ProgressBox` запускає callback в окремому thread,
   а локальний callback змінює `m_entries`, який належить menu/UI. Результати
   треба застосовувати у done callback після join.

6. **Zero-byte MTP лишає dummy transfer size.** Файл обнуляється, але `file_size`
   лишається `4_GB`, бо рівність container header size потрапляє в `else`.

7. **Forwarder icon focus блокує list touch.** Unconditional `return` при
   `m_icon_focused` не дає touch дійти до `List::OnUpdate()`.

8. **Homebrew refresh coverage неповне.** Web upload і redirected MTP upload
   частково покриті; Web delete, raw FTP і MTP delete/rename — ні. Простий
   suffix-only FTP callback також не розуміє custom roots.

9. **GameCard storage ratio не захищений.** `UpdateStorageSize()` result
   ігнорується, а Draw ділить на `m_size_total_*` без zero/range guard.

## 8. Hardware acceptance, що лишився відкритим

| Delivery | Software state | Потрібна ручна перевірка |
|---|---|---|
| NFS `v0.13.449` | Parser/source/host tests завершені | Browse, open, read, copy-from-NFS, nested export, відсутність write actions |
| HBL affinity `v0.13.452` | Реальна process mask відновлюється перед trampoline | Перший і повторний NRO launch, `envSetNextLoad()` |
| Майбутній MTP zero-byte fix | Ще не реалізовано | Windows/Android: створення й повторне копіювання 0-byte file; responder version |
| Майбутній Homebrew mutation path | Ще не реалізовано | FTP/Web/MTP upload, delete, rename; default і custom Homebrew roots |
| Майбутній forwarder focus fix | Ще не реалізовано | Touch list із focused icon, controller LEFT/RIGHT/A, scroll/tap matrix |
| Майбутній GC bar fix | Ще не реалізовано | Image-background theme, unavailable/zero storage query, normal theme |

Інші hardware gates поточного продукту ведуться у `task.md`; вони не змінюють
класифікацію upstream-функцій у цьому звіті.

## 9. Повний облік усіх 48 upstream-комітів

| Коміт | Короткий зміст | Актуальний статус |
|---|---|---|
| [`d9c3939`](https://github.com/NaGaa95/sphaira/commit/d9c3939130a4cd6b3e6fcfa68a0887c2418174dd) | Menu title alignment/scroll | `DONE-LOCAL`, локально ширше |
| [`0882439`](https://github.com/NaGaa95/sphaira/commit/08824397fefcda7f45dfb50a9174924eedc5dbbd) | `1.0.3`: old Haze fallback, ROM aliases, branding/version | `PARTIAL`: old Haze patch shape не застосовується; aliases лишилися; branding/version excluded |
| [`204fdde`](https://github.com/NaGaa95/sphaira/commit/204fddee97904cc151ea06015d8f1970a8615115) | Affinity-aware 3/4 cores | `PRODUCT` |
| [`6f8a886`](https://github.com/NaGaa95/sphaira/commit/6f8a88671c8b7057be2bc3e01987a9c0e0d775e5) | Publisher sort | `DONE-LOCAL` |
| [`dadce0f`](https://github.com/NaGaa95/sphaira/commit/dadce0f92648b56dd9a72ed4263425c4a1d29243) | English typo | `EXCLUDED`, translation pipeline |
| [`69f362d`](https://github.com/NaGaa95/sphaira/commit/69f362d01fefba3e737ea0e4991c8a6466aa31f3) | Direct-link install/ZIP validation | `PARTIAL`: shared ZIP safety краща; URL/free-space gaps лишилися |
| [`2e27148`](https://github.com/NaGaa95/sphaira/commit/2e271489cccea8e2124136641e0afee848a4734c) | Default icon for iconless NRO | `DONE-LOCAL` у `v0.13.450` |
| [`81f8b4b`](https://github.com/NaGaa95/sphaira/commit/81f8b4be26814e8653088f3e25b01b976f3573dc) | README/Discord | `EXCLUDED` |
| [`5b02f65`](https://github.com/NaGaa95/sphaira/commit/5b02f651cda7b3a1602b32476b58856dc0cbf335) | Custom NRO search paths | `DONE-LOCAL` у `v0.13.451/464` |
| [`16ada3e`](https://github.com/NaGaa95/sphaira/commit/16ada3e95f230db018b20c5819d5af4928021a24) | Game names for Atmosphère IDs | `DONE-LOCAL`, локально ширше |
| [`2d8b9ba`](https://github.com/NaGaa95/sphaira/commit/2d8b9baaaccf8770f024d738f791a56c23e42024) | Custom repositories + downloader robustness | `PARTIAL`: correctness P0; management UI `PRODUCT` |
| [`af4c64c`](https://github.com/NaGaa95/sphaira/commit/af4c64c630e21719113daa9db4942c7af3f6b200) | FTP NRO refresh callbacks | `PARTIAL`, потрібний cross-source path |
| [`e00ac7c`](https://github.com/NaGaa95/sphaira/commit/e00ac7c950229a6ef9b82652a5bf98dcccd4c846) | NFS export URL parsing | `DONE-SW / HW-PENDING` у `v0.13.449` |
| [`5b0779d`](https://github.com/NaGaa95/sphaira/commit/5b0779d14c9157260245e7befef11f81a29553b7) | Oversized NRO icon hardening | `DONE-LOCAL` у `v0.13.450` |
| [`9e4c46a`](https://github.com/NaGaa95/sphaira/commit/9e4c46af4bb26607d6c57f3522230d187747b3d4) | Required system version reset | `DONE-LOCAL`, краща pre-write точка |
| [`db06528`](https://github.com/NaGaa95/sphaira/commit/db06528f23c16db551925e201efc67fe4a195db3) | Localized export filenames | `PARTIAL`, shared export helper відсутній |
| [`fdcd5a1`](https://github.com/NaGaa95/sphaira/commit/fdcd5a17ac2eb4501cd78fe30603fa7d9a36e3cd) | Applet Mode layout | `DONE-LOCAL`, еквівалентний compact UI |
| [`d84d475`](https://github.com/NaGaa95/sphaira/commit/d84d4756e9143618406710bae00bda18b92da248) | Current libnx NACP layout | `DONE-LOCAL`, centralized adapter |
| [`ec854b0`](https://github.com/NaGaa95/sphaira/commit/ec854b0eff8404e8f4b41918f590c6130fc19969) | Async playtime | `PARTIAL`: local bulk/cache є; data race `MISSING-ENG`; selected worker `PRODUCT` |
| [`05279db`](https://github.com/NaGaa95/sphaira/commit/05279dbdc21f09af80e450269a8e81010d8df07b) | Avoid redundant core relaunch | `PRODUCT`, залежить від 3/4-core model |
| [`bc90664`](https://github.com/NaGaa95/sphaira/commit/bc9066455be73d886a91c1531e0cea90ab4ff858) | Update/DLC checker | `PRODUCT`, відсутній |
| [`fac197d`](https://github.com/NaGaa95/sphaira/commit/fac197dfea0973056ebeddb92c95016f57343577) | Version `1.0.4` | `EXCLUDED` |
| [`8c6e0e8`](https://github.com/NaGaa95/sphaira/commit/8c6e0e8a2b48ac1f6736af829c385b9e6b6f45de) | Play statistics toggle | `PRODUCT`, відсутній |
| [`400c514`](https://github.com/NaGaa95/sphaira/commit/400c5140cdfdb57de5f2a479eb5743233b22b88a) | MSP packages | Parser lessons `DONE-LOCAL`; MSP product layer відсутній |
| [`c19e5a3`](https://github.com/NaGaa95/sphaira/commit/c19e5a3ac893b3aafe3f229cc2bffe70493ae111) | Merge playtime toggle | `EXCLUDED`, merge-only |
| [`a55b54e`](https://github.com/NaGaa95/sphaira/commit/a55b54e9c71bf0d809efd5434a5ddfe8bd8a1912) | Screen-off in install/dump flows | Local mechanism кращий; wider coverage `PRODUCT` |
| [`f4efedf`](https://github.com/NaGaa95/sphaira/commit/f4efedf6d8065388a04d23d0b6f78f6217c957dc) | Spanish translation | `EXCLUDED` |
| [`87c855a`](https://github.com/NaGaa95/sphaira/commit/87c855a5973f6fa5e2bffc818ca5a69fb18d6090) | Restore loader affinity | `DONE-SW / HW-PENDING` у `v0.13.452` |
| [`ff87305`](https://github.com/NaGaa95/sphaira/commit/ff87305cc01f35f2afe692898cc9c3e7dd05ad85) | Version `1.0.5` | `EXCLUDED` |
| [`23f3ca6`](https://github.com/NaGaa95/sphaira/commit/23f3ca64e16d29b0218eb63b678408c7db4cbe57) | Forwarder options-list touch | `MISSING-ENG` |
| [`1476905`](https://github.com/NaGaa95/sphaira/commit/1476905cb562731f7dab1df912061035858c9cf2) | Forwarder focus edge cases | `MISSING-ENG` разом із `23f3ca6` |
| [`3c04cbc`](https://github.com/NaGaa95/sphaira/commit/3c04cbc544205578ee52eebc0934e68560e9301c) | Merge forwarder fixes | `EXCLUDED`, merge-only |
| [`92da773`](https://github.com/NaGaa95/sphaira/commit/92da773d4a538ab3ba179d4eb88304d45e8db611) | Reorder/translation updates | `EXCLUDED`, local translation pipeline |
| [`2eabcec`](https://github.com/NaGaa95/sphaira/commit/2eabcec1eab6ba593348fb431af74473c0d23135) | Versioned HTTP User-Agent | `DONE-LOCAL` у `v0.13.467` |
| [`3ef698b`](https://github.com/NaGaa95/sphaira/commit/3ef698be07596549ac4b1db65d8cee28ae93cd4d) | Merge User-Agent | `EXCLUDED`, merge-only |
| [`16169c2`](https://github.com/NaGaa95/sphaira/commit/16169c2f7d1b8440a7f9fcbb6810b5d9502f49ac) | Korean translation typo | `EXCLUDED` |
| [`f44ca2c`](https://github.com/NaGaa95/sphaira/commit/f44ca2c5fe0b67f0b90941197b4756fa5bab59b8) | Merge localization | `EXCLUDED`, merge-only |
| [`ccd290e`](https://github.com/NaGaa95/sphaira/commit/ccd290e43737e615becc3f0f5cbf35f9bfe66092) | Missing Update/DLC filter | `PRODUCT`, відсутній разом із checker |
| [`a78e92e`](https://github.com/NaGaa95/sphaira/commit/a78e92ec6f2d70e52b436a40e689d3064ae04840) | Application content shortcut | `DONE-LOCAL`, ширший Details UI; `Y` лишається multi-select |
| [`3f8303d`](https://github.com/NaGaa95/sphaira/commit/3f8303db00f33bfffa83ce0a1b750a1de14656e2) | MTP folder installation | `PRODUCT`, повністю відсутній у flat `FsInstallProxy` |
| [`1180672`](https://github.com/NaGaa95/sphaira/commit/1180672984021559718f339255bdbe7df71b0f64) | Version `1.0.6` | `EXCLUDED` |
| [`d2e95b2`](https://github.com/NaGaa95/sphaira/commit/d2e95b262405b5fc32e6c4c6ad0852c9db68224b) | Zero-byte MTP + responder version | `MISSING-ENG`; buffer-resize частина вже є |
| [`4392a66`](https://github.com/NaGaa95/sphaira/commit/4392a66fe33340087bfb9f488975d4a682cde6a9) | Portuguese translation | `EXCLUDED` |
| [`6954208`](https://github.com/NaGaa95/sphaira/commit/695420881734a9925facba90d2566188a31ee7d0) | NACP/export sanitization correction | `PARTIAL`, best-of-both task |
| [`a66eeb8`](https://github.com/NaGaa95/sphaira/commit/a66eeb87a30045993d200b03c917c9e1539f8a6d) | Merge NACP fix | `EXCLUDED`, merge-only |
| [`e16c0e1`](https://github.com/NaGaa95/sphaira/commit/e16c0e18f9e1ec3e4c8155491412de132441f394) | Image-theme storage bars | `MISSING-ENG` тільки у GameCard UI |
| [`aaf7d61`](https://github.com/NaGaa95/sphaira/commit/aaf7d614c2923d4ee876437fb72af19077dbbbe7) | Merge branches | `EXCLUDED`, merge-only |
| [`eacb54b`](https://github.com/NaGaa95/sphaira/commit/eacb54b35548ff99057744bd94f56d67c3449fed) | Merge PR #352 / HEAD | `EXCLUDED`, merge-only |

## 10. Стратегія інтеграції

- Працювати лише в primary checkout `D:\git\dev\sphaira`, лише в чистому
  `master`, по одному coding task.
- Перед кожною задачею ще раз перевіряти upstream HEAD: цей файл є snapshot,
  а не припущенням, що upstream більше не зміниться.
- Брати behavior/invariant, а не upstream file layout. Локально суттєво
  перероблені GitHub menu, File Browser, MTP, NACP, header, installer та HBL.
- Root-cause fixes ставити у найнижчу спільну локальну точку, але не нижче, ніж
  дозволяє контекст. Наприклад, ZIP validation належить shared extractor;
  Homebrew mutation policy — shared notifier з transport adapters, а не generic
  filesystem, який не знає UI search roots.
- Не додавати нову залежність або дубльований framework, якщо чинний helper чи
  patch pipeline покриває задачу.
- Кожна прийнята реалізація має окремий version bump, docs update, релевантні
  host/build checks, hardware gate за потреби та focused commit у `master`.
