# Audit Plan — Web Sharing / Direct Install subsystem

This is a plain code-quality audit of a self-written homebrew app (not a security
review). It covers the recently added web server and direct-install feature
(commits `16881b1`..`9af3d5f`). The goal is to find real bugs, crashes/hangs, and
architectural rough edges, then hand them to Agent 1 to fix one at a time.

Grounding: a fresh graphify knowledge graph of the whole `sphaira/` tree
(`graphify-out/`) confirms `web.cpp` is a fairly isolated subsystem — it hangs
off direct calls into `Fs` / `yati` / `ProgressBox` rather than being integrated
into the menu architecture. Core abstractions it leans on: `ProgressBox`
(community 18), yati install threading (19), `FsNative` API (9), path utils (13),
`FsView` (22, the caller of `ShareFolder`).

Primary files:
- `sphaira/source/web.cpp`
- `sphaira/source/yati/yati.cpp` (`InstallFromCollections` dynamic override)
- `sphaira/source/yati/source/stream.cpp` (`Stream::Read` skip cap)
- `sphaira/source/ui/menus/filebrowser.cpp` (`FsView::ShareFolder`)
- `sphaira/source/ui/progress_box.cpp` / `include/ui/progress_box.hpp` (`Mute`)
- `sphaira/source/ui/menus/menu_base.cpp` (battery bolt)

Findings are ordered by severity. Each item has **Problem / Where / Fix /
Acceptance criteria**. Work top-down; one logical fix per commit, and update
`audit.md` in the same commit.

---

## P0 — Correctness / functional regressions

### P0-1. [RESOLVED] "Share Images" and "Gallery" are broken; two page builders are dead code
**Problem.** In `HandleRequest` the routes `/`, `/files`, `/images`, `/gallery`
all return `BuildFolderPage(...)` (web.cpp:1833-1836). Consequences:
- `WebShareImages()` advertises URL `.../images`, but that URL now renders the
  full file browser instead of the image grid.
- `WebShareImages()` stores the selected images in `g_share_entries` and clears
  `g_share_folder_root` to `{}`, so `BuildFolderPage` falls back to
  `CanonicalizeAbsolutePath("")` == `/` and lists the SD root, ignoring the
  selected images entirely.
- `BuildImagesPage()` (web.cpp:391), `BuildGalleryPage()` (web.cpp:1292),
  `SendImage()` and the `/image/N` route are never reached — dead code.

**Where.** web.cpp:1833-1836, 391, 1292, 1124 (`SendImage`), 2323 (`WebShareImages`).

**Fix (pick one, record the choice in walkthrough):**
- Option A (keep images): route `/images` → `BuildImagesPage()` and
  `/gallery` → `BuildGalleryPage(path)`; leave `/` and `/files` on
  `BuildFolderPage`. Verify the images-share flow renders the grid.
- Option B (drop images): remove `BuildImagesPage`, `BuildGalleryPage`,
  `SendImage`, the `/image/` route, `WebShareImages`, `ShareMode::Images` and
  the now-unused `g_share_entries`, plus any UI entry point that calls them.

**Acceptance criteria.** No dead functions remain. If kept: sharing images opens
a working grid whose thumbnails and `/image/N` load. If dropped: compiles with no
unreferenced-function warnings and no UI path invokes the removed feature.

---

### P0-2. [RESOLVED] Upload / stream / send loops can hang forever → app freeze on Stop
**Problem.** Every socket read/write loop retries `EWOULDBLOCK`/`EAGAIN` with a
1 ms sleep and no timeout and no cancellation check:
- `SocketStream::ReadChunk` (web.cpp:1453-1467)
- `ReceiveUpload` body loop (web.cpp:1617-1639)
- `SendAll` (web.cpp:326-340)

If a client stalls or half-closes (Wi-Fi drop, not a clean FIN), `recv`/`send`
keeps returning `EWOULDBLOCK` and the server thread spins forever. Because
`WebShareStop()` (web.cpp:2390) does `threadWaitForExit(&g_share_thread)`, and
`FsView::ShareFolder`'s ProgressBox loop calls `WebShareStop()` on exit
(filebrowser.cpp:1216), a stuck server thread makes Stop/cancel hang the whole
UI. During a direct install the install runs on the server thread, so
cancel-during-install becomes unrecoverable too.

**Where.** web.cpp:326-340, 1453-1467, 1617-1639. Note `ReadHttpRequest`
(web.cpp:366) already self-bounds at 5000 attempts — mirror that.

**Fix.** Give all three loops a bounded idle timeout: count consecutive
`EWOULDBLOCK` sleeps and bail after a sane wall-clock limit (e.g. 10–30 s of no
progress), returning an error so the request tears down and the thread returns.
In `SocketStream::ReadChunk` also poll `WebGetProgressBox()->ShouldExit()` so a
user Stop breaks promptly. Simpler alternative: set `SO_RCVTIMEO`/`SO_SNDTIMEO`
on the accepted client socket instead of the manual nonblocking spin.

**Acceptance criteria.** Dropping the connection mid-upload, or a client that
connects and never sends a body, no longer wedges the server; pressing B (Stop)
during a transfer returns to the UI within a few seconds.

---

## P1 — Robustness / concurrency

### P1-3. `g_web_pbox` is a non-atomic raw pointer shared across threads
**Problem.** `WebSetProgressBox`/`WebGetProgressBox` (web.cpp:2421-2429) store a
plain `ui::ProgressBox*` written by the ProgressBox thread and read by the server
thread — a data race (non-atomic). `ProgressBox::Mute` reads/writes `m_muted`
with no mutex (progress_box.hpp:39) while `Draw`/`Set*` run on the render thread.
Today the lifetime happens to be safe only because `WebShareStop`'s join blocks
the ProgressBox thread until the server thread ends — but P0-2 shows that join
can hang, and the pointer/`m_muted` races are still UB.

