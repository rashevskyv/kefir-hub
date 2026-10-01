# Kefir Hub (sphaira fork) — agent rules

Single source of truth for any AI agent working here. Keep this file under 100 lines.
Reports to the user are in Ukrainian; code, commits and this file are in English.

## Workspace
- Primary checkout: `D:\git\dev\sphaira` (WSL: `/mnt/d/git/dev/sphaira`). No git worktrees, ever.
- Never `git push` unless the user asks. Commit on Windows (Git Bash / PowerShell), not from a Linux VM mount
  (a VM sees CRLF noise in hundreds of files — do not stage it).
- `assets/romfs/tegra/TegraExplorer.bin` changes in the background (Kefir updates). Ignore it when checking
  tree cleanliness; never stage it with unrelated work.
- Do not touch `libs/` or upstream patch files unless the task names them.

## Session start (≈3k tokens, not 60k)
1. `git log --oneline -5` and `git status --short`.
2. `plan.md` — read the **current phase only**; it is the work queue.
3. `head -20 docs/dev/CHANGELOG.md` — what shipped recently.
4. Graph: `graphify update .` if source is newer than `graphify-out/GRAPH_REPORT.md`, else skip.
Do **not** read `README.md`, `docs/wiki/`, `docs/dev/AUDIT-*.md` or the whole `GRAPH_REPORT.md` unless the task needs them.

## Context discipline
- Before reading code: `graphify explain "<symbol>"`, `graphify path "A" "B"`. Then open only the files it names,
  with line ranges. Never `cat` a file over 300 lines whole.
- `GRAPH_REPORT.md`: read only `## Summary` and `## God Nodes` (first ~60 lines + the god-node block).
- 19 structs are named `Menu` in different namespaces (table: `docs/dev/ARCHITECTURE.md`). Always qualify: `<path>::Menu` in graphify, `-l` + path in grep.
- God nodes (`App` 841 edges, `Result`, `log_write`, `Fs`): a change there has blast radius — say so, keep it minimal.
- Do not add new static/global state to `App`. New logic goes into free functions in the owning module.
- Module routing: `docs/dev/ARCHITECTURE.md` (directories, Menu table, thread map). Quick list: Save UI `source/ui/menus/save*`,
  Web `source/web*`, App lifecycle `source/app*.cpp`, Transfers `source/threaded_file_transfer*`,
  Installer `source/yati/`, MTP/USB `source/haze*`, `source/app_usb.cpp`, `source/utils/devoptab_mtp*`.

## Build and test policy
- **No NRO build during normal edit turns.** Work in batches: several commits, then one build checkpoint.
- Build checkpoint = run the `test-build` skill (`.claude/skills/test-build/SKILL.md`) in WSL. Mandatory at the
  end of every plan phase and after at most 5 unbuilt commits. Fix errors surgically, bump, commit.
- Host tests are cheap and **required** after touching pure logic, headers, tests or patches:
  `wsl bash -lc 'cd /mnt/d/git/dev/sphaira && tests/run.sh'` (≈1 min; no devkitPro needed).
- Never claim a build or a test ran unless you ran it in this session. Hardware (Switch) verification is the
  user's; list what needs it in the CHANGELOG entry.

## Code rules
- Files ≤ 600 lines (first-party code, build, tests). Not applied to i18n JSON, tables, embedded HTML.
- Surgical scope: only task-related code. No interfaces with one implementation, no registries, no speculative config,
  no behavior change mixed with a file split.
- Cross-thread flags are `std::atomic` or guarded by the module mutex — never plain `bool`/`u64` globals.
- No `strcpy`/`strcat`/`sprintf` into fixed buffers from variable input; use `snprintf`/`std::string`.
- New pure logic goes into a header (or a libnx-free `.cpp`) so it can get a host test in `tests/`.
- Tests must execute real code (`tests/test_*.cpp` including a project header) or validate real data files.
  Regex assertions on C++ source text are not tests — do not write them.

## Delivery ritual (every commit that ships product code, i18n or tests)
1. Bump patch in `sphaira/CMakeLists.txt`: `set(sphaira_VERSION 0.13.X)` — the only version source.
2. Prepend 1–3 lines to `docs/dev/CHANGELOG.md`: `## v0.13.X — <what>` + what changed + verification state
   (`host tests: pass|not run`, `nro: built|not built`, `switch: verified|pending`).
3. Tick the task's checkbox in `plan.md`; add nothing else there.
4. Commit: `v0.13.X: <short description>`. Stage code, i18n, tests, CMakeLists, CHANGELOG, plan.md.
Docs-only or tooling-only commits: no bump, prefix `docs:` / `chore:`, still note in CHANGELOG under `## unreleased`.

## Graphify
Canonical graph: repo-root `graphify-out/` only. Never create `sphaira/graphify-out/`.
Run `graphify update .` after structural changes (new/moved files, new functions, changed includes);
skip after body-only edits, comments, version bumps.
