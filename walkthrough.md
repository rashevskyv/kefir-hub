# walkthrough.md

Актуальний shipped delivery — **v0.13.870** (2026-09-21).

## v0.13.870 — Game Tools save-slot manager

Selected-game `Saves` використовує shared authoritative discovery і зберігає повний `FsSaveDataInfo`. Рядки показують nickname/type, space, rank, index, save ID та allocated/data/journal sizes без Account UID. Exact extra-data read failure видимий і блокує grow без remap або fallback.

Create обмежено installed-title Account/User/Primary/index-0 та explicit nickname-only local user. NACP defaults і aligned +16/+64 MiB presets проходять plan → confirm → worker → `CreateSaveDataChecked`. Grow використовує retained exact identity через `ExtendSaveDataChecked`; cancellation, invalid size і no-op не доходять до IPC. Verified success або post-IPC uncertainty запускають authoritative refresh.

Реалізація: game-side helper/header/test — 372/36/477 рядків. Перевірено 18 compiler-free Python suites, dead-symbol gate (983 declarations), EN/UK JSON parse та exact 2549-key parity, `git diff --check`: PASS. Compile/WSL/NRO/libnx IPC/Switch hardware не запускалися. Дозволена фонова зміна `TegraExplorer.bin` не входила в delivery і не валідувалася.

Попередня історія delivery доступна через Git (`git log` / `git show`) і не дублюється в активному контексті агента.
