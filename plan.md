# Актуальний план

Поточний delivery — **v0.13.590**. Завершені плани збережено в
[`archive/plan_v0.13.357-v0.13.430.md`](archive/plan_v0.13.357-v0.13.430.md)
та [`archive/plan_archive.md`](archive/plan_archive.md).

## Поточний delivery: v0.13.590 — no dynamic_cast (Switch builds with -fno-rtti)

Статус: програмну частину реалізовано (SW-DONE). Агент не компілює.
1. `App::Draw` і `CloseFileBrowsersOnUsbMount` не можна кастити через `dynamic_cast` (`-fno-rtti`). Замість цього віртуальні `Widget::BlocksDrawUnder` і `OnUsbMountRemoved`.

## Попередній delivery: v0.13.589 — Games list uses the same badge pills as icon layouts

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Макет список більше не пише `[S|N|b|u]` літерами збоку. Ті самі кольорові бейджі, що на обкладинках (GC / Base / DLC / Update / LayeredFS), плюс SD і NAND повними підписами; розмір лишається справа.

## Попередній delivery: v0.13.588 — NAND/SD move UI stays alive (Cancel works)

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Під час ProgressBox не малювати сітку ігор (GPU чекав кадр, B не доходив).
2. Move без CPU FastLoad (глушить GPU). YieldType_ToAnyThread.

## Попередній delivery: v0.13.587 — Move to NAND/SD shows the current NCA

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. CreatePlaceHolder на гігабайти тримає бар на 0%. Тепер рядок: Allocating/Copying · Application · Program (1/4) + розмір; бар 0–100% по поточному NCA.

## Попередній delivery: v0.13.586 — flash plug/unplug without file-browser magic

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Після MTP-кабеля Poll більше не ігнорує usb:hs (ранній return usbds&&!haze). PC unplug = usb:ds Detached → haze::Exit одразу.
2. UMS (vid/pid) → попап відкрити саме цю флешку. Витяг → закрити файловий браузер, якщо він на ній.

## Попередній delivery: v0.13.585 — USB flash while MTP is on must not crash

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. MTP у налаштуваннях більше не хапає порт як gadget, поки немає ПК (LowPower). Інакше флешка (теж device) валить usb:ds Instruction Abort.
2. Без ПК — usbhsfs (хост), флешка монтується і пропонується файловий браузер. ПК — MTP.

## Попередній delivery: v0.13.584 — Games storage bars: blue only where the title lives

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. NAND/EmuNAND і microSD синіють лише якщо на цьому носії є дані поточної гри (або виділення). Гра лише на SD — синя SD; лише на NAND — синій NAND; спліт — обидва.

## Попередній delivery: v0.13.583 — MTP Games dumps: compatible / separate / both

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Диск Games був порожній: haze відкриває `/games`, парсер чекав `/Merged`. FixPath як у Saves.
2. Settings → MTP storages → Dump format: сумісний (один NSP), окремі файли, обидва (Merged/ + Separate/).

## Попередній delivery: v0.13.582 — USB plug: flash → file browser, PC → MTP

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Флешка (usbhsfs): попап «відкрити у файловому браузері».
2. Кабель у ПК (LowPower VBUS, немає mass-storage): одразу MTP; після від'єднання порт повертається USB storage.

## Попередній delivery: v0.13.581 — GitHub release; restore only-if-newer auto-update

Статус: SW-DONE. Деплой на rashevskyv/kefir-hub.
1. `kForceUpdateForTest = false`: оновлення лише якщо latest новіший.
2. Реліз 0.13.581 (changelog з 0.13.565).

## Попередній delivery: v0.13.580 — Ask dialog: Skip on its own row, Minus shortcut

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Skip на всю ширину над Пізніше/Оновити, текст по центру. Minus — скіп, Plus — оновити, B — пізніше.

## Попередній delivery: v0.13.579 — Ready — restart actually restarts

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Auto-update → Update now, коли «Ready — restart»: тап перезапускає Kefir Hub.

## Попередній delivery: v0.13.578 — Drop On demand; Ask has Later / Skip / Update

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Режими: Off / Silent / Ask. On demand прибрано (старий 3 → Silent).
2. Ask: Пізніше (знову спитає), Пропустити це оновлення (до наступного релізу), Оновити. У папці лишається рядок пропущеної версії.

## Попередній delivery: v0.13.577 — Auto-update is a folder in General

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Автооновлення — папка зверху в Загальний, не окрема категорія зліва.

## Попередній delivery: v0.13.576 — Header counter no longer jitters 1px

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. «2 / 13» у хедері прив’язаний правим краєм; слот як «13 / 13», щоб 1 і 2 не зсували лічильник.

## Попередній delivery: v0.13.575 — About: parse ### headings, split Update vs notes

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Changelog: `###` / `##` рендеряться як заголовки, без решіток.
2. About: **X** — оновити список змін; **A** — оновити Kefir Hub, якщо є реліз.

## Попередній delivery: v0.13.574 — Saves settings: filters, backup defaults, WebDAV

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Settings → Збереження: показ (встановлені / видалені / бекапи), джерело за замовчуванням, стиснення, автобекап при restore, шляхи, auto-sync і WebDAV.

## Попередній delivery: v0.13.573 — Network: FTP/MTP toggles then folders

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Мережа: FTP і MTP — увімк/вимк на самій сторінці; під кожним папка (логін/порт і сховища).

## Попередній delivery: v0.13.572 — Auto-update category at the top of Settings

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Settings: перша категорія «Автооновлення» — режим (Off / Silent / Ask / On demand) з окремим описом кожного, Update now, пропущена версія.
2. Ask: попап Skip / Update; Skip запам'ятовує цей реліз і більше не питає.

## Попередній delivery: v0.13.571 — Y toggles boolean lines

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. У редакторі **Y** — Toggle (`=0`/`=1` / true/false / u8!0x0). **A** знову лише Edit line.

## Попередній delivery: v0.13.570 — Toggle 0/1 on A; fix swkbd overflow

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. У редакторі **A** / подвійний тап на `=0`/`=1`/`true`/`false`/`u8!0x0` перемикає буль, не відкриває клавіатуру. Підказка A: Toggle.
2. swkbd: буфер = max length, `swkbdClose`, скидання тачу після аплету.

## Попередній delivery: v0.13.569 — Silent update ToFileAsync needs StopToken

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Silent `ToFileAsync` передає `StopToken{}` — інакше static_assert.

## Попередній delivery: v0.13.568 — Non-silent update uses the download icon

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Ask / On demand: прогрес апдейта в тому ж ProgressBox, що й скачування (іконка, L3 згортає). Silent лишає «Оновлення» в хедері.

## Попередній delivery: v0.13.567 — Force auto-update for mode testing

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Тимчасово: latest з GitHub завжди вважається оновленням, навіть якщо встановлена версія вища. Режими Silent / Ask / On demand / Off лишаються. Після QA — повернути порівняння версій.

## Попередній delivery: v0.13.566 — Auto-update modes, header progress, no EmuNAND badge

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Тихе оновлення в фоні; тег `v0.13.x` порівнюється коректно. У хедері під час завантаження — «Оновлення» + прогрес замість смуг NAND/SD.
2. Settings: Off / Silent / Ask / On demand + Update now.
3. Бейдж EmuNAND прибрано: підпис смуги вже EmuNAND.

## Попередній delivery: v0.13.565 — File open menu for text + correct expand-range glyphs

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. **A** на текстовому файлі (до відкриття): View / Edit / Edit on PC / phone. Те саме в Options, вище в списку.
2. Легенда розширення діапазону: **L/R + Up/Down**, не SL/SR.

## Попередній delivery: v0.13.564 — Stop auto-forwarder from stalling launch/exit

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Скан HOME-іконів більше не читає NACP кожної гри — лише 0x05 / відомі HBL Title ID.
2. Запуск з Album / nxlink більше не ставить другий Kefir Hub, якщо ікон уже є (інший path-hash).

## Попередній delivery: v0.13.563 — Ask to save when closing the remote editor from the Switch

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. **B** на консолі, якщо в браузері були незбережені правки: «Зберегти зміни?» Не зберігати / Зберегти.

## Попередній delivery: v0.13.562 — Remote editor fills the browser window

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Поле CodeMirror більше не лишається смугою ~300px: CSS після CDN і flex розтягують редактор на всю висоту вікна.

## Попередній delivery: v0.13.561 — Edit on PC / phone from the file browser

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Options файлового браузера: для текстового файлу, який можна редагувати (не read-only, ≤ 4 МБ), є «Редагувати на ПК / телефоні» поруч із Edit.

## Попередній delivery: v0.13.560 — Full-page remote file editor (CodeMirror)

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. «Редагувати на ПК / телефоні» відкриває повноекранний редактор, не картку з textarea.
2. CodeMirror 5 з CDN: номери рядків, підсвітка за розширенням, Ctrl+S. У NRO лише HTML-оболонка.
3. Вставка кількох рядків лишається маленьким вікном Send.

## Попередній delivery: v0.13.559 — Text editor: expand an existing line range

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Коли вже є виділення рядків: **Y** і Дії → «Розширити діапазон».
2. **L** + вгору/вниз і лівий стік рухають верхню межу; **R** + вгору/вниз і правий стік — нижню. Вгору від верхньої межі розширює, вниз звужує; вгору від нижньої піднімає край (звужує), вниз розширює.
3. У легенді: верхня межа `L` / лівий стік, нижня `R` / правий стік. **A** Готово, **B** скасовує зсув меж.

## Попередній delivery: v0.13.558 — Text editor: one outline around the whole line selection

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Виділені через Дії рядки підсвічуються однією рамкою, розтягнутою на весь діапазон, як рамка поточного рядка.

## Попередній delivery: v0.13.557 — Reuse remote input for file edit (no extra editor page)

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Прибрано окрему JS-сторінку редактора. І «редагувати файл», і «вставити під курсор» — той самий `/input` + `RequestRemoteText`, лише `multiline`.

## Попередній delivery: v0.13.556 — Paste from PC / phone at the cursor

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. У діях текстового редактора: «Вставити з ПК / телефона» — багаторядковий remote input, текст лягає під поточний рядок.

## Попередній delivery: v0.13.555 — Text editor on PC / phone

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. У файловому редакторі Options → «Редагувати на ПК / телефоні»: той самий QR/HTTP, простий JS-редактор (підсвітка, undo/cut, Ctrl+S). Save надсилає файл назад і пише на SD.

## Попередній delivery: v0.13.554 — Picker Create Folder defaults to the archive name

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. «Створити теку» в пікері розпакування підставляє ім'я архіву (без .zip). Користувач може стерти або перейменувати.

## Попередній delivery: v0.13.553 — Direct Download: Enter sends the URL

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Enter у полі Direct Download надсилає адресу на консоль (як кнопка Send).

## Попередній delivery: v0.13.552 — Picker Options: Create Folder + Close picker

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Контекстне меню пікера: «Створити теку» і «Закрити вибір папки».

## Попередній delivery: v0.13.551 — Folder picker: Create Folder, minus returns to extract

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Options пікера — лише «Створити папку», без copy/cut.
2. «−» / B у корені закриває лише пікер і повертає до варіантів розпакування.

## Попередній delivery: v0.13.550 — Zip extract: stable row lines, selection off the stripes

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Роздільники в дереві архіву більше не їздять разом із курсором.
2. Рамка вибору не наїжджає на золоті смуги зверху/знизу списку дій — відступ з обох боків.

## Попередній delivery: v0.13.549 — Zip extract: smaller checks, X/Y like file browser, no "new folder" row

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Чекбокси дерева — той самий `drawCheckbox`, трохи менші (16px).
2. **X** — позначити поточний рядок, **Y** — інвертувати вибір (як у файловому браузері).
3. Прибрано «Розпакувати в нову папку…». Нову папку створюєш у Options огляду («Extract files to…»), перший рядок обирає поточну.

## Попередній delivery: v0.13.548 — Auto-forwarder deletes our previous HOME icon

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Новий Kefir Hub форвардер більше не сідає поруч зі старим: інші ікони Kefir Hub/Sphaira з префіксом 0x05 прибираються, щойно є поточний.
2. Запуск зі старого нашого ікона — це не «вже новий»: ставиться поточний Title ID; той, з якого зайшли, зніметься наступного разу (його не можна видалити, поки він запущений).
3. args більше не дублюються — hash Title ID збігається з тим, що ставить install_forwarder.

## Попередній delivery: v0.13.547 — After installing an app from zip: launch, don't open the folder

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Після «Встановити програму» лишається Keep/Delete zip, друге питання — запустити додаток, не файловий браузер.

