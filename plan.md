# План реалізації: Усунення USB DMA overflow при читанні MTP відповідей (v0.13.357)

Цей реліз виправляє системний баг USB DMA контролера, через який `GetObjectInfo` та `ReceiveMtpResponse` повертали помилку переповнення (0x25A8C), спричиняючи відображення порожнього списку файлів.

## Зроблені зміни

1. **Гарантований 64КБ DMA буфер для читання з USB IN ендпоінта (`PostAndWaitMtpTransfer`)**:
   - У `PostAndWaitMtpTransfer` при викликах читання (`is_write == false`) розмір буфера `post_size` для `usbHsEpPostBufferAsync` тепер строго вирівнюється до 64 КБ (`MTP_XFER_BUF_SIZE`).
   - Раніше виклик з `size = 32` (для `ReceiveMtpResponse`) змушував USB DMA контролер Nintendo Switch запитувати у пристрою лише 32 байти. Коли смартфон надсилав стандартний 512-байтовий USB Bulk пакет, виникав оверфлоу (`0x25A8C`), через що відповіді MTP губилися і список файлів залишався порожнім.

2. **Версіонування**:
   - Версію оновлено до `0.13.357` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt#L3).

## План верифікації

- [x] Збірка `.nro` бінарника у WSL (`wsl bash -l -c "cd /mnt/d/git/dev/sphaira && cmake --build --preset ReleaseWithInstall"`).
- [ ] Перевірка відображення файлів MTP у `mtp0:`.
