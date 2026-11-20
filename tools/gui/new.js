// ===========================================================================
// Gullay's own pages and pictures
// ===========================================================================
try { S.motion = localStorage.getItem('gullay.motion') || 'Full'; } catch (e) { S.motion = 'Full'; }
S.learnKind = 0; S.maps = []; S.learning = null;
const hd2 = (t) => h('h2', { class: 'hd', text: t, style: 'margin:0;font-size:15px' });
const vline = () => h('div', { style: 'width:1px;align-self:stretch;background:var(--line)' });

// ----- Engines: the memory (both engines), the Juba (only when one is in use; Juba 1 / Juba 2 with two),
// the answer and mirror (one engine) or the balance (two), transport and gain
S.tapoEdit = S.tapoEdit || 1;
const tapoName = (n) => (S.tapoEdit === 2 && tapoBoth() ? n.replace(/^Juba /, 'Juba 2 ') : n);
function tapoBoth() { return Math.round(P('Engine 1').v) === 1 && Math.round(P('Engine 2').v) === 2; }
function tapoAny() { return Math.round(P('Engine 1').v) === 1 || Math.round(P('Engine 2').v) === 2; }
function pageEngines() {
  const randMem = () => setMany([[P('DLD Memory'), chance(0.7) ? 0 : 2], [P('Memory Rate'), chance(0.4) ? Math.log2(44100) : Math.log2(rnd(4000, 30000))],
    [P('Crusher Bits'), chance(0.4) ? 16 : Math.round(rnd(4, 12))], [P('Crusher Mix'), rnd(0.4, 1)], [P('Memory Tilt'), rnd(-0.6, 0.6)], [P('Memory Noise'), chance(0.6) ? 0 : rnd(0, 0.3)],
    [P('Crusher Place'), Math.floor(Math.random() * 4)], [P('Tape Drive'), rnd(0.2, 0.8)], [P('Tape Saturation'), rnd(0.2, 0.8)], [P('Tape Bias'), rnd(0.3, 0.8)]]);
  const memP = P('DLD Memory'), tape = h('div', { class: 'row', style: 'gap:6px;align-items:flex-end;border-top:1px solid var(--line);padding-top:5px' }, h('div', { class: 'lbl', text: 'TAPE', style: 'font-size:9.5px;letter-spacing:.08em;align-self:center;width:34px' }), knob(P('Tape Drive'), 'Drive', ink, 28), knob(P('Tape Saturation'), 'Sat', ink, 28), knob(P('Tape Bias'), 'Bias', ink, 28));
  bind(memP, () => { tape.style.display = Math.round(memP.v) === 2 ? '' : 'none'; });
  const where = seg(P('Crusher Place'), ['Loop', 'Out', 'In', 'Both']); where.classList.add('sm');
  const what = h('div', { class: 'hint', style: 'font-size:11px;min-height:28px' });
  const say = () => {
    const bits = Math.round(P('Crusher Bits').v), hz = Math.round(Math.pow(2, P('Memory Rate').v)), mix = P('Crusher Mix').v, tilt = P('Memory Tilt').v, noise = P('Memory Noise').v;
    const wh = ['in every repeat', 'on the echoes out', 'on what goes in', 'in and out'][Math.round(P('Crusher Place').v)] || '';
    const parts = [];
    if (bits < 16) parts.push(bits + ' bits');
    if (hz < 44000) parts.push((hz / 1000).toFixed(hz < 10000 ? 1 : 0) + ' kHz');
    if (!parts.length) { what.textContent = 'Clean: Bits 16 and Rate 44.1 kHz change nothing.' + (Math.round(memP.v) === 2 ? ' Tape still colours every repeat.' : ''); return; }
    what.textContent = parts.join(' · ') + ' ' + wh + ', ' + Math.round(mix * 100) + '% mixed' + (Math.abs(tilt) > 0.05 ? ', grit ' + (tilt < 0 ? 'in the lows' : 'in the highs') : '') + (noise > 0.01 ? ', hiss ' + Math.round(noise * 100) + '%' : '') + '.';
  };
  ['Crusher Bits', 'Memory Rate', 'Crusher Mix', 'Memory Tilt', 'Memory Noise', 'Crusher Place', 'DLD Memory'].forEach((n) => bind(P(n), say));
  const memSeg = seg(memP, ['Clean', 'Tape'], { values: [0, 2] });
  const mem = card('width:300px;flex:none;gap:5px', h('div', { class: 'row', style: 'gap:8px' }, hd2('MEMORY'), tip(memSeg, 'Memory', 'How both engines keep their echoes. Clean: as they are. Tape: every repeat goes through a tape model (from CHOW Tape) and saturates a little more: Drive, Sat and Bias shape it.'), h('div', { class: 'sp' }), dice(ink, 'Random: memory, tape and crush', randMem)),
    h('div', { class: 'lbl', style: 'font-size:9.5px;letter-spacing:.08em;margin-bottom:-3px', text: 'BITCRUSH · SAMPLE RATE · NOISE' }),
    h('div', { class: 'row', style: 'gap:4px;align-items:flex-end' },
      tip(knob(P('Crusher Bits'), 'Bits', ink, 28), 'Bits', 'Bit depth: 16 (clean) down to 2. Smooth adds dither so it hisses instead of buzzing.'),
      tip(knob(P('Memory Rate'), 'Rate', ink, 28), 'Rate', 'Bitcrush rate: 44.1 kHz (clean) down to 2 kHz.'),
      tip(knob(P('Crusher Mix'), 'Mix', ink, 28), 'Mix', 'How much of the crushed sound you hear.'),
      tip(knob(P('Memory Tilt'), 'Tilt', ink, 28), 'Tilt', 'Which end takes the grit: left, the lows get crushed harder; right, the highs. It tilts before the crush and back after, so the tone stays.'),
      tip(knob(P('Memory Noise'), 'Noise', ink, 28), 'Noise', 'A hiss floor under the echoes, like an old sampler or tape.')),
    tip(where, 'Where', 'Where Bits and Rate work. Loop: inside the repeats, dirtier each time. Out: on the echoes coming out. In: on what goes into the delays (your dry sound stays clean). Both: In and Out.'),
    h('div', { class: 'row', style: 'gap:6px' }, tip(toggle(P('Crusher Steps'), 'Smooth'), 'Smooth', 'Adds dither: the crush hisses instead of buzzing.'), tip(toggle(P('Chip Mode'), 'Rate follows time'), 'Rate follows time', 'The crush rate drops as the delay gets longer, like an old sampler stretching its memory.')),
    what, tape);
  const cards = [mem];
  if (tapoAny()) cards.push(tapoCard());
  const two = Math.round(P('Engine 2').v) > 0;
  if (!two) {
    const randOne = () => setMany([[P('Answer After'), Math.floor(Math.random() * 6)], [P('Answer Chance'), rnd(0.4, 1)], [P('Mirror Time'), pick([0.5, 0.75, 1, 1.5, 2])]]);
    const bplays = seg(P('B Plays'), ['Separate', 'Repeat A', 'Reverse A']); bplays.classList.add('sm');
    const bhint = h('div', { class: 'hint' });
    bind(P('B Plays'), (p) => { bhint.textContent = ['Separate: B is its own delay, with its own Time and Feedback.', 'Repeat A: B replays A’s echoes after the wait, on the beats Chance lets through.', 'Reverse A: B plays A’s echoes backwards, over this many of A’s times.'][Math.round(p.v)] || ''; });
    cards.push(card('width:250px;flex:none;gap:7px', h('div', { class: 'row' }, tip(hd2('B PLAYS'), 'B plays', TIPS.PartB), h('div', { class: 'sp' }), dice(ink, 'Random: answer and mirror', randOne)),
      bplays, bhint,
      h('div', { class: 'row', style: 'gap:10px;align-items:flex-end' }, labelled('Answer after', sel(P('Answer After'), 70)), knob(P('Answer Chance'), 'Chance', ink, 34)),
      h('div', { class: 'row', style: 'gap:8px;align-items:center;border-top:1px solid var(--line);padding-top:5px' }, knob(P('Mirror Time'), 'Length', ink, 28), h('span', { class: 'hint', text: 'Reverse length, in A’s times.' }))));
    const bc = cards[cards.length - 1]; bind(P('B Plays'), (p) => { const m = Math.round(p.v); bc.children[3].style.opacity = m === 1 ? 1 : 0.35; bc.children[4].style.opacity = m === 2 ? 1 : 0.35; });
  }
  if (two || spectralOn()) cards.push(pathCard(two));
  const vari = h('div', { class: 'row', style: 'gap:10px;align-items:center' }, tip(toggle(P('Varispeed'), 'Varispeed'), 'Varispeed', 'Like a tape machine: when Time changes, the tape speeds up or slows down to get there, so the echoes bend in pitch while they settle.'), knob(P('Varispeed Glide'), 'Glide', ink, 28));
  const freeBox = h('div', { class: 'col', style: 'gap:6px' }, h('div', { class: 'row', style: 'gap:8px' }, labelled('Tempo', stepper(P('Free Tempo'))), toggle(P('Free Play'), 'Play')), vari);
  bind(P('Sync'), (p) => { freeBox.style.opacity = Math.round(p.v) === 1 ? 1 : 0.35; });
  const m = mode();
  cards.push(card('width:210px;flex:none', hd2('TRANSPORT'), seg(P('Sync'), ['Host', 'Free']), freeBox,
    h('div', { class: 'row', style: 'gap:10px' }, knob(P(m + ' Input'), 'Input', ink, 34), knob(P(m + ' Output'), 'Output', ink, 34))));
  return h('div', {}, ...cards);
}
function tapoCard() {
  const both = tapoBoth(), pat = P(tapoName('Juba Pattern')), grid = h('div', { class: 'pat', role: 'group', 'aria-label': 'Juba pattern' });
  // the 24 factory patterns in the grid; the tapped one (25th) sits with the TAP pad below
  S.patBtns = pat.lab.map((l, i) => { const b = h('button', { type: 'button', class: i < 24 ? 'patcard' : '', onclick: () => setMany([[pat, i]]) });
    if (i < 24) { b.append(h('span', { class: 'pl', text: l }), h('canvas', { class: 'pm', width: 56, height: 11 }), h('span', { class: 'pw' })); grid.appendChild(b); } else b.textContent = 'Tapped';
    return b; });
  if (S.pats) setTimeout(() => S.drawPats && S.drawPats(), 0); else post('pats');
  bind(pat, () => S.patBtns.forEach((b, i) => b.classList.toggle('on', Math.round(pat.v) === i)));
  const randTapo = () => setMany([[pat, Math.floor(Math.random() * 24)], [P(tapoName('Juba Morph')), Math.log2(rnd(20, 3000))], [P(tapoName('Juba Morph Sync')), Math.floor(Math.random() * 7)], [P(tapoName('Juba Velocity')), rnd(0.1, 0.7)], [P(tapoName('Juba Mod Amount')), chance(0.6) ? 0 : rnd(0, 0.4)], [P(tapoName('Juba Mod Rate')), rnd(0, 1)]]);
  const head = h('div', { class: 'row' }, tip(hd2('JUBA ⓘ'), 'Juba', 'Echoes in a rhythm, like patting juba: pick one of 24 patterns, morph between them, or tap your own. Dynamics: how much each tap’s loudness follows the pattern. Wobble and Rate add drift.'));
  if (both) {
    const sw = h('div', { class: 'seg sm' });
    [1, 2].forEach((k) => sw.appendChild(h('button', { type: 'button', class: S.tapoEdit === k ? 'on' : '', text: 'Juba ' + k, onclick: () => { S.tapoEdit = k; buildPages(); } })));
    head.append(tip(sw, 'Juba 1 / Juba 2', 'Both slots are Juba: pick which one this card sets. Each has its own pattern, morph, dynamics and wobble.'));
  }
  head.append(h('div', { class: 'sp' }), tip(toggle(P('MIDI Picks Patterns'), 'MIDI picks'), 'MIDI picks patterns', 'Notes C1–B2 (Bitwig’s C1 = MIDI note 36) and Program Change 1–24 pick slot 1’s pattern. Juba Pattern is a mod target too.'), dice(ink, 'Random: pattern, morph and tap movement', randTapo));
  return card('flex:1.5', head, grid,
    h('div', { class: 'row', style: 'gap:8px;align-items:flex-end' }, morphBox(), knob(P(tapoName('Juba Velocity')), 'Dynamics', ink, 34), knob(P(tapoName('Juba Mod Amount')), 'Wobble', ink, 34), knob(P(tapoName('Juba Mod Rate')), 'Rate', ink, 34)),
    tapPad(pat));
}
// tap a pattern with the mouse: the first click starts, then 4, 8 or 16 taps; it plays back as you tapped it
function tapPad(pat) {
  const cnt = P('Juba Tap Count'), status = h('span', { class: 'hint' }), pad = h('button', { type: 'button', class: 'btn', text: 'TAP', style: 'width:84px;height:34px;font-weight:800;letter-spacing:.1em' });
  let times = [], timer = null;
  const need = () => [4, 8, 16][Math.round(cnt.v)] || 4;
  const reset = (msg) => { times = []; pad.classList.remove('on'); status.textContent = msg || 'Click TAP to start, then tap ' + need() + ' echoes.'; };
  pad.addEventListener('pointerdown', (e) => {
    e.preventDefault(); const t = performance.now(); times.push(t); pad.classList.add('on');
    clearTimeout(timer); timer = setTimeout(() => reset('Took too long: start again.'), 4000);
    const n = times.length - 1;
    if (n < need()) { status.textContent = n === 0 ? 'Started: now tap ' + need() + '.' : 'Tap ' + n + ' of ' + need(); return; }
    clearTimeout(timer);
    const ms = times.slice(1).map((x) => Math.round(x - times[0]));
    post('taps ' + ms.join(' ')); setMany([[pat, 24]]);
    if (P(mode() + ' ' + (S.tapoEdit === 2 && tapoBoth() ? 'B' : 'A') + ' Time Sync').v >= 0.5) toast('Tapped. Turn Sync off on that part to keep your exact timing.');
    reset('Tapped ' + need() + ' taps over ' + (ms[ms.length - 1] / 1000).toFixed(2) + ' s.');
  });
  bind(cnt, () => reset());
  const sg = seg(cnt, ['4', '8', '16']); sg.classList.add('sm');
  const use = S.patBtns[24]; use.style.cssText = 'height:30px;padding:0 10px;border:1px solid var(--line);border-radius:6px;background:var(--card);font-weight:700;color:var(--dim);font-size:11px';
  bind(pat, () => { const on = Math.round(pat.v) === 24; use.style.background = on ? 'var(--ink)' : 'var(--card)'; use.style.color = on ? 'var(--onink)' : 'var(--dim)'; });
  return h('div', { class: 'row', style: 'gap:8px' }, tip(pad, 'Tap', 'Click to start, then click in the rhythm you want: each click is an echo. The pattern becomes Tapped and plays back as you tapped it.'), sg, tip(use, 'Tapped', 'Play the pattern you tapped (saved with the project).'), status);
}
function morphBox() {
  const tm = P(tapoName('Juba Morph Timing')), box = h('div', { class: 'col', style: 'gap:4px;align-items:center' }), inner = h('div');
  const sg = seg(tm, ['Free', 'Sync']); sg.classList.add('sm');
  bind(tm, () => inner.replaceChildren(Math.round(tm.v) === 1 ? labelled('Morph', sel(P(tapoName('Juba Morph Sync')), 72)) : knob(P(tapoName('Juba Morph')), 'Morph', ink, 40)));
  box.append(tip(sg, 'Morph timing', 'Free: the glide between patterns in milliseconds. Sync: in note values, at the song’s tempo.'), inner);
  return box;
}
// the kill switch: a press empties every loop
function killButton() {
  const b = h('button', { type: 'button', class: 'btn', text: 'Kill', style: 'height:28px;font-weight:800;padding:0 8px' });
  b.onclick = () => { const k = P('Kill'); setMany([[k, 1]]); setTimeout(() => setMany([[k, 0]]), 80); };
  S.killBtn = b; return b;
}
// ----- Tone: the filter, the tilts, Runaway, the crusher, and what the sound looks like
function pageTone() {
  const tilt = card('width:290px;flex:none', h('div', { class: 'row' }, hd2('TILT'), h('span', { class: 'hint', text: 'into and out of the delays' })),
    h('div', { class: 'row', style: 'gap:10px;align-items:flex-end' }, knob(P('Tilt In'), 'In', ink, 40), knob(P('Tilt In Pivot'), 'Pivot', ink, 30), vline(), knob(P('Tilt Out'), 'Out', ink, 40), knob(P('Tilt Out Pivot'), 'Pivot', ink, 30)));
  const hold = h('button', { type: 'button', class: 'tog' }, h('i'), 'Hold');
  const hp = P('Runaway Hold');
  hold.addEventListener('pointerdown', () => { gesture(hp, true); setP(hp, 1); });
  const up = () => { if (hp.v >= 0.5) { setP(hp, 0); gesture(hp, false); } };
  hold.addEventListener('pointerup', up); hold.addEventListener('pointerleave', up);
  bind(hp, () => hold.classList.toggle('on', hp.v >= 0.5));
  S.rwMeters = [0, 1, 2].map((b) => { const i = h('i'); i.style.background = bandFill(b); return [h('div', { class: 'rmeter' }, i), i]; });
  const randRun = () => setMany([[P('Runaway Tame'), rnd(0.2, 0.9)], [P('Runaway Ceiling'), rnd(-24, -9)], [P('Loop Low Cut'), Math.log2(rnd(30, 300))], [P('Loop High Cut'), Math.log2(rnd(2500, 14000))]]);
  const run = card('flex:1', h('div', { class: 'row' }, tip(hd2('RUNAWAY'), 'Runaway', 'Feedback past 100 % rings on, held under the ceiling. Drive (on each part) pushes up to +21 dB into the loop. Tame: high holds a smooth drone, low lets it bite and sputter.'), h('span', { class: 'hint', text: 'Feedback past 100 % rings on, held under the ceiling.' }), h('div', { class: 'sp' }), dice(ink, 'Random: tame, ceiling, loop cuts', randRun)),
    h('div', { class: 'row', style: 'gap:10px;align-items:flex-end' }, knob(P('Runaway Tame'), 'Tame', ink, 42), knob(P('Runaway Ceiling'), 'Ceiling', ink, 42), knob(P('Loop Low Cut'), 'Low cut', ink, 36), knob(P('Loop High Cut'), 'High cut', ink, 36), knob(P('Runaway Return'), 'Return', ink, 36),
      tip(hold, 'Hold', 'While held, every part’s feedback goes to 112 % and the loop builds; let go and it glides back over Return. Map it to a button (MIDI Learn, Momentary).'),
      vline(), tip(toggle(P('Auto Kill'), 'Auto kill'), 'Auto kill', 'Empties the loops when a runaway (the ceiling holding it) goes on longer than After. Turn it off for endless drones.'), knob(P('Auto Kill After'), 'After (s)', ink, 34)),
    h('div', { class: 'row', style: 'gap:6px' }, h('div', { class: 'lbl', text: 'Holding' }), ...S.rwMeters.map(([e]) => e)),
    h('div', { class: 'hint', text: 'Drive on each part pushes up to +21 dB into the loop. Tame high: a smooth drone; low: it bites and sputters.' }));
  const randCrush = () => setMany([[P('Crusher Bits'), rnd(4, 14)], [P('Crusher Rate'), Math.log2(rnd(3000, 30000))], [P('Crusher Mix'), rnd(0.3, 1)], [P('Crusher Steps'), chance(0.5) ? 1 : 0]]);
  const steps = seg(P('Crusher Steps'), ['Raw', 'Smooth']); steps.classList.add('sm');
  const crush = card('width:290px;flex:none', h('div', { class: 'row' }, hd2('CRUSHER'), h('div', { class: 'sp' }), seg(P('Crusher Place'), ['Loop', 'Out']), dice(ink, 'Random: bits, rate, mix', randCrush)),
    h('div', { class: 'row', style: 'gap:8px;align-items:flex-end' }, knob(P('Crusher Bits'), 'Bits', ink, 34), knob(P('Crusher Rate'), 'Rate', ink, 34), knob(P('Crusher Mix'), 'Mix', ink, 34),
      h('div', { class: 'col', style: 'gap:4px' }, steps, tip(toggle(P('Chip Mode'), 'Chip'), 'Chip mode', 'Steps the bits and rate to an old sound chip’s: coarse, buzzy, in tune with itself.'))));
  const sp = h('canvas', { style: 'flex:1;width:0;min-width:0;min-height:70px;background:var(--graph);border:1px solid var(--line);border-radius:8px', 'aria-label': 'Spectrum: your sound and the echoes' });
  S.specCanvas = sp;
  const left = h('div', { class: 'col', style: 'flex:1;gap:10px;min-width:0' }, h('div', { class: 'row', style: 'gap:10px;align-items:stretch' }, run), h('div', { class: 'row', style: 'gap:10px;align-items:stretch;flex:1;min-height:0' }, tilt, crush, sp));
  return h('div', {}, left);
}
function drawSpectrum(u) {
  const cv = S.specCanvas; if (!cv || !cv.isConnected || S.page !== 'tone') return;
  const [g, W, H] = prep(cv); g.fillStyle = T.graph; g.fillRect(0, 0, W, H);
  const sp = u && u.spec; if (!sp) return;
  const y = (db) => H - 4 - (Math.max(-90, Math.min(6, db)) + 90) / 96 * (H - 8);
  [[1, T.inkA(0.18), true], [3, bandColour(1), false]].forEach(([w, c, fillIt]) => {
    const a = sp[w]; if (!a) return; g.beginPath();
    a.forEach((v, i) => { const x = i / (a.length - 1) * W; i ? g.lineTo(x, y(v)) : g.moveTo(x, y(v)); });
    if (fillIt) { g.lineTo(W, H); g.lineTo(0, H); g.closePath(); g.fillStyle = c; g.fill(); } else { g.strokeStyle = c; g.lineWidth = 2; g.stroke(); }
  });
  [0, 1, 2].forEach((b) => { const cp = P(BAND[b] + ' Filter Cutoff'), fc = 440 * Math.pow(2, (S.mods[cp.id] ?? cp.v) / 12);
  if (Math.round(P(BAND[b] + ' Filter Type').v) > 0) { const x = Math.log(fc / 20) / Math.log(1000) * W; g.strokeStyle = bandColour(b); g.setLineDash([4, 3]); g.beginPath(); g.moveTo(x, 4); g.lineTo(x, H - 4); g.stroke(); g.setLineDash([]); } });
  g.fillStyle = T.label; g.font = '10px -apple-system, sans-serif'; g.textAlign = 'left'; g.fillText('your sound (shaded) · the echoes (line)', 8, 13);
}

