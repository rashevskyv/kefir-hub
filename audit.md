# audit.md

Версія коду: **v0.13.872**. Дата аудиту: 2026-09-21.

## Поточний стан

- v0.13.872 розклала `save_paths.cpp` з 1 794 до 463 рядків на path/config, discovery, metadata decode, archive admission та backup inspection.
- Нові C/C++ units: 84, 109, 292, 353 і 555 рядків; public API стабільний, нових abstractions/dependencies немає.
- Full WSL `ReleaseWithInstall`, 18/18 repository contracts, dead-symbol gate і EN/UK parity PASS.
- Save build-closure units лишилися 591/597 рядків.
- 35 oversized files знаходяться в `sphaira/source/ui/menus`.
- Graphify після delivery: 13 279 nodes, 26 515 edges, 662 communities; сім C/C++ файлів розібрано частково через parser syntax limitations.
- Активні delivery-документи скорочено; попередня історія лишається в Git без потрійного дублювання.

## Черга — виконувати серійно

### A1 — dead-code cleanup (`v0.13.871`) — DONE

1. `App::DisplayAdvancedOptions` не має caller-а. Разом із ним мертві `GetMiscMenuEntries`, `MISC_MENU_ENTRIES` і generator.
2. `stream::Menu` не має subclasses/instances. `SetActiveMenu` не має caller-а; `s_active_menu` branches обслуговують лише цей шлях.
3. `yati::source::StreamFile` не інстанціюється.
4. `App::GetWebdavUrl/User/Pass` мають лише declaration + definition; активні callers використовують `GetWebdavUrlName` та `location::Entry`.
5. Фактичний результат: no new dependencies/abstractions, full build PASS.

### A2 — Saves structural split — IN PROGRESS

Legacy units: `save_menu.cpp` 2 788 і `save_menu_ops.cpp` 2 696 рядків. `save_paths.cpp` split завершено у v0.13.872. Наступне:

- v0.13.873: view/layout, filters і backup catalog зі стабільним `Menu` API;
- наступний delivery: backup, ZIP/folder restore, remote sync і deletion з `save_menu_ops.cpp`.

Не вводити service interfaces/factories. Existing `Menu`/free-function APIs спочатку лишити стабільними.

### A3 — Web

`web.cpp` 2 381: file routes/upload, server+mDNS, public UI bridge. `web_pages.hpp` 1 010: розкласти існуючі constexpr templates за сторінками; template engine не додавати.

### A4 — App

`app.cpp` 2 111: runtime loop, widget stack, USB/MTP, platform lifecycle, renderer. `app_settings.cpp` 1 101: доменні implementation units зі стабільним API. `app.hpp` спочатку зменшити видаленням dead declarations.

### A5 — Transfers and installer

- `threaded_file_transfer.cpp` 1 956: core pipeline, archive paths, ZIP/unzip, verification.
- `yati.cpp` 1 842: ticket/CNMT analysis, threaded install pipeline, public entry points.

### A6 — Provider-heavy UI

Відділяти API/parsing/storage від UI в `cheat_download_menu.cpp`, `appstore.cpp`, `kefir_menu.cpp`, `download.cpp`, `themezer.cpp`, `cheats_menu.cpp`, `settings_fancurve.cpp`.

### A7 — Tests/tooling

18 Python contract scripts, 17 483 рядки; 11 більші за 600. `tests/run.sh` їх автоматично не запускає. Додати один stdlib/shell runner; великі suites ділити за scenario boundaries, не видаляти safety cases і не додавати framework.

## Не чіпати механічно

- i18n JSON — табличні дані, line cap не застосовувати.
- `hbl/source/main.c`, patch CMake, platform adapters — ділити лише з конкретним maintenance outcome.
- Не мінімізувати embedded HTML заради line count.
- Не змішувати behavior change з broad file split.
- Не створювати interfaces з однією реалізацією, registries або speculative config.
- Не запускати compile/build до фінального Gemini follow-up поточного delivery.

## Agent routing

- Save UI/state: `source/ui/menus/save_menu*.cpp`
- Save wire/discovery/mutation: `source/ui/menus/save/`
- Web HTTP/UI: `source/web*.cpp`, `source/web_pages.hpp`
- App lifecycle/settings: `source/app*.cpp`, `include/app.hpp`
- Installer: `source/yati/`, `include/yati/`
- MTP filesystem: `source/utils/devoptab_mtp.cpp`
- Active work: `plan.md`, `task.md`; shipped outcome: `walkthrough.md`; next cut: this file.

Історичні accepted-delivery докази доступні через Git і не є активною чергою.
