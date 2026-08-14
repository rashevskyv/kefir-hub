# Актуальний план

Поточний delivery — **v0.13.452**. Завершені плани збережено в
[`archive/plan_v0.13.357-v0.13.430.md`](archive/plan_v0.13.357-v0.13.430.md)
та [`archive/plan_archive.md`](archive/plan_archive.md).

## Поточний delivery: v0.13.452 — відновлення loader thread affinity перед NRO

Статус: реалізацію, senior review і програмну верифікацію завершено. У `loadNro()` безпосередньо перед trampoline відновлюється фактична process core mask: `svcGetInfo(InfoType_CoreMask, CUR_PROCESS_HANDLE)` → `svcSetThreadCoreMask(CUR_THREAD_HANDLE, -1, core_mask)`. Будь-яка помилка проходить через `diagAbortWithResult`; `highest_cpu_id = 3` не перетворюється на жорстку mask. Gemini успішно виконав WSL `ReleaseWithInstall` (`[100%] Built target sphaira_nro`) і `git diff --check`; версію піднято до `0.13.452`. Залишається лише апаратний smoke-test.

1. Перевірити на Switch старт NRO з Homebrew Menu та повернення/перезапуск через `envSetNextLoad()`.
2. Очікуваний результат: NRO запускається і повторно запускається без зависання, крашу або зміни UI/CPU-налаштувань.
3. Якщо запуск переривається, зафіксувати точний Horizon Result з abort screen; це симптом для наступного bounded fix.

## 0. v0.13.451 — custom NRO search paths

Статус: реалізацію та всі перевірки завершено. `/switch` лишається незмінним default root; додаткові native-SD roots зберігаються в `[homebrew_paths]`, валідні absolute paths нормалізуються та дедуплікуються, а сканування custom roots має глибину 2. Пройдено `tests/run.sh` (154 checks у `path_util`), JSON validation, WSL `ReleaseWithInstall` і `git diff --check`. Російську локаль свідомо виключено з цього delivery до окремого i18n pipeline.

1. Повторно використано `minIni`, event-based Homebrew refresh і `path_util.hpp`; не додано subsystem, dependency, network filesystem або конфігурацію для `/switch`.
2. `NormalizeSearchPath` відхиляє не-absolute, `.`/`..`, backslash, `:`, control bytes, root, `/switch` і довжину `>= FS_MAX_PATH` до будь-якого створення `fs::FsPath`; неіснуючі roots пропускаються під час scan.
3. File Browser дозволяє add/remove лише для дозволеного SD-каталогу; remove має підтвердження, а успішна зміна конфігу надсилає `homebrew::SignalChange()`.
4. `/switch` сканується чинним `nro_scan`, кожен custom root — `nro_scan_depth(..., 2)`; NRO entries дедуплікуються за canonical path, а empty Homebrew list безпечний.

## 0.1. v0.13.449 — NFS phase 1 (read-only source)

Статус: програмну реалізацію та senior review завершено; host suite (`nfs_url`: 194 checks), dead-symbol guard, WSL `ReleaseWithInstall` і `git diff --check` пройдено 2026-08-14. Апаратна перевірка на реальній Switch залишається відкритою.

1. Підключено статичний `ITotalJustice/libnfs@65f3e11` через `FetchContent`; dependency documentation, examples і tests вимкнено.
2. Додано read-only `devoptab_nfs.cpp`, що належить спільному `MountNetworkDevice2()`, використовує `nfs_parse_url_dir()`, RAII cleanup та повертає `EROFS` для мутацій.
3. Додано host-testable NFS URL validator із canonical lowercase scheme, hostname/IPv4 і port validation, збереженням nested export path, лімітом `FsPath`, відхиленням credentials, traversal, query/fragment, IPv6 та небезпечного percent encoding.
4. NFS підключено до File Browser, source picker і Settings; на кожному маршруті збережено read-only flag, а невалідні saved URLs відсіюються до копіювання у фіксовані `FsPath`.
5. Оновлено англійську та українську локалізації, додано 194 host checks і завершено software verification. Наступний крок — browse/read/copy-from-NFS smoke test на Switch.