// ----- Hymn: Three's Company's Tune bank, plus each part's shift
function pageHymn() {
  const d = pageTune();
  const row = (b) => {
    const sh = P(BAND[b] + ' Shift'), md = P(BAND[b] + ' Shift Mode'), sg = seg(md, ['Off', 'Once', 'Each']); sg.classList.add('sm', 'tight');
    sg.style.setProperty('--segc', bandFill(b)); sg.style.setProperty('--segt', textOn(bandFill(b)));
    return h('div', { class: 'row', style: 'gap:8px' }, btag(b), h('div', { class: 'hk' }, knob(sh, 'Shift', () => bandColour(b), 24)), sg);
  };
  const keyTxt = h('span', { class: 'secs' }); S.keyTxt = keyTxt;
  const shift = card('width:286px;flex:none;gap:7px', h('div', { class: 'row' }, tip(hd2('SHIFT'), 'Shift', 'Moves the echoes by semitones in the key. Once: one shift. Each: every repeat goes further.'), h('div', { class: 'sp' }), dice(ink, 'Random: shifts', () => setMany([0, 1, 2].flatMap((b) => [[P(BAND[b] + ' Shift'), pick([0, 0, 5, 7, 12, -12, -5])], [P(BAND[b] + ' Shift Mode'), pick([0, 1, 2])]])))),
    row(0), row(1), row(2),
    tip(toggle(P('Tune Only Ringing'), 'Only the ringing'), 'Only the ringing', 'With Tune on, you hear only what Tune rings out of your sound (and its echoes), never the sound itself: at Amount 100 % a clap becomes just its rhythm, played by the notes of the key.'),
    h('div', { class: 'row', style: 'gap:8px' }, tip(toggle(P('Shift Settle'), 'Settle'), 'Settle', 'Each repeat on short echoes: the shift waits until the echoes are long enough to hear each step, so fast repeats don’t smear into noise.'),
      tip(toggle(P('MIDI Sets Key'), 'MIDI key'), 'MIDI sets the key', 'Hold notes on Gullay’s MIDI input: the lowest one becomes Hymn’s root (Tune and the shifts snap to it). Letting go keeps the last key.'), keyTxt));
  d.appendChild(shift);
  d.appendChild(card('width:236px;flex:none;gap:6px', hd2('SHAPE'), hymnShapeRow()));
  return d;
}

