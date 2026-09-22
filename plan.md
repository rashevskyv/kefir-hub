# plan.md

Версія коду: **v0.13.873**.

## Поточний delivery: v0.13.874 — split Save operations

Мета: behavior-preserving розкладання `save_menu_ops.cpp` (2 696 рядків) за вже наявними backup, restore, remote sync та deletion responsibilities.

1. Зберегти public `Menu` API, operation routing, archive admission, recovery і mutation semantics.
2. Винести backup publication, restore ZIP/folder, remote sync та deletion за cohesive boundaries ≤600 рядків.
3. Залишити shared helpers приватними й конкретними; не дублювати ZIP reader або safety logic.
4. Не додавати service/interface/factory/registry чи змінювати save formats.
5. Gemini спочатку робить split і compiler-free checks; WSL build/fixes — тільки після senior review і явного follow-up.
6. Після прийняття: bump `0.13.873` → `0.13.874`, docs і focused commit без push.

## Попередній delivery: v0.13.873 — Save menu UI/catalog split

`save_menu.cpp` зменшено з 2 788 до 308 рядків. Draw, filters, options, scan, target lookup, catalog і action prompts винесено у сім implementation units та 28-рядковий private header; усі нові units ≤573 рядків. Public API та поведінка не змінені. Gemini повідомив про успішний WSL `ReleaseWithInstall`, 18/18 contracts, dead-symbol gate, EN/UK parity і diff check.

## Наступні serial deliveries

1. Web: file routes/upload окремо від server+mDNS та page templates.
2. App: widget stack, USB/MTP і platform lifecycle; спочатку dead declarations.
3. Transfers/Yati: core pipeline, ZIP/unzip/verification та install analysis.
4. Provider UI: Cheats/AppStore/Themezer/Kefir — API/parsing окремо від меню.
5. Tests: один discoverable runner; великі suites ділити лише за стабільними сценаріями.

## Межі

- Один product delivery за раз на primary `master`.
- Нові source/test/instruction files — не більше 600 physical lines.
- Не додавати abstraction або dependency заради самого поділу.
- Не запускати compile/build до фінального Gemini follow-up поточного delivery.
- `assets/romfs/tegra/TegraExplorer.bin` — дозволена фонова зміна, поза delivery.

Історія попередніх delivery доступна через Git; активні документи її не дублюють.
