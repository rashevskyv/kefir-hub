# plan.md

Версія коду: **v0.13.872**.

## Поточний delivery: v0.13.873 — split Save menu UI/catalog

Мета: behavior-preserving розкладання `save_menu.cpp` (2 788 рядків) за вже наявними UI/catalog responsibilities; `save_menu_ops.cpp` лишити окремим наступним delivery.

1. Зберегти public `Menu` API, routes, ordering, filters і user-visible behavior.
2. Винести backup-library scan/group/catalog helpers з UI lifecycle у cohesive units ≤600 рядків.
3. Відділити layout/entry rendering і picker/dialog callbacks лише там, де межа вже очевидна.
4. Не додавати service/interface/factory/registry; не міняти save formats або restore admission.
5. Gemini спочатку робить split і compiler-free checks; WSL build/fixes — тільки після senior review і явного follow-up.
6. Після прийняття: bump `0.13.872` → `0.13.873`, docs і focused commit без push.

## Попередній delivery: v0.13.872 — Save paths structural split

`save_paths.cpp` зменшено з 1 794 до 463 рядків. Exact discovery, bounded metadata decoders, archive admission та backup inspection винесено у чотири implementation units і один private header; усі нові файли мають 84–555 рядків. Public API та поведінка не змінені. WSL `ReleaseWithInstall`, 18/18 contract suites, dead-symbol gate, EN/UK parity і diff check пройшли.

## Наступні serial deliveries

1. Saves operations: розкласти `save_menu_ops.cpp` без зміни backup/restore semantics.
2. Web: file routes/upload окремо від server+mDNS та page templates.
3. App: widget stack, USB/MTP і platform lifecycle; спочатку dead declarations.
4. Transfers/Yati: core pipeline, ZIP/unzip/verification та install analysis.
5. Provider UI: Cheats/AppStore/Themezer/Kefir — API/parsing окремо від меню.
6. Tests: один discoverable runner; великі suites ділити лише за стабільними сценаріями.

## Межі

- Один product delivery за раз на primary `master`.
- Нові source/test/instruction files — не більше 600 physical lines.
- Не додавати abstraction або dependency заради самого поділу.
- Не запускати compile/build до фінального Gemini follow-up поточного delivery.
- `assets/romfs/tegra/TegraExplorer.bin` — дозволена фонова зміна, поза delivery.

Історія попередніх delivery доступна через Git; активні документи її не дублюють.