function hymnShapeRow() {
  const sh = P('Hymn Shape'), sg = seg(sh, ['Free', 'Envelope'], { values: [0, 3] }); sg.classList.add('sm');
  const cv = h('canvas', { width: 130, height: 44, style: 'width:130px;height:44px;border-radius:6px;background:var(--graph);border:1px solid var(--line)', 'aria-hidden': 'true' });
  const draw = () => { const g = cv.getContext('2d'), W = 130, Hh = 44; g.clearRect(0, 0, W, Hh);
    const a = Math.pow(2, P('Hymn Attack').v), d = Math.pow(2, P('Hymn Decay').v), su = P('Hymn Sustain').v, r = Math.pow(2, P('Hymn Release').v), hold = 400, tot = a + d + hold + r, sx = (W - 8) / tot, y0 = Hh - 5, yt = 5;
    const pts = [[4, y0], [4 + a * sx, yt], [4 + (a + d) * sx, y0 - (y0 - yt) * su], [4 + (a + d + hold) * sx, y0 - (y0 - yt) * su], [4 + tot * sx, y0]];
    g.beginPath(); pts.forEach(([x, y], i) => i ? g.lineTo(x, y) : g.moveTo(x, y)); g.lineTo(pts[0][0], y0); g.fillStyle = bandFill(1); g.globalAlpha = 0.25; g.fill(); g.globalAlpha = 1;
    g.beginPath(); pts.forEach(([x, y], i) => i ? g.lineTo(x, y) : g.moveTo(x, y)); g.strokeStyle = T.ink; g.lineWidth = 1.6; g.stroke();
    g.fillStyle = T.label; g.font = '9px -apple-system, sans-serif'; g.textAlign = 'center'; ['A', 'D', 'S', 'R'].forEach((l, i) => { const x = (pts[i][0] + pts[i + 1][0]) / 2; g.fillText(l, x, Hh - 1); }); };
  ['Hymn Attack', 'Hymn Decay', 'Hymn Sustain', 'Hymn Release'].forEach((n) => bind(P(n), draw));
  const knobs = h('div', { class: 'row', style: 'gap:4px;align-items:flex-end' },
    knob(P('Hymn Attack'), 'Attack', ink, 26), knob(P('Hymn Decay'), 'Decay', ink, 26), knob(P('Hymn Sustain'), 'Sustain', ink, 26), knob(P('Hymn Release'), 'Release', ink, 26));
  const env = h('div', { class: 'col', style: 'gap:4px' }, h('div', { class: 'row', style: 'gap:8px;align-items:flex-end' }, cv, tip(knob(P('Hymn Threshold'), 'Trigger', ink, 24), 'Trigger', 'How loud your sound has to get to start the envelope.')), knobs);
  bind(sh, () => { env.style.opacity = Math.round(sh.v) === 0 ? 0.35 : 1; });
  return h('div', { class: 'col', style: 'gap:4px' }, tip(h('div', { class: 'row', style: 'gap:6px' }, sg), 'Shape',
    'Free: the ringing rings for Ring time. Envelope: each time your sound passes Trigger, the ringing rises over Attack, falls over Decay to Sustain, holds while your sound stays loud, then fades over Release.'), env);
}
// ----- Reverb
function pageReverb() {
  const randRv = () => setMany([[P('Reverb Size'), rnd(0.3, 1)], [P('Reverb Decay'), rnd(0.4, 0.95)], [P('Reverb Diffusion'), rnd(0.4, 1)], [P('Reverb Mod Depth'), rnd(0, 0.7)], [P('Reverb Mod Rate'), rnd(0, 0.7)], [P('Reverb Pre-delay'), rnd(0, 0.4)]]);
  const main = card('flex:1', h('div', { class: 'row' }, hd2('REVERB'), toggle(P('Reverb On'), 'On'), seg(P('Reverb Flavour'), ['Plateau', 'Cloud Seed']), h('div', { class: 'sp' }), dice(ink, 'Random: the reverb’s space', randRv)),
    h('div', { class: 'row', style: 'gap:12px;align-items:flex-end;flex-wrap:wrap' }, knob(P('Reverb Size'), 'Size', ink, 50), knob(P('Reverb Decay'), 'Decay', ink, 50), knob(P('Reverb Pre-delay'), 'Pre-delay', ink, 40),
      knob(P('Reverb Diffusion'), 'Diffusion', ink, 40), vline(), knob(P('Reverb Mod Depth'), 'Mod depth', ink, 40), knob(P('Reverb Mod Rate'), 'Mod rate', ink, 40), knob(P('Reverb Mod Shape'), 'Mod shape', ink, 40),
      vline(), knob(P('Reverb Low Cut'), 'Low cut', ink, 40), knob(P('Reverb High Cut'), 'High cut', ink, 40)),
    h('div', { class: 'row', style: 'gap:8px' }, tip(toggle(P('Reverb Freeze'), 'Freeze'), 'Freeze', 'Holds the reverb’s tail forever and stops new sound coming in.'),
      tip(toggle(P('Reverb Tuned'), 'Tuned'), 'Tuned', 'Plateau only: tunes the tank so it rings on a pitch.'), tip(toggle(P('Reverb Follows Key'), 'Follows key'), 'Follows key', 'The tuned tank follows Hymn’s key.')),
    h('div', { class: 'hint', text: 'Plateau: the big, smooth tank. Cloud Seed: wide, grainy, very long. The same knobs drive both.' }));
  const send = card('width:300px;flex:none', hd2('SEND AND RETURN'), h('div', { class: 'row', style: 'gap:12px;align-items:flex-end' }, knob(P('Reverb Send'), 'Send', ink, 46), knob(P('Reverb Level'), 'Level', ink, 46), knob(P('Reverb into Delays'), 'Into delays', ink, 46)),
    h('div', { class: 'hint', text: 'Send: how much of the echoes go into the reverb. Into delays: the reverb fed back into the delays, for washes that build.' }));
  return h('div', {}, main, send);
}

// ----- the River: three currents read off the course of the Ogeechee (a stand-in course for now)
function riverValue(pos, meander) {
  const x = pos - Math.floor(pos), tw = 2 * Math.PI;
  const drift = 0.55 * Math.sin(tw * x) + 0.15 * Math.sin(tw * 2 * x + 0.9);
  const bends = 0.22 * Math.sin(tw * 9 * x + 0.3) * (0.6 + 0.4 * x) + 0.12 * Math.sin(tw * 23 * x + 1.7) * x + 0.06 * Math.sin(tw * 51 * x + 0.2) * x * x;
  return Math.max(-1, Math.min(1, drift + meander * bends * 1.6));
}
function pageRiver() {
  const cv = h('canvas', { style: 'flex:1;width:100%;min-height:110px;background:var(--graph);border:1px solid var(--line);border-radius:8px', 'aria-label': 'The river and its three currents' });
  S.riverCanvas = cv;
  const rate = h('div');
  bind(P('River Timing'), (p) => { rate.replaceChildren(Math.round(p.v) === 0 ? labelled('One pass', sel(P('River Pass'), 80)) : knob(P('River Rate'), 'Rate', ink, 40)); });
  const randRiver = () => setMany([[P('River Meander'), rnd(0.2, 1)], [P('River Spread'), rnd(0, 0.8)], [P('River Pass'), Math.floor(Math.random() * 7)]]);
  const c1 = card('flex:1', h('div', { class: 'row' }, hd2('THE RIVER'), h('span', { class: 'hint', text: 'Three currents, A, Lead and B, read off the river’s course as one pass goes by.' }), h('div', { class: 'sp' }), dice(ink, 'Random: meander, spread, pass', randRiver)),
    cv,
    h('div', { class: 'row', style: 'gap:22px;align-items:flex-end' }, labelled('Timing', seg(P('River Timing'))), rate, vline(), knob(P('River Meander'), 'Meander', ink, 40), knob(P('River Spread'), 'Spread', ink, 40), vline(),
      tip(toggle(P('River Tide'), 'Tide'), 'Tide', 'A tidal reach: every few bars the flow turns and runs back up the river.'), labelled('Turns every', sel(P('Tide Turns Every'), 80))));
  const c2 = card('width:420px;flex:none;gap:8px;align-self:flex-start', h('div', { class: 'row' }, hd2('SENDS'), h('span', { class: 'hint', text: 'where the currents go (Mod slots 13–16)' })),
    ...[13, 14, 15, 16].map((k) => riverSend(k)),
    h('div', { class: 'row', style: 'gap:14px;border-top:1px solid var(--line);padding-top:6px' }, knob(P('Depth River A'), 'A depth', () => bandColour(0), 26), knob(P('Depth River Lead'), 'Lead depth', () => bandColour(1), 26), knob(P('Depth River B'), 'B depth', () => bandColour(2), 26)));
  return h('div', {}, c1, c2);
}
// one River send: which current (or off), where it goes, how far
function riverSend(k) {
  const pre = 'Mod ' + k + ' ', src = P(pre + 'Source'), vals = [0, 8, 9, 10], names = ['Off', 'A', 'Lead', 'B'];
  const sg = h('div', { class: 'seg sm' }), bs = names.map((n, i) => { const b = h('button', { type: 'button', text: n, onclick: () => setMany([[src, vals[i]]]) }); sg.appendChild(b); return b; });
  bind(src, () => { const v = Math.round(src.v); bs.forEach((b, i) => b.classList.toggle('on', vals[i] === v)); if (v !== 0 && vals.indexOf(v) < 0) bs.forEach((b) => b.classList.remove('on')); });
  return h('div', { class: 'row', style: 'gap:6px' }, h('span', { class: 'hint', text: String(k - 12) }), sg, sel(P(pre + 'Target'), 120), h('div', { style: 'flex:1;min-width:120px;display:flex' }, bipBar(P(pre + 'Amount'), pre)));
}
function drawRiverPage(u) {
  const cv = S.riverCanvas; if (!cv || !cv.isConnected || S.page !== 'river') return;
  const [g, W, H] = prep(cv); g.fillStyle = T.graph; g.fillRect(0, 0, W, H);
  const mea = P('River Meander').v, spr = P('River Spread').v, pos = (u && u.gy) ? u.gy.riverPos : 0, t = performance.now() / 1000;
  const N = 240, yAt = (f) => H / 2 - riverValue(f, mea) * H * 0.36, wAt = (f) => 9 + 7 * (0.5 + 0.5 * Math.sin(f * 17.3 + 1.7)) + 6 * f; // widens toward the coast
  const top = [], bot = [];
  for (let i = 0; i <= N; ++i) { const f = i / N, x = f * W, y = yAt(f), y2 = yAt(Math.min(1, f + 1 / N)), y0 = yAt(Math.max(0, f - 1 / N));
    const ang = Math.atan2(y2 - y0, 2 * W / N), nx = -Math.sin(ang), ny = Math.cos(ang), w = wAt(f);
    top.push([x + nx * w, y - ny * w]); bot.push([x - nx * w, y + ny * w]); }
  const rc = riverColour();
  // banks: a soft shoulder of land either side
  g.save(); g.globalAlpha = 0.18; g.strokeStyle = T.dim; g.lineWidth = 10; g.lineJoin = 'round';
  [top, bot].forEach((edge) => { g.beginPath(); edge.forEach(([x, y], i) => i ? g.lineTo(x, y) : g.moveTo(x, y)); g.stroke(); }); g.restore();
  // the water: deeper in the middle
  g.beginPath(); top.forEach(([x, y], i) => i ? g.lineTo(x, y) : g.moveTo(x, y)); for (let i = N; i >= 0; --i) g.lineTo(bot[i][0], bot[i][1]); g.closePath();
  const gr = g.createLinearGradient(0, 0, 0, H); gr.addColorStop(0, rc); gr.addColorStop(1, rc); g.fillStyle = gr; g.globalAlpha = 0.55; g.fill(); g.globalAlpha = 1;
  g.strokeStyle = rc; g.lineWidth = 1.5; g.stroke();
  // the current: a deep line and ripples drifting downstream
  g.beginPath(); for (let i = 0; i <= N; ++i) { const f = i / N; i ? g.lineTo(f * W, yAt(f)) : g.moveTo(0, yAt(0)); }
  g.strokeStyle = rc; g.lineWidth = 3; g.globalAlpha = 0.9; g.stroke();
  g.setLineDash([6, 14]); g.lineDashOffset = -t * 18; g.strokeStyle = T.graph; g.lineWidth = 1.6; g.globalAlpha = 0.75; g.stroke();
  for (const off of [-0.45, 0.45]) { g.beginPath(); for (let i = 0; i <= N; ++i) { const f = i / N, x = top[i][0] * (0.5 - off) + bot[i][0] * (0.5 + off), y = top[i][1] * (0.5 - off) + bot[i][1] * (0.5 + off); i ? g.lineTo(x, y) : g.moveTo(x, y); }
    g.setLineDash([3, 19]); g.lineDashOffset = -t * 11; g.lineWidth = 1; g.globalAlpha = 0.5; g.stroke(); }
  g.setLineDash([]); g.globalAlpha = 1; g.lineWidth = 1;
  for (let b = 0; b < 3; ++b) {
    const p = (pos + spr * b * 0.25) % 1, x = p * W, y = yAt(p);
    g.fillStyle = bandFill(b); g.beginPath(); g.arc(x, y, 7, 0, Math.PI * 2); g.fill(); g.strokeStyle = T.graph; g.lineWidth = 2; g.stroke();
    g.font = 'italic 800 11px "Avenir Next", -apple-system, sans-serif'; g.textAlign = 'center'; g.lineWidth = 3; g.strokeStyle = T.graph; g.strokeText(BAND[b], x, y - 12); g.fillStyle = T.ink; g.fillText(BAND[b], x, y - 12);
  }
  g.fillStyle = T.label; g.font = '10px -apple-system, sans-serif'; g.textAlign = 'left'; g.fillText('upstream', 6, H - 6); g.textAlign = 'right'; g.fillText('to the coast', W - 6, H - 6);
}

