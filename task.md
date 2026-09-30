# task.md

Версія коду: **v0.13.922**.

## Поточний delivery: v0.13.922 — відновлення MTP без перепідключення кабелю

- [x] MTP-RECOVERY-922 — захистити інтервал від локального cancel до повторної USB-ініціалізації від наглядача USB та не скидати захист на старій сесії.
- [x] MTP-REMOUNT-922 — дозволити повторне ввімкнення MTP за фактичного незапущеного haze і підключеного VBUS.
- [x] VERIFY-922 — Python контракти/моделі, чистий та проміжний CMake patch, ідемпотентність, `git diff --check`.
- [x] DOCS-BUMP-922 — версія й delivery-документи.
- [x] COMMIT-922 — сфокусований коміт на primary master без ROMFS binary.
- [ ] BUILD-HW-922 — зібрати NRO та перевірити на Switch/Windows скасування, повторне підключення MTP без перепідключення кабелю й PC-side cancel.

## Попередній delivery: v0.13.921 — виправлення збірки libhaze та верифікація NRO

- [x] BUILD-HAZE-RESP-921 — усунути дублювання зовнішнього блоку `R_TRY_CATCH` при заміні обробників скасування у `source/ptp_responder.cpp` через виділення цільового блоку `resp_cpp_catch_cancel_new`.
- [x] BUILD-NRO-921 — успішно зібрати таргет `sphaira_nro` (100%) через пресет `ReleaseWithInstall` у WSL без помилок компіляції та лінкування.
- [x] VERIFY-921 — перевірено Python тести контрактів MTP скасування (`test_mtp_cancellation_contract.py`, `test_mtp_cancellation_models.py`), застосування й ідемпотентність CMake-патчів та `git diff --check`.
- [x] DOCS-BUMP-921 — оновити версію, delivery-документи (plan.md, task.md, walkthrough.md, audit.md).
- [x] COMMIT-921 — сфокусований коміт на primary master без ROMFS binary.
- [ ] HW-921 — перевірка поведінки скасування MTP та повторного відкриття сесії на консолі Nintendo Switch / Windows.

## Попередній delivery: v0.13.920 — негайне переривання MTP передачі

- [x] MTP-ABORT-920 — припинити читання поточного файла після локального cancel без drain до EOT.
- [x] MTP-RECOVER-920 — завершити USB transport і відновити MTP лише після локального cancel, із обмеженими повторними спробами ініціалізації.
- [x] MTP-PATCH-920 — підтримати чистий upstream, проміжні форми й ідемпотентне застосування патча.
- [x] VERIFY-920 — Python контракти, моделі та `git diff --check` без компіляції.
- [x] DOCS-BUMP-920 — оновити версію, документи й README.
- [x] COMMIT-920 — сфокусований коміт на primary master без ROMFS binary.
- [x] BUILD-HW-920 — успішна компіляція NRO (100%) у WSL.

## Попередній delivery: v0.13.919 — динамічний вибір мови

- [x] I18N-LIST-919 — формувати список із валідних JSON та їх `__language_name`, відсортувати й виключити `ru`.
- [x] I18N-CONFIG-919 — зберігати код локалі, мігрувати чинні числові значення, показувати вибір за відсутньої або недоступної мови.
- [x] I18N-UI-919 — один діалог для першого запуску й налаштувань; вилучити метадані з перекладів UI.
- [x] VERIFY-919 — цільові Python тести й `git diff --check` без компіляції.
- [x] DOCS-BUMP-919 — версія, план, walkthrough, audit, README та wiki.
- [x] COMMIT-919 — сфокусований коміт на primary master без ROMFS binary.
- [ ] BUILD-HW-919 — окрема збірка NRO та перевірка на Switch.

## Попередній delivery: v0.13.918 — завершення MTP транзакції після скасування

- [x] MTP-DRAIN-918 — зберегти local cancel та відкидати залишок передачі без запису consumer.
- [x] MTP-LIFECYCLE-918 — обмежений cleanup wait, завершення URB, stop та припинення transport при втраті framing.
- [x] MTP-PATCH-918 — розділені patch-файли, оновлення попередніх форм та повторна ідемпотентність.
- [x] VERIFY-918 — Python контракти/моделі, patch-check і git diff --check без компіляції.
- [x] DOCS-BUMP-918 — версія та delivery-документи.
- [x] COMMIT-918 — сфокусований commit на primary master без ROMFS binary.
- [ ] BUILD-918 — зібрати NRO через окремий test-build запит.
- [ ] HW-918 — Switch/Windows: тести 2.1–2.3, повторні cancel, наступний Install/SD файл і Start під час cleanup.

