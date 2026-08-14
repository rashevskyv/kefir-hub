# Аудит інтеграції NFS back-end

**Статус:** реалізовано у `v0.13.449`; software verification завершено, hardware smoke test на Switch очікується.
**Локальна база:** `7ac634860bfcf086531778683f4538e67bf6b719` (`v0.13.447`).
**Upstream-база для порівняння:** `eeac5ff8fcffaf57d88b91d05d704e6fb0c75dba` (`1.0.2`).
**Перевірений upstream `master`:** `f4efedf6d8065388a04d23d0b6f78f6217c957dc` (2026-08-14).

## 1. Призначення NFS у Sphaira

NFS має бути ще одним мережевим джерелом у File Browser: користувач додає
експорт, відкриває його як файлову систему, переглядає каталоги й копіює файли
між ним та SD/іншими вже підтриманими джерелами. Це **не** окрема система
завантажень, не заміна SMB/WebDAV/FTP і не ручна реалізація NFS-протоколу.

Перший інкремент повинен додати NFS до чинного маршруту
`location::Entry → File Browser → devoptab → fs::FsStdio`; усі наявні операції
File Browser тоді автоматично використовуватимуть той самий `fs::Fs` інтерфейс.

## 2. Стан нашої гілки

- У commit-базі `7ac6348` немає `devoptab_nfs.cpp`, `libnfs` або NFS-пункту в
  CMake. Після початку аудиту робоче дерево стало dirty сторонніми,
  **неперевіреними** змінами, зокрема в CMake/File Browser/i18n; цей документ
  описує базову архітектуру й не приймає та не перезаписує той diff.
- `location::Entry` вже зберігає `name`, `url`, облікові поля, `port` і
  `protocol` у `paths::LOCATIONS`; старі INI-записи без `protocol` досі
  розпізнаються за схемою URL.
- `FsView::ConnectToLocation()` є спільною точкою підключення. SMB має власний
  `CSMB2FS`; WebDAV, FTP і HTTP проходять спільним `MountCurlDevice`.
- Усі підключені мережеві devoptab-и володіються
  `devoptab::common::MountNetworkDevice2()`, отримують стабільний mount за
  хешем URL і звільняються `UmountAllNeworkDevices()` при закритті File Browser.
- File Browser працює через `fs::Fs`; для stdio/devoptab-джерел це `FsStdio`.
  Отже, читання, каталог, copy/paste та наявні error paths не потребують
  окремого NFS-фреймворку.

### Фактичний шлях джерела

```text
Settings / File Browser source picker
  → location::Add / location::Load  (locations.ini)
  → FsEntry { url, protocol, credentials, port }
  → FsView::ConnectToLocation()
  → [SMB: CSMB2FS] | [WebDAV/FTP/HTTP: MountCurlDevice]
  → devoptab mount
  → fs::FsStdio
  → FsView scan, open directory, File/Dir I/O, copy/transfer
  → Menu destructor → UmountAllNeworkDevices()
```

NFS має бути третьою гілкою в точці підключення, але використовувати наявне
володіння мережевим mount-ом і `FsStdio`, а не копіювати глобальний SMB singleton.

## 3. Upstream-реалізація NFS

Upstream використовує fork `https://github.com/ITotalJustice/libnfs` на
ревізії `65f3e11` через `FetchContent`. У CMake library має окремий
`NINTENDO_SWITCH` include path, тому upstream уже збирав саме Switch-варіант.
Залежність компілюється статично й лінкується як target `nfs`.

Upstream `source/utils/devoptab_nfs.cpp`:

1. створює один `nfs_context` на mounted device;
2. парсить URL через `nfs_parse_url_dir()`;
3. монтує `nfs_url->server` і **весь** `nfs_url->path`;
4. надає devoptab POSIX-операції: open/close/read/write/seek/stat, directory
   iteration, rename/unlink/mkdir/rmdir, truncate, statvfs, fsync та utimes;
5. руйнує контекст і unmount у деструкторі;
6. ділить read/write на negotiated `nfs_get_readmax()` / `nfs_get_writemax()`.

Це корисний перевірений back-end, але його старі `MountConfig`, INI та startup
`MountNfsAll()` не збігаються з нашою інтерактивною системою джерел. Переносити
треба адаптер і семантику життєвого циклу, а не upstream UI/config layer.

### Значення `e00ac7c`

Коміт `e00ac7c` замінив `nfs_parse_url_full()` на `nfs_parse_url_dir()`.
Перший вимагає й відокремлює останній компонент як **файл**; для URL джерела
`nfs://nas.example/export/media` це помилково залишало export `/export` і
трактувало `media` як filename. `nfs_parse_url_dir()` не відокремлює filename,
тому mount root коректно лишається `/export/media`. Це обов'язковий regression
test першого етапу.

