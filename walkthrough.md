# Результати впровадження: Повноцінний броузинг WebDAV, FTP, HTTP джерел та навігація через System Root (v0.13.237)

Усі заплановані зміни для реалізації підтримки мережевих джерел та зручної ієрархічної навігації було успішно впроваджено.

## Зроблені зміни

### [Component: Devoptab Curl Device]
- **[devoptab_curl_device.hpp](file:///d:/git/dev/sphaira/sphaira/include/utils/devoptab_curl_device.hpp)**:
  - Додано оголошення віртуальних методів `devoptab_t` для файлових та директорійних операцій.
- **[devoptab_curl_device.cpp](file:///d:/git/dev/sphaira/sphaira/source/utils/devoptab_curl_device.cpp)**:
  - Впроваджено структури `CurlFileState` та `CurlDirState` для збереження стану відкритих файлів та кешування лістингу папок.
  - Реалізовано повний POSIX-сумісний інтерфейс файлових операцій через `CURL` для протоколів WebDAV, FTP та HTTP:
    - Читання (`devoptab_read`) через `PushThreadData` та запис (`devoptab_write`) через `PullThreadData`.
    - Лістинг директорій (`devoptab_diropen`, `devoptab_dirnext`) з підтримкою `PROPFIND` (WebDAV/HTTP) та `NLST` (FTP).
    - Видалення файлів/папок (`devoptab_unlink`, `devoptab_rmdir`) через метод `DELETE`.
    - Створення директорій (`devoptab_mkdir`) через `MKCOL` (WebDAV) або `MKD` (FTP).
    - Перейменування (`devoptab_rename`) через `MOVE` з заголовком `Destination`.

### [Component: Devoptab Common]
- **[devoptab_common.hpp](file:///d:/git/dev/sphaira/sphaira/include/utils/devoptab_common.hpp)** / **[devoptab_common.cpp](file:///d:/git/dev/sphaira/sphaira/source/utils/devoptab_common.cpp)**:
  - Додано функцію `IsNetworkDeviceMounted(const std::string& url)`, яка перевіряє, чи примонтований пристрій у devoptab.

### [Component: File Browser UI & Logic]
- **[filebrowser.hpp](file:///d:/git/dev/sphaira/sphaira/include/ui/menus/filebrowser.hpp)**:
  - Додано тип `FsType::Root` та структури статусів `ConnectionStatus`.
  - Додано поля статусу підключення та цільового переходу в `FileEntry` та `FsEntry`.
- **[filebrowser.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/filebrowser.cpp)**:
  - **FsView::Draw**: Додано малювання круглих кольорових позначок (badges) статусу підключення біля мережевих джерел у System Root: зелена (підключено), сіра (розмонтовано), червона (помилка).
  - **FsView::Scan**: Додано підтримку віртуальної директорії `System Root` (`root:/`), яка містить microSD, системні розділи (якщо увімкнено God Mode) та мережеві джерела з їх статусами.
  - **Кнопка A (Open)**: При виборі розмонтованого пристрою в System Root автоматично запускається його підключення через `ConnectToLocation`.
  - **Кнопка B (Back)**: При натисканні назад у корні будь-котрого пристрою користувач переходить у System Root замість виходу з файлового менеджера.
  - **Деструктор Menu**: Додано автоматичне розмонтування всіх мережевих джерел при закритті вікна.
- **[settings_menu.cpp](file:///d:/git/dev/sphaira/sphaira/source/ui/menus/settings_menu.cpp)**:
  - Прибрано перевірки на SMB, що дозволило запускати файловий менеджер для будь-яких налаштованих мережевих джерел.

### [Component: CMake Configuration]
- **[CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt)**:
  - Ітеровано версію програми `sphaira_VERSION` до `0.13.237`.