## Попередній delivery: v0.13.917 — безпечне скасування MTP встановлення

- [x] `MTP-CANCEL-917` — не перезапускати MTP після підтвердженого скасування; зберегти відновлення при незалежному обриві джерела.
- [x] `MTP-USB-917` — зупиняти MTP для відновлення без перемикання USB у host mode.
- [x] `VERIFY-917` — цільові Python контракти й `git diff --check` без компіляції.
- [x] `DOCS-BUMP-917` — оновити версію й delivery-документи.
- [x] `COMMIT-917` — сфокусований commit на primary master без ROMFS binary.
- [ ] Зібрати NRO та перевірити скасування, повторну передачу й вихід Start на Switch/Windows.

## Попередній delivery: v0.13.916 — виправлення компіляції проксі haze та верифікація збірки

- [x] `BUILD-HAZE-MACRO-916` — усунути конфлікт макросів `R_SUCCEED`, `R_THROW`, `R_TRY` та типів `ams::Result` шляхом вилучення `#include <haze/results.hpp>` з `haze_fs_proxy.cpp` та `haze_install_proxy.cpp`.
- [x] `BUILD-HAZE-CANCEL-916` — визначити функцію `::haze::ResultCancelled()` з поверненням `MAKERESULT(420, 19)` типу `Result` (`u32`) у `sphaira/include/haze/haze_internal.hpp`.
- [x] `BUILD-VERIFY-916` — повна збірка `ReleaseWithInstall` у WSL з успішною генерацією таргету `sphaira_nro` (100%).
- [x] `TESTS-PARALLEL-916` — паралельна перевірка тестів контрактів MTP скасування (`test_modal_priority_and_mtp_cancel_contract.py` та `test_mtp_cancellation_contract.py`).
- [x] `DOCS-BUMP-916` — оновити версію й delivery-документи (plan.md, task.md, walkthrough.md, audit.md).
- [x] `COMMIT-916` — сфокусований commit на primary master без ROMFS binary.
- [ ] Перевірити бінарник `sphaira_nro` та скасування MTP на Switch/ПК.

## Попередній delivery: v0.13.915 — перший вибір мови та 26 локалізацій

- [x] `LANG-FIRST-915` — вимагати підтвердження мови при першому запуску й зберігати її після вибору.
- [x] `LANG-LEGACY-915` — зберегти старі ідентифікатори, мігрувати Auto та Russian і виключити російську з інтерфейсу.
- [x] `LANG-26-915` — додати 26 вбудованих локалей, регіональні варіанти й повні ключі перекладу.
- [x] `LANG-GATE-915` — перевіряти локалізації перед деплоєм; оновити README та wiki.
- [x] `VERIFY-915` — контракт деплою, 8 перевірок логіки, 39 тестів перекладача й `git diff --check` без компіляції.
- [x] `DOCS-BUMP-915` — оновити версію й delivery-документи.
- [x] `COMMIT-915` — сфокусований commit на primary master без ROMFS binary.
- [ ] Скомпілювати NRO та перевірити перший запуск, збереження й відображення всіх мов на Switch.

## Попередній delivery: v0.13.914 — скасування MTP встановлення та безпечне завершення

- [x] `MTP-MODAL-914` — передати ввід верхньому модальному підтвердженню під час активного встановлення.
- [x] `MTP-CANCEL-914` — передати скасування libhaze та повернути `ResultCancelled` для перерваного MTP запису.
- [x] `MTP-SHUTDOWN-914` — завершити worker перед MTP і не перезапускати сервіс під час виходу.
- [x] `VERIFY-914` — п'ять цільових Python контрактів і `git diff --check` без компіляції.
- [x] `DOCS-BUMP-914` — оновити версію й delivery-документи.
- [x] `COMMIT-914` — сфокусований commit на primary master без ROMFS binary.
- [ ] Скомпілювати NRO та перевірити скасування й вихід під час активного MTP на Switch/ПК, зокрема відсутність збою `usb`.

## Попередній delivery: v0.13.913 — MTP копіювання в корінь пристрою

- [x] `MTP-ROOT-913` — маршрутизувати файли й папки з кореня пристрою до Install або microSD без зміни прямого копіювання у сховища.
- [x] `VERIFY-913` — перевірити патч libhaze, MTP контракт і `git diff --check` без збірки.
- [x] `DOCS-BUMP-913` — оновити версію, README, wiki та delivery-документи.
- [x] `COMMIT-913` — сфокусований commit на primary master без ROMFS binary.
- [ ] Скомпілювати NRO та перевірити копіювання на вузол Nintendo Switch у Windows і на консолі.

## Попередній delivery: v0.13.912 — виправлення збірки C++ та патча libhaze

