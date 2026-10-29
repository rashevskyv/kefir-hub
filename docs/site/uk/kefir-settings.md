# [[Kefir Settings]]

Перемикачі Kefir та Atmosphère, що змінюють файли на карті пам'яті. Більшість із них перезавантажує консоль.

**Де:** [[Tools]] → [[Kefir Settings]]

<!-- shot: kefir-settings-list | Kefir Settings list: Overclock status, 40MB Memory, USB 3.0, 8GB DRAM status, Translate Interface, each with On/Off -->

Перед запуском TegraExplorer Kefir Hub порівнює версію на карті пам’яті з вбудованою копією. Якщо файл відсутній, Hub встановлює свою копію. Якщо версія на карті старіша, Hub оновлює її. Однакову або новішу версію залишає. Якщо встановлення не вдалося, Hub не запускає payload.

## Змініть параметр

1. Виберіть параметр і натисніть **A**.
2. Вікно підтвердження пояснить, що станеться. Утримуйте **A**, доки смуга не заповниться (пів секунди; для [[8GB DRAM status]] — три секунди). Щоб скасувати, натисніть **B**.
3. Kefir Hub змінить файли й одразу перезавантажить консоль (крім [[USB 3.0]] — тут він запитає).

<!-- shot: kefir-settings-hold-confirm | Hold-to-confirm box for a Kefir setting, "Hold A to continue" with progress bar -->

Значення праворуч у кожному рядку зчитується з карти пам'яті щоразу, коли ви відкриваєте меню,
тож воно показує справжній стан, а не те, що ви вибрали востаннє.

## Параметри

| Параметр | Що робить | Показано «увімкнено», коли | Типово |
|---|---|---|---|
| [[Overclock status]] | Вмикає чи вимикає файли розгону Kefir. Вимкнення видаляє модуль sys-clk, його оверлей і kip-файли розгону, а конфіг sys-clk зберігає в `/config/oc_bkp`. Увімкнення копіює їх назад із `/config/oc`. Перезавантажує консоль. | існує `/atmosphere/kips/kefir.kip` | Вимк. |
| [[40MB Memory]] | Новіші версії Atmosphère лишають менше пам'яті для аплетів (homebrew, запущені з Альбому) і для модів LayeredFS. Цей патч повертає цю пам'ять (`force_40mb_applet` у файлі Atmosphère `system_settings.ini`). Цей параметр показується лише на прошивках нижче 23.0.0 або коли Kefir нижче 926 (чи якщо файл версії неможливо прочитати), оскільки в новіших версіях патч не потрібен. Деякі ігри з ним працюють неправильно: якщо гра поводиться дивно, вимкніть його. Перезавантажує консоль. | патч установлено | Вимк. |
| [[USB 3.0]] | Примусово вмикає USB 3.0 в Atmosphère. Під час увімкнення з'являється попередження, що це може спричинити збої, нестабільність або проблеми з деякими USB-пристроями (утримуйте **A**, щоб підтвердити). Зміна діє лише після перезавантаження; Kefir Hub запитає [[Later]] чи [[Reboot]]. | параметр явно ввімкнено | Вимк. |
| [[Redirect Emunand saves to SD]] | Експериментальне. Зберігає збереження emuMMC на карті пам'яті. Видно, лише коли emuMMC увімкнено. Під час вимкнення також видаляє `/config/redirect.bin`. Перезавантажує консоль. | параметр увімкнено | Вимк. |
| [[8GB DRAM status]] | Лише для консолей із фізично впаяними 8 ГБ оперативної пам'яті. Перезавантажує консоль у TegraExplorer, щоб застосувати або прибрати конфігурацію 8 ГБ. Якщо консоль завантажилася з увімкненим параметром, 8 ГБ на місці й працюють. | існує `/tegraexplorer/scripts/Remove_8GB-RAM_config.te`. Консоль без 8 ГБ з увімкненим параметром не завантажується, тож «увімкнено» буває лише на справжній консолі з 8 ГБ. | Вимк. |
| [[Translate Interface]] | Відкриває інструменти перекладу системи. Див. нижче. | — | — |

!!! warning
    [[Redirect Emunand saves to SD]] змінює місце, звідки читаються збереження emuMMC. Доки ви не вимкнете параметр, збереження можуть здаватися зниклими.

!!! warning
    [[8GB DRAM status]] — лише для консолей із 8 ГБ пам'яті, впаяними на платі. Будь-яка інша консоль не завантажиться правильно.
    Щоб скасувати це, якщо консоль не завантажується: у hekate відкрийте **Payloads** → **TegraExplorer** і запустіть скрипт `Remove_8GB-RAM_config.te`.

