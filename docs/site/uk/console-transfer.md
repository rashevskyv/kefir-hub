# [[Console Transfer]]

Перенесення профілів, наіграних годин, резервних копій збережень, знімків екрана, homebrew та інших файлів з однієї
консолі на іншу через домашню мережу. Консоль-відправник відкриває доступ до файлів, а консоль-отримувач (або
браузер на ПК чи телефоні) їх завантажує.

**Де:** на консолі-відправнику [[Tools]] → [[Console Transfer]].
Для профілів і годин консоль-отримувач використовує [[Tools]] → [[Tools]] → [[Users]].

<!-- shot: console-transfer-menu | Console Transfer screen with all seven share items -->

## Що можна перенести {#what-you-can-move}
| Що | Консоль-відправник | Консоль-отримувач |
|---|---|---|
| Усі профілі та їхні години | [[Share Profiles & Play Hours]] | [[Users]] → [[Restore from another console]] або [[Receive from another console]] |
| Резервні копії збережень | [[Share Save Backups]] | Завантажте файли, потім відновіть їх у [[Saves]] |
| Знімки екрана й відео | [[Share Screenshots & Videos]] | Завантажте в браузері |
| Homebrew-програми (`/switch`) | [[Share switch Folder]] | Завантажте в браузері |
| Уся карта пам'яті | [[Share Entire microSD]] | Завантажте в браузері |
| Одна вибрана папка | [[Choose Folder...]] | Завантажте в браузері |

Що **не** переноситься:

- Встановлені ігри. [[Console Transfer]] передає файли, а не встановлені тайтли. Встановіть ігри на новій консолі
  зі своїх файлів, див. [Встановлення ігор](install/index.md).