- [x] `BUILD-OPERATOR-912` — додати перевантаження `operator==` для `ams::Result` у `include/haze/results.hpp`.
- [x] `BUILD-SCOPE-912` — звузити заміну `ops_read_ok` до `GetObject` у `patch_libhaze_cancel.cmake` без зачіпання `GetObjectHandles`.
- [x] `BUILD-IDEMPOTENT-912` — усунути дублювання блоків перевірки `m_reactor` та надати безпечну дедуплікацію.
- [x] `BUILD-VERIFY-912` — повна збірка `ReleaseWithInstall` у WSL з генерацією `sphaira_nro` (100%).
- [x] `TESTS-PARALLEL-912` — паралельний запуск цільових MTP контрактів та перевірки патча libhaze.
- [x] `DOCS-BUMP-912` — оновити версію й delivery-документи.
- [x] `COMMIT-912` — сфокусований commit на primary master без ROMFS binary.
- [ ] Перевірити бінарник `sphaira_nro` та скасування MTP на Switch/ПК.

## Попередній delivery: v0.13.911 — скасування MTP передачі на microSD

- [x] `MTP-CANCEL-911` — передати підтверджене скасування в libhaze, завершити UI без повторного відкриття та видалити неповний файл.
- [x] `MTP-LIFECYCLE-911` — захистити наступний файл і вихід MTP від запізнілого сигналу та взаємного блокування.
- [x] `VERIFY-911` — Python контракти, патч libhaze на чистому HEAD із повторним запуском, `git diff --check`; без C++ збірки.
- [x] `DOCS-BUMP-911` — оновити версію й delivery-документи.
- [x] `COMMIT-911` — сфокусований commit на primary master без ROMFS binary.
- [ ] Скомпілювати та перевірити скасування на Switch/ПК і наступну передачу в тій самій MTP сесії.

## Попередній delivery: v0.13.910 — 39-бітний форвардер за замовчуванням і вибір CPU-ядер

- [x] `FORWARDER-DEFAULT-910` — 39 біт і 3 CPU-ядра типово, явні 36 біт збережено.
- [x] `FORWARDER-CORE-910` — попередження перед 4 ядрами у загальних і персональних опціях; NPDM ACI0/ACID отримує вибрані права.
- [x] `VERIFY-910` — JSON локалізацій і `git diff --check`; C++ збірку не запускати під час звичайної зміни.
- [x] `DOCS-BUMP-910` — версія, локалізації, README, wiki та delivery-документи синхронізовані.
- [x] `COMMIT-910` — сфокусований коміт на primary master без ROMFS binary.
- [ ] Скомпілювати та перевірити форвардери 36/39 біт і 3/4 CPU-ядра на фізичному Switch.

## Попередній delivery: v0.13.909 — ручне встановлення форвардера поточного додатка в меню «+»

- [x] `FORWARDER-MENU-909` — додати прямий пункт над «Налаштуваннями» у спільному меню «+» без залежності від виділеного homebrew.
- [x] `VERIFY-909` — перевірити маршрут викликів, наявність локалізацій і `git diff --check`.
- [x] `DOCS-BUMP-909` — підняти версію й оновити delivery-документи.
- [x] `COMMIT-909` — сфокусований коміт на primary master без ROMFS binary.
- [ ] Скомпілювати та перевірити ручне встановлення на фізичному Switch.

## Попередній delivery: v0.13.908 — MTP копіювання пакетів на SD та прогрес згорнутого встановлення

- [x] `MTP-ROUTE-908` — зберігати пакунки в корені й підпапках microSD, лишити потокове встановлення за віртуальним Install.
- [x] `MTP-UI-908` — заповнювати згорнуту плашку за відомим прогресом пакунка та показувати копіювання після завершеної MTP інсталяції.
- [x] `VERIFY-908` — три цільові Python контракти й `git diff --check` пройшли.
- [x] `DOCS-BUMP-908` — підняти версію, оновити README, wiki й delivery-документи.
- [x] `COMMIT-908` — сфокусований коміт на primary master без ROMFS binary.
- [ ] Скомпілювати та перевірити MTP копіювання й встановлення на фізичному Switch.

## Попередній delivery: v0.13.907 — усунення колізії імен у CollectBackups та верифікація збірки C++

- [x] `BUILD-FIX-907` — додати `#include "path_util.hpp"` та кваліфікувати `sphaira::path::IsSubpathOf` у `save_backup_pub.cpp`.
- [x] `BUILD-VERIFY-907` — перевірити збірку `ReleaseWithInstall` у WSL до успішного створення `sphaira_nro` (100%).
- [x] `TEST-907` — запустити контрактний тест `test_save_backup_destination_contract.py`.
- [x] `DOCS-BUMP-907` — підняти версію й оновити delivery-документи.
- [x] `COMMIT-907` — сфокусований коміт на primary master без ROMFS binary.
- [ ] Перевірити бінарник `sphaira_nro` на фізичному Switch.

