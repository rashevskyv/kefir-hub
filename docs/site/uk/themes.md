# [[Themes]]

Завантажуйте системні теми для головного меню, створюйте власну тему з картинки й змінюйте вигляд самого Kefir Hub.

**Де:** [[Tools]] → [[Themes]]

<!-- shot: themes-list | Themes menu: Themezer, Mario BG Dark, Switch 2 Theme by alexwak, one starred favorite -->

У меню [[Themes]] є:

- [[Themezer]] — перегляд і завантаження наборів тем із themezer.net.
- Два готові набори: **Mario BG Dark** і **Switch 2 Theme by alexwak**.
- Ваші улюблені набори з Themezer, якщо ви позначили їх зірочкою.

## Як установлюються системні теми

Kefir Hub завантажує файли тем (`.nxtheme`). Застосовує їх до консолі застосунок NXThemes Installer.

- Якщо NXThemes Installer на карті пам'яті немає, Kefir Hub запитає «[[NXthemes_Installer.nro not found, download now?]]».
  Виберіть [[Download]]; застосунок збережеться в `/switch/Switch_themes_Installer/NXThemesInstaller.nro`.
- Після завантаження теми Kefir Hub запитає «[[Theme downloaded, install now?]]». Виберіть [[Install]], щоб відкрити
  NXThemes Installer із завантаженими файлами, і завершіть установлення там.

## Установіть готовий набір

1. Відкрийте [[Tools]] → [[Themes]].
2. Виберіть **Mario BG Dark** або **Switch 2 Theme by alexwak** і натисніть **A**.
3. Підтвердьте «[[Download theme?]]» кнопкою [[Download]].
4. Набір завантажиться й розпакується в `/themes/` на карті пам'яті.
5. Коли з'явиться запит, виберіть [[Install]] і застосуйте тему в NXThemes Installer.

## Завантажте тему з Themezer

1. Відкрийте [[Tools]] → [[Themes]] → [[Themezer]].
2. Перегляньте набори. На кожній плитці — попередній перегляд, назва й автор.
3. Натисніть **Y** ([[Screenshot]]), щоб переглянути знімки екрана вибраного набору.
4. Натисніть **A** ([[Download]]) і підтвердьте «[[Download theme?]]».
5. Усі теми набору зберігаються в `/themes/sphaira/` на карті пам'яті, у теці з назвою набору та ім'ям автора.
6. Коли з'явиться запит, виберіть [[Install]] і застосуйте тему в NXThemes Installer.

<!-- shot: themes-themezer-grid | Themezer grid with preview tiles, page counter in the subheading -->

| Кнопка | Дія |
|---|---|
| **A** | [[Download]] — завантажити вибраний набір |
| **Y** | [[Screenshot]] |
| **R** / **L** | [[Next Page]] / [[Previous Page]] |
| **ZR** / **ZL** | На 10 сторінок уперед / назад |
| **R3** | [[Star]] / [[Unstar]] — додати набір в улюблені або прибрати з них |
| **+** | [[Options]] |
| **B** | [[Back]] |

### Параметри Themezer

Натисніть **+** ([[Themezer Options]]).

| Параметр | Що робить |
|---|---|
| [[Sort]] | [[Rising]], [[Trending]], [[Created]], [[Updated]], [[Downloads]] або [[Saves]]. |
| [[Order]] | [[Descending]] або [[Ascending]]. |
| [[Target]] | Показувати теми лише для одного екрана: [[All]], [[Home Menu]], [[Lock Screen]], [[All Apps]], [[Settings]], [[Player Select]], [[User Page]], [[News]]. |
| [[Tags]] | Фільтр за тегами, наприклад `anime, dark`. Розділяйте теги пробілами або комами. |
| [[Page]] | Перейти до сторінки за номером. |
| [[Search]] | Пошук за назвою чи ключовим словом. |
| [[Launch NXthemes_Installer.nro]] | Відкрити NXThemes Installer. Видно, лише коли його встановлено. |