## 4. Повна релевантна upstream-історія

| Commit | Дата | Назва / роль | Змінені релевантні файли | Потрібен | Перенесення |
|---|---|---|---|---|---|
| `5158e264` | 2025-09-03 | Перше додавання HTTP, NFS, SMB devoptab | `CMakeLists.txt`, `fs.hpp`, `app.cpp`, `location.cpp`, `devoptab_common.cpp`, `devoptab_nfs.cpp`, SMB/HTTP files | Так | Адаптувати NFS back-end і dependency; не переносити старі config/startup шляхи |
| `181ff3f2` | 2025-09-04 | Мережеві options: timeout, uid, port; збільшення path | `fs.hpp`, `devoptab_nfs.cpp`, common та інші back-end-и | Частково | Лише перевірити межі локальних fixed-size `FsEntry`; не додавати UID/GID/timeout UI без вимоги |
| `b99d1e5d` | 2025-09-07 | Рефактор network device, WebDAV, threaded I/O | common/WebDAV layer | Так, архітектурно | У нас уже є новіший `MountNetworkDevice2`; не переносити код |
| `384e8794` | 2025-09-08 | Custom mounts через helper | `devoptab_common.*` та back-end-и | Так, принцип | Наш `MountDevice` уже є спільною точкою; використати його |
| `931531e7` | 2025-09-09 | CMake options / lite build | `CMakeLists.txt` | Ні | У нас немає upstream option matrix; додати одну pinned dependency без нової системи опцій |
| `8b2e541b` | 2025-09-13 | Виправлення seek/timeout і NFS I/O узгодження | `devoptab_common.*`, `devoptab_nfs.cpp` | Частково | Зберегти chunked I/O й коректні негативні errno; наш common layer уже сучасніший |
| `63c420d5` | 2025-09-15 | Mount creator defaults | `devoptab.cpp`, common | Частково | Додати NFS у **наш** source editor/picker, не переносити старий creator |
| `3c504cc8` | 2025-09-21 | Mount wrapper / lifecycle | common, mounts, app, FTP/MTP | Так, архітектурно | Локальний `MountNetworkDevice2`/unmount already covers this; не копіювати |
| `e00ac7c9` | 2026-08-13 | Fix NFS export URL parsing | `devoptab_nfs.cpp` | Обов'язково | Перенести exactly `nfs_parse_url_dir()` behavior й test |

Після `e00ac7c` до перевіреного `f4efedf6` інших змін файлу NFS back-end не
знайдено. У базі `eeac5ff` NFS уже був присутній; проти сучасного upstream NFS
відрізняється лише parser fix. У нашій гілці back-end відсутній повністю, тому
це не cherry-pick одного `e00ac7c`, а адаптоване відновлення всього back-end-а.

## 5. Порівняння з SMB, FTP і WebDAV

| Аспект | SMB | FTP / WebDAV / HTTP | NFS-рішення |
|---|---|---|---|
| Конфігурація | `protocol=smb`, URL share, user/pass | URL, user/pass, port | `protocol=nfs`, URL export; не використовувати user/pass |
| Connect | окремий `CSMB2FS` singleton | curl probe + common devoptab | libnfs context + common devoptab |
| File API | custom devoptab | `MountCurlDevice` | copied/adapted libnfs devoptab |
| Mount name | fixed `smb2:/` | hashed per URL | hashed per URL, як curl mounts |
| Teardown | File Browser cleanup | `UmountAllNeworkDevices()` | той самий common cleanup |
| Auth | SMB credentials | protocol credentials | NFS identity/UID/GID, не login/password |

FTP у поточному продукті також означає локальний FTP **server** у Settings;
це не має бути змінено. NFS додається лише як клієнтське browse/copy source.

## 6. Залежність і ліцензування

- Точна upstream dependency: `ITotalJustice/libnfs@65f3e11`.
- Її CMake оголошує `project(libnfs VERSION 16.2.0)` і має Switch-specific
  `NINTENDO_SWITCH` include path.
- Власне client library та headers — **LGPL-2.1-or-later**; protocol `.x` та
  generated raw headers мають simplified BSD; examples — GPL-3.0-or-later.
- Sphaira вже GPL-3.0, тому статичне включення LGPL-2.1-or-later library в
  GPL-3.0 combined work ліцензійно сумісне. Перед merge все одно слід зберегти
  upstream copyright/license notices у dependency metadata та перевірити
  фактичний fetched `COPYING`.
- Не виявлено локальної залежності `libnfs`; вона буде новою, pinned
  FetchContent-залежністю. Немає підстав писати власний NFS client.

## 7. Формат конфігурації та сумісність

Використовуємо наявний `locations.ini`, без нової конфігураційної підсистеми:

```ini
[NAS NFS]
url=nfs://192.168.1.20/export/media
protocol=nfs
user=
pass=
port=0
```