## Попередній delivery: v0.13.906 — стабільний шлях форвардера Kefir Hub

- [x] `FORWARDER-PATH-906` — обирати `/hbmenu.nro` для автоматичного й ручного форвардера за ввімкненої заміни HB Menu.
- [x] `FORWARDER-PROMPT-906` — показувати обрану ціль у ручному підтвердженні.
- [x] `VERIFY-906` — перевірити diff, шляхи встановлення та `git diff --check`; C++ збірка і Switch runtime окремо.
- [x] `DOCS-BUMP-906` — підняти версію й оновити delivery-документи.
- [x] `COMMIT-906` — сфокусований коміт на primary master без ROMFS binary.
- [ ] Скомпілювати та перевірити обидва режими форвардера на фізичному Switch.

## Попередній delivery: v0.13.905 — власна папка для дампів сейвів

- [x] `SAVE-PATH-905` — писати ігрові DBI ZIP у вибрану папку; типово `/dumps`.
- [x] `SAVE-RESTORE-905` — знаходити бекапи у вибраній папці та зберегти читання старих DBI каталогів.
- [x] `SAVE-SYNC-905` — передавати в автосинхронізацію точний шлях створеного архіву.
- [x] `VERIFY-905` — цільові Python контракти й `git diff --check`; C++ збірка та Switch runtime окремо.
- [x] `DOCS-BUMP-905` — версію, локалізації, вікі й delivery-документи синхронізовано.
- [x] `COMMIT-905` — сфокусований коміт на primary master без ROMFS binary.
- [ ] Перевірити дамп, відновлення з DBI й автосинхронізацію на фізичному Switch.

## Попередній delivery: v0.13.904 — завершення MTP плашки після останнього файла

- [x] `MTP-IDLE-904` — скидати таймер для кожного нового файла, зокрема короткого між двома опитуваннями.
- [x] `MTP-RELAUNCH-904` — повторно показувати плашку для файла, що почався під час закриття попередньої; зберігати стан завершеної сесії.
- [x] `MTP-PUSH-904` — не блокувати UI при відмові `PushTransfer`; прибрати зайвий прапорець і ручне очищення події.
- [x] `VERIFY-904` — цільовий Python контракт і `git diff --check` пройшли; C++ збірка та Switch runtime окремо.
- [x] `DOCS-BUMP-904` — версію та delivery-документи синхронізовано.
- [x] `COMMIT-904` — сфокусований коміт на primary master без ROMFS binary.
- [ ] Перевірити MTP передачу на фізичному Switch.

## Попередній delivery: v0.13.903 — виправлення життєвого циклу UI передачі MTP та попереджень компіляції


- [x] `MTP-UI-903` — відстежувати `g_mtp_transfer_active` та `g_mtp_transfer_seq` під м'ютексом, усунути втрату сигналів завершення та зависання ProgressBox.
- [x] `BUILD-FIX-903` — виправити синтаксис виклику макроса `R_SUCCEED()` у `haze_helper.cpp` та `static_cast<unsigned int>` для `external_fa` у `threaded_file_transfer_preflight.cpp`.
- [x] `TEST-903` — додати поведінковий контракт `tests/test_mtp_transfer_lifecycle_contract.py` та перевірити проходження.
- [x] `DOCS-BUMP-903` — оновити версію 0.13.903 у CMakeLists.txt та документах супроводу.
- [x] `COMMIT-903` — сфокусований коміт без бінарників ROMFS.

## Попередній delivery: v0.13.902 — синхронізація README та документації/вікі до релізу

- [x] `DOCS-AUDIT-902` — перевірити всі зміни від релізу 0.13.601 до 0.13.901 і додати їх до README та вікі.
- [x] `WIKI-902` — створити повну модульну вікі в `docs/wiki/` (Home, Installation-and-USB, Save-Management, User-Profiles, Console-Transfer, Firmware-and-Downgrades, Network, System-and-Tools).
- [x] `DOCS-BUMP-902` — оновити версію 0.13.902 у CMakeLists.txt та документах супроводу.
- [x] `COMMIT-902` — сфокусований коміт оновлення документації без бінарників ROMFS.

## Попередній delivery: v0.13.901 — оновлення USB черги під час пакунка