## 0.2. v0.13.448 — очищення екранних NTP-сповіщень

Статус: реалізацію завершено; прибрано тимчасові діагностичні tooltip-и та нелокалізоване сповіщення UI refresh; збережено повне логування `[NTP]` та єдине локалізоване сповіщення "Clock synced" для фактично оновленого User Clock; пройдено WSL `ReleaseWithInstall`, `git diff --check`, оновлено living docs.

1. У `sphaira/source/ntp.cpp` вилучено `SHOW_NTP_PROGRESS_TOOLTIPS` та виклик `App::Notify` із `ReportSyncStage()`, зберігши запис усіх етапів і результатів у `[NTP]` лог.
2. Прибрано нелокалізоване сповіщення `App::Notify("NTP: UI clock refreshed", ...)` з блоку оновлення UI.
3. Збережено виклик локалізованого `App::Notify("Clock synced"_i18n)` як єдиного екранного сповіщення, що чергується в UI-потоці через `evman::push` виключно після успішного live-запису User Clock та `__libnx_init_time()`.
4. Гарантовано відсутність сповіщень на шляху, коли зміщення менше за `MIN_CORRECTION_SECONDS` (час уже точний), та на fallback-шляху `used_fallback` (коли увімкнено automatic correction і діє процесний offset).
5. Піднято `sphaira_VERSION` до `0.13.448`, оновлено `task.md`, `plan.md`, `walkthrough.md`.

## 0.3. v0.13.447 — upstream-equivalence hardening: безпечне ZIP extraction

Статус: реалізацію, валідатор і тести завершено; пройдено `tests/run.sh` (106 checks у `path_util`), WSL `ReleaseWithInstall` (`[100%] Built target sphaira_nro`), `git diff --check`, враховано senior review (захист `number_entry` overflow та оновлення коментаря санітизації), піднято версію до `0.13.447` і створено сфокусований коміт.

1. Досліджено всі 11 викликів `thread::TransferUnzipAll()` та виправлено root cause у спільній функції, захистивши всі операції розпакування (Appstore, direct-link/GitHub downloads, cheats, firmware, File Browser, save restore, translations).
2. Додано inline helper `path::IsSafeArchiveEntry(std::string_view)` у `sphaira/include/path_util.hpp`, який валідує відносні шляхи й каталоги, відхиляє порожні імена, початковий `/`, backslash `\`, керуючі символи (< 0x20, DEL 0x7F), `:` (захист від device/scheme) та `.`/`..` компоненти шляху, зберігаючи валідні файли з крапками (`.config`, `..data`, `file.name`).
3. У першому проході `thread::TransferUnzipAll()` додано перевірку `info.size_filename` на відповідність буферу та `strlen`, валідацію `path::IsSafeArchiveEntry()`, перевірку сумарної довжини шляху призначення з `base_path` на ліміт `sizeof(fs::FsPath)`, а також захист від переповнення `s64` для сумарного `uncompressed_size` та `ginfo.number_entry`.
4. Збережено чинну HOS character sanitization для безпечних неструктурних символів (`*`, `?`, `"`, `<`, `>`, `|`), чинні filter callbacks і progress semantics; не додавалося SD-специфічних перевірок вільного місця у спільний helper.
5. Розширено host-тести `tests/test_path_util.cpp`, пройдено `tests/run.sh`, WSL `ReleaseWithInstall` та `git diff --check`.
6. Піднято `sphaira_VERSION` з `0.13.446` до `0.13.447`, оновлено `task.md`, `plan.md`, `walkthrough.md`, `upstream_audit.md` і створено один сфокусований коміт.

## 0.1. v0.13.446 — NTP через системну automatic correction

Статус: реалізацію та програмні перевірки завершено (WSL `ReleaseWithInstall` та `git diff --check` успішно пройдено 2026-08-13, версію піднято до v0.13.446). Апаратна перевірка на реальній Switch залишається відкритою.