Канонічний URL першої версії:

```text
nfs://<IPv4-or-hostname>/<export-path>
```

`<export-path>` — mount root, а не шлях до окремого файла чи додатковий
внутрішній subpath. URL можна percent-encode-ити за правилами libnfs. URL з
port (`nfs://host:port/export`) library вміє парсити, але окреме поле UI для
port не потрібне в першому інкременті.

Валідація до mount повинна відхилити: порожній host, порожній export, пробіли,
`.`/`..` components, query/fragment, username/password, URL без `nfs://` і
рядок, довший за місткість destination buffer. IPv6 не рекламується й не
вмикається в UI до окремого перевіреного тесту; поточний план покриває IPv4 та
hostname. Старі SMB/FTP/WebDAV/HTTP записи не змінюються.

## 8. Версії, transport та identity

libnfs README для pinned revision описує NFSv3 як default і можливість вибору
v4. Upstream back-end допускає `version=3|4`, але наша поточна конфігурація не
має безпечного, підтвердженого UX для такого option. Тому перша версія:

- підтримує **NFSv3 over TCP**, покладаючись на library default;
- використовує portmapper/rpcbind, якщо URL не задає port;
- не заявляє NFSv4, UDP, IPv6, TLS, Kerberos/RPCSEC_GSS або автоматичний
  reconnect як підтриману функцію продукту;
- не показує username/password як NFS auth fields і не передає локальні
  credentials до NFS;
- не додає настройку UID/GID. libnfs використовує свій default; root squashing
  та дозволи визначає NFS server/export.

Наступний етап може додати лише ті options (наприклад, `version=4`, explicit
UID/GID), які пройшли hardware validation на Switch і мають зрозумілу модель
збереження та локалізації.

## 9. Підтримувані операції першого інкременту

Ціль — **browse і copy reading**: mount, directory listing, stat, open, seek,
read і копіювання файлу з NFS на SD через наявний File Browser. Це вже достатнє
повноцінне джерело для browse/install workflows і не залежить від припущень про
серверні права на запис.

Upstream adapter також має write/mkdir/rename/delete/fsync. Їх не слід
advertise як підтримані до hardware test на write-enabled export. Прапорець
read-only у devoptab можна зберегти, щоб UI не пропонував руйнівні операції
під час раннього запуску.

## 10. Security і стабільність

- Парсити URL один раз перед mount, чітко відображати локалізовану помилку без
  показу секретів; NFS URL не повинен містити пароль.
- Межі: перевірити довжини URL/host/export, conversion до `FsPath`, усі integer
  offsets/sizes, `ssize_t`/`off_t` errors і partial read.
- Кожна library error має лишатися negative errno у devoptab; не замінювати її
  хибним success. Логи можуть містити NFS error text, але не credentials.
- `nfs_context`, URL parser result, file handles і dirs повинні бути звільнені
  на mount failure, close і File Browser teardown. Не кешувати raw pointers
  поза `MountNetworkDevice2` ownership.
- Сервер може зникнути в copy; кінцевий Result має пройти існуючим шляхом
  transfer/error UI, а mount можна прибрати тільки після закриття handles.
- Не normalise-ити remote path «на свій розсуд»: export дозволи — authority
  сервера. Локально відхиляються лише неоднозначні URL components до mount.
- Не створювати власні потоки, cache або retry loop у v1. libnfs already
  negotiates its transfer maximum; upstream chunked read is достатній.

## 11. Файли майбутньої зміни

| Файл | Зміна |
|---|---|
| `sphaira/CMakeLists.txt` | pinned `libnfs@65f3e11`, static/no-examples/no-tests options, FetchContent, include dirs, link target, NFS source |
| `sphaira/source/utils/devoptab_nfs.cpp` | новий адаптований libnfs `MountDevice`, тільки потрібні v1 read/browse operations та safe teardown |
| `sphaira/include/utils/nfs_url.hpp` | малий host-testable parser/validator NFS source URL; без protocol implementation |
| `sphaira/include/location.hpp` | `Entry::IsNfs()` і stricter NFS configured check |
| `sphaira/source/ui/menus/filebrowser.cpp` | protocol branch, NFS device creation via `MountNetworkDevice2`, dynamic root/name, source label |
| `sphaira/source/ui/menus/settings_menu.cpp` | NFS in protocol picker, NFS URL edit field, NFS connection probe/mount path |
| `tests/test_nfs_url.cpp` | host regression tests for URL validation and `e00ac7c` export semantics |
| `assets/romfs/i18n/en.json`, `assets/romfs/i18n/uk.json` | only the new visible protocol/field/error strings |

Не змінювати без окремого обґрунтування: SMB adapter, curl back-end, FTP server,
WebDAV save sync, `fs::Fs` API, custom NRO search paths і будь-які чужі зміни
робочого дерева.