// ----- MIDI: every learnt control, its kind, behaviour, pickup and range
const MIDI_KINDS = ['Absolute', '14-bit', 'NRPN', 'Relative 1', 'Relative 2', 'Relative 3'];
const MIDI_BEH = ['Knob', 'Toggle', 'Momentary'];
function mapLabel(m) { return 'ch ' + (m.ch + 1) + ' · ' + (m.kind === 2 ? 'NRPN ' : 'CC ') + m.num; }
function startLearn(p) {
  S.learning = p.id; post('learn ' + p.id + ' ' + S.learnKind);
  toast('MIDI Learn: move a control for ' + p.n);
}
function pageMidi() {
  const kind = h('div', { class: 'seg sm' });
  const kbs = MIDI_KINDS.map((t, i) => { const b = h('button', { type: 'button', text: t, onclick: () => { S.learnKind = i; kbs.forEach((x, k) => x.classList.toggle('on', k === i)); } }); kind.appendChild(b); return b; });
  kbs.forEach((x, k) => x.classList.toggle('on', k === S.learnKind));
  const list = h('div', { class: 'col', style: 'gap:4px;overflow:auto;min-height:0;flex:1' }); S.mapList = list;
  const learnCard = card('flex:1;min-height:0', h('div', { class: 'row' }, hd2('MIDI LEARN'), h('span', { class: 'hint', text: 'Right-click any knob → MIDI Learn…, then move a control.' }), h('div', { class: 'sp' }),
      h('button', { type: 'button', class: 'btn', text: 'Save as default', onclick: () => post('mapdefault save') }), h('button', { type: 'button', class: 'btn', text: 'Load default', onclick: () => post('mapdefault load') }),
      h('button', { type: 'button', class: 'btn', text: 'Clear all', onclick: () => confirmBox('Forget every MIDI mapping in this project?', 'Clear', 'Cancel', () => post('mapdel -1')) })),
    h('div', { class: 'hint', text: 'Works with any controller: the kind of message is found by itself. If an endless knob jumps, change its Kind in the list.' }),
    h('div', { class: 'maprow maphead' }, h('span', { text: 'Control' }), h('span', { text: 'Ch' }), h('span', { text: 'Number' }), h('span', { text: 'Kind' }), h('span', { text: 'Behaviour' }), h('span', { text: 'Pickup' }), h('span', { text: 'Range %' }), h('span')),
    list);
  const ext = card('width:260px;flex:none', hd2('MIDI IN'), tip(toggle(P('MIDI Sets Key'), 'Notes set Hymn’s key'), 'MIDI sets the key', 'Play or hold notes and Hymn follows: the lowest held note becomes its root.'),
    tip(toggle(P('MIDI Picks Patterns'), 'Notes / PC pick patterns'), 'MIDI picks patterns', 'C1–B2 and Program Change 1–24 pick the Juba pattern.'),
    h('div', { class: 'hint', text: 'Mappings are saved with the project. Save as default: new copies of Gullay start with them.' }));
  fillMaps();
  return h('div', {}, learnCard, ext);
}
function fillMaps() {
  const list = S.mapList; if (!list) return; list.innerHTML = '';
  if (!S.maps.length) { list.appendChild(h('div', { class: 'hint', text: 'Nothing mapped yet.' })); return; }
  S.maps.forEach((m) => {
    const send = (o) => { const x = Object.assign({}, m, o); post(`mapset ${x.slot} ${x.ch} ${x.num} ${x.kind} ${x.beh} ${x.pickup} ${x.lo.toFixed(4)} ${x.hi.toFixed(4)}`); };
    const ddOf = (labels, cur, on, w) => { const b = h('button', { type: 'button', class: 'dd', style: `width:${w}px` }, h('span', { class: 't', text: labels[cur] }), h('span', { class: 'car', text: '▾' }));
      b.onclick = (e) => { e.stopPropagation(); openPop(b, labels.map((l, i) => ({ label: l, value: i })), cur, on, { width: w + 20 }); }; return b; };
    const num = (v, on) => { const i = h('input', { type: 'number', min: 0, max: 100, step: 1, value: Math.round(v * 100) }); i.onchange = () => on(Math.max(0, Math.min(100, +i.value || 0)) / 100); return i; };
    const chIn = h('input', { type: 'number', min: 1, max: 16, value: m.ch + 1, style: 'width:40px' }); chIn.onchange = () => send({ ch: Math.max(1, Math.min(16, +chIn.value || 1)) - 1 });
    const nIn = h('input', { type: 'number', min: 0, max: 16383, value: m.num }); nIn.onchange = () => send({ num: Math.max(0, +nIn.value || 0) });
    const pk = h('button', { type: 'button', class: 'tog' + (m.pickup ? ' on' : '') }, h('i'), 'Pickup'); pk.onclick = () => send({ pickup: m.pickup ? 0 : 1 });
    list.appendChild(h('div', { class: 'maprow' }, h('span', { text: m.name, style: 'overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-weight:600' }), chIn, nIn,
      ddOf(MIDI_KINDS, m.kind, (v) => send({ kind: v }), 130), ddOf(MIDI_BEH, m.beh, (v) => send({ beh: v }), 90), pk,
      h('div', { class: 'row', style: 'gap:3px' }, num(m.lo, (v) => send({ lo: v })), num(m.hi, (v) => send({ hi: v }))),
      h('button', { type: 'button', class: 'ibtn', text: '✕', 'aria-label': 'Forget this mapping', onclick: () => post('mapdel ' + m.slot) })));
  });
}
function snapshotDialog() {
  const old = document.querySelector('.snapbox'); if (old) old.remove();
  const inp = h('input', { type: 'text', placeholder: 'What were you doing? What did you hear?' });
  const close = () => box.remove();
  const box = h('div', { class: 'modal savebox snapbox' }, h('div', { class: 'mbox' }, h('p', { text: 'Snapshot: every setting and your note, saved as a text file (the folder opens).' }), inp,
    h('div', { class: 'row', style: 'justify-content:flex-end;margin-top:12px' }, h('button', { type: 'button', class: 'btn', text: 'Cancel', onclick: close }),
      h('button', { type: 'button', class: 'btn on', text: 'Save snapshot', onclick: () => { post('snapshot ' + inp.value.replace(/[\r\n]+/g, ' ')); close(); } }))));
  box.addEventListener('pointerdown', (e) => { if (e.target === box) close(); });
  document.getElementById('stage').appendChild(box); setTimeout(() => inp.focus(), 0);
}
document.addEventListener('click', (e) => { if (e.target && e.target.id === 'snapBtn') snapshotDialog(); });
Object.assign(window.tc, {
  maps(list) { S.maps = list || []; fillMaps(); },
  snapshot(path) { toast(path ? 'Bug report saved: ' + path.split(/[\\/]/).pop() : 'Could not write the bug report'); },
  note(t) { toast(t); }
});

// ----- live: the parts' readouts, Runaway, the pattern playing, the River, Learn
function fmtSec(s) { return s >= 1 ? s.toFixed(2) + ' s' : Math.round(s * 1000) + ' ms'; }
function gyLive(u) {
  const g = u.gy; if (!g) return;
  (S.partUi || []).forEach((pu, b) => { pu.secs.textContent = fmtSec(g.sec[b]) + (g.inf[b] ? ' · INF' : ''); pu.rm.style.width = Math.round(Math.min(1, Math.max(0, 1 - g.runaway[b])) * 100) + '%'; });
  (S.rwMeters || []).forEach(([, i], b) => { i.style.width = Math.round(Math.min(1, Math.max(0, 1 - g.runaway[b])) * 100) + '%'; });
  if (S.patBtns) S.patBtns.forEach((b, i) => b.classList.toggle('now', Math.round(g.pattern) === i));
  if (S.keyTxt) S.keyTxt.textContent = g.key >= 0 ? 'key ' + ['C', 'C♯', 'D', 'D♯', 'E', 'F', 'F♯', 'G', 'G♯', 'A', 'A♯', 'B'][g.key] : '';
  if (S.killsSeen !== undefined && g.kills > S.killsSeen) { toast('Killed: every loop emptied'); if (S.killBtn) { S.killBtn.classList.add('on'); setTimeout(() => S.killBtn && S.killBtn.classList.remove('on'), 400); } }
  S.killsSeen = g.kills;
  if (S.killBtn) { const warn = g.runT >= 10; S.killBtn.classList.toggle('warn', warn); S.killBtn.title = warn ? 'Running away for ' + Math.round(g.runT) + ' s' + (P('Auto Kill').v >= 0.5 ? ' (auto kill at ' + Math.round(P('Auto Kill After').v) + ' s)' : '') : 'Kill: empty every loop'; }
  if (S.lockInBtn) S.lockInBtn.classList.toggle('live', !!g.locked);
  if (S.paintSnapPos && S.page === 'snaps') S.paintSnapPos(u);
  if (S.learning !== null && g.learn < 0) { S.learning = null; toast('Learnt'); post('maps'); }
  S.riverPos = g.riverPos; S.riverAt = performance.now();
  if (g.lvl) { const w = S.wl || (S.wl = { a: [], t: [] }); w.a.push(g.lvl); w.t.push(performance.now()); while (w.a.length > 420) { w.a.shift(); w.t.shift(); } }
  drawRiverPage(u); drawSpectrum(u);
}

// ----- the water line: your sound above, the echoes below, each part's echo time marked
function drawGraph() {
  const cv = document.getElementById('graph'); if (!cv || !S.ready) return;
  return drawEcho(cv);
  const [g, W, H] = prep(cv); g.fillStyle = T.graph; g.fillRect(0, 0, W, H);
  const w = S.wl, y0 = H * 0.46;
  g.strokeStyle = riverColour(); g.lineWidth = 2; g.beginPath(); g.moveTo(0, y0); g.lineTo(W, y0); g.stroke(); g.lineWidth = 1;
  if (!w || w.a.length < 2) { g.fillStyle = T.label; g.font = '11px -apple-system, sans-serif'; g.textAlign = 'center'; g.fillText('The water line: play something', W / 2, y0 - 8); return; }
  const now = w.t[w.t.length - 1], span = Math.max(1000, now - w.t[0]), xOf = (t) => W - (now - t) / span * W;
  const sc = (v) => Math.min(1, Math.sqrt(Math.max(0, v)));
  g.fillStyle = T.inkA(0.5); g.beginPath(); g.moveTo(xOf(w.t[0]), y0);
  w.a.forEach((l, i) => g.lineTo(xOf(w.t[i]), y0 - sc(l[0]) * (y0 - 4))); g.lineTo(W, y0); g.closePath(); g.fill();
  g.fillStyle = bandColour(1); g.globalAlpha = 0.55; g.beginPath(); g.moveTo(xOf(w.t[0]), y0);
  w.a.forEach((l, i) => g.lineTo(xOf(w.t[i]), y0 + sc(l[1]) * (H - y0 - 4))); g.lineTo(W, y0); g.closePath(); g.fill(); g.globalAlpha = 1;
  [2, 3].forEach((k) => { g.strokeStyle = bandColour(k === 2 ? 0 : 2); g.lineWidth = 1.5; g.beginPath(); w.a.forEach((l, i) => { const x = xOf(w.t[i]), y = y0 + sc(l[k]) * (H - y0 - 4); i ? g.lineTo(x, y) : g.moveTo(x, y); }); g.stroke(); });
  const gy = S.upd && S.upd.gy;
  if (gy) [0, 2].forEach((b) => { const x = W - gy.sec[b] * 1000 / span * W; if (x < 30) return; g.strokeStyle = bandColour(b); g.setLineDash([3, 3]); g.beginPath(); g.moveTo(x, 4); g.lineTo(x, H - 4); g.stroke(); g.setLineDash([]);
    g.fillStyle = bandColour(b); g.font = 'italic 800 10px "Avenir Next", -apple-system, sans-serif'; g.textAlign = 'center'; g.fillText(BAND[b] + ' ' + fmtSec(gy.sec[b]), x, 12 + b * 5); });
  g.fillStyle = T.label; g.font = '10px -apple-system, sans-serif'; g.textAlign = 'left'; g.fillText('in', 6, 12); g.fillText('echoes', 6, H - 6);
}

// ----- the river behind everything, flowing at the River's pass length
(function riverLoop() {
  let still = 0;
  const frame = (t) => {
    const cv = document.getElementById('river');
    if (cv && S.ready && T.river) {
      const motion = S.motion || 'Full';
      if (S.riverStill === 0) still = 0;
      if (motion !== 'Still' || !still) {
        const [g, W, H] = prep(cv); g.clearRect(0, 0, W, H);
        const mea = P('River Meander') ? P('River Meander').v : 0.7;
        let pos = S.riverPos || 0; if (S.riverAt && motion === 'Full') pos += 0;
        const calm = motion === 'Calm', alpha = calm ? 0.5 : 0.8, flow = motion === 'Still' ? 0 : t / (calm ? 90 : 40);
        g.lineCap = 'round';
        for (let k = 0; k < (calm ? 5 : 8); ++k) {
          g.strokeStyle = riverColour(); g.globalAlpha = alpha * (1 - k * 0.06); g.lineWidth = 5 + 2 * (k % 3);
          g.setLineDash([60 + k * 17, 40 + k * 11]); g.lineDashOffset = -flow * (1 + k * 0.15);
          g.beginPath();
          for (let i = 0; i <= 120; ++i) { const x = i / 120 * W, y = H * 0.6 + (k - 3.5) * 18 - riverValue(pos + i / 120 * 0.35, mea) * H * 0.22; i ? g.lineTo(x, y) : g.moveTo(x, y); }
          g.stroke();
        }
        g.setLineDash([]); g.globalAlpha = 1; still = motion === 'Still' ? 1 : 0; S.riverStill = still;
      }
    }
    if (S.ready) { const gc = document.getElementById('graph'); if (gc) drawEcho(gc); }
    requestAnimationFrame(frame);
  };
  requestAnimationFrame(frame);
})();

// ----- the spectral split: your sound's spectrum, the three bands in their parts' colours, the split points to drag
const hzX = (f, W) => Math.log(Math.max(20, f) / 20) / Math.log(1000) * W;
function drawSplit(cv) {
  const [g, W, H] = prep(cv); g.fillStyle = T.graph; g.fillRect(0, 0, W, H);
  const tonal = tonalOn(), gy = S.upd && S.upd.gy, sp = S.upd && S.upd.spec;
  const lo = gy && gy.split ? gy.split[0] : P('Spectral Low/Mid').v, hi = gy && gy.split ? gy.split[1] : P('Spectral Mid/High').v;
  const f1 = 440 * Math.pow(2, lo / 12), f2 = 440 * Math.pow(2, hi / 12), x1 = hzX(f1, W), x2 = hzX(f2, W);
  const lv = ['Spectral Low Level', 'Spectral Mid Level', 'Spectral High Level'].map((n) => P(n).v);
  if (!tonal) [[0, x1, 0], [x1, x2, 1], [x2, W, 2]].forEach(([a, b, k]) => { g.fillStyle = bandColour(k); g.globalAlpha = 0.16 + 0.12 * Math.max(0, Math.min(1, (lv[k] + 60) / 72)); g.fillRect(a, 0, b - a, H); });
  g.globalAlpha = 1;
  if (sp && sp[1]) {
    const y = (db) => H - 4 - (Math.max(-90, Math.min(6, db)) + 90) / 96 * (H - 18);
    g.beginPath(); sp[1].forEach((v, i) => { const x = i / (sp[1].length - 1) * W; i ? g.lineTo(x, y(v)) : g.moveTo(x, y(v)); });
    g.lineTo(W, H); g.lineTo(0, H); g.closePath(); g.fillStyle = T.inkA(0.22); g.fill();
  }
  g.font = 'italic 800 11px "Avenir Next", -apple-system, sans-serif'; g.textAlign = 'center';
  const tag = (x, k, txt) => { g.fillStyle = bandColour(k); g.fillText(txt, x, 14); };
  if (tonal) {
    g.fillStyle = T.label; g.font = '11px -apple-system, sans-serif'; g.textAlign = 'left';
    g.fillText('Spectral by Tonal: steady tones → A (slot 1) · hits → both (Lead) · air → B (slot 2). Levels on the Spectral page.', 10, 16);
    return;
  }
  const db = (v) => (v <= -59.9 ? 'off' : (v >= 0 ? '+' : '') + v.toFixed(1) + ' dB');
  tag(x1 / 2, 0, 'LOW → A  ' + db(lv[0])); tag((x1 + x2) / 2, 1, 'MID → BOTH  ' + db(lv[1])); tag((x2 + W) / 2, 2, 'HIGH → B  ' + db(lv[2]));
  [[x1, f1], [x2, f2]].forEach(([x, f]) => { g.strokeStyle = T.ink; g.lineWidth = 2; g.beginPath(); g.moveTo(x, 20); g.lineTo(x, H - 4); g.stroke();
    g.fillStyle = T.ink; g.font = '700 10px -apple-system, sans-serif'; g.textAlign = 'center'; g.fillText(f >= 1000 ? (f / 1000).toFixed(f >= 10000 ? 0 : 1) + ' kHz' : Math.round(f) + ' Hz', x, H - 6); });
  g.lineWidth = 1;
}
(() => {
  const cv = document.getElementById('graph'); let dragging = null;
  const fOf = (e) => { const r = cv.getBoundingClientRect(); return 20 * Math.pow(1000, (e.clientX - r.left) / r.width); };
  cv.addEventListener('pointerdown', (e) => {
    if (!S.ready || !spectralOn() || tonalOn()) return; const f = fOf(e);
    const a = P('Spectral Low/Mid'), b = P('Spectral Mid/High'), hz = (st) => 440 * Math.pow(2, st / 12);
    const da = Math.abs(Math.log(f / hz(a.v))), dbb = Math.abs(Math.log(f / hz(b.v)));
    if (Math.min(da, dbb) > 0.35) return; dragging = da < dbb ? a : b; gesture(dragging, true); cv.setPointerCapture(e.pointerId);
  });
  cv.addEventListener('pointermove', (e) => { if (dragging) { setP(dragging, 12 * Math.log2(fOf(e) / 440)); drawGraph(); } });
  cv.addEventListener('pointerup', (e) => { if (dragging) { gesture(dragging, false); dragging = null; cv.releasePointerCapture(e.pointerId); } });
})();

