# plan.md

Версія коду: **v0.13.870**. Accepted baseline: `c04f7f495b6c8c8247a3d065a2bfc732a291d590`.

## Поточний delivery: v0.13.871 — dead-code cleanup

Мета: прибрати перевірені недосяжні UI/API-гілки без зміни живої поведінки та без механічного поділу великих файлів у тому самому delivery.

1. Видалити мертвий `App::DisplayAdvancedOptions` і єдину залежну від нього фабрику `MISC_MENU_ENTRIES` / `GetMiscMenuEntries`.
2. Видалити неінстанційований `stream::Menu`, `BackgroundInstaller::SetActiveMenu` та `s_active_menu` branches; зберегти `stream::Stream`, `BackgroundInstaller` і DBI install flow.
3. Видалити неінстанційований `yati::source::StreamFile` та його CMake entry.
4. Видалити `App::GetWebdavUrl/User/Pass`, які мають лише declaration + definition; `GetWebdavUrlName` і `location::Entry` не змінювати.
5. Перевірити diff, callers, file sizes і dead-symbol gate. Gemini запускає компіляцію лише після окремого фінального запиту senior-а.
6. Після прийняття: bump `0.13.870` → `0.13.871`, синхронізувати чотири delivery-документи і створити focused commit без push.

## Наступні serial deliveries

1. Saves: розкласти три legacy-файли 1 794–2 788 рядків за вже наявними відповідальностями, без зміни поведінки.
2. Web: відділити file routes/upload від server+mDNS та page templates.
3. App: відділити widget stack, USB/MTP і platform lifecycle; спочатку прибрати dead facade methods.
4. Transfers/Yati: відділити core pipeline, ZIP/unzip/verification та install analysis.
5. Provider UI: Cheats/AppStore/Themezer/Kefir — API/parsing окремо від меню.
6. Tests: один discoverable runner для compiler-free Python contracts; великі suites ділити тільки за стабільними сценаріями.

## Межі

- Один product delivery за раз на primary `master`.
- Нові source/test/instruction файли — не більше 600 physical lines.
- Не додавати interface/factory/template заради самого поділу.
- Не запускати compile/build до фінального Gemini follow-up цього delivery.
- `assets/romfs/tegra/TegraExplorer.bin` — дозволена фонова зміна, поза delivery.

Історія попередніх delivery доступна через Git; активні документи її не дублюють.
