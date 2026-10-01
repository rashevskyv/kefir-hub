# Developer Guide

Where to start when changing Kefir Hub.

---

## 1. Read first

- [AGENTS.md](../../AGENTS.md) — project rules: scope, file size cap, delivery ritual, build policy.
- [ARCHITECTURE.md](../dev/ARCHITECTURE.md) — directory map, the `Menu` structs, thread map, god nodes, i18n.
- [CHANGELOG.md](../dev/CHANGELOG.md) — what shipped in each version and how it was verified.
- [plan.md](../../plan.md) — the current work queue.

---

## 2. Build the NRO

Requires devkitPro (see the README's *Building from source*). In WSL:

```sh
cmake --preset ReleaseWithInstall
cmake --build --preset ReleaseWithInstall --parallel $(nproc)
```

The build-and-fix procedure for agents is the `test-build` skill: [.claude/skills/test-build/SKILL.md](../../.claude/skills/test-build/SKILL.md).

---

## 3. Host tests

No Switch and no devkitPro needed:

```sh
tests/run.sh
```

It compiles and runs every `tests/test_*.cpp` against the real headers in `sphaira/include`, then runs the
dead-symbol guard, the libhaze/ftpsrv patch checks (these need `cmake`) and the Python contracts.
One test on its own:

```sh
g++ -std=c++20 -Wall -Wextra -Werror -I sphaira/include tests/test_path_util.cpp -o /tmp/t && /tmp/t
```

New pure logic goes into a libnx-free header (or a libnx-free `.cpp` listed with `// LINK:` in the test) so it can get
a host test. Device behaviour is checked with [HARDWARE-CHECKLIST.md](../dev/HARDWARE-CHECKLIST.md).

---

## 4. Translations and catalogs

- Interface strings: `assets/romfs/i18n/*.json`, tooling in `tools/i18n-translate/` ([README](../../tools/i18n-translate/README.md)).
- Sysmodule catalog: `tools/module_catalog/` ([README](../../tools/module_catalog/README.md)).