1. Вилучено некоректний виклик `DisableAutomaticCorrection()`.
2. Залишено спроби запису User Clock та Network Clock через `time:su` і `time:s`. Після спроб запису виконується повторне зчитування User Clock: якщо час збігається з NTP (до 2 с), це вважається негайним live-синхроном (скидається process offset, оновлюється libnx time, виводиться "Clock synced").
3. Якщо User Clock все ще відхиляється, через `set:sys` зберігається `NetworkSystemClockContext` та вмикається `setsysSetUserSystemClockAutomaticCorrectionEnabled(true)`.
4. У tooltip/log для fallback шляху виводиться `automatic correction enabled; reboot required to update HOS User Clock`, при цьому не показується "Clock synced" і не стверджується live-зміна HOS User Clock. Process offset Sphaira зберігається для миттєвого відображення часу в додатку.
5. Пройдено WSL `ReleaseWithInstall`, `git diff --check`, піднято версію до `0.13.446`, оновлено living docs.

## 0.1. v0.13.445 — NTP User Clock через set:sys

Статус: реалізацію та програмні перевірки завершено (WSL `ReleaseWithInstall` та `git diff --check` успішно пройдено 2026-08-13, версію піднято до v0.13.445). Апаратна перевірка на реальній Switch залишається відкритою (hardware verification remains pending: миттєве оновлення годинника Sphaira, NTP trace та збереження часу після перезапуску).

1. У `SetSystemTime()` зберегти чинні спроби `time:su` і `time:s` для live User Clock; після їхньої відмови спробувати штатний `set:sys` IPC: отримати standard steady-clock time point, утворити `TimeSystemClockContext { NTP - steady, steady }`, записати User і Network context та вимкнути automatic correction у `set:sys`. Запис у `errors.txt` виконувати лише при відмові `set:sys`, щоб успішний fallback не залишав хибних записів про помилки.
2. Не вважати `set:sys` live-успіхом без перевірки: оновити часовий display Sphaira на NTP offset одразу в поточному процесі, а persisted context лишити джерелом правильного часу після перезавантаження HOS.
3. Залишити `SHOW_NTP_PROGRESS_TOOLTIPS = true` і показати відкриття `set:sys`, зчитування steady clock, запис кожного context, результат automatic-correction та підсумок.
4. Пройти WSL `ReleaseWithInstall` і `git diff --check`, підняти версію до `0.13.445`, оновити living docs і виконати ручну перевірку на Switch.

## 0.1. v0.13.444 — видимий NTP diagnostic trace

Статус: реалізацію завершено; WSL ReleaseWithInstall пройшов 2026-08-13, версію піднято до v0.13.444. Потрібна ручна перевірка на Switch.

1. Лог v0.13.443 показав, що NTP-відповідь отримано, але обидва шляхи `time:su` та `time:s` відхилили User system clock з `0x00000274`.
2. Додати тимчасовий `ReportSyncStage`: він записує `[NTP]`-рядок і thread-safe tooltip зліва. Прапор `SHOW_NTP_PROGRESS_TOOLTIPS` залишити `true` до завершення апаратної діагностики.
3. Покрити tooltip-ами кожен етап: мережу, DNS, socket/send/receive, валідну відповідь, читання й offset User Clock, кожну операцію `time:su` і `time:s`, fallback, UI refresh і фінальний Result.
4. Пройдено WSL `ReleaseWithInstall` та `git diff --check`; версію піднято до `0.13.444`.

## 0.1. v0.13.443 — запис NTP-часу через `time:su`

Статус: реалізацію завершено; WSL ReleaseWithInstall пройшов 2026-08-13, версію піднято до v0.13.443. Потрібна ручна перевірка на Switch.

1. Логи з HOS 20.5.0 показали, що NTP-відповідь надходить, але `time:s` відхиляє і вимкнення automatic correction, і запис User system clock з `0x00000274`.
2. `SetSystemTimeWithService` виконує чинну спробу запису для одного сервісу; `SetSystemTime` спершу викликає її для `time:su`, а потім для `time:s` лише якщо User system clock не було записано.
3. Невдале вимкнення automatic correction лишається best-effort. Успіх визначає лише запис User system clock; Network system clock лишається best-effort.
4. За повної невдачі в `errors.txt` записуються Result обох сервісів, що робить наступний апаратний тест діагностичним.
5. Пройдено WSL `ReleaseWithInstall` та `git diff --check`; версію піднято до `0.13.443`.

