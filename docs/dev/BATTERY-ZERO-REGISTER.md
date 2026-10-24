# Battery calibration: which register holds the zero

Research note, 2026-10-24 (cloud agent, Fable). It answers the open question in `BATTERY-CALIBRATION.md`, step 2.
Every claim carries a kind: **datasheet**, **driver code**, **Hub code**, **inferred** or **unknown**.
Not reached: the official MAX17050 Software Implementation Guide and two ez.analog.com articles (host refused).

## Answer

- The coulomb counter to write is **RemCapMIX, 0x0F** (`MAX17050_MixCap`). Unit: 0.5 mAh per LSB at 10 mOhm [datasheet].
- RemCapREP 0x05 and RepSOC 0x06 are filtered copies of it and follow slowly [datasheet].
  So write the three in this order: **0x0F, then 0x05, then 0x06**. Plain I2C writes, no unlock [driver code: Linux `max17042_load_new_capacity_params`, hekate `_max17050_load_new_capacity_params`].
- 0x62/0x63 lock the model table only. 0x60 guards VFSOC0 only [driver code].
- The datasheet marks the registers "Output" and never says the host may write them. Writability rests on driver code and on our own `RunAlign` [inferred].
- RepSOC unit: 1/256 % (0x6400 = 100 %) [driver code].
- Raw coulomb count: **QH 0x4D**. It resets to 0 at power-up [datasheet]. A cleaner counter to subtract than RemCapMIX.

## Consequences for our design

1. **The drain log must record 0x0F (and QH 0x4D).** Today it records no mAh. `RepCap` is a filtered register: `N - X` with X from RepCap mixes two different quantities.
2. **Guard conflict.** The shipped `PlanAlign` (`battery_gauge.hpp`) refuses a write with the charger connected. The wizard's zero is written with the charger in. One of them must change. hekate wrote its full reset with a charger attached [driver code].
3. **Never write a bare 0 unless the gauge was reset (Status bit 1, POR, set).** A value below the real charge makes the gauge learn a FullCAP that is too low at the next end of charge [datasheet p.21 + inferred].
4. Better than `N - X` (to test, not a verdict): use the **QH delta** when a log from the same session exists
   (`added = QH_now - QH_lastlog`, 0.5 mAh per LSB, floor at 0), else the VFSOC-based align of `RunAlign`.
   The QH delta errs on the safe (low) side, because the console kept draining after the last log line.
5. Does the gauge count while the console is off: the chip keeps RAM in shutdown and resumes [datasheet p.30].
   With I2CSH set it may shut down after SHDNTIMER (default 1.6 h) and then stops counting [datasheet + inferred].
   What HOS does at power-off: **unknown**. Test 2 below settles it.
6. The chip's own empty learning never fires: the console cuts off near 3.3 V, V_empty defaults to 3.12 V [datasheet + inferred]. Whether Nintendo changed 0x3A: **unknown**.

## Write sequence (the zero step)

