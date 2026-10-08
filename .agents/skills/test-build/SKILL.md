---
name: test-build
description: >
  Test building Sphaira / Kefir Hub via WSL, fix compilation/linker errors iteratively,
  bump version in sphaira/CMakeLists.txt, add a CHANGELOG entry, tick plan.md, and commit fixes.
  Triggers: /test-build, /протестуй-збірку, "Протестуй збірку", "протестуй збірку",
  "test build", "перевір збірку", "виправ помилки збірки".
---

# Test Build («Протестуй збірку»)

Workflow for testing project compilation, diagnosing build/link errors, applying surgical fixes, and shipping a clean build commit with a version bump.

> **Note on Workspace Policy**: `AGENTS.md` forbids NRO compilation during normal edit turns; this skill is the build checkpoint that is authorized to compile, verify and resolve build errors. Host tests (`tests/run.sh`) are separate and always allowed.

## Build Environment & Requirements

- **Environment**: WSL (Windows Subsystem for Linux) with devkitPro installed at `/opt/devkitpro`.
- **Primary Checkout**: `D:\git\dev\kefir-hub` (mapped to `/mnt/d/git/dev/kefir-hub` in WSL). Do not use worktrees.
- **Preset**: `ReleaseWithInstall` (the shipped one; `Release` without network install does not link since v0.13.960).

## Procedure

### 1. Run Compilation

Execute the build via WSL:

```bash
wsl bash -lc 'cd /mnt/d/git/dev/kefir-hub && cmake --preset ReleaseWithInstall && cmake --build --preset ReleaseWithInstall --parallel $(nproc)'
```

Alternatively run `./build.sh`.

### 2. Inspect Output & Diagnose Errors

- If compilation exits with code 0 (`[100%] Built target sphaira_nro`), the build is clean.
- If errors occur (`error:`, undefined references, invalid types, macro mismatches):
  - Identify the exact files and lines reported by the compiler (`g++` / `clang`).
  - Read surrounding code to understand intended types and invariants.
  - Pay special attention to libnx types (`Result`, `R_SUCCEED()`, `R_FAILED()`, `R_TRY()`, `FsSaveDataSpaceId`, etc.).

### 3. Apply Surgical Fixes

- Modify only the files and lines necessary to resolve the build errors.
- Never remove features or refactor unrelated code.
- Ensure all symbols and types match libnx and project definitions.

### 4. Repeat Build Until Clean

Re-run the build command from Step 1. Repeat Steps 2 and 3 until the build completes with code 0 and `sphaira_nro` is built.

### 5. Bump Version, Changelog & Commit

Once all compilation errors are resolved (follow the delivery ritual in `AGENTS.md`):

1. **Bump Version**: increment patch in `sphaira/CMakeLists.txt`: `set(sphaira_VERSION 0.13.X)`.
2. **Changelog**: prepend to `docs/dev/CHANGELOG.md`:
   `## v0.13.X — build fix` + 1–2 lines: which compile/link errors were fixed, and
   `host tests: pass|not run · nro: built · switch: pending`.
3. **plan.md**: tick the build-checkpoint checkbox of the current phase. Add nothing else.
4. **Commit**: stage the fixed sources, `sphaira/CMakeLists.txt`, `docs/dev/CHANGELOG.md`, `plan.md`.
   Message: `v0.13.X: fix compilation errors and verify build`. Do not `git push`.