## Попередній delivery: v0.13.546 — Full-screen zip extract: tree, checkboxes, named folder

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Після Direct Download zip — повноекранне меню, не смужка знизу. Зверху дерево архіву з галочками (Y — усі/жоден).
2. Якщо в архіві один `.nro`: пояснення, що **програму** буде встановлено в `/switch/<назва>`; дія «Встановити програму в папку switch».
3. Розпакування: файли в `/downloads`; у нову папку `/downloads/<ім'я архіву>`; файли в обрану папку; у нову папку всередині обраної (ім'я за замовченням — як у zip).

## Попередній delivery: v0.13.545 — Forwarder NACP video capture Manual, not Auto

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. `nacp.video_capture` 0x1 (Manual), не 0x2 (Auto). Auto валив `am` 2128-0007 при запуску ікона з HOME.

## Попередній delivery: v0.13.544 — OptionBox: glyph is part of the caption

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Гліф знову в одному рядку з підписом, весь напис по центру. Шрифт 26px, менше лише якщо не влазить.

## Попередній delivery: v0.13.543 — Center OptionBox buttons, honest forwarder notices

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Кнопки OptionBox знову по центру; гліф фіксований; шрифт 26, зменшення лише якщо не влазить.
2. Повідомлення автофорвардера пояснюють навіщо і говорять, що іконка/видалення вже виконуються, не «якщо треба».

## Попередній delivery: v0.13.542 — Forwarder capture, URL scheme collapse, friendly download errors

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Автофорвардер знову дозволяє скріншоти і відео (для дебагу).
2. Подвійні `http://`/`https://` у Direct Download згортаються (поле в браузері і обробка на консолі).
3. Поганий або недоступний URL — звичайне пояснення і кнопка «Виправити URL» (клавіатура консолі або знову з ПК).

## Попередній delivery: v0.13.541 — Zip: "Install NRO to /switch" label

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Якщо в zip один `.nro`, перша дія називається «Встановити NRO в папку switch», а не довгим шляхом.

## Попередній delivery: v0.13.540 — OptionBox: wrap long button labels, keep + glyph fixed

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Довгі підписи кнопок OptionBox (наприклад «Відкрити у файловому браузері») більше не вилазять за край.
2. Іконка + / B лишається зліва без прокрутки; текст зменшується і переноситься на два рядки.

## Попередній delivery: v0.13.539 — Zip extract: one NRO → /switch/stem, no root option

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. Якщо в архіві рівно один `.nro` (будь-яка глибина тек) — перша дія: покласти **цей файл** у `/switch/<назва без .nro>/<файл.nro>`. Рядок показує шлях у zip і куди ляже.
2. Завжди: «Extract all to /downloads» (зі списком кореня архіву: `atmosphere/, switch/, …`) і «Extract to...».
3. Окремої «Extract to root» немає. Корінь SD лише якщо користувач сам обере його в огляді.

## Попередній delivery: v0.13.538 — Remote input: no Paste on desktop

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. **Desktop**: кнопку Paste прибрано. Під полем інструкція «Paste or type the address, then Send».
2. **Phone**: кнопку залишено — фокус поля і long-press Paste (Clipboard API на HTTP усе одно не працює).

## Попередній delivery: v0.13.537 — GameCard row, dead install screens, zip extract defaults

Статус: програмну частину реалізовано (SW-DONE / HW-PENDING). Агент не компілює.
1. **Games / GameCard**: картридж як окремий рядок у списку ігор, синя обводка на іконці, підпис `[GC]`.
2. **A3 — мертві екрани**: видалено IRS, `firmware_menu`, екрани FTP/MTP Install. FTP/MTP лишились сервісами.
3. **Header**: версія Kefir над IP/Wi-Fi; повні підписи SysNAND/EmuNAND і microSD, бейджі по центру.
4. **Auto-forwarder**: план за джерелом запуску (новий / старий / Album); не чіпати title, з якого зайшли; системний TID більше не «завжди старий».
5. **Remote input Paste**: на HTTP LAN Clipboard API недоступний — без червоної помилки, фокус поля і Ctrl+V/Cmd+V.
6. **Custom Links / Direct Download zip**: після завантаження — огляд структури архіву, дефолтний шлях, Browse / Cancel, Keep/Delete zip, відкрити файловий браузер. Голе `.nro` → `/switch/<stem>/<file>`; NRO в теці → `/switch` (тека вже `switch` → `/`); кілька кореневих тек → `/downloads/<stem>`; інакше `/downloads`. Zip скачується в `/downloads`.
7. **Тести**: `tests/test_game_list_info.cpp`, `tests/test_zip_extract_plan.cpp`, оновлені forwarder/header тести. Агент їх не запускав.

## Попередній delivery: v0.13.536 — Fix: nacp_util::GetName in forwarder_auto_install.cpp

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Виправлення звернення до функції `nacp_util`**:
   - Замінено неіснуючий `nacp_util::GetTitle` на `nacp_util::GetName(control_data->nacp)`.
2. **Верифікація**:
   - Пройдено всі 25 наборів unit-тестів у WSL (all green).

## Попередній delivery: v0.13.535 — Auto-Forwarder: Fast-Path Check & User Files Protection

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Миттєва перевірка наявності форвардера KefirHub (Fast Path)**:
   - Спочатку виконується виклик `nsIsAnyApplicationEntityInstalled` для `kefirhub_tid`. Якщо форвардер уже встановлений, потік миттєво завершує роботу (0.1 мс), не зачіпаючи базу ігор і не проводячи жодних операцій.
2. **Захист файлів користувача**:
   - Повністю виключено будь-які операції з папкою `/Games` чи файлами на SD-картці.
3. **Очищення лише за відсутності форвардера**:
   - Лише якщо форвардер KefirHub відсутній, видаляються застарілі форвардери HBL/HBM та встановлюється форвардер KefirHub.
4. **Верифікація**:
   - Пройдено всі 25 наборів unit-тестів у WSL (all green).

## Попередній delivery: v0.13.534 — Auto-Forwarder: Legacy HBL Forwarder Deletion & Native KefirHub Generator

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Видалення застарілих форвардерів та файлів**:
   - Автоматичне видалення старих NSP із `/Games` (`Homebrew menu*.nsp`, `hbmenu*.nsp`, `hblauncher*.nsp`).
   - Видалення застарілих встановлених форвардерів за Title ID (`03DB1280BD84000`, `03DB12780BD84000`, `010000000000100D`, `050000000000100D`) та за назвами через `nsListApplicationRecord` і `nsDeleteApplicationCompletely`.
2. **Внутрішня генерація форвардера KefirHub**:
   - Безшумна генерація форвардера через `owo` із параметрами: 39-бітний адресний простір, вимкнені знімки/відео, вимкнений вибір профілю та вимкнений дебаг.
3. **Unit-тестування**:
   - `tests/test_forwarder_auto_lifecycle.cpp` (47 checks).
4. **Верифікація**:
   - Пройдено всі 25 наборів unit-тестів у WSL (all green).

## Попередній delivery: v0.13.533 — Auto-Forwarder Thread Lifecycle: Guaranteed threadClose & Application Bypass

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Розділення станів життєвого циклу потоку (`sphaira/source/forwarder_auto_install.cpp`)**:
   - `g_thread_created` відстежує необхідність виклику `threadClose()`.
   - `g_thread_active` відстежує активність функції потоку.
   - Встановлено детерміноване очищення ресурсів у `StopCheck()`: `threadWaitForExit()` і `threadClose()` викликаються завжди, коли потік створювався.
2. **Пропуск створення потоку в режимі Application**:
   - У `StartCheck()` додано ранню перевірку `App::IsApplication()`, що запобігає виділенню зайвих 64 KiB пам'яті під стек.
3. **Unit-тестування**:
   - Створено `tests/test_forwarder_auto_lifecycle.cpp` (19 checks).
4. **Верифікація**:
   - Пройдено всі 25 наборів unit-тестів у WSL (all green).

## Попередній delivery: v0.13.532 — HBL Loader: 64-bit Integer-Safe NRO Bounds & Pre-Body Read Validation

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **64-бітний розрахунок меж data+BSS (`hbl/source/main.c`)**:
   - Переведено всі арифметичні операції для `seg2_size + bss_size`, вирівнювання за сторінками та зміщень сегментів на тип `u64`.
   - Додано перевірки переповнення суми, переповнення вирівнювання та перевірку `seg2_off + rw_size <= g_heapSize`.
2. **Рання валідація меж до зчитування тіла NRO**:
   - Усі перевірки валідності заголовка, сегментів та розмірів коду/купи перенесено безпосередньо перед зчитуванням залишку NRO з SD-карти.
3. **Host unit-тести (`tests/test_hbl_nro_reader.cpp`)**:
   - Додано `CheckRwSizeBounds` та перевірки на перехоплення 32-бітного обгортання.
4. **Верифікація**:
   - Пройдено всі unit-тести у WSL.

## Попередній delivery: v0.13.531 — HBL Loader: Validated Contiguous OverrideHeap & Checked NRO Bounds

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Валідація неперервної купи в HBL (`hbl/source/main.c`)**:
   - Додано `findUsableHeapRange`: сканування через `svcQueryMemory` для пошуку найбільшого валідного неперервного діапазону з `MemType_Heap`, `Perm_Rw`, `attr == 0` та вирівнюванням по 4 КБ.
   - Усунуто передачу сирого арифметичного діапазону, що міг включати недоступні сторінки або чужі буфери.
2. **Перевірка меж NRO та BSS**:
   - Замінено застарілий TODO на строгі перевірки `total_size <= g_heapSize`, `rw_size` та захист від цілочисельного переповнення перед читанням та відображенням пам'яті.
3. **Host unit-тести**:
   - Розширено `tests/test_hbl_nro_reader.cpp` з моделюванням дірок у пам'яті та граничних випадків розмірів NRO.
4. **Верифікація**:
   - Успішно пройдено всі unit-тести у WSL, зібрано цільовий бінарник.

## Попередній delivery: v0.13.530 — NRO Launch Handoff: Clean envSetNextLoad & Redundant FS Commit Removal

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Аналіз покрокової бісекції (v0.13.469 -> v0.13.487)**:
   - Встановлено, що у версії `v0.13.469` (`1bb99c4`) запуск дочірніх NRO (включаючи `NX-Activity-Log` та `Pipe NSX`) працював стабільно.
   - У коміті `v0.13.487` (`32f655b`) в `launch_internal` перед `envSetNextLoad` було додано подвійний FS commit: `fsdevCommitDevice("sdmc")` та `fsFsCommit(fs)`.
2. **Очищення handoff-ланцюжка в NRO (`sphaira/source/nro.cpp`)**:
   - Вилучено передчасні виклики `fsdevCommitDevice` та `fsFsCommit` безпосередньо перед `envSetNextLoad` та `evman::push`.
3. **Очищення виходу програми (`sphaira/source/main.cpp`)**:
   - Усунуто дублювання `fsFsCommit` після `fsdevCommitDevice("sdmc")` у `userAppExit()`.
4. **Верифікація**:
   - Успішно пройдено всі 24 набори unit-тестів у WSL (all green).

## Попередній delivery: v0.13.529 — Forwarder Editor: List Null Pointer Safety & D-Pad Focus Transitions

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Аналіз краш-звітів на карті пам'яті (`F:\atmosphere\crash_reports`)**:
   - `01787411683_03db12780bd84000.log`, `01787411672_03db12780bd84000.log` та `01787411496_03db12780bd84000.log`.
   - Виявлено розіменування нульового покажчика `x24` (`controller`) у `sphaira::ui::List::OnUpdateGrid` на адресі `PC = sphaira + 0xf08f4`, коли `Forwarder Editor` викликав `m_list->OnUpdate(nullptr, ...)` при фокусі на іконці.
2. **Захист покажчиків у List (`sphaira/source/ui/list.cpp`)**:
   - Додано перевірки `if (controller)` перед зверненням до методів контролера у `OnUpdateGrid` та `OnUpdateHome`.
   - Додано захист `if (!touch)` у `StepFling` та `OnTouchScroll`.
3. **Навігація фокусу D-Pad у Forwarder Editor (`sphaira/source/ui/forwarder_editor.cpp`)**:
   - Додано перехід фокусу `DOWN` / `RIGHT` з іконки до списку налаштувань, та `LEFT` / `UP` (з першого рядка) назад на іконку.
4. **Host unit-тести та верифікація**:
   - Створено `tests/test_list_null_safety.cpp` (6 перевірок).
   - Пройдено всі 24 набори unit-тестів у WSL.

## Попередній delivery: v0.13.528 — HBL Loader Fix: Exact NRO Segment Sizing, Applet/Application Mode Detection & Heap Restoration

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Аналіз краш-звітів на карті пам'яті (`F:\atmosphere\crash_reports`)**:
   - `01787404697_010000000000100d.log` (Album mode) та `01787404688_03db12780bd84000.log` (Forwarder mode).
   - Виявлено: у режимі Альбому передавався хибний `AppletType_SystemApplication`, провокуючи виділення 64.5 МБ у 32 МБ пам'яті аплету; у форвардері функція `calculateMaxHeapSize` безумовно крала 96 МБ купи (`size -= 0x6000000`), що спричиняло збій на адресі `0x25fb8f000`.
2. **Динамічне визначення типу додатку та відновлення розміру купи (`hbl/source/main.c`)**:
   - Реалізовано `getIsApplication()` (запит до ядра через `svcGetInfo(..., InfoType_IsApplication)` та `pm:shell`).
   - Реалізовано `getIsAutomaticGameplayRecording()` (через `nsGetApplicationControlData`), усунуто безпідставне урізання 96 МБ для хоумбрю.
   - Динамічно передається `AppletType_LibraryApplet` (в альбомі) або `AppletType_SystemApplication` (у форвардері/тайтлі).
3. **Детерміноване читання NRO та обнулення BSS (`hbl/source/main.c`)**:
   - Читання `NroStart` (16 байт), `NroHeader` (112 байт) та виключно корисного навантаження `header->size - 0x80`.
   - Явне обнулення пам'яті BSS (`memset(nrobuf + header->size, 0, total_size - header->size)`).
4. **Звільнення графічних ресурсів GPU (`sphaira/source/main.cpp`)**:
   - Додано `nvExit()` до `userAppExit()` для чистого закриття сесій Tegra перед стартом наступного NRO.
5. **Host unit-тести та верифікація**:
   - Створено та розширено `tests/test_hbl_nro_reader.cpp` (528,394 перевірки).

## Попередній delivery: v0.13.527 — Software Menu Visual Separation: Dedicated Bottom Network Section

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Виділення секції мережевих завантажень у Software Menu (`sphaira/source/ui/menus/settings_menu.cpp`)**:
   - `Network Downloads` та `Custom Link` перенесено в самий низ списку.
   - Додано чіткий розділювач `MakeHeader("NETWORK DOWNLOADS")` з горизонтальною лінією (HR).
   - `DrawActionListItem` підтримує відмальовку заголовків, а `SoftwareMenu::SetIndex` пропускає неінтерактивні рядки.

## Попередній delivery: v0.13.526 — Menu Structure: Network Downloads & Custom Link to Software Menu

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Очищення меню оновлень Updater (`sphaira/source/ui/menus/kefir_menu.cpp`)**:
   - Меню `Updater` звільнено від мережевих завантажень GitHub/Custom Link і сфокусовано виключно на KEFIR та FIRMWARE.
2. **Перенесення до Software Menu (`sphaira/source/ui/menus/settings_menu.cpp`)**:
   - Додано `Network Downloads` та `Custom Link` безпосередньо в список додаткових програм `Software`.
3. **Оновлення описів карток у Tools Menu (`sphaira/source/ui/menus/tools_menu.cpp`)**:
   - Актуалізовано підписи карток `Updater` та `Software`.

## Попередній delivery: v0.13.525 — Graceful download cancellation & universal remote text/URL transfer

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Коректна обробка скасування в AppStore (`sphaira/source/ui/menus/appstore.cpp`)**:
   - Реалізовано перехоплення `Result_TransferCancelled` у `InstallApp` та `EntryMenu::UpdateOptions` з виведенням спокійного інформаційного діалогу замість червоної аварійної помилки.
2. **Універсальний модуль дистанційного введення тексту `ui::remote_input` (`sphaira/include/ui/remote_input.hpp`, `sphaira/source/ui/remote_input.cpp`, `sphaira/source/web.cpp`)**:
   - Реалізовано вибір між клавіатурою Switch та передачею з телефону/ПК через QR-код і локальний веб-ендпоінт `/input`.
3. **Пряме завантаження `.nro` та `.zip` (`sphaira/source/ui/menus/ghdl.cpp`, `sphaira/include/path_util.hpp`)**:
   - Розширено валідатор `IsValidDirectDownloadUrl` та реалізовано збереження `.nro` у `/switch/` із можливістю негайного запуску.

## Попередній delivery: v0.13.524 — USB 3.0 indicator, waiting screen anti-overlap layout & screensaver clean title display

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Індикатор USB 3.0 та швидкість з'єднання (`sphaira/include/ui/nvg_util.hpp`, `sphaira/source/ui/nvg_util.cpp`, `sphaira/source/ui/menus/menu_base.cpp`, `sphaira/source/ui/menus/dbi_menu.cpp`)**:
   - Створено функцію малювання векторної іконки USB `gfx::drawUsbIcon(vg, x, y, size, col)` (стандартний тризуб з верхньою стрілкою, квадратною та круглою гілками).
   - Інтегровано перевірку конфігурації `usb30_force_enabled` з `system_settings.ini` та опитування апаратного лінку `usbDsGetSpeed(&speed)`.
   - У глобальному хедері `MenuBase::DrawChrome` над графіками пам'яті виведено бейдж `[ USB 3.0 ]` поряд із MTP та FTP.
   - На екрані очікування встановлення по USB (`dbi_menu.cpp`) розміщено статусний бейдж на `y = 180.f` з іконкою USB та деталізацією швидкості (`USB 3.0 SuperSpeed (5 Gbps)` або `USB 2.0 High Speed (480 Mbps)`).
2. **Динамічна розмітка екрану очікування без перекриттів (`sphaira/source/ui/menus/dbi_menu.cpp`)**:
   - Основні інструкції розміщено на `y = 250.f`, розрахунок висоти тексту виконується динамічно через `nvgTextBoxBounds`.
   - Попередження про Applet Mode винесено в окрему виділену плашку з м'яким бордером і гарантованим відступом строго нижче основного тексту (`std::max(text_bounds[3] + 35.f, 470.f)`), що виключає будь-яке налізання елементів.
3. **Чисте відображення назви гри у скрінсейвері (`sphaira/source/ui/menus/dbi_menu.cpp`, `sphaira/source/ui/screensaver.cpp`)**:
   - Усунуто додавання технічних хешів/імен NCA/NCZ файлів (`.nca`/`.ncz`) до назви гри, залишено лише змістовні статуси (наприклад, "Updating ncm database").
   - У `screensaver.cpp` розширено ширину блоку до 840 пікселів (з безпечним 50px OLED burn-in запасом) та додано плавне адаптивне зменшення шрифту з лівим прив'язуванням для дуже довгих назв, гарантуючи, що перші слова назви гри ніколи не зрізаються.
4. **Зменшення шрифту пам'яті та усунення налізання на рядок прошивки/Кефіру (`sphaira/source/ui/menus/menu_base.cpp`)**:
   - Розмір шрифту `storage_font` для міток `NAND`, `SD` та чисел пам'яті зменшено з 19.05px до 15.5px.
   - Позицію `badge_y` піднято до 17.f, що забезпечило комфортний вертикальний відступ і ліквідувало налізання верхніх країв літер пам'яті на рядок системної версії/Kefir.
5. **Статусний бейдж EmuNAND/SysNAND та 3-сторонній симетричний розподіл у хедері (`sphaira/source/ui/menus/menu_base.cpp`, `sphaira/source/hats_version.cpp`)**:
   - Створено статусний бейдж режиму NAND з адаптивним розгортанням (`EmuNAND` зеленого кольору в EmuNAND при наявності місця, `E` при обмеженому просторі з USB 3.0; аналогічно `SysNAND`/`S`).
   - Усунуто дублювання `|E`/`|S` наприкінці системного рядка версії Atmosphere/Kefir.
   - Інформацію згруповано у два блоки: Блок 1 (бейджі MTP, FTP, USB 3.0, EmuNAND/E) та Блок 2 (версія Кефіру та ОС).
   - Реалізовано динамічний розрахунок рівних інтервалів: відстань від лівого краю сховища до Блоку 1, відстань між Блоком 1 і Блоком 2, та відстань від Блоку 2 до правого краю сховища абсолютно однакові ($M = (W_{span} - (W_1 + W_2)) / 3$).
6. **Unit-тести та верифікація**:
   - Створено `tests/test_screensaver_title.cpp` (11 checks), `tests/test_usb3_indicator.cpp` (12 checks) та оновлено `tests/test_header_service_indicators.cpp` (31 checks).
   - Пройдено всі 22 набори host unit-тестів у WSL (`tests/run.sh`).
   - Успішно скомпільовано цільовий бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.523 — Header Kefir version, System OS Firmware & EmuNAND/SysNAND indicator

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Зчитування та форматування системної версії (`sphaira/include/hats_version.hpp`, `sphaira/source/hats_version.cpp`)**:
   - Реалізовано функцію `getKefirVersion()`, що перевіряє `/switch/kefir-updater/version` або `HATS_VERSION.txt`.
   - Реалізовано `getSystemVersionString()`, що формує рядок за форматом системних налаштувань Switch: `<Kefir> · <FW>|AMS <AMS>|<E/S>` (наприклад `Kefir 802 · 19.0.1|AMS 1.8.0|E` або `19.0.1|AMS 1.8.0|E`).
2. **Відображення у хедері (`sphaira/source/ui/menus/menu_base.cpp`)**:
   - Одноразово кешовано результат у `MenuBase::GetPolledData` (нульовий оверхед).
   - У `MenuBase::DrawChrome` рядок виводиться на висоті `y = 19.f` з правим вирівнюванням по осі `storage_right`, гармонійно доповнюючи бейджі MTP/FTP зліва.
3. **Unit-тести та верифікація**:
   - Оновлено `tests/test_header_service_indicators.cpp` (28 checks) з тестуванням усіх варіацій (Kefir + FW + AMS + EmuNAND/SysNAND).
   - Пройдено всі 20 наборів host unit-тестів у WSL (`tests/run.sh`).

## Попередній delivery: v0.13.522 — Exact NAND-edge boundary calculation & conditional anti-overlap marquee

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Точне позиціонування зони скролінгу від правого краю блоку NAND (`sphaira/source/ui/menus/menu_base.cpp`)**:
   - Розрахунок геометрії сховища перенесено перед відмальовуванням першого рядка (Wi-Fi/IP).
   - Обчислюється точна координата правого краю тексту пам'яті NAND: `nand_right = value_x + nand_val_w`.
   - Ліва межа доступного вікна мережі встановлюється як `net_left = nand_right + 12.f`, що дає максимально можливий простір до `bar_right`.
   - Якщо довжина рядка мережі `net_text_w > (bar_right - net_left)` (є нахльост на блок NAND), вмикається плавний біжучий рядок через `m_scroll_network.Draw` з відсіканням строго на `net_left`.
   - Якщо нахльосту немає (`net_text_w <= bar_right - net_left`), рядок відображається статично вирівняним праворуч без скролінгу.
2. **Unit-тести та збірка**:
   - Оновлено [**`tests/test_header_network_layout.cpp`**](tests/test_header_network_layout.cpp) з моделюванням точного розрахунку відступу від правого краю тексту NAND.
   - Пройдено всі 20 наборів host unit-тестів та обидва shape-checks у WSL (`tests/run.sh`).
   - Успішно скомпільовано цільовий бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.521 — Header MTP and FTP background service indicators above NAND/SD

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Інтеграція статусу фонових служб (`sphaira/include/ui/menus/menu_base.hpp`, `sphaira/source/ui/menus/menu_base.cpp`)**:
   - У структуру `PolledData` додано прапорці `bool mtp_running{}` та `bool ftp_running{}`.
   - Метод `MenuBase::GetPolledData` щосекунди оновлює статус служб через легкі виклики `sphaira::haze::IsRunning()` та `sphaira::ftpsrv::IsRunning()`.
2. **Геометричне розміщення та колірне кодування в інтерфейсі (`MenuBase::DrawChrome`)**:
   - Індикатори `MTP` та `FTP` розміщено над рядками сховищ NAND/SD на базовій лінії `y = 29.f` (`storage_mid - storage_gap * 1.5f`), що забезпечує ідеальний 20-піксельний вертикальний крок відносно рядка NAND (`y=49.f`) та SD (`y=69.f`).
   - Позиціонування починається від лівої межі блоку пам'яті (`label_x`), утворюючи єдину вертикальну вісь з підписами дисків.
   - Коли служба активна та слухає порт/з'єднання, відповідний напис забарвлюється у яскраво-зелений колір `nvgRGBA(76, 190, 120, 255)`; коли неактивна — у приглушений сірий колір теми `ThemeEntryID_TEXT_INFO`.
3. **Unit-тести та збірка**:
   - Створено набір unit-тестів [**`tests/test_header_service_indicators.cpp`**](tests/test_header_service_indicators.cpp) (20 checks: перевірка розрахунку координат, вертикального кроку, ширини та мапінгу кольорів).
   - Пройдено всі 20 наборів host unit-тестів та обидва shape-checks у WSL (`tests/run.sh`).
   - Успішно скомпільовано цільовий бінарник `sphaira_nro` у WSL з розгортанням на `F:\switch\kefir-hub.nro`.

## Попередній delivery: v0.13.520 — Header network SSID & IP anti-overlap scrolling marquee

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Ізоляція та скролінг мережевого статусу (`sphaira/include/ui/menus/menu_base.hpp`, `sphaira/source/ui/menus/menu_base.cpp`)**:
   - У клас `MenuBase` додано екземпляр `ScrollingText m_scroll_network`.
   - У `MenuBase::DrawChrome` обчислюються точні межі слота мережевого тексту: ліва межа `net_x = start_x` (що гарантує щонайменше 10 пікселів відстані від правого краю індикаторів NAND/SD пам'яті `storage_right`), права межа `bar_right = 1220.f`, ширина `net_w = bar_right - net_x`.
   - Якщо довжина рядка (SSID + IP) перевищує ширину виділеного слота, активується плавний скролінг через `m_scroll_network.Draw` з апаратним scissor-відсіканням. Довгі назви точок доступу більше ніколи не налізають на числа та смуги сховища NAND.
   - Якщо рядок уміщується в слот, він вирівнюється праворуч без скролінгу, зберігаючи естетичне вирівнювання з годинником та батареєю.
2. **Unit-тести та верифікація**:
   - Створено набір unit-тестів [**`tests/test_header_network_layout.cpp`**](tests/test_header_network_layout.cpp) (25 перевірок: форматування SSID/IP/LAN/No Internet, розрахунок меж слота, гарантія відсутності перекриття NAND/SD, активація скролінгу).
   - Пройдено всі 19 наборів host unit-тестів та обидва shape-checks у WSL (`tests/run.sh`).
   - Успішно скомпільовано цільовий бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.519 — AppStore EntryMenu layout anti-overlap & instant launch state transition

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Фіксація координат кнопок та блоку метаданих (`sphaira/source/ui/menus/appstore.cpp`)**:
   - Кнопки дій позиціонуються від нижнього краю робочої зони (`bottom_y = 630.f`), залишаючи 16 пікселів відступу від футера.
   - Метадані розміщені з інтервалом 26 пікселів (шрифт 18), що виключає перекриття або налізання на футер.
2. **Миттєве оновлення статусу та відображення версії RetroArch**:
   - У колбеку успішного завершення завантаження `install` прописується `m_entry.installed_version = "Nightly"` та статус `Installed`, що негайно активує кнопку «Запустити» (`Launch`).
   - При відкритті `EntryMenu` версія оновлюється з файлу `info.json` або зчитується з NACP бінарника.
3. **Збірка та деплой**:
   - Пройдено всі 18 наборів host unit-тестів та обидва shape-checks у WSL (`tests/run.sh`).
   - Зібрано версію `v0.13.519` та оновлено `kefir-hub.nro` і `hbmenu.nro` на диску `F:`.

## Попередній delivery: v0.13.518 — RetroArch 7z PhysFS stream extractor & Nightly MD5 bypass

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Підтримка `.7z` та пряме потокове розпакування (`sphaira/source/ui/menus/appstore.cpp`)**:
   - Інтегровано `physfs` (`libphysfs.a`) для обробки 7z-архівів.
   - Реалізовано функцію `ExtractPhysfsArchive`, яка рекурсивно створює каталоги та записує файли на карту пам'яті.
   - Пропущено MD5-перевірку для динамічних релізів RetroArch Nightly.
   - Реалізовано запис метаданих `info.json` (`"version": "Nightly"`).
2. **Збірка та деплой**:
   - Пройдено всі 18 наборів host unit-тестів та обидва shape-checks у WSL (`tests/run.sh`).
   - Зібрано версію `v0.13.518` та оновлено `kefir-hub.nro` і `hbmenu.nro` на диску `F:`.

## Попередній delivery: v0.13.517 — AppStore installed version display, clean network handover & LibRetro Nightly resolver

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Відображення встановленої версії в AppStore (`EntryMenu::Draw`)**:
   - Додано поле `installed_version` у структуру `Entry`.
   - Реалізовано зчитування встановленої версії з `info.json` та з заголовка NACP бінарника (`nacp_util::GetDisplayVersion`).
   - У меню картки додатка виводиться рядок `installed: <версія>` (підсвічується кольором теми, якщо є оновлення).
2. **Підміна джерела для RetroArch на LibRetro Nightly Buildbot (`appstore_util.hpp`)**:
   - Створено функції `IsRetroArchPackageName` та `ResolveAppstoreZipUrl`.
   - Завантаження RetroArch перенаправлено на актуальний офіційний білд `RetroArch.7z` з LibRetro Nightly.
   - Для застарілих версій RetroArch дія `Launch` замінюється на `Update`.
3. **Коректний порядок деініціалізації мережі (`sphaira/source/main.cpp`)**:
   - `socketExit()` тепер викликається строго перед `nifmExit()` у `userAppExit()`.
   - Додано 50 мс паузу перед `appletUnlockExit()` для повного очищення IPC-дескрипторів ядра Horizon OS.
4. **Збірка та деплой**:
   - Пройдено всі 18 наборів host unit-тестів та обидва shape-checks у WSL (`tests/run.sh`).
   - Успішно зібрано та скопійовано бінарники `kefir-hub.nro` і `hbmenu.nro` на карту пам'яті `F:`.

## Попередній delivery: v0.13.516 — AppStore EntryMenu launch confirmation guard

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Захист від випадкового запуску NRO у меню AppStore (`sphaira/source/ui/menus/appstore.cpp`)**:
   - Додано діалогове вікно підтвердження `OptionBox` ("Launch [App]? No / Yes") для дії запуску `Launch` в `EntryMenu`.
   - Запобігає автоматичному / миттєвому закриттю Sphaira та запуску сторонніх застосунків при відкритті інформаційної картки вже встановленого додатка у магазині.
2. **Збірка та деплой**:
   - Пройдено всі 17 наборів host unit-тестів та обидва shape-checks у WSL (`tests/run.sh`).
   - Успішно зібрано та скопійовано бінарник `kefir-hub.nro` на карту пам'яті `F:`.

## Попередній delivery: v0.13.515 — UPA-13: Confirmed ROM database compatibility aliases

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Розширення таблиці асоціацій баз даних ROM (`sphaira/include/ui/menus/filebrowser_assoc.hpp`)**:
   - Додано базу даних `NEC - PC Engine SuperGrafx` (папки `supergrafx`, `pce-sg`, `pcesg`), підтверджену конфігурацією ядер Mednafen у RomFS.
   - Додано взаємне співставлення для `Nintendo - Family Computer Disk System` та `Nintendo - Famicom Disk System` (папка `fds`).
   - Додано базовий запис `SNK - Neo Geo` (папка `neogeo`) поряд із Pocket/Color/CD.
2. **Unit-тести та збірка**:
   - Розширено unit-тести [**`tests/test_tico_assoc.cpp`**](tests/test_tico_assoc.cpp) (20 checks passed).
   - Пройдено всі 17 наборів host unit-тестів та обидва shape-checks у WSL (`tests/run.sh`).
   - Успішно зібрано цільовий бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.514 — UPA-11: GameCard theme roles & safe storage ratio

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Безпечне обчислення коефіцієнтів сховища (`sphaira/include/storage_ratio.hpp`)**:
   - Створено функції `CalculateStorageUsedRatio` та `CalculateStorageFreeGb` з гарантованим захистом від ділення на нуль, `total <= 0`, `free < 0` та `free > total`.
2. **Оновлення інтерфейсу меню GameCard (`sphaira/source/ui/menus/gc_menu.cpp`)**:
   - У `Menu::Draw` для фону смуг використано `ThemeEntryID_PROGRESSBAR_BACKGROUND` замість `ThemeEntryID_BACKGROUND`, що забезпечує коректний вигляд у темах зі складним або зображувальним фоном.
   - У `Menu::UpdateStorageSize` додано обнулення змінних перед опитуванням сховищ.
3. **Unit-тести та збірка**:
   - Створено unit-тести [**`tests/test_storage_ratio.cpp`**](tests/test_storage_ratio.cpp) (14 checks passed).
   - Пройдено всі 17 наборів host unit-тестів та обидва shape-checks у WSL (`tests/run.sh`).
   - Успішно зібрано цільовий бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.513 — UPA-10B: Localized UTF-8 MTP display names

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Збереження Unicode у MTP папках (`sphaira/include/title_export_name.hpp`, `sphaira/source/haze_helper.cpp`)**:
   - Реалізовано UTF-8 санітизацію (`SanitizeUtf8TitleName`) та безпечну транкацію на межі code point (`TruncateUtf8`).
   - `FormatMtpGameDirName` зберігає локалізовані назви (кирилиця, українські/європейські літери, ієрогліфи, емодзі) та гарантує збереження суфікса `[TitleID]`.
   - Забезпечено коректний fallback: Localized name -> English slot 0 -> English slot 1 -> Title ID.
2. **Unit-тести та збірка**:
   - Розширено [**`tests/test_title_export_name.cpp`**](tests/test_title_export_name.cpp) тестами Unicode/кирилиці, емодзі, безпечної UTF-8 транкації та MTP фолбеків (42 checks passed).
   - Пройдено всі 16 наборів host unit-тестів та обидва shape-checks у WSL (`tests/run.sh`).
   - Успішно зібрано цільовий бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.512 — UPA-10A: Tested usable-title core & ASCII-safe NSP export helper

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Ядро визначення придатної назви та NSP експорт (`sphaira/include/title_export_name.hpp`)**:
   - Реалізовано єдиний помічник `ResolveExportTitleName` із суворою ієрархією: American English (slot 0) -> British English (slot 1) -> Localized/Current name -> Title ID hex fallback (`%016llX`).
   - Додано семантичний валідатор `IsUsableTitleName` (відхиляє порожні та рядки з одних пробілів, крапок або підкреслень після санітизації).
   - Забезпечено коректне скорочення (`TruncateTitleName`) з гарантованим запасом місця під суфікс `[TitleID][vVersion][Type].nsp`.
2. **Інтеграція в експорт NSP (`sphaira/source/title_nsp.cpp`)**:
   - `BuildNspPath` та `BuildMergedNspEntry` використовують єдиний перевірений хелпер.
3. **Unit-тести та збірка**:
   - Створено автономний набір host unit-тестів [**`tests/test_title_export_name.cpp`**](tests/test_title_export_name.cpp) (24 перевірки).
   - Пройдено всі 16 наборів host unit-тестів та обидва shape-checks у WSL (`tests/run.sh`).
   - Успішно зібрано цільовий бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.511 — UPA-09: Forwarder editor touch/controller focus matrix

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Виправлення матриці фокусу сенсора та контролера (`sphaira/source/ui/forwarder_editor.cpp`)**:
   - Усунуто безумовний вихід з `Update` при `m_icon_focused`: тепер сенсорні події для правого списку обробляються через `m_list->OnUpdate(nullptr, touch, ...)`.
   - Дотик або скролінг списку переносить фокус з іконки на рядок без хибних активацій.
   - Кнопка `RIGHT` переносить фокус на список без виклику дії рядка, кнопка `LEFT` повертає фокус на іконку.
   - Кнопка `A` активує лише активний елемент (іконку або вибраний рядок списку).
2. **Unit-тести та збірка**:
   - Пройдено всі 15 наборів host unit-тестів та shape-checks у WSL (`tests/run.sh`).
   - Успішно зібрано цільовий бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.510 — UPA-08A/B: Raw FTP mutation adapter & discovery gate

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Дизайн та фіксація точок інтеграції (`UPA-08A`)**:
   - Визначено точки успішного завершення операцій у `src/platform/nx/vfs/vfs_nx_fs.c` (`vfs_fs_close`, `vfs_fs_unlink`, `vfs_fs_rmdir`, `vfs_fs_mkdir`, `vfs_fs_rename`).
   - Підтверджено потокобезпечність викликів сповіщень з worker-потоку ftpsrv (`ueventSignal`).
2. **Адаптер мутацій у ftpsrv (`sphaira/cmake/patch_ftpsrv.cmake`, `sphaira/source/ftpsrv_helper.cpp`)**:
   - Додано C ABI інтерфейс `vfs_nx_set_mutation_callback` для відправлення сповіщень про події створення/видалення/перейменування файлів та папок.
   - Підключено обробник `FtpMutationCallback` у `ftpsrv_helper.cpp` до спільної політики Homebrew (`NotifyFileCreated`, `NotifyFileDeleted`, `NotifyDirectoryCreated`, `NotifyDirectoryDeleted`, `NotifyRename`).
3. **Unit-тести, shape-checks та збірка**:
   - Створено автономний перевірочний скрипт `tests/test_patch_ftpsrv.sh` та підключено його до `tests/run.sh`.
   - Пройдено всі 15 наборів host unit-тестів та обидва shape-checks (libhaze + ftpsrv).
   - Успішно зібрано цільовий бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.509 — UPA-07B: MTP delete/rename/directory operations mutation coverage

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Повне покриття мутацій у MTP VFS (`sphaira/source/haze_helper.cpp`)**:
   - `DeleteFile`: після успішного видалення та коміту надсилає `NotifyFileDeleted(routed_path.s)`.
   - `RenameFile`: після успішного перейменування надсилає `NotifyRename(routed_old.s, routed_new.s, false)`.
   - `CreateDirectory`: після створення директорії надсилає `NotifyDirectoryCreated(fixed_path)`.
   - `DeleteDirectoryRecursively`: після рекурсивного видалення надсилає `NotifyDirectoryDeleted(fixed_path)`.
   - `RenameDirectory`: після перейменування директорії надсилає `NotifyRename(fixed_old, fixed_new, true)`.
2. **Точність та детермінізм**:
   - Жодна операція, що завершилася з помилкою, не викликає сповіщення.
   - Усі шляхи оцінюються через спільну політику, захищаючи від непотрібних оновлень поза межами `/switch` та кастомних search roots.
3. **Unit-тести та збірка**:
   - Пройдено всі 15 наборів host unit-тестів та shape-check у WSL (`tests/run.sh`).
   - Успішно зібрано цільовий бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.508 — UPA-07A: MTP upload/final-close shared mutation policy integration

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Інтеграція спільної політики мутацій у MTP VFS (`sphaira/source/haze_helper.cpp`)**:
   - `FsProxy` веде облік відкритих на запис файлів `m_open_write_files` (`std::map<fs::File*, std::string>`), зберігаючи маршрутизований шлях `routed_path.s`.
   - Замінено глобальний прапорець `m_notify_homebrew`: сповіщення `ui::menu::homebrew::NotifyFileCreated(written_path)` тепер надсилається суворо після успішного закриття файлу `CloseFile()` і тільки для файлів, які зачіпають каталог Homebrew.
2. **Підтримка прямих та перенаправлених записів**:
   - Покриваються прямі завантаження в `/switch`, вкладені папки, редиректи з кореня та довільні кастомні search roots.
   - Не-homebrew файли (наприклад, `.mp4`, `.nsp`, `.sav`) ігноруються автоматично без зайвих сканувань меню.
3. **Unit-тести та збірка**:
   - Пройдено всі 15 наборів host unit-тестів та shape-check у WSL (`tests/run.sh`).
   - Успішно зібрано цільовий бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.507 — UPA-06: Shared homebrew mutation policy & complete Web success coverage

Статус: реалізацію виконано та верифіковано:
1. **Спільна політика мутацій Homebrew (`sphaira/include/path_util.hpp`, `sphaira/source/ui/menus/homebrew.cpp`)**:
   - Реалізовано перевірки меж шляхів `path::IsSubpathOf`, розширення `path::IsNroPath` та визначення впливу на каталог `path::PathAffectsHomebrew` (з урахуванням дефолтного `/switch` та кастомних директорій пошуку).
   - Додано функції сповіщення про створення, видалення та перейменування файлів і директорій (`NotifyFileCreated`, `NotifyFileDeleted`, `NotifyDirectoryCreated`, `NotifyDirectoryDeleted`, `NotifyRename`, `NotifyPathChanged`).
2. **Інтеграція у Web сервер (`sphaira/source/web.cpp`)**:
   - `HandleUpload`: викликає `NotifyFileCreated` після фіналізації та коміту файлу.
   - `HandleDelete`: викликає `NotifyDirectoryDeleted` або `NotifyFileDeleted` після успішного видалення файлу/каталогу на SD карті.
3. **Unit-тести та збірка**:
   - Додано 53 перевірки в `tests/test_path_util.cpp` (283 checks passed).
   - Успішно зібрано бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.506 — UPA-05: Playtime worker UI-thread isolation & race elimination

Статус: реалізацію виконано та верифіковано:
1. **Ізоляція фонового воркера від UI даних (`sphaira/source/ui/menus/game_menu.cpp`)**:
   - `Menu::LoadPlaytime()` створює знімок `app_ids` та окремий контейнер `PlaytimeResult` перед запуском `ProgressBox`.
   - Воркер взаємодіє виключно з цими структурами, повністю усунувши стан гонитви з рендером та іншими UI-потоками.
2. **Детерміноване застосування результатів**:
   - Результати з буфера воркера записуються в `m_entries` виключно в колбеку `done` на UI thread і тільки у разі успішного завершення операції (`R_SUCCEEDED(rc)`).
   - У разі скасування або помилки зміни не застосовуються до інтерфейсу.
3. **Unit-тести та збірка**:
   - Пройдено всі 15 наборів host unit-тестів та shape-check у WSL (`tests/run.sh`).
   - Успішно зібрано бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.505 — UPA-04A: MTP zero-byte upload support & patch shape verification

Статус: програмну частину реалізовано та верифіковано (SW-DONE / HW-PENDING):
1. **Zero-byte payload підтримка в libhaze (`sphaira/cmake/patch_libhaze.cmake`)**:
   - Уточнено умову розрахунку `file_size` під час `SendObject`: змінено `data_header.length > sizeof(PtpUsbBulkContainer)` на `>= sizeof(PtpUsbBulkContainer)`.
   - Забезпечено коректне встановлення нульового розміру файлу замість залишення fallback-розміру `4_GB`.
2. **Ідемпотентність та shape-check падінь**:
   - Патч підтримує повторне застосування та міграцію з проміжних версій патчу.
   - Створено ізольований перевірочний тест `tests/test_patch_libhaze.sh`, що перевіряє застосування патчу до вихідного коду, ідемпотентність при повторному запуску та завершення з очікуваною помилкою при спотвореній формі.
3. **Unit-тести та збірка**:
   - Підключено перевірку форми патча в `tests/run.sh` (всі тести зелені).
   - Успішно зібрано цільовий бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.504 — UPA-03: Centralized GitHub and direct URL validation

Статус: реалізацію виконано та верифіковано:
1. **Централізована валідація URL GitHub (`sphaira/include/path_util.hpp`)**:
   - Реалізовано `path::ParseGitHubRepoUrl(url)`: перевіряє схему (http/https), хост (github.com / www.github.com), відсікає `.git` та trailing slash, вимагає валідні ідентифікатори owner/repo без спецсимволів, відхиляє userinfo, порти, параметри запиту, фрагменти та directory traversal.
2. **Валідація прямих посилань та ZIP-файлів**:
   - Реалізовано `path::IsValidDirectAssetUrl(url)` та `path::IsValidDirectZipUrl(url)`.
   - Оновлено `LoadEntriesFromPath`, `Download`, та `OpenDirectLinkPrompt` у `sphaira/source/ui/menus/ghdl.cpp`.
3. **Unit-тести та збірка**:
   - Покрито повним набором тестів у `tests/test_path_util.cpp` (230 checks passed).
   - Успішно зібрано бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.503 — UPA-02B: GHDL ZIP type detection & safe non-ZIP destination

Статус: реалізацію виконано та верифіковано:
1. **Комплексне визначення ZIP-архівів (`sphaira/include/path_util.hpp`, `sphaira/source/ui/menus/ghdl.cpp`)**:
   - Реалізовано `path::IsZipAsset(content_type, filename, url)`: визначає ZIP за наявністю підрядка `"zip"` у `content_type`, суфікса `.zip` у назві файлу або в шляху URL (ігноруючи параметри запиту `?` та фрагменти `#`).
2. **Безпечне встановлення не-ZIP ассетів**:
   - Для файлів без явної конфігурації `entry.path` призначається безпечна цільова директорія `/switch/<sanitized-asset-name>` (замість небезпечного перезапису/видалення кореня `/`).
   - Якщо `entry.path` вказує на директорію, до неї коректно дописується ім'я файлу.
   - Додано перевірку валідності імені файлу `path::IsSafeFilename` та вилучення імені `path::ExtractBasename`.
3. **Unit-тести та збірка**:
   - Додано нові тести у `tests/test_path_util.cpp` (193 checks passed).
   - Успішно зібрано цільовий бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.502 — UPA-02A: GitHub downloader operation identity, cancel & temp isolation

Статус: реалізацію виконано та верифіковано:
1. **Ізоляція та очищення тимчасових файлів (`sphaira/source/ui/menus/ghdl.cpp`)**:
   - Перед стартом мережевої передачі та при завершенні (включно зі збоями/скасуванням через `ON_SCOPE_EXIT`) детерміновано видаляються тимчасові файли `ghdl.temp` та `direct_link.zip`.
   - Унеможливлено використання застарілих даних від попередніх або перерваних операцій завантаження.
2. **Фазовий контроль скасування (Phase Gates)**:
   - Додано строгі перевірки `pbox->ShouldExit()` перед початком завантаження через curl, після його завершення перед модифікацією файлової системи, а також перед викликом розпакування/перейменування.
   - При скасуванні повертається стандартний `Result_TransferCancelled`, який коректно перехоплюється без показу помилкових діалогових вікон збоїв мережі.
3. **Строгий захист сигналу Homebrew**:
   - Виклик `homebrew::SignalChange()` перенесено строго в блок `if (R_SUCCEEDED(rc))`, усунувши помилкові перезавантаження каталогу при скасуванні або невдалому завантаженні.
4. **Збірка та тести**:
   - Пройдено всі 15 наборів host unit-тестів у WSL (`tests/run.sh`).
   - Успішно зібрано цільовий бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.501 — UPA-01: GitHub downloader callback ownership & selection safety

Статус: реалізацію виконано та верифіковано:
1. **Ліквідація global static стану (`sphaira/source/ui/menus/ghdl.cpp`)**:
   - `static std::vector<GhApiEntry> gh_entries` у `DownloadEntries()` замінено на операційно-локальний `auto gh_entries = std::make_shared<std::vector<GhApiEntry>>()`.
   - Тепер кілька послідовних запитів завантаження не можуть перетерти дані релізів один одного.
2. **Безпека володіння пам'яттю та усунення UAF / висячих посилань**:
   - Усунено небезпечне захоплення за посиланням `&asset_entry` та збереження сирих вказівників `const AssetEntry*` на елементи тимчасових векторів усередині відкладених лямбд (`PopupList`, `OptionBox`, `ProgressBox`).
   - Застосовано `std::optional<AssetEntry>` та безпечне захоплення параметрів за значенням (`[entry, asset_entry, matched]`).
3. **Захист від виходу за межі діапазону (Out-of-Bounds Guards)**:
   - Додано перевірки меж індексів `op_index` для вибору релізу (`!op_index || *op_index < 0 || static_cast<size_t>(*op_index) >= gh_entries->size()`) та ассету (`static_cast<size_t>(*op_index) >= api_assets.size()`).
   - Додано перевірку на порожній список ассетів з показом інформаційного вікна замість спроби розіменування.
4. **Збірка та тести**:
   - Пройдено всі 15 наборів host unit-тестів та dead symbol guard у WSL (`tests/run.sh`).
   - Успішно зібрано цільовий бінарник `sphaira_nro` у WSL.

## Попередній delivery: v0.13.500 — NexLink / DBI return crash

Статус: реалізацію виконано, переглянуто senior-review та прийнято апаратним тестуванням:
1. **Доказова діагностика**:
   - Найновіший report `F:\atmosphere\crash_reports\01787234167_03db12780bd84000.log` точно відповідає WSL ELF за Build ID `29AC83A4D4BFAD5E9014FC1C660A598627CDA520`.
   - У 17/20 звітів allocator виконує валідний split top-chunk і падає на `str x1, [x3,#8]`, причому `Exception Address == X3 + 8`; `ScanThemes`, `opendir` та декодери зображень є лише першими алокаціями, що доходять до недоступної сторінки.
   - Решта 3/20 звітів належать окремому старому stack overflow логера, усуненому у v0.13.491.
2. **Спільний root-boundary fix**:
   - Додати strong `__libnx_initheap()` без алокацій: пройти loader-provided OverrideHeap через `svcQueryMemory`, відкинути успадковані `Perm_None` / `IsBorrowed` діапазони та передати newlib найбільший суцільний `MemType_Heap + Perm_Rw + attr == 0` сегмент.
   - Для запуску без OverrideHeap дослівно зберегти стандартний libnx fallback через `__nx_heap_size` і `svcSetHeapSize`; при помилці query або відсутності придатного сегмента завершуватися контрольовано з `LibnxError_HeapAllocFailed`.
   - Перенести outbound NXLink logger thread із `userAppInit/userAppExit` у межі `App::App/App::~App`, виправити socket sentinel `-1` та обмежити довжину копіювання фактично записаними байтами `buf[512]`.
3. **Реалізація й верифікація**:
   - Додано strong `__libnx_initheap()` у `sphaira/source/main.cpp`: без алокацій він обходить OverrideHeap через `svcQueryMemory` і передає newlib найбільший суцільний `MemType_Heap + Perm_Rw + attr == 0` сегмент; fallback без OverrideHeap відповідає libnx.
   - Lifecycle outbound logger перенесено до `App`, socket sentinel виправлено на `-1`, а довжину повідомлення обмежено фактичним вмістом `buf[512]`; inbound `nxlinkInitialize()` не змінювався.
   - Gemini виконав `tests/run.sh` (усі host-тести) та WSL ReleaseWithInstall build; Build ID нового ELF — `93E0BD21BD490A235A75C52D4DE6ECBC243D0879`.
   - Апаратне тестування прийнято.

## Попередній delivery: v0.13.499 — Tools Menu Layout Reorganization, Software Description Update & 4th Row Expansion

Статус: реалізацію виконано та перевірено:
1. **Реорганізація сітки іконок меню Tools (`sphaira/source/ui/menus/tools_menu.cpp`)**:
   - Оновлено розташування пунктів меню:
     - 1-й ряд: File Browser, Games, Themes.
     - 2-й ряд: Updater, Saves, Software (Додаткові програми).
     - 3-й ряд: Cheats, Kefir Settings, Settings.
     - 4-й ряд (експериментальний): Tools (Інструменти).
2. **Оновлення опису розділу Software**:
   - Задано опис `"Homebrew App Store, DBI and mod utilities."` для точного відображення вмісту каталогу Homebrew App Store.
3. **Експериментальний 4-й ряд (пункт Tools)**:
   - Додано елемент `Tools` з іконкою `advanced-options.png` та дією переходу в менеджер системних модулів `UninstallerMenu`.
   - Забезпечено підтримку вертикального скролінгу сітки та безшовне відображення.
4. **Локалізація та версія**:
   - Синхронізовано нові ключі локалізації у всіх 14 мовних файлах `assets/romfs/i18n/*.json`.
   - Піднято версію до `0.13.499` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `task.md`, `walkthrough.md`.

## Попередній delivery: v0.13.498 — Header Subtitle Top-Row Alignment & Section Title Sizing Fix

Статус: реалізацію виконано та перевірено:
1. **Перенесення довгих описів елементів на верхній рядок хедера (`sphaira/source/ui/menus/tools_menu.cpp`, `save_hub_menu.cpp`, `settings_menu.cpp`, `uninstaller_menu.cpp`, `ftp_menu.cpp`, `appstore.cpp`)**:
   - Замість `SetSubHeading(description)`, що виводило текст у нижньому рядку поруч із заголовком розділу, реалізовано виклик `SetTitleSubHeading(description, true); SetSubHeading("");`.
   - Тепер опис обраного інструмента або налаштування відображається у верхньому рядку хедера праворуч від версії програми (`v0.13.498`), плавно прокручуючись за потреби.
2. **Збереження повного кегля назви розділу (`MenuBase::DrawChrome`)**:
   - Звільнення нижнього рядка від довгих описів гарантує повний простір для назви розділу («Інструменти» / «Tools», «Налаштування» / «Settings» тощо). Заголовок виводиться повним кеглем 28px без стискання до 40% та непотрібної прокрутки.
3. **Збірка, тести та версія**:
   - Піднято версію до `0.13.498` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `task.md`, `walkthrough.md`.
   - Успішно скомпільовано та пройдено всі unit-тести.

## Попередній delivery: v0.13.497 — Clean Switch compilation, translation pipeline sync & NX-Link deployment

Статус: реалізацію виконано та перевірено:
1. **Виправлення сумісності компіляції під Nintendo Switch (`sphaira/source/auto_update.cpp`, `sphaira/source/ui/about_box.cpp`, `sphaira/source/ui/menus/install_stream_menu_base.cpp`, `sphaira/source/ftpsrv_helper.cpp`)**:
   - Виправлено виклик `fs.OpenFile(staging_path, FsOpenMode_Read, &file)` з перевіркою розміру через `file.GetSize(&file_size)`.
   - Оновлено `AboutBox::Update` на використання коректних полів `TouchInfo` (`touch->is_touching`, `touch->cur.y`).
   - Виправлено форматні специфікатори `%ld` для `s64` та прибрано зайві попередження компілятора.
2. **Повний переклад через Gemini 3.6 Flash (`tools/i18n-translate/translate.py`)**:
   - Синхронізовано та повністю перекладено всі мовні файли (`uk`, `ru`, `en`, `de`, `fr`, `es`, `it`, `ja`, `ko`, `nl`, `pt`, `se`, `vi`, `zh`) без помилок (0 missing keys).
3. **Успішна збірка та розгортання через NX-Link**:
   - Скомпільовано цільовий двійковий файл `kefir-hub.nro` у WSL середовищі devkitA64 (`ReleaseWithInstall`).
   - Успішно передано бінарник на консоль через `make nxlink` (`192.168.50.69`).

## Попередній delivery: v0.13.496 — Automatic Silent Update, Background Self-Updating & About Changelog Box

Статус: реалізацію виконано та перевірено:
1. **Налаштування автоматичного оновлення (`sphaira/include/app.hpp`, `sphaira/source/app_settings.cpp`, `sphaira/source/ui/menus/settings_menu.cpp`)**:
   - Додано конфігураційну опцію `m_auto_update` (`App::GetAutoUpdateEnable()`, `App::SetAutoUpdateEnable(bool)`).
   - Відображено пункт `Auto-update` у меню `Settings → General` із детальним описом та перемикачем On/Off (увімкнено за замовчуванням).
   - Оновлено словники перекладу у всіх 14 мовних файлах `assets/romfs/i18n/*.json`.
2. **Фонове тихе оновлення виконуваного файлу (`sphaira/include/auto_update.hpp`, `sphaira/source/auto_update.cpp`, `sphaira/source/ui/menus/main_menu.cpp`)**:
   - Оновлено URL репозиторію на `https://api.github.com/repos/rashevskyv/kefir-hub/releases/latest` та `kefir-hub.json`.
   - Реалізовано асинхронне фонове завантаження релізного ассету (`kefir-hub.nro` / `sphaira.nro`) у тимчасовий файл `/switch/sphaira/cache/sphaira_update.temp` без блокування інтерфейсу чи підгальмовувань.
   - Реалізовано атомарне копіювання завантаженого бінарника у шлях поточного запущеного файлу (`App::GetExePath()`), а також синхронізацію з `/hbmenu.nro`, якщо увімкнено відповідний параметр.
   - Процес оновлення відбувається на 100% тихо та прозоро для користувача — без спливаючих діалогових вікон, без вимоги перезапуску чи підтверджень; оновлена версія безшовно запускається при наступному відкритті програми.
3. **Діалогове вікно About та перегляд списку змін (`sphaira/include/ui/about_box.hpp`, `sphaira/source/ui/about_box.cpp`, `sphaira/source/ui/menus/settings_menu.cpp`)**:
   - Створено модальний віджет `AboutBox` для перегляду поточної версії, посилання на репозиторій та списку змін (changelog) останніх релізів.
   - Реалізовано Markdown-форматування тексту, плавне прокручування (стіки, D-Pad, L/R для перегортання сторінок, жест тач-скролінгу) та кнопку оновлення (X: Refresh).
   - Додано пункт `About` у меню `Settings → General`.
4. **Unit-тести та документація**:
   - Створено `tests/test_auto_update_asset.cpp` з 8 перевірками точності вибору ассетів релізу для різних середовищ та назв файлів.
   - Піднято версію до `0.13.496` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `task.md`, `walkthrough.md`.
   - Пройдено всі 15 наборів host unit-тестів та перевірку відсутності мертвих символів (`tests/run.sh`).

## Попередній delivery: v0.13.495 — Pixel-balanced split & full-width justified 2-row footer layout

Статус: реалізацію виконано та перевірено:
1. **Попіксельне балансування рядків футера (`sphaira/source/ui/widget.cpp`)**:
   - Алгоритм вибору індексу поділу $k$ оптимізовано для мінімізації різниці реальної піксельної ширини зайнятого контенту між нижнім та верхнім рядками ($|W_{\text{bottom}} - W_{\text{top}}| \to \min$), що забезпечує однакове візуальне навантаження обох смуг незалежно від кількості елементів.
2. **Рівномірний розподіл елементів на всю ширину футера (`sphaira/source/ui/widget.cpp`)**:
   - Реалізовано `LayoutUiButtonsRowJustified`, який розраховує міжкнопковий інтервал $gap = (W_{\text{avail}} - W_{\text{content}}) / (M - 1)$, розтягуючи елементи від `30px` до `1220px`.
   - Сенсорні зони елементів кожного рядка адаптовано так, щоб вони безшовно покривали всю нижню половину екрана без сліпих зон.
3. **Unit-тести, документація та збірка**:
   - Оновлено `tests/test_title_scaling.cpp` (перевірка мінімізації різниці пікселів та точного позиціонування лівого краю).
   - Піднято версію до `0.13.495` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `task.md`, `walkthrough.md`.
   - Успішно зібрано `sphaira_nro` у WSL та пройдено тести.

## Попередній delivery: v0.13.494 — Unified Prev/Next Image button hint in image viewer footer

Статус: реалізацію виконано та перевірено:
1. **Об'єднання підказок навігації по зображеннях через слеш (`sphaira/source/ui/menus/file_viewer.cpp`)**:
   - Дві окремі підказки «Попереднє зображення» та «Наступне зображення» об'єднано в одну єдину дію з гліфами `\uE0ED / \uE0EE` (`◀ / ▶`) та локалізованим текстом `"Prev / Next Image"_i18n` на кнопці `Button::LEFT`.
   - Для кнопки `Button::RIGHT` зареєстровано приховану дію (`m_hint = ""`), що забезпечує збереження повної функціональності перемикання зображень кнопкою D-Pad Right на контролері та звільняє простір у підвалі вівера.
2. **Версія, збірка та перевірка**:
   - Піднято версію до `0.13.494` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `task.md`, `walkthrough.md`.
   - Успішно скомпільовано бінарник `sphaira_nro` у WSL та пройдено всі unit-тести.

## Попередній delivery: v0.13.493 — Image viewer uncluttered header, dynamic title scaling/scrolling & 2-row footer layout

Статус: реалізацію виконано та перевірено:
1. **Приховування смуг пам'яті NAND/SD у вівері зображень (`sphaira/include/ui/menus/menu_base.hpp`, `sphaira/source/ui/menus/file_viewer.cpp`)**:
   - Додано `SetShowStorage(bool)` / `ShowStorage()` у `MenuBase`.
   - У вівері зображень встановлено `SetShowStorage(false)`, що повністю приховує смуги NAND та SD, звільняючи верхню смугу під назву зображення від лівого поля `x = 80` аж до годинника/статус-блоку (`m_status_left_x = start_x`).
2. **Адаптивне масштабування та плавна прокрутка назв файлів і заголовків (`sphaira/source/ui/menus/menu_base.cpp`)**:
   - У `MenuBase::DrawChrome` реалізовано динамічний розрахунок кегля шрифту: якщо назва не поміщається у відведений простір, розмір пропорційно зменшується (базовий 28px, зменшення до 40% / мінімум 16.8px).
   - Якщо навіть при мінімальному кеглі назва файлу перевищує доступну ширину, автоматично активується плавний скролінг `m_scroll_title` (`ScrollingText`).
3. **Автоматичний перенос підказок кнопок футера на 2 рядки (`sphaira/source/ui/widget.cpp`)**:
   - У `Widget::SetupUiButtons` додано інтелектуальне розбиття дій на два рядки, коли для одного рядка масштаб стає меншим за 0.85.
   - Алгоритм вибирає оптимальний поділ $k$, який максимізує масштаб і балансує ширину обох рядків (основні кнопки дій у нижньому рядку, тригери та вторинні дії у верхньому).
   - Забезпечено збереження читабельних шрифтів (17px/22px) та незалежні неперетинні сенсорні зони для кожного рядка ([646, 682] та [682, 720]).
4. **Unit-тести, документація та компіляція**:
   - Створено `tests/test_title_scaling.cpp` з 17 перевірками точності зменшення кегля та розбиття рядків футера.
   - Піднято версію до `0.13.493` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `task.md`, `walkthrough.md`.
   - Успішно скомпільовано ціль `sphaira_nro` у WSL та пройдено повний набір хостових тестів.

## Попередній delivery: v0.13.492 — Homebrew App Store restored to Tools > Software

Статус: реалізацію виконано та перевірено:
1. **Повернення магазину додатків у розділ програм (`sphaira/source/ui/menus/settings_menu.cpp`)**:
   - `Homebrew App Store` повернено на першу позицію у `BuildSoftwareItems()` (`Tools → Software`), забезпечуючи швидкий і логічний доступ до каталогу застосунків.
   - З категорії `Settings → Homebrew` (`BuildCategories()`) вилучено пункт запуску `Homebrew App Store`, зберігши розділ налаштувань суто для параметрів конфігурації (`Homebrew Search Paths`, `Forwarders`, `Replace hbmenu on exit`).
2. **Версія, збірка та перевірка**:
   - Піднято версію до `0.13.492` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `task.md`, `walkthrough.md`.
   - Успішно зібрано `sphaira_nro` у WSL (`[100%] Built target sphaira_nro`), пройдено повний набір host unit-тестів.

## Попередній delivery: v0.13.491 — Fix flush thread stack overflow

Статус: реалізацію виконано та перевірено:
1. **Усунення переповнення стеку у фоновому потоці логування (`sphaira/source/log.cpp`)**:
   - Масив `batch` (64 КБ) перенесено зі стеку функції `flush_thread_func` у статичну пам'ять `g_flush_batch`.
   - Збільшено розмір стеку потоку `g_flush_thread` з `0x4000` (16 КБ) до `0x8000` (32 КБ), усунувши Stack Overflow (`Data Abort` при старті програми).
2. **Версія та збірка**:
   - Піднято версію до `0.13.491` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `task.md`, `walkthrough.md`.
   - Успішно зібрано `sphaira_nro` у WSL.

## Попередній delivery: v0.13.490 — Fix cstring include in static logger

Статус: реалізацію виконано та перевірено:
1. **Виправлення компіляції `log.cpp`**:
   - Додано заголовок `<cstring>` у `sphaira/source/log.cpp` для повної підтримки `std::memcpy` у статичному неалокуючому буфері.
2. **Версія та збірка**:
   - Піднято версію до `0.13.490` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `task.md`, `walkthrough.md`.
   - Успішно зібрано `sphaira_nro` у WSL (`[100%] Built target sphaira_nro`).

## Попередній delivery: v0.13.489 — Zero-heap static logging buffer & image load ordering

Статус: реалізацію виконано та перевірено. Повністю усунено алокації кучі у фоновому потоці логування та нормалізовано порядок ініціалізації графіки:
1. **Статичний буфер логування без звернень до heap (`sphaira/source/log.cpp`)**:
   - Замінено динамічний `std::string` та операції `append`/`swap`/`free` на фіксований статичний буфер `g_buffer_data` (64 КБ). Це повністю виключає звернення до `_malloc_r`, `_realloc_r` та `free` під час запису логів з фонових потоків і скидання на диск/сокет, унеможливлюючи пошкодження метаданих чанків кучі (`Data Abort 0x4A8`).
2. **Порядок завантаження ресурсів (`sphaira/source/app.cpp`)**:
   - `InitDefaultImage()` перенесено перед запуском фонових воркерів `ntp::Start()` та `forwarder_auto::StartCheck()`, що гарантує ексклюзивне розкодування системних іконок без конкуренції за пам'ять.
3. **Версія та інтеграція**:
   - Піднято версію до `0.13.489` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `task.md`, `walkthrough.md`.

## Попередній delivery: v0.13.488 — Sysmodule slow SD boot timeout & crash prevention

Статус: реалізацію виконано та перевірено. На основі аналізу патчу SwitchThemeInjector усунено падіння та зависання на повільних microSD картах:
1. **Збільшення тайм-аутів ініціалізації ФС у сисмодулі (`sysmodule/source/main.c`)**:
   - Збільшено ліміт спроб підключення `fsInitialize()` та монтування `fsdevMountSdmc()` зі 100 ітерацій (10 секунд) до 3000 ітерацій (300 секунд / 5 хвилин), що гарантує успішний старт сисмодуля на повільних картах пам'яті.
   - Видалено фатальний аборт `diagAbortWithResult` при помилці `smInitialize()`, що запобігає крашу Atmosphere при затримках сервісів під час завантаження ОС.
2. **Версія та перевірка**:
   - Ітеровано версію програми до `0.13.488` у `sphaira/CMakeLists.txt`, оновлено `README.md`, `task.md`, `walkthrough.md`.

## Попередній delivery: v0.13.487 — SD card FS sync, malloc & NanoVG stability on slow cards / NX-Link handoff

Статус: реалізацію виконано та перевірено. Усунено аварійні падіння (Data Abort 0x4A8 в `_malloc_r`) та збої файлової системи на повільних картах пам'яті під час передачі NRO через NX-Link та запуску:
1. **Фіксація файлової системи та захист від пошкодження microSD (`main.cpp`, `nxlink.cpp`, `nro.cpp`, `log.cpp`)**:
   - Виправлено ім'я монтування пристрою в `fsdevCommitDevice("sdmc")` та `fsdevGetDeviceFileSystem("sdmc")` у `userAppExit()`. Раніше передавався некоректний суфікс `"sdmc:"`, через що системне збереження кешу ФС не викликалося під час виходу з програми.
   - Додано виклики `fsdevCommitDevice("sdmc")` після створення/перейменування файлів у `nxlink.cpp`, перед викликом `envSetNextLoad` у `nro.cpp` (`launch_internal`), а також при записі логів у `log.cpp` (`do_flush`, `log_write_error`).
2. **Усунення race conditions та захист хіпу при логуванні (`log.cpp`)**:
   - Переведено мережеву передачу логів у фоновому потоці з `stdout`/stdio на прямий `send(sock, ...)`. Це усуває конфлікти алокацій у stdio нових потоків newlib, які призводили до пошкодження заголовків чанків heap (`_malloc_r`).
   - Замінено `std::localtime` на реентрабельний `localtime_r` у `log_write_error`.
3. **Безпечне завантаження ресурсів тем та текстур (`app_theme.cpp`)**:
   - У `LoadElementImage` та `LoadElementColour` додано явне створення нуль-термінованих рядків `std::string` перед викликом `nvgCreateImage` та `std::strtoul`, що усуває вихід за межі буфера `std::string_view`.
4. **Версія, збірка та розгортання**:
   - Оновлено версію до `0.13.487` у `CMakeLists.txt`, успішно зібрано `sphaira_nro` у WSL, виконано тести (`tests/run.sh`), бінарник розгорнуто на microSD диск `I:\` (`I:\hbmenu.nro` та `I:\switch\kefir-hub.nro`).

## Попередній delivery: v0.13.486 — Saves menu L/R shoulder button tab navigation

Статус: реалізацію виконано та перевірено. Додано можливість швидкого та безшовного перемикання між категоріями збережень («Встановлені ігри», «Видалені ігри», «Резервні копії») плечовими кнопками L та R:
1. **Реєстрація дій плечових кнопок (`save_menu.cpp`)**:
   - У конструкторі `Menu::Menu` додано дії `Button::L` ("Previous tab"_i18n) та `Button::R` ("Next tab"_i18n) для автономного режиму меню (`!m_app_id_filter`).
   - Кнопки відображаються у футері та підтримують як натискання фізичних кнопок контролера, так і сенсорні натискання по підказках у футері.
2. **Циклічне перемикання категорій (`save_menu.hpp`, `save_menu.cpp`)**:
   - Реалізовано метод `Menu::ChangeCategory(s64 delta)`, який циклічно перемикає категорію за списком `Installed` <-> `Deleted` <-> `Backups`.
   - Реалізовано метод `Menu::SetCategory(Category category)`: змінює `m_category`, оновлює заголовок `SetTitle(...)`, відтворює звуковий ефект зміни фокусу `App::PlaySoundEffect(SoundEffect_Focus)` та перезавантажує список елементів через `ScanHomebrew()`.
3. **Версія, тести та збірка**:
   - Піднято версію до `0.13.486` у `sphaira/CMakeLists.txt`, оновлено `README.md`, успішно скомпільовано ціль `sphaira_nro` у WSL (`[100%] Built target sphaira_nro`), пройдено перевірки host unit tests (`tests/run.sh`) та `git diff --check`.

## Попередній delivery: v0.13.485 — Screensaver display sleep prevention & OLED user brightness retention

Статус: реалізацію виконано та перевірено. Забезпечено надійну роботу скрінсейвера без вимкнення екрана та оптимізовано яскравість для різних типів матриць:
1. **Запобігання вимкненню екрана та авто-сну (`app.hpp`, `screensaver.cpp`)**:
   - Оновлено `App::SetAutoSleepDisabled(bool enable)`: тепер виклик `appletSetMediaPlaybackState(true)` здійснюється обов'язково разом із `appletSetAutoSleepDisabled(true)`. Згідно зі специфікацією HOS, саме `SetMediaPlaybackState` блокує системне приглушення яскравості (dimming) та вимкнення підсвітки екрана через неактивність.
   - Додано виклики `App::SetAutoSleepDisabled(true)` у `Screensaver::Start()` та скидання у `Screensaver::Stop()`.
   - У `Screensaver::Update(...)` додано виклик `appletReportUserIsActive()`, який періодично передає ОС сигнал про активність користувача на рівні HID, унеможливлюючи спрацьовування таймерів очікування HOS.
2. **Збереження яскравості користувача на OLED та зниження на LCD (`app_settings.cpp`, `screensaver.cpp`)**:
   - Реалізовано апаратне визначення типу консолі `App::IsOledModel()` через опитування `splGetConfig(SplConfigItem_HardwareType, &hardware_type)` (значення `5` відповідає моделі Aula / Switch OLED).
   - У `Screensaver::Start()` для OLED-моделей встановлено збереження поточної виставленої користувачем яскравості `m_saved_brightness` (чистий чорний фон скрінсейвера `#000000` вимикає пікселі OLED і споживає 0W, тому годинник і статистика залишаються яскравими та легко читабельними з відстані).
   - Для LCD-моделей (Switch V1, V2, Lite) яскравість знижується до значення `App::GetBlankBrightness() / 100.f`, що заощаджує батарею та усуває засвітку підсвітки в темряві.
   - Збережено можливість ручного регулювання яскравості правим стіком (Up/Down) на будь-якому типі дисплея та обов'язкове відновлення початкової яскравості користувача при виході зі скрінсейвера.
3. **Попередній перегляд скрінсейвера (`screensaver.cpp`)**:
   - У `SaverPreview::Update` додано виклик `m_saver.Update` для коректного оновлення дрейфу, реакції на стіки та запобігання засинанню консолі в режимі прев'ю.
4. **Версія, документація та збірка**:
   - Піднято версію до `0.13.485` у `sphaira/CMakeLists.txt`, синхронізовано `README.md`, успішно зібрано бінарник `sphaira_nro` у WSL (`[100%] Built target sphaira_nro`), пройдено перевірки host unit tests (`tests/run.sh`) та `git diff --check`.

## Попередній delivery: v0.13.484 — NX-Link SD commit, path normalization, buffer bounds & forwarder auto-install stabilization

Статус: реалізацію виконано та перевірено. На основі аналізу дампів аварійних збоїв (Atmosphere Crash Reports) усунено причини падіння пам'яті та крашу процесу при роботі NX-Link та старті застосунку:
1. **Безпечна ініціалізація `m_app_path` (`app.cpp`)**: Виправлено нетермінований рядок шляху виконуваного файлу при старті (`argv0` з `sdmc:/`). Раніше `std::strncpy` копіював байти без завершального `\0`, що призводило до читання пам'яті за межами буфера, битих записів у `playlog.ini` та некоректного обчислення SHA256-хешу. Тепер буфер явно обнуляється та гарантовано термінується `\0`.
2. **Захист від повторного встановлення активного тайтла (`forwarder_auto_install.cpp`)**:
   - Додано перевірку режиму виконання: якщо Sphaira вже працює як встановлений Application (forwarder), потік автоматично завершує перевірку без сканування та встановлення.
   - Реалізовано вилучення Title ID із назви знайденого NSP (`Homebrew menu [03DB12780BD84000]...`) та перевірку через `nsIsAnyApplicationEntityInstalled`. Якщо знайдений тайтл вже встановлено на консолі, фонове встановлення пропускається, що усуває конкурентний перезапис активного тайтла та конфлікти алокацій пам'яті/NCM під час завантаження тем і Deko3D.
   - Додано функцію `StopCheck()` та обробку запиту на зупинку `g_stop_requested` у `SilentInstallProgress`, що забезпечує коректну зупинку фонового потоку в деструкторі `App::~App()`.
3. **Нормалізація шляхів та фіксація файлової системи в `nxlink.cpp`**:
   - Шляхи, передані з хоста через NX-Link, нормалізуються (видаляється префікс `sdmc:`, гарантується початковий `/`), що усуває збої нативних викликів `FsFileSystem` (`0x202` / `0x402`).
   - Додано обов'язкові виклики `fs.Commit()` після запису даних у тимчасовий файл та після фінального `fs.RenameFile`, що гарантує збереження таблиці кластерів FAT32/exFAT на карті пам'яті перед запуском NRO та запобігає падінню файлової системи microSD.
   - Забезпечено безпечну роботу з буфером аргументів `args_buf` із гарантованим нуль-термінатором та `strncpy` для колбеків повідомлень.
   - `SocketWrapper` переведено на move-only семантику з коректним закриттям сокетів без подвійного `close`.
4. **Потокобезпечне логування (`log.cpp`)**: Замінено небезпечний `std::localtime` на реентерабельний `localtime_r` у `log_write_arg_internal`.
5. **Паралельний запуск тестів (`tests/run.sh`)**: Скрипт тестів хоста переведено на паралельну компіляцію та запуск усіх тестових наборів.
6. **Версія та збірка**: Піднято версію до `0.13.484` у `sphaira/CMakeLists.txt`, успішно скомпільовано цільовий бінарник у WSL (`[100%] Built target sphaira_nro`), пройдено всі unit-тести.

## Попередній delivery: v0.13.483 — Install queue list layout bounds fix & auto-advance on X button

Статус: реалізацію виконано та перевірено. Виправлено накладання списку пакунків у черзі встановлення на футер та додано автоматичний крок курсора при виборі пунктів кнопкою X:
1. **Геометрія списку черги інсталяції (`dbi_menu.cpp`)**: Виправлено перекриття списком елементів лінії та кнопок футера (`FOOTER_LINE_Y = 646.f`). Зменшено висоту рядка з 82.f до 78.f, скориговано позицію списку `queue_pos` на `{70.f, GetY() + 63.f, 1140.f, 470.f}` (150.f по осі Y). Тепер 6 рядків списку займають висоту 468.f (край на рівні 618.f, рамка фокусу 622.f), що залишає 24px безпечного відступу до розділювача футера.
2. **Геометрія списків журналу та помилок**: Скориговано висоту `log_pos` з 330.f до 310.f (`m_log_list` на 10 рядків по 30.f = 300.f, `m_error_list` на 5 рядків по 55.f = 275.f), усунувши накладання на футер у режимах `Installing`, `Summary` та `Cancelled`.
3. **Автоматичний перехід курсора при виборі кнопкою X**: Оновлено дію `Button::X` ("Select") для стану `State::ReviewQueue`. При натисканні кнопки X перемикається стан виділення `m_queue[m_index].selected`, а потім, якщо це не останній елемент черги (`m_index + 1 < m_queue.size()`), курсор автоматично переходить на наступний рядок (`m_index++`) із забезпеченням видимості через `m_list->EnsureVisible`. Це повністю відповідає логіці вибору в інших меню програми (`game_menu`, `homebrew`, `filebrowser`, `save_menu`).
4. **Захист ножиць кадрування контенту (`layout.hpp`)**: У `PaddedContentClipY` встановлено обов'язкове обмеження `bottom = std::min(bottom, CONTENT_BOTTOM)` для будь-яких блоків контенту, які починаються нижче лінії заголовка (`y >= HEADER_LINE_Y`), що запобігає малюванню контенту поверх футера.
5. **Версія, тести та збірка**: Піднято версію до `0.13.483` у `CMakeLists.txt`, оновлено документацію, успішно виконано прогін усіх тестів та збірку `sphaira_nro` у WSL.

## Попередній delivery: v0.13.482 — Fully silent background forwarder installation without restart prompt

Статус: реалізацію виконано та перевірено. Переведено автоматичне встановлення форвардера при старті програми у повністю тихий режим без запитів на перезапуск:
1. При старті програми фоновий потік перевіряє наявність встановленого форвардера для Homebrew Menu / Sphaira через `nsIsAnyApplicationEntityInstalled`.
2. Якщо форвардер відсутній, у фоні виконується пошук `Homebrew menu*.nsp` у папці `/Games/` та тихе встановлення через `yati::InstallFromFile`.
3. Повністю видалено діалогові вікна `OptionBox` та пропозиції перезапуску: після завершення встановлення потік тихо фіксує успіх у логах і завершує роботу, не перериваючи та не турбуючи користувача.
4. Очищено невикористовувані заголовні файли в `forwarder_auto_install.cpp`.
5. Піднято версію програми до `0.13.482` у `CMakeLists.txt`, оновлено документацію, успішно виконано збірку в WSL та пройдено всі тести.

## Попередній delivery: v0.13.481 — Install queue package skip fix & USB link resynchronization

Статус: реалізацію виконано та перевірено. Виправлено проблему, коли дія пропуску пакунка кнопкою B у черзі встановлення переривала всю чергу:
1. Усунено розсинхронізацію USB-протоколу: при пропуску пакунка користувачем (`user_skipped`) у `ThreadFunction` активний USB endpoint скасовується, через що хост-застосунок на ПК залишався в середині передачі попереднього файлу. Додано автоматичний виклик `ReestablishUsbLink()`, який повторно проводить handshake та переводить хост у режим очікування нової команди перед переходом до наступного пакунка в черзі.
2. Розширено умови повторних спроб (`attempt`) у `ThreadFunction`: тепер при виникненні помилок протоколу/сесії DBI (`IsDbiSessionError`) відбувається спроба повторного підключення замість миттєвого завершення черги.
3. Оновлено діалог `OptionBox` для кнопки `B` ("Skip package") під час встановлення (`State::Installing`): встановлено дефолтний індекс `1` ("Yes"), що дозволяє користувачеві підтвердити пропуск пакунка кнопками `A` або `+`, або скасувати діалог кнопкою `B`.
4. Синхронізовано `LocalThreadFunction`: включено перевірку `Result_UsbCancelled` та уніфіковано встановлення прапорця `m_cancel_requested` при загальному скасуванні черги.
5. Розширено `test_queue_outcome.cpp` тестом багатопакетної черги з пропуском одного пакунка та успішним встановленням наступного, піднято версію до `0.13.481` у `CMakeLists.txt`, успішно скомпільовано реліз у WSL та пройдено всі unit-тести.

## Попередній delivery: v0.13.480 — Save data deletion mechanism & auto-creation on restore

Статус: реалізацію виконано та перевірено. Додано механізм видалення збережень для встановлених та видалених ігор (а також резервних копій) у меню Tools > Saves, і покращено відновлення сейвів:
1. Додано пункт `"Delete"` до спливаючого списку дій `Save Action` при виборі збереження кнопкою `A` або групи збережень кнопкою `X`, а також у бічне меню `Save Options` (кнопка `+`).
2. Реалізовано бічне меню `Delete Options` з підтримкою фільтрації за обліковими записами (`Accounts`) та типами сейвів (`Save Types`).
3. Додано захисні діалоги підтвердження `OptionBox` із попередженням про незворотність видалення та відображенням іконки гри.
4. Реалізовано метод `Menu::DeleteSaves`:
   - Для встановлених та видалених ігор: видалення сейвів із консолі за допомогою `fsDeleteSaveDataFileSystemBySaveDataSpaceId` та резервного `fsDeleteSaveDataFileSystemBySaveDataAttribute`. Для категорії "Deleted Games" гра повністю зникає зі списку після видалення сейву.
   - Для категорії "Backups": видалення файлів резервних копій (`.zip`, `.disa`) з SD-карти / накопичувача та очищення порожніх каталогів.
5. Покращено функцію `RestoreSaveInternal`: усунуто падіння при відновленні на чистих/відновлених EmuNAND або нових іграх без попереднього сейву. За відсутності файлової системи збереження на консолі вона автоматично створюється через `fsCreateSaveDataFileSystem` на основі метаданих архіву перед розпакуванням файлів.
6. Оновлено файли локалізації (`en.json`, `uk.json`), синхронізовано документацію (`README.md`), піднято версію до `0.13.480`, успішно скомпільовано цільовий `sphaira_nro` у WSL та пройдено всі unit-тести.

## Попередній delivery: v0.13.479 — Automatic forwarder check, silent install & title mode restart prompt

Статус: реалізацію виконано та перевірено. Додано автоматичну фонову перевірку наявності встановленого форвардера при старті програми, тихе встановлення з microSD та пропозицію перезапуску в Title Mode:
1. При запуску Sphaira у фоновому потоці (`forwarder_auto::StartCheck()`) перевіряється, чи встановлено форвардер для Homebrew Menu або Sphaira (перевірка стандартних тайтлів `010000000000100D`, `050000000000100D`, а також згенерованих ідентифікаторів Sphaira на базі виконуваного NRO через `nsIsAnyApplicationEntityInstalled`).
2. Якщо форвардер відсутній, виконується прозорий фоновий пошук NSP-пакета в директорії `/Games/` на microSD карті за маскою `Homebrew menu*.nsp`.
3. Знайдений NSP встановлюється у фоні за допомогою `yati::InstallFromFile` із застосуванням спеціалізованого `SilentInstallProgress` без блокування інтерфейсу та з прапорцем `skip_if_already_installed = 1`.
4. Розширено інтерфейс `ui::InstallProgress` та механізм `yati.cpp` методом `OnTitleInstalled(u64 title_id)` для точного визначення встановленого ідентифікатора тайтла.
5. Після успішного встановлення через `evman::push` на головний UI-потік виводиться діалогове вікно `OptionBox` із запитом `"Homebrew Menu forwarder installed. Restart into Title Mode now?"`. При підтвердженні ("Restart") викликається `appletRequestLaunchApplication(target_tid, nullptr)` та `App::Exit()`.
6. Додано допоміжну функцію `path::StartsWithIC` у `path_util.hpp`, розширено host unit tests (`test_path_util.cpp`, 162 checks), синхронізовано 14 файлів локалізації.
7. Піднято версію до `0.13.479`, успішно виконано збірку в WSL (`sphaira_nro`), пройдено всі тести та `git diff --check`.

## Попередній delivery: v0.13.478 — Theme packages download & instant install prompt

Статус: реалізацію виконано та перевірено. Додано автоматичну пропозицію встановлення для готових пакетів тем (Mario BG Dark, Switch 2 Theme by alexwak) у меню Tools -> Themes:
1. Раніше готові пакети тем завантажувалися через `MakePackageAction`, який лише розпаковував zip у `/themes/` та показував сповіщення "Done", не пропонуючи запуск `NXThemesInstaller` (на відміну від завантаження з Themezer та закріплених тем).
2. Реалізовано спільні функції `PromptInstallTheme` та `InstallThemePackage` у `themezer.hpp` / `themezer.cpp`: під час розпакування zip-архіву автоматично відстежуються шляхи до всіх видобутих `.nxtheme` файлів, після чого показується запит `"Theme downloaded, install now?"`.
3. При підтвердженні встановлення запускається `NXThemesInstaller.nro` з передачею аргументів розпакованих тем (`sdmc:/themes/...`). Якщо інсталятор відсутній на консолі, пропонується його швидке завантаження з GitHub.
4. Додано функцію `MakeThemePackageItem` у `settings_menu.cpp` з попереднім запитом `"Download theme?"`, уніфікуючи поведінку для всіх типів тем.
5. Піднято версію до `0.13.478`, успішно виконано збірку в WSL, пройдено всі тести та `git diff --check`.

## Попередній delivery: v0.13.477 — Game details stat label vertical alignment fix

Статус: реалізацію виконано та перевірено. Виправлено вертикальне зміщення та накладання прокручуваних лейблів статистики гри:
1. При перевищенні довжини локалізованого лейблу (наприклад, "Останній запуск" / "Last played") 1/3 ширини блоку, текст переходить у режим автопрокручування `m_stat_label_scrolls[...].Draw`. Раніше передавалося вирівнювання `NVG_ALIGN_LEFT` без визначення вертикальної площини, через що NanoVG вирівнював текст за базовою лінією (`NVG_ALIGN_BASELINE`) замість верхнього краю (`NVG_ALIGN_TOP`), зміщуючи весь рядок угору (~15px) відносно свого значення.
2. Додано прапорець `NVG_ALIGN_LEFT | NVG_ALIGN_TOP` до `m_stat_label_scrolls` та уніфіковано вирівнювання для `m_language_scroll.Draw` (`NVG_ALIGN_LEFT | NVG_ALIGN_TOP`, `y + 1.f`).
3. Піднято версію до `0.13.477`, успішно виконано збірку в WSL, пройдено всі тести та `git diff --check`.

## Попередній delivery: v0.13.476 — UTF-16 to UTF-8 decoding & Cyrillic filename fix for MTP

Статус: реалізацію виконано та перевірено. Виправлено критичну проблему зі створенням та перейменуванням файлів/папок з кириличними та іншими не-ASCII назвами (зокрема системними іменами "Нова папка" / "Новая папка" у Windows Explorer):
1. В оригінальній бібліотеці `libhaze` функція `ReadString` у `ptp_data_parser.hpp` некоректно виконувала `static_cast<char>(chr)` над UTF-16 символами, відкидаючи старший байт. Для кириличних символів (наприклад 'Н' = `0x041D`) це призводило до перетворення на неприпустимі керуючі символи (0x1D) замість дійсного UTF-8 (`\xD0\x9D`), через що файлова система Switch відхиляла створення папки з помилкою `FsError_InvalidCharacter`.
2. Реалізовано повноцінне декодування UTF-16 у UTF-8 у `ReadString` (`ptp_data_parser.hpp`) та кодування UTF-8 у UTF-16 у `AddString` (`ptp_data_builder.hpp`).
3. Додано секції 7 та 8 у `patch_libhaze.cmake`, піднято версію до `0.13.476`, успішно зібрано бінарник у WSL та пройдено всі тести.

## Попередній delivery: v0.13.475 — Full read-write support with commit for MTP Saves drive

Статус: реалізацію виконано та перевірено. Перетворено віртуальне MTP-сховище `Saves` з режиму read-only у повноцінний read-write режим:
1. Додано підтримку створення та запису файлів (`CreateFile`, `WriteFile`, `SetFileSize`, `OpenFile` з `FsOpenMode_Write`) з автоматичним комітом змін (`fsFsCommit`) у файлову систему збереження при завершенні запису.
2. Додано підтримку створення піддиректорій (`CreateDirectory`), видалення файлів і папок (`DeleteFile`, `DeleteDirectoryRecursively`) та перейменування (`RenameFile`, `RenameDirectory`) всередині змонтованих збережень.
3. Оновлено монтування сейвів у `FsSaveProxy`: тепер відкриття виконується у режимі читання-запису (з безпечним fallback у read-only для захищених системних сейвів).
4. Оновлено `GetFreeSpace` для відображення доступного місця та змінено відображення назви диска з `Saves (read-only)` на `Saves`.

## Попередній delivery: v0.13.474 — Full MTP property handling, GetObjectPropDesc & SendObjectPropList fixes

Статус: реалізацію виконано та перевірено. Виправлено критичні збої та відмови в MTP-обробнику `libhaze`:
1. У `SendObjectPropList` (0x9808) замінено виклик помилки `ResultUnknownPropertyCode` на безпечне вичитування та обробку всіх типів властивостей об'єктів MTP (U8, U16, U32, U64, U128, String, масиви). Раніше будь-яка стандартна властивість від Windows Explorer (наприклад `StorageID`, `ObjectFormat`, `ParentObject`, `PersistentUniqueObjectIdentifier`), що надсилалася перед ім'ям файлу, спричиняла аварійний викид помилки та відмову створення папки.
2. У `GetObjectPropDesc` виправлено пропущений `break;` у switch після властивості `PersistentUniqueObjectIdentifier`, що спричиняло падіння у наступний `case ObjectSize` та надсилання пошкодженого блоку дескриптора властивості.
3. У `GetObjectPropList` додано підтримку `property_code == 0` (запит усіх властивостей згідно з MTP специфікацією).
4. У `SetObjectPropValue` додано підтримку встановлення імені об'єкта через властивість `PtpObjectPropertyCode_Name`.
5. Усі патчі додано в `patch_libhaze.cmake`, піднято версію до `0.13.474`, збірка та тести успішно пройдені.

## Попередній delivery: v0.13.473 — Installed Games save scanning & category listing fix

Статус: реалізацію виконано та перевірено. Виправлено відображення списку в `Saves -> Installed Games`: тепер меню надійно відображає всі встановлені на консолі ігри (аналогічно Tools Games через `nsListApplicationRecord` + `title::GetMetaEntries`), навіть якщо для них ще не було створено сейв на консолі або активний інший обліковий запис. Для кожної гри прив'язуються наявні активні сейви, або створюється запис гри для швидкого створення чи відновлення бекапів. Категорії "Видалені ігри" та "Резервні копії" при відсутності записів коректно показують стан "Empty...". У `PromptSaveTypeOptions` додано можливість відновлення бекапів безпосередньо для встановленої гри без наявності попереднього сейву.

## Попередній delivery: v0.13.472 — MTP folder and file creation storage_id fix

Статус: реалізацію виконано та перевірено. Виправлено критичний баг у протоколі MTP/PTP бібліотеки `libhaze`, через який Windows Explorer не міг створювати нові папки та файли ("Пристрій припинив відповідати, або його було відключено"). У відповідях `SendObjectInfo` (0x100C) та `SendObjectPropList` (0x9808) поле `storage_id` помилково заповнювалося `parentobj->GetObjectId()` (дескриптором об'єкта) замість `parentobj->GetStorageId()` (ідентифікатора сховища). Додано відповідні патчі в `patch_libhaze.cmake` та виправлено логування в `haze_helper.cpp`.

## Попередній delivery: v0.13.471 — Save categories hub & custom save backup search paths

Статус: реалізацію виконано та перевірено. Додано початкове меню категорій (`SaveHubMenu`) при вході у розділ Saves (Tools -> Saves та Main Menu -> Saves) з 3 пунктами: "Встановлені ігри" (Installed Games), "Видалені ігри" (Deleted Games), "Резервні копії" (Backups). Додано підтримку налаштування додаткових користувацьких папок для пошуку резервних копій у Settings -> Saves -> Save Backup Search Paths із вибором папок через `filepicker::Menu`, збереженням у конфігураційний файл `[save_backup_paths]` та автоматичним скануванням цих папок у `CollectBackups` та `ReadBackupEntries`.

## Попередній delivery: v0.13.470 — raw DISA save restore, save discovery & MTP USER:/save

Статус: реалізацію виконано та перевірено. Додано підтримку відновлення запакованих/сирових DISA/DPFS сейвів (монолітні контейнери `000000000000001e`, `.disa`, `.bin`, дампи з DBI Explorer) шляхом прямого блокового запису у відповідний NAND BIS-розділ (`FsBisPartitionId_User` або `FsBisPartitionId_System`) за шляхом `/save/<save_data_id>` із відображенням прогресу. Розширено `CollectBackups` та `ReadBackupEntries` для виявлення сирових сейвів у каталогах бекапів. Додано дію "Restore save data" у File Browser для швидкого відновлення збережень з будь-якого носія (SD, USB HDD, мережа). Додано MTP-сховища `USER:/save` та `SYSTEM:/save` з підтримкою читання й запису.

## Попередній delivery: v0.13.469 — unified pending UI & updater work

Статус: відновлено збережений WIP поверх повної колишньої mainline. Залишено
новішу спільну реалізацію `gfx::ImageViewport` для Theme Creator і forwarder crop,
покращений File Viewer з вибором діапазону та діями над ним, іконки дій у sidebar/
popup/File Browser, а також Updater focus, Kefir update badge і reconnect. File Picker
використовує стабільний верхній слот header з відновленої mainline.

## Попередній delivery: v0.13.468 — cURL shutdown & shared handle serialization

Статус: інтеграцію відновлено поверх `v0.13.467`. Спільний синхронний дескриптор
`g_curl_single` серіалізовано через `g_mutex_single`; `curl::RequestShutdown()`
викликається на початку виходу з App і не дає почати нові transfer після запиту
зупинки. Очищення handle виконується під тим самим mutex.

## Попередній delivery: v0.13.467 — versioned HTTP User-Agent

Статус: реалізацію виконано та перевірено. Замінено застарілий downloader User-Agent `TotalJustice` на єдине спільне джерело `APP_USER_AGENT` (`Sphaira/<APP_VERSION>`) у `defines.hpp` та додано встановлення `CURLOPT_USERAGENT` до `MountCurlDevice::curl_set_common_options()`. Політику TLS, редиректи, автентифікацію, HTTP-семантику, UI, i18n та залежності залишено без змін. Gemini успішно пройшов `git diff --check` та WSL `ReleaseWithInstall` (`[100%] Built target sphaira_nro`). Очікується ручний Switch remote-mount smoke check.

1. Додати єдину константу `APP_USER_AGENT = "Sphaira/" APP_VERSION;` у `sphaira/include/defines.hpp`.
2. Видалити локальний `API_AGENT` у `sphaira/source/download.cpp` та використати `APP_USER_AGENT` у `SetCommonCurlOptions()`.
3. Додати `curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, APP_USER_AGENT)` у `MountCurlDevice::curl_set_common_options()` (`sphaira/source/utils/devoptab_curl_device.cpp`).
4. Оновити `upstream_audit.md` (пункти `2eabcec` / `3ef698b`), підняти версію до `0.13.467` у `sphaira/CMakeLists.txt`, синхронізувати living docs, пройти `git diff --check` та WSL `ReleaseWithInstall`.

## Попередній delivery: v0.13.466 — caller-selected header layout

Статус: реалізацію виконано та перевірено. `MenuBase::SetTitleSubHeading` отримав стабільний параметр `top_row`, тому шлях або довільна назва більше не стрибає між рядками залежно від ширини. Шляхи й назви рендеряться після версії у верхньому широкому слоті, а лічильники та короткі статуси лишаються внизу.

1. Додати `bool top_row = false` до `MenuBase::SetTitleSubHeading` та скидати scroll state при зміні слота або очищенні тексту.
2. Рендерити верхній слот після виміряного `v%s` і до межі fixed status area; лишити `ScrollingText` для переповнення.
3. Передати `top_row = true` лише caller-ам шляхів і довільних назв; залишити компактні summary на нижньому рядку.
4. Підняти версію до `0.13.466`, оновити living docs, пройти host tests, WSL `ReleaseWithInstall` та `git diff --check`.

## Попередній delivery: v0.13.465 — text editor multi-line editing

Статус: реалізацію виконано та перевірено. Реалізовано вибір діапазону рядків у режимі редагування через меню дій (`Select range` / `Clear selection`), під час активного вибору A = `Finish selection`, B = `Cancel selection`. Додано процесовий буфер обміну рядками (`s_line_clipboard`) для дій Copy, Cut, Paste below та Delete над виділеним діапазоном або активним рядком. Реалізовано чисті допоміжні функції `CommentIniLine` та `UncommentIniLine` у `text_helper.hpp` із збереженням відступів та додано пункт `Undo` й `Redo` у спливаючий список дій. Пройдено повний набір host unit tests (`tests/run.sh` - 13 suites, 752 declarations) та `git diff --check`.

1. Додати підтримку вибору діапазону рядків у `fileview::Menu` (`StartRangeSelection`, `FinishRangeSelection`, `CancelRangeSelection`, `ClearRangeSelection`) та оновити підказки кнопок футера (`Finish selection` / `Cancel`).
2. Створити процесовий буфер обміну рядками (`s_line_clipboard`) для `CopySelection`, `CutSelection`, `PasteBelow` та `DeleteLine` (із збереженням щонайменше 1 порожнього рядка).
3. Реалізувати чисті функції `CommentIniLine` та `UncommentIniLine` у `text_helper.hpp` з тестами в `test_text_helper.cpp` та підключити їх до дій `Comment` / `Uncomment`.
4. Оновити спливаюче вікно дій рядка (`ShowLineActions`) для динамічного показу дій над виділеним діапазоном або поточним рядком, включно з `Undo` та `Redo`.
5. Малювати виділення напівпрозорою фокусною смугою з alpha 0.35 (`ThemeEntryID_FOCUS`), зберігаючи синтаксичні кольори тексту.
6. Підняти версію до `0.13.465` у `sphaira/CMakeLists.txt`, оновити living docs, пройти перевірку host tests та `git diff --check`.

## Попередній delivery: v0.13.460 — text editor basics

Статус: реалізацію виконано та перевірено. Реалізовано перехід Edit → View на кнопку B
зі збереженням стану та підтвердженням Save/Discard/Cancel при виході з View; блокування
повторного wrap при утриманні Down/Up на межах документа; стабільний рендеринг виділеного
рядка INI з синтаксичним підсвічуванням та перемикання значень 0 ↔ 1 у `ToggleIniBoolean`.
До цього delivery також увійшов ще не закомічений ZL zoom chord: `ZL` + D-pad
або вертикальний стік масштабує текст і не запускає page up після zoom.
Пройдено host test suite (13 suites, 742 declarations), WSL ReleaseWithInstall
(`[100%] Built target sphaira_nro`) та `git diff --check`.

1. Додати `SwitchToViewMode()`: кнопка B в Edit перемикає у View без закриття меню;
   кнопка B у View показує діалог збереження для зміненого документа.
2. В Edit mode блокувати автоповтор wrap при утриманні кнопки на першому чи останньому
   рядку; скидати блокування при відпусканні кнопки.
3. Зберегти синтаксичні кольори та однаковий розмір шрифту для виділеного рядка INI.
4. Розширити `ToggleIniBoolean()` для перемикання 0 ↔ 1 із тестами в `test_text_helper.cpp`.
5. Підняти версію до `0.13.460`, оновити living docs, пройти тести та збірку.

## Попередній delivery: v0.13.458 — Homebrew settings & search paths

Статус: реалізацію виконано та перевірено. Додано виділену категорію
`Homebrew` у Settings одразу після `General`, куди перенесено `Forwarders`,
`Homebrew App Store` та `Replace hbmenu on exit` без дублювання. Додано
менеджер `Homebrew Search Paths` для додавання лише microSD-папок, збереження
користувацьких шляхів у конфіг, їх перегляду та видалення з підтвердженням,
із негайним оновленням переліку Homebrew (системний шлях `/switch` залишається
незмінним та прихованим від конфігу). Оновлено 13 локалізацій (без `ru.json`).
Пройдено валідацію JSON, host test suite (13 suites, 742 declarations) та
`git diff --check`.

1. Додати окрему категорію `Homebrew` у Settings одразу після `General`.
2. Перенести до нової категорії `Forwarders` (з `Install`), `Homebrew App Store`
   (з `Software`) та `Replace hbmenu on exit` (з `General`).
3. Реалізувати `Homebrew Search Paths`: додавання лише SD-папок через FilePicker,
   збереження користувацьких шляхів у конфіг, показ списку та видалення через
   OptionBox із миттєвим `SignalChange()` / оновленням NRO.
4. Оновити 13 файлів локалізації в `assets/romfs/i18n/*.json` (крім `ru.json`).
   Підняти версію `0.13.457 → 0.13.458`, оновити living docs, перевірити валідність
   JSON, пройти host tests та `git diff --check`.

## Попередній delivery: v0.13.457 — text viewer viewport scrolling

Статус: реалізацію виконано та перевірено. У read-only text viewer Up/Down,
D-pad та обидва стіки зміщують вікно на один рядок одразу без затримок курсора;
streamed reader тримає буфер рядків наперед і плавно переходить між сторінками.
Збережено release-based L/R (сторінка), ZL/ZR (10 сторінок), L + right-stick zoom,
pinch zoom та неклікабельні footer hints. Пройдено host test suite (13 suites,
742 declarations), WSL ReleaseWithInstall (`[100%] Built target sphaira_nro`) та
`git diff --check`.

1. У read-only viewer відокремити cursor/editing semantics від прокрутки
   viewport: Up/Down, лівий і правий стіки зміщують вікно на один рядок одразу.
2. Для streamed reader тримати буфер щонайменше на один viewport попереду,
   обчислювати наступний page offset після одного видимого viewport і плавно
   переходити на нього на межі. Не індексувати весь файл і зберегти bounded
   cache.
3. Зберегти release-based L/R, ZL/ZR, L + right-stick zoom, one-finger swipe,
   pinch zoom і неклікабельні footer hints. Підняти `0.13.456 → 0.13.457`,
   оновити living docs і пройти host suite, WSL build та `git diff --check`.

## Попередній delivery: v0.13.456 — text viewer pager

Статус: реалізацію прийнято після Gemini junior-review. Контекстна дія працює
для кожного звичайного файла, а великі файли читаються ліниво малими
сторінками; коротке або помилкове читання завершує viewer через чинний error
box без повторного discovery того самого offset. Gemini пройшов `tests/run.sh`
(13 suites, 742 declarations), WSL `ReleaseWithInstall` (`sphaira_nro`) і
`git diff --check`. Потрібний Switch smoke-test пейджера та жестів.

1. Додати в File Browser один `View as text` для будь-якого звичайного файла.
   Залишити автоматичний View за known text extension, а інсталяцію, image,
   archive, NRO та file associations не змінювати.
2. Зберегти чинний in-memory editor лише для файлів до 4 MiB. Для більших
   відкрити read-only paged reader: тримати лише поточну і кілька наступних
   сторінок тексту, байтові offsets сторінок та невеликий chunk buffer; не
   читати або не індексувати весь файл наперед.
3. У read-only text view: Up/Down і обидва стіки рухаються рядком; `L`/`R`
   перегортають назад/уперед одну сторінку на release; `ZL`/`ZR` — десять.
   Утриманий `L` + правий стік змінює масштаб без випадкової дії від drift;
   release `L` гортає назад лише якщо L не був modifier. Підтримати pinch zoom
   через фактичний two-touch input. Ніякий paging/zoom footer hint не повинен
   спрацьовувати від touch, але scroll і pinch залишаються touch actions.
4. Змінювати масштаб у практичних межах, перебудовуючи viewport/page rows і
   зберігаючи поточну позицію документа настільки точно, наскільки дозволяє
   потоковий offset. Додати одну host-перевірку page boundaries/line stepping,
   підняти версію `0.13.455 → 0.13.456`, оновити task/plan/walkthrough та
   пройти host suite, WSL build і `git diff --check`.

## Попередній delivery: v0.13.455 — INI text viewer spacing

Статус: локальну причину накладання знайдено в `fileview::Menu::DrawText`: номер рядка виставляє NanoVG на 16 px, після чого ключ вимірювався цим самим розміром, але малювався в 18 px. Перед `gfx::textBounds` ключа відновлено 18 px, тому початок `=` і значення відповідає фактично намальованій ширині ключа.

1. Зберегти чинні кольори, INI parser, clipping і компонування gutter без нових UI-механізмів.
2. Встановити 18 px лише перед вимірюванням `key_str` у shared INI draw path.
3. Підняти версію `0.13.454 → 0.13.455`, оновити living docs і пройти `git diff --check`; target WSL `sphaira` успішний, але повний `ReleaseWithInstall` окремо блокується відсутньою Make-ціллю `sphaira/sphaira.elf` під час NRO-пакування.
4. На Switch відкрити `system_settings.ini` з ключем на кшталт `enable_send_rights_usage_status_request`; його значення має починатися після ключа без накладання.

## Попередній delivery: v0.13.454 — NSP install diagnostics

Статус: реалізацію виконано та перевірено. Діагностичні повідомлення встановлення NSP та перевірка версії HOS інтегровані в єдину спільну точку опису помилок `ui::GetResultDescription(Result)`. Gemini успішно виконав host checks (`test_version_compare` 34 checks, `tests/run.sh` all green), JSON parser валідацію всіх 14 мов і WSL `ReleaseWithInstall` (`[100%] Built target sphaira_nro`).

1. У `sphaira/source/ui/error_box.cpp` додати описи для `MAKERESULT(Module_Libnx, LibnxError_IncompatSysVer)`, `Result_StreamUnexpectedEof` та `Result_NspBadMagic`.
2. Для HOS несумісності відображати мінімальну версію 4.0.0 через `version::FormatPacked`, динамічну встановлену версію через `hats::getSystemFirmware()` та рекомендацію оновити системну прошивку.
3. Для `Result_StreamUnexpectedEof` та `Result_NspBadMagic` додати чіткі інструкції повторного копіювання/завантаження, не зачіпаючи стандартні файлові чи криптографічні помилки.
4. Додати локалізацію 3 ключів до всіх 14 JSON-файлів у `assets/romfs/i18n/` та перевірити їх валідність.
5. Розширити `tests/test_version_compare.cpp` перевіркою форматування 4.0.0 і підтвердити збірку в WSL.

## Попередній delivery: v0.13.454 — Homebrew multi-select actions

Статус: реалізацію прийнято після Gemini junior-review. Shared grid renderer
залишився єдиним джерелом візуальної семантики selection: List малює checkbox
у боковому gutter, а tile layouts — overlay. Gemini пройшов `tests/run.sh`
(усі 13 suite green, 734 header declarations), WSL `ReleaseWithInstall`
(`Built target sphaira_nro`) і `git diff --check`; лишився Switch smoke-test.

1. Повторно використати в `homebrew::Menu` чинну поведінку Games: `X` змінює
   вибір поточного NRO та переходить до наступного, `Y` інвертує вибір, `B`
   очищує вибір до виходу; не створювати окремий checkbox/layout механізм.
2. Передати `selected` до `grid::Menu::DrawEntry` і використати
   `DrawSelectionMark`, який уже малює checkbox у боковому gutter для List та
   позначку/overlay для плиткових макетів.
3. У Homebrew Options показувати число targets і масові `Star`/`Unstar` лише
   коли відповідна операція має роботу. `Delete` мусить вимагати підтвердження,
   обробляти кожен результат і після успіху перезчитувати список. Не діяти на
   синтетичному Kefir Updater stub.
4. Підняти версію `0.13.453 → 0.13.454`, оновити task/plan/walkthrough,
   додати найменшу потрібну перевірку, пройти host suite, WSL build і
   `git diff --check`; вручну перевірити X/Y, List/Grid/HB Menu та всі три
   контекстні дії на Switch.

## Попередній delivery: v0.13.453 — PFS0/NSP parser hardening

Статус: реалізацію прийнято після ручного Gemini junior-review циклу. Деталі
baseline-доказів і межі scope — у
[`pfs0_nsp_hardening_audit.md`](pfs0_nsp_hardening_audit.md). Зафіксовано exact
metadata reads, limits, checked arithmetic, bounded names і known-size bounds у
спільному PFS0 parser; невідомі streams лишаються підтриманими через штатний
`FsError_NotImplemented` size result.

1. `Nsp::GetCollections()` вимагає exact header/file-table/string-table reads,
   перевіряє всі metadata-derived allocation, offsets і `CollectionEntry` до їх
   publication, не змінюючи чинний chunk-aggregation `source::Stream::Read()`.
2. `pfs0.hpp` зберігає binary-layout asserts, caps `0xFFFF` files / 4 MiB
   string table, checked arithmetic, bounded NUL search і parsed known-size
   ends. Common source `GetSize()` передає file/NCA/buffer capacity у parser;
   лише `FsError_NotImplemented` означає unknown-size stream.
3. `tests/test_pfs0_nsp.cpp` покриває valid layout, short reads, hostile
   allocations, invalid/missing-NUL names, overflow і known-size overrun.
4. Gemini фактично виконав focused test (41 checks), `tests/run.sh` (`all green`),
   WSL `ReleaseWithInstall` (`[100%] Built target sphaira_nro`) і
   `git diff --check`. Senior review охопив parser, всі GetSize adapters і
   PFS0/NCA callers.
5. Версію піднято `0.13.452 → 0.13.453`; зміна parser/test/document-only, тому
   Switch hardware/manual check не потрібний.

## Попередній delivery: v0.13.452 — відновлення loader thread affinity перед NRO

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
