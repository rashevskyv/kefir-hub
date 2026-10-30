# Battery calibration

Battery calibration finds the real empty point of the battery and sets the fuel gauge zero. The wizard is in Kefir Hub: see [System tools](system-tools.md#battery-calibration). The guide below is an interactive simulator, not the real console. It still shows an earlier order of the steps, so follow the screens on the console where they differ.

**Where:** [[Tools]] → [[Maintenance]] → [[Battery calibration]]

<iframe src="../battery-wizard/index.html?lang=en&v=26" title="Battery calibration screens" width="100%" height="860" style="border:0;max-width:100%"></iframe>

[Open the guide full screen](../battery-wizard/index.html?lang=en)

## Why the cut-off counts, not the percent

The percent on the screen is not a safe guide. The console may switch off at 35 %, or keep showing 0 % for an hour. The wizard therefore drains until the console itself switches off under load, and sets the zero at that point.

## Run the calibration

<!-- draft
- the order since 0.14.233: [[1. Reset the gauge]] → [[2. Cycle 1: drain, the zero, charge]] → [[3. Cycle 2: drain, charge]] → [[4. Report]] → [[5. Transfer to the other systems]]; a road map of the five steps at the foot of every page; a finished step is green with a tick and the cursor steps over it
- open [[Tools]] → [[Maintenance]] → [[Battery calibration]], read the two intro pages, press **A** on [[Start calibration]]: the Hub backs the gauge up first (the toast names the file under /config/kefir/battery/)
- step 1, [[Reset the gauge]]: at once, at any charge level, no charging first; a backup, then the factory battery model; what the gauge learned before is dropped
- step 2, cycle 1: take the charger out, press **A** on [[Start the drain]]; full load and brightness; the screen saver after 3 minutes or on Minus (right stick: brightness, left stick: moves the numbers); when the console switches off, switch it on again: the drain goes on in the tail mode (minimum load and brightness) until it does not start at all; if a low battery only puts the console to sleep and you wake it, that counts as the cut-off too ([[The console slept: the drain goes on slowly]])
- the zero: plug the charger in, switch the console on (it may need a few minutes on the charger first), open Kefir Hub; the Hub takes the last voltage before the gap in the drain log and writes it into the gauge as the zero by itself ([[The console switched off at]] X V, [[Written into the gauge: this voltage is now 0 %.]]); then the charge page opens: wait for 100 %, the cycle counts by itself from the log
- step 3, cycle 2: the same drain and charge, no zero
- step 4, [[Calibration report]]: the zero, the learned capacity against design, the verdict
- step 5, [[Transfer to the other systems]]: the calibration is noted in a ledger on the microSD; the other system (sysMMC, another emuMMC) takes it at its own next start of Kefir Hub and shows a message
- the progress is kept on the microSD: the Hub can be closed and the console used between the steps; come back through [[Tools]] → [[Maintenance]] → [[Battery calibration]]
- the console switches off and on many times during a drain: this is normal and needed; keep switching it on until it does not start at all
-->

## Move the calibration to the other system
After the report, the Hub keeps the calibration with a marker on the microSD. Start the other system and open Kefir Hub there. The calibration moves by itself and the Hub shows a message. Another emuMMC takes it at its own first start.

## Problems
**The console does not start after a cut-off.** This is normal: the battery is empty. Plug in the charger and let the console lie on it for a while. As soon as it starts, open Kefir Hub at once.

**The calibration was not moved.** The Hub keeps the marker when the write fails, or when the other system has a newer calibration or another battery model. Open the other system once more after the cause is fixed.
