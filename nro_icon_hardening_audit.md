# NRO icon hardening — senior audit

**Stage:** read-only design audit — no production change, version bump, or commit.

**Observed worktree state:** clean before this audit; `HEAD` is
`320dd10fb0a1e39f728ddf0cc5537014102b5c74` (`v0.13.448: limit NTP
notifications`).  This is one version beyond the delegated `v0.13.447` baseline;
the audit intentionally neither changes it nor reserves a later version.

**Scope:** untrusted NRO icon bytes, including the same normalisation route used by
forwarder creation and SteamGridDB.  This explicitly does **not** put an NRO-icon
limit on themes, covers, screenshots, gallery images, or the generic file viewer.

## Executive conclusion

The local 1 MiB compressed-blob limit in `nro_get_icon_internal()` is a good first
boundary, and its exact-read check prevents partial data from masquerading as an
icon.  It is not a decoded-image boundary.  `ImageLoadFromMemory()` currently calls
the decoder before validating dimensions, computes `x * y * 4` in `int`, and has no
overflow-safe checks in resize/JPEG output paths.  A crafted small image header can
therefore request an excessive decoded allocation or create unsafe arithmetic before
the Homebrew UI reaches NanoVG.

The smallest correct delivery is one **icon-only** policy in `image.cpp`: shared
overflow-safe image-size helpers plus `ImageLoadIcon()`/`ImageNormalizeIcon()`.
Generic image loaders retain their existing unlimited-by-policy behavior (while using
the shared arithmetic guards), and only NRO/forwarder icon callers opt into
`256x256` output and `1024`/`1,048,576` decoded limits.  The iconless-forwarder
fallback must ship atomically with this: both features need the same normalizer and
the same safe default icon fallback.

### User-facing resize/crop behavior

The repository already has two relevant mechanisms, with different jobs:

- **`ImageResize`** is the existing shared RGBA resampler.  It already converts a
  picked icon to `256x256` in `GetNroIcon` and SteamGridDB; hardened code must retain
  and reuse it rather than inventing another resizer.
- **Theme Creator's `RenderVisibleImage`** is a user-controlled wallpaper composer:
  it fits, zooms, pans, and crops a selected image into the fixed `1280x720` theme
  canvas before JPEG encoding.  It is intentionally not a generic resize helper and
  cannot be reused verbatim for square icons without turning a simple security fix
  into a second editor with incorrect wallpaper geometry.

For the security delivery, legitimate NRO/SteamGridDB icons are resampled to the
required square size automatically—the same established icon behavior, now safe.  If
the product wants a **user choice** for a non-square icon chosen in the forwarder
file picker, the right follow-up is a small square variant of the existing Theme
Creator *interaction* (fit / zoom / pan / crop, target 256x256), scoped to manual
picker selection only.  It must not run while rendering Homebrew entries or parsing
untrusted NROs.  This preserves a normal user workflow without centralizing a new
all-purpose image editor in the hardening patch.

## Evidence and threat model

| Boundary | Local state | Remaining risk / required policy |
|---|---|---|
| NRO asset compressed bytes | `nro_get_icon_internal()` rejects `size > 1 MiB`, reads once, and requires `bytes_read == icon.size()`. | Keep it.  This bounds I/O and compressed storage only; JPEG/PNG decompression can expand far beyond 1 MiB. |
| Decoder metadata | `ImageLoadFromMemory()` has no `stbi_info_from_memory()` preflight and passes `data.size()` to an API taking `int`. | Reject empty data and `data.size() > INT_MAX`; inspect dimensions before stb decode/allocation. |
| Decoded RGBA | `x*y*BPP` occurs in `int` in stb result storage, resize allocation, and JPEG reserve. | Require `x,y > 0`; use checked `size_t` multiplication for pixels and bytes; verify input spans and all row pitches. |
| NVJPG | JPEG path allocates `nj::Surface{image.width,image.height}` after `parse()` and later uses unchecked width/height/pitch products. | Validate parsed dimensions before surface allocation; reject values above `INT_MAX` before conversion; validate actual surface BPP/row-copy arithmetic again. |
| GPU/UI | Homebrew passes decoded RGBA to `nvgCreateImageRGBA`; failed reads can retry unless metadata is cleared. | `ImageLoadIcon()` returns only 256x256 RGBA; failed icon clears the entry metadata so it uses the default image and does not retry. |
| Forwarder output | `GetNroIcon()` returns original bytes after failure; editor treats empty icon as a required-field error. | Normalisation failure must fall back to safely normalised default bytes, with raw default only as a final known-bundled fallback. |

