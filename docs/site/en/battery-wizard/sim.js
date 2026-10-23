// The console and the Hub's calibration wizard, without any DOM. Physics per simulated second.
// Rules: docs/dev/BATTERY-CALIBRATION.md, "Cutoff pipeline", "Brightness rule" and
// "The wizard remembers; the Hub need not stay open" (commit 4182c044). The model is rough; numbers are not measurements.
var SIM = (function () {
  'use strict';
  var CUT_V = 3.2, BOOT_V = 3.25, RECOVER = 0.03, I_MAX = 1.8, I_MIN = 0.35, I_SELF = 1.2;
  var LIMIT_T = 60, COOL_T = 55, SAVER_S = 180, CHG_RATE = 0.6 / 60, CHG_MA = 1296;
  var ZERO_MIN = 40; // sim seconds on the charger before a flat console starts again (the zero step)
  var BASE_T = 13 * 3600 + 17 * 60; // the clock of the Switch at the start of the page
  var SYSTEMS = ['sysMMC', 'emuA', 'emuB'];
  var SYS_NAME = { sysMMC: 'sysMMC', emuA: 'emuMMC A', emuB: 'emuMMC B' };

  function copy() { return { model: 'LiPo-3600', cycles: 0, seq: 0, time: '—' }; }
  function fresh() {
    return {
      t: 0, speed: 10, paused: false, power: true, sys: 'sysMMC', charger: false,
      q: 62, R: 100, E: 0, shift: 0, gaugeReset: false, logLost: false,
      bright: 100, brightBefore: 60, brightSaved: 60, autoB: true,
      userOn: false, userBright: 40, userSkipped: false, bMarker: { before: 60, lastSet: 100 },
      saver: false, saverAuto: false, drainRun: false, loadOff: false,
      T: 34, sagMeas: null, lines: 0, drainSec: 0, ctx: 0, minRun: 0, tail: false, pausedS: 0,
      hist: [], nextS: 0,
      charge3: 0, chargeStarted: false, chargeMode: '', fullFlag: false, cycleWait: false,
      lastTry: null, offChg: 0,
      ph: {},                // per wizard step: the phases of a cycle (charge A, drain B) and the drain choice
      hubOpen: true, askSelf: false, saidOnce: false,
      cuts: [], lastX: null, zeroInfo: null, noBoot: false,
      scr: 'tools', sub: 0, focus: 2, popup: null, toast: null, hint: '', started: false,
      cur: 1, done: [false, false, false, false, false, false, false],
      seqN: 0, copies: { sysMMC: copy(), emuA: copy(), emuB: copy() }, marker: null, ledger: [],
      applyFail: false, sysNewer: false, sysOther: false, emus: 1, fullMah: 3032, needRender: false,
      stick: { lx: 0, ly: 0, rx: 0, ry: 0 }, sx: 0, sy: 0, dAcc: 0, dMult: 1, osdUntil: 0
    };
  }
  var G = fresh();

  function clamp(v, a, b) { return Math.min(b, Math.max(a, v)); }
  function ocv(q) { return 3.2 + q / 100; }
  function counter() { return clamp(Math.round(G.q + G.E + G.shift), 0, 100); }
  function ph(k) { return G.ph[k] || (G.ph[k] = { aS: false, aD: false, aSec: 0, bD: false, armed: false, selfCut: false }); }
  function drainActive() { return G.scr === 'drain' && !G.popup && G.power && !G.charger && G.drainRun; }
  // Maximum load before the first cut-off; minimum load after every restart (the tail mode).
  function loadNow() { return drainActive() && !G.loadOff ? (G.tail ? I_MIN : I_MAX) : 0; }
  // the drain phase is armed (phase A done in a cycle; the step 1 page shown): the console runs down on its own
  function selfDraining() {
    var p = G.ph[G.cur];
    return !!p && (p.aD || p.armed) && !p.bD && G.power && !G.charger && !G.drainRun;
  }
  function systems() { return G.emus === 2 ? SYSTEMS : ['sysMMC', 'emuA']; }
  function clockText(extraSec) {
    var s = Math.floor(BASE_T + G.t + (extraSec || 0)) % 86400;
    var p = function (n) { return (n < 10 ? '0' : '') + n; };
    return p(Math.floor(s / 3600)) + ':' + p(Math.floor(s / 60) % 60);
  }
  // CPU core and GPU load levels (percent) the Hub sets: about 90 at maximum, about 15 at minimum.
  // The small wobble is for the picture only; the real levels need a reading on the console.
  function loadLevels(I, t) {
    var base = I === 0 ? 0 : (I === I_MAX ? 90 : 15), c = [];
    for (var i = 0; i < 4; i++) c.push(clamp(Math.round(base + 4 * Math.sin(t / 7 + i * 1.7)), 0, 100));
    return { c: c, g: clamp(Math.round(base + 2 * Math.sin(t / 9)), 0, 100) };
  }
  // Samples for the live charts: one point every 15 simulated seconds.
  function pushHist(V, I2) {
    var L = loadLevels(I2, G.t);
    G.hist.push({ V: V, P: V * I2, T: G.T, C1: L.c[0], C2: L.c[1], C3: L.c[2], C4: L.c[3], G: L.g, low: !!G.tail });
    if (G.hist.length > 200) G.hist.shift();
  }

  // A cut-off. The gauge reading is kept only when the Hub was open and drained (the Hub logged it).
  function cutoff(V) {
    var logged = G.hubOpen && G.drainRun;
    G.cuts.push({ n: G.cuts.length + 1, V: V, gauge: logged ? counter() : null, logged: logged, t: G.t });
    G.lastX = logged ? counter() : null;
    if (!logged) ph(G.cur).selfCut = true;
    G.power = false; G.drainRun = false; G.saver = false; G.popup = null;
    G.scr = 'black'; G.needRender = true;
  }

  // One simulated step of dt seconds. Returns the first notable event, or null.
  function step(dt) {
    var ev = null, I = loadNow();
    var target = G.power ? 25 + I * 15 : 25;
    G.T += (target - G.T) * Math.min(1, 0.003 * dt);
    if (G.T >= LIMIT_T && !G.loadOff) { G.loadOff = true; ev = 'hot'; }
    if (G.T <= COOL_T) G.loadOff = false;
    if (G.charger && G.q < 100) {
      G.q = Math.min(100, G.q + CHG_RATE * dt);
      if (G.q >= 100 && !G.fullFlag) { G.fullFlag = true; ev = ev || 'full'; }
    }
    // the zero step counts the charging time (no target)
    if (G.charger && G.chargeStarted && G.cur === 2 && !G.done[2]) G.charge3 += dt;
    // a cycle's phase A (charge to 100 %) counts while it runs, also with the Hub closed
    var pc = G.ph[G.cur];
    if (pc && G.charger && pc.aS && !pc.aD && (G.cur === 3 || G.cur === 4)) {
      pc.aSec += dt;
      if (G.q >= 100) { pc.aD = true; ev = ev || 'full'; }
    }
    if (G.charger && !G.power) G.offChg += dt;
    if (G.drainRun && G.charger) G.pausedS += dt; // the Hub's drain is on pause while the charger is in
    if (drainActive()) {
      var I2 = loadNow(), V = ocv(G.q) - I2 * G.R / 1000;
      if (G.sagMeas === null && I2 === I_MAX) G.sagMeas = I2 * G.R / 1000;
      if (G.minRun > 0) G.minRun = Math.max(0, G.minRun - dt);
      else if (V <= CUT_V) { cutoff(V); return 'cut'; }
      if (G.t >= G.nextS) { pushHist(V, I2); G.nextS = G.t + 15; }
      G.q = Math.max(0, G.q - I2 * dt / 129.6);
      G.drainSec += dt; G.lines += dt;
      if (!G.saverAuto && G.drainSec >= SAVER_S) { G.saverAuto = true; G.saver = true; ev = ev || 'saver'; }
    } else if (selfDraining()) {
      // the person uses the console; the battery runs down without the Hub (nothing logged)
      var Vs = ocv(G.q) - I_SELF * G.R / 1000;
      if (Vs <= CUT_V) { cutoff(Vs); return 'cut'; }
      G.q = Math.max(0, G.q - I_SELF * dt / 129.6);
    }
    G.t += dt;
    return ev;
  }
  // Advance by sec seconds, stop at the first event. Returns that event or null.
  function advance(sec) {
    var left = sec;
    while (left > 0) {
      var dt = Math.min(left, 1), e = step(dt);
      left -= dt;
      if (e) return e;
    }
    return null;
  }
  function jumpNext() {
    var e = advance(4 * 3600);
    G.needRender = true;
    if (!e) G.toast = { text: TX('No event in the next 4 hours.'), bad: false, until: Date.now() + 4000 };
    return e;
  }

  function powerOff() {
    if (!G.power) return;
    G.power = false; G.drainRun = false; G.saver = false; G.popup = null;
    G.scr = 'black'; G.needRender = true;
  }
  function powerOn() {
    if (G.power) return;
    // the zero step: a flat console starts only after a little charging; the Hub is not opened yet
    if (G.cur === 2 && G.chargeStarted && !G.done[2]) {
      if (G.charge3 < ZERO_MIN) { G.lastTry = 'nostart'; G.needRender = true; return; }
      G.power = true; G.noBoot = false; G.scr = 'idle'; G.lastTry = 'started'; restoreBright(); G.needRender = true;
      return;
    }
    if (ocv(G.q) + RECOVER < BOOT_V && !G.charger) { G.noBoot = true; G.needRender = true; return; }
    G.power = true; G.noBoot = false;
    // a self drain ended: the console is on, the Hub may stay closed (the wizard asks when it is opened)
    var p = G.ph[G.cur];
    if (p && p.selfCut && !p.bD) { G.scr = 'home'; G.needRender = true; return; }
    G.minRun = 60; // a boot runs at least a minute before the next cut-off
    G.lastTry = null;
    bootScreen();
    restoreBright();
  }
  // The Hub is opened (Tools > Battery calibration; the wizard reads its marker).
  function openHub() {
    G.hubOpen = true;
    if (!G.power) return;
    var p = G.ph[G.cur];
    if (G.cur === 2 && G.chargeStarted && !G.done[2]) { setZero(); G.scr = 'zero'; G.focus = 0; G.needRender = true; return; }
    if (p && !p.bD && p.selfCut && G.charger) { G.askSelf = true; G.scr = 'start'; G.sub = G.cur; G.focus = 0; G.needRender = true; return; }
    bootScreen();
  }
  function closeHub() {
    G.hubOpen = false; G.drainRun = false; G.saver = false; G.popup = null;
    if (G.power) G.scr = 'home'; G.needRender = true;
  }
  // Drain phase choice: the Hub's mechanism, or the person uses the console (the Hub may be closed).
  function drainChoice() { drainStart(); }
  // "Консоль вимкнулась від розряду?": Так completes the phase, Ні starts the choice again.
  function selfAnswer(yes) {
    G.askSelf = false;
    if (yes) { ph(G.cur).bD = true; complete(G.cur); return; }
    ph(G.cur).selfCut = false; G.scr = 'start'; G.sub = G.cur; G.needRender = true;
  }
  function setBright(v) { G.bright = v; }
  // The brightness rule: lowered only while the tail drain runs. With the charger in the Hub gives the person's
  // brightness and auto-brightness back at once. A value the person changed himself (not the last one the Hub set) stays.
  function restoreBright() {
    if (!G.charger || G.autoB) return;
    if (G.userOn && G.bright !== G.bMarker.lastSet) { G.userSkipped = true; G.needRender = true; return; }
    G.bright = G.brightSaved; G.autoB = true; G.tail = false; G.userSkipped = false; G.needRender = true;
  }
  function bootSystem(sys) {
    if (G.power) powerOff();
    G.sys = sys;
    powerOn();
    if (!G.power) G.needRender = true;
  }
  // The Hub opens. A drain that was cut off goes on by itself at once, in the tail mode (minimum brightness, minimal load).
  function bootScreen() {
    var tr = transferCheck(G.sys);
    if (G.cur === 2) { G.scr = 'start'; G.sub = 2; }
    else if (G.cur === 1 || G.cur === 3 || G.cur === 4) {
      if (G.drainSec > 0 && G.ctx === G.cur && G.drainRun === false && !(G.ph[G.cur] && G.ph[G.cur].selfCut)) {
        G.scr = 'drain'; G.drainRun = true; G.saver = false; G.hubOpen = true;
        G.tail = G.cuts.length > 0; G.bright = G.tail ? 1 : 100; G.autoB = false;
        if (G.tail) G.bMarker.lastSet = 1;
      }
      else { G.scr = 'start'; G.sub = G.cur; }
    }
    else { G.scr = G.started ? 'steps' : 'tools'; }
    G.focus = 0; G.popup = null;
    if (tr) G.toast = tr;
    G.needRender = true;
  }
  // "Консоль не вмикається": the Hub's drain step is done; the console stays off until the person charges it.
  function consoleNoBoot() {
    ph(G.cur).bD = true;
    G.done[G.cur] = true; G.cur = G.cur + 1; G.noBoot = false;
    G.drainRun = false; G.popup = null; G.scr = 'black'; G.needRender = true;
  }

  function complete(k) {
    G.done[k] = true; G.cur = k + 1; G.drainRun = false; G.noBoot = false;
    if (k === 1 || k === 3 || k === 4) ph(k).bD = true;
    G.chargeMode = ''; G.cycleWait = false; G.focus = 0; G.scr = 'steps'; G.popup = null;
    if (k === 2) { G.bright = G.brightSaved; G.autoB = true; G.tail = false; } // the Hub puts the console back as it was
    G.needRender = true;
  }

  function drainStart() {
    if (G.ctx !== G.cur) { // a new cycle: the drain counts from zero
      if (G.cur === 1) { G.brightSaved = G.brightBefore; G.bMarker = { before: G.brightBefore, lastSet: 100 }; }
      else G.bMarker.lastSet = 100; // the Hub sets the maximum for the drain
      G.ctx = G.cur; G.drainSec = 0; G.lines = 0; G.loadOff = false; G.pausedS = 0;
      G.saverAuto = false; G.saver = false; G.sagMeas = null;
      G.tail = false; G.bright = 100; G.hist = []; G.nextS = G.t;
    }
    G.autoB = false; // the Hub turns the auto-brightness sensor off for the drain
    G.drainRun = true; G.hubOpen = true; G.scr = 'drain'; G.popup = null; G.focus = 0; G.needRender = true;
  }
  function stopDrain() {
    G.drainRun = false; G.saver = false; G.popup = null; G.scr = 'start'; G.sub = G.cur;
    G.needRender = true;
  }
  // Phase A (charge to 100 %) starts: the Hub writes the start into the marker.
  function chargeStart(mode) {
    G.chargeMode = mode; G.fullFlag = false; G.scr = 'charge'; G.popup = null; G.focus = 0;
    if (mode === 'zero') G.chargeStarted = true;
    else { G.charge3 = 0; ph(G.cur).aS = true; }
    G.needRender = true;
  }
  function toggleCharger() {
    G.charger = !G.charger;
    if (G.charger) {
      G.pausedS = 0; G.offChg = 0;
      restoreBright(); // the charger is in: the brightness is the person's again, at once
      // the zero step starts counting the charge as soon as the charger goes in, even with the console off
      if (G.cur === 2 && !G.done[2] && !G.chargeStarted) { G.chargeStarted = true; G.charge3 = 0; }
      if (G.power && G.cur === 2 && G.chargeStarted && G.scr === 'start') G.scr = 'charge';
    }
    G.needRender = true;
  }

  function setZero() {
    var N = G.gaugeReset ? 100 : counter(), X = G.logLost ? null : G.lastX, result, note = '';
    var last = G.cuts[G.cuts.length - 1];
    if (G.logLost) { result = 0; note = 'log'; X = null; }
    else if (X === null) { result = 0; note = 'nolog'; }
    else if (N >= 99) { result = 0; note = 'reset'; X = X; }
    else result = clamp(N - X, 0, 100);
    G.shift = result - (G.q + G.E);
    G.zeroInfo = { N: N, X: X, result: result, note: note, V: last ? last.V : null };
  }

  function calGood() {
    var logged = G.cuts.filter(function (c) { return c.logged; });
    var last = logged[logged.length - 1];
    return !!G.zeroInfo && !G.zeroInfo.note && G.done[3] && G.done[4] && (!last || last.gauge <= 2);
  }
  function reportNext() {
    var from = G.sys, seq = ++G.seqN, time = clockText(), good = calGood();
    G.copies[from] = { model: 'LiPo-3600', cycles: 2, seq: seq, time: time };
    var pending = {};
    systems().forEach(function (s) { if (s !== from) pending[s] = true; });
    G.marker = { from: from, seq: seq, time: time, model: 'LiPo-3600', cycles: 2, good: good, pending: pending };
    G.ledger.push({ kind: 'made', seq: seq, from: from, time: time });
    G.done[5] = true; G.cur = 6; G.scr = 'transfer'; G.focus = 0; G.needRender = true;
  }
  // The Hub start in system sys: apply the marker if it is safe. Returns a toast or null.
  function transferCheck(sys) {
    var m = G.marker, c = G.copies[sys], toast = null;
    if (!m || !m.pending[sys]) return null;
    function done(text, bad) { return { text: text, bad: bad, until: Date.now() + 4500 }; }
    if (c.seq > m.seq) {
      m.pending[sys] = false;
      toast = done(TX('%s already has a newer calibration. Not overwritten.', SYS_NAME[sys]), false);
    } else if (c.model !== m.model) {
      toast = done(TX('Another battery model: the calibration was not transferred.'), false);
    } else if (!m.good) {
      toast = done(TX('The calibration did not pass the check. Not transferred.'), false);
    } else if (G.applyFail) {
      toast = done(TX('Calibration was not transferred: the write failed. The marker is kept.'), true);
    } else {
      G.copies[sys] = { model: m.model, cycles: m.cycles, seq: m.seq, time: clockText() };
      m.pending[sys] = false;
      G.ledger.push({ kind: 'written', seq: m.seq, from: m.from, sys: sys, time: clockText() });
      toast = done(TX('Battery calibration was transferred from %s.', SYS_NAME[m.from]), false);
    }
    if (Object.keys(m.pending).every(function (k) { return !m.pending[k]; })) {
      G.marker = null;
      if (G.cur === 6) G.done[6] = true;
    }
    return toast;
  }

  return {
    G: G, CUT_V: CUT_V, BOOT_V: BOOT_V, I_MAX: I_MAX, I_MIN: I_MIN, I_SELF: I_SELF, CHG_MA: CHG_MA, ZERO_MIN: ZERO_MIN, SYS_NAME: SYS_NAME,
    ocv: ocv, counter: counter, loadNow: loadNow, loadLevels: loadLevels, drainActive: drainActive, clockText: clockText,
    ph: ph, systems: systems, step: step, advance: advance, jumpNext: jumpNext,
    powerOff: powerOff, powerOn: powerOn, openHub: openHub, closeHub: closeHub, drainChoice: drainChoice,
    selfAnswer: selfAnswer, restoreBright: restoreBright, bootSystem: bootSystem, consoleNoBoot: consoleNoBoot,
    complete: complete, drainStart: drainStart, stopDrain: stopDrain, chargeStart: chargeStart,
    toggleCharger: toggleCharger, reportNext: reportNext, setZero: setZero, setBright: setBright,
    saverTick: function (dt) {
      // screensaver.cpp: the left stick moves the block (450 / 260 px limits), right stick up/down brightness
      // (40 %/s at full stick), left/right the drift speed (0.1..8).
      var s = G.stick;
      if (!G.saver) return;
      if (s.lx || s.ly) { G.sx = clamp(G.sx + s.lx * 450 * dt, -450, 450); G.sy = clamp(G.sy - s.ly * 450 * dt, -260, 260); }
      if (s.ry) { G.bright = clamp(G.bright + s.ry * 40 * dt, 1, 100); G.osdUntil = Date.now() + 1200; }
      if (s.rx) G.dMult = clamp(G.dMult + s.rx * 1.5 * dt, 0.1, 8);
      G.dAcc += dt * G.dMult;
    },
    saverCentre: function () {
      return { x: Math.round(640 + Math.sin(G.dAcc * 0.021) * 170 + G.sx), y: Math.round(360 + Math.cos(G.dAcc * 0.013) * 90 + G.sy) };
    },
    reset: function () { G = fresh(); SIM.G = G; }
  };
})();
