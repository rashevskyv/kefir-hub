# План: аудит і переписування MTP Host (v0.13.364 — v0.13.365)

Зовнішній MTP-пристрій (телефон) було видно у Root, але всередині завжди
показувалось порожньо, пристрій то зникав то зʼявлявся, а іноді програма
падала. Нижче — знайдені причини і що з ними зроблено.

## Першопричини

1. **Розмір USB endpoint.** `usbHsIfOpenUsbEp(..., 1, wMaxPacketSize=512, ...)`,
   але кожне читання постило буфер 64 KiB. usb:hs відповідав `2140-0301` на все,
   що не влазило в один urb. Тому маленькі лістинги працювали, а решта — ні.
   Це і є регресія v0.13.357, після якої «перестало показувати файли».
2. **Немає відновлення після збою.** Після невдалого bulk-трансферу фази
   command/data/response розсинхронізовані, але код продовжував слати команди і
   далі вважав себе `connected`. Кожна наступна операція повертала «0 записів» —
   тобто зламаний лінк виглядав як порожній телефон.
3. **Шторм запитів від файлового менеджера.** Фоновий metadata-потік викликав
   `DirGetEntryCount` для кожного рядка. На MTP це повний лістинг папки по USB:
   ~20 рядків → кілька сотень запитів за секунди. Телефон захлинався.
4. **Кеш не працював.** Ключем був parent handle, а резолв шляху його не
   використовував, тож кожен перехід заново вичитував увесь корінь.
5. **Читання файлу.** Fallback на `GetObject` алокував увесь обʼєкт — вектор на
   1.4 GiB для nsp. Плюс короткі читання, які yati не переживає при розборі
   заголовків nca/nsp. Звідси помилка парсингу при встановленні.
6. **Витоки і гонки.** Гілки виходу в циклі сканування лишали відкритими
   interface і обидва endpoint; `g_mtp_mutex` тримався поверх
   `MountNetworkDevice2` (інверсія порядку блокувань з devoptab rwlock);
   демонтування при збої probe звільняло `Device`, на який ще вказували відкриті
   `DIR`/`FILE`.

## Зроблено

- `sphaira/source/utils/devoptab_mtp.cpp` переписано: транспорт, протокол,
  сесія і devoptab розділені; датасети читаються через bounds-checked `Reader`;
  `log_write` прибрано з гарячих шляхів.
- Кеш став path-based; лістинг папки одразу кешує всі дочірні обʼєкти.
- Читання: `GetPartialObject`/`GetPartialObject64` за
  `DeviceInfo.OperationsSupported`, повне читання без коротких відповідей,
  `st_blksize` = 512 KiB щоб newlib не рефілив stdio-буфер по 1 KiB.
- `MountConfig.no_stat_dir` тепер дійсно поважається у filebrowser.
- `fs.cpp`: `Dir::Read`, `Dir::ReadAll` і `DirGetEntryCount` користуються одним
  хелпером `ClassifyStdioEntry` (раніше `ReadAll` мовчки викидав `DT_UNKNOWN`).

## Верифікація

- [x] Збірка `cmake --build --preset ReleaseWithInstall` у WSL, без warnings.
- [ ] Перевірка на реальній консолі: список кореня і вкладених папок телефону,
      перевстановлення кабелю (пристрій має зникати/зʼявлятись коректно),
      встановлення NSP з телефону.

# План: стабілізація MTP Host за логами з залізa (v0.13.366+)

Ітеративний цикл: тест на консолі → лог (`log.txt` + `errors.txt` з SD) →
діагноз → мінімальний фікс → коміт → новий білд на картку. Деталі кожного
раунду — у `walkthrough.md` (розділ v0.13.366 — v0.13.371).

Стан після v0.13.371:

- Лістинги, перепідключення, вихід/вхід у браузер — стабільні (FixDkpBug,
  generation+path у file handle).
- Транспортні збої лікуються на місці: Cancel (0x64) → опитування Get Device
  Status (0x67) до готовності → CLEAR_FEATURE(ENDPOINT_HALT) → retry.
  Сесія рветься тільки якщо телефон не заспокоївся за ~2 с.
- Стрім-транзакції ≤ 16 MiB: тестовий телефон рве довші data phase рівно на
  20 MiB.