## 0.1. v0.13.442 — усунення крашу File Browser при завантаженні асоціацій

Статус: реалізацію завершено; WSL ReleaseWithInstall пройшов 2026-08-13, версію піднято до v0.13.442.

1. Причина крашу: під час додавання багатьох асоціацій запусків зростання `std::vector<FileAssocEntry>` викликало реалокацію вектора й копіювання великих об'єктів `FileAssocEntry` (кожен з яких містить 0x301-байтний буфер `fs::FsPath`), що призводило до переповнення стеку / крашу в `memset`.
2. Виправлення: додати static-функцію `CountAssocEntriesPath` і перед додаванням асоціацій обчислити максимальну кількість `.ini` файлів-кандидатів у `romfs:/assoc/` та `paths::ASSOC`, після чого підготувати ємність вектора через `m_assoc_entries.reserve(...)`.
3. Успішно виконано збірку WSL `ReleaseWithInstall` та перевірено `git diff --check`. Версію піднято до `0.13.442`.

## 0.1. v0.13.441 — захист звичайного хрому UI Sphaira

Статус: реалізацію завершено; WSL ReleaseWithInstall пройшов 2026-08-13, версію піднято до v0.13.441.

1. У `App::Draw()` (`sphaira/source/app.cpp`) перенесено виклик `DrawChrome()` після відмальовки всіх немодальних віджетів і контенту, щоб елементи звичайного контенту не перекривали лінії заголовка та футера.
2. Додано метод `IsModal()` у `Widget` та перевизначено для модальних діалогів (`OptionBox`, `PopupList`, `ProgressBox`, `ErrorBox`, `HoldConfirmBox`, `HoldOkBox`, `KefirChangelogBox`), щоб вони малювалися поверху хрому та залишали ефект затемнення екрана.
3. Оновлено `WantsChrome()` у `fileview::Menu` (`file_viewer.hpp`), щоб повертати `!m_fullscreen`, вмикаючи стандартний хром у неповноекранному перегляді та вимикаючи у повноекранному.
4. Оновлено `ImageBounds()` у `file_viewer.cpp` з використанням констант `layout::ContentBand()`.
5. Пройдено збірку WSL `ReleaseWithInstall` та `git diff --check`, піднято версію до `0.13.441`.

## 0.1. v0.13.440 — інтерактивне керування чергою інсталяції (Skip / Cancel)

Статус: реалізацію та тести завершено; WSL ReleaseWithInstall пройшов 2026-08-12, версію піднято до v0.13.440.

1. У стані `Installing` призначити кнопку `B` на пропуск поточного пакета (`Skip package`), а `X` — на скасування всієї черги (`Cancel queue`).
2. Обидві дії показують явний діалог підтвердження через `App::Push<OptionBox>` із варіантами `No` (типовий) та `Yes`.
3. Підтвердження пропуску (`B` -> `Yes`) перериває встановлення лише поточного пакета через `m_skip_requested` та `m_cancel_event`, записує пакет як `Skipped` у статистиці та підсумку (без помилки в error list), після чого автоматично скидає сигнал переривання й переходить до наступного пакета черги.
4. Підтвердження скасування (`X` -> `Yes`) викликає `CancelSession()`, перериває інсталяцію зі збереженням уже встановлених пакетів і завершує сеанс.
5. Логіка уніфікована та працює ідентично для обох режимів черги: USB (`ThreadFunction`) та локальних файлів (`LocalThreadFunction`).
6. Додано переклади EN/UK для нових текстів підтверджень, додано host unit-тест `test_queue_outcome.cpp`, пройдено всі перевірки та піднято версію до `0.13.440`.

## 0.1. v0.13.439 — миттєва NTP-синхронізація

Статус: реалізацію та senior-review завершено; WSL ReleaseWithInstall пройшов 2026-08-10, версію піднято до v0.13.439. Залишилася ручна перевірка на Switch.

