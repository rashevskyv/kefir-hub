# Battery calibration: the community procedure vs the Kefir Hub wizard

Source: a Russian-language guide "Battery desync fix, extended edition" (2026-10-21, given by the owner) plus the video
"Switch OLED 8600mAh Battery Upgrade" (youtu.be/uNj7n0z4VEI; its content could not be read by the agent: the PRODINFO edit
steps are taken from the guide's text only and must be checked on a console before anything is written).

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