- [x] `QUEUE-901` — опитувати SPHQ після завершеного FileRange читання та застосовувати лише майбутню частину черги.
- [x] `PLAN-901` — аналізувати новий пакунок перед його встановленням і лише потім обирати Auto носій.
- [x] `VERIFY-901` — сім цільових Python контрактів пройшли; C++ збірка й фізичний Switch окремо.
- [x] `DOCS-BUMP-901` — версію та документи оновлено.
- [x] `COMMIT-901` — сфокусований коміт на primary master без ROMFS binary.
- [ ] Перевірити нову поведінку на фізичному Switch.

## Попередній delivery: v0.13.900 — виправлення помилок збірки C++ та верифікація

- [x] `BUILD-900` — усунено дублювання `SetTitle` у `sidebar.hpp`, виправлено базовий клас `OwnfoilForm` (`ui::Sidebar`), енуми `SoundEffect`, виклики `List::Draw`, `swkbd::ShowText`, `ProgressBox` та глобальні libnx виклики `nsInitialize`/`nsExit`.
- [x] `VERIFY-900` — повна чиста збірка WSL `ReleaseWithInstall` успішно завершилась генерацією `sphaira_nro` (100%); чотири контрактні Python тести Sphaira та 36 тестів DBI Backend пройшли паралельно; whitespace/diff check чистий.
- [x] `DOCS-BUMP-900` — версія 0.13.900 та документи оновлено.
- [x] `COMMIT-900` — сфокусований коміт на primary master без ROMFS binary.
- [ ] Перевірити повний USB цикл на фізичному Switch.

## Попередній delivery: v0.13.899 — жива USB черга

- [x] `QUEUE-899` — застосовувати впорядкований SPHQ список під час ReviewQueue та лише до майбутніх пакетів під час Installing.
- [x] `ACK-899` — надсилати підтвердження застосованої ревізії; повторювати після помилки відправки.
- [x] `VERIFY-899` — шість цільових Python контрактів Sphaira і 36 тестів backend пройшли; C++ збірка та Switch runtime окремо.
- [x] `DOCS-BUMP-899` — patch version і документи синхронізовано.
- [x] `COMMIT-899` — сфокусований коміт на primary master без ROMFS binary.
- [ ] Перевірити повний USB цикл на фізичному Switch та скомпілювати Sphaira.

## Попередній delivery: v0.13.898 — порожня черга SPHQ

- [x] `SPHQ-898` — приймати лише точний маркер порожньої відповіді, вмикати sync без фіктивного пакунка; backend надсилає маркер лише для порожнього SPHQ.
- [x] `VERIFY-898` — два цільові Python контракти й diff check пройшли; C++ збірка та Switch runtime окремо.
- [x] `DOCS-BUMP-898` — patch version і delivery-документи синхронізовано.
- [x] `COMMIT-898` — сфокусований коміт на primary master без ROMFS binary.

## Попередній delivery: v0.13.897 — початковий DBI USB запит (завершено)

- [x] `DBI-897` — після Awoo listen надсилати List/SPHQ до читання DBI відповіді.
- [x] `VERIFY-897` — цільовий Python контракт і diff check пройшли; C++ збірка та Switch runtime окремо.
- [x] `DOCS-BUMP-897` — версію та delivery-документи синхронізовано.
- [x] `COMMIT-897` — сфокусований коміт на primary master без ROMFS binary.

## Попередній delivery: v0.13.896 — клієнт Ownfoil (завершено)

- [x] `OWNFOIL-896` — сервери, виявлення, авторизація, каталог, сторінка гри та вибір контенту інтегровані в меню.
- [x] `INSTALL-896` — HTTP Range, докачування, скасування та фільтр title IDs підключені до Yati.
- [x] `VERIFY-896` — дев'ять статичних контрактів, JSON і diff check пройшли; C++ збірка та Switch runtime окремо.
- [x] `DOCS-BUMP-896` — версію та delivery-документи синхронізовано.
- [x] `COMMIT-896` — focused commit на primary master без ROMFS binary.

## Попередній delivery: v0.13.895 — рекурсивне встановлення з папок (завершено)

- [x] `SCAN-895` — контекстна дія знаходить підтримувані пакети у вибраних папках і всіх вкладених папках.
- [x] `QUEUE-895` — знайдені пакети відкривають наявну чергу встановлення для перегляду й вибору; сканування не запускає встановлення.
- [x] `VERIFY-895` — Gemini повідомив про проходження короткого контракту, EN/UK JSON і diff check; senior перевірив код та ліміти рядків. Збірка й Switch runtime окремо.
- [x] `DOCS-BUMP-895` — patch version і delivery-документи синхронізовано.
- [x] `COMMIT-895` — focused commit на primary master без ROMFS binary.

## Попередній delivery: v0.13.894 — групи бекапів за грою (завершено)

