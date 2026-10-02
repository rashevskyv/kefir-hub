# [[Kefir Settings]]

Перемикачі Kefir та Atmosphère, що змінюють файли на карті пам'яті. Більшість із них перезавантажує консоль.

**Де:** [[Tools]] → [[Kefir Settings]]

<!-- shot: kefir-settings-list | Kefir Settings list: Overclock status, 40MB Memory, USB 3.0, 8GB DRAM status, Translate Interface, each with On/Off -->

## Змініть параметр

1. Виберіть параметр і натисніть **A**.
2. Вікно підтвердження пояснить, що станеться. Утримуйте **A**, доки смуга не заповниться (пів секунди; для [[8GB DRAM status]] — три секунди). Щоб скасувати, натисніть **B**.
3. Kefir Hub змінить файли й одразу перезавантажить консоль (крім [[USB 3.0]] — тут він запитає).

<!-- shot: kefir-settings-hold-confirm | Hold-to-confirm box for a Kefir setting, "Hold A to continue" with progress bar -->

Значення праворуч у кожному рядку зчитується з карти пам'яті щоразу, коли ви відкриваєте меню,
тож воно показує справжній стан, а не те, що ви вибрали востаннє.

## Параметри

| Параметр | Що робить | Показано «увімкнено», коли |
|---|---|---|
| [[Overclock status]] | Вмикає чи вимикає файли розгону Kefir. Вимкнення видаляє модуль sys-clk, його оверлей і kip-файли розгону, а конфіг sys-clk зберігає в `/config/oc_bkp`. Увімкнення копіює їх назад із `/config/oc`. Перезавантажує консоль. | існує `/atmosphere/kips/kefir.kip` |
| [[40MB Memory]] | Перемикає патч пам'яті аплетів на 40 МБ (`force_40mb_applet` у файлі Atmosphère `system_settings.ini`). Перезавантажує консоль. <!-- TODO(verify): what the 40MB applet patch is for, in user terms --> | патч установлено |
| [[USB 3.0]] | Примусово вмикає USB 3.0 в Atmosphère. Під час увімкнення з'являється попередження, що це може спричинити збої, нестабільність або проблеми з деякими USB-пристроями (утримуйте **A**, щоб підтвердити). Зміна діє лише після перезавантаження; Kefir Hub запитає [[Later]] чи [[Reboot]]. | параметр не вимкнено явно |
| [[Redirect Emunand saves to SD]] | Експериментальне. Зберігає збереження emuMMC на карті пам'яті. Видно, лише коли emuMMC увімкнено. Під час вимкнення також видаляє `/config/redirect.bin`. Перезавантажує консоль. | параметр увімкнено |
| [[8GB DRAM status]] | Лише для консолей із фізично впаяними 8 ГБ оперативної пам'яті. Перезавантажує консоль у TegraExplorer, щоб застосувати або прибрати конфігурацію 8 ГБ. | існує `/tegraexplorer/scripts/Remove_8GB-RAM_config.te` <!-- TODO(verify): this file looks like it ships with Kefir; does the status really reflect whether the 8GB config is active? --> |
| [[Translate Interface]] | Відкриває інструменти перекладу системи. Див. нижче. | — |

<!-- TODO(verify): Kefir's out-of-the-box value for each switch -->

!!! warning
    [[Redirect Emunand saves to SD]] змінює місце, звідки читаються збереження emuMMC. Доки ви не вимкнете параметр, збереження можуть здаватися зниклими.

!!! warning
    [[8GB DRAM status]] — лише для консолей із 8 ГБ пам'яті, впаяними на платі. Будь-яка інша консоль не завантажиться правильно.
    Щоб скасувати це, якщо консоль не завантажується: у hekate відкрийте **Payloads** → **TegraExplorer** і запустіть `Remove_8GB-RAM_config.te`.

## Перекладіть системний інтерфейс

[[Translate Interface]] замінює одну з власних мов консолі (головне меню, системні налаштування та інші
системні екрани) перекладом від спільноти. Мову Kefir Hub це не змінює.

1. Відкрийте [[Kefir Settings]] → [[Translate Interface]].
2. Виберіть [[Load translations]] і утримуйте **A**, щоб завантажити список для вашої прошивки. Згодом цей пункт називатиметься [[Refresh translations]].
3. Виберіть переклад і натисніть **A**.
4. Виберіть системну мову, яку буде замінено ([[Replace language]], або [[Select language to replace]], якщо жоден варіант не відповідає мові й регіону вашої консолі).
5. Якщо переклад зроблено для іншої прошивки, з'явиться попередження, що частина тексту може бути відсутня чи показуватися неправильно. Виберіть [[Continue]] або [[Cancel]].
6. Утримуйте **A**, щоб підтвердити. Переклад установиться, і консоль перезавантажиться.
7. Після перезавантаження, якщо потрібно, увімкніть замінену мову в системних налаштуваннях консолі.
   <!-- TODO(verify): does the user need to switch the system language manually after install? -->

<!-- shot: kefir-settings-translate | Translate Interface list: Refresh translations, Remove installed translation, one translation entry -->

Щоб повернути оригінальний текст, виберіть [[Remove installed translation]] і утримуйте **A**. Консоль перезавантажиться.

Якщо для вашої прошивки перекладу немає, у першому рядку буде ваша прошивка й «Unsupported»; доступний лише пункт
[[Remove installed translation]].

!!! tip
    Кожне встановлення прошивки через [Оновлення](updater.md) видаляє встановлені переклади. Після нього встановіть переклад знову.

## Проблеми

**«Failed to apply Kefir setting».** Не вдалося записати файл. Перевірте, чи карта пам'яті не заповнена й не захищена від запису, і спробуйте знову.

**Консоль не завантажується після ввімкнення [[8GB DRAM status]].** Запустіть `Remove_8GB-RAM_config.te` з hekate → **Payloads** → **TegraExplorer**.

**Зникли збереження на emuMMC.** Якщо ви вмикали [[Redirect Emunand saves to SD]], вимкніть цей параметр.
