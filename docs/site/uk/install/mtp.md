# Встановлення через MTP

Під'єднайте консоль до ПК USB-кабелем, і вона з'явиться у файловому менеджері ПК, як телефон. Скопіюйте файл гри на диск для встановлення, і консоль встановить його просто під час копіювання. Сам файл на консолі не зберігається.

**Де:** на ПК — диск `Games` → [[Install to SD]] або [[Install to NAND]]

<!-- draft (v0.14.345, uk)
- The drive `Install (NSP, XCI, NSZ, XCZ)` is gone, with its options "Show Install folder" and "Install folder name".
- To install over MTP, open the `Games` drive. At its root are two folders, in every [[Dump format]]: [[Install to SD]] and [[Install to NAND]].
- Copy an NSP, NSZ, XCI or XCZ file into [[Install to SD]]: it installs to the microSD card. Into [[Install to NAND]]: to the system memory. The folder decides, [[Install location]] is not used.
- The file is not stored: the folder stays empty after the copy. Other file types are refused.
- The `Games` drive must be on ([[Show Games]], on by default).
- A game file copied to the root of the `microSD card` drive is still installed, and there [[Install location]] decides.
- Readme.txt on the `Games` drive names both folders: [[Copy an NSP, NSZ, XCI or XCZ file here to install it to the microSD card.]], [[Copy an NSP, NSZ, XCI or XCZ file here to install it to the system memory.]], [[Delete a game here to remove it from the console. Its saves stay.]]
-->

Встановлення через MTP не показує попередження про бан, також на sysMMC. Див. [Попередження про бан на sysMMC](index.md#ban-warning).

## Запустіть MTP {#start}

MTP запускається сам, коли ви під'єднуєте консоль до ПК у портативному режимі (не в доці) і не підключено жодного USB-накопичувача. Приблизно за дві секунди з'явиться [[Computer connected — MTP started]].

Щоб запустити вручну:

- [[Tools]] → натисніть **+** ([[Install & Share]]) → [[Mount MTP]]. Пункт зміниться на [[MTP: Active]]; виберіть його ще раз, щоб зупинити MTP.
- Або увімкніть [[Tools]] → [[Settings]] → [[Network]] → [[MTP]], щоб MTP працював постійно.

MTP і USB-накопичувачі використовують той самий USB-порт. Коли вмикається MTP, USB-накопичувачі вимикаються ([[USB storage turned off to free the USB port]]).

Коли ПК відкриває з'єднання, з'являється [[MTP connected]].

<!-- shot: install-mtp-explorer | Windows Explorer showing the console with the microSD card and Install (NSP, XCI, NSZ, XCZ) drives -->

## Встановіть гру {#install}

1. На ПК відкрийте консоль у файловому менеджері.
2. Відкрийте диск `Games`, а в ньому теку [[Install to SD]] або [[Install to NAND]].
3. Скопіюйте в неї один або кілька файлів NSP, NSZ, XCI чи XCZ.
4. Консоль відкриє екран [[MTP Install]] і встановлюватиме файл у міру надходження.
5. Кілька файлів встановлюються один за одним в одному сеансі.
6. Коли файл готовий, з'являється [[MTP install success!]]. Приблизно за три секунди після останнього файлу, коли більше нічого не надходить, відкривається [[Session summary]]. Натисніть **B** ([[Back]]), щоб закрити його.

Якщо скопіювати файл гри на диск карти пам'яті, він просто збережеться на карті. Встановити його можна пізніше з файлового менеджера (див. [Встановлення з карти пам'яті](sd-card.md)).

<!-- shot: install-mtp-progress | MTP Install screen during a copy: Mode MTP, Written, Average speed, log -->

## Що показує хід встановлення {#progress}

ПК не повідомляє консолі розмір файлу, тож консоль не може показати відсоток для всього копіювання. У верхньому рядку видно [[Mode]] MTP, скільки файлів [[Installed]], скільки [[Written]] і [[Average speed]]. [[Remaining]] з'являється, лише коли розмір відомий. Наскільки просунулося копіювання, дивіться у вікні копіювання на ПК.

Натисніть **R3** ([[Minimize]]), щоб сховати екран під час встановлення, і **−** ([[Screen off]]), щоб вимкнути екран, як у [черзі встановлення](sd-card.md#minimize).

У Windows вікно копіювання закривається приблизно тоді, коли завершується встановлення.

## Скасуйте {#cancel}

**На консолі:** натисніть **X** ([[Cancel installation]]) і підтвердьте [[Cancel installation?]]. Консоль перестане приймати файл.

**На ПК:** скасуйте копіювання у файловому менеджері. Консоль покаже [[Install cancelled: the source stopped sending data]] і перезапустить MTP ([[Restarting MTP service...]]). Консоль ненадовго зникне з ПК і з'явиться знову.

В обох випадках завершені файли лишаються встановленими, а файл, що встановлювався, видаляється.

Якщо скасувати на консолі, Windows не покаже помилки: вікно копіювання просто закриється.

## Що працює інакше через MTP {#differences}

- [[Skip if already installed]] не використовується: уже встановлена гра встановлюється знову.
- [[Convert to standard crypto]] і [[Re-encrypt to master key 0]] не застосовуються.
- Куди встановити гру, вирішує тека; [[Install location]] не використовується (див. [Куди встановлюються ігри](index.md#location)).
- Файл домашньої програми `.nro`, скопійований на диск для встановлення, не встановлюється як гра. Він зберігається у `/switch/<назва>/<назва>.nro` і з'являється на вкладці домашніх програм.

## Сховища MTP {#storages}

**Де:** [[Tools]] → [[Settings]] → [[Network]] → [[MTP storages]]

| Параметр | Що робить | Типово |
|---|---|---|
| [[Show microSD card]] | Показує диск карти пам'яті. | Увімк. |
| [[microSD card name]] | Власна назва диска карти пам'яті. | `microSD card` |

Інші диски з цієї сторінки (збереження, ігри) описано в розділі [Спільний доступ](../sharing.md). Якщо вимкнути всі диски, MTP не запуститься, і з'явиться [[No MTP storages enabled]].

## Проблеми {#problems}

**Консоль не з'являється на ПК.** MTP запускається сам лише в портативному режимі й лише тоді, коли не підключено USB-накопичувач. Запустіть його через [[Mount MTP]]. Також MTP вимкнений, поки відкрито [встановлення з ПК через USB](usb.md).

**Копіювання одразу переривається.** Консоль показує [[Install failed: another installation is in progress.]]: триває інше встановлення або передавання файлів. Дочекайтеся, поки воно завершиться.

**Файли NSZ не встановлюються.** У режимі аплету пам'яті мало; коли ПК під'єднується, консоль про це попереджає. Запустіть Kefir Hub у режимі тайтлу (див. [Підтримувані файли](index.md#formats)).

Під час копіювання багатьох файлів через MTP показується [[Copying via MTP]] з однією смугою для поточного файла, а під нею рядок [[Copied so far:]] з кількістю завершених [[files]], їхнім розміром і середньою швидкістю всього копіювання. Лічильник починається знову після завершення копіювання. ПК не повідомляє загальний обсяг, тому смуги для всієї партії немає.
