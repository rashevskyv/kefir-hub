# Список завдань: Виправлення зауважень щодо Samba (SMB) локацій (v0.13.219)

- [x] Виправити `RegisterFilesystem()` та `RegisterFilesystem_v2()` у `devoptab_smb2.cpp` (додати перевірку результату `register_fs()`).
- [x] Реалізувати безпечний `disconnect()` в `devoptab_smb2.cpp` з очищенням контексту `smb2`, URL/parser-ресурсів та обнуленням вказівників при помилках підключення.
- [x] Оновити `FsView::IsReadOnly(const fs::FsPath& path)` у `filebrowser_ops.cpp` для врахування `m_fs_entry.IsReadOnly()`.
- [x] Задати прапорець `FsEntryFlag_ReadOnly` для SMB джерел у `ShowSourcePicker()` в `filebrowser.cpp`.
- [x] Замінити ручну перевірку схеми на `e.IsSmb()` у `ShowSourcePicker()`.
- [x] Додати статичний метод `UrlEncode` у `filebrowser.cpp` та закодувати компоненти URL (user, pass, share, path) для NXMP.
- [x] Враховувати user/password при порівнянні SMB-локацій, щоб зміна облікових даних створювала нову сесію.
- [x] Не дозволяти перемикачу `Ignore read only` знімати примусовий read-only режим з SMB.
- [x] Відновити опис інших мережевих протоколів (NFS/SFTP/FTP/HTTP) в `plan.md` як незавершених.
- [x] Оновити версію на `0.13.219` у `sphaira/CMakeLists.txt`.
- [x] Виконати повну WSL-збірку проекту; збірка успішна, warnings є у сторонній `libsmb2` та в наявному коді проєкту, не зміненому цим релізом.
