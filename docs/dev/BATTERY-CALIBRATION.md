# Battery calibration: the community procedure vs the Kefir Hub wizard

Source: a Russian-language guide "Battery desync fix, extended edition" (2026-10-21, given by the owner) plus the video
"Switch OLED 8600mAh Battery Upgrade" (youtu.be/uNj7n0z4VEI). The video has no voice; the owner pasted its on-screen steps on 2026-10-21 (section "The video, step by step").

## What the guide says

Measure with Status Monitor (not the console's own number): Actual Capacity, Voltage (mV), Current Flow (mA),
Power Flow (W). The battery counts as **full** when Current Flow and Power Flow are ~0 and stay so for about 5 minutes
(the charger can resume after a first zero). A cycle is one uninterrupted charge from empty; keep the **screen on**
(minimum brightness) while charging, because in sleep the console does not always charge to the end. The console
recomputes the capacity ("Age") when it reaches 100 %, so the peak is only reached on a full charge. A drop of about
300 mAh in use is normal and comes back with a normal discharge-charge cycle; a full cycle once a month is enough as
maintenance. Raw Charge may differ from the console's % by 1-3 %. The console sleeps itself at 3200 mV.

Procedure, in emuMMC:
1. discharge to 3.3 V without overclock;
2. Battery Desync Fix: **X** (reset the calibration), exit with B;
3. charge to 100 %;
4. discharge until the red battery shows and the console no longer starts (ideally at 1 % / 3.2 V);
5. charge, switch on after about **3 minutes** (the battery icon on black goes away and Atmosphère starts);
6. charge to 100 %;
7. discharge to the red battery once more.

Moving the calibration: charge to 100 % in emuMMC, BDF **A** (save to the SD), boot sysMMC, BDF **Y** (restore).

Voltage limit in BDF can be wrong (OLED/Mariko v2 4190 mV, Lite 4320 mV). Then PRODINFO (CAL0) has to be edited, **with a
backup of the original first**. Owners of enlarged batteries: PRODINFO must read like a Lite (limit 4320 mV), battery
type byte **02** (00 = OLED, 02 = Lite). Cycles: the video shows the right arrow adding 50 cycles per step.

## How the wizard maps to it

| Guide | Kefir Hub wizard | Gap |
|---|---|---|
| X: reset the calibration | step 2 "Calibration reset" (waits on the charger for 100 %, backup first) | the guide resets at ~3.3 V, the wizard at 100 %: both write the factory model; keep ours, note the difference |
| two discharge-charge cycles to the red battery | steps 3 and 4 with the drain page (0 % counted, time after 0 % kept) | none |
| "charge 3 minutes, then switch on" | not said | add the sentence to the drain page and to the interrupted-drain notice |
| full = current and power ~0 for 5 min | full = psm 100 % | add: on the charger the page shows the charge current and says when it stays ~0 |
| keep the screen on while charging | not said | add the hint; the drain page already keeps the screen on |
| match sysMMC and emuMMC (A then Y) | step 5 "Match", the hop to the other MMC (0.14.163) | done: no manual A/Y |
| monthly maintenance cycle | not said | docs sentence |
| voltage limit wrong / enlarged battery: PRODINFO battery type 02 | not covered | see F.29 |
| cycle count edit (steps of 50) | the wizard keeps one count for both MMCs | PRODINFO cycle edit is not planned; the SD copy holds the count |

## Enlarged batteries (F.29 design)

- detect: the chip's learned full capacity above the design capacity by a clear margin (design 4600 / 3600 mAh): "a larger
  battery is built in" (a verdict row, pure function with a host test);
- the "capacity under design = desync" rule (`CAPACITY_LOSS`) must not misfire: a larger battery is above design;
- check CAL0 battery type: read-only first (already read at 0x4310 for the table choice). When it is not the Lite value, the page
  says what to change and why (charge limit 4320 mV), and offers the TegraExplorer script path (like F.25: backup of PRODINFO,
  a clear warning, a script; the Hub does not write PRODINFO itself in HOS).
- never write PRODINFO without the CAL0 section CRC rewritten and a verified backup.

## The video, step by step (enlarged 8600 mAh battery in a Switch OLED)

- Why: the cell is rated 4.45 V; v1/v2/OLED charge only to 4.2 V (about 7000-7500 mAh). Setting BatteryVersion in PRODINFO makes
  HOS treat it as a Lite battery: charge up to 4.32 V (about 7500-8000 mAh). Cost: the Lite charge current limit (1.6 A, not 2 A).
- Steps: Lockpick_RCM (prod.keys) -> TegraExplorer: dump PRODINFO to `PRODINFO.bin` -> on a PC hex editor: offset **0x4310**, byte
  **00 -> 02** -> a Python script fixes the **CRC16 and SHA256 hash** of the BatteryVersion block (without it HOS cannot read the
  version and falls back to 00) -> TegraExplorer: restore the edited PRODINFO -> boot Atmosphère -> Battery Desync Fix NX.
- Check: BDF must show **LiHV in white**. **LIPO in yellow** = PRODINFO was not changed correctly.
- Then X (reset the calibration); when it asks for charge cycles: 0 and A; B to leave; reboot; **2-3 full cycles**.
- Result seen: Hekate -> Console Info -> Battery Info: full capacity 7810 mAh after 3 cycles.

Consequences for the Hub (plan F.29):
- the cell type is already derivable: CAL0 battery version 2 = Lite table = LiHV 4.32 V, any other = LiPo 4.2 V (`ChooseTable`);
  show it on the Battery page ("LiHV" / "LiPo") and in the wizard, so the owner sees at once whether PRODINFO took;
- the wizard's reset must not ask for cycles: the count is kept by the SD copy (the video's "0" = start counting again);
- the capacity checks (design 4600 / 3600) must know "larger battery": expected full capacity 7500-8000 mAh, not 4300;
- the PRODINFO edit (byte 0x4310, CRC16, SHA256) is a TegraExplorer-script job (HOS cannot write CAL0): build the script from
  the dump (dump, patch, verify, restore) and keep the dump as the rollback. The CRC16/SHA256 layout of the block must be read
  from the owner's Python script before it is implemented.

