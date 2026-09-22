# audit.md

Версія коду: **v0.13.875**. Дата аудиту: 2026-09-22.

## Поточний стан

- v0.13.875 розклала `web.cpp` з 2 381 до 538 рядків і `web_pages.hpp` з 1 010 до 5; усі нові Web files ≤576 рядків.
- mDNS, shared FS, upload/file routes і HTTP router розділені; 10 page payloads збережено byte-for-byte, public API стабільний.
- Gemini: full WSL `ReleaseWithInstall`, 18/18 repository contracts, dead-symbol gate і EN/UK parity PASS.
- Save build-closure units лишилися 591/597 рядків.
- 32 C/C++ files понад 600 рядків знаходяться в `sphaira/source/ui/menus`.
- Graphify є картою зв’язків; source-level перевірка потрібна для кожного нового delivery.
- Активні delivery-документи скорочено; попередня історія лишається в Git без потрійного дублювання.

## Черга — виконувати серійно

### A1 — dead-code cleanup (`v0.13.871`) — DONE

1. `App::DisplayAdvancedOptions` не має caller-а. Разом із ним мертві `GetMiscMenuEntries`, `MISC_MENU_ENTRIES` і generator.
2. `stream::Menu` не має subclasses/instances. `SetActiveMenu` не має caller-а; `s_active_menu` branches обслуговують лише цей шлях.
3. `yati::source::StreamFile` не інстанціюється.
4. `App::GetWebdavUrl/User/Pass` мають лише declaration + definition; активні callers використовують `GetWebdavUrlName` та `location::Entry`.
5. Фактичний результат: no new dependencies/abstractions, full build PASS.

### A2 — Saves structural split — DONE

`save_paths.cpp` split завершено у v0.13.872; `save_menu.cpp` — у v0.13.873; `save_menu_ops.cpp` — у v0.13.874.

- Backup, ZIP/folder restore, remote sync і deletion винесені зі стабільним `Menu` API та safety ordering.

Не вводити service interfaces/factories. Existing `Menu`/free-function APIs спочатку лишити стабільними.

### A3 — Web — DONE

У v0.13.875 `web.cpp` 538 і `web_pages.hpp` 5 рядків; existing constexpr templates розкладено за сторінками без template engine. Full WSL build і 18/18 contracts PASS за звітом Gemini.

### A4 — App — NEXT

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
- Не запускати compile/build до фінального Gemini follow-up наступного delivery.

## Agent routing

- Save UI/state: `source/ui/menus/save_menu*.cpp`
- Save wire/discovery/mutation: `source/ui/menus/save/`
- Web HTTP/UI: `source/web*.cpp`, `source/web_pages.hpp`
- App lifecycle/settings: `source/app*.cpp`, `include/app.hpp`
- Installer: `source/yati/`, `include/yati/`
- MTP filesystem: `source/utils/devoptab_mtp.cpp`
- Active work: `plan.md`, `task.md`; shipped outcome: `walkthrough.md`; next cut: this file.

Історичні accepted-delivery докази доступні через Git і не є активною чергою.
