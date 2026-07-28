# План реалізації: Додавання stat fallback для DT_UNKNOWN записів devoptab (v0.13.358)

Цей реліз вирішує фундаментальну проблему у `fs.cpp`, через яку елементи з типом `DT_UNKNOWN` відкидалися при читанні каталогів MTP/devoptab.

## Зроблені зміни

1. **Фолбек на `stat()` для `DT_UNKNOWN` у `fs.cpp`**:
   - У `DirGetEntryCount` та `Dir::Read` при значенні `d_type == DT_UNKNOWN` (0) запис раніше пропускався (`continue`). Newlib `readdir()` для devoptab пристроїв повертає 0 (`DT_UNKNOWN`), якщо прапорці типу не підтягнулися з stat.
   - Додано автоматичний виклик `stat()` на запис: якщо `stat()` визначає категорію об'єкта як `S_ISDIR` або `S_ISREG`, елемент додається у підсумковий список як папка або файл відповідно.

2. **Версіонування**:
   - Версію оновлено до `0.13.358` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt#L3).

## План верифікації

- [x] Збірка `.nro` бінарника у WSL (`wsl bash -l -c "cd /mnt/d/git/dev/sphaira && cmake --build --preset ReleaseWithInstall"`).
- [ ] Перевірка відображення списку файлів MTP у `mtp0:`.
