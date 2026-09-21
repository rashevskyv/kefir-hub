# plan.md

Версія коду: **v0.13.871**.

## Поточний delivery: v0.13.872 — split Save paths and metadata

Мета: behavior-preserving розкладання `save_paths.cpp` (1 794 рядки) за вже наявними відповідальностями; жодних нових interfaces/factories і жодної зміни save formats, discovery або restore admission.

1. Залишити path naming/building у `save_paths.cpp`.
2. Винести bounded JKSV/Sphaira/DBI/NX metadata decode/read у cohesive unit ≤600 рядків.
3. Винести backup search/discovery та archive inspection/integrity за стабільними API з `save_paths.hpp`.
4. Оновити explicit CMake source list; не міняти callers або public semantics.
5. Gemini спочатку робить split і non-build checks; WSL compile та виправлення compile/link errors — лише після фінального senior follow-up.
6. Після прийняття: bump `0.13.871` → `0.13.872`, docs і focused commit без push.

## Попередній delivery: v0.13.871 — dead-code cleanup і build closure

Видалено недосяжні advanced/misc menu factory, install `stream::Menu` route, active-menu callback synchronization, `StreamFile` і три dead WebDAV getters. Додано лише явні live includes; build-closure виправила п'ять накопичених Save type/include errors без зміни контрактів. Повний WSL `ReleaseWithInstall`, 18/18 compiler-free suites, dead-symbol gate, EN/UK parity та diff check пройшли.

## Наступні serial deliveries

1. Saves UI/actions: розкласти `save_menu.cpp` і `save_menu_ops.cpp` без зміни поведінки.
2. Web: відділити file routes/upload від server+mDNS та page templates.
3. App: відділити widget stack, USB/MTP і platform lifecycle; спочатку прибрати dead facade methods.
4. Transfers/Yati: відділити core pipeline, ZIP/unzip/verification та install analysis.
5. Provider UI: Cheats/AppStore/Themezer/Kefir — API/parsing окремо від меню.
6. Tests: один discoverable runner для compiler-free Python contracts; великі suites ділити тільки за стабільними сценаріями.

## Межі

- Один product delivery за раз на primary `master`.
- Нові source/test/instruction файли — не більше 600 physical lines.
- Не додавати interface/factory/template заради самого поділу.
- Не запускати compile/build до фінального Gemini follow-up поточного delivery.
- `assets/romfs/tegra/TegraExplorer.bin` — дозволена фонова зміна, поза delivery.

Історія попередніх delivery доступна через Git; активні документи її не дублюють.
