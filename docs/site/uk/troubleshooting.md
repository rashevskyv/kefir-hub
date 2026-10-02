# Усунення проблем

Що робити, коли щось пішло не так, і як зібрати все потрібне для звіту про помилку.

## Як отримати журнал {#get-the-log}

Журнал записує, що робив Kefir Hub. Типово він вимкнений, бо запис сповільнює програму.

1. Відкрийте [[Tools]] → [[Settings]] → [[General]] і увімкніть [[Logging]].
2. Повторіть дію, яка не вдається.
3. Скопіюйте `/config/kefir/log.txt` на комп'ютер **до наступного запуску Kefir Hub**.
4. Вимкніть [[Logging]].

!!! warning
    `log.txt` очищується під час кожного запуску Kefir Hub, а також коли ви вимикаєте й знову вмикаєте [[Logging]].
    Спершу скопіюйте файл, потім перезапускайте.

Як скопіювати файл, поки Kefir Hub відкритий: через [[MTP]] USB-кабелем або через [[FTP]]
(див. [Обмін файлами з ПК](sharing.md)). Або закрийте Kefir Hub і прочитайте карту пам'яті на комп'ютері: вихід файл не стирає, його очищує лише наступний запуск.

Поки [[Logging]] увімкнено, під час кожного запуску з'являється [[Warning! Logs are enabled, Kefir Hub will run slowly!]]. Так і має бути.

<!-- shot: troubleshooting-logging | Settings, General category, Logging option set to On -->

## Список помилок (errors.txt) {#the-error-list-errorstxt}

`/config/kefir/errors.txt` записується навіть тоді, коли [[Logging]] вимкнено. Він зберігає записи з попередніх запусків,
кожен із датою і часом, і починається заново, коли виростає понад приблизно 256 КБ.

Сюди потрапляють пакунки з черги встановлення, які не вдалося встановити. Коли черга завершується з помилками,
підсумок показує, скільки помилок записано; натисніть **Y**, щоб переглянути їх на консолі. Див. [Встановлення](install/index.md).

## Звіти про збої {#crash-reports}

**Уся консоль зупиняється на екрані помилки Atmosphère.** Екран називає файл, який він зберіг:
`/atmosphere/fatal_errors/report_<номер>.bin`. Сфотографуйте екран і надішліть цей файл.

**Kefir Hub закривається, консоль показує помилку, але далі працює.** Atmosphère записує звіт у
`/atmosphere/crash_reports/` (файл `.log`, у назві — позначка часу). Надішліть найновіший.

<!-- TODO(verify): does Atmosphère write a crash_reports entry for every Kefir Hub crash in both Applet Mode (Album) and as a HOME Menu title? -->

## Kefir Hub ніби завис під час передачі {#kefir-hub-looks-frozen-during-a-transfer}

**Під час встановлення чи копіювання смуга прогресу стоїть, а екран майже не оновлюється.**
На час передачі [[Boost CPU during transfer]] підвищує частоту процесора і знижує частоту графіки, тому
екран може оновлюватися лише кілька разів на секунду. Передача триває і завершується; кнопка HOME працює.

- Дочекайтеся кінця передачі.
- Якщо потрібен плавний екран, вимкніть [[Tools]] → [[Settings]] → [[Install]] → [[Boost CPU during transfer]].
  Встановлення стиснених пакунків (NSZ, XCZ) може стати повільнішим.

## Поширені проблеми {#common-problems}

**Kefir Hub загалом працює повільно.** Перевірте, що [[Logging]] вимкнено.

*Встановлення не відбувається, або Kefir Hub питає [[Installing is disabled, enable now?]]* Для системи, яку ви
запустили, встановлення вимкнено. Увімкніть [[Enable sysMMC]] або [[Enable emuMMC]] у [[Settings]] → [[Install]].
Спершу прочитайте попередження про бан. Див. [Встановлення](install/index.md).

*[[Applet Mode has limited memory. NSZ packages are unlikely to install. Use Title Mode for reliable installation.]]*
Kefir Hub запущено з Альбому, і йому бракує пам'яті. Запускайте його з іконки на HOME Menu. Щоб створити
іконку, відкрийте [[Tools]], натисніть **+** ([[Install & Share]]) і виберіть [[Install Title Mode forwarder]]. Див. [Початок роботи](getting-started.md).

