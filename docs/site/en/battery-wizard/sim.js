// The console and the Hub's calibration wizard, without any DOM. Physics per simulated second.
// Rules: docs/dev/BATTERY-CALIBRATION.md, "Cutoff pipeline" and "Transfer to the other MMC".
// The model is rough (the cell is a straight line from 3.2 V to 4.2 V). Numbers are not measurements.
var SIM = (function () {
  'use strict';
  var CUT_V = 3.2, BOOT_V = 3.25, RECOVER = 0.03, LATCH_V = 3.4, I_MAX = 1.8, I_MIN = 0.35;
  var LIMIT_T = 60, COOL_T = 55, SAVER_S = 180, CHG_RATE = 0.6 / 60, CHG_MA = 1296, CHG3_S = 180;
  var BASE_T = 13 * 3600 + 17 * 60; // the clock of the Switch at the start of the page
  var SYSTEMS = ['sysMMC', 'emuA', 'emuB'];
  var SYS_NAME = { sysMMC: 'sysMMC', emuA: 'emuMMC A', emuB: 'emuMMC B' };

  function copy() { return { model: 'LiPo-3600', cycles: 0, seq: 0, time: '—' }; }
  function fresh() {
    return {
      t: 0, speed: 10, paused: false, power: true, sys: 'sysMMC', charger: false,
      q: 62, R: 100, E: 0, shift: 0, gaugeReset: false, logLost: false,
      bright: 100, saver: false, saverAuto: false, drainRun: false, latched: false, loadOff: false,
      T: 34, sagMeas: null, lines: 0, drainSec: 0, ctx: 0, minRun: 0,
      charge3: 0, chargeStarted: false, chargeMode: '', fullFlag: false, cycleWait: false,
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
  function tailSkip() { return I_MAX * G.R / 1000 < 0.05; }
  function drainActive() { return G.scr === 'drain' && !G.popup && G.power && !G.charger && G.drainRun; }
  function loadNow() { return drainActive() && !G.loadOff ? (G.latched ? I_MIN : I_MAX) : 0; }
  function systems() { return G.emus === 2 ? SYSTEMS : ['sysMMC', 'emuA']; }
  function clockText(extraSec) {
    var s = Math.floor(BASE_T + G.t + (extraSec || 0)) % 86400;
    var p = function (n) { return (n < 10 ? '0' : '') + n; };
    return p(Math.floor(s / 3600)) + ':' + p(Math.floor(s / 60) % 60);
  }

  function cutoff(V) {
    G.cuts.push({ n: G.cuts.length + 1, V: V, gauge: counter(), ctx: G.ctx, t: G.t });
    G.lastX = counter();
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
    if (G.charger && G.charge3 > 0) {
      G.charge3 = Math.max(0, G.charge3 - dt);
      if (!G.charge3) ev = ev || 'chargeDone';
    }
    if (drainActive()) {
      var I2 = loadNow(), V = ocv(G.q) - I2 * G.R / 1000;
      if (G.sagMeas === null && I2 === I_MAX) G.sagMeas = I2 * G.R / 1000;
      if (G.minRun > 0) G.minRun = Math.max(0, G.minRun - dt);
      else if (V <= CUT_V) { cutoff(V); return 'cut'; }
      if (!G.latched && I2 === I_MAX && V <= LATCH_V && !tailSkip()) { G.latched = true; ev = ev || 'latch'; }
      G.q = Math.max(0, G.q - I2 * dt / 129.6);
      G.drainSec += dt; G.lines += dt;
      if (!G.saverAuto && G.drainSec >= SAVER_S) { G.saverAuto = true; G.saver = true; ev = ev || 'saver'; }
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
    if (!e) G.toast = { text: 'No event in the next 4 hours.', bad: false, until: Date.now() + 4000 };
    return e;
  }

  function powerOff() {
    if (!G.power) return;
    G.power = false; G.drainRun = false; G.saver = false; G.popup = null;
    G.scr = 'black'; G.needRender = true;
  }
  function powerOn() {
    if (G.power) return;
    if (ocv(G.q) + RECOVER < BOOT_V && !G.charger) { G.noBoot = true; G.needRender = true; return; }
    G.power = true; G.noBoot = false;
    G.minRun = 60; // a boot runs at least a minute before the next cut-off
    bootScreen();
  }
  function bootSystem(sys) {
    if (G.power) powerOff();
    G.sys = sys;
    powerOn();
    if (!G.power) G.needRender = true;
  }
  function bootScreen() {
    var tr = transferCheck(G.sys), resumed = false;
    if (G.cur === 2 && G.chargeStarted && G.charge3 === 0) { setZero(); G.scr = 'zero'; }
    else if (G.cur === 1 || G.cur === 3 || G.cur === 4) { resumed = G.drainSec > 0; G.scr = 'start'; G.sub = G.cur; }
    else { G.scr = G.started ? 'steps' : 'tools'; }
    G.focus = 0; G.popup = null;
    if (tr) G.toast = tr;
    else if (resumed) G.toast = { text: TX('Continuing the drain'), bad: false, until: Date.now() + 4000 };
    G.needRender = true;
  }
  function consoleNoBoot() { complete(G.cur); G.noBoot = false; }

  function complete(k) {
    G.done[k] = true; G.cur = k + 1; G.drainRun = false; G.noBoot = false;
    G.chargeMode = ''; G.cycleWait = false; G.focus = 0; G.scr = 'steps'; G.popup = null;
    G.needRender = true;
  }

  function drainStart() {
    if (G.ctx !== G.cur) { // a new cycle: the drain counts from zero
      G.ctx = G.cur; G.drainSec = 0; G.lines = 0; G.latched = false; G.loadOff = false;
      G.saverAuto = false; G.saver = false; G.sagMeas = null;
    }
    G.drainRun = true; G.scr = 'drain'; G.popup = null; G.focus = 0; G.needRender = true;
  }
  function stopDrain() {
    G.drainRun = false; G.saver = false; G.popup = null; G.scr = 'start'; G.sub = G.cur; G.resumed = true;
    G.needRender = true;
  }
  function chargeStart(mode) {
    G.chargeMode = mode; G.fullFlag = false; G.scr = 'charge'; G.popup = null; G.focus = 0;
    if (mode === 'zero') { G.charge3 = CHG3_S; G.chargeStarted = true; }
    else G.charge3 = 0;
    G.needRender = true;
  }
  function toggleCharger() {
    G.charger = !G.charger;
    if (!G.charger && G.cycleWait && (G.cur === 3 || G.cur === 4)) { G.cycleWait = false; drainStart(); }
    G.needRender = true;
  }

  function setZero() {
    var N = G.gaugeReset ? 100 : counter(), X = G.logLost ? null : G.lastX, result, note = '';
    if (X === null) { result = 0; note = 'log'; }
    else if (N >= 99) { result = 0; note = 'reset'; }
    else result = clamp(N - X, 0, 100);
    G.shift = result - (G.q + G.E);
    G.zeroInfo = { N: N, X: X, result: result, note: note };
  }

  function calGood() {
    var last = G.cuts[G.cuts.length - 1];
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

  // The screen saver, as screensaver.cpp: the left stick moves the block (450 / 260 px limits), the
  // right stick up/down changes the brightness (40 %/s at full stick), left/right the drift speed (0.1..8).
  function saverTick(dt) {
    var s = G.stick;
    if (!G.saver) return;
    if (s.lx || s.ly) {
      G.sx = clamp(G.sx + s.lx * 450 * dt, -450, 450);
      G.sy = clamp(G.sy - s.ly * 450 * dt, -260, 260);
    }
    if (s.ry) { G.bright = clamp(G.bright + s.ry * 40 * dt, 1, 100); G.osdUntil = Date.now() + 1200; }
    if (s.rx) G.dMult = clamp(G.dMult + s.rx * 1.5 * dt, 0.1, 8);
    G.dAcc += dt * G.dMult;
  }
  // Block centre on the 1280x720 stage, whole pixels, as Draw() computes it.
  function saverCentre() {
    return { x: Math.round(640 + Math.sin(G.dAcc * 0.021) * 170 + G.sx), y: Math.round(360 + Math.cos(G.dAcc * 0.013) * 90 + G.sy) };
  }

  return {
    saverTick: saverTick, saverCentre: saverCentre,
    G: G, CUT_V: CUT_V, BOOT_V: BOOT_V, I_MAX: I_MAX, I_MIN: I_MIN, CHG_MA: CHG_MA, SYS_NAME: SYS_NAME,
    ocv: ocv, counter: counter, loadNow: loadNow, drainActive: drainActive, clockText: clockText,
    systems: systems, step: step, advance: advance, jumpNext: jumpNext,
    powerOff: powerOff, powerOn: powerOn, bootSystem: bootSystem, consoleNoBoot: consoleNoBoot,
    complete: complete, drainStart: drainStart, stopDrain: stopDrain, chargeStart: chargeStart,
    toggleCharger: toggleCharger, reportNext: reportNext, setZero: setZero,
    reset: function () { G = fresh(); SIM.G = G; }
  };
})();