## 12. План тестування

### Host tests

Додати один standalone `tests/test_nfs_url.cpp`, який `tests/run.sh` already
compile-ить without Switch/devkitPro. Мінімум:

- valid: `nfs://192.168.1.20/export`, nested export і hostname;
- reject: missing scheme/host/export, spaces, `.`/`..`, malformed port,
  query/fragment, credentials, overlong inputs;
- verify mount/export output for `nfs://host/export/media` is exactly
  `/export/media` — regression for `e00ac7c`;
- serialised `location::Entry` compatibility: old non-NFS records remain
  untouched (runtime UI verification is sufficient if minIni is not host-safe).

### Build verification

1. `wsl bash -l -c "cd /mnt/d/git/dev/sphaira && tests/run.sh"`
2. `wsl bash -l -c "cd /mnt/d/git/dev/sphaira && cmake --build --preset ReleaseWithInstall --parallel 16"`
3. `git diff --check`

### Hardware plan (не виконано)

1. Switch + reachable Linux/NAS NFSv3 TCP export, configured `insecure` if
   server requires privileged client ports.
2. Add `nfs://server/export`, test connection, browse root and a directory with
   many files.
3. Read/open a small file and copy it to SD; verify bytes/hash.
4. Copy a file larger than 4 GiB to SD if storage allows; observe progress and
   absence of truncation.
5. Unreachable host, invalid URL, inaccessible export, server loss mid-copy,
   disconnect/reconnect and reopen source.
6. Read-only export: ensure browse/read works and write actions are blocked.
7. Write-enabled export: test mutation only in a later explicitly enabled phase.

## 13. Невирішені питання, що справді потребують рішення

1. Чи потрібен NFSv4 у продукті після валідації v1, або NFSv3/TCP покриває
   наявні NAS? Без hardware evidence його не вмикати.
2. Чи є цільові сервери, що відхиляють non-privileged client ports? Якщо так,
   user-facing setup має прямо вимагати server-side `insecure` export; Switch
   не має тут задокументованого Linux capability workaround.
3. Чи потрібен writable NFS у File Browser? До відповіді v1 є read/browse/copy
   source і не обіцяє mutation.
4. Чи потрібні configurable UID/GID? Це впливає на server permissions і не
   повинно з'являтися як випадкові текстові поля.

## 14. Реалізація малими етапами

1. **Dependency + safe read-only NFS devoptab.** Add pinned library, URL
   validator, read/browse adapter, parser regression test, WSL build.
2. **Source configuration and UI.** NFS picker/editor, `locations.ini`
   round-trip, test connection, File Browser mount/status/localisation.
3. **Reliability.** Error mapping, cancellation/server-loss behavior,
   reconnect teardown, Switch hardware read/copy validation.
4. **Optional write support.** Only after a write-enabled export test; expose
   no extra operations beyond the existing File Browser policy.
5. **Optional v4/identity.** Only if Stage 3 reveals a real need and hardware
   tests prove exact options.

Етапи 1–2 можуть бути одним bounded Gemini task, якщо зміна не торкається
write-path. Stages 3–5 не слід змішувати з initial implementation.

## 15. Рекомендація

**Перенести:** pinned `libnfs@65f3e11`, NFS devoptab semantics, chunked
read/write mechanics (навіть якщо write не експонується), `nfs_parse_url_dir()`
fix, RAII teardown і devoptab error propagation.

**Адаптувати:** mount creation до `MountNetworkDevice2`, NFS до existing
`location::Entry`/File Browser picker, canonical `nfs://host/export` validation,
current i18n/error UI і tests/run.sh.

**Не переносити:** upstream legacy `mount.ini`, startup `MountNfsAll()`, його
generic mount creator, config extras без UX, SMB/FTP/WebDAV changes, global
singletons, custom retry/cache/thread framework, та будь-який custom NFS
protocol code.

Це найменший повноцінний шлях: один library client, один devoptab adapter і
одна source branch. Він переиспользує наявний File Browser, `fs::FsStdio`,
copy flow та cleanup, замість дублювання їх для NFS.

## 16. Джерела та межі тверджень

- Upstream Sphaira history and source were inspected locally through
  `f4efedf6`, including `5158e264`, `181ff3f2`, `8b2e541b`, `e00ac7c` and
  all commits returned by `git log --follow -- devoptab_nfs.cpp`.
- Pinned libnfs CMake states `VERSION 16.2.0` and an explicit Switch include
  branch; its `COPYING` states LGPL-2.1-or-later for library sources.
- libnfs README documents v3 default, selectable v4, URL grammar, portmapper
  behavior and NFSv4's lack of a MOUNT protocol.
- No Switch hardware test, successful local NFS build, NFSv4 test, IPv6 test,
  UDP test, writable export test or Kerberos test was performed in this audit.