## The edit itself: BatteryVersionEditor (lulle2007200, GPL-2.0, release V0.1, 2026-10-21)

A homebrew NRO that writes PRODINFO from HOS (`fsOpenBisStorage(CalibrationBinary)` under Atmosphère). What it does, and what the
Hub would do (written fresh from this layout, not copied):
1. back up the whole PRODINFO (`sdmc:/backup/prodinfo.bin`, numbered when it exists);
2. read the 0x10-byte block at **0x4310**; byte 0 = the battery version (0 / 1 / 2); write it back;
3. the last two bytes of the block (0x431E) = **CRC16 over the first 0xE bytes**: init 0x55AA, nibble table
   `0000 CC01 D801 1400 F001 3C00 2800 E401 A001 6C00 7800 B401 5000 9C01 8801 4400`, two nibble steps per byte (low nibble first);
4. the CAL0 body hash: **SHA-256 over [0x40, 0x40 + u32 at 0x08)**, written at **0x20**;
5. verify: the client certificate hash (SHA-256 of [0xAE0, 0xAE0 + u32 at 0xAD0) against the hash at 0x12E0), the body hash and the CRC.
   The cert hash is only checked, never rewritten;
6. restore = copy the 0x10 block from the backup and redo steps 3-4. Reboot after.

So no TegraExplorer script is needed for the battery version: the Hub can do it in HOS (needs the write to the calibration
partition to be allowed, as the NRO shows it is on the owner's setup). The pure parts (CRC16, hashes, the block patch) go into
a header with a host test against the layout above. F.25 (region) can reuse the same backup/verify path, with other fields.

## Letting the Hub write PRODINFO (Atmosphère rules, read from its source 2026-10-21)

Atmosphère blocks writes to the calibration partition. Two switches open it, both read at start (so a reboot is needed):
- `exosphere.ini`: `allow_writing_to_cal_sysmmc=1` (sysMMC only). With it on, Atmosphère first keeps its own secure backup of
  the calibration data in unused space; if it cannot make that backup it leaves writes off (`amsmitm_prodinfo_utils.cpp`).
- `atmosphere/config/system_settings.ini`, `[atmosphere]`: `enable_hbl_bis_write = u8!0x1` (BIS writes for homebrew;
  `fs_mitm_service.cpp`).

Flow for "Change battery type": check both switches; if off, say so, turn them on, store a pending marker, reboot; at the next
start the Hub backs up PRODINFO to the SD (verified by reading it back), writes byte 0x4310, CRC16, body SHA-256, verifies, turns
both switches off again and offers the reboot. The backup stays for the rollback.

## Cutoff pipeline (decided with the owner, 2026-10-22)

Why: the percent of the gauge is not a safe guide. The owner saw the console cut off with the big battery icon at 35 %, and also
show 0 % for an hour. What counts is the real cut-off (the console switches itself off, about 3.2 V under load).

**Entry.** Tools > "Battery calibration", straight into the wizard (not through the Battery page; that page stays for the readings).
The wizard first says the method, then shows the steps. Only the first step is active; each next one opens when the one before
is done. The state is on the SD, so a restart or a cut-off does not lose the place.

**Steps**
1. **Drain to the dead battery.** The charger must be out (the wizard checks). Hub runs the maximum load (CPU and GPU) at the
   maximum brightness. The screensaver starts on Minus, or after 3 minutes, at the maximum brightness; the stick lowers it
   (the log keeps the brightness). Hub logs every second to the SD: voltage, current, gauge percent, temperature, load, brightness.
   The wizard tells the user: when the console switches off, switch it on again; do it as many times as the console starts,
   until it does not start at all. Each start continues the same step **by itself, without a button**, already in the tail mode:
   low brightness and minimal load (the cell is nearly empty, there is no point in hammering the CPU and GPU again). The user can
   still change the brightness with the stick. The drain page shows a big battery and, under it, live charts (voltage with the
   3.4 V / 3.2 V lines, current and power, temperature, the load the Hub sets); CPU and GPU load as a reading is to be checked on the console.
2. **Charge about 3 minutes, switch on; the Hub sets the zero by itself** (a step of the wizard, no menu). The gauge stays powered by the
   battery while the console is off, so it keeps counting the charge of these minutes (to confirm on a console). The last line
   of the log before the cut-off holds X, what the gauge believed was left (the real value was 0, so X is its error). At the start
   the Hub reads the counter N and writes N - X: the charge of the 3 minutes stays, the error goes. If the gauge was reset (the
   counter is obviously fresh) or the log is missing, the Hub writes 0 and says so in the report; the next cut-off fixes it.
   The factory model is written at the same time (the guide resets at ~3.3 V). Open: which register holds it (RemCap / RepSOC).
3. **Cycle 1:** charge to 100 % ("full" by psm 100 %, later by the current ~0 for 5 minutes, screen on while charging), drain as in step 1.
4. **Cycle 2:** the same.
5. **Report:** the learned capacity (verdict, enlarged battery) and the gauge percent just before each cut-off (near 0 % = the gauge
   agrees with the console).

**Load plan of a drain (owner, 2026-10-23; replaces the 3.4 V latch).** Before the first cut-off: maximum load and maximum brightness,
all the way (no switch at 3.4 V). After every cut-off, when the user switches the console on again: the **tail mode** at once, low
brightness and minimal load, until the next cut-off or until the console does not start. Reason: under a high current the cell
sags by current x internal resistance, the console cuts off early and leaves charge in the cell; after the first cut-off the
voltage has recovered, and a gentle drain takes that charge out. It costs no extra rule in the code: the load follows the number
of restarts (0 = maximum, 1 or more = minimal). The old "skip the tail when there is no sag" idea is dropped.

**Cautions:** high load at the maximum brightness heats the console: Hub logs the temperature and stops the load above the limit
(value to decide); the cut-off depends on the load and the temperature, so a game that pulls more current stops earlier than the drain.

**Transfer to the other MMC (decided 2026-10-22; replaces the hekate hop in the wizard).** After the report in emuMMC the Hub saves the
calibrated model and the cycle count to the SD and sets a marker "calibrated in emuMMC, not yet applied to sysMMC". The report
says: open the Hub in sysMMC. At every Hub start, in either system, the Hub compares the marker with the copy this system holds. If the
SD copy is newer and the calibration was good (same battery model in CAL0), the Hub applies it **by itself**: backup of the current
copy first, then the write, then the marker is cleared for this system. It tells the user with a notification (a toast) at the
start: "Battery calibration was transferred from emuMMC." A failure gives an error toast and keeps the marker. It never overwrites a
newer calibration (the marker carries a sequence number and the time). The same works the other way (sysMMC to emuMMC). The old
hop (0.14.163) stays as a manual action only.

**Several emuMMCs (owner, 2026-10-22).** A user can have more than one emuMMC, so "the other MMC" is not enough. Every system the
Hub can run in has an id: `sys` for sysMMC; for an emuMMC the kind plus its place from `emummc.ini` (the start sector of a
partition, or the folder of a file-based one), shown with a name the user knows (the folder, or "emuMMC, sector 0x...").
The Hub keeps a ledger on the SD (`/config/kefir/battery/ledger.json`): the calibration (sequence number, time, the id of the system
it was made in) and, for every system, the sequence number last written there. At a Hub start the current id is looked up: if
this is not the origin and its number is older than the newest calibration, the Hub writes the value **openly**: a toast ("Battery
calibration of emuMMC `X` was written to `Y`"), a line in the Battery page history, and a record in the ledger ("written to emuMMC
`Y` at time T, sequence N"). A system that never ran the Hub gets it at its first start. The ledger is the single place that
answers "where was this written".