### Улюблені

Натисніть **R3** ([[Star]]) на наборі в Themezer. Після цього він з'явиться в меню [[Themes]], і його можна буде завантажити знову без пошуку.
Щоб прибрати його, виберіть набір у меню [[Themes]] і натисніть **R3** ([[Unstar]]).

## Створіть тему з картинки

Створіть тему головного меню з будь-якого зображення на карті пам'яті.

1. Відкрийте [Файловий менеджер](file-browser.md) і виберіть зображення.
2. Натисніть **+** і виберіть [[Create Switch Theme]]. (У переглядачі зображень цей пункт теж є в меню **+**.)
3. Скадруйте картинку. Зображення теми має розмір 1280×720.
    - **D-pad** або стік пересуває зображення; утримуйте **ZL** і натискайте вгору/вниз, щоб змінити масштаб.
    - **A** ([[Fit Image]]) повертає початковий вигляд.
    - **ZR** ([[Full Screen]]) показує повноекранний попередній перегляд.
4. Натисніть **L** ([[Target]]), щоб вибрати екран, для якого призначена тема. За замовчуванням — головне меню.
5. Натисніть **X** ([[Theme Name]]) і **Y** ([[Author]]), щоб задати назву й автора.
6. Натисніть **+** ([[Generate Theme]]). Тема збережеться в `/themes/` як `<назва>_<дата>.nxtheme`.
7. Якщо NXThemes Installer установлено, відкриється екран [[Theme Created Successfully!]]:
    - утримуйте **A** 3 секунди, щоб установити тему;
    - утримуйте **Y** 3 секунди, щоб установити її й перезавантажити консоль;
    - натисніть **B**, щоб повернутися до редактора.

<!-- shot: themes-creator | Theme creator: image framed on screen, top line with Theme / Author / Target -->

## Видаліть тему

У Kefir Hub немає кнопки для видалення встановленої системної теми. Скористайтеся для цього NXThemes Installer.
<!-- TODO(verify): name of the NXThemes Installer option that restores the default theme -->

Будь-яке встановлення прошивки через [Оновлення](updater.md) автоматично видаляє власні теми, бо теми для
іншої прошивки можуть завадити консолі завантажитися (помилка Atmosphère 2162-0002). Після оновлення встановіть тему знову.

## Змініть вигляд Kefir Hub

Це змінює лише Kefir Hub, а не головне меню консолі.

**Де:** [[Settings]] → [[Appearance]]

| Параметр | Що робить | За замовчуванням |
|---|---|---|
| [[Theme]] | Колірна тема Kefir Hub. Вбудовані: Abyss, Black, Black alt-icons-SP, Default, OLED Black, White. | Default |
| [[Animated waves]] | Анімовані хвилі в нижній панелі. | Увімкнено |
| [[Kefir Hub theme options]] | [[Select Theme]] (той самий список, що й [[Theme]]) і [[12 Hour Time]] для годинника (за замовчуванням [[Off]]). | — |

Власні теми Kefir Hub (файли `.ini`) у теці `/config/kefir/themes/` з'являються в тому самому списку.
Див. також [Налаштування](settings.md).

## Проблеми

**«Failed to download theme».** Перевірте з'єднання з інтернетом і спробуйте знову.

**Щоразу з'являється «[[NXthemes_Installer.nro not found, download now?]]».** Kefir Hub шукає NXThemes Installer у `/switch/NXThemesInstaller.nro`, `/switch/NXThemesInstaller/NXThemesInstaller.nro` і `/switch/Switch_themes_Installer/NXThemesInstaller.nro`. Дозвольте Kefir Hub завантажити його або перемістіть файл в один із цих шляхів.

**Після оновлення прошивки консоль показує помилку 2162-0002.** Досі встановлено тему чи переклад для старої прошивки. Див. [Оновлення → Проблеми](updater.md#problems).
