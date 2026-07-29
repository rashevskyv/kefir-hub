# Результати: аудит і переписування MTP Host (v0.13.364 — v0.13.365)

## 1. `sphaira/source/utils/devoptab_mtp.cpp` — переписано

Файл поділено на чотири шари з коментарями про інваріанти: транспорт
(post/wait на endpoint), протокол (`Transact` — command/data/response як одна
операція), сесія (scan, connect, health) і `MtpMountDevice`.

Виправлені дефекти:

| # | Дефект | Наслідок |
|---|--------|----------|
| 1 | endpoint відкривався з `maxXferSize = 512`, а пости були 64 KiB | `2140-0301`, порожні лістинги — регресія v0.13.357 |
| 2 | збій трансферу не завершував сесію | назавжди «порожній» телефон замість помилки |
| 3 | не перевірявся `transaction_id` відповіді | сміття замість даних при розсинхроні |
| 4 | `GetObject` fallback читав увесь обʼєкт у `std::vector` | падіння на 1.4 GiB nsp |
| 5 | короткі читання | помилка парсингу nca/nsp в yati |
| 6 | 32-бітний офсет у `GetPartialObject` | тихо неправильні дані для файлів > 4 GiB |
| 7 | `continue` після вдалого OpenSession | витік interface + двох endpoint |
| 8 | `g_mtp_mutex` тримався поверх `MountNetworkDevice2` | інверсія блокувань → deadlock з metadata-потоком |
| 9 | демонтування при збої probe | use-after-free `Device` при відкритому `DIR`/`FILE` |
| 10 | `log_write` на кожен трансфер | запис на SD під глобальним мʼютексом у гарячому шляху |
| 11 | сирі офсети при розборі датасетів | падіння/сміття на нестандартній відповіді |
| 12 | кеш за parent handle, резолв шляху його не бачив | повторний вичит кореня на кожен перехід |

Плюс: сурогатні пари UTF-16 (емодзі в іменах), `SEEK_END`, вибір саме bulk
endpoint (а не interrupt), фільтр usb:hs по класу інтерфейсу 06/01/01, дешевий
health-probe замість `GetDeviceInfo` на кожен лістинг.

## 2. `sphaira/source/ui/menus/filebrowser.cpp` + `location.cpp`

`MountConfig.no_stat_file` / `no_stat_dir` доходили до `FsEntry`, але їх ніхто
не читав. Тепер `QueueRemoteMetadata` пропускає підрахунок вмісту папок для
маунтів, які цього не підтримують. Розміри файлів лишились — на MTP `lstat`
віддається з кешу лістингу і не коштує USB-запиту.

## 3. `sphaira/source/fs.cpp`

`DirGetEntryCount` і `Dir::Read` отримали fallback на `stat()` для
`DT_UNKNOWN`, а `Dir::ReadAll` — ні, хоча саме він використовується для
головного лістингу у файловому менеджері. Усі три тепер користуються спільним
`ClassifyStdioEntry`, який заодно повертає розмір файлу.

## Збірка

`cmake --build --preset ReleaseWithInstall` у WSL — успішно, без warnings.
Перевірка на залізі — за `tests.md`, поки не проведена.

# Результати: стабілізація за логами з консолі (v0.13.366 — v0.13.371)

Кожен раунд — реальний тест на консолі, лог з `/config/kefir/log.txt` +
`errors.txt`, діагноз, фікс. Хронологія:

## v0.13.367 — краш при закритті файлового браузера

Рерайт devoptab_mtp загубив єдиний виклик `FixDkpBug()` (закладання
`dotab_stdnull` у NULL-дірки `devoptab_list` після RemoveDevice). Наступний
прохід ітерації по таблиці ловив data abort. Виклик тепер сидить в обох
Umount-функціях `devoptab_common.cpp` — покриває всіх викликачів.

## v0.13.368 — 0x1DF9 на ~27 MB встановлення

Реконект посеред читання працював, але retry повторно використовував старий
MTP object handle — а handle валідний лише в межах сесії. `MtpFileHandle`
тепер несе `generation` + `path`; `RefreshFileLocked()` після реконекту
перерезолвлює handle за шляхом.

## v0.13.369 — м'ютекс ringbuf лишався захопленим

Early-return-и у wait-циклах `Set/GetDecompressBuf`, `Set/GetWriteBuf`
виходили без `mutexUnlock`. `ON_SCOPE_EXIT(mutexUnlock)` реєструється одразу
після lock.

## v0.13.370 — STALL це не смерть лінка

Лог: `transfer failed: 0x25A8C` → `no MTP interface on the bus`. 0x25A8C —
endpoint STALL, штатний спосіб телефона перервати трансфер. Код закривав
інтерфейс і перезахоплював — саме це збивало телефон з шини. Тепер
`RecoverLinkLocked`: MTP Cancel Request (0x64) на control pipe +
GET_STATUS/CLEAR_FEATURE(ENDPOINT_HALT) на обох bulk endpoint — так само
робить libusbhsfs. Заодно: `StartStreamLocked` перевіряє `connected` (0xE401 —
пост у щойно закритий endpoint), кап стріму 32 MiB.

## v0.13.371 — дедлок yati при збої читання + settle після Cancel

Лог нарешті показав зависання по B: після збою read-потоку write-потік
виходить, а `decompress thread returned now` не друкується ніколи. Причина:
escape-умова wait-циклу `SetWriteBuf` перевіряла `decompress_running` —
власний прапорець потоку-викликача, який завжди true — замість
`write_running` (споживача, що вже вийшов). `SetDecompressBuf` мав дзеркальну
підміну. Плюс жоден wait-цикл не перевіряв `GetResults()` зсередини, тож
збій/скасування не міг розбудити запаркований потік з помилкою. Оркестратор
крутився в `IsAnyRunning()` вічно → B висить, force-close падає.

MTP: після `recovering link` наступна ж команда падала 0/28 байтів — телефон
після Cancel відповідає `Device_Busy` (0x2019) на Get Device Status (0x67),
поки не дочистить скасування, і до того ігнорує bulk. `AwaitDeviceIdleLocked()`
опитує статус (до 2 с, крок 20 мс) перед зняттям halt-ів.

Окремо: обидва зафейлені рани обірвалися рівно на 20 MiB усередині однієї
data phase (раз із безлімітним запитом, раз із 32 MiB). Кап знижено до
16 MiB — фаза, що завершується сама, не потребує cancel-рукостискання.
