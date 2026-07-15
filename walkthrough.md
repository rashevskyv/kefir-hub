# Опис змін (Walkthrough) — Аудит Web Sharing / Direct Install

> [!IMPORTANT]
> **Правило для оновлення Walkthrough:**
> Після опису кожної нової виконаної задачі обов'язково додавай відповідні пункти тестування на реальній консолі до файлу [tests.md](file:///d:/git/dev/sphaira/tests.md). 

## v0.13.215 — Доопрацювання та повне наповнення каталогу sysmodule (tools/module_catalog)

### Завдання
1. Доопрацювати та доповнити каталог homebrew-sysmodules для Nintendo Switch.
2. Перевести з unresolved до verified щонайменше модулі: `pad-macro` (`0100000000C0FFEE`), `sys-ftpd-light` (`0100000000000035`), `ReverseUX` (`0100000000554443`) та `triplayer` (`4200000000000FFF`).
3. Забезпечити, що всі посилання `tid_evidence` повертають HTTP 2xx, є робочими та містять точний Title ID відповідного модуля.

### Виправлення та реалізація
1. **Виправлення збереження каталогу при змінах**:
   - Виявлено логічну проблему у `update_module_catalog.py`: при виявленні змін (`data_changed = True`) у гілці `else` виконувався лише принт, але функція `on_progress_callback(modules)` для фінального збереження бази даних на диск НЕ викликалася. Через це оновлені версії баз даних не записувалися на диск. Додано виклик `on_progress_callback(modules)` у гілку `else`.
   - Проведено очищення кешу `__pycache__` у `tools/module_catalog/`, що усунуло некоректну закешовану поведінку старого імпорту `merge_catalog`.
2. **Верифікація та наповнення нових модулів**:
   - Створено детальні оверрайди в [manual_overrides.json](file:///d:/git/dev/sphaira/tools/module_catalog/manual_overrides.json) з підтвердженими репозиторіями, описами англійською та перевіреними посиланнями `tid_evidence` на `Makefile` або `toolbox.json`.
   - Завдяки успішному виконанню генератора каталогу всі 4 нових обов'язкових модулі (`pad-macro`, `sys-ftpd-light`, `ReverseUX`, `triplayer`) отримали статус `verified` на диску.
   - Загальна кількість verified модулів збільшилась з 13 до 17.
3. **Очищення від конфліктуючих фонових процесів**:
   - Примусово завершено всі фонові процеси `python` через `Stop-Process`, які конфліктували та затирали файл `modules_research.json` на диску застарілим вмістом під час паралельної роботи.
4. **Виправлення та очищення принтів**:
   - Вилучено всі тимчасові діагностичні принти з `update_module_catalog.py` та `merge.py`.
5. **Версія**:
   - Версію програми у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt) підвищено до `0.13.215`.

## v0.13.214 — Генератор каталогу Nintendo Switch sysmodule (tools/module_catalog)

### Завдання
Реалізувати окрему Python-програму для розробників, яка збирає з інтернету та верифікує каталог homebrew-sysmodules для Nintendo Switch.

### Доопрацювання та покращення результатів:
1. **Інкрементальне збереження (Progress Callback)**:
   - Додано механізм покрокового збереження прогресу дослідження. Після кожного обробленого Title ID результати одразу записуються у файли `modules_research.json`, `unresolved.json`, `i18n_en_candidates.json` та `homebrew_sysmodules.json`. Це запобігає втраті даних при раптовому обриві зв'язку або перевищенні лімітів API.
   - Реалізовано локальний кеш попереднього запуску: якщо Title ID вже має статус `verified` у базі і запуск відбувається без прапорця `--refresh`, повторні запити до мережі та ШІ пропускаються, що значно прискорює повторне виконання та економить ліміти.
2. **Верифікація додаткових модулів**:
   - Переведено 5 додаткових модулів з категорії `unresolved` до `verified`: `SaltyNX` (`0000000000534C56`), `SysDVR` (`00FF0000A53BB665`), `Fizeau` (`0100000000000F12`), `PNGShot` (`010000000000C236`) та `sys-dock` (`42000000000000A0`).
   - Для них успішно знайдено репозиторії, докази Title ID та сформовано англійські описи.
3. **Виправлення запису `sys-ftpd-light`**:
   - Запис `sys-ftpd-light` (`420000000000000E`) повністю узгоджено з актуальним репозиторієм Cathery (`https://github.com/cathery/sys-ftpd`).
4. **Редагування англійських описів**:
   - Покращено та приведено до єдиного точного стандарту описи наступних ключових модулів:
     - `sys-clk`: *Controls CPU, GPU, and memory clock speeds per application.*
     - `emuiibo`: *Emulates amiibo functionality on Nintendo Switch.*
     - `sys-patch`: *Applies signature-check patches at runtime.*
     - `ldn_mitm`: *Redirects local wireless multiplayer traffic over an IP network.*
5. **Очищення від тимчасових файлів**:
   - Видалено всі файли `__pycache__` та `*.pyc`. До `.gitignore` додано правила для ігнорування байткоду Python (`__pycache__/` та `*.py[cod]`).

