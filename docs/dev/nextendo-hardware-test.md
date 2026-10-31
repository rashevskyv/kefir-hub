# Nextendo: сценарій перевірки на консолі

Збірка: `kefir-hub.nro` з `master` (Prelude-Nextendo v4.0.18). Перед початком зробіть копію SD-картки (або хоча б папки `atmosphere`).

Позначки: **ОК** — очікуваний результат. Якщо результат інший, запишіть, що саме ви бачили.

## A. Режим emuMMC (головний сценарій)

1. Покладіть новий `kefir-hub.nro` на картку і запустіть Hub з emuMMC.
2. Відкрийте Kefir Settings → Nextendo.
   - **ОК:** є рядки статусу, «Nextendo on emuMMC» увімкнено, «Release» показує v4.0.18.
   - **ОК:** немає перемикача sysNAND (він є лише в Semi-stock).
3. Подивіться рядок «System firmware».
   - **ОК:** в описі є ssl build ID (40 hex-символів), а статус не каже, що патча немає.
   - Якщо ID не показано, запишіть це: Hub не отримав доступ до ldr:dmnt. Nextendo тоді лишається увімкненим.
4. Перевірте картку (через Hub → File browser, або з ПК).
   - **ОК:** `atmosphere/hosts/emummc.txt` існує, зверху є позначка Nextendo, є рядок `prod-api.sora-online.net`.
   - **ОК:** є папки `atmosphere/exefs_patches/nextendo_*` (зокрема `nextendo_disable_ca_verification` і `nextendo_terraria-nextendo`).
   - **ОК:** є `atmosphere/contents/0100000000000803/romfs/browser/RootCaEtc.pem`.
5. Якщо Hub попросив перезапуск, перезапустіть консоль в emuMMC. Повторіть кроки 2–4.
   - **ОК:** статус той самий, файли на місці, повторних запитів на перезапуск нема.
6. Запустіть Nextendo-гру з онлайн-режимом.
   - **ОК:** гра з'єднується з Nextendo Network.
7. Нічого не повинно йти на `*.nintendo.net`. Якщо маєте змогу, перевірте DNS-лог роутера під час кроку 6.

## B. Вимкнення і повернення

1. У Nextendo вимкніть «Nextendo on emuMMC» (утримайте **A**).
   - **ОК:** Hub видаляє `nextendo_*`, `emummc.txt` (якщо він наш) і вимагає перезапуск тільки якщо файли змінились.
2. Перевірте картку.
   - **ОК:** немає `nextendo_*` у `exefs_patches` і `nro_patches`, немає `contents/0100000000000803` від Nextendo. Ваші власні патчі не зачеплені.
3. Увімкніть назад. **ОК:** усе повертається, як у кроці A4.

## C. Оновлення Kefir (update.te)

1. Запустіть оновлення Kefir через kefir-updater (будь-яку збірку, що має новий `update.te`).
   - **ОК:** після оновлення папки `atmosphere/exefs_patches/nextendo_*` лишились, інші старі папки в `exefs_patches` видалені.
2. Запустіть Hub. **ОК:** Nextendo не просить перевстановлення без причини.

## D. Semi-stock

1. Увімкніть Semi-stock. Відкрийте Nextendo.
   - **ОК:** з'явився перемикач «Nextendo on sysNAND», він вимкнений за замовчуванням, є попередження про eShop.
2. З Semi-stock увімкненим, але перемикач sysNAND вимкнений:
   - **ОК:** немає `atmosphere/hosts/sysmmc.txt`, у `nro_patches` немає `nextendo_*` (їх Semi-stock перемістив у `config/semistock/nextendo/nro_patches`).
3. Вимкніть Semi-stock.
   - **ОК:** `nextendo_*` у `nro_patches` повернулись на місце, якщо Nextendo на emuMMC увімкнений.
4. Перемикач sysNAND (лише за потреби, і лише якщо готові до ризику): увімкніть, перезапустіть у sysNAND, перевірте онлайн-гру; вимкніть і перевірте, що `sysmmc.txt` зник.

## E. Безпека (найважливіше)

- Протягом усіх кроків офіційні сервіси Nintendo в sysNAND не мають змінюватись, якщо ви не вмикали sysNAND-перемикач.
- Якщо щось пішло не так, вимкніть Nextendo (крок B) і надішліть мені опис.

## Що надіслати назад

Для кожного пункту: ОК / не ОК + одне речення. Окремо: ssl build ID з кроку A3 і чи була гра в мережі (A6).
