# Battery calibration: handoff for a fresh session (written 2026-10-27)

Read this first, then `docs/dev/BATTERY-CALIBRATION.md` (design) and `docs/dev/BATTERY-ZERO-REGISTER.md` (gauge research).
Reports to the owner are in Ukrainian; code, commits, CHANGELOG, plan.md are in English. Short sentences, one term for one thing.
Owner's glossary (Ukrainian UI): "датчик заряду" (never "гейдж"), "Далі" (never "Наступний"), "відсік" for the cut-off.

## 1. Goal

Tools > "Battery calibration" (and + on the Battery page) teaches the Switch fuel gauge (MAX17050) again. Two desync types:
1. The console shows many percent but switches off by voltage (35 %, "3193 mV, chip 32 %, HOS 24 %").
2. The console shows 0 % but runs for another hour.
Owner's idea (2026-10-24): the voltage where the console really switches off is the zero (about 3.2 V under load). The chip's own
`V_empty` (0x3A) of this console is already 0xA05F = 3.20 V (VE) / 3.80 V (VR). The threshold is not the problem; the learned values
(FullCAP, counters, QResidual) drift, and only a reset plus two full cycles relearn them.

## 2. The decision that matters (owner, 2026-10-24)

"We did everything right in the old version; the problems were the UI. We worked on the UI and the logic went somewhere wrong."
The working logic is **0.14.111-163** (commits a147ba6b, 51a6b3d2, c0c6102d, ad6c663d, e6a3a6da, 93235c99, 55653ce7, 85df1dd3, ce4a003e).
It cured a user's console. It is restored in **0.14.203** (commit 3d23440e). Do not redesign it again without the owner.

Old and restored logic, in order:
1. **Reset first**, at 100 % on the charger, backup first (`detail::RunReset`: `WaitForFull` then `ResetNow`: backup, `FactoryInit`
   = BDF's X sequence, `SaveCopy`, writes `wizard::State{reset_at, reset_mmc, total_at_reset}`). Proven on .141 (v0.14.112 log:
   RepSOC 94 % = VFSOC 94 % right after).