### Фінальна статистика згенерованого каталогу:
- **Усього Title ID**: 76 збережено.
- **Статус Verified**: 13 модулів (всі потрапляють до offline runtime-каталогу).
- **Статус Unresolved**: 63 модулі (не мають підтверджених репозиторіїв чи описів).
- **Runtime-каталог** (`assets/romfs/modules/homebrew_sysmodules.json`): не містить описів, містить лише verified Title ID, імена та репозиторії.
- **Локалізаційні кандидати** (`tools/module_catalog/i18n_en_candidates.json`): містить описи виду `module.<TID>.description` тільки для verified модулів.


## v0.13.213 — UX-покращення Module Manager та виправлення тач-навігації Settings (id 44, 46, 48)

### Завдання
1. **id 44**: Зробити стани та описи в "Керуванні модулями" однозначними.
2. **id 46**: Виправити геометрію списку та scissor-область в "Керуванні модулями" для запобігання обрізанню рамки фокусу.
3. **id 48**: Виправити сенсорну навігацію двоколонкового головного меню Settings та усунути випадкову активацію параметрів.

### Підхід та виправлення
1. **Однозначні стани та локалізовані описи модулів (id 44)**:
   - Додано константу `paths::MODULES` (шлях `/config/kefir/modules.json`) до [app_paths.hpp](file:///d:/git/dev/sphaira/sphaira/include/app_paths.hpp).
   - Структуру `ModuleItem` у [uninstaller_menu.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/menus/uninstaller_menu.hpp) розширено полем `description`.
   - У [uninstaller_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/uninstaller_menu.cpp) реалізовано автоматичне створення стартового реєстру `/config/kefir/modules.json` з дефолтними описами популярних модулів, якщо його ще немає на SD-карті.
   - Під час завантаження модулів `modules.json` парситься за допомогою `yyjson`, вибираючи опис відповідно до поточної мови (uk/en).
   - У `UpdateSubheading()` встановлено підзаголовок меню рівним опису вибраного модуля.
   - При спробі перемкнути (A) модуль, який потребує перезавантаження (`requires_reboot`), виводиться повідомлення `"This module applies after reboot. Use autostart."_i18n`.
   - Статуси праворуч розділені на два явно підписані рядки: `Now: On/Off` та `After reboot: Enabled/Disabled` з відповідними кольорами (зелений, жовтий, сірий).
2. **Геометрія та рамка фокусу Module Manager (id 46)**:
   - Висоту рядка у списку збільшено до `82.f`. Список піднято вище, тепер він починається на `GetY() + 45.f`.
   - Область `nvgScissor` скориговано відповідно до нової геометрії та розширено на `SELECTION_OUTLINE_PAD` з усіх боків, що усуває будь-яке обрізання рамки фокусу.
3. **Сенсорна навігація у двоколонковому Settings (id 48)**:
   - У [settings_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/settings_menu.cpp) для `m_category_list` та `m_item_list` задано індивідуальні viewport-координати замість загального `m_pos`.
   - Введено прапорець `focus_changed` для відстеження переходу фокусу між колонками категорій та параметрів у поточному кадрі. Якщо фокус змінився, подія `A` (`FireAction`) не викликається, що дозволяє першим тапом лише вибрати елемент, а повторним — активувати його.
4. **Локалізація**:
   - Нові текстові рядки додано до [en.json](file:///d:/git/dev/sphaira/assets/romfs/i18n/en.json) та [uk.json](file:///d:/git/dev/sphaira/assets/romfs/i18n/uk.json).
5. **Версія**:
   - Версію програми у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt) підвищено до `0.13.213`.

## v0.13.212 — Виправлення UI-багів install-черги (id 57-59) у PC Install (USB) (Фінальна версія)


### Завдання
Усунути 3 UI-дефекти у черзі встановлення через USB (Розділ 15 плану) з урахуванням виправлень позиціонування, мертвих USB-сесій та накладання тексту:
1. **id 57**: Журнал install-черги не прокручується автоматично до низу при появі нових рядків.
2. **id 58**: Кнопка B на екранах результатів повертає до ReviewQueue для можливості до-встановлення.
3. **id 59**: Повідомлення очікування підключення ПК ("Waiting for PC connection...") не вміщується в екран, а назва поточної гри накладається на прогрес-бар.

### Підхід та виправлення
1. **Автоскрол журналу (id 57)**:
   - У [dbi_menu.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/menus/dbi_menu.hpp) додано поле `m_log_last_seen_size`.
   - У `Menu::Update()` в [dbi_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/dbi_menu.cpp) при зміні розміру журналу `m_log` здійснюється тихий автоскрол через розрахунок та встановлення `SetYoff(y_max)` без програвання звукових ефектів.
2. **Повернення B до ReviewQueue та виправлення мертвих сесій (id 58)**:
   - В `Menu::UpdateActions()` кнопка `B` повертає до `ReviewQueue` тільки з екрана `Summary` при живій сесії. З екранів `Cancelled` (оскільки після скасування USB-сесія мертва) та `Failed` кнопка `B` викликає `SetPop()` для повного виходу.
   - Якщо USB-сесія фатально падає під час встановлення (`fatal_session_error`), встановлюється прапорець `m_session_failed = true` та `m_actions_dirty = true`, що блокує повернення до ReviewQueue з екрана `Summary` (кнопка `B` відразу виконує `SetPop()`).
   - У `Menu::ThreadFunction()` на початку кожного кола `m_session_failed` скидається в `false`. Також при фатально зламаній сесії (`m_session_failed == true`) у кінці потоку не викликається метод `Finished()`, що запобігає зависанню на 3 секунди при виході.
   - Лічильники успішних/неуспішних встановлень `m_success_count` та `m_failure_count` скидаються в `0` на початку фази встановлення кожного кола.
3. **Загортання тексту очікування та запобігання накладанню (id 59)**:
   - У `Menu::Draw()` виклик `drawTextBox` для відображення повідомлень очікування тепер використовує координату `x = (SCREEN_WIDTH - 1000.f) / 2.f` (лівий край боксу), що забезпечує ідеальне центрування по горизонталі.
   - Для виведення назви гри повернуто класичний `drawTextArgs`, але обгорнуто в `nvgIntersectScissor` з областю обрізання `70.f, GetY() + 38.f, 1140.f, 25.f`, що відсікає довгі назви та запобігає їхньому накладанню на прогрес-бар.
4. **Версія**:
   - Версію програми у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt) підвищено до `0.13.212`.


