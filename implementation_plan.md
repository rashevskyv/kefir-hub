# План впровадження: Підтримка протоколів Awoo та GoldLeaf у USB-інсталяторі (v0.13.338)

Цей план описує додавання підтримки протоколів передачі **GoldLeaf** та **Awoo (TinFoil)** для сумісності з ПК-клієнтами (зокрема [ns-usbloader](https://github.com/developersu/ns-usbloader)).

## Необхідний розгляд користувачем

> [!IMPORTANT]
> - Програма `ns-usbloader` використовує два основних USB-протоколи встановлення: **Awoo** (TinFoil TUL0/TUC0) та **GoldLeaf** (GL01/GLC1).
> - У розділі **USB Install** реалізується автоматичне визначення заголовка підключення: якщо клієнт відправляє заголовок Awoo (`TUL0`), використовується протокол Awoo; якщо клієнт відправляє заголовок GoldLeaf (`GL01`), використовується протокол GoldLeaf.
> - Це забезпечує повну сумісність із будь-яким розжимом у `ns-usbloader` без необхідності змінювати налаштування на Switch.
> - Версію програми буде ітеровано до `0.13.338` у [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt).

## Відкриті питання

Немає.

## Пропоновані зміни

---

### [Компонент: USB Protocol & Yati Source]

#### [NEW] [goldleaf.hpp](file:///d:/git/dev/sphaira/sphaira/include/usb/goldleaf.hpp)
- Створення файлу заголовка протоколу GoldLeaf (`Magic_GoldleafList0` `GL01`, `Magic_GoldleafCommand0` `GLC1`, структури `USBCmdHeader` та `FileRangeHeader`).

#### [MODIFY] [usb.hpp](file:///d:/git/dev/sphaira/sphaira/include/yati/source/usb.hpp)
#### [MODIFY] [usb.cpp](file:///d:/git/dev/sphaira/sphaira/source/yati/source/usb.cpp)
- Додавання автоматичного розпізнавання магічних чисел сигнатури підключення: підтримка заголовків `TUL0` (Awoo) та `GL01` (GoldLeaf).
- Адаптація відправки команд запиту даних у залежності від розпізнаного протоколу (Awoo vs GoldLeaf).

---

### [Компонент: Конфігурація та Документація]

#### [MODIFY] [CMakeLists.txt](file:///d:/git/dev/sphaira/sphaira/CMakeLists.txt)
- Оновлення `sphaira_VERSION` з `0.13.337` на `0.13.338`.

#### [MODIFY] [README.md](file:///d:/git/dev/sphaira/README.md)
- Додавання опису підтримки двох протоколів USB-інсталяції (Awoo та GoldLeaf для `ns-usbloader`).

#### [MODIFY] [task.md](file:///d:/git/dev/sphaira/task.md)
- Додавання нового delivery `v0.13.338` із задачею `FEAT-GOLDLEAF-AWOO-USB-338`.

## План верифікації

### Автоматичні тести
- Автоматична збірка NRO у середовищі WSL (`./build.sh`).

### Ручна перевірка (після збірки NRO)
1. Запустити `ns-usbloader` на ПК.
2. Протестувати відправку NSP/XCI у режимі **Awoo Protocol**.
3. Переключити `ns-usbloader` у режим **GoldLeaf Protocol** та перевірити успішне визначення та встановлення пакету.
