# [[Battery calibration]]

Калібрування батареї знаходить справжню межу розряду батареї й ставить нуль датчика заряду. Майстер уже є в Kefir Hub: див. [Системні інструменти](system-tools.md#battery-calibration). Нижче — інтерактивний гід-симулятор, а не справжня консоль. Він ще показує давніший порядок кроків, тож там, де вони відрізняються, дійте за екранами на консолі.

**Де:** [[Tools]] → [[Maintenance]] → [[Battery calibration]]

<iframe src="../battery-wizard/index.html?lang=uk&v=26" title="Екрани калібрування батареї" width="100%" height="860" style="border:0;max-width:100%"></iframe>

[Відкрити гід на весь екран](../battery-wizard/index.html?lang=uk)

## Чому межа — відсік, а не відсоток

Відсотку датчика заряду не можна довіряти. Консоль може вимкнутися при 35 %, або показувати 0 % годину. Справжня межа — відсік: консоль сама вимикається під навантаженням. Майстер бере напругу в цю мить (від 3,00 до 3,50 В) і записує її в датчик як нуль.

## Пройти калібрування

<!-- draft
- the order since 0.14.233: [[1. Reset the gauge]] → [[2. Cycle 1: drain, the zero, charge]] → [[3. Cycle 2: drain, charge]] → [[4. Report]] → [[5. Transfer to the other systems]]; a road map of the five steps at the foot of every page; a finished step is green with a tick and the cursor steps over it
- open [[Tools]] → [[Maintenance]] → [[Battery calibration]], read the two intro pages, press **A** on [[Start calibration]]: the Hub backs the gauge up first (the toast names the file under /config/kefir/battery/)
- step 1, [[Reset the gauge]]: at once, at any charge level, no charging first; a backup, then the factory battery model; what the gauge learned before is dropped
- step 2, cycle 1: take the charger out, press **A** on [[Start the drain]]; full load and full brightness (the charger can stay in: the Hub cuts its input) down to 3.5 V under load, below it no load and the minimum brightness ([[Below 3.5 V: the load is off and the brightness is at the minimum, so the console drains slowly to a real empty.]]); a restart by hand at a charged battery is no cut-off, the drain goes on at the full load; the screen saver after 3 minutes or on Minus (right stick: brightness, left stick: moves the numbers); when the console switches off, switch it on again: the drain goes on (the same 3.5 V rule) until it does not start at all; if a low battery only puts the console to sleep and you wake it, that counts as the cut-off too ([[The console slept: the drain goes on slowly]])
- the zero: plug the charger in, switch the console on (it may need a few minutes on the charger first), open Kefir Hub; the Hub takes the last voltage before the gap in the drain log and writes it into the gauge as the zero by itself ([[The console switched off at]] X V, [[Written into the gauge: this voltage is now 0 %.]]); then the charge page opens: wait for 100 %, the cycle counts by itself from the log
- since 0.14.314: the zero is a cut-off from 2.80 to 3.40 V (with the load off below 3.5 V the console runs down to about 3.0 V); a cut-off the Hub cannot use ([[The zero was not written. A backup was saved first.]] or no last voltage in the log) ends with [[OK starts the drain again.]]: OK starts the drain at once, the next switch-off gives the zero; the page [[Empty battery is now 0 %]] goes on to the charge by itself after 10 s ([[Go on]] counts down), a button press keeps it waiting for A
- since 0.14.315: the Ultrahand notice at boot that asks to open Kefir Hub goes away by itself once the Hub is open (needs the Ultrahand of Kefir)
- since 0.14.319: a sleep in which the console charged (the charger input came back) is not a cut-off: no zero is written, [[The console charged in the sleep: the drain starts over]], and the drain goes on from the new voltage; the zero writes the counters at 1 % only while the cell is still empty (at most 3.70 V), a charged cell keeps its own %
- step 3, cycle 2: the same drain and charge, no zero
- step 4, [[Calibration report]]: the zero, the learned capacity against design, the verdict; since 0.14.310 also [[The checks]]: [[Charge counted from the switch-off to full]] (the gauge's charge counter from cycle 2's switch-off to the full battery) against the learned capacity (within 7 %, less the % the gauge showed at the switch-off: the number is the battery's; else [[The gauge did not learn right: start the calibration over.]]), and [[The gauge at cycle 2's switch-off]] (3 % or less: [[About 0 %: the zero is right.]]); both passed and a low capacity: the battery is really worn, not a calibration to run again; the calibration goes to the other systems unless the gauge's value is above the factory one, its two capacities differ by 20 % or a check failed (a worn battery measured right goes too)
- step 5, [[Transfer to the other systems]]: the calibration is noted in a ledger on the microSD; the other system (sysMMC, another emuMMC) takes it at its own next start of Kefir Hub and shows a message
- the progress is kept on the microSD: the Hub can be closed and the console used between the steps; come back through [[Tools]] → [[Maintenance]] → [[Battery calibration]]
- the console switches off and on many times during a drain: this is normal and needed; keep switching it on until it does not start at all
- after the cut-off: plug the charger in at once and do not leave the empty console without it for long; it may not start for about 20 minutes on the charger, this is normal and no sign of a fault; the Hub does not need the console switched on in time
- the charge that came in while the console slept or was off counts: the Hub notes the gauge's charge counter on every drain tick (and every 30 s in the marker) and counts what came in from the last note; a battery that is full when the Hub looks (4.15 V or more and a charging current of 600 mA or less) is written as 100 %, so the console and the Hub show 100 % at once, not a value counted from the start of the Hub
- the page header says [[Battery charge]] while the charge runs and [[Battery drain]] while the drain runs
-->

## Перенести калібрування в іншу систему
Після звіту калібрування зберігається з маркером (номер і час). Кожна інша система бере його при першому старті: з'являється тост і рядок в історії батареї. Можливі невдачі: запис не вдався (повідомлення про помилку, маркер лишається), є новіше калібрування (не перезаписується) або інша модель батареї (не застосовується).

## Проблеми
**Консоль не вмикається після відсіку.** Це означає, що крок завершено. Зарядіть консоль, а потім одразу відкрийте Kefir Hub.
