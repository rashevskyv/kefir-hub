# audit.md

Канонічний робочий файл. Версія коду: **v0.13.777**. Дата: 2026-09-08.
Ponytail-аудит усього дерева. Фікси цим файлом не застосовуються.

Карта коду: repo-root `graphify-out/` (див. `AGENTS.md`). Перед grep —
`graphify query` / `path` / `explain`. `graph.json` оновлено 2026-09-07:
12146 nodes, 22312 edges, 791 communities; HTML/REPORT цим incremental run не регенерувалися.

Далі працюємо тільки з чергою в §2.

v0.13.777 поза ponytail-чергою: кнопку Minus переведено на інтелектуальну навігацію з динамічною перевіркою контексту: закриття програми (`App::Exit()`), якщо кнопка натиснута безпосередньо на головному екрані Homebrew, та миттєве повернення на екран Homebrew (скидання віджетів/оверлеїв вище кореня та перемикання вкладки в `MainMenu`) при натисканні в будь-якому іншому меню або субменю програми; уніфіковано прив'язку `Button::SELECT` у `MenuBase`, `MainMenu`, `settings_fancurve`, `filebrowser`, `file_picker` та `sidebar`; оновлено `README.md`. Compile/tests/NRO не запускалися за policy. **Не закриває** чергу §2 A1–A7.

v0.13.776 поза ponytail-чергою: у `OfferPendingRestore` (`sphaira/source/ui/menus/users/users_restore.cpp`) для незавершеного відновлення профілів та годин гри (`wait_nand_restore`) стан pending переведено в `applied` через `SavePending`, що гарантує показ сповіщення рівно один раз без зациклення на наступних завантаженнях; прибрано зайвий інструктаж «If the console will not boot», оскільки консоль уже успішно завантажилася в Horizon; додано ключ перекладу до всіх 14 мовних файлів `assets/romfs/i18n/*.json`; оновлено `README.md`. Compile/tests/NRO не запускалися за policy. **Не закриває** чергу §2 A1–A7.

v0.13.775 поза ponytail-чергою: усунено дефект паразитної лінії перекриття під язичком векторної іконки папки в `StrokeFolder` (`sphaira/source/ui/file_icon.cpp`); арка язичка відокремлена від тіла папки, спирається на `top_body` без нижньої грані, а тіло папки формує завершений контур `nvgRoundedRectVarying` із повноцінним горизонтальним ребром на `top_body`; оновлено `README.md`. Compile/tests/NRO не запускалися за policy. **Не закриває** чергу §2 A1–A7.

v0.13.774 поза ponytail-чергою: у `sphaira/source/ui/menus/users/users_profile.cpp` (`RunSetAvatar`) після оновлення аватара користувача додано модальний запит `OptionBox` на перезавантаження консолі (інформування про набуття чинності після перезавантаження, вибір "Later" / "Reboot"); додано ключ до всіх 14 мовних файлів `assets/romfs/i18n/*.json`; оновлено `README.md`. Compile/tests/NRO не запускалися за policy. **Не закриває** чергу §2 A1–A7.

v0.13.773 поза ponytail-чергою: виправлено помилку компіляції в `sphaira/source/app.cpp` (звернення до полів `TouchInfo` `finger_down`, `x`, `y` замінено на `is_clicked && in_range(Vec4(bx, by, bw, bh))`); успішно зібрано цільовий NRO бінарник (`build/wsl_build.sh`) у WSL та пройдено всі хост-тести (`tests/run.sh`). **Не закриває** чергу §2 A1–A7.

v0.13.772 поза ponytail-чергою: пряме завантаження перекладів інтерфейсу з NX-Family/NX-Translation за точним тегом релізу без сканування списку; завантаження api.json для мовних метаданих з фолбеком на FW17.0.1-TR1.18 для FW16.1.0-TR1.09 та FW17.0.0-TR1.11; підтримка modern (NX-Translation_<id>.zip) та legacy (TR..._<id>_FW...zip) найменування архівів; коректне визначення кореневої папки розпакування ukrainian_FW... для legacy архівів; посилена перевірка запису кешу та безпечний парсинг JSON; юніт-тести policy розширено. Compile/tests/NRO не запускалися за policy. **Не закриває** чергу §2 A1–A7.

v0.13.771 поза ponytail-чергою: додано `TransportOrigin::Usb` та статус "USB Install", дію L3 ("Minimize"/"Expand") підключено до всіх станів очікування та ReviewQueue для USB-інсталяцій; у `DrawMiniBadge` додано позначку "USB", статуси очікування та гліф " Expand"; у `app.cpp` додано сенсорне розгортання по тапу на бейджі; тести та README оновлено. Compile/tests/NRO не запускалися за policy. **Не закриває** чергу §2 A1–A7.

