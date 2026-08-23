# Workspace policy

- Work exclusively in the primary checkout: `D:\git\dev\sphaira`.
- Do not create, select, or operate in a Git worktree for this project.
- Do not run `git worktree` commands or delegate work to a separate worktree.
- Before making a change, verify that the active repository is the primary checkout above. If it is not, stop and ask the user rather than editing another checkout.
- Do not compile. No `cmake --build`, no NRO/WSL ReleaseWithInstall, no `g++` of tests, no `tests/run.sh`. After a code change, tell the user to compile.

## After a completed change (mandatory)

When a user-requested product change is finished — code, i18n, or tests that should ship — do **all** of the following in the same turn. Do not wait to be asked. Do not leave finished work unversioned.

1. **Bump the app version.** Increment the patch in `sphaira/CMakeLists.txt` (`set(sphaira_VERSION 0.13.X)`). This is the only version source. Never ship a completed change on the previous number.
2. **Update the plan files** to that new version, in the same turn:
   - `plan.md` — new «Поточний delivery»; the previous block becomes «Попередній».
   - `task.md` — matching checkboxes, including a `DOCS-BUMP-X` item.
   - `walkthrough.md` — what actually shipped.
   - `audit.md` — version line at the top; mark queue items done if this delivery completed them.
   Do not claim tests or an NRO build were run unless they were (this agent does not compile).
3. **Commit** on the primary checkout. Subject: `v0.13.X: <short description of what shipped>`. Stage the code, i18n, tests, and the plan files for this delivery. Do not `git push` unless the user asks.

Skip bump + commit only for unanswered questions, investigation-only turns, or edits that do not ship product code. If the turn both ships product code and updates this file, bump and commit together.

## Graphify

Canonical graph: repo-root `graphify-out/` (not `sphaira/graphify-out/`).

Before grepping or reading files to understand how the code fits together, query the graph:

```
graphify query "<question>"
graphify path "A" "B"
graphify explain "X"
```

Then fall back to grep / file reads for the exact lines to edit.

Refresh at the start of a dialog, and whenever the graph is stale, before relying on it. Prefer `/graphify --update`. `graphify update sphaira` writes a second tree under `sphaira/graphify-out/` — do not do that; keep the canonical graph at the repo root.

Work queue lives in `audit.md`. Graphify is the map; `audit.md` is the next cut.