1. Залишити один фоновий worker і чинні NTP fallback-сервери. Першу спробу
   виконувати без стартової 10-секундної паузи; `Start()` для вже активного
   worker має лише розбудити його, а не створювати другий thread.
2. Після отримання NTP часу нічого не робити при різниці меншій за чинні 2 с.
   Якщо корекція потрібна, вимкнути live automatic-correction flag і записати
   user clock; успіх network clock не може маскувати помилку user clock.
3. Після успішного запису через чинний thread-safe `evman::FunctionalEventData`
   перейти на UI-потік, повторно ініціалізувати часову базу libnx і показати
   локалізований `Clock synced`. Це має одразу оновити всі чинні виклики
   `std::time()` без окремого offset-cache або змін у кожному caller.
4. Не показувати toast для вже точного годинника, відсутньої мережі чи помилки;
   зберегти чинний retry/backoff і діагностичні логи.
5. Перевірити WSL `ReleaseWithInstall`, підняти версію до `0.13.439`, оновити
   living docs і створити focused commit.

## 0.1. v0.13.438 — перемикач USB 3.0

Статус: реалізацію та senior-review завершено; EN/UK JSON і WSL
`ReleaseWithInstall` пройшли 2026-08-10, версію піднято до `v0.13.438`.
Залишилася ручна перевірка на Switch.

1. У `Tools → Налаштування кефіру` показувати один `USB 3.0` On/Off-рядок.
   Лише точне `u8!0x0` означає Off; відсутній файл або ключ означає типовий On.
2. Після успішного запису `[usb] usb30_force_enabled` commit-ити SD до показу
   діалогу. Помилка запису показує чинний error box і не пропонує reboot.
3. Повідомити, що зміна набуде чинності лише після перезавантаження, та дати
   вибір `Пізніше` / `Перезавантажити` через чинний forced-reboot шлях.
4. Перевірити EN/UK JSON, WSL `ReleaseWithInstall`, підняти версію до
   `0.13.438`, оновити living docs і створити focused commit.

## 0.2. v0.13.436 — незалежний скрінсейвер

Статус: реалізацію та senior-review завершено; host-тести й WSL
`ReleaseWithInstall` пройшли 2026-08-10. Відкрита лише ручна перевірка на Switch.

1. Не створювати окремий render thread: NanoVG/deko3d і HID лишаються в UI
   thread. Натомість зробити активний шлях неблокуючим: семплер графіка не
   потребує `m_mutex`, а prompt/snapshot читаються через `mutexTryLock` із
   поверненням останнього готового `SaverInfo`, якщо worker коротко зайнятий.
2. Прибрати `App::SetBlankBrightness()` з кожного кадру правого стіка. Тримати
   нове значення локально, одразу застосовувати його через `lbl*`, а INI
   записувати один раз тільки після виходу worker зі стану `Installing`; preview
   без активного запису може зберегти значення при закритті.
3. Залишити графік UI-власністю й додавати семпл кожні 0,5 с з атомарних
   `m_total_read`/`m_total_write`. Нульовий приріст є валідним нульовим семплом,
   а не причиною зупинити ані графік, ані скрінсейвер.
4. Додати в `SaverInfo` явний finished-стан. У `Summary` графік не малювати, а
   на його місці незалежно від `saver_fields` показувати локалізоване
   `Finished` / `Finished with errors`.
5. Додати одну INI-опцію timeout у секцію чинних screen-off налаштувань:
   `Off` за замовчуванням і короткий набір практичних preset-ів. Таймер працює
   лише у `State::Installing`, скидається будь-якою кнопкою, touch або рухом
   стіка та після ручного/автоматичного wake.
6. Виділити лише мінімальну чисту timeout-перевірку, потрібну host-тесту; не
   додавати scheduler, thread class чи залежність. Прогнати host-тести та відому
   WSL-збірку.
7. Після senior review підняти версію за чинною схемою, оновити
   `task.md`/`plan.md`/`walkthrough.md` і створити focused commit лише з
   screensaver delivery, зберігши всі наявні незакомічені зміни інших задач.

## 0.3. v0.13.437 — Text Viewer / Editor UX