v0.13.770 поза ponytail-чергою: для потокових інсталяцій (MTP, FTP, Web/HTTP) під час встановлення кнопку B повністю вимкнено у футері, а на кнопку X призначено "Cancel installation" із модальним запитом "Cancel installation?"; додано ключ "Cancel installation?" до всіх 14 i18n JSON; README оновлено. Compile/tests/NRO не запускалися за policy. **Не закриває** чергу §2 A1–A7.

v0.13.769 поза ponytail-чергою: усунено дефект векторної іконки папки в `StrokeFolder` (`sphaira/source/ui/file_icon.cpp`), де накладання прямокутників язичка та тіла утворювало паразитичну лінію перекриття; реалізовано єдиний суцільний векторний контур NanoVG без внутрішніх ліній; оновлено `README.md`. Compile/tests/NRO не запускалися за policy. **Не закриває** чергу §2 A1–A7.

v0.13.768 поза ponytail-чергою: виправлено пріоритет відмальовки (z-order) списків у `ConsoleTransferMenu` (`install_share.cpp`), `users_manage.cpp` та `users_restore.cpp`. Виділений елемент з рамкою `drawRectOutline` тепер малюється другим проходом поверх усіх неактивних елементів, завдяки чому фон сусіднього неактивного елемента більше не перекриває нижній край рамки виділення. **Не закриває** чергу §2 A1–A7.

v0.13.767 поза ponytail-чергою: розділено сповіщення про успішне встановлення за транспортом у `BackgroundInstaller::OnInstallStart` (`MTP/FTP/Web/generic install success!`); виправлено історичну згадку FTP у загальному ключі `Install success!` для `ru.json`, `es.json`, `ja.json`, `ko.json`, `zh.json`; додано ключі у всі 14 i18n JSON. Хост-тести `./tests/run.sh` та Switch NRO збірка `make build` у WSL успішно пройдені. **Не закриває** чергу §2 A1–A7.

v0.13.766 поза ponytail-чергою: усунено передчасне перемикання в Summary під час пакетної MTP-інсталяції; додано `haze::HasActiveTransfer()` з м'ютексом, захищено `current_file` у `haze_install_proxy.cpp`, введено 3-секундний grace period для MTP/FTP з динамічною кнопкою `B` ("Done"), миттєвий Summary на `CallbackType_CloseSession`, розширено тести `tests/test_transport_install_queue.cpp`. Хост-тести `./tests/run.sh` успішно пройдені у WSL, `git diff --check` без помилок. **Не закриває** чергу §2 A1–A7.

v0.13.765 поза ponytail-чергою: футер MTP інсталяції переведено на `X` для скасування та `B` для пропуску пакета; у `DrawStatRow` додано шрифтовий пробіл між двокрапкою мітки та значенням; ключ `"Cancel installation"` додано до 14 i18n JSON; README оновлено. Compile/tests/NRO не запускалися. **Не закриває** чергу §2 A1–A7.

v0.13.764 поза ponytail-чергою: USB mass-storage removal каскадно позначає matching File Browser і весь overlay subtree над ним для штатного top-down pop; premature `OnFocusGained()` до pop прибрано. Atmosphère crash-report, stack/caller cases і `git diff --check` перевірені; compile/tests/NRO не запускалися. **Не закриває** чергу §2 A1–A7.

v0.13.763 поза ponytail-чергою: Auto plan отримує fresh storage snapshot перед кожним входом у ReviewQueue і після зміни location/reserve; queue sidebar розділено на верхні Install Options та нижній View із sort/order. Planner/legend/install semantics без змін; compile/tests/NRO не запускалися. **Не закриває** чергу §2 A1–A7.

v0.13.762 поза ponytail-чергою: ReviewQueue завжди показує три projection values для обох носіїв, прибирає дубль planned totals зі stat-row та розрізняє Auto/pinned NAND/microSD чинними theme colours. Planner/install semantics без змін; compile/tests/NRO не запускалися. **Не закриває** чергу §2 A1–A7.

v0.13.761 поза ponytail-чергою: version-only correction `0.13.760` → `0.13.761`; продуктова поведінка не змінена, compile/tests/NRO не запускалися. **Не закриває** чергу §2 A1–A7.

v0.13.760 поза ponytail-чергою: ReviewQueue має session-local stable sorting за original order/name/package/install size; `source_index` зберігає local/external USB path після сортування; local і PC USB використовують спільний Auto planner/live refresh; projection header додає actual free NAND/microSD. Host assertions додані, але compile/tests/NRO не запускалися за policy. **Не закриває** чергу §2 A1–A7.