## Перекладіть системний інтерфейс

[[Translate Interface]] замінює одну з власних мов консолі (головне меню, системні налаштування та інші
системні екрани) перекладом від спільноти. Якщо в Kefir Hub є така сама мова, він запитує [[Switch Kefir Hub to this language too?]] (назва мови розміщена під цим питанням, а нижче — [[The console restarts after this.]]) із варіантами [[Keep]] або [[Switch language]]; після будь-якого вибору консоль перезавантажується. Якщо Kefir Hub уже використовує цю мову або не має відповідної, консоль перезавантажується одразу.

1. Відкрийте [[Kefir Settings]] → [[Translate Interface]].
2. Виберіть [[Load translations]] і утримуйте **A**, щоб завантажити список для вашої прошивки. Згодом цей пункт називатиметься [[Refresh translations]].
3. Виберіть переклад і натисніть **A**.
4. Виберіть системну мову, яку буде замінено ([[Replace language]], або [[Select language to replace]], якщо жоден варіант не відповідає мові й регіону вашої консолі).
5. Якщо переклад зроблено для іншої прошивки, з'явиться попередження, що частина тексту може бути відсутня чи показуватися неправильно. Виберіть [[Continue]] або [[Cancel]].
6. Утримуйте **A**, щоб підтвердити. Переклад установиться, і консоль перезавантажиться.
7. Після перезавантаження, якщо потрібно, увімкніть замінену мову в системних налаштуваннях консолі.
   <!-- TODO(verify): does the user need to switch the system language by hand after install? (Kefir Hub's own language switches automatically.) -->

<!-- shot: kefir-settings-translate | Translate Interface list: Refresh translations, Remove installed translation, one translation entry -->

Щоб повернути оригінальний текст, виберіть [[Remove installed translation]] і утримуйте **A**. Консоль перезавантажиться.

Якщо для вашої прошивки перекладу немає, у першому рядку буде ваша прошивка й «Unsupported»; доступний лише пункт
[[Remove installed translation]].

!!! tip
    Кожне встановлення прошивки через [Оновлення](updater.md) видаляє встановлені переклади. Після нього встановіть переклад знову.

## Nextendo

<!-- draft
- [[Nextendo]] is a folder in [[Kefir Settings]]; it opens a page with the status, the switches and the version rows
- Nextendo is the Nextendo Network: online play for supported games on the emuMMC, set up by Kefir Hub at start with no button press (hosts file, real PRODINFO inside the emuMMC)
- [[Nextendo on emuMMC]] is on by default; changing it asks for a hold of **A** and restarts the console only if a file really changed
- [[Nextendo on sysNAND (Semi-stock)]] is shown only while Semi-stock is on; it is off by default; the warning says the Nintendo services then go to Nextendo and the eShop may stop working
- leaving Semi-stock (back in the emuMMC) removes the sysNAND hosts file and switches that option off
- [[System firmware]] shows the installed version; the description shows the ssl build ID of the console; Nextendo works when this Kefir Hub carries the ssl patch for that ID; without it Nextendo is switched off until a Kefir Hub update brings the patch
- Kefir Hub carries the Nextendo patches itself (game patches, certificate patches, browser certificates) and writes them to the card at start: folders `nextendo_*` in `atmosphere/exefs_patches` and `atmosphere/nro_patches`, and `atmosphere/contents/0100000000000803`; when Nextendo is off they are deleted
- a Kefir update keeps the `nextendo_*` folders; Semi-stock moves them out of the way while it is on
- new Prelude-Nextendo releases reach the console as an ordinary Kefir Hub update (the Hub updates itself); no Kefir update is needed
- [[Server]] shows the server address used in the hosts file; [[Release]] shows the Prelude-Nextendo release the build is based on
- the text of the license notice: based on Prelude-Nextendo by Nextendo Network, PolyForm Shield License 1.0.0
-->

## Проблеми

**«Failed to apply Kefir setting».** Не вдалося записати файл. Перевірте, чи карта пам'яті не заповнена й не захищена від запису, і спробуйте знову.

**Консоль не завантажується після ввімкнення [[8GB DRAM status]].** Запустіть `Remove_8GB-RAM_config.te` з hekate → **Payloads** → **TegraExplorer**.

**Зникли збереження на emuMMC.** Якщо ви вмикали [[Redirect Emunand saves to SD]], вимкніть цей параметр.