// ----- how far each source reaches, wherever it is used
function depthRow() {
  const names = ['LFO 1', 'LFO 2', 'LFO 3', 'River A', 'River Lead', 'River B'];
  const col = (n) => () => (/ A$/.test(n) ? bandColour(0) : / B$/.test(n) ? bandColour(2) : / Lead$/.test(n) ? bandColour(1) : T.ink);
  return h('div', { class: 'row', style: 'gap:10px;align-items:flex-end' }, tip(h('h2', { class: 'hd', text: 'DEPTH', style: 'margin:0;font-size:15px' }), 'Source depth',
    'How far each source reaches, everywhere it is used (its own target, the matrix, the River sends). Pull one down to tame it without touching its routes.'),
    ...names.map((n) => knob(P('Depth ' + n), n.replace('Seq ', 'Seq ').replace('Follower', 'Follow'), col(n), 26)));
}
// ----- P-locks: how long the selected step's locks last
function lockLenBox() {
  const box = h('div', { class: 'row', style: 'gap:6px' });
  S.fillLockLen = () => {
    box.innerHTML = '';
    if (!S.lockSel) { box.appendChild(h('span', { class: 'hint', text: 'Select a step to set how long it lasts.' })); return; }
    const p = P('Lock ' + BAND[S.lockSel.b] + ' Steps ' + (S.lockSel.step + 1));
    box.append(btag(S.lockSel.b), h('span', { class: 'lbl', text: 'Step ' + (S.lockSel.step + 1) + ' lasts' }), stepper(p), h('span', { class: 'hint', text: 'steps' }));
  };
  S.fillLockLen();
  return box;
}
// the steps an earlier lock still holds get a faint mark
function markHeld() {
  const off = S.stepPage * 16;
  (S.lockCells || []).forEach((cells, b) => {
    const has = (i) => PLN.some((_, k) => lockP(b, k, i).v >= 0);
    const held = new Array(32).fill(false);
    for (let i = 0; i < 32; ++i) if (has(i)) { const n = Math.round(P('Lock ' + BAND[b] + ' Steps ' + (i + 1)).v); for (let d = 1; d < n && i + d < 32 && !has(i + d); ++d) held[i + d] = true; }
    cells.forEach((c, j) => c.el.classList.toggle('held', held[j + off]));
  });
}
// ----- the Chaperone's lock-in: keep the last steps it played, looping
function lockInBox() {
  const lp = P('Chaperone Lock'), b = h('button', { type: 'button', class: 'tog' }, h('i'), 'Lock in'); S.lockInBtn = b;
  b.onclick = () => setMany([[lp, lp.v >= 0.5 ? 0 : 1]]);
  bind(lp, () => b.classList.toggle('on', lp.v >= 0.5));
  const sg = seg(P('Chaperone Lock Steps'), ['4', '8', '16', '32']); sg.classList.add('sm');
  return tip(h('div', { class: 'row', style: 'gap:4px' }, b, sg), 'Lock in', 'Heard something you like? Lock in keeps the last 4, 8, 16 or 32 steps the Chaperone played and loops them. Turn it off to let it roam again.');
}

// ----- the echo map: where every echo lands, how loud, and your sound travelling through them
S.hits = [];
function echoList() {
  const gy = S.upd && S.upd.gy; if (!gy) return [];
  const m = mode(), fbL = P(m + ' Lead Feedback').v, out = [];
  const s1 = Math.round(P('Engine 1').v), s2 = Math.round(P('Engine 2').v), m2 = Math.round(P('B Plays').v);
  const dld = (b, sec) => { const fb = Math.min(1.2, P(m + ' ' + BAND[b] + ' Feedback').v * fbL), inf = gy.inf && gy.inf[b];
    let lv = 1; for (let n = 1; n <= 40; ++n) { const t = n * sec; if (t > 16) break; out.push({ t, lv: inf ? 1 : lv, b }); lv *= inf ? 1 : fb; if (lv < 0.03) break; } };
  const tapo = (sl, b) => { const taps = (gy.taps && gy.taps[sl]) || []; if (!taps.length) return; const len = taps.reduce((a, x) => Math.max(a, x[0]), 0), fb = Math.min(1.2, P(m + ' ' + BAND[b] + ' Feedback').v * fbL);
    let g = 1; for (let r = 0; r < 6 && g > 0.03; ++r, g *= fb) taps.forEach(([t, v]) => { const tt = t + r * len; if (tt < 16) out.push({ t: tt, lv: g * (0.25 + 0.75 * v), b: b === 0 && s2 === 0 ? (t < len * 0.5 ? 0 : 2) : b }); }); };
  if (s1 === 0) dld(0, gy.sec[0]); else tapo(0, 0);
  if (s2 > 0) { if (s2 === 1) dld(2, gy.sec[2]); else tapo(1, 2); }
  else if (s1 === 0) dld(2, m2 === 1 ? gy.sec[0] + gy.sec[2] : gy.sec[2]);
  return out;
}
function drawEcho(cv) {
  const [g, Wfull, H] = prep(cv); g.fillStyle = T.graph; g.fillRect(0, 0, Wfull, H);
  const spec = spectralOn(), W = Wfull;
  if (spec) drawSplitIn(g, 0, Wfull, H, true);
  const gy = S.upd && S.upd.gy, now = performance.now();
  const echoes = echoList(), bpm = (gy && gy.bpm) || 120, beat = 60 / bpm;
  const last = echoes.reduce((a, e) => Math.max(a, e.t), 0);
  const span = Math.min(16, Math.max(4 * beat, last * 1.08 + beat * 0.5)), x0 = 46, xOf = (t) => x0 + t / span * (W - x0 - 8), mid = H * 0.5;
  // the beat grid
  for (let k = 1; k * beat <= span; ++k) { const x = xOf(k * beat), bar = k % 4 === 0; g.strokeStyle = T.inkA(bar ? 0.22 : 0.08); g.lineWidth = bar ? 1.5 : 1; g.beginPath(); g.moveTo(x, 10); g.lineTo(x, H - 4); g.stroke();
    if (bar) { g.fillStyle = T.label; g.font = '9px -apple-system, sans-serif'; g.textAlign = 'center'; g.fillText('bar ' + (k / 4 + 1), x, 9); } }
  // the water line through the middle
  g.strokeStyle = riverColour(); g.lineWidth = 2; g.beginPath(); g.moveTo(x0, mid); g.lineTo(W, mid); g.stroke();
  // input hits start a ripple that travels through the echoes
  const lv = gy && gy.lvl ? gy.lvl[0] : 0;
  if (lv > 0.08 && (!S.lastHit || now - S.lastHit > 90) && lv > (S.prevLv || 0) * 1.3) { S.hits.push({ at: now, lv }); S.lastHit = now; }
  S.prevLv = lv;
  S.hits = S.hits.filter((h0) => (now - h0.at) / 1000 < span + 0.3).slice(-12);
  // the echoes: A above the water, B below (Lead sets how long they ring)
  const run = gy && gy.runaway ? gy.runaway : [1, 1, 1];
  echoes.forEach((e) => {
    const x = xOf(e.t), up = e.b !== 2, len = Math.sqrt(Math.min(1.2, e.lv)) * (mid - 14);
    let glow = 0; S.hits.forEach((h0) => { const age = (now - h0.at) / 1000, d = Math.abs(age - e.t); if (d < 0.09) glow = Math.max(glow, (1 - d / 0.09) * Math.min(1, h0.lv * 2)); });
    const c = gy && gy.runT >= 1 && run[e.b] < 0.97 ? mixRGB(bandColour(e.b), '#D9534F', Math.min(0.7, gy.runT / 15)) : bandColour(e.b);
    g.strokeStyle = c; g.globalAlpha = 0.35 + 0.65 * Math.min(1, e.lv) * 0.6 + glow * 0.4; g.lineWidth = 3 + glow * 4; g.lineCap = 'round';
    g.beginPath(); g.moveTo(x, mid); g.lineTo(x, up ? mid - len : mid + len); g.stroke();
    if (glow > 0.05) { g.fillStyle = c; g.globalAlpha = glow; g.beginPath(); g.arc(x, up ? mid - len : mid + len, 4 + glow * 5, 0, Math.PI * 2); g.fill(); }
  });
  g.globalAlpha = 1;
  // the ripples: a ring travelling along the water
  S.hits.forEach((h0) => { const age = (now - h0.at) / 1000, x = xOf(age); if (x > W) return; const a = Math.max(0, 1 - age / span);
    g.strokeStyle = midColour(); g.globalAlpha = 0.6 * a; g.lineWidth = 2; g.beginPath(); g.ellipse(x, mid, 6 + 10 * h0.lv, 3 + 4 * h0.lv, 0, 0, Math.PI * 2); g.stroke(); });
  g.globalAlpha = 1;
  // your sound right now, at the start of the water
  g.fillStyle = T.inkA(0.5); const ih = Math.min(1, Math.sqrt(lv)) * (mid - 12); g.fillRect(12, mid - ih, 10, ih * 2);
  g.fillStyle = T.label; g.font = '9px -apple-system, sans-serif'; g.textAlign = 'left'; g.fillText('in', 12, H - 4);
  g.font = 'italic 800 10px "Avenir Next", -apple-system, sans-serif'; g.fillStyle = bandColour(0); g.fillText('A ' + (gy ? fmtSec(gy.sec[0]) : ''), x0 + 4, 14);
  g.fillStyle = bandColour(2); g.fillText('B ' + (gy ? fmtSec(gy.sec[2]) : ''), x0 + 4, H - 6);
  if (gy && gy.runT >= 1) { g.fillStyle = '#D9534F'; g.textAlign = 'right'; g.font = '700 10px -apple-system, sans-serif'; g.fillText('running away ' + Math.round(gy.runT) + ' s', W - 8, 14); }

}
const SPLIT_AT = 0; // with Split, the band split shares the whole graph with the echo map
function drawSplitIn(g, X, w, H, under) {
  const tonal = tonalOn(), gy = S.upd && S.upd.gy, sp = S.upd && S.upd.spec;
  const lo = gy && gy.split ? gy.split[0] : P('Spectral Low/Mid').v, hi = gy && gy.split ? gy.split[1] : P('Spectral Mid/High').v;
  const f1 = 440 * Math.pow(2, lo / 12), f2 = 440 * Math.pow(2, hi / 12), x1 = X + hzX(f1, w), x2 = X + hzX(f2, w);
  const lv = ['Spectral Low Level', 'Spectral Mid Level', 'Spectral High Level'].map((n) => P(n).v);
  g.save(); g.beginPath(); g.rect(X, 0, w, H); g.clip();
  if (!tonal) [[X, x1, 0], [x1, x2, 1], [x2, X + w, 2]].forEach(([a, b, k]) => { g.fillStyle = bandColour(k); g.globalAlpha = (under ? 0.07 : 0.16) + 0.08 * Math.max(0, Math.min(1, (lv[k] + 60) / 72)); g.fillRect(a, 0, b - a, H); });
  g.globalAlpha = 1;
  if (sp && sp[1]) {
    const y = (db) => H - 4 - (Math.max(-90, Math.min(6, db)) + 90) / 96 * (H - 18);
    g.beginPath(); sp[1].forEach((v, i) => { const x = X + i / (sp[1].length - 1) * w; i ? g.lineTo(x, y(v)) : g.moveTo(x, y(v)); });
    g.lineTo(X + w, H); g.lineTo(X, H); g.closePath(); g.fillStyle = T.inkA(under ? 0.1 : 0.22); g.fill();
  }
  g.font = 'italic 800 10px "Avenir Next", -apple-system, sans-serif'; g.textAlign = 'center';
  if (tonal) { g.fillStyle = T.label; g.font = '10px -apple-system, sans-serif'; g.fillText('Split by Tonal: tones → A · hits → Lead · air → B', X + w / 2, 16); g.restore(); return; }
  const db = (v) => (v <= -59.9 ? 'off' : (v >= 0 ? '+' : '') + v.toFixed(0) + ' dB');
  const yT = under ? H - 26 : 13, yD = under ? H - 15 : 25;
  [[ (X + x1) / 2, 0, 'LOW→A'], [(x1 + x2) / 2, 1, 'MID→LEAD'], [(x2 + X + w) / 2, 2, 'HIGH→B']].forEach(([x, k, t]) => { g.fillStyle = bandColour(k); g.globalAlpha = under ? 0.75 : 1; g.fillText(t, x, yT); g.font = '9px -apple-system, sans-serif'; g.fillText(db(lv[k]), x, yD); g.font = 'italic 800 10px "Avenir Next", -apple-system, sans-serif'; g.globalAlpha = 1; });
  [[x1, f1], [x2, f2]].forEach(([x, f]) => { g.strokeStyle = T.inkA(0.55); g.setLineDash([4, 4]); g.lineWidth = 2; g.beginPath(); g.moveTo(x, 30); g.lineTo(x, H - 14); g.stroke(); g.setLineDash([]);
    g.fillStyle = T.ink; g.font = '700 9px -apple-system, sans-serif'; g.fillText((f >= 1000 ? (f / 1000).toFixed(f >= 10000 ? 0 : 1) + ' kHz' : Math.round(f) + ' Hz') + ' ⇔', x, H - 3); });
  if (under) { g.fillStyle = T.label; g.font = '9px -apple-system, sans-serif'; g.textAlign = 'right'; g.fillText('bands: low → high (dashed lines, drag them)  ·  echoes: time →', X + w - 8, H - 3); }
  g.lineWidth = 1; g.restore();
}