- [x] `GROUP-894` — одна гра у Backups відкриває окремі типи/користувачів/слоти без втрати архівів і джерел.
- [x] `RESTORE-894` — «Відновити всі» показує архіви, дати й цілі до одного підтвердження; відсутні архіви та Device/BCAT цілі блокують старт з поясненням.
- [x] `VERIFY-894` — Gemini повідомив про п'ять успішних Python контрактів; senior перевірив diff, i18n, межу 600 рядків і whitespace. Збірка та Switch runtime окремо.
- [x] `DOCS-BUMP-894` — patch version і delivery-документи синхронізовано.
- [x] `COMMIT-894` — focused commit на primary master без ROMFS binary.

## Попередній delivery: v0.13.893 — повернення з вкладеного файлового браузера (завершено)

- [x] `BACK-893` — «−» закриває браузер, відкритий з іншого меню, і повертає до нього.
- [x] `SCOPE-893` — перевірено всі місця відкриття браузера та прапорець вкладки.
- [x] `VERIFY-893` — source/diff check пройшов; збірка та Switch runtime окремо.
- [x] `DOCS-BUMP-893` — patch version і delivery-документи синхронізовано.
- [x] `COMMIT-893` — focused commit на primary master без ROMFS binary.

## Попередній delivery: v0.13.892 — виправлено виклик batch restore (завершено)

- [x] `BUILD-892` — виклик `PromptBatchRestoreTargets` використовує перевантаження з прапорцем `return_to_actions`.
- [x] `VERIFY-892` — звірено помилку компілятора з сигнатурами; цільовий Python контракт і diff check пройшли. Повторна збірка та Switch runtime очікують користувача.
- [x] `DOCS-BUMP-892` — patch version і delivery-документи синхронізовано.
- [x] `COMMIT-892` — focused commit на primary master без ROMFS binary.

## Попередній delivery: v0.13.891 — спрощення дій Backups (завершено)

- [x] `NAV-891` — B зі списку користувачів після A → Restore повертає меню дій для тієї самої вибірки.
- [x] `MENU-891` — прибрано дубль «Restore for user…»; у + → ACTIONS вкладки Backups залишено лише Restore.
- [x] `VERIFY-891` — шість цільових Python контрактів і diff check пройшли; NRO-збірка та Switch runtime окремо.
- [x] `DOCS-BUMP-891` — patch version і delivery-документи синхронізовано.
- [x] `COMMIT-891` — focused commit на primary master без ROMFS binary.

## Попередній delivery: v0.13.890 — виправлення збірки назв плиток (завершено)

- [x] `BUILD-890` — назва плитки HB Menu використовує власний `ScrollingText` без доступу до приватного поля `grid::Menu`.
- [x] `VERIFY-890` — звірено помилку компілятора з вихідним кодом і перевірено `git diff --check`; повторна збірка та Switch runtime очікують користувача.
- [x] `DOCS-BUMP-890` — patch version і delivery-документи синхронізовано.
- [x] `COMMIT-890` — focused commit на primary master без ROMFS binary.

## Попередній delivery: v0.13.889 — назви плиток та іконки Backups (завершено)

- [x] `TITLE-889` — назва HB Menu поверх світіння, центрована або прокручувана; Inner Glow послаблено.
- [x] `ICON-889` — локальна іконка має пріоритет; асинхронний fallback за Title ID, перевірений JPEG і SD/session кеш для повторних бекапів.
- [x] `VERIFY-889` — Gemini: цільові Python контракти й diff check PASS; senior: source/diff/file-size review; без збірки й Switch runtime.
- [x] `DOCS-BUMP-889` — patch version і delivery-документи синхронізовано.
- [x] `COMMIT-889` — focused commit на primary master без ROMFS binary.

## Попередній delivery: v0.13.888 — один пункт сітки в макетах Saves (завершено)

- [x] `LAYOUT-888` — «Іконки» прибрано; «Сітка», «HB Menu» і «Список» мають правильні індекси.
- [x] `VERIFY-888` — два цільові Python контракти й diff check пройдено; збірка та Switch runtime окремо.
- [x] `DOCS-BUMP-888` — patch version і delivery-документи синхронізовано.
- [x] `COMMIT-888` — focused commit на primary master без ROMFS binary.

## Попередній delivery: v0.13.887 — циклічна навігація UP у Backups (завершено)

- [x] `NAV-887` — UP/LEFT з першого елемента пропускає службову позицію та вибирає останній.
- [x] `VERIFY-887` — два цільові Python контракти й diff check пройдено; збірка та Switch runtime окремо.
- [x] `DOCS-BUMP-887` — patch version і delivery-документи синхронізовано.
- [x] `COMMIT-887` — focused commit на primary master без ROMFS binary.