v0.13.759 поза ponytail-чергою: blocking install widget знову малює себе; detached transport session має chrome, чесні known/unknown totals, session log і безмодальний MTP cancel/disconnect teardown. **Не закриває** чергу §2 A1–A7.

v0.13.758 поза ponytail-чергою: background MTP batch серіалізує наступний Windows file на same-origin `InstallSession` через wait + atomic `s_installing` CAS; cross-origin/unrelated transfer guards, cancel/exit та nonblocking `OnInstallClose` збережені. **Не закриває** чергу §2 A1–A7.

v0.13.757 поза ponytail-чергою: DBI/MTP/FTP/Web встановлення повторно використовують один `InstallSession` metrics/summary/screensaver/minimize path; Web має validated upfront manifest, FTP/MTP — чесну growing queue, MTP Cancel — worker teardown перед `haze` restart. **Не закриває** чергу §2 A1–A7.

v0.13.756 поза ponytail-чергою: TE auto-dump profiles + PlayData перемальовує `copy/skip index/total` в одному 77-column рядку, лишає cursor для spinner виправленого `saveObj.read()` і після каталогу переходить нижче spinner-cell. RESULT/dumped.ok/known-tree/player.vend/cleanup/reboot без змін; paired TegraExplorer fix і hardware-check обов’язкові. **Не закриває** чергу §2 A1–A7.

v0.13.755 поза ponytail-чергою: власний auto/manual Kefir Hub HOME-форвардер має сталий `05C838DF22834000`; optional `OwoConfig::title_id` лишає generic NRO/ROM path+args hash без змін; `StaleOwn` більше не ставить другий ікон. **Не закриває** чергу §2 A1–A7.

v0.13.754 поза ponytail-чергою: hardware `v0.13.753` довів zero-identity existing BAAS як fatal preflight root cause до rollback. Global collision scan тепер пропускає zero placeholder (валідований incoming NAS завжди nonzero); read/short/nonzero collision лишаються fail-closed, а target-specific BAAS replacement без змін. **Не закриває** чергу §2 A1–A7.

v0.13.753 поза ponytail-чергою: persistent `[ACC_DIAG]` у `/config/kefir/errors.txt` переживає forced reboot і фіксує PM/BCAT/ACCOUNT/OLSC Result, RW-open `0010`, BAAS preflight, rollback mkdir та Commit без UID/NAS/donor/token даних. Link/rollback/reboot поведінка не змінена; один hardware run має підтвердити чи спростувати orphan/collision гіпотезу. **Не закриває** чергу §2 A1–A7.

v0.13.752 поза ponytail-чергою: Users sidebar прибрав individual `Backup user`/`Restore Backup`; `Manage Backups` у `CONSOLE MOVE` напряму повторно використовує `ConfirmNandRestore` → комплексну `/config/kefir/nand_transfer` бібліотеку всіх профілів + PlayData. Stale copy/i18n/docs оновлено; legacy/pending backend і дані не видалені; account/donor paths без змін. **Не закриває** чергу §2 A1–A7.

v0.13.751 поза ponytail-чергою: embedded Nintendo Account donor pool = legacy root + 8 byte-exact opaque packages; one unused donor per known-unlinked live profile; unknown/shortage fail до terminate; `ApplyLinkPackages` має duplicate UID/NAS guards і strict fail-closed BAAS collision preflight до rollback/мутацій; failure після daemon termination примусово reboot; raw donor identifiers прибрані з shared lookup logs. Safe validator/JSON/diff/caller checks пройдені, hardware HOS 18.1.0 очікується. **Не закриває** чергу §2 A1–A7.

v0.13.750 поза ponytail-чергою: DBI Installing header для активного NAND/microSD показує накопичувальне `written / total` з фіксованим total; окремий progress mode лишає жовтий bar як `total - written`, без double-count та без проєкції решти черги. ReviewQueue/Games modes без змін. **Не закриває** чергу §2 A1–A7.

v0.13.749 поза ponytail-чергою: account-link BAAS payload тепер opaque і переноситься byte-for-byte у live `ApplyLinkPackages` та TE staging; UID цілі задається filename, не байтами `0..15`; `FindLiveUidByNasId` більше не читає UID з body. Реальний firmware 18.1.0 bootloop відтворив і підтвердив дефект. **Не закриває** чергу §2 A1–A7.

v0.13.748 поза ponytail-чергою: Tools→Users→Delete — local BAAS unlink then DeleteUser; pre-delete backup **all** accounts without killing ACCOUNT; saves question; clearer ProgressBox/OK glyph. ZIP Manage Backups / nand pack / restore / TE **не** чіпали. **Не закриває** чергу §2 A1–A7.

v0.13.747 поза ponytail-чергою: Manage Backups nand packs — list `created_label` date; detail nicknames з `profiles.dat`; delete whole pack лише зі списку (detail = inspect + Restore). Per-account nand edit **не** робили. **Не закриває** чергу §2 A1–A7.

