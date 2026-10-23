# Battery calibration

Battery calibration finds the real empty point of the battery and sets the fuel gauge zero. The wizard is **planned**: it is not in Kefir Hub yet. The guide below walks through the screens the wizard will show. It is a simulator, not the real console.

**Where:** [[Tools]] → [[Maintenance]] → [[Battery calibration]]

<!-- draft
- Status: planned, plan F.29 f. Not in the Hub yet. Say so once, at the top.
- Why: the gauge percent cannot be trusted. The console can switch off at 35 %, or show 0 % for an hour. The real end is the cut-off: the console switches itself off at about 3.2 V under load.
- Wizard steps: 1 drain to the dead battery; 2 charge about 3 minutes, then switch on, the Hub sets the zero; 3 cycle 1 (charge to 100 %, then drain); 4 cycle 2; 5 report; 6 transfer to the other system.
- Only the next step is open. Progress is kept on the microSD, so a restart or a cut-off keeps the place.
- Drain: the charger must be out. Max load (CPU and GPU) and max brightness. Screen saver after 3 minutes or on Minus; the stick lowers the brightness. Logged to the microSD every second.
- Load: max load until the voltage first falls to 3.4 V, then minimum load, latched (the voltage rises when the load drops, so a threshold would flip back). Minimum load is skipped when the first run shows a sag below 0.05 V.
- After each cut-off, switch the console on again and open the wizard; the same step goes on. When it does not start, the step is done.
- Zero (step 2): the Hub reads counter N after switch-on and writes N minus X, X is the last logged gauge value before the cut-off. Fallback 0 when the gauge looks reset or the log is lost; the report says so. Unconfirmed on a console: the gauge keeps counting while the console is off.
- Full cycle: 100 % by the percent. The rule with the current near 0 for 5 minutes is planned.
- Report: learned capacity and verdict, the gauge percent before each cut-off, the zero line, what is written where.
- Transfer: after the report the calibration is saved with a marker (sequence number and time). Each other system takes it at its own first start: a toast and a line in the battery history. Failure cases: write fails (error toast, marker kept), a newer calibration exists (not overwritten), another battery model (not applied). The hekate hop is a manual action only.
- Open: the register for the zero (RemCap or RepSOC), the temperature limit (60 °C in the guide), the real cut-off voltage under load (3.2 V in the guide), the rule for a good calibration, the toast wording.
-->

<iframe src="../battery-wizard/index.html?lang=en" title="Battery calibration screens" width="100%" height="860" style="border:0;max-width:100%"></iframe>

[Open the guide full screen](../battery-wizard/index.html?lang=en)

## Why the cut-off counts, not the percent

The percent on the screen is not a safe guide. The console may switch off at 35 %, or keep showing 0 % for an hour. The wizard therefore drains until the console itself switches off under load, and sets the zero at that point.

## Run the calibration
1. Open [[Tools]] → [[Maintenance]] → [[Battery calibration]] and read the method.
2. Press **A** on **Start calibration**.
3. Step 1: take the charger out, then press **A** on **Start the drain**. Keep the console near you. In the screen saver, the right stick sets the brightness and the drift speed, the left stick moves the block (on this page: the sticks under the screen; keys W/S, A/D, arrows).
4. When the console switches off, switch it on again and open the wizard. The same step goes on.
5. Step 2: charge about 3 minutes, unplug the charger and switch the console on. The Hub sets the zero by itself.
6. Steps 3 and 4: charge to 100 %, unplug the charger, drain again until the console does not start.
7. Step 5: read the report, then press **A** on [[Next]].

## Move the calibration to the other system
After the report, the Hub keeps the calibration with a marker on the microSD. Start the other system and open Kefir Hub there. The calibration moves by itself and the Hub shows a message. Another emuMMC takes it at its own first start.

<!-- TODO(verify): the toast wording and the rule for a good calibration are not decided. -->

## Problems
**The console does not start after a cut-off.** The battery is too low to start. Charge it with the charger in, then switch the console on. The step is done when the console does not start even then.

**The calibration was not moved.** The Hub keeps the marker when the write fails, or when the other system has a newer calibration or another battery model. Open the other system once more after the cause is fixed.
