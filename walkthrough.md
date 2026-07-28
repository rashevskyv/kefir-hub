# Результати роботи: Виправлення відображення файлів у MTP-накопичувачі та активна детекція від'єднання (v0.13.355)

У цій версії виправлено проблему порожніх папок ("empty") при переході в MTP-накопичувач та усунуто фальшивий статус підключення і помилки при від'єднаному смартфоні.

## Внесені зміни

### 1. Багатопакетний прийом MTP Data Phase ([devoptab_mtp.cpp](file:///d:/git/dev/sphaira/sphaira/source/utils/devoptab_mtp.cpp))
- **Виявлена проблема**: Якщо payload MTP-контейнера перевищував розмір першого буфера (64 КБ), `ReceiveMtpData` повертав частину даних, але не зчитував решту з USB IN ендпоінта. Непрочитані байти блокували наступні запити відповідей MTP `ReceiveMtpResponse`, викликаючи таймаут та USB stall.
- **Виправлення**: Впроваджено цикл зчитування залишків даних до повного заповнення `payload_len = hdr->length - sizeof(MtpContainerHeader)`.

### 2. Запит кореневих об'єктів ([devoptab_mtp.cpp](file:///d:/git/dev/sphaira/sphaira/source/utils/devoptab_mtp.cpp))
- **Виявлена проблема**: Для кореневої папки `parent_handle = 0xFFFFFFFF` відправлявся `GetObjectHandles` із `parent = 0xFFFFFFFF`. Більшість Android MTP-серверів шукають об'єкти з `parent = 0x00000000` для кореневих файлів/папок і повертали 0 елементів.
- **Виправлення**: У `FetchDirectoryEntries` для кореневої папки першим кандидат-ParentHandle опитується `0x00000000`, а потім `0xFFFFFFFF`.

### 3. Детекція від'єднання кабелю та скидання сесії ([devoptab_mtp.cpp](file:///d:/git/dev/sphaira/sphaira/source/utils/devoptab_mtp.cpp))
- **Виправлення**: У `ScanAndMountMtpDevices()` перед вибором кешованої сесії виконується легкий пінг пристрою `GetDeviceInfo (0x1001)`. При від'єднанні кабелю або збої викликається `CloseMtpSessionLocked()`, що демонтує `mtpX:` пристрої з devoptab та очищає кеш.

### 4. Безпечний парсинг UTF-16 іменування ([devoptab_mtp.cpp](file:///d:/git/dev/sphaira/sphaira/source/utils/devoptab_mtp.cpp))
- **Виправлення**: Каст покажчика `info_data.data() + 53` змінено на побайтове читання UTF-16LE у `Utf16LeToUtf8`, що усуває alignment faults на архітектурі ARM64.

### 5. Версіонування та документація
- Версію оновлено до `0.13.355` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).
- Оновлено [task.md](file:///d:/git/dev/sphaira/task.md), [plan.md](file:///d:/git/dev/sphaira/plan.md) та [walkthrough.md](file:///d:/git/dev/sphaira/walkthrough.md).

## Верифікація збірки

- **WSL збірка**: Програму буде зкомпільовано через WSL при запуску верифікації.
