# plan.md

Версія коду: **v0.13.875**.

## Поточний delivery: v0.13.876 — App structural split

Мета: behavior-preserving зменшення `app.cpp` (2 111 рядків) і `app_settings.cpp` (1 101 рядок) за наявними lifecycle, widget stack, USB/MTP та settings responsibilities. Спочатку перевірити dead declarations у `app.hpp`.

1. Підтвердити callers і точні state/lifecycle boundaries через graph та source review.
2. Виділяти конкретні implementation units ≤600 рядків, зберігаючи public `App` API та порядок shutdown/startup.
3. Не змішувати structural split зі зміною поведінки UI, USB/MTP або settings.
4. Gemini спочатку робить split і compiler-free checks; WSL build/fixes — тільки після senior review і явного follow-up.
5. Після прийняття: bump `0.13.875` → `0.13.876`, docs і focused commit без push.

## Попередній delivery: v0.13.875 — Web structural split

`web.cpp` зменшено з 2 381 до 538 рядків; `web_pages.hpp` — з 1 010 до 5. Page templates, mDNS, shared FS helpers, upload/file routes і router розкладено у конкретні units; усі нові файли ≤576 рядків. Public Web API і 10 raw-string payloads збережено. Gemini повідомив про успішний WSL `ReleaseWithInstall`, 18/18 contracts після оновлення version/source-location assertions, dead-symbol gate, EN/UK parity і diff check.

## Наступні serial deliveries

1. Transfers/Yati: core pipeline, ZIP/unzip/verification та install analysis.
2. Provider UI: Cheats/AppStore/Themezer/Kefir — API/parsing окремо від меню.
3. Tests: один discoverable runner; великі suites ділити лише за стабільними сценаріями.

## Межі

- Один product delivery за раз на primary `master`.
- Нові source/test/instruction files — не більше 600 physical lines.
- Не додавати abstraction або dependency заради самого поділу.
- Не запускати compile/build до фінального Gemini follow-up наступного delivery.
- `assets/romfs/tegra/TegraExplorer.bin` — дозволена фонова зміна, поза delivery.

Історія попередніх delivery доступна через Git; активні документи її не дублюють.