## Попередній delivery: v0.13.886 — відновлення без повторного запиту про слот (завершено)

- [x] `RESTORE-886` — прибрано друге підтвердження створення слота після перевірки архіву.
- [x] `VERIFY-886` — два цільові Python контракти й diff check пройдено; збірка та Switch runtime окремо.
- [x] `DOCS-BUMP-886` — patch version і delivery-документи синхронізовано.
- [x] `COMMIT-886` — focused commit на primary master без ROMFS binary.

## Попередній delivery: v0.13.885 — прибрано порожній ряд між джерелами (завершено)

- [x] `GRID-885` — наступне джерело Backups починається в наступному звичайному ряду без рядка-заглушки.
- [x] `SPACING-885` — відступ 34 px в Backups вміщує підпис і не змінює інші вкладки.
- [x] `VERIFY-885` — два Python контракти, diff і ліміт рядків пройдені; збірка й консольний runtime окремо.
- [x] `DOCS-BUMP-885` — patch version і delivery-документи синхронізовано.
- [x] `COMMIT-885` — focused commit на primary master без ROMFS binary.

## Попередній delivery: v0.13.884 — вирівняні підписи секцій Backups (завершено)

- [x] `SPACING-884` — однаковий відступ від підпису кожного джерела до його першої плитки в Grid.
- [x] `LABEL-884` — хмаринка назви гри малюється над лінією секції.
- [x] `VERIFY-884` — два цільові Python контракти, diff, whitespace і ліміт рядків пройдено; збірка й Switch runtime окремо.
- [x] `DOCS-BUMP-884` — patch version і delivery-документи синхронізовано.
- [x] `COMMIT-884` — focused commit на primary master без ROMFS binary.

## Попередній delivery: v0.13.883 — компактний перший розділювач бекапів (завершено)

- [x] `GRID-883` — прибрано порожній перший ряд у плитковому вигляді Backups; підпис джерела лишився під вкладками.
- [x] `VERIFY-883` — контрактний тест секцій пройшов; перевірено diff, кількість рядків і whitespace; збірка та консольний runtime очікують перевірки.
- [x] `DOCS-BUMP-883` — patch version і delivery-документи синхронізовано.
- [x] `COMMIT-883` — focused commit на primary master без ROMFS binary.

## Попередній delivery: v0.13.882 — походження та папкові бекапи (завершено)

- [x] `MAP-882` — знайдено групування за identity, розділювач live/backup, наявний folder-stage/restore і обмеження створення нового слота.
- [x] `SOURCE-882` — достовірне походження, окремі групи/розділювачі DBI, JKSV, Checkpoint, Kefir Hub та інших джерел.
- [x] `FOLDER-882` — безпечна видимість і restore папкових бекапів JKSV/Checkpoint, включно з перевіркою відсутньої цілі.
- [x] `VERIFY-882` — WSL `ReleaseWithInstall` (`sphaira_nro`), цільові контракти, EN/UK parity, diff і розміри; консольний runtime окремо.
- [x] `DOCS-BUMP-882` — patch version і документи.
- [x] `COMMIT-882` — focused primary-master commit без ROMFS binary.

## Попередній delivery: v0.13.881 — відновлення сейву без встановленої гри (завершено)

- [x] `MAP-881` — знайдено NACP-only gate у `PlanAccountSaveCreation`; ZIP уже несе metadata owner/size.
- [x] `RESTORE-881` — безпечне планування слота з валідних метаданих архіву за відсутності NACP.
- [x] `VERIFY-881` — Gemini: Python contract, EN/UK parity, whitespace; senior: diff/межі/CMake/розміри; консольний runtime окремо.
- [x] `DOCS-BUMP-881` — patch version і delivery-документи.
- [x] `COMMIT-881` — focused commit на primary master без ROMFS binary.

## Попередній delivery: v0.13.880 — DBI-бекапи в каталозі (завершено)

- [x] `MAP-880` — перевірено реальні DBI ZIP на SD, сканер, інспектор архівів і втрату `payload_count`.
- [x] `FIX-880` — збережено кількість payload під час копіювання розібраних метаданих; metadata-only і invalid лишаються відхиленими.
- [x] `VERIFY-880` — Gemini: `git diff --check`; senior: diff, source flow, file-size та primary-master review; без збірки й device test.
- [x] `DOCS-BUMP-880` — версію та delivery-документи синхронізовано.
- [x] `COMMIT-880` — focused primary-master commit без push; ROMFS binary виключено.

## Попередній delivery: v0.13.879 — remaining file-size audit (завершено)

