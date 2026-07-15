# Список завдань: Доопрацювання генератора каталогу sysmodule (v0.13.215)

- [x] Оновити `tools/module_catalog/sources.py` для роботи з OmniRoute (порт 20128) <!-- id: 0 -->
  - Використовувати модель `kiro/claude-sonnet-4.5`
  - Передавати заголовок `Authorization: Bearer sk-a42ea38fcbf6f291-02aa37-e9755ddc`
  - Передавати параметр `stream: False` у JSON-тіло
- [x] Виправити всі `tid_evidence` (які зараз повертають 404) <!-- id: 1 -->
  - Перевірити та оновити посилання на докази для verified модулів, щоб вони повертали HTTP 2xx
  - Додати правильні посилання на вихідний код, Makefile, конфіги чи toolbox.json, які містять точний 16-символьний Title ID
- [x] Перенести з unresolved до verified наступні модулі: <!-- id: 2 -->
  - `0100000000C0FFEE` — pad-macro (https://github.com/impeeza/pad-macro, evidence: `Makefile`)
  - `0100000000000035` — sys-ftpd-light (https://github.com/mrdude2478/sys-ftpd-light, evidence: `Makefile`)
  - `0100000000554443` — ReverseUX (https://github.com/masagrator/ReverseUX-App, evidence: `Makefile`)
  - `4200000000000FFF` — triplayer (https://github.com/samueldr/sys-triplayer, evidence: `Makefile`/`toolbox.json`)
- [x] Оновити `manual_overrides.json` та верифікувати генерацію каталогу <!-- id: 3 -->
  - Додати правильні метадані, TID-докази та описи для перерахованих вище модулів
- [x] Оновити документацію та плани <!-- id: 4 -->
  - Підняти версію у `CMakeLists.txt` до `0.13.215`
  - Оновити `plan.md` та `walkthrough.md` українською мовою
  - Додати опис нових функцій до README.md англійською мовою