v0.13.746 поза ponytail-чергою: TE `nand_transfer_dump_auto.te` — видалено `waitFive` busy-loop (RESET BPMP / Err 18); після RESULT і failOut одразу `goHekate()`; skip `player.vend.dat`. Hub C++ / Manage Backups / restore не чіпали. **Не закриває** чергу §2 A1–A7.

v0.13.745 поза ponytail-чергою: `NandPackLibraryMenu` multi-select delete (X/Y/B/Minus як ZIP; A Open detail; ProgressBox multi-dir delete); detail без checkbox; multi-restore nand packs **не** робили. **Не закриває** чергу §2 A1–A7.

v0.13.744 поза ponytail-чергою: TE dump progress + 5s auto reboot; sticky Ultrahand toast вимкнено (`ArmReopenHubHint` не з LaunchTegraRomfs; Clear знімає boot hook); Manage Backups обидва види; nand pack library + restore_00F0 flag; per-account nand restore **не** робили. **Не закриває** чергу §2 A1–A7.

v0.13.743 поза ponytail-чергою: dump one-shot temps — TE `goHekate` чистить startup/dump_auto/dump_result; Hub `CleanDumpHandshake()` після confirmed `wait_nand_dump`. Pack / Undo / reopen_hub.flag / state.json не чіпає. **Не закриває** чергу §2 A1–A7.

v0.13.742 поза ponytail-чергою: TE `nand_transfer_dump_auto.te` RESULT — per-save stats + `color()` green OK / red NOT OK / yellow optional; `dumped.ok` лише 0010+00F0; known-tree dump без змін шляху. Hub C++ / restore / i18n не чіпали. **Не закриває** чергу §2 A1–A7.

v0.13.741 поза ponytail-чергою: TE `nand_transfer_dump_auto.te` — console-proven known-tree dump (clear/RESULT/pause/goHekate); skip missing optional dirs; `dumped.ok` лише при 0010+00F0; без recurse/combinepath/dump_result.txt. Hub C++ / restore / i18n не чіпали. **Не закриває** чергу §2 A1–A7.

v0.13.740 поза ponytail-чергою: TE `nand_transfer_dump_auto.te` combinepath crash на nested save files (`su/registry.dat`); dump/restore scripts пишуть nested шляхи конкатенацією; pack лишається `/config/kefir/nand_transfer/<stamp>/`. Hub C++ staging не чіпали. **Не закриває** чергу §2 A1–A7.

v0.13.739 поза ponytail-чергою: compile fix після haze split — `SUPPORTED_EXT`/`NRO_EXT` `inline constexpr` з bound; `FsSaveProxy` знову має `m_mounts`/`m_mount_tick`/`m_mount_mutex` і закритий `};` перед `MakeFsSaveProxy`. MTP логіку не рефакторили. **Не закриває** чергу §2 A1–A7.

v0.13.738 поза ponytail-чергою: compile fix після Games split — `game_internal.hpp` → `title_nsp.hpp`; `game_scan.cpp` → `yati/nx/ncm.hpp`; `game_details.cpp` → `save/save_paths.hpp`. Wrappers/поведінка без змін; **`gc_menu` не чіпали**. **Не закриває** чергу §2 A1–A7.

v0.13.737 поза ponytail-чергою: compile fix — slim `users_menu.cpp` включає `account/account_user.hpp` для `LoadImageJpeg` у `TryLoadAvatar` (після 731 split лишався лише `account_link.hpp`). Публічний header без змін. **Не закриває** чергу §2 A1–A7.

v0.13.736 поза ponytail-чергою: architecture slice 5 — live USB/DBI queue (`dbi_menu.cpp` → `dbi/*`), MTP haze (`haze_helper.cpp` → `haze/*`), File Viewer (`file_viewer.cpp` → `file_viewer/*`). Публічні headers без змін; **`gc_menu` не чіпали**. **Не закриває** чергу §2 A1–A7.

v0.13.735 поза ponytail-чергою: architecture slice 4 — live Games split (`game_menu.cpp` → `game/*` TUs + slim Menu). Публічний `game_menu.hpp` / `game_list_info.hpp` без змін; `AppendGameCardEntries` перенесено зі scan; **`gc_menu` не чіпали**. **Не закриває** чергу §2 A1–A7. Наступний live (optional): `dbi_menu.cpp` USB UI, `haze_helper`, `file_viewer`.

v0.13.734 поза ponytail-чергою: architecture slice 3 — live Settings split (`settings_menu.cpp` → `settings/*` TUs + slim Menu). Публічний `settings_menu.hpp` і шість окремих submenu типів без unify. Gray zone не чіпали. **Не закриває** чергу §2 A1–A7. Наступний live slice: `game_menu.cpp` (не `gc_menu`).

