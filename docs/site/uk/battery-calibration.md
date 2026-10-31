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
- step 2, cycle 1: take the charger out, press **A** on [[Start the drain]]; full load and brightness; the screen saver after 3 minutes or on Minus (right stick: brightness, left stick: moves the numbers); when the console switches off, switch it on again: the drain goes on in the tail mode (minimum load and brightness) until it does not start at all; if a low battery only puts the console to sleep and you wake it, that counts as the cut-off too ([[The console slept: the drain goes on slowly]])
- the zero: plug the charger in, switch the console on (it may need a few minutes on the charger first), open Kefir Hub; the Hub takes the last voltage before the gap in the drain log and writes it into the gauge as the zero by itself ([[The console switched off at]] X V, [[Written into the gauge: this voltage is now 0 %.]]); then the charge page opens: wait for 100 %, the cycle counts by itself from the log
- step 3, cycle 2: the same drain and charge, no zero
- step 4, [[Calibration report]]: the zero, the learned capacity against design, the verdict
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