Статус: реалізацію та corrective senior-review завершено; host-тести й WSL
`ReleaseWithInstall` пройшли 2026-08-10, версію піднято до `v0.13.437`.
Відкрита лише повторна ручна перевірка на Switch.

1. Додати одну спільну перевірку відомих текстових форматів і викликати її з
   головної дії `A` та контекстного меню File Browser. Спеціальні типи
   (`nro`, install, image, zip) залишити пріоритетними.
2. Передати text viewer неволодіючий `fs::Fs*` поточного `FsView` і окремий
   writable-прапорець. Нижній File Browser живе довше за pushed viewer, тому
   pointer безпечний; image-viewer і його `FsNativeSd` не зливати з цим шляхом.
3. Обробляти Open/GetSize/Read як одну fallible операцію: при будь-якій помилці
   не створювати порожній editable buffer, а відкласти показ Result до першого
   Update, коли viewer уже лежить у стеку UI.
4. Зберігати точний baseline останнього успішного Save. Після кожної зміни,
   Undo і Redo обчислювати dirty як `BuildText() != saved_text`; успішний Save
   оновлює baseline і очищає історію.
5. Не писати поверх оригіналу: створити sibling temp, повністю записати його,
   перейменувати оригінал у recovery backup, temp — в оригінал і відновити
   backup при помилці. Невдалий Save повертає failure, не закриває editor і не
   губить buffer; read-only джерела взагалі не отримують write actions.
6. Go to line затискає номер до фактичного діапазону та викликає
   `List::EnsureVisible`. Insert спочатку відкриває keyboard і додає новий
   рядок лише після підтвердження, як у перевіреному upstream UX.
7. Розділити File Viewer на явні `View` і `Edit`: View не має курсора та не
   змінює файл; Edit зберігає чинні undo/redo/save і редагування всього рядка
   через Switch keyboard.
8. Правий стік прокручує viewport незалежно. Лівий стік і D-pad рухають курсор
   лише в Edit; `A` одразу викликає keyboard для вибраного рядка без
   проміжного popup.
9. Для INI мінімально підсвітити section/comment/key/value. Подвійний touch tap
   по рядку з boolean RHS безпечно перемикає лише окремий токен `true` або
   `false`, не чіпаючи коментарі чи частини інших слів, і створює undo snapshot.
10. Залишити обмеження редагування великих файлів, але дозволити їх перегляд без
   безконтрольного читання всього файла в RAM. Додати одну невелику host-перевірку
   чистої логіки розпізнавання/toggle.
11. Після review прогнати host-тести та відому WSL-збірку, підняти версію за
   чинною схемою, оновити `task.md`/`plan.md`/`walkthrough.md` і зробити focused
   commit тільки з цієї функції.
12. Окремими атомарними комітами зафіксувати незалежні стабілізаційні виправлення:
    teardown transfer UI, auto-detect формату HB-іконок і USBDS detach на HOS 22.5.

## 1. MTP Games: merged NSP core

1. Залишити `BuildNspEntries` канонічним шляхом окремого дампу, але прибрати
   припущення, що всі NCA одного `NspEntry` лежать в одному storage.
2. Додати один merged-builder для BASE, останнього встановленого UPD і всіх
   встановлених DLC. DataPatch не включати до merged-пакета без окремо
   погодженої семантики назви.
3. Не дублювати однакові NCA та rights ID; PFS0 має містити всі потрібні NCA,
   CNMT, ticket і certificate та читатися потоком без тимчасового файла.
4. Формувати ім'я `Назва [TitleID][B+U65536+9DLC].nsp`. Відсутні складові
   опускати: `[B]`, `[B+U65536]`, `[B+9DLC]`.
5. Залишити чисте форматування суфікса доступним host-тесту без Switch SDK.

## 2. MTP Games: структура диска

1. Корінь read-only диска містить лише `Merged` і `Separate`.
2. `Merged` містить по одному об'єднаному NSP на встановлену гру.
3. `Separate/<Game [TitleID]>` містить наявні окремі BASE/UPD/DLC NSP через
   чинний `BuildNspEntries`.
4. Усі write/create/delete/rename операції залишаються забороненими; відкриті
   transfer handles мають переживати очищення кешу.