v0.13.733 поза ponytail-чергою: USB/DBI Auto install балансує usable free (`PlanPickSd` + `ChooseInstallTarget` + live `RefreshAutoInstallTarget`); під Installing жовтий bar = remaining поточного пакета (без проєкції всієї черги). Не закриває чергу §2.

v0.13.732 поза ponytail-чергою: architecture slice 2 — live File Browser split (`filebrowser.cpp` → `filebrowser/*` TUs + slim Menu). Живі Advanced options і MTP/FTP share поточної теки збережено/перенесено. Gray zone (Tools Coming soon, `gc_menu`, dead MTP install) не чіпали. **Не закриває** чергу §2 A1–A7.

v0.13.731 поза ponytail-чергою: architecture slice 1 — Users god-file split + `account/` domain folders (`users_menu` → `users/*`, account/nand sources під `include|source/account/`). Без зміни поведінки. **Не закриває** чергу §2 A1–A7.

v0.13.730 поза ponytail-чергою: Backup profiles & play hours при lock 0010/00F0 → TE `nand_transfer_dump_auto.te` (не ручний dump.te); `wait_nand_dump` + dumped.ok; Ultrahand `[on-boot]` one-shot notify (overlay не запускає NRO/форвардер). Не закриває чергу §2.

v0.13.729 поза ponytail-чергою: Restore profiles & play hours → TE `nand_transfer_restore_auto.te` (не Horizon Import); `wait_nand_restore` + `nand_restored.ok`; Export без змін. Не закриває чергу §2.

v0.13.728 поза ponytail-чергою: `Tools → Users → Manage Backups` повторно використовує наявні pack/restore/share шляхи для restore, delete, archive-only duplicate/rename та sender share. `Sidebar` глобально зберігає section header над першим focus-row без зміни generic `List` і без page snap; Tools START повернено до наявного Install & Share sidebar. Не закриває чергу §2.

v0.13.727 поза ponytail-чергою: User Backup зберігається як один атомарний `.kefir-user.zip` на профіль у `/config/kefir/account_backups`, з manifest/profile validation, safe archive paths, legacy directory compatibility і remote exact-size verification. `user_packs` лишається безпечно сумісним legacy root; не закриває чергу §2.

v0.13.726 поза ponytail-чергою: File Browser на native SD видаляє папки через наявний `DeleteDirectoryRecursively()` всередині ProgressBox, без ручного recursive walker і хибного `FsError_TargetLocked`; `/config/kefir` показує локалізовані пояснення власних папок через наявний другий рядок. Не закриває чергу §2.

v0.13.725 поза ponytail-чергою: launch-time network connect File Browser відкладено до першого focus, щоб ProgressBox був над живим owner menu й completion не звертався до знищеного `FsView`. Не закриває чергу §2.

v0.13.724 поза ponytail-чергою: crash під час закриття прибрано спільним ProgressBox teardown guard; HTTP/HTTPS source автоматично використовує Sphaira JSON `/list` і `/download`, без впливу на WebDAV/WebDAVS/FTP/plain HTML. Не закриває чергу §2.

v0.13.723 поза ponytail-чергою: усі шість Console Transfer shares дають root `http://IP:port` без `/?path=…`; File Browser URL вибраної теки не чіпали. Не закриває чергу §2.

v0.13.722 поза ponytail-чергою: Restore Backup Create+link → SD staging + TE `account_0010_apply_link.te` (без Horizon 0010 write / ApplyLinkPackages); Replace = name/avatar only; wait_link OfferPending. Не закриває чергу §2.

v0.13.721 поза ponytail-чергою: SnapshotOk приймає TE raw dump (stat/dir listing); wait_dump→ready auto-continue StartRestoreBackup; live dump same-session. Не закриває чергу §2.

v0.13.720 поза ponytail-чергою: Restore Backup = nickname/avatar/official NA; без playtime/00F0; Replace лише при доказаному nas (IPC або baas); евристику unproven-Replace з 719 скасовано. Не закриває чергу §2.

v0.13.719 поза ponytail-чергою: Restore Backup — Replace коли pack nas недоведений (0010 closed + Query fail) замість Create на новий uid; TerminateAccountDaemons більше не вбиває ns/friends. Не закриває чергу §2.

v0.13.718 поза ponytail-чергою: restore_pending Undo-знімок — сирий `8000000000000010` + прапорець nand, без unpack/`0010/su`. Не закриває чергу §2.

v0.13.717 поза ponytail-чергою: other-console Restore Backup більше не пише два baas на один nas_id (account 2168-0006) і зупиняє ns перед записом 0010. Не закриває чергу §2.

