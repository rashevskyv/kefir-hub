---
name: test-build
description: >
  Test building Sphaira / Kefir Hub via WSL, fix compilation/linker errors iteratively,
  bump version in sphaira/CMakeLists.txt, update plan/task/walkthrough/audit, and commit fixes.
  Triggers: /test-build, /протестуй-збірку, "Протестуй збірку", "протестуй збірку",
  "test build", "перевір збірку", "виправ помилки збірки".
---

# Test Build («Протестуй збірку»)

Workflow for testing project compilation, diagnosing build/link errors, applying surgical fixes, and shipping a clean build commit with a version bump.

> **Note on Workspace Policy**: While `AGENTS.md` forbids routine compilation during normal editing turns to conserve resources, this skill is the dedicated procedure specifically authorized to compile, verify, and resolve build errors.

## Build Environment & Requirements

- **Environment**: WSL (Windows Subsystem for Linux) with devkitPro installed at `/opt/devkitpro`.
- **Primary Checkout**: `D:\git\dev\sphaira` (mapped to `/mnt/d/git/dev/sphaira` in WSL). Do not use worktrees.
- **Preset**: `ReleaseWithInstall` (or `Release` for non-network builds).

## Procedure

### 1. Run Compilation

Execute the build via WSL:

```bash
wsl bash -lc 'cd /mnt/d/git/dev/sphaira && cmake --preset ReleaseWithInstall && cmake --build --preset ReleaseWithInstall --parallel $(nproc)'
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

### 5. Bump Version, Update Docs & Commit

Once all compilation errors are resolved:

1. **Bump Version**:
   - Increment patch version in `sphaira/CMakeLists.txt`: `set(sphaira_VERSION 0.13.X)`.

2. **Update Plan & Tracking Files** (in Ukrainian):
   - `plan.md`: Add new «Поточний delivery: v0.13.X» at the top; previous becomes «Попередній».
   - `task.md`: Add checkboxes for the build fixes, version bump, and docs bump. Mark completed.
   - `walkthrough.md`: Add entry detailing which compilation errors were fixed and verification status.
   - `audit.md`: Update version header and notes.

3. **Commit**:
   - Stage modified source files, `sphaira/CMakeLists.txt`, `plan.md`, `task.md`, `walkthrough.md`, `audit.md`, and skill files.
   - Commit message format: `v0.13.X: fix compilation errors and verify build`.
   - Do not `git push` unless explicitly asked by the user.