- Ігрові збереження. Копіювати їх разом із профілями було б надто довго. Зробіть їхні копії в [[Saves]] і
  перенесіть копії через [[Share Save Backups]], див. [Перенести копії збережень](#move-save-backups).
- [[Share User Backups]] відкриває доступ до копій профілів у `/config/kefir/account_backups/`. Відновити їх на
  консолі-отримувачі поки що не можна.

## Перед початком {#before-you-start}
- Обидві консолі в одній локальній мережі (та сама Wi-Fi, не гостьова мережа).
- На обох консолях Kefir з Kefir Hub.
- Для відновлення профілів і годин також потрібні hekate і TegraExplorer. Kefir Hub сам копіює свій
  `TegraExplorer.bin` у `/bootloader/payloads/`, коли це потрібно.
- Перед відновленням профілів зробіть резервну копію SYSTEM у hekate на консолі-отримувачі.

## Відкрийте доступ на консолі-відправнику {#start-sharing-on-the-sending-console}
1. Відкрийте [[Tools]] → [[Console Transfer]].
2. Виберіть, що передати, і натисніть **A**. Якщо консоль не в мережі, Kefir Hub спершу запропонує підключитися.
3. Відкриється екран сервера з адресою (наприклад, `http://192.168.1.20`) і QR-кодом.
   Запишіть IP-адресу: її треба ввести на консолі-отримувачі.
4. Не закривайте цей екран, доки отримувач не закінчить. Натисніть **B**, щоб припинити доступ.

Якщо папки, яку треба передати, не існує (наприклад, ще немає копій збережень), Kefir Hub покаже
«Failed to start folder server». Якщо сервер уже працює, Kefir Hub перемкне його на нові папки й коротко покаже адресу.

Сервер зупиняється сам, коли консоль втрачає мережу або змінюється її IP-адреса.

<!-- shot: console-transfer-server-screen | Server screen titled Console Transfer: address, QR code, "Press B to Stop Server" -->

## Перенести профілі та години {#move-profiles-and-play-hours}
Копіюються всі профілі консолі-відправника з тими самими ID користувачів і, за бажанням, наіграні години.
Ігрові збереження не копіюються: перенесіть їх окремо, див. [Перенести копії збережень](#move-save-backups).
Оскільки ID лишаються ті самі, копії збережень зі старої консолі відновлюються до тих самих профілів на новій.

!!! warning
    Консоль-отримувач втрачає всі свої профілі: їх замінюють профілі з консолі-відправника.
    Якщо відновлювати години, її журнал гри теж замінюється. Спершу зробіть резервну копію SYSTEM у hekate.
    <!-- TODO(verify): what happens to game saves of profiles that existed only on the receiving console? If they become unreachable, tell the reader to back them up first. -->

На консолі-відправнику:

1. Відкрийте [[Users]], натисніть **A**, виберіть [[Backup profiles & play hours]], підтвердьте кнопкою [[Backup]].
   Якщо консоль перезапуститься в TegraExplorer, дочекайтеся завершення, потім запустіть Kefir і знову відкрийте
   Kefir Hub. Див. [Резервна копія](users.md#back-up).
2. Відкрийте [[Tools]] → [[Console Transfer]] → [[Share Profiles & Play Hours]].
   Можна також скористатися пунктом [[Send to another console]] у [[Manage Backups]].

На консолі-отримувачі:

3. Відкрийте [[Users]], натисніть **A**, виберіть [[Restore from another console]].
   Щоб лише завантажити копію й відновити пізніше, виберіть натомість [[Receive from another console]].
4. Введіть IP-адресу консолі-відправника. На клавіатурі вже буде перша частина адреси вашої мережі.
   Можна дописати порт (`192.168.1.20:8080`); без нього Kefir Hub сам перебере звичні порти.
5. Kefir Hub перевірить з'єднання й покаже список копій на консолі-відправнику. Виберіть потрібну.
   Якщо у [[Receive from another console]] копій кілька, перший рядок завантажує їх усі.
6. Копія зберігається в `/config/kefir/nand_transfer/`. Після [[Restore from another console]] Kefir Hub запитає,
   що відновити: [[Profiles only]] чи [[Profiles + play hours]].
7. Натисніть [[Launch TegraExplorer]]. Консоль перезапуститься в TegraExplorer, запише копію й повернеться в hekate.
   Не чіпайте консоль, поки триває запис.
8. Запустіть Kefir і відкрийте Kefir Hub. Він покаже, чи завершилося відновлення.

<!-- shot: console-transfer-ip-entry | Keyboard "Enter sending console IP address" with the network prefix filled in -->
<!-- shot: console-transfer-remote-list | List of backups on the sending console, first line receives all -->
<!-- shot: console-transfer-te-confirm | "Ready to restore profiles & play hours through TegraExplorer" dialog with Launch TegraExplorer -->

!!! tip
    Без мережі: скопіюйте файл `.kefir-nand.zip` з `/config/kefir/nand_transfer/` на карті пам'яті консолі-відправника
    в ту саму папку на карті отримувача, а потім скористайтеся [[Restore profiles & play hours]] в [[Users]].

## Перенести копії збережень {#move-save-backups}
1. На консолі-відправнику зробіть копії потрібних збережень у [[Saves]]. Див. [Збереження](saves.md).
2. Відкрийте [[Tools]] → [[Console Transfer]] → [[Share Save Backups]]. Kefir Hub відкриє доступ до папок із копіями
   збережень: `/dumps`, папок збережень DBI та додаткових папок копій, які ви додали в [[Saves]].
3. Перенесіть файли на карту пам'яті консолі-отримувача одним зі способів:
    - відкрийте адресу в браузері на ПК чи телефоні, завантажте копії, а потім передайте їх на консоль-отримувач
      (див. [Спільний доступ](sharing.md));
    - на консолі-отримувачі відкрийте [[File Browser]] → [[Sources]] → [[Add network location]], виберіть HTTP та
      адресу консолі-відправника, а потім скопіюйте копії в ту саму папку (наприклад, `/dumps`).
      <!-- TODO(verify): confirm that an HTTP network location pointing at another Kefir Hub opens and copies correctly. -->
4. На консолі-отримувачі відновіть збереження в [[Saves]]. Див. [Збереження](saves.md#restore-a-save).

## Перенести знімки екрана, homebrew та інші файли {#move-screenshots-homebrew-and-other-files}
1. На консолі-відправнику відкрийте [[Tools]] → [[Console Transfer]] і виберіть [[Share Screenshots & Videos]],
   [[Share switch Folder]], [[Share Entire microSD]] або [[Choose Folder...]].
   Для [[Choose Folder...]] зайдіть у потрібну папку й виберіть [[Select current folder]].
2. Відкрийте показану адресу в браузері на ПК чи телефоні й завантажте потрібне. Див. [Спільний доступ](sharing.md).

На emuMMC пункт [[Share Screenshots & Videos]] передає альбом того emuMMC, з якого завантажено консоль.

## Скасувати відновлення профілів {#undo-a-profiles-restore}
Перед записом Kefir Hub і TegraExplorer зберігають на карті пам'яті копію профілів і годин консолі-отримувача.
Якщо після відновлення консоль не завантажується:

1. Увійдіть у hekate, відкрийте **Payloads** → **TegraExplorer**.
2. Запустіть скрипт `Undo_restore_if_wont_boot.te`. TegraExplorer керується кнопкою живлення та кнопками гучності.
   Скрипт повертає старі профілі.
3. Якщо копії для скасування немає або консоль усе одно не завантажується, відновіть резервну копію SYSTEM у hekate.

## Проблеми {#problems}
**«Could not connect to the remote console».** Перевірте, що обидві консолі в одній мережі і що на консолі-відправнику
досі відкритий екран сервера. Звірте IP-адресу з тією, що на цьому екрані.

**«No profiles & play hours backups found on the sending console».** Консоль-відправник передає щось інше або ще не має
копії. Зробіть там копію й запустіть [[Share Profiles & Play Hours]].

**«Failed to start folder server».** Папки, які треба передати, ще не існують на консолі-відправнику.

**«Could not start TegraExplorer».** Скопіюйте `TegraExplorer.bin` у `/bootloader/payloads/` і спробуйте знову.

**«TegraExplorer did not finish restoring profiles & play hours».** Відновлення не завершилося. Якщо консоль
завантажується, запустіть відновлення ще раз. Якщо ні — див. [Скасувати відновлення профілів](#undo-a-profiles-restore).

**«TegraExplorer did not finish the dump».** Дамп не завершився. Виберіть [[Retry]], щоб спробувати знову в TegraExplorer,
[[Cancel]], щоб закрити нагадування і зберегти стан на потім, або [[Don't remind again]], щоб скасувати операцію й очистити
тимчасові файли та скрипти.

**Сервер зупиняється під час передачі.** Консоль-відправник втратила мережу або отримала нову IP-адресу.
Підключіться знову, ще раз відкрийте доступ і використайте нову адресу.