v0.13.716 поза ponytail-чергою: Other console реально списує і завантажує один user pack без PIN, перевіряє маніфест/розміри та передає його в наявний restore. Не закриває чергу §2.

v0.13.715 поза ponytail-чергою: Share User Backups дає короткий root URL і приймач просить тільки IP; це ще не отримання/відновлення віддалених паків. Не закриває чергу §2.

v0.13.714 поза ponytail-чергою: Restore Backup має локальну бібліотеку, вибір SD-теки та cancellable HTTP-probe іншої консолі. Віддалені паки ще не передаються/не відновлюються. Не закриває чергу §2.

v0.13.713 поза ponytail-чергою: Console Transfer розшарює шість HTTP-джерел через наявний сервер; console-to-console receiver та MTP transport ще не реалізовано. Не закриває чергу §2.

v0.13.712 поза ponytail-чергою: Console Transfer — лише хаб-заглушка; транспорт і доступ до даних не реалізовано. Не закриває чергу §2.

v0.13.711 поза ponytail-чергою: same-console restore пізнає UID з пака. Не закриває чергу §2.

v0.13.710 поза ponytail-чергою: same-console restore питає Replace існуючого профілю. Не закриває чергу §2.

v0.13.709 поза ponytail-чергою: Restore Backup тримає NA і години (TE startup.te). Не закриває чергу §2.

v0.13.708 поза ponytail-чергою: шлях відкату + рестор раніше за лінк. Не закриває чергу §2.

v0.13.707 поза ponytail-чергою: після dump/rollback вантажимо hekate. Не закриває чергу §2.

v0.13.706 поза ponytail-чергою: скрипт відкату `Undo_restore_if_wont_boot.te`. Не закриває чергу §2.

v0.13.705 поза ponytail-чергою: дамп 0010 лише /startup.te, без dump.te в Scripts. Не закриває чергу §2.

v0.13.704 поза ponytail-чергою: dump.te читається після romfsInit. Не закриває чергу §2.

v0.13.703 поза ponytail-чергою: TegraExplorer стартує через autoboot, якщо немає payload-api. Не закриває чергу §2.

v0.13.702 поза ponytail-чергою: UI шлях `hekate > payloads > tegraexplorer`. Не закриває чергу §2.

v0.13.701 поза ponytail-чергою: UI Users без «RCM» — Hekate → Payloads → TegraExplorer. Не закриває чергу §2.

v0.13.700 поза ponytail-чергою: зайнятий 0010 дампиться через startup.te + TegraExplorer. Не закриває чергу §2.

v0.13.699 поза ponytail-чергою: знімок 0010 перед Restore Backup, TE rollback. Не закриває чергу §2.

v0.13.698 поза ponytail-чергою: Backup/Restore попереджають і ребутять самі; не дублюють NA. Не закриває чергу §2.

v0.13.697 поза ponytail-чергою: 00F0 більше не пишеться під Horizon (crash ns 2011-0301); Backup після terminate ACCOUNT просить ребут. Не закриває чергу §2.

v0.13.696 поза ponytail-чергою: review play-hours Backup Restore (порожній PlayEvent, чесний backup/restore текст). Не закриває чергу §2.

v0.13.695 поза ponytail-чергою: Backup Restore дописує зріз PlayEvent цього UID в 00F0 з ремапом; не закриває чергу §2.

v0.13.694 поза ponytail-чергою: експорт лінку через read-only 0010; overwrite/delete бекапів і дата в Restore Backup. Не закриває чергу §2.

v0.13.693 поза ponytail-чергою: Restore Backup ремапить UID у baas; Users Icon має зелену/червону крапку linked. Не закриває чергу §2.

v0.13.692 поза ponytail-чергою: Backup user / Restore Backup під окремим підзаголовком BACKUP & RESTORE USER, не в PROFILE. Не закриває чергу §2.

v0.13.691 поза ponytail-чергою: Backup user / Restore Backup знову в Users Options (PROFILE), L/R прибрано. Не закриває чергу §2.

v0.13.690 поза ponytail-чергою: crash Backup user — `NsApplicationControlData` зі стеку ProgressBox на купу; Users L/R Backup/Restore поза Options. Не закриває чергу §2.

v0.13.689 поза ponytail-чергою: build-fix для Restore Backup — код не посилається на відсутній `Result_FsPathNotFound`; local pack визначається наявною перевіркою теки `baas/`. Не закриває чергу §2.

v0.13.688 поза ponytail-чергою: Backup user експортує profile/avatar/playtime та валідований BaaS/NAS link-пак без game saves; Restore Backup створює нові UID з multi-select і не чіпає наявні профілі, 0011 або 00F0. Запис 0010 має валідацію пакета, перевірений SD rollback до будь-якої мутації та один Commit. Не закриває чергу §2.

