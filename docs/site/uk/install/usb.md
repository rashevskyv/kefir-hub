# Встановлення з ПК через USB

Надсилайте ігри з ПК на консоль через USB-кабель. Kefir Hub працює з програмами для ПК DBI Backend (зокрема DBI Backend Qt), ns-usbloader (режим Awoo/Tinfoil або GoldLeaf) і Fluffy. Яка програма на тому боці, він визначає сам.

**Де:** [[Tools]] → натисніть **+** ([[Install & Share]]) → [[PC Install (USB)]]

Той самий пункт є в меню **+** на вкладці домашніх програм.

Спершу увімкніть встановлення (див. [Увімкніть встановлення](index.md#enable)).

<!-- shot: install-usb-sidebar | Install & Share menu with PC Install (USB) highlighted -->

## Що потрібно {#requirements}

- USB-кабель між консоллю та ПК.
- Одна з програм, названих вище. Потоковий режим (stream mode) у ній має бути вимкнений: консоль перевіряє кожен пакет перед встановленням, а в потоковому режимі програма не може їх так передати.
- У Windows — USB-драйвер для консолі. Власного драйвера для неї Windows не має, тож програма на ПК не зможе відкрити консоль, доки його не встановлено.
    - DBI Backend Qt 2.9.0 і новіші встановлюють драйвер WinUSB самі. Коли програма вперше бачить консоль без драйвера, вона пропонує його встановити, а Windows один раз запитує дозвіл адміністратора. Також встановлення можна запустити з меню **Help → Install USB Driver** у DBI Backend Qt.
    - Для ns-usbloader і Fluffy встановіть драйвер так, як сказано в їхніх інструкціях (libusbK через Zadig).
    - Драйвер, уже встановлений через Zadig, і далі працює; DBI Backend Qt його не змінює.
- У Linux DBI Backend Qt пропонує додати правило udev (запитає ваш пароль), якщо ваш користувач не може відкрити консоль. У macOS нічого робити не треба.

## Встановіть ігри {#install}

1. Відкрийте [[PC Install (USB)]]. Консоль покаже [[Waiting for PC]] і значок зі швидкістю USB.

<!-- draft
- v0.14.038: when the cable was already in and MTP is running, a PC install app started afterwards cannot reach the console over USB (MTP and the install link are different USB devices); if the app writes a file named `kefir-hub.pc-install` to any MTP storage (or you copy a file with that name to the memory card in Explorer), Kefir Hub asks "A PC install app is waiting. Stop MTP and open PC Install (USB)?": [[PC Install (USB)]] stops MTP and looks for the app for a few seconds as on plug-in, [[No]] keeps MTP; the file is removed from the card
-->
2. Під'єднайте кабель і запустіть програму на ПК.
3. У програмі на ПК додайте файли й почніть передавання.
4. Консоль прочитає список і перевірить кожен пакет. Поки що нічого не встановлюється.
5. Відкриється черга. Перевірте сховища й розміри, виберіть потрібне й натисніть **A** ([[Install selected]]).
6. Дочекайтеся [[Session summary]].

Черга, екран ходу встановлення, згортання, вимкнення екрана та підсумок працюють так само, як для файлів на карті пам'яті. Див. [Перегляньте чергу](sd-card.md#queue) і наступні розділи.

<!-- shot: install-usb-waiting | PC Install (USB) waiting screen with the USB 2.0 badge -->
<!-- shot: install-usb-queue | Queue with packages received from DBI Backend Qt -->

## Автоматичне відкриття при під'єднанні {#auto-open}

<!-- draft
- new in v0.14.023, setting [[PC Install on connect]] (on by default) in Settings → Network
- handheld console plugged into a computer by cable: after 2 seconds Kefir Hub opens the USB install link and waits a few seconds for a PC app that is already running (DBI Backend, ns-usbloader, Goldleaf); if one answers, [[PC Install (USB)]] opens by itself with the file list already loaded; if nobody answers, MTP starts as before
- the PC app must be started before or right after plugging in; Kefir Hub cannot start the app on the computer
- turn the setting off to always get MTP on connect
-->

## Змінюйте чергу з ПК {#live-queue}

<!-- draft
- new in v0.14.026, DBI Backend Qt with the queue plan command: the sync now runs both ways. A tick removed or a target changed on the console (**X**, **Y**, **L3**) is sent to the PC within a moment and stays; it no longer comes back on the next poll
- the PC app shows for every [[Auto]] package where the console will really put it ("Auto → SD" / "Auto → NAND"), from the console's plan
- the PC app's NAND and microSD bars are drawn like the console's: before the install the selected packages are projected into the free space (red when they do not fit), the row under the mouse in amber at the head; while installing, the remaining bytes of the active package
- **+** → [[Sort]] on the console keeps its order while the PC keeps updating the queue; new keys [[Target]] (microSD first) and [[Status]] (ticked, unticked, already installed, failed analysis)
-->


З DBI Backend Qt черга синхронізується з програмою на ПК:

- Вибір пакетів, їхній порядок і сховище [[Auto]], [[microSD]] чи [[System memory]], задані на ПК, з'являються на консолі.
- Це працює й під час встановлення — для пакетів, які ще не почалися.
- Консоль повідомляє програмі на ПК, скільки в неї вільного місця.

<!-- TODO(verify): which DBI Backend Qt version supports the live queue, and where can users download it? -->

## Значок швидкості USB {#usb-speed}

Екран очікування показує режим USB:

- USB 3.0 — якщо USB 3.0 увімкнено в Atmosphère (`usb30_force_enabled` у `/atmosphere/config/system_settings.ini`) або з'єднання працює на швидкості USB 3.0.
- USB 2.0 — в інших випадках.

Коли USB 3.0 увімкнено, значок також показує, якщо кабель чи порт дають лише з'єднання USB 2.0.

## Що змінюється під час роботи {#while-running}

- На час встановлення через USB MTP вимикається; з'являється [[Disable MTP for usb install]]. Коли ви закриваєте екран, MTP вмикається знову ([[Re-enabled MTP]]).
- USB-накопичувачі, під'єднані до консолі, на час сеансу відключаються, а потім підключаються знову.

## Проблеми {#problems}

**Консоль не виходить з екрана очікування.** Перевірте кабель, потім почніть передавання в програмі на ПК. Консоль шукає ПК, доки ви не натиснете **B** ([[Cancel session]]).

**Програма на ПК не бачить консоль у Windows.** Бракує USB-драйвера. У DBI Backend Qt виберіть **Help → Install USB Driver** і дозвольте запит адміністратора. Якщо ви відхилили запит або не маєте прав адміністратора, попросіть адміністратора цього ПК запустити встановлення.

**Програма на ПК у потоковому режимі.** Консоль показує [[USB session failed]] і просить вимкнути потоковий режим. Вимкніть його в програмі на ПК і знову відкрийте [[PC Install (USB)]].

**Кабель від'єднався під час встановлення.** У журналі видно, що з'єднання втрачено; консоль пробує під'єднатися знову й повторити пакет. Якщо не вдається, сеанс завершується, а решта пакетів пропускається. Перевірте кабель і порт, тоді почніть знову.

**На екрані очікування попередження про режим аплету.** У режимі аплету пам'яті мало, і пакети NSZ навряд чи встановляться. Запустіть Kefir Hub у режимі тайтлу (див. [Підтримувані файли](index.md#formats)).
