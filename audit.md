# audit.md

Канонічний робочий файл. Версія коду: **v0.13.712**. Дата: 2026-08-31.
Ponytail-аудит усього дерева. Фікси цим файлом не застосовуються.

Карта коду: repo-root `graphify-out/` (див. `AGENTS.md`). Перед grep —
`graphify query` / `path` / `explain`. Граф оновлено 2026-08-23:
10719 nodes, 20360 edges. HTML агрегований (`graph.html`, 1077 community nodes).

Далі працюємо тільки з чергою в §2.

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