1. Read Status 0x00. If bit 1 (POR) is set the counters are default: use target T = 0 after the model restore.
2. Read 0x0F = N, 0x10 FullCAP, 0x23 FullCapNom, 0xFF VFSOC.
3. T = max(0, N - X), X in 0x0F units from the last log line (or the QH delta above). No log: use the VFSOC align.
4. Write-verify 0x0F = T.
5. Write-verify 0x05 = T x FullCAP / FullCapNom (the Hub's scaling; Linux writes T unchanged. Both are conventions).
6. Write 0x06 = T x 0x6400 / FullCAP.
7. Read back 0x0F, 0x05, 0x06 and log them. Do not touch 0x60-0x63, 0x45, 0x46, 0x17 or Status.

## Console test plan

1. Read 0x00 Status, 0x3A V_empty, 0x1D CONFIG, 0x3F SHDNTIMER, 0x28 LearnCFG, 0x17 Cycles, 0x4D QH, 0x0F, 0x05, 0x06, 0xFF.
2. Power-off test: note QH and Status, power off for 3 h on battery, boot the Hub, read both again.
   QH changed and POR clear: the gauge counted while off. QH unchanged: it shut down. POR set: it lost power.
3. With the charger in at about 50 %: write 0x0F = value - 10 (5 mAh). Read back 0x0F/0x05/0x06 at 0 s, 1 s, 30 s, 10 min.
   Expected: 0x0F holds; 0x05 and 0x06 move toward it over minutes.
4. Zero rehearsal: drain to cut-off with the log recording 0x0F and 0x4D, plug in, run the step, then one full charge.
   FullCAP 0x10 before and after end of charge: a drop above 5 % means T was too low.
5. Status must not change after each write (hekate's `0x8801` sets bits we do not want).

## Sources

- MAX17047/MAX17050 datasheet text (mirror, pages 9-34): https://www.manualsdir.com/manuals/281093/rainbow-electronics-max17047.html
- Linux `drivers/power/supply/max17042_battery.c`: https://github.com/torvalds/linux/blob/master/drivers/power/supply/max17042_battery.c
- hekate `bdk/power/max17050.c`: https://github.com/CTCaer/hekate/blob/master/bdk/power/max17050.c

## Zero by voltage (owner, 2026-10-24)

Goal: the voltage at which this console really switches off is the zero. It covers both desyncs:
(1) the gauge shows many percent, the console is already off; (2) the gauge shows 0 %, the console runs for another hour.

Facts from the Hub backup of 2026-10-16 (this console, emuMMC, Hub 0.14.103; the file is deleted, the values are kept here). Read again live on 2026-10-24 (Hub 0.14.198, 61 %): V_empty 0x3A is still 0xA05F, Status 0x00 = 0 (no POR), CONFIG 0x7214, FullCAP 4488 mAh:
- V_empty 0x3A = 0xA05F: **VE = 3.20 V** (bits 15..7, 10 mV), VR = 3.80 V (bits 6..0, 40 mV). The owner measured the real cut-off at about 3.2 V under load. The two are the same voltage.
- FullCAP 0x10 = 4503 mAh, DesignCap 0x18 = 4667 mAh, FullCapNom 0x23 = 4470 mAh, Cycles 0x17 = 0x0130, CONFIG 0x1D = 0x7214 (hekate's fix writes 0x7254), LearnCFG 0x28 = 0x2473, SHDNTIMER 0x3F = 0xE000, VALRT 0x01 = 0xFF00 (min alert off: HOS does not use the gauge alert to switch off).

Method:
1. **The anchor is measured, not assumed.** V_cut = the VCELL of the last log line before each real cut-off (`Cutoffs()` already finds the gap). The median over the cut-offs is the number for this console. The report shows it.
2. **The zero is physical.** After a cut-off the true remaining charge is only what was charged since. `N - X` gives it for both desyncs: in (1) X is the gauge's belief at the last line, in (2) the counter was clamped at 0, so X = 0 and N is already the charge added. This holds only if the counter keeps counting while the console is off (test 2 below).
3. **Capacity is measured by the Hub, not trusted.** Charge from the zero to 100 % and integrate the logged current (1 Hz) to get the real capacity. In (2) the gauge FullCAP is too small, in (1) too big. The two cycles compare the measured value with FullCAP.
4. **Chip anchor (to test, do not ship blind).** Write VE = V_cut (keep VR) so the chip's own empty detection fires at the real cut-off in every later discharge, not only once.
5. **Acceptance test is the next cut-off:** the gauge percent just before it must be near 0 %. The report already shows it.
6. Caveat: V_cut under load depends on the current (internal resistance). The tail mode (minimum load) cuts at a different logged voltage than full load. Keep the load profile of the run beside the number.

Console experiments (charger in unless noted; Hub Battery page "Backup" writes a full register dump to `/config/kefir/battery/*.txt`, read it over FTP):
1. Dump now: Status 0x00, V_empty 0x3A, CONFIG 0x1D, SHDNTIMER 0x3F, Cycles 0x17, QH 0x4D, 0x0F, 0x05, 0x06, 0xFF, 0x10, 0x23.
2. Counts while off: note QH and Status, switch the console off for 1 h on battery, switch on, dump again. Same Status (POR clear) and a changed QH means it counted.
3. Does VE act: write VE = 3.60 V with the charger in, watch 0x0F, 0x06 and the shown percent for 10 minutes, write the old 0xA05F back. A snap to 0 % means the chip anchors the zero to VE.
4. Write order: write 0x0F lower by 10 mAh, read 0x0F, 0x05, 0x06 at 0 s, 1 s, 30 s, 10 min.
5. One full calibration run with the log (cut-off voltages, the integral of the current, FullCAP before and after).