The attacker controls a malformed/truncated NRO icon or an NRO selected through the
forwarder picker.  For SteamGridDB, downloaded bytes are also untrusted but have a
separate compressed-download cap.  The goals are no signed/unsigned overflow,
unbounded allocation/decode, pathological NanoVG upload, decoder retry loop, or
unsafe raw byte hand-off.  Decoder-internal resource consumption before its available
metadata API cannot be eliminated completely; preflighting stb dimensions and NVJPG
parsed dimensions is the earliest available normal application-level gate.

## Upstream source map

Primary upstream source: [`5b0779d` — safely handle oversized NRO icons](https://github.com/NaGaa95/sphaira/commit/5b0779d14c9157260245e7befef11f81a29553b7),
whose parent is `e00ac7c950229a6ef9b82652a5bf98dcccd4c846`.  The GitHub comparison
from `5b0779d` to audit-head `c19e5a3` contains no subsequent change to the six
icon-hardening files, so there is no later corrective patch to import.

| Upstream file / behavior | Local equivalent | Decision |
|---|---|---|
| `include/image.hpp`: `ImageLoadIcon`, `ImageNormalizeIcon` | `sphaira/include/image.hpp` | Adapt; public only because existing callers in different UI translation units require it. |
| `source/image.cpp`: checked size helper, `ImageLimits`, stb preflight, NVJPG guards, checked resize/JPEG | `sphaira/source/image.cpp` | Adapt, then review NVJPG pitch handling more strictly than the upstream patch. |
| `source/app.cpp::GetNroIcon` | `sphaira/source/app_settings.cpp::GetNroIcon` | Adapt location, not path.  Never return rejected original bytes. |
| Homebrew NRO rendering | `sphaira/source/ui/menus/homebrew.cpp` | Use `ImageLoadIcon` before NanoVG, preserve current lazy-load and clear-on-failure behavior. |
| `steamgriddb_icon.cpp` conversion | same local file | Replace duplicated decode/resize/JPEG sequence with `ImageNormalizeIcon`; retain its download-size cap. |
| `forwarder_editor.cpp` default/invalid fallback | same local file | Adapt.  Empty and invalid selected values both recover to default image data; `UpdatePreview()` already deletes the old handle first. |

Do **not** cherry-pick: the local `GetNroIcon` lives in `app_settings.cpp`, the
editor has extra title/platform rows, and concurrent Custom NRO-search-path/NFS work
must remain outside this delivery.

## Local flow and callers

```text
NRO file -> nro_get_icon overloads -> capped compressed bytes (<= 1 MiB)
  -> Homebrew Draw -> ImageLoadIcon -> 256x256 RGBA -> NanoVG
  -> InstallHomebrew / App::Install -> GetNroIcon -> ImageNormalizeIcon -> NCA JPEG
  -> forwarder Editor picker -> NormalizeIcon -> ImageNormalizeIcon -> preview/NCA JPEG

file / SteamGridDB bytes -> NormalizeIcon -> ImageNormalizeIcon -> preview/NCA JPEG
other images -> ImageLoadFromMemory/File (generic policy; no icon maximum)
```

Relevant callers were traced with Graphify structural extraction and direct source
reading:

- `nro_get_icon(path,size,offset)` feeds Homebrew display, `InstallHomebrew`,
  customize, the forwarder picker, and file-association NRO icon lookup.
- `App::Install` currently calls `GetNroIcon` only for nonempty `config.icon`;
  `install_share.cpp` separately rejects an empty current-NRO icon before reaching
  it.  That explicit product message is outside this atomic delivery unless its
  owner asks to make Kefir Hub itself iconless-installable.
- The forwarder editor calls `NormalizeIcon` on construction and for file/NRO/
  SteamGridDB selection, then `UpdatePreview()` deletes `m_preview` before creating
  a replacement and its destructor deletes any remaining handle.
- SteamGridDB `ConvertToForwarderIcon` duplicates current decoding and enforces
  `MAX_IMAGE_DOWNLOAD_SIZE`; it is the correct secondary consumer of the shared
  icon normalizer.
- Generic image callers include theme creation/Themezer/file viewer/AppStore/game
  covers/save/cheats/GameCard; they must remain on `ImageLoadFromMemory/File`.
  They get arithmetic hardening, not the 1024 icon cap.

## Required limits and edge behavior

- Display/retained icon: exactly **256x256 RGBA**, then JPEG for forwarder/NCA bytes.
- Accepted source icon: positive dimensions, each dimension `<= 1024`, total pixels
  `<= 1024 * 1024`; downscale every accepted non-256 image.  This admits the known
  SuperTux `1024x1024` icon (roughly 300 KiB compressed) and rejects `1025x1024` or
  any aspect ratio whose pixel count exceeds the cap.
- Preflight and post-decode validation are both necessary.  stb gets the former via
  `stbi_info_*`; dimensions returned by `stbi_load*` are checked again before copy.
  NVJPG validates parsed dimensions before `Surface::allocate()` and validates the
  actual surface before vector allocation/row copies.
- A failed/oversized/corrupt NRO icon is cleared from Homebrew entry metadata and
  displays `App::GetDefaultImage()`.  It is attempted once per entry state, not once
  per frame.
- `GetNroIcon` first normalizes supplied bytes, then normalizes
  `App::GetDefaultImageData()`, and only then returns the raw compiled-in default
  bytes.  Raw fallback is safe because it is trusted, immutable bundled data—not
  attacker input.
- Forwarder editor construction normalizes default data for empty values; if a
  supplied value is invalid it retries the default and labels it `Default`.  Its
  current preview teardown avoids a NanoVG handle leak on this path.

## Minimal design and exact implementation scope

1. **`sphaira/source/image.cpp`** — add private checked helpers (`GetImageSize` and
   limits predicate) using `size_t` division-before-multiply.  Check stb input length
   before cast, run `stbi_info_from_memory` / `stbi_info` first, and recheck decoded
   result.  Apply checked input/output byte requirements and `INT_MAX / BPP` pitch
   checks to `ImageResize`; validate JPEG input and callback `offset + size` before
   resize in `ImageConvertToJpg`.  Validate NVJPG parsed dimensions before surface
   allocation and each surface row/pitch range before copy.
2. **`sphaira/include/image.hpp`** — declare only `ImageLoadIcon` and
   `ImageNormalizeIcon`; no framework/type hierarchy.
3. **`sphaira/source/app_settings.cpp`** — make local `GetNroIcon` use the shared
   normalizer and the trusted default fallback described above; in `App::Install`,
   supply default data when `config.icon` is empty so iconless NRO forwarders work.
4. **`sphaira/source/ui/menus/homebrew.cpp`** — use `ImageLoadIcon`, leave generic
   NRO parsing untouched, and preserve metadata clearing/default rendering on failure.
5. **`sphaira/source/ui/steamgriddb_icon.cpp`** — keep download cap, delegate to
   `ImageNormalizeIcon` to remove duplicate policy.
6. **`sphaira/source/ui/forwarder_editor.cpp`** — normalize default for empty input;
   if supplied data fails, retry default, then use raw trusted default as the final
   fallback.  Do not add a second normalizer or alter preview ownership.
7. **Tests only if the existing host-test setup can compile the extracted pure helper**:
   add the smallest focused test file or a test-only translation-unit seam.  Do not
   create a production public abstraction merely to test private arithmetic.

### Deliberately not included

- No global `1024x1024` cap on `ImageLoadFromMemory/File`: themes, screenshots,
  covers and user file-viewer images have different legitimate limits.
- No special NRO parser rewrite: compressed size/exact read already belong there.
- No new image library, background decoder framework, retry queue, release bump,
  commit, Custom NRO search paths, or NFS change.
- No copy of Theme Creator's 1280x720 wallpaper renderer into the automatic icon
  route.  A manual square crop/fit UI is a separately scoped UX follow-up, not a
  prerequisite for safe NRO decoding.

## Verification plan

First inspect the present host-test harness (`tests/run.sh`, CMake test targets) to
choose the smallest buildable seam.  Required automated checks:

1. valid 256x256 JPEG remains 256x256 and normalizes to nonempty JPEG;
2. valid 512x512 and 1024x1024 JPEG/accepted image normalize to 256x256;
3. `1025x1024` and another image with `> 1,048,576` pixels reject before decoded
   allocation on the icon route;
4. helper-level zero, negative, `INT_MAX`, `size_t` overflow, short input span,
   and oversized stb-length cases reject;
5. truncated/corrupt compressed bytes reject; accepted non-JPEG input is normalized
   if the current stb flow accepts it;
6. resize and JPEG conversion reject short buffers and overflow/pitch boundaries;
7. empty icon and invalid icon choose the default route; both Homebrew and forwarder
   invoke the same shared policy (a source-level/dead-symbol check is acceptable for
   UI paths not executable on host).

Before acceptance in the implementation stage: Gemini runs host tests, the existing
dead-symbol guard, `git diff --check`, and the repository-prescribed WSL
`ReleaseWithInstall` build.  The senior then reviews every caller and both decoder
paths.  Do not report Switch checks as completed without hardware.

Manual Switch checks, separately required: scan an iconless NRO, a corrupt NRO icon,
a 1024x1024 SuperTux-style icon, and a rejected oversize icon; verify default tile,
no repeated frame stalls/log churn, successful forwarder default icon, editor preview
replacement, and no visible GPU/resource degradation after repeated selection.

## Delivery staging and risk register

**One atomic delivery is recommended.**  The upstream-audit item “default icon for
iconless NRO during forwarder creation” overlaps materially with decoded-size
hardening: splitting it would either duplicate normalization/fallback logic or leave
one consumer returning invalid raw bytes.  This delivery adapts both together, but
does not reserve a version number.  Choose the next free version only after the
parallel integrations are reconciled and the final diff is accepted.

Risks to review in Gemini's diff:

- Do not apply icon limits to generic image consumers.
- Never allocate/copy based on unvalidated signed dimensions; never cast `size_t` to
  stb's `int` first.
- NVJPG must not allocate `Surface` before the icon policy check, and fallback to stb
  must occur at most once, not recursively without a terminating flag change.
- Ensure `nvgCreateImageRGBA` only sees post-normalized 256x256 data for NRO display,
  and editor `UpdatePreview()` still deletes the old image exactly once.
- Raw fallback may contain only `DEFAULT_IMAGE_DATA`, never caller-provided bytes.

## Gemini handoff (first fresh chat)

```text
You are the implementing junior engineer for this one bounded coding task. Work only
in the current repository. Do not broaden the scope, add dependencies, refactor
unrelated code, modify Custom NRO search paths or any NFS back-end work, bump the
application version, or commit. Read `nro_icon_hardening_audit.md` in full first;
it is the senior-approved design for this stage.

Task
Implement the minimal, icon-specific decoded-size hardening and iconless/invalid
forwarder-default fallback specified in the audit. Do not cherry-pick upstream.

Target chat
Sphaira NRO icon hardening — implementation round 1

Version
08-14 13:42

Relevant context
- NRO compressed icon blobs are already capped at 1 MiB with exact reads in
  `sphaira/source/nro.cpp`; preserve that boundary.
- The vulnerable boundary is decoded dimensions/pixels/allocation and image arithmetic
  in `sphaira/source/image.cpp`, including both stb and `USE_NVJPG` paths.
- `GetNroIcon` is in local `sphaira/source/app_settings.cpp`, not upstream `app.cpp`.
- The accepted icon source limit is positive dimensions, max 1024 per axis, max
  1024*1024 pixels; accepted non-256 icons become 256x256.  Generic image consumers
  must not inherit this icon cap.

Implementation scope
- `sphaira/include/image.hpp`
- `sphaira/source/image.cpp`
- `sphaira/source/app_settings.cpp`
- `sphaira/source/ui/menus/homebrew.cpp`
- `sphaira/source/ui/steamgriddb_icon.cpp`
- `sphaira/source/ui/forwarder_editor.cpp`
- the smallest applicable existing host-test file/new focused test only if the current
  test infrastructure can compile it; otherwise document the concrete host-test
  limitation in your report.

Acceptance criteria
- Introduce one shared icon-only load/normalise path; do not duplicate decoder,
  resize, JPEG conversion, or fallback policy in callers.
- For icons, validate `x,y > 0`, dimensions, pixels, byte multiplication,
  `size_t -> int`, input spans, resize pitches and JPEG output-size arithmetic.
- stb checks metadata before full decode and validates actual output again.  NVJPG
  validates parsed dimensions before `Surface` allocation and validates actual
  surface/pitch copies; fallback to stb is bounded.
- Valid 256, 512, and 1024 square icons work; 512/1024 become 256.  1025x1024,
  too-many-pixel, corrupt/truncated, short-buffer and overflow cases fail safely.
- Homebrew sends only safe 256x256 icon RGBA to NanoVG and clears failed metadata so
  it falls back without repeated decode attempts.
- SteamGridDB and forwarder file/NRO picker use the same normalizer.  Empty or invalid
  forwarder icon uses a safely normalized default, with raw default bytes only as the
  final trusted fallback; previews retain correct NanoVG deletion/replacement.
- Do not cap themes, covers, screenshots, file viewer or other generic image callers.
- Reuse existing `ImageResize`; do not copy Theme Creator's 1280x720 fit/zoom/pan
  renderer or add a manual crop editor in this security task.

Verification
First inspect `tests/run.sh` and existing CMake test wiring. Run the smallest relevant
host tests you can genuinely build, then `python3 tests/check_dead_symbols.py` if that
is the project’s existing invocation, `git diff --check`, and exactly:
`wsl bash -l -c "cd /mnt/d/git/dev/sphaira && cmake --build --preset ReleaseWithInstall --parallel 16"`
If this worktree’s path differs, report that rather than silently building another
tree. Do not claim Switch hardware testing.

When finished, modify the working tree and respond with exactly one final plain-text
fenced code block. Its first line must be `## Target chat: Sphaira NRO icon hardening
— implementation round 1`; its second must be `## Version: <new MM-DD HH:mm>`.
Then state: concise summary, files changed, exact verification actually run/results,
and blockers/assumptions. Include `git diff --stat` and identify every source caller
you changed. Do not send acknowledgements, progress updates, a version bump, a commit,
or unrelated refactoring.
```