2. **Cycle 1 and 2** (`wizard::Advance`, `battery_wizard.hpp`): a cycle counts when `Got >= CYCLE_DONE (90)` (x100 of the battery
   discharged since the reset, from HOS's cycle total = set:sys copy + chip Cycles delta over 0x60) **and** psm shows 100 %.
   The Hub need not run while the console drains. Cycle 2 is measured from the moment cycle 1 counted. `Redo` exists in the old API.
3. Drain rule of the drain page: maximum load and brightness until the first cut-off, tail mode after each restart (auto-brightness
   off, minimum brightness, minimal load). (The older 0.14.152 rule "below 35 % load off, brightness 2 %" was replaced on 2026-10-23.)
4. **Align the charge to the voltage** (v0.14.153/154): after a drain the console cut short, `PollInterruptedDrain` offers to write
   RemCapMIX 0x0F, then RemCapREP 0x05, then RepSOC 0x06 from VFSOC (`PlanAlign`, `battery_gauge.hpp`): charger out, VFSOC <= 10 %,
   RepSOC at least 15 points above VFSOC. This is already "the voltage is the zero". It stays in the wizard (0.14.203 removed the
   `CalibrationActive()` gate).
5. Match / transfer to the other MMC: now by the ledger (see 4.4), no hekate hop in the wizard.

What went wrong in 0.14.193-202 (dropped): drain to the dead battery first, then a "zero" step with the charger in
(`ZeroAfterCutoff` N - X, factory model at the cut-off), cycles tracked by physical cut-offs in a marker. It drained on calibration
data that was already wrong (the reason for a switch-off at 35 %), it was never run end to end on a console, and it contradicted
the proven `PlanAlign` guard (charger out). `ZeroAfterCutoff`, `LastDrainRemainingMah` and the zero pages were deleted.

## 3. What is known about the gauge and this console

Live dump 2026-10-24 (Hub 0.14.198, 61 %, emuMMC; file `/config/kefir/battery/20261024-133517-emummc.txt`, 0.5 mAh per LSB at 10 mOhm):
- Status 0x00 = 0 (no POR). CONFIG 0x1D = 0x7214 (hekate's fix writes 0x7254). LearnCFG 0x28 = 0x2473. SHDNTIMER 0x3F = 0xE000.
- VALRT 0x01 = 0xFF00: the gauge's min-voltage alert is off, so HOS does not switch off by the gauge alert.
- V_empty 0x3A = 0xA05F = VE 3.20 V, VR 3.80 V (same on 2026-10-16).
- FullCAP 0x10 = 0x2310 = 4488 mAh, DesignCap 0x18 = 0x2476 = 4667 mAh, FullCapNom 0x23 = 0x2304 (4482). Cycles 0x17 = 0x00CF.
  RepCap 0x05 = 0x154C (2726 mAh), RepSOC 0x06 = 0x3CBE (60 %), MixCap 0x0F = 0x15AA, VFRemCap 0x4A = 0x15D4, VFSOC 0xFF = 0x3DC8 (61 %).
- 2026-10-16 backup (deleted at the owner's request): FullCAP 4503, FullCapNom 4470, Cycles 0x0130, HOS cycles 2589.
Research (cloud agent, Fable, 2026-10-24; full text in `BATTERY-ZERO-REGISTER.md`):
- The counter to write is **RemCapMIX 0x0F**; 0x05 (REP) and 0x06 (RepSOC) are filtered copies and follow slowly. Order 0x0F, 0x05, 0x06,
  plain I2C writes, no unlock (Linux `max17042_load_new_capacity_params`, hekate `_max17050_load_new_capacity_params`).
  The datasheet does not say the host may write them; writability rests on driver code and on our own `RunAlign`.
- Writing a value below the real charge makes the gauge learn a too-low FullCAP at the next end of charge.
- QH 0x4D is the raw coulomb count (resets at power-up). Unknown without hardware: does the gauge count while the console is off
  (datasheet: RAM kept in shutdown; I2CSH may stop counting after SHDNTIMER 1.6 h); what HOS does at power-off.
- `PlanAlign` refuses with the charger connected; hekate wrote its reset with a charger attached.
- Console experiments still to run (list in `BATTERY-ZERO-REGISTER.md`): dump now; counts while off (QH after 1 h off); does writing
  VE = 3.6 V snap the SOC to 0 (charger in, restore 0xA05F after); write order 0x0F -> 0x05 -> 0x06; one full run with the log.
  These need a small diagnostic action in the Hub (raw register write), which does not exist.

## 4. Code state (0.14.203)

### 4.1 Files
- `sphaira/include/battery_wizard.hpp`: old pure logic (`State`, `Live`, `Advance`, `Got`, `Redo`, `NoteDrain`, `DrainStepOf`, `Steps`). **Truth for reset and cycles.**
  State file `/config/kefir/battery/calibration.txt`.
- `sphaira/include/battery_calib_state.hpp`: marker `/config/kefir/battery/calib.txt`. `Step {Reset, Cycle1, Cycle2, Report, Transfer, Finished}`,
  `StepFor/Sync` (reset and cycles mirror the wizard state), `Check` (Hub restart vs cut-off by boot time), `StartDrain`, `LoadFor`,
  `DecideBrightness`. Fields: drain_started, drain_time, cutoffs, brightness_before, auto_before, last_set_brightness, boot_time.
- `sphaira/source/battery_calib.cpp` (steps, pages, `Resume`, `PollCalibration`), `battery_calib_report.cpp` (report, transfer, ledger,
  `ApplyLedger`), `battery_calib_internal.hpp` (`calib_flow` namespace shared), `battery_calib_ui.hpp` (`calib_ui::Page`: Head, Para,
  Item, Warn, Ok, Dim, Check, Row, Boxes).
- `battery_drain.cpp` (+ `_sample.cpp`, `_draw.cpp`, `battery_drain_menu.hpp`): `DrainMenu`; with `CalibRun{charge, tail, step, on_charge_done, on_stop}`
  it is the drain page and the charge page (big battery, charts, screensaver on Minus or after 3 min, a log line a second in
  `/config/kefir/battery/calib-log.csv`). In calibration mode it now also counts cycles (`UpdateStep` every 5 s).
- `battery_write.cpp`: the only code that writes the chip: `ResetNow`, `detail::RunReset(done)`, `RunAlign`, `RunRestore`, `RunMatch`, `ActionsMenu`
  (opened by `OpenActions`, the "Actions" row), `PollInterruptedDrain`, `detail::MatchNow`.
- `battery_cutoff.hpp`: ledger (`Ledger`, `Decide`, `RecordCalibration`, `RecordWritten`, `SystemId`, `SystemName`), `LoadPlan`/`NextLoad`/`TailWorthIt`
  (dead: the 3.4 V latch was dropped; remove later), `battery_calib_log.hpp` (CSV, `Cutoffs()`).
- Tests: `tests/test_battery_calib_state.cpp`, `test_battery_cutoff.cpp`, `test_battery_calib_log.cpp`, `test_battery_wizard.cpp`, `test_battery_gauge.cpp`.

### 4.2 Flow
Intro (2 pages) -> "Start calibration" writes a fresh marker and an empty wizard state -> steps list (5 rows + Actions + Remembered):
1 Reset the gauge (page, then `RunReset`: a ProgressBox waits for 100 % / 4.1 V, then resets); 2 and 3 Cycle (page: Start the drain
or, when 90 % is drained, Start charging; `StartDrainRun` / `StartChargeRun`); 4 Report; 5 Transfer (ledger, applied at the next Hub
start in another system by `ApplyLedger`). `Resume` at the Hub start (10 s after launch, `PollCalibration`): a drain that was running
goes on (Hub restart = same boot; cut-off = new boot without the charger -> tail mode).

### 4.3 Known gaps (not done)
- Auto start of the drain when the charger comes out (the old page had `AutoDrainTick`).
- The old non-calibration drain page (`StartDrain`, `OpenDrain`, old layout) is unreachable code.
- `battery_hop.cpp` (hekate hop) is no longer reachable from the wizard; `PollHop` still runs.
- Dead pure logic: `LoadPlan`, `NextLoad`, `TailWorthIt` in `battery_cutoff.hpp`.
- The interactive guide `docs/site/en/battery-wizard` shows the 0.14.197 order and must be redrawn (v26 stamp).
- 24 languages lack 262 i18n keys (`tests/test_i18n_deployment_contract.py` fails; pre-existing). Only en/uk are written.

### 4.4 Transfer ledger
`/config/kefir/battery/ledger.txt`: origin system id, sequence number, battery model (CAL0 version + vendor), whether the calibration was
good, per-system "written" numbers. System id: `sys`, `emu-f-<folder>`, `emu-p-<sector>`. At a Hub start the Hub writes the newest
calibration into another system (`MatchNow`, backup first) and shows a toast. Never overwrites a newer one.

## 5. Console test of 2026-10-27 (owner, 0.14.203): what to fix next

UI of the steps list and the reset:
1. The steps list shows all 5 rows; the locked ones say "Locked" in the same colour. At the start only step 1 must be active and the
   others greyed (or not focusable at all).
2. The row "Remembered" ("Запам'ятовується") is unclear (wrong word form: a label that says what is remembered, or drop it).
3. Reset page: the check "Charger is plugged in" with a tick tells the user nothing about what to do. The texts are confusing:
   "may be wrong, for example after the console switched off at 35 %" reads as if switching off writes something wrong; "the battery must be full
   for this ... the Hub waits on the charger" does not say what the user does now. Buttons "Back" and "Reset" are active with no explanation.
4. Pressing Reset opens the plain ProgressBox "Battery calibration reset" (the MTP-style box). The designed page was a charge page: big
   battery, load/charts, screensaver. Charging must be counted **as soon as the charger is in**, without pressing A; when the battery is
   full the page says it and asks (Reset: yes / no).
5. General: the logic must read naturally: detect, show state, then ask once.

Not battery, found in the same test (own tasks, not done):
- Time zone: the Hub kept the old zone (Denver, -6) after the zone was changed to Athens (+3) in Settings with the Hub open; the system
  time changed, the Hub's clock did not (needs a re-read of the zone/time on change, or at least `tzset`).
- "Replace the Homebrew menu" turned on in the Hub: after launching a forwarder from the desktop, sphaira opened instead of Kefir Hub
  (the replacement did not take effect; check what the setting writes and which NRO the forwarder loads).

## 6. Environment and traps

- Console: 192.168.50.69. Hub FTP anonymous on **:5000** (root = SD card). Script: `tools/dev/hub-input.ps1 -Ftp 192.168.50.69 Down Right A "wait 2" shot -Shot out.jpg`
  (commands A B X Y L R Plus Minus Up Down Left Right, `wait N`, `shot`, `dump`). Needs "Scripted input" on in the Hub. The MCP on :57331 gave
  no reply on 2026-10-24.
- **nxlink stalls at 38 %** on this Wi-Fi (twice, same byte). Use FTP: `curl -T build/ReleaseWithInstall/kefir-hub.nro ftp://192.168.50.69:5000/switch/kefir-hub.nro`.
  `/hbmenu.nro` cannot be overwritten while the Hub runs from it (the upload returns 0 and the size stays): close the Hub first.
- Build: WSL, `cmake --preset ReleaseWithInstall` then `cmake --build --preset ReleaseWithInstall --parallel $(nproc)` in `/mnt/d/git/dev/kefir-hub`.
  The build dir had a stale cache from the old checkout; it was deleted and rebuilt (about 10-15 min full, minutes incremental). Console build uses
  `-Werror` (an unused function is an error). Host tests: `wsl bash -lc 'cd /mnt/d/git/dev/kefir-hub && tests/run.sh --quick'`.
- `tests/test_doc_labels_contract.py` must print "0 unknown" (docs `[[labels]]` must exist in en.json). New UI strings go to `assets/romfs/i18n/en.json`
  and `uk.json` (en value = key).
- Bash heredocs collapse backslashes: write scripts with the Write tool, run with `python -I`. The scratch scripts of earlier sessions hardcode
  `D:\git\dev\sphaira\`; this tree is `D:\git\dev\kefir-hub`.
- Delivery ritual (AGENTS.md): bump `sphaira/CMakeLists.txt`, CHANGELOG entry, plan.md, i18n, docs drafts (`<!-- draft -->` in `docs/site/{en,uk}/*.md`), commit with
  `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`. Never push unless asked. No git worktrees.
- Old artifact 5SvDnLoNiA8TirZqd1BJtW cannot be deleted by the agent (the owner deletes it with `/artifacts`). The guide snapshot artifact is QR5pC8FvQKbs48WZjTXunc.
- I deleted by the owner's order: `calib.txt`, `calib-log.csv`, and the two files of 2026-10-16 in `/config/kefir/battery/`. The log with four test cut-offs was lost.

## 7. Suggested next steps

1. Fix the UI points of section 5 (steps list states, reset page texts and flow, the charge page for the reset, counting from the moment the charger is in).
2. Run a full calibration on the console (reset, two cycles, report) and write down what the log shows (cut-off voltages, cycle counts, the Align offer).
3. If a raw register diagnostic is wanted: a hidden action to read and write 0x0F/0x05/0x06/0x3A and run the experiments of section 3.
4. Time zone and "replace the Homebrew menu" bugs (section 5).
5. Redraw the guide, translate the new keys, remove the dead code listed in 4.3.