5. Не торкатися наявних незакомічених змін у forwarder-editor і `tests/run.sh`.

## Паралельний запит: TICO launchers

1. Повторно використати чинний механізм `assets/romfs/assoc/*.ini`: окремі
   TICO-асоціації завантажуються лише коли відповідний NRO реально існує у
   `/tico/cores`.
2. Додати одне необов’язкове поле фіксованого аргументу асоціації. Воно потрібне
   лише Gambatte (`gb`, `gbc`) і Genesis Plus GX (`genesis`, `master-system`,
   `game-gear`, `sega-cd`) та має однаково працювати для запуску і форвардера.
3. Один формат підпису використати в обох меню: спочатку RetroArch, потім TICO,
   усередині — назви ядер. Не змінювати загальний `PopupList` і не додавати
   залежностей.
4. Розпізнавати TICO-назви каталогів `sega-cd`, `fbneo`, `naomi`, `naomi2` та
   `atomiswave`; розширення брати з установлених ядер і чинних RetroArch INI.
5. Не перезаписувати незакомічені зміни delivery `0.13.432`, особливо у
   `filebrowser_forwarder.cpp`, `plan.md`, `task.md` та версії.

Реалізовано у `v0.13.433`: 17 конфігурацій покривають 13 установлених ядер,
спільний шлях аргументів працює для запуску, архівів і форвардерів; host-тести
та WSL-збірка пройдені. Залишилась апаратна перевірка на Switch.

## 3. Create repack — окремий етап після MTP

1. Додати `Create repack` у `Tools → Games → Game Actions`.
2. В окремому sidebar-вікні показувати лише фактично встановлені BASE, UPD і DLC;
   доступні компоненти за замовчуванням увімкнені, порожній вибір не запускає запис.
3. Розширити чинний merged NSP-builder прапорами вибору та повторно використати
   `NspSource` і `dump::Dump`, без нового формату чи проміжних файлів.
4. Результат записувати одним NSP у `/games` із погодженою схемою назви.
5. LayeredFS винести в наступний етап: опцію не показувати, доки немає коректної
   перебудови Program NCA, хешів і CNMT.

## 4. Верифікація і delivery

### UI-косметика

Прибрати порожнє посилання Progress з обох шапок вебсервера; на DBI-екрані USB-стан показувати над інструкцією, а Applet Mode — окремим вузьким текстовим блоком; перед NAND/SD-значеннями лишити видимий відступ. Host-тести та WSL-збірку пройдено.
1. Прогнати host-тести у WSL.
2. Зібрати `cmake --build --preset ReleaseWithInstall` у WSL.
3. Підняти версію до `0.13.435`, оновити `task.md`, `plan.md`,
   `walkthrough.md` і створити focused commit лише з прийнятими змінами.
4. Залишити hardware-gate відкритим до копіювання обох типів NSP на ПК і
   перевірки встановлення на Switch.

## 5. Закрити hardware-gates останніх delivery

1. Перевірити керування скрінсейвером у `v0.13.430`: обидва стіки, межі
   екрана, збереження яскравості та пробудження.
2. Перевірити чергу встановлення й скрінсейвер у `v0.13.429`: проєкцію
   NAND/SD без перекриття хедера, R/W-графік і preview.
3. Пройти USB-матрицю: DBI backend, Awoo/TinFoil і GoldLeaf v0.10+.
4. Повторити MTP smoke-test: лістинг телефона, перепідключення кабелю та
   встановлення NSP.

Результати ручних перевірок записувати в `tests.md`.

## 6. Наступні функціональні задачі

1. DBI UI: динамічний рядок журналу та наочний `ReviewQueue` із
   сегментованими NAND/SD-смугами.
2. Games: dump/verify/read-only mount, save integration і ticket details.
3. Network sources: NFS read-only завершено у `v0.13.449`; SFTP — лише після окремого погодження протоколу й UX.

Вбудований player залишається замороженим до окремого рішення.

## Правило завершення

Задача закривається після автоматичної перевірки збірки; hardware-задача —
лише після результату з реальної Switch у `tests.md`.
