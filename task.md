# task.md — Задачі розробки

## v0.13.928 — Чекпоїнт збірки та верифікація

- [x] Запустити повну компіляцію Sphaira в WSL з пресетом `ReleaseWithInstall` (`cmake --preset ReleaseWithInstall && cmake --build --preset ReleaseWithInstall --parallel $(nproc)`).
- [x] Діагностувати та перевірити відсутність помилок компіляції (`sphaira_nro` побудовано на 100%).
- [x] Виконати повний набір хост-тестів (`tests/run.sh`) у WSL (`all green`).
- [x] Підвищити версію проекту в `sphaira/CMakeLists.txt` до `0.13.928`.
- [x] Оновити документацію та журнал змін (`docs/dev/CHANGELOG.md`, `plan.md`, `task.md`, `walkthrough.md`, `audit.md`).
- [x] Створити комміт чекпоїнту збірки.
