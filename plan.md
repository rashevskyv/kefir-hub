# plan.md

Версія коду: **v0.13.876**.

## Поточний delivery: Transfers/Yati — scoping

Мета: вибрати перший вузький, behavior-preserving зріз із `threaded_file_transfer.cpp` (1 956 рядків) або `yati.cpp` (1 842 рядки). Новий product delivery починати лише після підтвердження інтеграції v0.13.876 у `master`.

1. Через graph і source review визначити конкретний pipeline boundary, callers та safety ordering.
2. Дати Gemini один обмежений зріз із новими units ≤600 рядків, без нової service abstraction.
3. Після senior review доручити Gemini фінальний WSL build і contracts; лише тоді синхронізувати version/docs та commit.

## Попередній delivery: v0.13.876 — App structural split

`app.cpp` зменшено з 2 111 до 523 рядків, `app_settings.cpp` — до 554. Startup, loop/frame, UI stack, USB/MTP, network і save settings розкладено у конкретні units ≤597 рядків; public `app.hpp` лишився незмінним (626 рядків, окремий legacy debt). Gemini повідомив про успішний WSL `ReleaseWithInstall`, 18/18 contracts, dead-symbol gate, EN/UK parity і whitespace checks.

## Наступні serial deliveries

1. Provider UI: Cheats/AppStore/Themezer/Kefir — API/parsing окремо від меню.
2. Tests: один discoverable runner; великі suites ділити лише за стабільними сценаріями.
3. `app.hpp` (626): окремо перевірити dead declarations і layout/ініціалізаційні ефекти перед будь-яким видаленням.

## Межі

- Один product delivery за раз на primary `master`.
- Нові source/test/instruction files — не більше 600 physical lines.
- Не додавати abstraction або dependency заради самого поділу.
- Не запускати compile/build до фінального Gemini follow-up наступного delivery.
- `assets/romfs/tegra/TegraExplorer.bin` — дозволена фонова зміна, поза delivery.

Історія попередніх delivery доступна через Git; активні документи її не дублюють.