v0.13.687 поза ponytail-чергою: Users → Icon повернув повнорозмірні avatar; nickname непоточних плиток над outline, local row gap прибирає overlap, focus comic-хмаринка й UID під сіткою збережені. Не закриває чергу §2.

v0.13.686 поза ponytail-чергою: базовий англійський launch-текст і всі локалі повідомляють про безпеку ігрових сейвів та потребу деяких ігор у linked Nintendo Account для запуску. Не закриває чергу §2.

v0.13.685 поза ponytail-чергою: Users → Icon має окрему верхню caption-смугу для nickname; focus лишає тільки comic-хмаринку, UID під сіткою збережено. Не закриває чергу §2.

v0.13.684 поза ponytail-чергою: український launch-текст прив’язки профілів уточнює безпеку ігрових сейвів та потребу деяких ігор у Nintendo Account для запуску. Не закриває чергу §2.

v0.13.683 поза ponytail-чергою: Users → Icon завжди показує nickname, фокусна comic-хмаринка збережена, а один UID поточного профілю винесено під сітку. Не закриває чергу §2.

## 1. Що перевірено

MainMenu — лише Homebrew і Tools. `MISC_MENU_ENTRIES.func` ніде не викликається.
`App::DisplayAdvancedOptions` не має caller. Живі входи: Tools tiles, Settings,
Software, Updater, `dbi::Menu` з Install & Share.

Граф (EXTRACTED):
- `GetMiscMenuEntries()` — degree 1, лише `contains` `main_menu.cpp`. Caller немає.
- `App::DisplayAdvancedOptions` (app.hpp) — degree 1, лише `defines App`.
- `DisplayDumpOptions` живий з `game_menu`; `DisplayForwarderOptions` живий з
  `homebrew`. `DisplayInstallOptions` тримається на dead `DisplayAdvancedOptions`
  + `gc_menu` + `stream::Menu`.
- `gc_menu.cpp` — degree 52, внутрішній дамп живий. Дверей з Tools немає.
- `BackgroundInstaller` — degree 20, живий хаб. `StreamFile` — лише Fs, caller немає.

Старі `audit.md` / `AUDIT_PLAN.md` / `upstream_audit.md` / hardening-звіти —
історія вже зробленого (USB unify, PFS0, NRO icon, NFS, playtime race, GHDL).
Не джерело наступної роботи.

## 2. Черга (по одному, зверху вниз)

1. **A1 — мертвий factory.** Видалити `MISC_MENU_ENTRIES`, `GetMiscMenuEntries`,
   `MiscMenuFlag_Install` / `IsInstall()`. Не чіпати `gc_menu` у цьому кроці.
   Функції вже в Tools: AppStore → Software, Games/FileBrowser/Saves — плитки,
   Themezer → Themes, GitHub → Software/Network Downloads, FTP/MTP — сервіси
   (Settings + BackgroundInstaller), не окремі екрани.
2. **A2 — GameCard у Games.** Рядок у списку ігор, синя обводка, підпис GameCard.
   Зроблено в v0.13.536: картридж додається/позначається з NCM GameCard storage,
   тримається зверху списку. `gc::Menu` (XCI dump) досі окремий і без дверей.
3. **A3 — мертві екрани.** Зроблено: IRS, `firmware_menu`, `ftp_menu`, `mtp_menu`.
   FTP/MTP як сервіси лишились. Tools знову відкриває Module Manager.
4. **A4 — `stream::Menu` UI.** Після A3 клас меню не має підкласів. Видалити
   `stream::Menu`, `SetActiveMenu` і гілки `s_active_menu`. Залишити `Stream` +
   `BackgroundInstaller` + ProgressBox.
5. **A5 — `DisplayAdvancedOptions`.** Logging / hbmenu / boost / scroll уже в
   Settings. Єдине унікальне — toggle `erpt_reports`. Або один bool у Settings,
   або викинути разом із сайдбаром. `DisplayDumpOptions` і
   `DisplayForwarderOptions` живі (Games / Homebrew) — не чіпати.
   `m_left_menu` / `m_right_menu` мертві.
6. **A6 — мертві i18n ключі** екранів з A3.
7. **A7 — дрібниці.** `StreamFile` (жодної інстанціації), `GetWebdavUrl/User/Pass`
   (немає caller), `haze::DisableInstallMode` / `ftpsrv::DisableInstallMode`.

Після A7 — стоп, знову відкрити цей файл. Не робити «顺便» шаблон settings-list
і не чіпати host-тести.

## 3. Findings (biggest cut first)

`delete:` IRS camera menu, unreachable. Nothing. [sphaira/source/ui/menus/irs_menu.cpp, include/ui/menus/irs_menu.hpp] (~620)