// ----- each path's own filter, under its knobs: type, shape, cutoff, reso, morph, and where it sits
S.fillFns = [];
S.fillFilters = () => S.fillFns.forEach((f) => f());
function stripFilter(b) {
  const pre = BAND[b] + ' Filter ', fp = P(pre + 'Type'), vp = P(pre + 'Variant'), col = () => bandColour(b);
  const fb = h('button', { type: 'button', class: 'fbtn', 'aria-haspopup': 'listbox', style: 'flex:1;min-width:0' }, h('span', { style: 'overflow:hidden;text-overflow:ellipsis;white-space:nowrap' }), h('span', { text: '▾', class: 'car', style: 'color:var(--dim);font-size:11px' }));
  fb.onclick = (e) => { e.stopPropagation(); openMenu(fp, vp, fb, b); };
  const vb = h('button', { type: 'button', class: 'dd', style: 'width:46px;flex:none;padding:0 4px', 'aria-haspopup': 'listbox', 'aria-label': BAND[b] + ' filter shape' });
  vb.onclick = (e) => { e.stopPropagation(); openVariants(fp, vp, vb, b); };
  const knobs = h('div', { class: 'row', style: 'gap:2px;align-items:flex-end' }, knob(P(pre + 'Cutoff'), 'Cutoff', col, 24), knob(P(pre + 'Reso'), 'Reso', col, 24), knob(P(pre + 'Morph'), 'Morph', col, 24));
  const place = sel(P(pre + 'Place'), 58);
  const fill = () => {
    const md = S.models[Math.round(fp.v)] || { name: 'None', variants: [] }, vars = md.variants, vi = vars.length > 1 ? Math.round(vp.v * (vars.length - 1)) : 0, nm = vars[vi] || '';
    fb.firstChild.textContent = md.name === 'None' ? 'No filter' : md.name;
    vb.innerHTML = ''; vb.append(respIcon(md.name, vars.length ? nm : '', 30, 16, bandColour(b)));
    vb.title = nm || 'No shapes'; vb.disabled = vars.length < 2;
    const on = Math.round(fp.v) > 0; knobs.style.opacity = on ? 1 : 0.35; place.style.opacity = on ? 1 : 0.35;
  };
  S.fillFns[b] = fill; bind(fp, fill); bind(vp, fill);
  const where = b === 1 ? 'In: before both. Out: on all the echoes. Loop: in every repeat.' : 'In: before ' + BAND[b] + '’s engine. Out: on ' + BAND[b] + '’s echoes. Loop: in ' + BAND[b] + '’s repeats.';
  return h('div', { class: 'row', style: 'gap:5px;align-items:flex-end;border-top:1px solid var(--line);padding-top:6px' },
    h('div', { class: 'col', style: 'gap:4px;flex:1;min-width:0' }, h('div', { class: 'row', style: 'gap:4px' }, h('span', { class: 'lbl', text: 'Filter' }), h('div', { class: 'sp' }), tip(place, BAND[b] + ' filter place', where)), h('div', { class: 'row', style: 'gap:4px' }, fb, vb)),
    knobs);
}

// ----- dropdown choices that are gone from Gullay (their values still load)
function hiddenOption(p, v) {
  if (/^Mod \d+ Source$/.test(p.n)) return [1, 2, 3, 7].includes(v); // Seq A / Lead / B, Follower
  if (p.n === 'Mutation Trigger') return v === 4;                     // Follower hit
  if (/^Mutation [AB] Moves$/.test(p.n)) return v === 12;             // Tilt (no knob for it any more)
  return false;
}

// ----- Lead's colour: a ramp from A through its own middle to B, steps far enough apart to read the Lean
function rgbOf(c) { const n = parseInt(c.slice(1), 16); return [(n >> 16) & 255, (n >> 8) & 255, n & 255]; }
function hslOf([r, g, b]) { r /= 255; g /= 255; b /= 255; const mx = Math.max(r, g, b), mn = Math.min(r, g, b), l = (mx + mn) / 2; if (mx === mn) return [0, 0, l]; const d = mx - mn, s = l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);
  const h = mx === r ? (g - b) / d + (g < b ? 6 : 0) : mx === g ? (b - r) / d + 2 : (r - g) / d + 4; return [h * 60, s, l]; }
function hexOfHsl([h, s, l]) { h = ((h % 360) + 360) % 360; const f = (n) => { const k = (n + h / 30) % 12, a = s * Math.min(l, 1 - l); return Math.round(255 * (l - a * Math.max(-1, Math.min(k - 3, 9 - k, 1)))); };
  return '#' + [f(0), f(8), f(4)].map((x) => x.toString(16).padStart(2, '0')).join(''); }
function midColour() {
  const pt = T.part || ['#5D68B0', '#80C2B9', '#786FA3'], d = leanAmount(), end = d < 0 ? pt[0] : pt[2], t = Math.abs(d);
  const a = hslOf(rgbOf(pt[1])), b = hslOf(rgbOf(end));
  let dh = b[0] - a[0]; if (dh > 180) dh -= 360; if (dh < -180) dh += 360;
  // hue moves evenly; lightness and saturation take a detour through the middle, so 80 % and 100 % differ
  const bump = Math.sin(Math.PI * t), dark = !!(THEMES[S.themeName] || {}).dark;
  const l = a[2] + (b[2] - a[2]) * t + (dark ? 0.10 : -0.10) * bump, sat = Math.min(1, a[1] + (b[1] - a[1]) * t + 0.12 * bump);
  return hexOfHsl([a[0] + dh * t, sat, Math.max(0.12, Math.min(0.88, l))]);
}

// ----- the Mutation page: Lead calls, A and B answer
const GY_MUT_HELP = ['Lead suddenly gets louder. The answer always moves up.', 'Lead’s time jumps, up or down.', 'The runaway ceiling catches Lead’s loop. The answer always moves down.', 'Every 1/16 step of the song.', '', 'Each step the Chaperone passes on.'];
const GY_MUT_SENS = [true, true, false, false, true, false];
function pageMutation() {
  const num = (n, t) => h('span', { class: 'mnum', text: n, title: t });
  const trigP = P('Mutation Trigger'), sensK = tip(knob(P('Mutation Sensitivity'), 'How easily', ink, 36), 'How easily', 'How small a change still counts as a call. Higher: smaller changes count, so it fires more often.');
  const help = h('div', { class: 'hint', style: 'font-size:11px;line-height:1.3' });
  bind(trigP, (p) => { const i = Math.round(p.v); help.textContent = GY_MUT_HELP[i] || ''; sensK.style.opacity = GY_MUT_SENS[i] ? 1 : 0.35; });
  const trig = card('width:232px;gap:7px', h('div', { class: 'row', style: 'gap:6px' }, num('1', 'The call'), h('div', { class: 'lbl', text: 'When Lead…' })),
    tip(h('div', {}, sel(trigP, 206)), 'When Lead…', 'What counts as Lead’s call: it gets louder, its time jumps, the runaway ceiling catches it, every 1/16 step, or each step the Chaperone passes on.'), help,
    h('div', { class: 'row' }, sensK, tip(labelled('Rest', sel(P('Mutation Hold-off'), 92)), 'Rest', 'After a call, wait at least this many 1/16 steps before the next one can land, so busy playing doesn’t set it off all the time.')));
  const ANS = ['Off', 'Follow', 'Oppose'];
  const resp = (b, pre) => card('flex:1;gap:7px', h('div', { class: 'row', style: 'gap:6px' }, num('2', 'The response'), btag(b), h('div', { class: 'lbl', text: 'response' }), h('div', { class: 'sp' }),
      tip(seg(P(pre + 'Answer'), ANS), 'Response', 'Off: ' + BAND[b] + ' ignores Lead. Follow: when Lead’s change goes up, ' + BAND[b] + ' moves up too. Oppose: it moves the other way.')),
    h('div', { class: 'row', style: 'align-items:flex-end' },
      tip(labelled('Responds with', sel(P(pre + 'Moves'), 104)), 'Responds with', 'Which of ' + BAND[b] + '’s controls the response turns: Time, Feedback, Tone, Filter, Hymn, the crusher …'),
      tip(knob(P(pre + 'Amount'), 'How far', () => bandColour(b), 36), 'How far', 'How far each answer turns it. Each answer jumps in quickly and eases back.'),
      tip(knob(P(pre + 'Chance'), 'Chance', () => bandColour(b), 36), 'Chance', 'How often it responds at all: 50 % responds to about every other call.'),
      tip(labelled('Wait', sel(P(pre + 'Delay'), 84)), 'Wait', 'Waits this many 1/16 steps after the call before answering, like an echo of Lead.')));
  const seed = P('Mutate Seed'), mutP = P('Mutate'), everyP = P('Mutate Every'), frzP = P('Mutate Freeze');
  const status = h('div', { class: 'hint', style: 'font-size:11.5px;line-height:1.35' });
  const driftTip = 'Drift slowly changes the responses by itself. 0 %: they stay exactly as you set them. Above 0 %, every few bars How far and Chance wander a little (pulled back toward your settings), and an answer may flip or turn something else.';
  const mut = card('width:244px;gap:7px', h('div', { class: 'row', style: 'gap:6px' }, num('3', 'Optional'), tip(h('div', { class: 'lbl', text: 'Drift ⓘ' }), 'Drift', driftTip), h('div', { class: 'sp' }), tip(toggle(frzP, 'Hold'), 'Hold', 'Stops the drifting where it is.')),
    h('div', { class: 'row', style: 'align-items:center;gap:10px' }, tip(knob(mutP, 'Drift', midLine, 36), 'Drift', driftTip),
      h('div', { class: 'col', style: 'gap:5px' }, tip(labelled('Drift every', sel(everyP, 82)), 'Drift every', 'How often the responses change.'),
        h('div', { class: 'row', style: 'gap:4px' }, tip(stepper(seed), 'Seed', 'Which way the drift goes. The same seed always drifts the same way.'), h('button', { type: 'button', class: 'btn', text: 'New roll', onclick: () => setMany([[seed, Math.floor(Math.random() * 1000)]]) })))),
    status);
  const paint = () => { status.textContent = mutP.v <= 0.001 ? 'Off: the responses stay as you set them.' : frzP.v >= 0.5 ? 'Holding where it drifted to.' : 'Drifting every ' + lab(everyP) + '.'; };
  [mutP, everyP, frzP].forEach((p) => bind(p, paint));
  return h('div', { style: 'flex-direction:column;gap:8px' },
    h('div', { class: 'hint', style: 'font-size:12px', text: 'Call & Response: Lead calls, A and B respond. When something happens to Lead, A and B move in reply. 1: what counts as a call. 2: how each responds. 3: let the responses drift over time.' }),
    h('div', { class: 'row', style: 'align-items:stretch;gap:10px' }, trig, resp(0, 'Mutation A '), resp(2, 'Mutation B '), mut));
}

