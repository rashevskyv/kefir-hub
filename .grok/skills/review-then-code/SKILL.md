---
name: review-then-code
description: >
  grok-4.6 investigates and reviews; grok-4.5 writes product code.
  Use when the parent is grok-4.6 and the task needs a code patch, implement,
  fix, or delivery commit. Slash: /review-then-code
---

# review-then-code

If the running parent is grok-4.6 (or the user asked 4.6 to review and 4.5 to code):

1. **4.6 does not write product code** (`sphaira/source`, `sphaira/include`, `assets/romfs` except docs-only). It may edit `Agents.md`, skills, `plan.md`/`task.md` only as review notes until 4.5 lands the patch.
2. 4.6 gathers evidence, writes a concrete patch spec (files, functions, invariants, what not to do).
3. Spawn `subagent_type=general-purpose` with `model=grok-4.5`, `isolation=none`, `background=true`. No git worktree.
4. Spec must include: primary checkout `D:\git\dev\sphaira`; do not compile; bump patch + plan/task/walkthrough/audit + commit when the change ships.
5. When 4.5 returns, 4.6 reads the diff, rejects drive-by changes, asks 4.5 to fix if wrong, then the 4.6 side confirms bump/commit happened.

Do not spawn 4.5 for questions-only turns.