`delete:` firmware list menu, unreachable; live path is kefir updater. Nothing. [sphaira/source/ui/menus/firmware_menu.cpp, include/ui/menus/firmware_menu.hpp] (~440)

`delete:` FTP/MTP Install screens. BackgroundInstaller already installs drops. [sphaira/source/ui/menus/ftp_menu.cpp, mtp_menu.cpp] (~210)

`delete:` stream::Menu UI + SetActiveMenu + s_active_menu branches. Keep Stream + BackgroundInstaller. [sphaira/source/ui/menus/install_stream_menu_base.cpp] (~250)

`delete:` MISC_MENU_ENTRIES factory, MiscMenuFlag_Install, GetMiscMenuEntries, m_left_menu/m_right_menu. Nothing instantiates .func. [sphaira/source/ui/menus/main_menu.cpp, include/ui/menus/main_menu.hpp, include/app.hpp] (~80)

`delete:` App::DisplayAdvancedOptions (no caller). Settings already has the same toggles except erpt. [sphaira/source/app_display_options.cpp] (~100)

`yagni:` GameCard menu is live code with a dead door. Belongs in Games, not Tools. [sphaira/source/ui/menus/gc_menu.cpp, game_menu.cpp]

`delete:` i18n keys only used by the dead install/IRS/firmware screens. Drop from all 14 json. [assets/romfs/i18n/*.json] (~200)

`delete:` StreamFile, unused, comment says so. Drop cpp/hpp and CMake line. [sphaira/source/yati/source/stream_file.cpp] (~40)

`yagni:` GetWebdavUrl/User/Pass, no callers; creds live on location::Entry. [sphaira/source/app_settings.cpp, include/app.hpp] (~40)

`delete:` DisableInstallMode in haze/ftpsrv, no callers. The “launch MTP install menu” toast is dead. [sphaira/source/haze_helper.cpp, ftpsrv_helper.cpp] (~40)

`shrink:` one SUPPORTED_EXT table instead of three copies. [haze_helper.cpp, ftpsrv_helper.cpp, install_stream_menu_base.cpp] (~30)

`yagni:` HashSource vtable + make_unique factory; only Hash(fs)/Hash(span) are called. Switch inside Hash(). [sphaira/source/hasher.cpp] (~150) — after A7, optional.

`shrink:` six nearly identical settings list menus (Software/Dbi/Kefir/Themes/Translate/SourceEdit). One template. [settings_menu.cpp:2610-3125] (~400) — after A7, optional.

`delete:` scratch/ (~15 MB, gitignored). Duplicate sphaira/graphify-out/ (~34 MB). Not in git; local disk only.

net: ~-1900 C++ lines, ~-200 i18n lines, 0 deps.

## 4. Не чіпати

- Автофорвардер: ставимо свій, якщо немає. Старий не видаляємо, якщо з нього зайшли
  (повідомлення: наступного разу — з нового). З нового / Album — можна видалити старий.
- Компіляцію не запускати. Сказати користувачу скомпілювати.
- Host-тести в `tests/` — це gate, не bloat. Не збирати їх агенту.
- `gc_menu` — спочатку двері (A2), не видалення дампу.
- `DisplayDumpOptions` / `DisplayForwarderOptions`.
- Background FTP/MTP install, `dbi::Menu`, NFS, web server.
- FetchContent (zstd/stb/libnfs) — платформа їх потребує.
- Продуктові епіки з старого upstream-аудиту (MTP folder install, MSP, DLC catalog).

## 5. Поза ponytail (не черга, поки не скажеш)

Коректність, не складність. Старий upstream-аудит майже весь уже в коді
(GHDL lifetime, playtime race, PFS0, zero-byte MTP size, GameCard storage-bar).
Лишилось:

- MTP DeviceInfo досі без `Sphaira/<version> (HOS/<fw>)` у `patch_libhaze.cmake`.
- `title_export_name` EN-US/EN-GB оверлоади є, call sites в NSP/MTP передають лише localized name.

## 6. Прибрані файли

Один аудит. Видалено як дублі/історія:

- `AUDIT_PLAN.md`, `implementation_plan.md`
- `upstream_audit.md`, `upstream_implementation_plan.md`
- `nfs_backend_audit.md`, `nro_icon_hardening_audit.md`, `pfs0_nsp_hardening_audit.md`
- `plan.md`, `task.md`, `walkthrough.md`, `archive/*.md`
- `FOOTERS.md`, `GEMINI.md`, `TESTPLAN.md`, `tests.md`

Лишились: цей файл, `README.md`, `AGENTS.md`, `LICENSE`, `hbl/nx-hbloader.LICENSE.md`, `tools/module_catalog/README.md`.
