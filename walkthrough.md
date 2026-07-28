# Результати роботи: Додавання stat fallback для DT_UNKNOWN записів у fs.cpp (v0.13.358)

У цій версії усунуто системну проблему у `fs.cpp`, яка призводила до ігнорування файлів та папок MTP.

## Внесені зміни

### 1. `stat()` fallback для `DT_UNKNOWN` ([fs.cpp](file:///d:/git/dev/sphaira/sphaira/source/fs.cpp))
- **Виявлена проблема**: Метод `Dir::Read` та `DirGetEntryCount` у `fs.cpp` перевіряли `d->d_type == DT_DIR` або `d->d_type == DT_REG`. При будь-якому іншому значенні (включаючи `DT_UNKNOWN = 0`) виводилося `[FS] WARNING: unknown type when reading dir: 0` і викликався `continue`. Драйвери newlib devoptab при виклику `readdir()` часто віддають `DT_UNKNOWN`, через що всі файли та папки мовчки ігнорувалися.
- **Виправлення**: Якщо `d_type` не `DT_DIR` і не `DT_REG`, викликається `stat()` для визначення `S_ISDIR` / `S_ISREG`. Об'єкти правильно класифікуються та додаються у список файлового менеджера.

### 2. Версіонування та документація
- Версію оновлено до `0.13.358` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).
- Оновлено [task.md](file:///d:/git/dev/sphaira/task.md), [plan.md](file:///d:/git/dev/sphaira/plan.md) та [walkthrough.md](file:///d:/git/dev/sphaira/walkthrough.md).

## Верифікація збірки

- **WSL збірка**: Програму буде зкомпільовано через WSL при запуску верифікації.
