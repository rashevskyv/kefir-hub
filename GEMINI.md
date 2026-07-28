# Project instructions

## Source-code research: use Graphify FIRST

This repo ships a persistent Graphify knowledge graph in `graphify-out/`
(graph root: `sphaira/`). **Before** grepping or reading files to understand
how the code fits together, query the graph — it maps the relationships that a
plain text search cannot.

Workflow:

1. Start every investigation with a query:
   ```
   graphify query "<your question>"
   ```
   Then use `graphify path "A" "B"`, `graphify explain "X"`, and
   `graphify affected "X"` to trace dependencies and impact.
2. Only after the graph has pointed you at the right nodes, fall back to
   grep / reading files for the exact lines to edit.

Artifacts: `graphify-out/GRAPH_REPORT.md` (overview), `graphify-out/graph.html`
(interactive), `graphify-out/graph.json` (raw, used by the CLI).

## Keep the graph fresh

The graph goes stale as code changes, so an outdated graph can mislead.

- **Update the graph at the start of each new dialog**, and again any time you
  notice it is out of date, before relying on it.
- Preferred refresh: the `/graphify` skill (updates the canonical
  `graphify-out/` in place; no LLM needed for a code re-extract).
- CLI caveat: the canonical graph lives in the **repo-root** `graphify-out/`.
  The current `graphify` package (0.9.6) `update <path>` writes to
  `<path>/graphify-out`, i.e. running `graphify update sphaira` from the repo
  root creates a *second* `sphaira/graphify-out/` instead of refreshing the
  canonical one. If you use the CLI, point queries at the graph you actually
  rebuilt, or copy the rebuilt `graph.json` back into repo-root `graphify-out/`.
  Add `--force` after refactors that delete code (a rebuild with fewer nodes is
  otherwise refused).

## Notes

- The Graphify skill is also available in-session via `/graphify`.
- If the CLI warns the installed skill is older than the package, run
  `graphify install` to sync it.