**Where.** web.cpp:2421-2429; progress_box.hpp:39; progress_box.cpp (all
`if (m_muted)` checks).

**Fix.** Make the global `std::atomic<ui::ProgressBox*>`; make `m_muted`
`std::atomic<bool>` (or guard with the existing `m_mutex`). Server thread should
read the pointer once into a local and null-check before use.

**Acceptance criteria.** No non-atomic cross-thread access to the pbox pointer or
`m_muted`; direct install still drives the progress UI correctly.

### P1-4. Recursive directory scan on a 32 KB server-thread stack
**Problem.** `ScanDirectoryRecursive` (web.cpp:1699) recurses once per directory
level, each frame holding a `fs::Dir` plus a `std::vector<FsDirectoryEntry>`. The
share thread stack is only `1024 * 32` bytes (web.cpp:1928). A deeply nested tree
can overflow the stack and crash the app.

**Where.** web.cpp:1699-1717, 1928.

**Fix.** Convert to an iterative work-queue traversal and/or raise the share
thread stack size; add a depth cap.

**Acceptance criteria.** `/list-recursive` on a deeply nested tree (100+ levels)
does not crash.

### P1-5. Recursive delete is not logged (optional / low priority)
**Problem.** Full-card browsing and deletion from the web UI is intentional — this
is a file manager, not a confined share. So deleting anywhere is by design. The
only real gap is observability: `HandleDelete` (web.cpp:1645) runs
`DeleteDirectoryRecursively` with no `log_write`, so an accidental large delete
from a phone leaves no trace on the device side.

**Where.** web.cpp:1645-1675.

**Fix (optional).** Add a `log_write` line recording the deleted path (and whether
it was a recursive dir delete). No behavior change, no read-only mode — just a log
entry so the action is traceable. Skip if Agent 1 judges it noise.

**Acceptance criteria.** Deletions appear in the log; behavior is otherwise
unchanged.

---

## P2 — Design / maintainability

### P2-6. The NAND-first auto-target heuristic is duplicated in two places
**The strategy is fine and intended** — try NAND first, fall back to SD so at
least ~500 MB stays free on NAND. The issue is purely that the *same* heuristic
is implemented twice, and the two copies already disagree:

- `ReceiveUpload` (web.cpp:1523-1537) estimates compressed size by the **file
  extension** `.nsz/.xcz` (×1.6).
- `InstallFromCollections` (yati.cpp:1561+) estimates it per-entry by the
  **`.ncz` content name** (×1.6).

Both hardcode the `500 MB` reserve and the `1.6` factor separately. Because
`ReceiveUpload` sets `override.sd_card_install` explicitly, the yati copy is
skipped for web installs — so the two paths can pick *different* targets for the
same file depending on which entry point runs, and a future change to one copy
silently diverges from the other.

**Fix.** Extract the heuristic into a single helper in the yati layer
(`ChooseInstallTarget(total_size, is_compressed) -> bool sd_card_install`) with
one `500 MB` constant and one `1.6` factor. Have the web path pass only the raw
size + a compression flag and call that helper, instead of re-deriving the
decision itself. Pick one consistent compression signal (extension vs content
name) and use it in both.

**Acceptance criteria.** Exactly one place computes the NAND/SD decision; web and
container installs of the same file choose the same target.

### P2-6b. Add an install-target strategy setting (feature note, not a bug)
Per the author's intent, the automatic NAND-first-with-500 MB-reserve behavior
should later become configurable in Settings — e.g. `Auto (NAND first)`,
`Always SD`, `Always NAND`. Not part of this audit's fixes; recorded here so it
isn't lost. When added, it should feed the single helper from P2-6 (the setting
selects the strategy; the helper stays the one place that applies it).

### P2-7. Single-connection blocking server
`ShareThreadFunc` (web.cpp:1872) accepts and fully handles one connection at a
time. During a multi-minute upload/install, every other request (thumbnails,
navigation) is blocked. Acceptable for now; document the limitation. A future
improvement is a small worker pool. No code change required this round unless
trivial.

### P2-8. ~30 KB of HTML/JS rebuilt per request
`BuildFolderPage` concatenates a huge CSS+JS blob into a `std::string` on every
request (web.cpp:539-1122) with hundreds of `body +=` calls. Move the static
CSS/JS into `constexpr std::string_view` blobs and only interpolate the dynamic
listing. Cuts per-request CPU/allocations and greatly improves readability.

### P2-9. Minor cleanups
- Double semicolon `;;` at web.cpp:1113.
- Magic numbers: `min_free_nand` (500 MB), `1.6` compression factor, port range,
  `HTTP_READ_LIMIT` — hoist to named constants.
- `ReadHttpRequest` silently drops requests whose header block exceeds
  `HTTP_READ_LIMIT` (4096). Deep `path=` values can be legitimately long — raise
  the limit or return a clear error instead of a truncated parse.

---

## Suggested execution order for Agent 1
1. P0-1 (decide images kept/dropped) — unblocks the rest of the web.cpp cleanup.
2. P0-2 (loop timeouts) — highest crash/hang risk.
3. P1-3, P1-4, P1-5.
4. P2 batch.

Each step: implement → write/update `walkthrough.md` (English) with task,
approach, and result → update `audit.md` → commit. Then hand back to Agent 2.
