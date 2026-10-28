# Battery calibration

Battery calibration finds the real empty point of the battery and sets the fuel gauge zero. The wizard is in Kefir Hub: see [System tools](system-tools.md#battery-calibration). The guide below is an interactive simulator, not the real console. It still shows an earlier order of the steps, so follow the screens on the console where they differ.

**Where:** [[Tools]] → [[Maintenance]] → [[Battery calibration]]

<iframe src="../battery-wizard/index.html?lang=en&v=26" title="Battery calibration screens" width="100%" height="860" style="border:0;max-width:100%"></iframe>

[Open the guide full screen](../battery-wizard/index.html?lang=en)

## Why the cut-off counts, not the percent

The percent on the screen is not a safe guide. The console may switch off at 35 %, or keep showing 0 % for an hour. The wizard therefore drains until the console itself switches off under load, and sets the zero at that point.

## Run the calibration
1. Open [[Tools]] → [[Maintenance]] → [[Battery calibration]] and read the method.
2. Press **A** on **Start calibration**.
3. Step 1: take the charger out, then press **A** on **Start the drain**. Keep the console near you. In the screen saver, the right stick sets the brightness and the drift speed, the left stick moves the block (on this page: the sticks under the screen; keys W/S, A/D, arrows).
4. When the console switches off, switch it on again and open the wizard. The same step goes on.

!!! warning
    The console will switch off and on many times. This is normal and needed. Keep switching it on each time, until it does not start at all. This can take a long time.
5. Step 2: plug in the charger. The console may not start at once: let it lie on the charger for a while. As soon as it starts, open Kefir Hub at once. Do not wait. The Hub sets the zero by itself.
6. Steps 3 and 4 have two phases each. Phase A: plug in the charger and press **A** on **Start charging** until it reaches 100 %. You may leave the console on the charger or use it meanwhile. The screensaver also runs during the charge after 3 minutes without input, or on Minus, so the screen does not burn in. Phase B: press **A** on **Start the drain**. You may also drain the console yourself: play, or leave it on, and close the Hub. Then switch it on with the charger in and open Kefir Hub: it asks whether that was the drain. The wizard remembers the progress: you can close the Hub and come back later through [[Tools]] → [[Maintenance]] → [[Battery calibration]].
7. Step 5: read the report, then press **A** on [[Next]].

## Move the calibration to the other system
After the report, the Hub keeps the calibration with a marker on the microSD. Start the other system and open Kefir Hub there. The calibration moves by itself and the Hub shows a message. Another emuMMC takes it at its own first start.

<!-- TODO(verify): the toast wording and the rule for a good calibration are not decided. -->

## Problems
**The console does not start after a cut-off.** This is normal: the battery is empty. Plug in the charger and let the console lie on it for a while. As soon as it starts, open Kefir Hub at once.

**The calibration was not moved.** The Hub keeps the marker when the write fails, or when the other system has a newer calibration or another battery model. Open the other system once more after the cause is fixed.
