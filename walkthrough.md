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