- [x] `SPLIT-879` — 25 remaining oversized first-party targets розділено на когезивні units; 53 нові файли.
- [x] `SIZE-879` — усі 620 перевірених code/build/test файлів у `sphaira`, `hbl`, `tests` ≤600 рядків.
- [x] `VERIFY-879` — Gemini: WSL `tests/run.sh` all green, save-restore C++ 40/40, dead-symbol 1033/1033, `ReleaseWithInstall` і whitespace PASS; senior: diff/CMake/size/source review, без повторної компіляції.
- [x] `DOCS-BUMP-879` — версію й delivery-документи синхронізовано.
- [x] `COMMIT-879` — focused primary-master commit без push; ROMFS binary виключено.

## Попередній delivery: v0.13.878 — File-size audit closure (завершено)

- [x] `MAP-YATI-878` — graph/source/callers перевірені; analysis/planning seam визначено.
- [x] `SPLIT-ANALYSIS-878` — NCZ analysis і вибір носія винесені; senior перевірив diff, compile ще попереду.
- [x] `SPLIT-TYPES-878` — спільні приватні типи інсталятора винесені в `yati_internal.hpp` (387 рядків); compile ще попереду.
- [x] `SPLIT-PIPELINE-878` — worker read/decompress/write винесені в 512-рядковий unit; compile ще попереду.
- [x] `SPLIT-INSTALL-878` — ticket/CNMT і threaded pipeline розділені; senior перевірив структуру, compile ще попереду.
- [x] `SPLIT-PROVIDER-878` — 7 provider-heavy targets розділені на units ≤590; Gemini WSL build PASS, senior structure/CMake review PASS.
- [x] `SPLIT-UI-878` — 32 oversized menu files розділені; всі 152 menu source/header файли ≤600.
- [x] `TEST-RUNNER-878` — 18 Python contracts discoverable в existing runner.
- [x] `SPLIT-TESTS-878` — 11 oversized suites розділені за сценаріями без вилучення safety cases.
- [x] `APP-HEADER-878` — dead declarations прибрані; `GetAccountList` перенесено без зміни логіки.
- [x] `SIZE-878` — 150 змінених/нових source/test файлів ≤600; legacy CMake та 22 інші oversized файли поза цим delivery.
- [x] `VERIFY-878` — Gemini: WSL build і `tests/run.sh` PASS; senior: 18/18 Python, dead-symbol 1033/1033, CMake/розміри/whitespace PASS. C++ test після точкового звуження source boundary не перезапускався.
- [x] `DOCS-BUMP-878` — version/docs синхронізовано.
- [x] `COMMIT-878` — focused primary-master commit без push.

## Попередній delivery: v0.13.877 — Transfer structural split (завершено)

- [x] `MAP-TRANSFER-877` — transfer core і archive safety boundary підтверджені graph + source review.
- [x] `SPLIT-CORE-877` — transfer engine/wrappers винесені в 499-рядковий unit; фінальна збірка ще попереду.
- [x] `SPLIT-ARCHIVE-877` — ZIP/path/preflight/verification розкладені за стабільними межами; збірка ще попереду.
- [x] `SIZE-877` — нові C/C++ files ≤501 рядка, `threaded_file_transfer.cpp` зменшено до 263 рядків.
- [x] `VERIFY-877` — Gemini: WSL `sphaira_nro`, 18/18 Python contracts, dead-symbol 981 і whitespace PASS.
- [x] `DOCS-BUMP-877` — version/docs синхронізовано.
- [x] `COMMIT-877` — focused primary-master commit без push.

## Попередній delivery: v0.13.876 — виконано

- [x] `MAP-APP-876` — lifecycle/state/callers підтверджені graph + source review.
- [x] `DEAD-DECL-876` — кандидати в `app.hpp` перевірені; layout/ініціалізаційні ефекти залишені для окремого delivery.
- [x] `SPLIT-APP-876` — App/settings responsibilities винесені без навмисної зміни поведінки.
- [x] `API-CMAKE-876` — public API стабільний; нові units явно зареєстровані.
- [x] `SIZE-876` — усі нові App files ≤597 рядків; `app.cpp` 523, `app_settings.cpp` 554.
- [x] `VERIFY-876` — Gemini: WSL NRO build, 18/18 contracts, dead-symbol 981, EN/UK 2549/2549 і whitespace PASS.
- [x] `DOCS-BUMP-876` — version/docs синхронізовано.
- [x] `COMMIT-876` — focused primary-master commit без push.

## Подальша черга

Ручний device/runtime тест save/restore та MTP; structural size queue для перевіреної області закрита.

Історія завершених checklist-ів доступна через Git і тут не дублюється.