- yati переживає збій читання і скасування: wait-цикли ringbuf перевіряють
  GetResults() зсередини, escape-прапорці називають правильний потік.

## Верифікація

- [x] Збірка v0.13.371 у WSL, деплой на SD (обидва nro, md5 збігаються).
- [ ] HW-SMOKE-371: встановлення NSP з телефону до кінця; B у черзі не висить;
      після `recovering link:` читання продовжуються.

# План: Audit Clean-up & Verification Artifact Alignment (v0.13.425)

Завершення останніх рекомендацій аудіту, документаційних застережень та ітерація версії програми:
- Синхронізація другого колбеку у `themezer.cpp:1160` (`if (!alive || !*alive)`).
- Документування у [README.md](README.md#L91) відкритого зберігання API-ключа SteamGridDB у `/config/kefir/config.ini`.
- Ітерація версії в `sphaira/CMakeLists.txt` до `0.13.425`.
- Перевірка актуальності артефактів бінарника NRO та `compile_commands.json`.

## Верифікація
- [x] Збірка бінарника в WSL з `/home/xhr/dev/sphaira` (`[100%] Built target sphaira_nro`), перевірка відповідності міток часу `kefir-hub.nro` та `APP_VERSION="0.13.425"`.

# План: WeakPtr Lifetime Guard for Callbacks (v0.13.424)

Додавання захисту життєвого циклу об'єктів для асинхронних колбеків у редакторі форвардерів, меню homebrew та виборі іконок SteamGridDB:
- Додано `std::shared_ptr<bool> m_alive` та `std::weak_ptr<bool>` перевірки у `Editor` (`forwarder_editor.cpp`), `IconGrid` (`steamgriddb_icon.cpp`) та `homebrew::Menu` (`homebrew.cpp`).
- Впроваджено `weak_alive` гард для точки входу `ChooseIconSource()` (`forwarder_editor.cpp:264`).
- Синхронізовано версію проекту в `sphaira/CMakeLists.txt` (піднято до `0.13.424`).
- Очищено побічну папку `sphaira/graphify-out/` та перезібрано бінарник `sphaira_nro` з `/home/xhr/dev/sphaira`.

## Верифікація
- [x] Збірка `cmake --build --preset ReleaseWithInstall` у WSL з `/home/xhr/dev/sphaira` — `[100%] Built target sphaira_nro`, exit 0.

# План: Forwarder Editor, SteamGridDB integration, Safe NRO update, Thread safety & API key gating (v0.13.423)

Розширення функціоналу створення форвардерів, кастомізації NRO та інтеграції з SteamGridDB з виправленням безпеки, потокобезпеки та обробки скасувань.

## 1. Safe NRO update (`nro.cpp`)
Потоковий 4-кроковий алгоритм модифікації NRO:
- Запис тимчасового файла `<path>.sphaira.tmp` шляхом потокового переносу вихідного NRO та модифікованих заголовків/іконки/NACP/RomFS шматками по 64 КБ (без 2x RAM вектора у пам'яті).
- Попереднє вилучення можливого застарілого `.bak` (`fs.DeleteFile`).
- `fs.RenameFile(path, bak)` та `fs.RenameFile(tmp, path)` з миттєвим відновленням оригіналу при помилці кроку перейменування.

## 2. Async NRO update (`homebrew.cpp`)
- `on_create` повертає `true` негайно для закриття віджета редактора.
- Модифікація NRO виконується у фоновому потоці `ProgressBox`.
- Сортування та оновлення списку `SortAndFindLastFile(true)` виконується виключно у `done`-колбеку на UI-потоці.

## 3. Gating `/apikey` route (`web.cpp`, `steamgriddb_icon.cpp`)
- Захист ендпоінта `/apikey` прапорцем `g_web_request_active` (`std::atomic_bool`).
- При звернуться до `/apikey` у неактивному стані сервер повертає `404 Not Found`.

## 4. Single-funnel options resolution & Thread safety (`app_settings.cpp`, `steamgriddb_icon.cpp`, `owo.cpp`)
- Воронка `App::Install(OwoConfig& config)` на UI-потоці обчислює `config.options = config.options.value_or(GetForwarderOptions())` до передачі у воркер `owo.cpp`.
- `GetApiKey()` читає впроваджений RAM-кеш `g_api_key_cache` під `g_api_key_mutex`, а `g_api_key.Set()` викликається лише на UI-потоці в колбеку завершення.

## 5. Cleanup `title::Init()` (`web.cpp`)
- Вилучено `g_title_initialized` та дублюючі виклики `title::Init()` / `title::Exit()` з `WebShareFolder` та `WebShareStop`.

## Верифікація
- [x] Збірка `cmake --preset ReleaseWithInstall && cmake --build --preset ReleaseWithInstall` у WSL з `/home/xhr/dev/sphaira` — exit 0.

# План: USB protocol unification + screensaver + forwarder setting (v0.13.416)

Три незалежні зміни, обʼєднані одним delivery.

## 1. USB protocol unification

Два джерела USB-інсталяції (`yati::source::Usb` — Awoo/TinFoil + GoldLeaf і
`yati::source::DbiUsb` — DBI backend) обʼєднані в один `yati::source::Usb`,
який автоматично визначає протокол на конекті (`WaitForConnection()`). Порядок
розпізнавання: DBI (16-байтовий probe) → Awoo/TinFoil (TUL0/TUC0 magic) →
GoldLeaf (GL01/GLC1 handshake). Кожна гонка «хост почав слати під час нашого
probe» покривається short-transfer check через `Base::TransferOnce()`.

Видалені файли (мертвий код):
- `sphaira/include/yati/source/usb_dbi.hpp` — обʼєднано в `usb.hpp`
- `sphaira/source/yati/source/usb_dbi.cpp` — обʼєднано в `usb.cpp`
- `sphaira/include/ui/menus/usb_menu.hpp` — меню `usb::Menu` нікому не було
  доступне (`app_display_options.cpp:45` пропускав не-shortcut рядки)
- `sphaira/source/ui/menus/usb_menu.cpp` — те саме

Вхідна точка: `dbi::Menu` (dbi_menu.cpp) тепер створює `yati::source::Usb`
замість `yati::source::DbiUsb`; усі три протоколи доступні через одне меню.
`MiscMenuEntry::IsInstall()` не мала жодного виклику і видалена.

## 2. Screensaver (Minus screen blank)

Під час роботи черги інсталяції кнопка Minus вимикає/зменшує яскравість панелі.
Три режими (Setting → Install → Screen off): «Lower brightness», «Turn off
backlight», «Screensaver». Screensaver малює поверх чорного фону дрейфуючу
зведену інформацію (годинник, прогрес, швидкість, ETA тощо) — набір полів
обирається користувачем. OLED mode: порожня частина прогрес-бару лишається чорною.

Нові файли:
- `sphaira/include/ui/screensaver.hpp` — `Screensaver` (Start/Stop/Draw/WantsWake),
  `SaverPreview`, `SaverInfo`, `SaverField` enum
- `sphaira/source/ui/screensaver.cpp` — реалізація

Інтеграція: `dbi_menu.cpp` тримає `Screensaver m_screensaver`; `Start()` зберігає
та знижує яскравість, `Stop()` відновлює; `Draw` перехоплює малювання, коли
screensaver активний; `WantsChrome()` та `OwnsFooter()` — load-bearing для
правильного хрому; деструктор і вихід зі стеку зупиняють saver.

## 3. Forwarder address space — глобальна настройка

Замість per-forwarder `SidebarEntryArray` у формі створення форвардера тепер
глобальна настройка `m_forwarder_address_space` в `app.hpp` з трьома значеннями:
Automatic (= 36-bit), 36-bit, 39-bit. Читається `owo.cpp:resolve_address_space()`
при генерації NCA форвардера. UI: Settings → Install → «Forwarder address space».
Параметр `nro_path` зарезервований під майбутній список nro, що потребують
ширшого простору.

Видалення з `filebrowser_forwarder.cpp`: per-forwarder `SidebarEntryArray`
«Address Space» та поле `m_address_space` у `ForwarderForm`.

## Верифікація

- [x] Збірка `cmake --build --preset ReleaseWithInstall` у WSL — `[100%] Built
  target sphaira_nro`, exit 0, нових warnings немає.
- [ ] HW-SMOKE-416: (a) install з dbibackend.py; (b) ns-usbloader TinFoil mode;
  (c) ns-usbloader GoldLeaf v0.10+ mode; (d) forced re-attach з кабелем від
  бута; (e) Minus у трьох blank modes під час multi-package queue.

# План: Вирівнювання карточки гри та налаштування хедера NAND/SD (v0.13.428)

1. **Хедер (3-й блок та NAND/SD storage bar)**:
   - Здвинуто 3-й блок вхедеру (годинник, індикатор батареї, IP-адресу) вправо на 20px (`bar_right = 1240.f`).
   - Збільшено 2-й блок (смуги пам'яті NAND та SD) на 20px від зсуву + додатково на 10% (`bar_w = 238.f`).

2. **Карточка гри (Game Details)**:
   - Вирівняно значення в межах 4 логічних блоків:
     - Блок 1: Title ID, Version
     - Блок 2: Languages, Mods folder
     - Блок 3: Play time, Last played
     - Блок 4: Components, Tickets, Saves, Save quota
   - Значення вирівнюються за позицією найдовшого лейблу у блоці.
   - Додано обмеження ширини лейблу 1/3 ширини рядка з автопрокручуванням довгих перекладів через `ScrollingText`.

## Верифікація

- [x] Код оновлено та впорядковано відповідно до правил вирівнювання.
- [x] Версію в `CMakeLists.txt` піднято до `0.13.428`.

# План: MTP-диск, виправлення хедера черги інсталяції та графік скрінсейвера (v0.13.429)

1. **Спільний код побудови NSP** — `NspEntry`, `ContentInfoEntry`,
   `BuildContentEntry`, `BuildNspEntries` винесені з `game_menu.cpp` у
   `title_nsp.hpp/cpp` (`sphaira::title`). Меню ігор тепер лише додає ім'я та
   іконку до готового списку; логіка тікетів (`es::PatchTicket`) і порядок
   колекцій (cnmt в кінці, StandardNSP) спільні для обох шляхів.
   `BuildNspPath` отримав фолбек на `[TitleID]`, якщо control не завантажився.

2. **Новий MTP-диск `Games (read-only)`** — `FsGameProxy` у `haze_helper.cpp`,
   поруч з `FsSaveProxy`:
   - корінь — тека на кожен встановлений тайтл (`Назва [TitleID]`), скан через
     `nsListApplicationRecord`; записи без встановленого контенту пропускаються;
   - у теці — по одному `.nsp` на компонент (BASE / UPD / DLC), розмір справжній;
   - NSP будується при відкритті теки (не під час скану) і кешується на 8 ігор;
     відкриті хендли тримають власний `shared_ptr`, тож скидання кешу не рве
     трансфер;
   - читання йде напряму з `ncmContentStorageReadContentIdFile`, для
     file-based emummc — той самий 2мс throttle, що й у дампі на карту;
   - усе, що модифікує (create/delete/rename/write), відхиляється.

3. **Налаштування** — `mtp_show_games` (за замовчуванням вимкнено) +
   пункт «Show Games (read-only)» у Settings → Network → MTP storages; зміна
   перезапускає MTP, як і решта тумблерів сховищ.

4. **Виправлення хедера черги встановлення**:
   - `storage_right` обмежено праворуч лівою межею блоку годинника/аплета (`start_x - 10.f`), усуваючи накладання довгого рядка об'єму (наприклад `+26.5 GB / 120.4 GB`) на значок `[A]` та годинник.
   - Адаптивний `value_col_w` розраховується за шаблоном `"+000000 WW / 000000 WW"` у режимі проекції `m_storage_projection`.

5. **Графік швидкості для скрінсейвера (Speed Graph)**:
   - Розширено enum `SaverField` (додано `SaverField_Graph`) та структуру `SaverInfo` (додано масиви історії читання/запису).
   - У `Screensaver::Draw()` додано відмальовку графіка R/W швидкості.
   - У Settings → Install → Show on screensaver додано опцію «Speed graph».

## Верифікація

- [x] Версію в `CMakeLists.txt` піднято до `0.13.429`.
- [x] Рядки додано в `en.json` (джерело для i18n-translate).
- [x] Виправлено позиціонування та ширину колонок пам'яті у хедері.
- [x] Оновлено скрінсейвер та його налаштування.
- [ ] HW-MTPGAMES-429: перевірка роботи MTP-диску та скрінсейвера на консолі.
