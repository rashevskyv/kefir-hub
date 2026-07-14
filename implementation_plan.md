# Реалізація розширення протоколу DBI Backend (передача розмірів файлів)

Цей план описує зміни для передачі розмірів файлів від `dbibackend-qt` до `Sphaira` через розширення USB DBI протоколу. Це дозволить показувати розміри файлів перед початком встановлення та правильно розраховувати необхідний обсяг вільної пам'яті.

## Proposed Changes

### [Component: Sphaira (C++)]

#### [MODIFY] [usb_dbi.hpp](file:///d:/git/dev/sphaira/sphaira/include/yati/source/usb_dbi.hpp)
- Додати `std::unordered_map<std::string, s64> m_file_sizes;` у приватні поля `DbiUsb`.
- Додати публічний метод `s64 GetFileSize(const std::string& name) const;` для отримання розміру файлу за його назвою.

#### [MODIFY] [usb_dbi.cpp](file:///d:/git/dev/sphaira/sphaira/source/yati/source/usb_dbi.cpp)
- У `DbiUsb::WaitForConnection`:
  - Змінити виклик `SendCmdHeader(dbi::CmdType::Request, dbi::CmdId::List, 0, timeout)` на `SendCmdHeader(dbi::CmdType::Request, dbi::CmdId::List, 0x53504841, timeout)` (символи `'SPHA'`).
  - Після зчитування списку імен файлів, розбирати кожен рядок. Якщо він містить символ `|`, відокремлювати чисту назву файлу та його розмір.
  - Чисту назву додавати в `out_names`, а розмір зберігати в `m_file_sizes`.
  - Якщо символу `|` немає, зберігати оригінальну назву і встановлювати розмір у `0` (для сумісності зі старим `dbibackend`).
- Реалізувати `s64 DbiUsb::GetFileSize(const std::string& name) const` який повертає розмір із `m_file_sizes`.

#### [MODIFY] [dbi_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/dbi_menu.cpp)
- У `Menu::ThreadFunction` під час циклу аналізу файлів з черги:
  - Перед викликом `yati::AnalyzeSource` отримати розмір від `m_usb_source->GetFileSize(name)` і зберегти його у `entry.analysis.source_size`.

---

### [Component: dbibackend-qt (Python)]

#### [MODIFY] [usb_handler.py](file:///e:/Switch/dbibackend-qt/src/usb_handler.py)
- Змінити сигнатуру `process_list_command(self)` на `process_list_command(self, data_size)`.
- У `poll_commands()` передати `data_size` під час виклику `self.process_list_command(data_size)`.
- У `process_list_command()`:
  - Якщо `data_size == 0x53504841`, формувати рядок списку у форматі `filename|size_in_bytes\n`.
  - Інакше формувати звичайний рядок `filename\n` для забезпечення сумісності.

## Verification Plan

### Automated Tests
- Немає автоматизованих тестів.

### Manual Verification
- Збірка `Sphaira` через WSL (`make` або `cmake`).
- Перевірка підключення та передачі файлів з ПК через `dbibackend-qt`:
  - Запустити `dbibackend-qt` на ПК з доданими файлами.
  - Запустити меню DBI Install у `Sphaira` на Switch (через емулятор або консоль).
  - Перевірити, що список файлів відображає правильні розміри.
  - Спробувати встановити гру та перевірити відсутність помилок/зависань.