// ----- Snapshots: store up to 8 settings, then step through them in time
// The knobs can hold a snapshot: load one (click it) and it shows as "Snapshot N"; turn knobs and Update keeps the change.
const SNAP_PARAMS = /^(Par|Ser) (A|Lead|B) (Time|Feedback|Tone|Drive|Level|Pan)$|^(Par|Ser) Mix$|^(A|Lead|B) Filter (Cutoff|Reso|Morph)$|^(Tune Amount|DLD Degrade|Memory Rate|Crusher Bits)$/;
S.snapEdited = false;
function snapValid() { return (S.upd && S.upd.gy && S.upd.gy.snap && S.upd.gy.snap.valid) || []; }
function snapLoaded() { const sn = S.upd && S.upd.gy && S.upd.gy.snap; return sn ? Math.round(sn.loaded) : -1; }
function freeSlot() { const v = snapValid(); for (let k = 0; k < 8; ++k) if (!v[k]) return k + 1; return 0; }
function presetItems() {
  const list = S.presets || [], items = [];
  const groups = {}; list.forEach(([n, g], i) => (groups[g] = groups[g] || []).push({ label: n, value: 'p' + i }));
  Object.keys(groups).sort((a, b) => (a === 'Start' ? -1 : b === 'Start' ? 1 : a.localeCompare(b))).forEach((g) => { items.push({ head: g }); groups[g].forEach((x) => items.push(x)); });
  const yours = (S.userPresets || []).slice().sort();
  if (yours.length) { items.push({ head: 'Yours' }); yours.forEach((n) => items.push({ label: n, value: 'u' + n })); }
  return items;
}
// a preset into a snapshot slot (the first free one unless given), then onto a step if given
function presetToSnap(v, step, slot) {
  const k = slot || freeSlot();
  if (!k) { toast('All 8 snapshots are full: clear one first (right-click it)'); return; }
  post('snap ' + (v[0] === 'p' ? 'preset ' + k + ' ' + v.slice(1) : 'user ' + k + ' ' + v.slice(1)));
  const nm = v[0] === 'p' ? ((S.presets || [])[+v.slice(1)] || ['?'])[0] : v.slice(1);
  if (step) setMany([[P('Snap Step ' + step), k]]);
  toast('“' + nm + '” is Snapshot ' + k + (step ? ', on step ' + step : ''));
}
function pageSnaps() {
  const big = h('div', { style: 'font:800 20px "Avenir Next",-apple-system,sans-serif;letter-spacing:.02em' }), sub = h('div', { class: 'hint', style: 'font-size:11px' });
  const updBtn = h('button', { type: 'button', class: 'btn on', text: 'Update', style: 'display:none' });
  updBtn.onclick = () => { const k = snapLoaded(); if (k >= 0) { post('snap store ' + (k + 1)); S.snapEdited = false; paintHead(); toast('Snapshot ' + (k + 1) + ' updated with the knobs'); } };
  const newBtn = h('button', { type: 'button', class: 'btn', text: '＋ New from knobs' });
  newBtn.onclick = () => { const k = freeSlot(); if (!k) { toast('All 8 snapshots are full: clear one first (right-click it)'); return; } post('snap store ' + k); S.snapEdited = false; toast('Stored the knobs as Snapshot ' + k); };
  const paintHead = () => {
    const sn = S.upd && S.upd.gy && S.upd.gy.snap, run = P('Snap Seq On').v >= 0.5, cur = sn ? Math.round(sn.cur) : -1, k = snapLoaded();
    if (run && cur >= 0 && cur < 8) { big.textContent = 'Snapshot ' + (cur + 1); sub.textContent = 'playing from the sequence'; }
    else if (run && cur === 8) { big.textContent = 'Preset'; sub.textContent = 'a blank step reverted to the selected preset'; }
    else if (k >= 0) { big.textContent = 'Snapshot ' + (k + 1) + (S.snapEdited ? ' •' : ''); sub.textContent = S.snapEdited ? 'on the knobs, edited: Update keeps the change' : 'on the knobs: turn any knob to edit it'; }
    else { big.textContent = 'Knobs'; sub.textContent = 'no snapshot loaded'; }
    updBtn.style.display = k >= 0 && S.snapEdited && !run ? '' : 'none';
  };
  S.snapParamChanged = (p) => { if (SNAP_PARAMS.test(p.n) && snapLoaded() >= 0) { S.snapEdited = true; paintHead(); } };
  const slots = [], slotGrid = h('div', { style: 'display:grid;grid-template-columns:repeat(4,1fr);gap:5px' });
  for (let k = 1; k <= 8; ++k) {
    const b = h('button', { type: 'button', class: 'btn snapslot', style: 'height:34px;font-weight:800' });
    b.textContent = 'S' + k;
    b.onclick = () => { if (!snapValid()[k - 1]) { post('snap store ' + k); toast('Stored the knobs as Snapshot ' + k); } else { post('snap recall ' + k); S.snapEdited = false; toast('Snapshot ' + k + ' is on the knobs: turn them to edit, Update to keep'); } };
    b.oncontextmenu = (e) => { e.preventDefault(); const full = !!snapValid()[k - 1];
      openPop(b, [{ head: 'Snapshot ' + k }, { label: 'Load onto the knobs (edit)', value: 'r', disabled: !full }, { label: full ? 'Overwrite with the knobs' : 'Store the knobs', value: 's' }, { label: 'From a preset…', value: 'p' }, { label: 'Clear', value: 'c', disabled: !full }], null,
        (v) => { if (v === 'r') { post('snap recall ' + k); S.snapEdited = false; } else if (v === 's') post('snap store ' + k);
          else if (v === 'p') openPop(b, presetItems(), null, (pv) => presetToSnap(pv, 0, k), { width: 240 });
          else if (v === 'c') { post('snap clear ' + k); const ch = []; for (let i = 1; i <= 16; ++i) if (Math.round(P('Snap Step ' + i).v) === k) ch.push([P('Snap Step ' + i), 0]); if (ch.length) { setMany(ch); toast('Cleared S' + k + ' and made its ' + ch.length + ' step' + (ch.length > 1 ? 's' : '') + ' blank'); } } }, { width: 230 }); };
    b.addEventListener('dragover', (e) => e.preventDefault());
    b.addEventListener('drop', (e) => { e.preventDefault(); const v = e.dataTransfer.getData('text/gullay-preset'); if (v) presetToSnap(v, 0, k); });
    slots.push(b); slotGrid.appendChild(b);
  }
  // a preset shelf: drag one onto a step or a slot
  // a preset pill: pick one (or roll one), then drag the pill onto a step or a slot, or add it to the next free slot
  S.snapPick = S.snapPick || null;
  const pillT = h('span', { class: 't', text: S.snapPick ? S.snapPick.label : 'Pick a preset…' });
  const pill = h('button', { type: 'button', class: 'dd pdd snappill', draggable: 'true', 'aria-haspopup': 'listbox', style: 'flex:1;width:auto!important' }, pillT, h('span', { class: 'car', text: '▾' }));
  const setPick = (it) => { S.snapPick = it; pillT.textContent = it.label; };
  pill.onclick = (e) => { e.stopPropagation(); openPop(pill, presetItems(), null, (v) => { const it = presetItems().find((x) => x.value === v); if (it) setPick(it); }, { width: 260, maxHeight: 300 }); };
  pill.addEventListener('dragstart', (e) => { if (!S.snapPick) { e.preventDefault(); return; } e.dataTransfer.setData('text/gullay-preset', S.snapPick.value); });
  const roll = h('button', { type: 'button', class: 'btn', id: 'snapRand', 'aria-label': 'Random preset', style: 'border-radius:999px;width:34px;padding:0' });
  roll.innerHTML = '<svg width="16" height="16" viewBox="0 0 16 16" aria-hidden="true"><rect x="1.5" y="1.5" width="13" height="13" rx="3" fill="none" stroke="currentColor" stroke-width="1.4"/><circle cx="5" cy="5" r="1.2" fill="currentColor"/><circle cx="11" cy="11" r="1.2" fill="currentColor"/><circle cx="8" cy="8" r="1.2" fill="currentColor"/></svg>';
  roll.onclick = () => { const its = presetItems().filter((x) => !x.head && x.value[0] === 'p'); if (its.length) setPick(pick(its)); };
  const addBtn = h('button', { type: 'button', class: 'btn', text: 'Add as snapshot', onclick: () => { if (!S.snapPick) { toast('Pick a preset first'); return; } presetToSnap(S.snapPick.value); } });
  const shelf = h('div', { class: 'col', style: 'gap:6px' }, h('div', { class: 'row', style: 'gap:6px' }, pill, tip(roll, 'Random preset', 'Picks a factory preset at random.')), h('div', { class: 'row', style: 'gap:6px' }, addBtn, h('span', { class: 'hint', text: 'or drag the pill onto a step' })));

  const snapsCard = card('width:360px;flex:none;gap:6px', h('div', { class: 'row', style: 'gap:10px;align-items:flex-end' }, h('div', { class: 'col', style: 'gap:0' }, big, sub), h('div', { class: 'sp' }), updBtn),
    slotGrid, h('div', { class: 'row', style: 'gap:6px' }, newBtn, h('span', { class: 'hint', text: 'Click a full slot to edit it; right-click for more.' })),
    h('div', { class: 'lbl', text: 'Presets as snapshots', style: 'margin-top:4px' }), shelf);
  // the 16 steps, each with its chance under it
  const len = P('Snap Seq Length'), steps = [], grid = h('div', { style: 'display:grid;grid-template-columns:repeat(16,1fr);gap:3px' });
  const stepMenu = (c, i) => { const p = P('Snap Step ' + i), v = Math.round(p.v), valid = snapValid(), items = [{ head: 'Step ' + i }];
    for (let k = 1; k <= 8; ++k) if (valid[k - 1]) items.push({ label: 'Snapshot ' + k, value: 's' + k, note: v === k ? '✓' : '' });
    items.push({ label: '– Blank', value: 'b', note: v === 0 ? '✓' : '' }, { sep: true }, { label: '＋ New snapshot from the knobs here', value: 'n' }, { label: 'Use a preset…', value: 'p' });
    if (v > 0) items.push({ label: 'Edit Snapshot ' + v + ' (load it onto the knobs)', value: 'e' });
    openPop(c, items, null, (x) => {
      if (x[0] === 's') setMany([[p, +x.slice(1)]]); else if (x === 'b') setMany([[p, 0]]);
      else if (x === 'n') { const k = freeSlot(); if (!k) { toast('All 8 snapshots are full: clear one first'); return; } post('snap store ' + k); setMany([[p, k]]); toast('The knobs are Snapshot ' + k + ', on step ' + i); }
      else if (x === 'p') openPop(c, presetItems(), null, (pv) => presetToSnap(pv, i), { width: 240 });
      else if (x === 'e') { post('snap recall ' + v); S.snapEdited = false; } }, { width: 270 }); };
  for (let i = 1; i <= 16; ++i) {
    const p = P('Snap Step ' + i), chP = P('Snap Step ' + i + ' Chance'), c = h('button', { type: 'button', class: 'snapstep' });
    const rng = h('input', { type: 'range', min: '1', max: '100', class: 'chance', 'aria-label': 'Step ' + i + ' chance' }), pc = h('div', { class: 'chpc' });
    rng.oninput = () => { setP(chP, +rng.value / 100); pc.textContent = rng.value + '%'; };
    rng.onpointerdown = () => gesture(chP, true); rng.onpointerup = () => gesture(chP, false);
    c.onclick = () => stepMenu(c, i); c.oncontextmenu = (e) => { e.preventDefault(); stepMenu(c, i); };
    c.addEventListener('dragover', (e) => { e.preventDefault(); c.classList.add('drop'); }); c.addEventListener('dragleave', () => c.classList.remove('drop'));
    c.addEventListener('drop', (e) => { e.preventDefault(); c.classList.remove('drop'); const v = e.dataTransfer.getData('text/gullay-preset'); if (v) presetToSnap(v, i); });
    bind(p, () => paint()); bind(chP, () => { rng.value = Math.round(chP.v * 100); pc.textContent = Math.round(chP.v * 100) + '%'; paint(); });
    steps.push(c); grid.appendChild(h('div', { class: 'col', style: 'gap:3px;align-items:stretch' }, c, rng, pc));
  }
  function paint() { const valid = snapValid(); steps.forEach((c, j) => {
    const i = j + 1, v = Math.round(P('Snap Step ' + i).v), ch = P('Snap Step ' + i + ' Chance').v, on = i <= Math.round(len.v), ok = v > 0 && valid[v - 1];
    c.textContent = ok ? 'S' + v : '–'; c.classList.toggle('off', !on); c.classList.toggle('blank', !ok); c.style.setProperty('--ch', Math.round(ch * 100) + '%');
    c.title = 'Step ' + i + (ok ? ': Snapshot ' + v : ': blank') + ', ' + Math.round(ch * 100) + '% chance. Click or right-click for options; drop a preset here.'; }); }
  bind(len, paint);
  S.paintSnapPos = (u) => { const sn = u && u.gy && u.gy.snap; if (!sn) return; const run = P('Snap Seq On').v >= 0.5;
    steps.forEach((c, i) => c.classList.toggle('now', run && i === Math.round(sn.pos)));
    slots.forEach((b, k) => { b.classList.toggle('full', !!sn.valid[k]); b.classList.toggle('playing', run && Math.round(sn.cur) === k); b.classList.toggle('loaded', !run && Math.round(sn.loaded) === k); });
    paint(); paintHead(); };
  // ways to fill the steps: only stored snapshots, or blanks on purpose
  const used = () => { const v = snapValid(), u = []; v.forEach((x, k) => { if (x) u.push(k + 1); }); return u; };
  const fill = (kind) => {
    const L = Math.round(len.v), u = used(), ch = [];
    if (!u.length && kind !== 'clear') { toast('Store a snapshot first: the steps only take stored ones'); return; }
    for (let i = 0; i < 16; ++i) {
      let v = 0;
      if (i < L && kind !== 'clear') {
        if (kind === 'order') v = u[i % u.length];
        else if (kind === 'random') v = chance(0.2) ? 0 : pick(u);
        else if (kind === 'euclid') { const hits = Math.max(1, Math.round(L * 3 / 8)), hit = Math.floor((i + 1) * hits / L) !== Math.floor(i * hits / L); v = hit ? u[Math.min(1, u.length - 1)] : (u.length > 1 ? u[0] : 0); }
        else if (kind === 'sparse') v = i === 0 ? u[0] : (chance(0.25) ? pick(u) : 0);
      }
      ch.push([P('Snap Step ' + (i + 1)), v]);
    }
    setMany(ch); toast({ order: 'Steps filled in order', random: 'Random: stored snapshots, with some blanks', euclid: 'Euclid: hits of one snapshot over another', sparse: 'A few changes, blanks between', clear: 'All steps blank' }[kind]);
  };
  const blank = seg(P('Snap Seq Blank Steps'), ['Hold last', 'Revert to preset']); blank.classList.add('sm');
  const seqCard = card('flex:1;gap:7px', h('div', { class: 'row', style: 'gap:8px' }, hd2('SEQUENCE'), tip(toggle(P('Snap Seq On'), 'Run'), 'Run', 'Steps through the snapshots in time with the song (or Free’s tempo). Off: the knobs are yours again.'), h('div', { class: 'sp' }),
      labelled('Step', sel(P('Snap Seq Rate'), 70)), labelled('Steps', stepper(len)), labelled('Order', sel(P('Snap Seq Order'), 92)), tip(knob(P('Snap Seq Glide'), 'Glide', ink, 28), 'Glide', 'How much of each step it takes to glide into the next snapshot. 0: jump.')),
    grid,
    h('div', { class: 'row', style: 'gap:6px' }, h('span', { class: 'lbl', text: 'Fill' }), ...[['In order', 'order'], ['Random', 'random'], ['Euclid', 'euclid'], ['Sparse', 'sparse'], ['Clear', 'clear']].map(([t, k]) => h('button', { type: 'button', class: 'btn', text: t, onclick: () => fill(k) })),
      h('div', { class: 'sp' }), tip(h('div', { class: 'row', style: 'gap:6px' }, h('span', { class: 'lbl', text: 'Blank steps' }), blank), 'Blank steps',
        'What a blank step does, and a step that misses its chance: Hold last keeps the last snapshot that played (before any has played, the knobs). Revert to preset goes back to the preset you loaded.')),
    h('div', { class: 'hint', text: 'Click a step to pick a stored snapshot, a blank, a new snapshot from the knobs, or a preset; or drag a preset onto it. The slider under each step is its chance (1–100%).' }));
  paint(); paintHead();
  return h('div', {}, snapsCard, seqCard);
}

