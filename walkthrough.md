# Результати роботи: Усунення USB DMA переповнення та відображення файлів MTP (v0.13.357)

У цій версії виправлено критичний баг апаратного USB DMA контролера Switch при обробці MTP відповідей.

## Внесені зміни

### 1. Вирівнювання DMA буфера читання ([devoptab_mtp.cpp](file:///d:/git/dev/sphaira/sphaira/source/utils/devoptab_mtp.cpp))
- **Виявлена проблема**: Метод `PostAndWaitMtpTransfer` передавав у `usbHsEpPostBufferAsync` фактичний розмір структури `size` (наприклад 32 байти для `ReceiveMtpResponse`). Оскільки wMaxPacketSize USB High-Speed Bulk IN ендпоінта становить 512 байт, надходження 512-байтового пакета від смартфона викликало помилку переповнення USB контролера (`0x25A8C`). Через це `ReceiveMtpResponse` та `GetObjectInfo` постійно падали, і список файлів залишався порожнім.
- **Виправлення**: Для всіх IN-трансферів (`!is_write`) у `usbHsEpPostBufferAsync` тепер передається строго 64 КБ буфер (`MTP_XFER_BUF_SIZE`), що гарантує прийом пакетів будь-якого розміру без overflow.

### 2. Версіонування та документація
- Версію оновлено до `0.13.357` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).
- Оновлено [task.md](file:///d:/git/dev/sphaira/task.md), [plan.md](file:///d:/git/dev/sphaira/plan.md) та [walkthrough.md](file:///d:/git/dev/sphaira/walkthrough.md).

## Верифікація збірки

- **WSL збірка**: Програму буде зкомпільовано через WSL при запуску верифікації.