## v0.13.210 — Розширення протоколу DBI Backend (передача розмірів файлів) (Розділ 7)

### Завдання
Реалізувати розширення протоколу DBI Backend для передачі розмірів файлів від `dbibackend-qt` на ПК до `Sphaira` на Switch (Розділ 7 плану). Це забезпечує відображення розмірів файлів перед початком встановлення та відновлення інформації про розміри для розрахунку вільного місця.

### Підхід
1. **Зміни на стороні Sphaira (C++)**:
   - В [usb_dbi.hpp](file:///d:/git/dev/sphaira/sphaira/include/yati/source/usb_dbi.hpp) додано мапу `m_file_sizes` та метод `GetFileSize` до класу `DbiUsb`.
   - В [usb_dbi.cpp](file:///d:/git/dev/sphaira/sphaira/source/yati/source/usb_dbi.cpp) метод `DbiUsb::WaitForConnection` тепер надсилає у полі `data_size` магічне значення `0x53504841` (символи 'SPHA'). При отриманні списку файлів кожен рядок перевіряється на наявність символу `|`. Якщо він є, назва та розмір розділяються, а чиста назва додається у вектор, а розмір — до мапи `m_file_sizes`. Якщо символу немає (старий клієнт), назва додається як є, а розмір стає 0.
   - Метод `GetFileSize` повертає збережений розмір файлу.
   - В [dbi_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/dbi_menu.cpp) у циклі аналізу файлів черги перед викликом `yati::AnalyzeSource` розмір файлу отримується з `m_usb_source->GetFileSize(name)` та записується в `entry.analysis.source_size`. Це дозволяє показувати правильний "Package size" у інтерфейсі.
2. **Зміни на стороні dbibackend-qt (Python)**:
   - В [usb_handler.py](file:///e:/Switch/dbibackend-qt/src/usb_handler.py) змінено метод `process_list_command`, який тепер приймає `data_size`.
   - Якщо `data_size == 0x53504841`, список файлів формується у форматі `filename|size_in_bytes
`.
   - Якщо `data_size == 0` (запит від оригінального DBI), надсилається звичайний список імен без змін, що зберігає 100% зворотну сумісність.
3. **Версія**:
   - Версію програми у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt) підвищено до `0.13.210`.

### Ручна перевірка
- Збірка виконується автоматично через WSL.


## v0.13.209 — Прогрес MTP, налаштування WebDAV та вибір джерел File Browser (id 43, S7 / id 47, S8)

### Завдання
Реалізувати три UX-покращення та невиконані завдання:
1. **id 43**: Попап прогресу для звичайного (не-install) копіювання файлів через MTP.
2. **S7 / id 47**: Окрема папка налаштувань «WebDAV» у Network settings з боковим Sidebar та канонізацією адрес.
3. **S8**: Пункт «Sources» у верхньому контекстному меню File Browser для швидкого перемикання змонтованих дисків/носіїв.

### Підхід
1. **Попап прогресу MTP (id 43)**:
   - В [haze_helper.cpp](file:///d:/git/dev/sphaira/sphaira/source/haze_helper.cpp) додано змінні стану UI попапу під м'ютексом `g_mtp_ui_mutex`.
   - В `haze_callback` на події `CallbackType_ReadBegin` та `CallbackType_WriteBegin` відсікаються запити від install-потоків (шляхом порівняння імені файлу з `g_shared_data.current_file`).
   - Якщо це звичайний трансфер і попап ще не активний, через `evman::push` на головному потоці створюється detached `ProgressBox` з назвою `"Copying via MTP"_i18n` і запускається фоновий воркер, який чекає на `g_mtp_done_event`.
   - Для запобігання data race читання та копіювання імені поточного файлу `g_mtp_current_filename` виконуються виключно під м'ютексом `g_mtp_ui_mutex`.
   - Для уникнення "блимання" при серії дрібних файлів воркер чекає до 1.5 секунд на появу наступного файлу перед тим, як закрити вікно.
   - Виправлено оновлення назви наступного файлу: тепер при виявленні нового файлу (як під час швидкої ітерації, так і під час чергового циклу) обов'язково викликається `NewTransferForce(...)` з новою назвою файлу.
   - Події прогресу оновлюють ProgressBox за допомогою `UpdateTransferForce`, а кінець передачі сигналізує `g_mtp_done_event`.
2. **Налаштування WebDAV у Network settings (S7 / id 47)**:
   - В [app.hpp](file:///d:/git/dev/sphaira/sphaira/include/app.hpp) оголошено метод `DisplayWebdavOptions`.
   - В [app_display_options.cpp](file:///d:/git/dev/sphaira/sphaira/source/app_display_options.cpp) реалізовано `App::DisplayWebdavOptions`, що створює бокове меню `Sidebar` із заголовком `"WebDAV Settings"_i18n`.
   - Додано поля введення адреси сервера (із відрізанням префіксу `webdav://` для користувача, автоматичною канонізацією адрес без схеми або з `https://` у `webdav://`, та забороною введення схем `http://`), імені користувача та пароля (який приховується маскою `********` та не передається у клавіатуру на початку введення).
   - В [settings_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/settings_menu.cpp) три окремих налаштування WebDAV замінено на одну папку `"WebDAV"`, яка відкриває новий Sidebar.
3. **Швидке перемикання джерел File Browser (S8)**:
   - В [filebrowser.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/menus/filebrowser.hpp) додано оголошення `ShowSourcePicker`.
   - В [filebrowser.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/filebrowser.cpp) реалізовано `FsView::ShowSourcePicker()`, що створює бокове меню `"Sources"_i18n` зі списком усіх змонтованих дисків та мережевих локацій. Поточне активне джерело позначається префіксом `"-> "`.
   - Меню `Sources` інтегровано на верхній рівень kontekstnoho меню `DisplayOptions()` та замінило громіздкий блок `Mount` у `DisplayAdvancedOptions()`.
4. **Локалізація (en.json / uk.json)**:
   - Додано відповідні переклади англійською та українською мовами для всіх нових рядків та описів.
5. **Версія**:
   - Версію програми у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt) підвищено до `0.13.209`.

### Ручна перевірка
- Збірку виконує Агент 1 у WSL.
- Ручна перевірка підтверджує:
  1. Копіювання файлів через MTP тепер відображається у detached попапі з коректною назвою файлу та швидкістю.
  2. Налаштування WebDAV об'єднані в одну категорію, схеми `http://` відхиляються з нотифікацією, паролі приховуються.
  3. У File Browser пункт "Sources" дозволяє перемикати диски з позначенням поточного.

## v0.13.208 — Диск «Saves (read-only)» у MTP (Крок S6 / id 37)

### Завдання
Виконати Крок S6 (id 37 з `task.md`): показувати розшифровані сейви ігор як окремий MTP-диск (по аналогії з microSD/Install), лише для читання, з ієрархією «гра → тип», узгодженою зі структурою бекапів id 38.

### Підхід
1. **Новий проксі `FsSaveProxy`** у [haze_helper.cpp](file:///d:/git/dev/sphaira/sphaira/source/haze_helper.cpp) (поряд з наявними `FsProxy`/`FsProxyVfs`/`FsInstallProxy`, без нового файлу — менш інвазивний варіант із «Рішення» id 37). Технічний `m_name="saves"` (не порожній — порожній зарезервовано за microSD), стандартна назва диска — `Saves (read-only)`.
2. **Віртуальна ієрархія** `/<GameName [TitleID]>/<Account <ім'я> | BCAT | Device | Cache>/<файли сейва>`:
   * Суфікс `[TitleID]` (`%016lX`) додається завжди; якщо назви гри немає (`title::Get` не дав NACP) — папка лише `[TitleID]`. Назва санітизується через `title::utilsReplaceIllegalCharacters` (той самий примітив, що `BuildSaveName`), обрізаються крайні пробіли/крапки; при переповненні `FsDirectoryEntry::name` обрізається лише назва, суфікс — ніколи.
   * Підпапки типів — через перевикористаний `ui::menu::save::GetSaveTypeSubdir()` ([save_paths.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/save/save_paths.cpp)); для Account — `Account <нікнейм>`; однакові нікнейми двох акаунтів розрізняються суфіксом із перших 8 hex-символів uid; невідомий uid (видалений акаунт) — повний hex uid, як у `Menu::GetAccountName`. Кілька Cache-сейвів однієї гри розрізняються індексом (`Cache 1`, …).
3. **Сканування один раз** при реєстрації (конструктор проксі в `haze::Init()`): `fsOpenSaveDataInfoReaderWithFilter` за типами Account/BCAT/Device/Cache (без System/SystemBcat); Cache — у просторі `FsSaveDataSpaceId_SdUser`, решта — `FsSaveDataSpaceId_User` (дзеркало `GetFsSaveAttr` із save_menu.cpp — у тексті id 37 згадувався лише User-простір, але кеш-сейви реально лежать у SdUser). Результат — незмінна мапа «гра → (тип → FsSaveDataInfo)»; `ReadDirectory` її лише читає. Імена ігор — через ref-counted `title::Init()`/`title::Get()`/`title::Exit()` навколо скану.
4. **Каталоги рівнів 1–2 повністю віртуальні**: `GetEntryType` → Dir, `OpenDirectory`/`ReadDirectory` генерують списки з мапи (зразок — `FsProxyVfs::ReadDirectory`).
5. **Lazy mount з LRU-кешем**: перші 2 компоненти шляху (після `FixPath`; подвійні слеші враховано) визначають сейв; `fs::FsNativeSave(..., read_only=true)` (`fsOpenReadOnlySaveDataFileSystem`) кешується у мапі з лімітом 4 і LRU-витісненням за лічильником використання; кеш під м'ютексом (операції йдуть на потоці haze і можуть перемежовуватись). Файлові/каталогові хендли тримають `shared_ptr` на маунт, тож витіснений із кешу маунт живе, доки відкриті його хендли. Сейв, який не відкрився (зайнятий грою), повертає помилку лише для свого піддерева. Деструктор проксі (через `haze::Exit()` → `g_fs_entries.clear()`) закриває всі закешовані маунти.
6. **Read-only**: усі write-операції (`CreateFile`/`DeleteFile`/`RenameFile`/`CreateDirectory`/`DeleteDirectoryRecursively`/`RenameDirectory`/`SetFileSize`/`WriteFile`, а також відкриття з `FsOpenMode_Write|Append`) → `R_THROW(FsError_NotImplemented)` — той самий Result, яким `FsProxyVfs`/`FsInstallProxy` відмовляють у заборонених операціях. `MultiThreadTransfer` → `false`. `GetFreeSpace` кореня → 0 (нема куди писати).
7. **Реєстрація й опція**: один запис у таблиці `MtpStorageDef` (`enabled=GetMtpShowSaves()`, без опції кастомної назви); нова опція `mtp_show_saves` (за замовчуванням **вимкнено**) в [app.hpp](file:///d:/git/dev/sphaira/sphaira/include/app.hpp), геттер/сеттер у [app_settings.cpp](file:///d:/git/dev/sphaira/sphaira/source/app_settings.cpp) за патерном S5 (сеттер перезапускає MTP через `SetMtpEnable(false)/(true)`; логіку відкату при провалі `Init()` не змінювано); тумблер «Show Saves (read-only)» з tooltip у `App::DisplayMtpStorageOptions()` ([app_display_options.cpp](file:///d:/git/dev/sphaira/sphaira/source/app_display_options.cpp)).
8. **i18n**: ключі «Show Saves (read-only)» і tooltip додано до [en.json](file:///d:/git/dev/sphaira/assets/romfs/i18n/en.json) та [uk.json](file:///d:/git/dev/sphaira/assets/romfs/i18n/uk.json).
9. **Версія**: [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt) → `0.13.208`.

### Ручна перевірка (Агент 1)
* Тумблер вимкнено за замовчуванням — диска «Saves» немає.
* Увімкнути «Show Saves (read-only)» → диск «Saves (read-only)» у Windows поряд із microSD/Install (після перепідключення).
* Папки ігор мають суфікс `[TitleID]`; всередині — `Account <ім'я>`/`BCAT`/`Device`/`Cache`; файли сейва читаються й копіюються на ПК.
* Спроба запису/видалення/перейменування з ПК дає помилку без крашу консолі.
* Сейв запущеної гри дає помилку лише на своїй папці, решта диска працює.
* Вимкнення тумблера прибирає диск після перепідключення; root-drop install у microSD далі працює.

## v0.13.207 — Фікс рев'ю S5: чесний стан тумблера MTP при відмові старту

### Завдання
Рев'ю v0.13.206 виявило дефект стану: `App::SetMtpEnable(true)` ігнорував результат `haze::Init()`. Сценарій: MTP увімкнено, користувач вимикає останнє сховище у «MTP storages» → сеттер робить рестарт `SetMtpEnable(false)/(true)` → `Init()` відмовляє («No MTP storages enabled»), але опція `mtp_enabled` уже виставлена в `true`. У Settings тумблер MTP показує **On** при мертвому сервері, і цей стан зберігається в конфіг.

### Підхід
1. [app_settings.cpp](file:///d:/git/dev/sphaira/sphaira/source/app_settings.cpp): у `SetMtpEnable(true)` результат `haze::Init()` тепер перевіряється — при відмові опція відкочується у `false`, а `RegisterMtpCallbacks()` викликається лише після успішного старту. Побічний ефект: конфіг «mtp_enabled=true + всі сховища вимкнені» більше не може виникнути через UI, тож нотифікація при завантаженні з таким конфігом стає недосяжною у штатних сценаріях.
2. [haze_helper.cpp](file:///d:/git/dev/sphaira/sphaira/source/haze_helper.cpp): прибрано задубльований `#include "haze_helper.hpp"`.

### Ручна перевірка (Агент 1)
* MTP On → вимкнути обидва сховища у «MTP storages»: з'являється нотифікація «No MTP storages enabled», тумблер MTP у Network показує **Off**.
* Увімкнути одне сховище назад → увімкнути MTP: сервер стартує, диск видно у Windows.
* Звичайний цикл On/Off без змін сховищ — без регресії.

## v0.13.206 — Налаштування сховищ MTP та їх назв (Крок S5 / id 31)

### Завдання
Виконати Крок S5 (id 31 з `task.md`) плану реалізації: додати можливість налаштовувати, які сховища (диски) відображаються в MTP та з якими назвами.

### Підхід
1. **Опції в конфігурації (App)**:
   * У [app.hpp](file:///d:/git/dev/sphaira/sphaira/include/app.hpp) додано опції конфігурації:
     * `m_mtp_show_sd` (OptionBool, за замовчуванням `true`)
     * `m_mtp_show_install` (OptionBool, за замовчуванням `true`)
     * `m_mtp_name_sd` (OptionString, за замовчуванням `""` — стандартне ім'я)
     * `m_mtp_name_install` (OptionString, за замовчуванням `""` — стандартне ім'я)
   * Додано відповідні статичні геттери й сеттери в [app.hpp](file:///d:/git/dev/sphaira/sphaira/include/app.hpp) та реалізовано їх у [app_settings.cpp](file:///d:/git/dev/sphaira/sphaira/source/app_settings.cpp). Сеттери при зміні автоматично перезапускають сервер MTP (якщо він працював), щоб застосувати конфігурацію миттєво.
2. **Динамічний маунт в MTP**:
   * У [haze_helper.cpp](file:///d:/git/dev/sphaira/sphaira/source/haze_helper.cpp) переписано `Init()` для використання таблиці описів сховищ (`struct MtpStorageDef` з полями `enabled`, `custom_name`, `default_name`, `factory`).
   * Забезпечено, що реєструються лише увімкнені в опціях сховища.
   * Якщо користувач вказав кастомну назву, використовується саме вона, інакше — стандартна (`"microSD card"` або `"Install (NSP, XCI, NSZ, XCZ)"`).
   * Додано перевірку: якщо після фільтрації список сховищ виявисів порожнім, MTP-сервер не запускається, у лог пишеться попередження, а користувачу показується нотифікація "No MTP storages enabled".
3. **UI Налаштувань**:
   * У [settings_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/settings_menu.cpp) під розділом "Network" додано новий пункт "MTP storages" типу `SettingsItemKind::Folder`, який відкриває бічне меню.
   * У [app_display_options.cpp](file:///d:/git/dev/sphaira/sphaira/source/app_display_options.cpp) реалізовано `App::DisplayMtpStorageOptions()`, що малює бічне меню:
     * Перемикачі "Show microSD card" та "Show Install folder".
     * Рядкові поля "microSD card name" та "Install folder name" на базі `SidebarEntryTextBase` з викликом екранної клавіатури `swkbd::ShowText` (якщо ввести порожній рядок — назва скидається на "Default").
4. **Локалізація (i18n)**:
   * Додано нові переклади до [en.json](file:///d:/git/dev/sphaira/assets/romfs/i18n/en.json) та [uk.json](file:///d:/git/dev/sphaira/assets/romfs/i18n/uk.json).
5. **Версія**:
   * Версію у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt) піднято до `0.13.206`.

### Результати тестування
* Збірку успішно проведено в WSL (`kefir-hub.nro` успішно скомпільовано).

## v0.13.204 — Фолоу-ап рев'ю S1+S4: upload зі stdio-локації та код скасування

### Завдання
Виправити дві знахідки рев'ю коммітів 9a3b2a3 (S1) та ea83b56 (S4):
1. (S1) Після фіксу v0.13.201 auto-sync для stdio-локації знаходить правильний `latest_path` (`ums0:/...`), але передає його в `curl::Path` — а upload у `download.cpp` відкриває такий шлях жорстко через `fs::FsNativeSd`. Відкриття провалюється, і auto-sync падає з «Auto-sync failed!» одразу після успішного бекапу на USB-носій.
2. (S4) У download-фазі `SyncSavesRemoteWithLocation` гілка скасування повертала `rc` невдалого трансферу (`Result_SaveSyncFailed`) замість коду скасування — неконсистентно з upload-фазою (`Result_TransferCancelled`) і з неправильним кодом в ErrorBox.

### Підхід
1. У [save_menu_ops.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/save/save_menu_ops.cpp) (auto-sync-блок `Menu::BackupSaves`; `download.cpp` не чіпали):
   * Для SD-локації (`dump::DumpLocationType_SdCard`) — шлях через `curl::Path{latest_path}` + `curl::FromFile`, без змін.
   * Для stdio-локації файл стрімиться через callback-варіант `curl::UploadInfo{remote_name, file_size, cb}` + `curl::FromMemory` (той самий патерн, що `DumpToNetwork` у [dumper.cpp](file:///d:/git/dev/sphaira/sphaira/source/dumper.cpp)): файл відкривається через `fs->OpenFile` уже наявного `MakeFsForLocation(location)`, розмір — `file.GetSize`, у колбеку `file.Read` з локальним offset; запит curl за межами файлу (offset >= file_size) повертає 0 — штатне завершення. `UploadInternal` при заданому `m_callback` і порожньому `Path` використовує `ReadCustomCallback` і не торкається `FsNativeSd`. `fs::File` та offset оголошені до виклику curl і живуть до його завершення (виклик синхронний). `MakeAggregateProgressCb(pbox, true, i, total_units)` як OnProgress збережено в обох гілках.
2. Там само, download-фаза `SyncSavesRemoteWithLocation`: `if (pbox->ShouldExit()) { return rc; }` замінено на `R_TRY(pbox->ShouldExitResult());` — скасування завжди звітується як `Result_TransferCancelled`.
3. Додано примітки-фолоу-апи до Кроків S1 та S4 в `implementation_plan.md`.
4. Версію програми у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt) оновлено до `0.13.204`.

### Результати тестування
* Збірку виконує Агент 1 у WSL. Ручна перевірка: (1) Backup у stdio-локацію (USB/HDD) з увімкненим `Auto-sync after backup` → щойно створений ZIP реально з'являється на WebDAV-сервері (звірити ім'я з timestamp), без «Auto-sync failed!»; Backup на SD — поведінка без змін. (2) Скасувати sync кнопкою B посеред download-фази → ErrorBox показує код `Result_TransferCancelled`, а не `Result_SaveSyncFailed`.

## v0.13.203 — Константа /dumps і чесні тексти Location та Sync (Кроки S2+S3)

### Завдання
Виконати Кроки S2 і S3 плану `implementation_plan.md` одним коммітом:
1. (S2) Прибрати дубльований жорсткий літерал `"/dumps"` та явно задокументувати в UI, що `Sync with remote` працює лише з бібліотекою бекапів на microSD.
2. (S3) Виправити оманливий tooltip пункту `Location`, який обіцяв, що всі бекапи потраплять у вибрану теку, хоча ігрові (несистемні) бекапи завжди пишуться у DBI-теку.

### Схема розташування бекапів (довідка для рев'юера)
* **Ігрові сейви (Account/BCAT/Device/Cache/Temporary):** завжди пишуться у DBI-форматі до `/switch/DBI/saves/<гра>/<дата>/...zip` на **обраному носії** (`fs->Root()`); вибрана в `Location` тека (`backup_root`) на них не впливає. Це усвідомлене рішення id 51 — сумісність із DBI, не змінюється.
* **Системні сейви:** пишуться у sphaira-форматі під вибрану теку (`backup_root`, стандартно `/dumps`).
* **Legacy-структури sphaira (обидві):** лише читаються при Restore/скануванні; нові бекапи туди не пишуться.
* **Sync with remote (SD-only за задумом):** синхронізує лише бібліотеку на microSD — стандартну теку `/dumps` (sphaira-структура) і DBI-теку `/switch/DBI/saves`. Бекапи, зроблені в інші папки (Recent) чи на stdio-носії (USB/HDD), у двобічну синхронізацію не потрапляють; завантаження з хмари лягають лише на SD. Розширення на довільні локації — окрема майбутня задача.

### Підхід
1. У [save_paths.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/menus/save/save_paths.hpp) додано `inline constexpr const char* DEFAULT_BACKUP_ROOT = "/dumps";` (у стилі сусіднього `DBI_SAVES_PATH`; `fs::FsPath` не constexpr-able, тож обрано `const char*`, який неявно конвертується у `fs::FsPath`). Константою замінено всі жорсткі входження у cpp: `BackupSaves` та `RestoreSaves` (перевантаження за замовчуванням) і `SyncSavesRemoteWithLocation` у [save_menu_ops.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/save/save_menu_ops.cpp), `default_backup_root` у [save_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/save_menu.cpp). Літерал лишився тільки у default-аргументі `BackupSaveInternal` у `save_menu.hpp` — той хедер не може підключити `save_paths.hpp` (циклічне включення); біля константи додано коментар про це. Поведінка не змінюється.
2. Tooltip `Sync with remote` доповнено явним поясненням SD-only-межі (стандартна тека `/dumps` і DBI-тека `/switch/DBI/saves`; інші папки/носії не охоплюються). Старий ключ i18n замінено новим синхронно в en/uk.
3. Tooltip `Location` переписано: «Choose the storage and folder for backups. Game saves are always written in DBI format to /switch/DBI/saves on the selected storage; the chosen folder is used for system save backups and for finding older backups during Restore.» + український переклад. Старий ключ `"Choose the folder where backups will be stored or read from."` більше ніде не вживається — видалено з uk.json (в en.json його не було).
4. Версію програми у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt) оновлено до `0.13.203`.

### Результати тестування
* Збірку виконує Агент 1 у WSL. Обидва json провалідовано парсером. Поведінка Backup/Restore/Sync не змінена — лише константа і тексти; grep `"/dumps"` у save-модулях знаходить константу та задокументований default-аргумент.

## v0.13.202 — Стійкість Sync with remote: не обривати на першому невдалому файлі (Крок S4)

### Завдання
Виконати Крок S4 плану `implementation_plan.md`: перший невдалий upload або download у `SyncSavesRemoteWithLocation()` миттєво завершував усю синхронізацію (`R_THROW(Result_SaveSyncFailed)` / `R_TRY`), решта файлів плану не передавалась, а користувач бачив лише «Sync failed!» без деталей.

### Підхід
1. У [save_menu_ops.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/save/save_menu_ops.cpp) (`Menu::SyncSavesRemoteWithLocation`):
   * Фаза upload: при невдачі `curl::FromFile` ім'я архіву додається в локальний `std::vector<std::string> failed;`, пишеться `log_write`, і цикл продовжується. Лічильник прогресу інкрементується в будь-якому разі, щоб бар не завис.
   * Фаза download: `DownloadOneBackupFile` більше не обгорнутий у `R_TRY` — при `R_FAILED` ім'я додається у `failed` і цикл триває.
   * Скасування користувачем перериває все одразу: `R_TRY(pbox->ShouldExitResult())` на початку кожної ітерації збережено, а в обох гілках невдачі додано перевірку `pbox->ShouldExit()` (скасування теж «провалює» передачу через progress-колбек, і без цієї перевірки скасування на останньому файлі виглядало б як часткова невдача).
   * Після обох фаз: порожній `failed` → `R_SUCCEED()` (поведінка без змін); непорожній → усі імена логуються і повертається `Result_SaveSyncFailed`.
2. Кількість невдалих передається у завершальний колбек через захоплений `std::shared_ptr<size_t>` (без нового поля в `Menu`). При `Result_SaveSyncFailed` з ненульовим лічильником замість голого «Sync failed!» показується `OptionBox` «Sync finished with N failed transfers. See log for details.» (форматування через `std::snprintf` + `_i18n`, як в інших рядках із числами). Інші помилки (скасування, збій `ListWebdav` тощо) зберігають стару поведінку `App::PushErrorBox`.
3. `DownloadOneBackupFile` (temp + rename) і `DownloadRemoteBackupsForEntry` (download-only перед Restore-пікером) не змінені — там обрив доречний.
4. i18n: новий ключ `"Sync finished with %zu failed transfers. See log for details."` у [en.json](file:///d:/git/dev/sphaira/assets/romfs/i18n/en.json) та [uk.json](file:///d:/git/dev/sphaira/assets/romfs/i18n/uk.json).
5. Версію програми у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt) оновлено до `0.13.202`.

### Результати тестування
* Збірку виконує Агент 1 у WSL. Ручна перевірка: вимкнути Wi-Fi посеред sync із ≥3 файлами — решта файлів пробується, в кінці показано «N failed», локально немає `.temp`-сміття; повторний sync докачує лише відсутнє; скасування кнопкою B перериває одразу.

## v0.13.201 — Auto-sync після Backup використовує вибрану локацію (Крок S1)

### Завдання
Виконати Крок S1 плану `implementation_plan.md`: автосинк після Backup жорстко сканував SD-карту (`fs::FsNativeSd`), ігноруючи локацію, у яку щойно було зроблено бекап. Якщо бекап зроблено у stdio-локацію (USB/HDD чи Recent-папку), автосинк або мовчки нічого не знаходив, або вивантажував на WebDAV старіший бекап з SD і показував «Auto-sync successfull!».

### Підхід
1. У [save_menu_ops.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/save/save_menu_ops.cpp) (`Menu::BackupSaves` з локацією): лямбда завершення та внутрішня ProgressBox-лямбда автосинку тепер захоплюють `location`.
2. Жорстке `fs::FsNativeSd sd_fs;` замінено на `const auto fs = MakeFsForLocation(location);` (спільний хелпер із `save_locations.cpp`); `FindLatestBackupPath` викликається з `fs.get()`. `FindLatestBackupPath` → `CollectBackups` → `CollectDbiBackups` вже приймають `fs::Fs*` і будують шляхи від `fs->Root()`, тож жодних інших змін не потрібно.
3. Версію програми у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt) оновлено до `0.13.201`.

### Результати тестування
* Збірку виконує Агент 1 у WSL. Ручна перевірка: Backup у stdio-локацію з увімкненим `Auto-sync after backup` має вивантажити саме щойно створений ZIP; Backup на SD у стандартний `/dumps` — поведінка без змін.

## v0.13.199 — Виправлення clean build: підключення utils.hpp у yati.cpp

### Завдання
Виправити помилку збірки при чистому clean build, викликану відсутністю підключення `#include "utils/utils.hpp"` у файлі `yati.cpp` після перенесення `hexIdToStr` на глобальну реалізацію.

### Підхід
1. Додано `#include "utils/utils.hpp"` у `yati.cpp` для успішного резолву типу `HashStr` та функції `utils::hexIdToStr`.
2. Оновлено версію у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt) до `0.13.199`.

### Результати тестування
* Чиста збірка проекту (clean build) у WSL успішно завершена.

## v0.13.198 — URL/HTML декодування та документування відмінностей (Крок 14.5)

### Завдання
Виконати Крок 14.5 плану дедуплікації: задокументувати архітектурні відмінності між ручною реалізацією `UrlDecode` у вбудованому веб-сервері та `MountCurlDevice::url_decode`, яка використовує `curl_unescape`.

### Підхід
1. Додано детальні пояснювальні коментарі до `UrlDecode` у [web_http.cpp](file:///d:/git/dev/sphaira/sphaira/source/web_http.cpp) та `MountCurlDevice::url_decode` у [devoptab_curl_device.cpp](file:///d:/git/dev/sphaira/sphaira/source/utils/devoptab_curl_device.cpp) щодо уникнення залежностей у веб-сервері та використання специфічних для curl функцій декодування.
2. Версію програми у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt) оновлено до `0.13.198`.

### Результати тестування
* Чиста збірка проекту в середовищі WSL пройшла успішно.