// ----- quick modulation: pick a source from a knob's menu, then drag that knob to set how far
document.addEventListener('pointerdown', (e) => { if (S.modDrag && !S.modDrag.drag && !(S.modDrag.el && S.modDrag.el.contains(e.target))) { S.modDrag = null; document.querySelectorAll('.knob.moddrag').forEach((x) => x.classList.remove('moddrag')); } }, true);

// ----- the Path's Split, with the engines it feeds: shown whenever the Path is Split
function splitCard(two, bare) {
  const tonal = tonalOn(), by = seg(P('Spectral Split By'), ['Freq', 'Tonal']); by.classList.add('sm');
  const lv = (n, l, b) => knob(P(n), l, () => bandColour(b), 28);
  return card('width:300px;flex:none;gap:6px', h('div', { class: 'row', style: 'gap:8px' }, ...(bare ? [] : [tip(hd2('SPLIT'), 'Split', TIPS.Spectral)]), by, h('div', { class: 'sp' }), sel(P('Spectral Quality'), 86)),
    h('div', { class: 'row', style: 'gap:4px;align-items:flex-end' }, lv('Spectral Low Level', tonal ? 'Tones' : 'Low', 0), lv('Spectral Mid Level', tonal ? 'Hits' : 'Mid', 1), lv('Spectral High Level', tonal ? 'Air' : 'High', 2),
      ...(tonal ? [] : [knob(P('Spectral Low/Mid'), 'Low/Mid', ink, 28), knob(P('Spectral Mid/High'), 'Mid/High', ink, 28)]), knob(P('Spectral Sharpness'), 'Sharp', ink, 28)),
    h('div', { class: 'hint', text: tonal ? 'Tones go to A, hits to both (Lead), air to B.' : 'Drag the dashed split lines in the graph above.' }));
}
// ----- the theme, chosen in Settings now
function themePicker() {
  const names = Object.keys(THEMES), th = h('button', { type: 'button', class: 'dd', style: 'width:170px', 'aria-haspopup': 'listbox' }, h('span', { class: 't', text: (THEMES[S.themeName] || THEMES.porch).label }), h('span', { class: 'car', text: '▾' }));
  th.onclick = (e) => { e.stopPropagation(); openPop(th, names.map((n, i) => ({ label: THEMES[n].label, value: i })), names.indexOf(S.themeName), (i) => { applyTheme(names[i]); post('theme ' + names[i]); build(); }, { width: 190 }); };
  return th;
}

// ----- each Juba pattern card: its taps drawn small, and a word for what it does
S.dWords = ['bounce', 'dotted', 'clave', '3 vs 4', 'fibonacci', 'swell'];
function patWord(taps) {
  taps = [...taps].sort((a, b) => a[0] - b[0]);
  const n = taps.length; if (!n) return 'empty'; if (n < 2) return 'single';
  const d = []; for (let i = 1; i < n; ++i) d.push(taps[i][0] - taps[i - 1][0]);
  const m = d.reduce((a, x) => a + x, 0) / d.length, sd = Math.sqrt(d.reduce((a, x) => a + (x - m) * (x - m), 0) / d.length), cv = m > 0 ? sd / m : 0;
  let tr = 0; for (let i = 1; i < d.length; ++i) tr += Math.sign(d[i] - d[i - 1]);
  const vs = taps.map((t) => t[1]), vr = Math.max(...vs) - Math.min(...vs);
  if (n <= 3) return 'sparse';
  if (tr >= (d.length - 1) * 0.6) return 'slowing';
  if (tr <= -(d.length - 1) * 0.6) return 'rushing';
  if (n >= 12) return cv < 0.25 ? 'stream' : 'cluster';
  if (cv < 0.12) return vr > 0.4 ? 'accents' : 'even';
  if (cv > 0.6) return 'scatter';
  return 'swing';
}
S.drawPats = () => {
  if (!S.pats || !S.patBtns) return;
  S.patBtns.forEach((b, i) => {
    const cv = b.querySelector('canvas.pm'), w = b.querySelector('.pw'), taps = S.pats[i] || [];
    if (!cv) return;
    const g = cv.getContext('2d'), W = cv.width, H = cv.height, on = b.classList.contains('on');
    g.clearRect(0, 0, W, H); g.fillStyle = on ? T.onink : T.ink; g.globalAlpha = on ? 0.9 : 0.75;
    taps.forEach(([t, v]) => { const x = 2 + t * (W - 4), hh = 3 + Math.max(0.15, v) * (H - 4); g.fillRect(Math.round(x) - 1, H - hh, 2, hh); });
    g.globalAlpha = 1; if (w) w.textContent = i >= 18 && i < 24 && S.dWords && taps.length ? S.dWords[i - 18] : patWord(taps);
    if (i >= 18 && i < 24) { const dEmpty = S.pats.slice(18, 24).every((t) => !t.length) && !S.patBtns.slice(18, 24).some((x) => x.classList.contains('on')); b.style.display = dEmpty ? 'none' : ''; } b.classList.toggle('empty', !taps.length); b.title = taps.length ? taps.length + ' taps · ' + patWord(taps) : 'Empty: tap a pattern on TAP, then save it here';
  });
};

// ----- the Path: how the sound flows through the two engines (and the Split's bands), with Balance under it
function pathCard(two) {
  const ser = Math.round(P('Wiring').v) === 1, split = spectralOn();
  const box = h('div', { style: 'height:64px' });
  const draw = () => {
    const c = (b) => bandColour(b), W = 236, Hh = 64, eng = (x, y, t, col) => `<rect x="${x}" y="${y}" width="58" height="20" rx="5" fill="${col}" opacity=".9"/><text x="${x + 29}" y="${y + 14}" text-anchor="middle" font-size="10" font-weight="700" fill="${textOn(col)}">${t}</text>`;
    const e1 = lab(P('Engine 1')), e2 = two ? lab(P('Engine 2')) : 'B half', line = (d) => `<path d="${d}" fill="none" stroke="${T.ink}" stroke-width="1.5" opacity=".6"/>`;
    let svg = `<text x="0" y="36" font-size="9" fill="${T.label}">in</text><text x="${W - 14}" y="36" font-size="9" fill="${T.label}">out</text>`;
    if (two && ser) svg += line('M14 32 H40') + eng(40, 22, '1 · ' + e1, c(0)) + line('M98 32 H107 M129 32 H138') + `<rect x="107" y="26" width="22" height="12" rx="3" fill="none" stroke="${T.ink}" stroke-width="1.2"/><text x="118" y="22" text-anchor="middle" font-size="8" fill="${T.label}">wire</text>` + eng(138, 22, '2 · ' + e2, c(2)) + line('M196 32 H222')
      + `<path d="M167 43 V54 H69 V43" fill="none" stroke="${c(2)}" stroke-width="1.5" stroke-dasharray="3 3" opacity="${0.3 + 0.7 * P('Chain Cross').v}"/><text x="118" y="63" text-anchor="middle" font-size="8" fill="${T.label}">cross</text>`;
    else if (split) svg += line('M14 32 H34 M34 10 V54 M34 10 H70 M34 32 H70 M34 54 H70') + `<text x="44" y="8" font-size="8" fill="${c(0)}">low</text><text x="44" y="30" font-size="8" fill="${c(1)}">mid</text><text x="44" y="52" font-size="8" fill="${c(2)}">high</text>`
      + eng(100, 2, two ? '1 · ' + e1 : 'A', c(0)) + eng(100, 42, two ? '2 · ' + e2 : 'B', c(2)) + line('M70 10 H100 M70 52 H100 M70 32 H86 V12 M86 32 V52 M158 12 H200 V32 M158 52 H200 V32 M200 32 H222');
    else svg += line('M14 32 H40 M40 12 V52 M40 12 H86 M40 52 H86') + eng(86, 2, '1 · ' + e1, c(0)) + eng(86, 42, '2 · ' + e2, c(2)) + line('M144 12 H190 V52 H144 M190 32 H222');
    box.innerHTML = `<svg width="${W}" height="${Hh}" viewBox="0 0 ${W} ${Hh}" aria-hidden="true">${svg}</svg>`;
  };
  draw(); if (two && ser) bind(P('Chain Cross'), draw);
  const words = two && ser ? 'Chain: engine 1’s echoes feed engine 2 through the wire; Cross sends them back.' : split ? (two ? 'Split: lows to engine 1, highs to engine 2, mids to both.' : 'Split: lows to A, highs to B, mids to both.') : 'Side by side: both engines hear you.';
  const kids = [h('div', { class: 'row', style: 'gap:8px' }, tip(hd2(split ? 'PATH · SPLIT' : 'PATH'), 'Path', TIPS.Slot2)), box, h('div', { class: 'hint', text: words })];
  if (two && ser) kids.push(h('div', { class: 'row', style: 'gap:8px;align-items:flex-end' }, tip(knob(P('Chain Cross'), 'Cross', () => bandColour(2), 30), 'Cross', 'Feeds engine 2’s echoes back into engine 1, so the sound loops between the two engines and builds.'),
    vline(), tip(knob(P('Chain Wire Tone'), 'Wire tone', ink, 30), 'Wire tone', 'A tilt EQ on the link between the engines: left darker, right brighter.'), tip(knob(P('Chain Wire Drive'), 'Wire drive', ink, 30), 'Wire drive', 'Saturation on the link: engine 2 hears engine 1 driven harder.')));
  if (two) { const bal = P('Engine Balance'), r = h('input', { type: 'range', min: '-100', max: '100', class: 'chance', style: 'flex:1', 'aria-label': 'Balance' }); r.oninput = () => setP(bal, +r.value / 100); r.onpointerdown = () => gesture(bal, true); r.onpointerup = () => gesture(bal, false); bind(bal, () => { r.value = Math.round(bal.v * 100); });
    kids.push(h('div', { class: 'row', style: 'gap:6px' }, h('span', { class: 'lbl', text: 'Engine 1' }), r, h('span', { class: 'lbl', text: 'Engine 2' }))); }
  if (split) { const ch = [...splitCard(two, true).children]; kids.splice(1, 0, ch[0]); ch.slice(1).forEach((el) => { if (!el.classList.contains('hint')) kids.push(el); }); }
  return card('width:260px;flex:none;gap:5px', ...kids);
}

// ----- Sync before Free, everywhere: a segment that offers Free and a synced choice shows the synced one first
function seg(p, labels, opts) {
  if (!p) return h('div'); opts = opts || {};
  const el = h('div', { class: 'seg', role: 'group', 'aria-label': p.n });
  let labs = (labels || p.lab).slice(), vals = opts.values ? opts.values.slice() : labs.map((_, i) => i);
  const fi = labs.indexOf('Free'), si = labs.findIndex((t) => /^(Sync|Synced|Clock|Host)$/.test(t));
  if (fi >= 0 && si > fi) { const order = labs.map((_, i) => i); order.splice(si, 1); order.splice(fi, 0, si); labs = order.map((i) => labs[i]); vals = order.map((i) => vals[i]); }
  const bs = labs.map((t, i) => { const b = h('button', { type: 'button', text: t, onclick: () => { gesture(p, true); setP(p, vals[i]); gesture(p, false); } }); el.appendChild(b); return b; });
  bind(p, () => bs.forEach((b, i) => { const on = Math.round(p.v) === vals[i]; b.classList.toggle('on', on); b.setAttribute('aria-pressed', on); }));
  return el;
}

// ----- the preset pill gets a random-preset button beside it
const _buildPresetsTC = buildPresets;
buildPresets = function () {
  _buildPresetsTC();
  const box = document.getElementById('presetBox'), b = box && box.querySelector('.pdd');
  if (!b || box.querySelector('#presetRand')) return;
  // one scrolling list instead of a wall of columns: Start first, the categories A–Z, your own last
  b.onclick = (e) => { e.stopPropagation();
    const items = presetItems().map((x) => x.value ? Object.assign({}, x, { value: x.value[0] === 'p' ? +x.value.slice(1) : 'u:' + x.value.slice(1) }) : x);
    items.push({ sep: true }, { label: '＋ Save the current sound…', value: 'save' });
    const cur = (S.presets || []).findIndex(([n]) => n === S.presetName);
    openPop(b, items, cur, (v) => {
      if (v === 'save') { saveDialog(); return; }
      histCommit(); H.expectUntil = performance.now() + 1500; const t = b.querySelector('.t');
      if (typeof v === 'string' && v.startsWith('u:')) { S.presetName = v.slice(2); if (t) t.textContent = S.presetName; H.label = 'load ' + S.presetName; post('uload ' + S.presetName); toast('Loaded ' + S.presetName); return; }
      S.presetName = S.presets[v][0]; if (t) t.textContent = S.presetName; H.label = 'load ' + S.presetName; post('preset ' + v); toast('Loaded ' + S.presetName); }, { width: 240 }); };
  const r = h('button', { type: 'button', class: 'btn', id: 'presetRand', 'aria-label': 'Random preset' });
  r.innerHTML = '<svg width="16" height="16" viewBox="0 0 16 16" aria-hidden="true"><rect x="1.5" y="1.5" width="13" height="13" rx="3" fill="none" stroke="currentColor" stroke-width="1.4"/><circle cx="5" cy="5" r="1.2" fill="currentColor"/><circle cx="11" cy="11" r="1.2" fill="currentColor"/><circle cx="8" cy="8" r="1.2" fill="currentColor"/></svg>';
  r.onclick = (e) => { e.stopPropagation(); const list = S.presets || []; if (!list.length) return; let v = Math.floor(Math.random() * list.length); if (list[v][0] === S.presetName && list.length > 1) v = (v + 1) % list.length;
    histCommit(); H.expectUntil = performance.now() + 1500; S.presetName = list[v][0]; const t = b.querySelector('.t'); if (t) t.textContent = S.presetName; H.label = 'load ' + S.presetName; post('preset ' + v); toast('Loaded ' + S.presetName); };
  b.after(tip(r, 'Random preset', 'Loads a factory preset at random. ↶ takes you back.'));
};