*[[Install failed: another installation is in progress.]]* Одночасно працює лише одне встановлення. Дочекайтеся, поки поточне завершиться.

*[[There is not enough free space on the selected storage.]]* Звільніть місце або виберіть інше
значення [[Install location]] у [[Settings]] → [[Install]].

**Консоль не бачить USB-диск, або MTP зникає, коли ви вставляєте диск.** MTP і USB-диски ділять один
USB-порт консолі, тому одночасно працює лише щось одне. Kefir Hub повідомляє [[MTP turned off to free the USB port]] або
[[USB storage turned off to free the USB port]]. Увімкніть [[MTP]] у [[Settings]] → [[Network]] або [[USB storage]]
у [[Settings]] → [[Sources]].

**Копіювання папки на карту пам'яті через MTP нічого не робить.** Відома проблема. Відкрийте папку на комп'ютері і скопіюйте файли з неї. Див. [Встановлення через MTP](install/mtp.md).

*Мережеве розташування не відкривається: [[Connection test failed!]], [[Failed to connect to network storage!]] або
[[The server is reachable but the listing failed. Check the credentials and the shared folder path.]]*
Перевірте адресу сервера, назву спільної папки, ім'я користувача і пароль: [[Settings]] → [[Sources]] → розташування →
**+** ([[Options]]) → [[Edit]]. Консоль і сервер мають бути в одній мережі. Див. [Файловий менеджер](file-browser.md).

*Примітки до випуску в [[About]] не завантажуються.* Вони беруться з GitHub і потребують інтернету. Натисніть **X** ([[Refresh notes]]), щоб спробувати ще раз.

**Альбом відкриває Kefir Hub замість Homebrew Menu.** Увімкнено [[Replace hbmenu on exit]]. Вимкніть його в
[[Settings]] → [[Homebrew]] і на запитання [[Restore hbmenu?]] виберіть [[Restore]]. Якщо Kefir Hub не знаходить
`/switch/hbmenu.nro`, спершу встановіть Homebrew Menu заново.

*[[Web listener started, but its local self-test failed; check the log or use Title Mode]]* Запустіть Kefir Hub
з іконки на HOME Menu або зберіть журнал. Див. [Обмін файлами з ПК](sharing.md).

**Після оновлення прошивки консоль показує помилку 2162-0002.** Її можуть спричинити власні теми і системні переклади,
зроблені для старої прошивки. Оновлення прошивки їх видаляє; якщо воно попередило, що не змогло, видаліть їх вручну. Див. [Оновлення](updater.md).

**Чити: Kefir Hub повідомляє, що `prod.keys` не знайдено.** Зніміть ключі консолі за допомогою Lockpick_RCM. Див. [Чити](cheats.md).

## Розділи «Проблеми» на інших сторінках {#problems-sections-on-other-pages}

- [Встановлення](install/index.md#problems)
- [Встановлення через USB](install/usb.md#problems), [через MTP](install/mtp.md#problems), [через мережу](install/network.md#problems)
- [Збереження](saves.md#problems)
- [Обмін файлами з ПК](sharing.md#problems)
- [Оновлення](updater.md#problems)
- [Налаштування](settings.md#problems)

## Як повідомити про помилку {#report-a-bug}

Повідомляйте про помилки на GitHub: <https://github.com/rashevskyv/kefir-hub/issues>

Додайте:

1. **Версію Kefir Hub.** Її видно в лівому верхньому куті екрана, над назвою екрана, а також у
   [[Settings]] → [[General]] → [[About]].
2. **Як ви запустили Kefir Hub:** з Альбому чи з іконки на HOME Menu. І що у вас працює — sysMMC чи emuMMC.
3. **Кроки:** що ви робили, чого очікували, що сталося натомість. Точний текст помилки або знімок екрана.
4. **Файли:** `log.txt` (див. [Як отримати журнал](#get-the-log)), `errors.txt` і звіт про збій, якщо він був.

<!-- shot: troubleshooting-version | Top-left corner of the main screen with the version number -->
