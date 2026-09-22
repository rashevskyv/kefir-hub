# plan.md

Версія коду: **v0.13.874**.

## Поточний delivery: v0.13.875 — Web structural split

Мета: behavior-preserving розділення `web.cpp` (2 381 рядок) за file routes/upload, server+mDNS і public UI bridge; `web_pages.hpp` (1 010 рядків) — за вже наявними сторінками.

1. Спочатку підтвердити callers і межі HTTP/server/UI, зберегти існуючі routes та public API.
2. Виділити конкретні implementation units ≤600 рядків без нових service/interface/factory або template engine.
3. Не змішувати structural split зі зміною HTTP-поведінки чи HTML-макетів.
4. Gemini спочатку робить split і compiler-free checks; WSL build/fixes — тільки після senior review і явного follow-up.
5. Після прийняття: bump `0.13.874` → `0.13.875`, docs і focused commit без push.

## Попередній delivery: v0.13.874 — Save operations split

`save_menu_ops.cpp` зменшено з 2 696 до 301 рядка. Backup publication/writer, ZIP/folder restore, remote sync і deletion винесено у шість implementation units та чотири приватні headers (усі ≤544 рядків). Public `Menu` API, safety ordering і save formats збережено. Gemini повідомив про успішний WSL `ReleaseWithInstall`, 18/18 contracts, dead-symbol gate, EN/UK parity і diff check.

## Наступні serial deliveries

1. App: widget stack, USB/MTP і platform lifecycle; спочатку dead declarations.
2. Transfers/Yati: core pipeline, ZIP/unzip/verification та install analysis.
3. Provider UI: Cheats/AppStore/Themezer/Kefir — API/parsing окремо від меню.
4. Tests: один discoverable runner; великі suites ділити лише за стабільними сценаріями.

## Межі

- Один product delivery за раз на primary `master`.
- Нові source/test/instruction files — не більше 600 physical lines.
- Не додавати abstraction або dependency заради самого поділу.
- Не запускати compile/build до фінального Gemini follow-up наступного delivery.
- `assets/romfs/tegra/TegraExplorer.bin` — дозволена фонова зміна, поза delivery.

Історія попередніх delivery доступна через Git; активні документи її не дублюють.
