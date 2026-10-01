# task.md — Задачі розробки

## v0.13.934 — Чекпоїнт збірки та верифікація

- [x] Запустити повну компіляцію Sphaira в WSL з пресетом `ReleaseWithInstall` (`cmake --preset ReleaseWithInstall && cmake --build --preset ReleaseWithInstall --parallel $(nproc)`).
- [x] Верифікувати відсутність помилок збірки після комітів v0.13.929–v0.13.933 (`[100%] Built target sphaira_nro`).
- [x] Виконати повний набір хост-тестів (`tests/run.sh`) у WSL (`all green`).
- [x] Підвищити версію проекту в `sphaira/CMakeLists.txt` до `0.13.934`.
- [x] Оновити документацію та журнал змін (`docs/dev/CHANGELOG.md`, `plan.md`, `task.md`, `walkthrough.md`, `audit.md`).
- [x] Створити комміт чекпоїнту збірки на Windows.
