# plan.md

Версія коду: **v0.13.877**.

## Поточний delivery: v0.13.877 — Transfer structural split (завершено)

`threaded_file_transfer.cpp` зменшено з 1 956 до 263 рядків без зміни public API. Core, archive preflight, native verification і ZIP I/O винесено в чотири конкретні `.cpp` та три приватні headers (17–501 рядок). `yati.cpp` — окремий наступний delivery; дозволена фонова зміна `TegraExplorer.bin` поза цим комітом.

Gemini повідомив про WSL `ReleaseWithInstall` build `[100%] Built target sphaira_nro`, 18/18 Python contracts, dead-symbol gate (981 declarations) та whitespace PASS. Senior перевірив diff, CMake, version, межі файлів і перенесені source assertions; не запускав тести чи збірку повторно. Консольний runtime ще не перевірено.

## Попередній delivery: v0.13.876 — App structural split

`app.cpp` зменшено з 2 111 до 523 рядків, `app_settings.cpp` — до 554. Startup, loop/frame, UI stack, USB/MTP, network і save settings розкладено у конкретні units ≤597 рядків; public `app.hpp` лишився незмінним (626 рядків, окремий legacy debt). Gemini повідомив про успішний WSL `ReleaseWithInstall`, 18/18 contracts, dead-symbol gate, EN/UK parity і whitespace checks.

## Наступні serial deliveries

1. `yati.cpp` (1 842): installer parsing і pipeline розділити за стабільними межами.
2. Provider UI: Cheats/AppStore/Themezer/Kefir — API/parsing окремо від меню.
3. Tests: один discoverable runner; великі suites ділити лише за стабільними сценаріями.
4. `app.hpp` (626): окремо перевірити dead declarations і layout/ініціалізаційні ефекти перед будь-яким видаленням.

## Межі

- Один product delivery за раз на primary `master`.
- Нові source/test/instruction files — не більше 600 physical lines.
- Не додавати abstraction або dependency заради самого поділу.
- Не запускати compile/build до фінального Gemini follow-up наступного delivery.
- `assets/romfs/tegra/TegraExplorer.bin` — дозволена фонова зміна, поза delivery.

Історія попередніх delivery доступна через Git; активні документи її не дублюють.
