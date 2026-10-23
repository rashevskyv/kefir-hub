// The Switch screens (1280x720, Kefir Hub look), input, the side panel "Світ" and the lists at the bottom.
(function () {
  'use strict';
  var F = [];            // actions of the focusable items on the current screen
  var ACTIVE_F = -1;     // focus index of the active step row (its A label is Begin step)
  var START1 = [         // the four paragraphs of the step 1 start page
    'Now the Hub drains the battery to the very end, until the console switches itself off. To keep the drain steady, the Hub loads the CPU and the GPU fully and sets the highest brightness.',
    'After 3 minutes (or at once, on Minus), the screen saver starts: the screen goes black and the numbers slowly move across it. This stops the pixels from burning in. The right stick changes the brightness, the left stick moves the numbers.',
    'When the console switches off, that is normal. Just switch it on again. The Hub goes on with the drain by itself, now at low brightness and weak load, to squeeze out the last percent. Keep switching it on until it does not start at all.',
    'Then plug the charger in for 3 minutes: the wizard moves to the next step.'];
  var STEPK = ['', 'Drain to the dead battery', 'Charge 3 min and zero', 'Cycle 1', 'Cycle 2', 'Report', 'Transfer to the other MMC'];
  function G() { return SIM.G; }
  function esc(s) { return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;'); }
  function S2(en, uk) { return LANG === 'uk' ? uk : en; }
  function noop() {}
  function fi(inner, act, cls) { var i = F.length; F.push(act); return '<div class="fi ' + (cls || '') + '" data-f="' + i + '">' + inner + '</div>'; }
  function row(title, desc, act) { return fi('<span class="rt">' + esc(title) + '</span><span class="rd">' + esc(desc || '') + '</span>', act, 'row'); }
  function grp(t) { return '<div class="grp">' + esc(t) + '</div>'; }
  function txt(s, cls) { return '<div class="' + (cls || 'txt') + '">' + esc(s) + '</div>'; }
  function kv(label, key) { return '<div class="kv"><span>' + esc(label) + '</span><b data-k="' + key + '"></b></div>'; }
  function name(s) { return SIM.SYS_NAME[s]; }
  function go(scr, sub) {
    G().scr = scr; if (sub !== undefined) G().sub = sub;
    G().focus = scr === 'tools' ? 2 : 0; G().popup = null; G().hint = '';
    if (scr !== 'drain') G().saver = false;
    renderAll();
  }
  function openStep(i) {
    if (i <= 4) go('start', i); else if (i === 5) go('report'); else go('transfer');
  }
  function bar(list) {
    return list.map(function (b) {
      return '<span class="bi ' + (b[2] || '') + '"><i class="gl">' + esc(b[0]) + '</i><span class="bl">' + esc(b[1]) + '</span></span>';
    }).join('');
  }
  // one live chart: sparklines of the recent samples (drawn in live), its value, reference lines and a marker
  function chart(title, vk, ymin, ymax, refs, lines, mark) {
    return '<div class="chart"><div class="ct"><span>' + esc(title) + '</span><b data-k="' + vk + '"></b></div>' +
      '<div class="cbox"><svg viewBox="0 0 600 110" preserveAspectRatio="none" data-spark="1" data-min="' + ymin + '" data-max="' + ymax + '">' +
      refs.map(function (r) { return '<line class="ref" data-refv="' + r + '" x1="0" x2="600"/>'; }).join('') +
      lines.map(function (k, i) { return '<polyline class="ln' + i + '" data-line="' + k + '"/>'; }).join('') + '</svg>' +
      (mark ? '<div class="mlab" data-mlab="1" hidden>' + esc(TX('Load lowered')) + '</div>' : '') + '</div></div>';
  }
  function legendOf(items) {
    return items.map(function (it, i) { return '<span class="lg"><i class="dot ln' + i + '"></i>' + esc(it[0]) + ' <b data-k="' + it[1] + '"></b> %</span>'; }).join('');
  }
  function sparks() {
    var hist = SIM.G.hist, n = hist.length;
    document.querySelectorAll('[data-spark]').forEach(function (svg) {
      var min = +svg.dataset.min, max = +svg.dataset.max;
      var yOf = function (v) { return (110 - (Math.min(max, Math.max(min, v)) - min) / (max - min) * 110).toFixed(1); };
      svg.querySelectorAll('[data-line]').forEach(function (pl) {
        var k = pl.dataset.line, pts = [];
        for (var i = 0; i < n; i++) pts.push((n > 1 ? i / (n - 1) * 600 : 600).toFixed(1) + ',' + yOf(hist[i][k]));
        pl.setAttribute('points', pts.join(' '));
      });
      svg.querySelectorAll('[data-refv]').forEach(function (ln) { var y = yOf(+ln.dataset.refv); ln.setAttribute('y1', y); ln.setAttribute('y2', y); });
    });
    // the marker: the first sample taken after the load was lowered (the tail mode)
    var mk = document.querySelector('[data-mlab]');
    if (mk) {
      var at = -1;
      for (var j = 0; j < n; j++) if (hist[j].low && (j === 0 || !hist[j - 1].low)) { at = j; break; }
      mk.hidden = at < 0;
      if (at >= 0) {
        var pc = n > 1 ? at / (n - 1) * 100 : 100;
        mk.style.left = pc.toFixed(1) + '%';
        mk.style.transform = pc < 15 ? 'none' : pc > 85 ? 'translateX(-100%)' : 'translateX(-50%)';
      }
    }
  }
  function battery(noText) {
    return '<div class="bbox"><div class="bfill" data-bw="1"></div>' + (noText ? '' : '<div class="bpct" data-k="pctText"></div>') + '</div><div class="nub"></div>';
  }
  function ctxLabel() {
    var c = G().ctx || G().cur;
    return c === 1 ? TX('Drain to the dead battery') : c === 3 ? TX('First cycle') : TX('Second cycle');
  }

  // Each view returns body html, bar list and the screen title and subtitle.
  var VIEWS = {
    tools: function () {
      return { title: TX('Tools'), sub: '', bar: [['B', TX('Back')], ['A', TX('Open')]], body:
        grp(TX('Diagnostics')) + row(TX('System information'), TX('Firmware, Atmosphere and console data.'), noop) +
        grp(TX('Maintenance')) + row(TX('Battery'), TX('Charge sensor state, system copy and backup.'), function () { go('battery'); }) +
        row(TX('Battery calibration'), TX('Step by step: drain, charge, report. The cut-off is the real end.'),
          function () { go(G().started ? 'steps' : 'intro'); }) };
    },
    battery: function () {
      var h = '', led = G().ledger.filter(function (l) { return l.kind === 'written'; });
      led.forEach(function (l) { h += '<div class="kv"><span>' + esc(TX('Transferred from %s', name(l.from))) + '</span><b>' + l.time + '</b></div>'; });
      return { title: TX('Battery'), sub: TX('Charge sensor state, system copy and backup.'), bar: [['B', TX('Back')], ['A', TX('Open')]], body:
        kv(TX('Battery charge'), 'psm') + kv(TX('Voltage'), 'V') + h +
        fi('<span class="rt">' + esc(TX('Calibration history')) + '</span><span class="rd">' + esc(TX('Calibration ledger')) + '</span>',
          function () { go('ledger'); }, 'row') };
    },
    ledger: function () {
      var h = '';
      G().ledger.forEach(function (l) {
        var what = l.kind === 'made' ? TX('Made in %s', name(l.from)) : TX('Written to %s', name(l.sys));
        h += '<div class="kv"><span>#' + l.seq + ' · ' + esc(what) + '</span><b>' + l.time + '</b></div>';
      });
      return { title: TX('Calibration ledger'), sub: '', bar: [['B', TX('Back')]],
        body: h || txt(TX('No calibration yet.')) };
    },
    intro: function () {
      return { title: TX('Battery calibration'), sub: TX('Step by step: drain, charge, report. The cut-off is the real end.'),
        bar: [['B', TX('Back')], ['A', TX('Start calibration')]], body:
        txt(TX('Why this is needed'), 'head') +
        txt(TX('With time the charge gauge goes wrong. The console can switch off at 35 %, or show 0 % for a whole hour. The wizard teaches the gauge again, so the percent is honest.'), 'para s') +
        txt(TX('How it works'), 'head') +
        txt(TX('We drain the battery to the very end: until the console switches itself off (about 3.2 V under load). That is the real zero. Then we charge and drain it fully, two times.'), 'para s') +
        txt(TX('What you do'), 'head') +
        txt(TX('Take the charger out and leave the console on a table: the Hub gives it a full load by itself. When the console switches off, plug the charger in for 3 minutes and switch it on. Open the wizard: it goes on from the same place.'), 'para s') +
        txt(TX('How long it takes'), 'head') +
        txt(TX('Several hours, with breaks for charging. Your games and saves are not touched. Before it writes to the gauge, the Hub makes a backup.'), 'para s') +
        '<div class="btnrow">' + fi(esc(TX('Start calibration')), function () { G().started = true; go('steps'); }, 'btn') + '</div>' };
    },
    steps: function () {
      var g = G(), body = '';
      for (var i = 1; i <= 6; i++) {
        var state = g.done[i] ? 'done' : (g.cur === i ? 'active' : 'locked');
        var label = state === 'done' ? TX('Done') : state === 'active' ? TX('Active') : TX('Locked');
        var inner = '<span class="num">' + i + '</span><span class="rt">' + esc(TX(STEPK[i])) + '</span><span class="stt ' + state + '">' + esc(label) + '</span>';
        if (state === 'active') ACTIVE_F = F.length;
        body += state === 'locked' ? '<div class="fi row dim">' + inner + '</div>'
          : fi(inner, state === 'active' ? function (k) { return function () { openStep(k); }; }(i) : noop, 'row');
      }
      return { title: TX('Battery calibration'), sub: g.cur > 5 ? TX('Calibration finished') : TX('Step %d of 5', g.cur),
        bar: [['B', TX('Back')], ['A', TX('Open'), 'bA']], body: body };
    },
    start: function () {
      var i = G().sub || G().cur, g = G(), checks = [], btn, h = '';
      if (i === 1) {
        checks.push([TX('Charger is out'), !g.charger]);
        btn = [TX('Start the drain'), function () { if (!g.charger) SIM.drainStart(); else g.hint = TX('Charger is in: take it out first.'); }];
        START1.forEach(function (p) { h += '<p class="para s">' + esc(TX(p)) + '</p>'; });
      } else {
        checks.push([TX('Plug the charger in.'), g.charger]);
        var what = i === 2 ? [TX('Charge 3 min. Then unplug the charger and switch the console on.'), TX('The 3 min of charge stay in the counter.')]
          : [TX('Charge to 100 %. Then unplug the charger: the drain starts by itself.')];
        what.forEach(function (t) { h += txt(t, 'li'); });
        btn = [TX('Start charging'), function () {
          if (!g.charger) { g.hint = TX('Plug the charger in.'); renderAll(); return; }
          SIM.chargeStart(i === 2 ? 'zero' : 'full');
        }];
      }
      var pre = '';
      checks.forEach(function (c) { pre += '<div class="chk ' + (c[1] ? 'ok' : 'no') + '">' + (c[1] ? '✓' : '✗') + ' ' + esc(c[0]) + '</div>'; });
      if (g.hint) pre += '<div class="hint">' + esc(g.hint) + '</div>';
      h = pre + h + '<div class="btnrow">' + fi(esc(btn[0]), btn[1], 'btn') + '</div>';
      return { title: TX(STEPK[i]), sub: TX('Step %d of 5', i), bar: [['B', TX('Back')], ['A', btn[0]]], body: h };
    },
    charge: function () {
      var g = G(), body = '<div class="bigbat">' + battery() + '</div>' +
        '<div class="kvs">' + kv(TX('Charging:'), 'charging') + kv(TX('Current'), 'curr') + '</div>' +
        txt(TX('Keep the screen on while charging.'));
      if (g.chargeMode === 'zero') {
        body += '<div class="kvs">' + kv(TX('Left to charge:'), 'left') + '</div>';
        if (g.charge3 === 0) body += txt(TX('Unplug the charger, switch the console on. The Hub sets the zero by itself.'), 'txt warn');
      } else if (g.fullFlag) body += txt(TX('Full: 100 %. Unplug the charger: the drain starts by itself.'), 'txt ok');
      return { title: TX('Battery charge'), sub: TX(STEPK[g.cur]), bar: [['B', TX('Back')]], body: body };
    },
    zero: function () {
      var z = G().zeroInfo || { N: 0, X: null, result: 0, note: '', V: null };
      var box = function (l, v) { return '<div class="zbox"><small>' + esc(l) + '</small><b>' + v + '</b></div>'; };
      var body = txt(TX('The console switched off at the cut-off, and you charged it for about 3 minutes.'), 'li') +
        txt(TX('Now the Hub sets this point as 0 %.'), 'li') +
        '<div class="kv"><span>' + esc(TX('Last voltage in the log before the cut-off')) + '</span><b>' + (z.V === null ? '—' : z.V.toFixed(2) + ' V') + '</b></div>' +
        '<div class="zrow">' + box(TX('Gauge counter N'), z.N + ' %') + box(TX('Last logged value X'), z.X === null ? '—' : z.X + ' %') +
        box(TX('Result N − X'), z.result + ' %') + '</div>' +
        txt(TX('Factory model written'), 'li ok') + txt(TX('The backup was made.'), 'li ok') +
        (z.note === 'reset' ? txt(TX('The gauge looks reset. 0 is written.'), 'li warn')
          : z.note === 'log' ? txt(TX('The log is lost. 0 is written.'), 'li warn') : txt(TX('The 3 min of charge stay in the counter.'), 'li')) +
        '<div class="btnrow">' + fi(esc(TX('Next')), function () { SIM.complete(2); go('start', 3); }, 'btn') + '</div>';
      return { title: TX('Gauge zero'), sub: TX('Step %d of 5', 2), bar: [['A', TX('Next')]], body: body };
    },
    report: function () {
      var g = G(), v = g.fullMah * 100 > 3600 * 130 ? TX('Larger battery is built in.')
        : g.fullMah * 100 < 3600 * 90 ? TX('Sign of desync: learned capacity below 90 % of design.') : TX('Normal: no sign of desync.');
      var cuts = '';
      g.cuts.slice(-4).forEach(function (c) { cuts += txt(TX('Cut-off %d · %s V · gauge %s %%', c.n, c.V.toFixed(2), c.gauge), 'li'); });
      if (g.zeroInfo) cuts += txt(TX('Zero: N − X = %s %%', g.zeroInfo.result), 'li');
      cuts += txt(TX('Written: calibration on the microSD, copy in this system.'), 'li');
      var body = '<div class="twocol"><div>' + kv(TX('Learned capacity'), '') .replace('<b data-k=""></b>', '<b>' + g.fullMah + ' / 3600 mAh</b>') +
        '<div class="kv"><span>' + esc(TX('Verdict')) + '</span><b>' + esc(v) + '</b></div></div>' +
        '<div><h3>' + esc(TX('Gauge before each cut-off')) + '</h3>' + cuts + '</div></div>' +
        '<div class="btnrow">' + fi(esc(TX('Next')), function () { SIM.reportNext(); renderAll(); }, 'btn') + '</div>';
      return { title: TX('Calibration report'), sub: TX('Step %d of 5', 5), bar: [['B', TX('Back')], ['A', TX('Next')]], body: body };
    },
    transfer: function () {
      var m = G().marker, body = '';
      if (m) {
        body += txt(TX('Calibration saved on the microSD. Marker: from %s, number %d, %s.', name(m.from), m.seq, m.time), 'li');
        body += txt(TX('Open the Hub in the other system. The calibration moves by itself.'), 'li');
        Object.keys(m.pending).forEach(function (s) { body += '<div class="kv"><span>' + esc(name(s)) + '</span><b>' + esc(m.pending[s] ? TX('Waiting') : TX('Done')) + '</b></div>'; });
      } else body += txt(TX('Calibration finished'), 'li ok');
      body += txt(TX('The hekate hop is a manual action only.'), 'li dim');
      return { title: TX('Transfer to the other MMC'), sub: TX('Step %d of 5', 6), bar: [['B', TX('Back')]], body: body };
    },
    lowbat: function () {
      return { title: '', sub: '', bar: [], body: '<div class="lowbat"><div class="bigbat bbig">' + battery(true) + '</div>' +
        '<p class="para s">' + esc(TX('The console does not start: the battery is still low. It is charging: wait until 3 minutes have passed.')) + '</p></div>' };
    },
    drain: function () {
      var body = '<div class="dtop"><div class="dleft"><div class="bigbat bbig">' + battery(false) + '</div>' +
        '<div class="approx">' + esc(TX('The percent is approximate: the console may switch off earlier.')) + '</div></div>' +
        '<div class="dinfo"><div class="vbig" data-k="V"></div>' +
        '<div class="status" data-k="status"></div>' +
        '<div class="chips"><span class="chip">' + esc(TX('Load')) + ': <b data-k="load"></b></span>' +
        '<span class="chip">' + esc(TX('Brightness')) + ': <b data-k="bright"></b></span></div>' +
        '<div class="kvs">' + kv(TX('Battery consumption'), 'I') + kv(TX('Draining for'), 'elapsed') + kv(TX('On pause for'), 'pauseT') + '</div>' +
        '<div class="logline">' + esc(TX('Everything is logged.')) + '</div></div></div>' +
        '<div class="charts">' +
        chart(TX('Voltage'), 'V', 3.0, 4.2, [3.2], ['V'], true) +
        '<div class="chart wide"><div class="ct"><span>' + esc(TX('CPU / GPU load')) + '</span><b data-k="pw"></b></div>' +
        '<div class="cbox"><svg viewBox="0 0 600 110" preserveAspectRatio="none" data-spark="1" data-min="0" data-max="100">' +
        ['C1', 'C2', 'C3', 'C4', 'G'].map(function (k, i) { return '<polyline class="ln' + i + '" data-line="' + k + '"/>'; }).join('') + '</svg></div>' +
        '<div class="legend">' + legendOf([[TX('Core 1'), 'c1'], [TX('Core 2'), 'c2'], [TX('Core 3'), 'c3'], [TX('Core 4'), 'c4'], [TX('GPU'), 'gpu']]) + '</div></div>' +
        chart(TX('Temperature'), 'T', 20, 70, [60], ['T'], false) +
        '</div>';
      return { title: TX('Battery drain'), sub: ctxLabel(), bar: [['−', TX('Screensaver')], ['B', TX('Stop')]], body: body, drain: true };
    },
    black: function () { return { title: '', sub: '', bar: [], body: '' }; }
  };

  function openStop() {
    G().popup = { text: TX('Stop draining? The cycle then needs to be finished another way.'),
      btns: [[TX('Cancel'), function () { G().popup = null; renderAll(); }], [TX('Stop'), function () { SIM.stopDrain(); renderAll(); }]] };
  }
  function back() {
    var s = G().scr;
    if (G().popup) { G().popup = null; }
    else if (s === 'battery') go('tools');
    else if (s === 'ledger') go('battery');
    else if (s === 'intro' || s === 'steps') go('tools');
    else if (s === 'start') go('steps');
    else if (s === 'charge') go('start', G().cur);
    else if (s === 'report' || s === 'transfer') go('steps');
    else if (s === 'drain') { if (G().drainRun) openStop(); else go('start', G().cur); }
    renderAll();
  }
  function act() { if (F[G().focus]) F[G().focus](); }
  function move(d) { if (F.length) { G().focus = (G().focus + d + F.length) % F.length; highlight(); } }
  function highlight() {
    var a = document.querySelector('#stage .bA .bl');
    if (a && G().scr === 'steps') a.textContent = G().focus === ACTIVE_F ? TX('Begin step') : TX('Open');
    var scope = G().popup ? '#stage .pop [data-f]' : '#stage .body [data-f]';
    document.querySelectorAll(scope).forEach(function (el) { el.classList.toggle('sel', +el.dataset.f === G().focus); });
  }
  function press(b) {
    var g = G();
    // In the saver the sticks move and dim the screen; the D-pad does nothing, any other button wakes it.
    if (g.saver) { if (!/^(UP|DOWN|LEFT|RIGHT)$/.test(b)) { g.saver = false; renderAll(); } return; }
    if (g.popup && (b === 'UP' || b === 'DOWN' || b === 'LEFT' || b === 'RIGHT')) { move(b === 'UP' || b === 'LEFT' ? -1 : 1); return; }
    if (b === 'UP') move(-1);
    else if (b === 'DOWN' || b === 'RIGHT') move(1);
    else if (b === 'A') { act(); renderAll(); }
    else if (b === 'B') back();
    else if (b === 'MINUS' && g.scr === 'drain' && g.power) { g.saver = true; renderAll(); }
  }

  // The stage: build the screen, then live values each tick.
  function buildStage() {
    var g = G();
    USED_NEW = {};
    F = [];
    var view = (VIEWS[g.scr] || VIEWS.tools)();
    var body = view.body, popHtml = '';
    if (g.scr === 'black') return { html: '', bar: '', popped: false };
    if (g.popup) {
      F = [];
      g.popup.btns.forEach(function (b) { F.push(b[1]); });
      popHtml = '<div class="pop"><div class="pbox"><p>' + esc(g.popup.text) + '</p><div class="btnrow">' +
        g.popup.btns.map(function (b, i) { return '<div class="fi btn' + (i ? '' : ' danger') + '" data-f="' + i + '">' + esc(b[0]) + '</div>'; }).join('') + '</div></div></div>';
    }
    if (g.focus >= F.length) g.focus = 0;
    var toast = g.toast && Date.now() < g.toast.until ? '<div class="toast' + (g.toast.bad ? ' bad' : '') + '">' + esc(g.toast.text) + '</div>' : '';
    var saver = g.saver && g.scr === 'drain' ? '<div class="saver"><div class="sblock" data-move="1">' +
      '<div class="sclock" data-k="clock"></div><div class="sstat">' + esc(ctxLabel()) + '</div>' +
      '<div class="sbat">' + battery() + '</div><div class="smet">' + kv(TX('Voltage'), 'V') + kv(TX('Current'), 'I') +
      kv(TX('Gauge'), 'gauge') + kv(TX('Load'), 'load') + kv(TX('Elapsed'), 'elapsed') + '</div></div>' +
      '<div class="sdim" data-dim="1"></div><div class="osd" data-osd="1"></div>' +
      '<div class="shint">' + esc(TX('Right stick: brightness; left stick: move.')) + ' ' + esc(TX('Any button exits.')) + '</div></div>' : '';
    var html = '<div class="hdr"><div class="hl"><div class="ver">v0.14.192' + (view.sub ? ' · ' + esc(view.sub) : '') + '</div><div class="ttl">' + esc(view.title) + '</div></div>' +
      '<div class="hr"><div class="pills"><span class="pill g">MTP</span><span class="pill g">FTP</span><span class="pill b">LOG</span></div>' +
      '<div class="stor"><div><span>EmuNAND</span><i style="width:16%"></i><b>6.32 GB</b></div><div><span>microSD</span><i style="width:20%"></i><b>197.89 GB</b></div></div>' +
      '<div class="clk"><div class="tm" data-k="clock"></div><div class="bt" data-k="psm"></div></div></div></div>' +
      '<div class="body">' + body + '</div>' +
      '<div class="ftr"><svg class="wave" viewBox="0 0 1280 80" preserveAspectRatio="none"><path d="M0 30 C200 0 360 60 560 34 C760 8 920 58 1100 30 C1190 16 1240 22 1280 26 V80 H0Z" fill="#1c5a4a"/><path d="M0 50 C240 24 420 70 640 46 C860 22 1040 66 1280 44 V80 H0Z" fill="#2c7a5f"/></svg><div class="bar">' + bar(view.bar) + '</div></div>' +
      saver + toast + popHtml;
    return { html: html, popped: !!g.popup };
  }
  var stage = null;
  function renderStage() {
    var b = buildStage();
    stage.className = 'stage' + (b.popped ? ' popped' : '');
    stage.innerHTML = b.html;
    highlight(); live();
  }

  function pct() { return Math.max(0, Math.min(100, Math.round(G().q))); }
  function statusText(g) {
    if (g.charger) return TX('Charger connected. The drain is on pause. Unplug the charger to continue the drain.');
    if (g.loadOff) return TX('Paused: the battery is hot, waiting until it cools to 55 °C');
    if (g.tail) return TX('The console has already switched off: now it drains slowly to squeeze out the last charge.');
    return TX('Full load: the console drains as fast as possible.');
  }
  function chgText(g) {
    if (g.charge3 > 0) return S2('The console is off and charging: ', 'Консоль вимкнена й заряджається: ') + mmss(SIM.CHG3_S - g.charge3) + S2(' of 3:00', ' з 3:00');
    return S2('Charging for 3 minutes is done. Switch the console on.', 'Заряд 3 хв пройшов. Вмикайте консоль.');
  }
  function hms(s) { s = Math.floor(s); var p = function (n) { return (n < 10 ? '0' : '') + n; }; return p(Math.floor(s / 3600)) + ':' + p(Math.floor(s / 60) % 60) + ':' + p(s % 60); }
  function mmss(s) { s = Math.ceil(s); return (s < 600 ? '0' : '') + Math.floor(s / 60) + ':' + ((s % 60) < 10 ? '0' : '') + (s % 60); }
  function vals() {
    var g = G(), I = SIM.loadNow(), V = SIM.ocv(g.q) - (I * g.R / 1000), p = pct(), L = SIM.loadLevels(I, g.t);
    var load = g.scr !== 'drain' ? '—' : g.charger ? '—' : g.loadOff ? TX('Off') : I === SIM.I_MAX ? TX('Max') : TX('Min');
    var charging = g.charger ? '+' : '';
    var sline = V.toFixed(2) + ' V · ' + Math.round(I * 1000) + ' mA · ' + Math.round(g.T) + ' °C';
    return {
      clock: SIM.clockText(), psm: p + ' %', pctText: '≈ ' + p + ' %', V: V.toFixed(2) + ' V', I: Math.round(I * 1000) + ' mA',
      gauge: SIM.counter() + ' %', T: Math.round(g.T) + ' °C', load: load, bright: g.bright + ' %',
      elapsed: hms(g.drainSec), pauseT: g.charger && g.drainRun ? hms(g.pausedS) : '—', status: statusText(g),
      charging: TX('Charging:') + ' ' + p + ' % / 100 %', curr: charging + (g.charger ? SIM.CHG_MA : 0) + ' mA',
      left: mmss(g.charge3), sline: sline, pctNum: p, pw: (V * I).toFixed(1) + ' W',
      c1: L.c[0], c2: L.c[1], c3: L.c[2], c4: L.c[3], gpu: L.g, chgTimer: chgText(g),
      sys: name(g.sys), pwr: g.power ? S2('on', 'увімкнена') : S2('off', 'вимкнена'), chg: g.charger ? S2('in', 'є') : S2('out', 'немає'),
      cuts: String(g.cuts.length), cur: String(g.cur), scrName: g.scr, mmc: String(g.emus)
    };
  }
  function live() {
    if (!stage) return;
    var v = vals();
    document.querySelectorAll('[data-k]').forEach(function (el) { var k = el.dataset.k; if (v[k] !== undefined) el.textContent = v[k]; });
    document.querySelectorAll('[data-bw]').forEach(function (el) { el.style.width = v.pctNum + '%'; });
    if (SIM.G.scr === 'drain') sparks(); // the charts keep moving under the screen saver too
    if (SIM.G.saver) saverFrame();
  }
  // Move the block (whole pixels), dim the stage by the brightness, show the short OSD.
  function saverFrame() {
    var c = SIM.saverCentre(), g = G(), W = 600, H = 400;
    var left = Math.min(1280 - W, Math.max(0, c.x - W / 2)), top = Math.min(720 - H, Math.max(0, c.y - H / 2));
    var blk = stage.querySelector('[data-move]');
    if (blk) { blk.style.left = Math.round(left) + 'px'; blk.style.top = Math.round(top) + 'px'; }
    var dim = stage.querySelector('[data-dim]');
    if (dim) dim.style.opacity = (1 - Math.round(g.bright) / 100).toFixed(2);
    var osd = stage.querySelector('[data-osd]');
    if (osd) {
      var on = Date.now() < g.osdUntil;
      osd.textContent = TX('Brightness %d %%', Math.round(g.bright));
      osd.classList.toggle('show', on);
    }
  }
  // Sticks: keys (arrows = left stick; W/S brightness, A/D drift speed) and pointer drag on the on-screen sticks.
  var HELD = { ArrowLeft: ['lx', -1], ArrowRight: ['lx', 1], ArrowUp: ['ly', 1], ArrowDown: ['ly', -1],
    w: ['ry', 1], s: ['ry', -1], d: ['rx', 1], a: ['rx', -1] };
  function setStick(k, v) { SIM.G.stick[k] = v; }
  function stickPointer(el, e, release) {
    var side = el.dataset.stick, r = el.getBoundingClientRect();
    var x = 0, y = 0;
    if (!release) {
      x = Math.max(-1, Math.min(1, (e.clientX - (r.left + r.width / 2)) / (r.width / 2)));
      y = Math.max(-1, Math.min(1, -(e.clientY - (r.top + r.height / 2)) / (r.height / 2)));
    }
    var knob = el.querySelector('i');
    knob.style.transform = 'translate(' + (x * 30).toFixed(1) + 'px,' + (-y * 30).toFixed(1) + 'px)';
    if (side === 'L') { setStick('lx', x); setStick('ly', y); } else { setStick('rx', x); setStick('ry', y); }
  }

  // Side panel "Світ": what the person does, time, events, the tag of the screen.
  function sideBtn(label, cls, action) { return '<button type="button" class="sb ' + (cls || '') + '" data-sa="' + action + '">' + esc(label) + '</button>'; }
  function renderSide() {
    var g = G(), side = document.getElementById('side');
    var people = [
      sideBtn(g.charger ? S2('Take out the charger', 'Вийняти зарядку') : S2('Plug in the charger', 'Вставити зарядку'), '', 'charger'),
      sideBtn(g.power ? S2('Press power (off)', 'Натиснути живлення (вимкнути)') : S2('Press power (on)', 'Натиснути живлення (увімкнути)'), '', 'power'),
      sideBtn(S2('Wait 3 minutes', 'Чекати 3 хвилини'), '', 'wait3')
    ];
    if (g.noBoot && !g.power) people.push(sideBtn(TX('Console does not start'), 'warn', 'noboot'));
    SIM.systems().forEach(function (s) { if (s !== g.sys) people.push(sideBtn(S2('Boot ', 'Запустити ') + name(s), '', 'boot:' + s)); });
    var ev = [
      sideBtn(S2('Power loss', 'Вимкнення живлення'), '', 'loss'),
      sideBtn(S2('High temperature (+20 °C)', 'Висока температура (+20 °C)'), '', 'hot'),
      sideBtn(S2('Gauge looks reset', 'Гейдж скинувся перед увімкненням') + (g.gaugeReset ? ' ✓' : ''), g.gaugeReset ? 'on' : '', 'toggle:gaugeReset'),
      sideBtn(S2('Log lost', 'Журнал втрачено') + (g.logLost ? ' ✓' : ''), g.logLost ? 'on' : '', 'toggle:logLost'),
      sideBtn(S2('Gauge shows 35 % at cut-off', 'Гейдж показує 35 % при відсіку') + (g.E === 35 ? ' ✓' : ''), g.E === 35 ? 'on' : '', 'toggle:E35'),
      sideBtn(S2('Gauge at 0 % early (reads 40 % low)', 'Гейдж 0 % раніше (занижує на 40 %)') + (g.E === -40 ? ' ✓' : ''), g.E === -40 ? 'on' : '', 'toggle:Em40'),
      sideBtn(S2('Larger battery (7810 mAh)', 'Велика батарея (7810 мАг)') + (g.fullMah > 3600 ? ' ✓' : ''), g.fullMah > 3600 ? 'on' : '', 'toggle:big'),
      sideBtn(S2('Write fails', 'Запис не вдається') + (g.applyFail ? ' ✓' : ''), g.applyFail ? 'on' : '', 'toggle:applyFail'),
      sideBtn(S2('Newer calibration in sysMMC', 'Новіше калібрування в sysMMC') + (g.sysNewer ? ' ✓' : ''), g.sysNewer ? 'on' : '', 'toggle:sysNewer'),
      sideBtn(S2('Other battery model in sysMMC', 'Інша модель батареї в sysMMC') + (g.sysOther ? ' ✓' : ''), g.sysOther ? 'on' : '', 'toggle:sysOther'),
      sideBtn(S2('emuMMC count: ', 'Кількість emuMMC: ') + g.emus, g.emus === 2 ? 'on' : '', 'toggle:emus')
    ];
    var speeds = [1, 10, 100].map(function (s) { return sideBtn('x' + s, g.speed === s ? 'on' : '', 'speed:' + s); }).join('');
    var tag = g.scr === 'drain' ? S2('Built partly: the drain page and the screensaver (0.14.179, not confirmed on a console). The cut-off, the per-second log and the wizard are planned.',
      'Частково побудовано: сторінка розряду й заставка (0.14.179, на консолі не підтверджено). Вихід по відсіку, лог щосекунди і майстер заплановані.')
      : S2('Planned (F.29 f): not in the Hub yet.', 'Заплановано (F.29 f): у Hub ще немає.');
    var keys = Object.keys(USED_NEW).map(function (k) { return '<li><code>' + esc(k) + '</code></li>'; }).join('');
    side.innerHTML =
      '<h3>' + esc(S2('The world', 'Світ')) + '</h3>' +
      '<div class="kvs side">' + kv(S2('Console system', 'Система'), 'sys') + kv(S2('Power', 'Живлення'), 'pwr') + kv(S2('Charger', 'Зарядка'), 'chg') +
      kv(S2('Screen', 'Екран'), 'scrName') + kv(S2('Cut-offs so far', 'Відсіків'), 'cuts') + kv(S2('Step', 'Крок'), 'cur') + kv(S2('Gauge', 'Гейдж'), 'gauge') + kv(S2('Voltage', 'Напруга'), 'V') + '</div>' +
      (!g.power ? '<p class="tag" data-k="chgTimer"></p>' : '') +
      '<h3>' + esc(S2('Time', 'Час')) + '</h3><div class="sbrow">' + speeds + sideBtn(g.paused ? S2('Resume', 'Йде') : S2('Pause', 'Пауза'), g.paused ? 'on' : '', 'pause') +
      sideBtn(S2('To the next event', 'До наступної події'), '', 'next') + '</div>' +
      '<h3>' + esc(S2('The person', 'Людина')) + '</h3><div class="sbcol">' + people.join('') + '</div>' +
      '<h3>' + esc(S2('Events', 'Події')) + '</h3><div class="sbcol">' + ev.join('') + '</div>' +
      '<label class="sl">' + esc(S2('Internal resistance (mΩ)', 'Внутрішній опір (мОм)')) + ' <b>' + g.R + '</b><input type="range" min="5" max="300" value="' + g.R + '" data-sa="R"></label>' +
      '<h3>' + esc(S2('Screen tag', 'Позначка екрана')) + '</h3><p class="tag">' + esc(tag) + '</p>' +
      '<h3>' + esc(S2('New texts on this screen', 'Нові тексти на екрані')) + '</h3><ul class="keys">' + (keys || '<li>—</li>') + '</ul>';
  }
  function sideAction(a) {
    var g = G(), p = a.split(':');
    if (a === 'charger') SIM.toggleCharger();
    else if (a === 'power') { if (g.power) SIM.powerOff(); else SIM.powerOn(); }
    else if (a === 'wait3') { var e = SIM.advance(180); if (e) handleEv(e); }
    else if (a === 'noboot') SIM.consoleNoBoot();
    else if (p[0] === 'boot') SIM.bootSystem(p[1]);
    else if (a === 'loss') SIM.powerOff();
    else if (a === 'hot') { g.T += 20; g.needRender = true; }
    else if (a === 'toggle:gaugeReset') g.gaugeReset = !g.gaugeReset;
    else if (a === 'toggle:logLost') g.logLost = !g.logLost;
    else if (a === 'toggle:E35') g.E = g.E === 35 ? 0 : 35;
    else if (a === 'toggle:Em40') g.E = g.E === -40 ? 0 : -40;
    else if (a === 'toggle:big') g.fullMah = g.fullMah > 3600 ? 3032 : 7810;
    else if (a === 'toggle:applyFail') g.applyFail = !g.applyFail;
    else if (a === 'toggle:sysNewer') { g.sysNewer = !g.sysNewer; g.copies.sysMMC.seq = g.sysNewer ? 999 : 0; }
    else if (a === 'toggle:sysOther') { g.sysOther = !g.sysOther; g.copies.sysMMC.model = g.sysOther ? 'LiHV-4320' : 'LiPo-3600'; }
    else if (a === 'toggle:emus') g.emus = g.emus === 2 ? 1 : 2;
    else if (p[0] === 'speed') g.speed = +p[1];
    else if (a === 'pause') g.paused = !g.paused;
    else if (a === 'next') { var ev = SIM.jumpNext(); if (ev) handleEv(ev); }
    renderAll();
  }
  function handleEv(ev) {
    var g = G();
    if (ev === 'full' && g.chargeMode === 'full' && (g.cur === 3 || g.cur === 4)) g.cycleWait = true;
    g.needRender = true;
  }

  function renderAll() {
    renderStage(); renderSide(); G().needRender = false;
  }

  function fit() {
    var frame = document.getElementById('frame');
    document.documentElement.style.setProperty('--k', (frame.clientWidth / 1280).toFixed(4));
  }
  function bottomLists() {
    var keys = Object.keys(NEW), lines = keys.map(function (k) { return k + '\n    ' + NEW[k]; }).join('\n');
    document.getElementById('newtexts').textContent = lines;
    var q = [
      S2('Register for the zero (RemCap or RepSOC): not decided.', 'Регістр нуля (RemCap або RepSOC): не вирішено.'),
      S2('Temperature limit: 60 °C in this page. The value is not decided.', 'Межа температури: 60 °C на цій сторінці. Значення не вирішено.'),
      S2('Real cut-off voltage under load: 3.2 V here. To measure on the first run.', 'Справжня напруга відсіку під навантаженням: 3,2 В тут. Виміряти на першому прогоні.'),
      S2('The rule for a good calibration (the marker): not decided.', 'Правило «добре калібрування» для маркера: не вирішено.'),
      S2('The wording of the toasts: not decided.', 'Текст тостів: не вирішено.'),
      S2('Does the gauge keep counting while the console is off? Not confirmed on a console.', 'Чи рахує гейдж, поки консоль вимкнена? На консолі не підтверджено.'),
      S2('The CPU and GPU load in the charts is the level the Hub sets (100 % at max, 15 % at min). The measured load needs a reading on the console.', 'Навантаження ЦП і GPU на графіках — рівень, який виставляє Hub (100 % на максимумі, 15 % на мінімумі). Виміряне навантаження треба зняти на консолі.'),
      S2('An unfinished sentence of the owner that starts with «в черні…»: not received, not guessed.', 'Незакінчене речення власника, яке починається з «в черні…»: не отримано, не вгадуємо.')
    ];
    document.getElementById('questions').innerHTML = q.map(function (t) { return '<li>' + esc(t) + '</li>'; }).join('');
  }

  function init() {
    stage = document.getElementById('stage');
    document.querySelectorAll('[data-lang]').forEach(function (a) { a.classList.toggle('on', a.dataset.lang === LANG); });
    stage.addEventListener('click', function (e) {
      var el = e.target.closest('[data-f]');
      if (!el) return;
      G().focus = +el.dataset.f; act(); renderAll();
    });
    document.getElementById('pad').addEventListener('click', function (e) {
      var b = e.target.closest('[data-b]'); if (b) press(b.dataset.b);
    });
    document.getElementById('side').addEventListener('click', function (e) {
      var b = e.target.closest('[data-sa]'); if (b && b.tagName === 'BUTTON') sideAction(b.dataset.sa);
    });
    document.getElementById('side').addEventListener('input', function (e) {
      if (e.target.dataset.sa === 'R') { SIM.G.R = +e.target.value; renderSide(); }
    });
    document.querySelectorAll('.stick').forEach(function (el) {
      el.addEventListener('pointerdown', function (e) { el.setPointerCapture(e.pointerId); stickPointer(el, e, false); });
      el.addEventListener('pointermove', function (e) { if (el.hasPointerCapture(e.pointerId)) stickPointer(el, e, false); });
      el.addEventListener('pointerup', function (e) { stickPointer(el, e, true); });
      el.addEventListener('pointercancel', function (e) { stickPointer(el, e, true); });
    });
    document.addEventListener('keyup', function (e) {
      var h = HELD[e.key]; if (h) setStick(h[0], 0);
    });
    document.addEventListener('keydown', function (e) {
      if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA') return;
      var h = HELD[e.key];
      if (h && SIM.G.saver) { e.preventDefault(); setStick(h[0], h[1]); return; }
      var map = { ArrowUp: 'UP', ArrowDown: 'DOWN', ArrowLeft: 'LEFT', ArrowRight: 'RIGHT', Enter: 'A', Escape: 'B', Backspace: 'B',
        x: 'X', y: 'Y', '+': 'PLUS', '=': 'PLUS', '-': 'MINUS', '_': 'MINUS', q: 'L', e: 'R' };
      var b = map[e.key];
      if (!b) return;
      if (b.indexOf('ARROW') === 0 || /^(UP|DOWN|LEFT|RIGHT)$/.test(b)) e.preventDefault();
      press(b);
    });
    document.getElementById('copy').addEventListener('click', function () {
      var t = document.getElementById('newtexts').textContent;
      if (navigator.clipboard) navigator.clipboard.writeText(t).catch(function () {});
    });
    window.addEventListener('resize', fit);
    fit(); bottomLists(); renderAll();
    setInterval(function () {
      var g = G();
      if (!g.paused) { var ev = SIM.step(g.speed * 0.1); if (ev) handleEv(ev); }
      SIM.saverTick(0.1); // the drift and the sticks run on wall-clock time, like the Hub
      if (g.toast && Date.now() > g.toast.until) { g.toast = null; g.needRender = true; }
      if (g.needRender) renderAll(); else live();
    }, 100);
  }
  // Layout check (call window.__fit() in the console): renders every screen of the guide and lists each element
  // that leaves the body area of the 1280x720 stage (or the header / footer strip). The state is restored afterwards.
  window.__fit = function () {
    var g = SIM.G, base = JSON.parse(JSON.stringify(g));
    var sEl = document.getElementById('stage'), sr = sEl.getBoundingClientRect(), k = sr.width / 1280;
    function box(el) {
      var r = el.getBoundingClientRect();
      return [(r.left - sr.left) / k, (r.top - sr.top) / k, (r.right - sr.left) / k, (r.bottom - sr.top) / k];
    }
    var out = [];
    function check(label, sel, lim) {
      sEl.querySelectorAll(sel).forEach(function (el) {
        if (el.closest('[hidden]')) return;
        var b = box(el);
        if (b[2] - b[0] < 0.5 || b[3] - b[1] < 0.5) return;
        if (b[0] < lim[0] - 1 || b[1] < lim[1] - 1 || b[2] > lim[2] + 1 || b[3] > lim[3] + 1) {
          out.push({ screen: label, element: el.tagName.toLowerCase() + '.' + (el.getAttribute('class') || '').split(' ').join('.'),
            text: (el.textContent || '').trim().slice(0, 28), box: [b[0], b[1], b[2], b[3]].map(Math.round) });
        }
      });
    }
    function restore() {
      Object.keys(g).forEach(function (key) { delete g[key]; });
      Object.assign(g, JSON.parse(JSON.stringify(base)));
    }
    var CUTS = [];
    for (var c = 1; c <= 8; c++) CUTS.push({ n: c, V: 3.2, gauge: c, t: 0 });
    function drain(extra) {
      return function (s) { s.scr = 'drain'; s.cur = 1; s.ctx = 1; s.drainRun = true; s.power = true; s.charger = false; s.drainSec = 1; s.minRun = 0; extra(s); SIM.advance(900); };
    }
    var SCREENS = [
      ['tools', function (s) { s.scr = 'tools'; s.focus = 2; }],
      ['battery', function (s) { s.scr = 'battery'; }],
      ['ledger', function (s) { s.scr = 'ledger'; }],
      ['intro', function (s) { s.scr = 'intro'; }],
      ['steps', function (s) { s.started = true; s.scr = 'steps'; s.cur = 1; }],
      ['start step 1', function (s) { s.scr = 'start'; s.sub = 1; s.cur = 1; }],
      ['start step 2', function (s) { s.scr = 'start'; s.sub = 2; s.cur = 2; }],
      ['charge step 2', function (s) { s.scr = 'charge'; s.cur = 2; s.chargeMode = 'zero'; s.chargeStarted = true; s.charge3 = 120; s.charger = true; s.power = true; }],
      ['lowbat step 2', function (s) { s.scr = 'lowbat'; s.cur = 2; s.power = false; s.chargeStarted = true; s.charge3 = 60; s.charger = true; }],
      ['zero', function (s) { s.scr = 'zero'; s.cur = 2; s.zeroInfo = { N: 3, X: 18, result: 0, note: '', V: 3.2 }; }],
      ['report', function (s) { s.scr = 'report'; s.cur = 5; s.cuts = CUTS; s.zeroInfo = { N: 3, X: 18, result: 1, note: '', V: 3.2 }; }],
      ['transfer', function (s) { s.scr = 'transfer'; s.cur = 6; s.marker = { from: 'emuA', seq: 1, time: '13:00', good: true, pending: { sysMMC: true } }; }],
      ['drain', drain(function (s) { s.bright = 100; })],
      ['drain after cut-off', drain(function (s) { s.tail = true; s.bright = 20; s.cuts = CUTS; })],
      ['drain, charger in', drain(function (s) { s.charger = true; s.pausedS = 4000; })],
      ['drain, screensaver', drain(function (s) { s.saver = true; })],
      ['drain, stop popup', drain(function (s) {})],
      ['black', function (s) { s.scr = 'black'; s.power = false; }]
    ];
    SCREENS.forEach(function (item) {
      restore();
      var label = item[0];
      item[1](g);
      if (label === 'drain, stop popup') { g.popup = null; }
      if (label === 'drain, stop popup') { renderAll(); }
      renderAll();
      if (label === 'drain, stop popup') {
        // open the stop confirmation the way the B button does
        openStop(); renderAll();
      }
      if (g.saver === false && label.indexOf('screensaver') < 0) g.saver = false;
      renderAll();
      var body = box(sEl.querySelector('.body') || sEl);
      check(label, '.hdr *', [0, 0, 1280, 100]);
      check(label, '.body *', body);
      check(label, '.ftr .bar *', [0, 628, 1280, 720]);
      check(label, '.saver *, .pop *, .toast', [0, 0, 1280, 720]);
    });
    restore();
    renderAll();
    console.table(out);
    console.log('__fit: ' + out.length + ' element(s) outside the body area');
    return { overflows: out.length, list: out };
  };
  document.addEventListener('DOMContentLoaded', init);
})();
