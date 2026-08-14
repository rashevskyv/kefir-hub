# PFS0/NSP parser hardening audit

Дата аудиту: 2026-08-14. Baseline: `efd2edf` (`v0.13.452`). Джерело уроків —
лише parser-related частина upstream `400c5140cdfdb57de5f2a479eb5743233b22b88a`;
MSP installer, manifest/staging/rollback, Atmosphère payload, UI, MTP/web і i18n
виключені зі scope.

## Простежений шлях даних

`InstallFromFile()` створює `yati::source::File`, а `InstallFromSource()` за
розширенням `.nsp`/`.nsz` створює `container::Nsp` і викликає
`Nsp::GetCollections()`. Той самий парсер використовується під час
`AnalyzeSource()` для USB-черги, stream installer (`ui::menu::stream::Stream`),
web direct-install (`SocketStream`) і випадково-доступного USB reader. Після
цього колекції доходять до `InstallFromCollections()`.

Окремий caller у `utils/devoptab_nca.cpp` застосовує `Nsp::GetCollections(out,
off)` до PFS0 всередині NCA. Його `NcaReader` already clips reads to its capacity
and rejects short underlying reads, але сам PFS0 parser досі не перевіряє його
metadata. Отже, коректна точка виправлення — спільний `Nsp::GetCollections()`,
не окремі guards у installers або NCA mount.

## Підтверджені проблеми

| Ділянка | Доказ у baseline | Наслідок | Мінімальне root-cause виправлення |
|---|---|---|---|
| Exact read | `nsp.cpp` приймає успішній `Source::Read()` без порівняння `bytes_read` для PFS0 header, file table і string table. `Stream::Read()` може законно повернути short read на EOF. | Частково заповнений нулями metadata buffer може пройти до allocation/parse. | Локальний `ReadExact()` у PFS0 parser: після успішного `Read()` вимагати рівно запитаний розмір. |
| Untrusted allocation | `header.total_files` одразу керує `vector<Pfs0FileTableEntry>`, а `header.string_table_size` — `vector<char>`; cap немає. | Малий malformed input може вимагати багатогігабайтні allocation. | До allocation обмежити file count і string-table size консервативними parser limits. |
| Header/table arithmetic | `off += bytes_read`, `file_table.size() * sizeof(...)` і наступні offsets не мають checked arithmetic; `off` є `s64`, а header fields/data fields — unsigned. | Wrap або signed conversion може створити некоректний read/seek або негативний `CollectionEntry`. | Обчислювати table/string/data/file ends у `u64` checked-add, відхиляти значення поза `s64` до будь-якого seek/read. |
| Name validation | `string_table.data() + file_table[i].name_offset` не перевіряє offset і покладається на неявний NUL для `std::string`. | OOB pointer/read при name offset поза table або відсутньому NUL. | Вимагати `name_offset < string_table.size()` і NUL у залишку таблиці через bounded `memchr`. |
| Declared entry bounds | `data_offset + file.data_offset` і `+ file.data_size` не перевіряються. Із known source/container size parser також не порівнює declared end з ним. | Wrap або запис за фактичним container end може дійти до later read/stream skip. | Для кожного entry перевірити checked start/end та, коли reader має відомий size, end ≤ container end; невідомі streams все одно отримують checked arithmetic та exact metadata reads. |

## Уже наявний захист — не дублювати

- `source::Stream::Read()` уже накопичує часткові chunks і повертає short тільки
  на EOF; це не замінює parser-level exact read, але його не потрібно змінювати.
- `nca::NcaReader::ReadInternal()` already clips to `m_capacity` і вимагає exact
  underlying read. PFS0 validation має лишитися спільною в `Nsp`, а не
  дублюватися в NCA caller.
- `AnalyzeSource()` already rejects negative/overflowed published collection
  offsets/sizes and uses checked aggregate sizing. Це downstream defence, не
  заміна безпечного parsing metadata.

## Вузький implementation contract для Gemini

1. Не змінювати MSP/product/i18n/UI/transport code і не торкатися
   `.codex-tmp/usb-re/`.
2. Посилити лише спільний PFS0/NSP parser і найближчу наявну size abstraction,
   якщо вона потрібна, щоб передати known file/container bounds у parser; не
   додавати caller-specific guards.
3. Повертати чинний доречний container/parser Result для malformed metadata;
   не вводити translation work лише заради нового UI text.
4. Додати найменший host-negative test, який доводить rejection short header,
   oversized table/string allocation, invalid/missing-NUL name і overflowing
   file offset/size (а також known-size overrun, якщо size передається).
5. Зберегти valid PFS0/NSP behavior. Після non-trivial implementation підняти
   версію за чинною послідовністю `0.13.452 → 0.13.453` у
   `sphaira/CMakeLists.txt`.

## Перевірка після реалізації

- Новий boundary test і чинний `tests/run.sh`.
- `wsl bash -l -c "cd /mnt/d/git/dev/sphaira && cmake --build --preset ReleaseWithInstall --parallel 16"`.
- `git diff --check`.
- Hardware/manual smoke test не очікується: зміна відхиляє malformed metadata
  до будь-якого OOB read/seek/allocation і не змінює UI або transport protocol.
