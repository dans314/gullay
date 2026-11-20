// ===========================================================================
// Gullay's own pages and pictures
// ===========================================================================
try { S.motion = localStorage.getItem('gullay.motion') || 'Full'; } catch (e) { S.motion = 'Full'; }
S.learnKind = 0; S.maps = []; S.learning = null;
const hd2 = (t) => h('h2', { class: 'hd', text: t, style: 'margin:0;font-size:15px' });
const vline = () => h('div', { style: 'width:1px;align-self:stretch;background:var(--line)' });

// ----- Engines: the memory (both engines), the Tapographic (only when one is in use; Tapo 1 / Tapo 2 with two),
// the answer and mirror (one engine) or the balance (two), transport and gain
S.tapoEdit = S.tapoEdit || 1;
const tapoName = (n) => (S.tapoEdit === 2 && tapoBoth() ? n.replace(/^Tapo /, 'Tapo 2 ') : n);
function tapoBoth() { return Math.round(P('Slot 1').v) === 1 && Math.round(P('Slot 2').v) === 2; }
function tapoAny() { return Math.round(P('Slot 1').v) === 1 || Math.round(P('Slot 2').v) === 2; }
function pageEngines() {
  const randMem = () => setMany([[P('DLD Memory'), chance(0.7) ? 1 : 0], [P('DLD Degrade'), Math.round(rnd(11, 16))]]);
  const mem = card('width:215px;flex:none', h('div', { class: 'row' }, hd2('MEMORY'), h('div', { class: 'sp' }), dice(ink, 'Random: memory and degrade', randMem)),
    h('div', { class: 'row', style: 'gap:10px;align-items:flex-end' }, labelled('Memory', seg(P('DLD Memory'), ['Clean', '16-bit'])), knob(P('DLD Degrade'), 'Degrade', ink, 46)),
    h('div', { class: 'hint', text: 'For both engines. 16-bit stores the echoes the way the modules do; Degrade takes them down toward 11 bits, like an old sampler.' }));
  const cards = [mem];
  if (tapoAny()) cards.push(tapoCard());
  const two = Math.round(P('Slot 2').v) > 0;
  if (!two) {
    const randOne = () => setMany([[P('Answer After'), Math.floor(Math.random() * 6)], [P('Answer Chance'), rnd(0.4, 1)], [P('Mirror Time'), pick([0.5, 0.75, 1, 1.5, 2])]]);
    cards.push(card('width:230px;flex:none', h('div', { class: 'row' }, hd2('ONE ENGINE'), h('div', { class: 'sp' }), dice(ink, 'Random: answer and mirror', randOne)),
      h('div', { class: 'row', style: 'gap:10px;align-items:flex-end' }, labelled('Answer after', sel(P('Answer After'), 70)), knob(P('Answer Chance'), 'Chance', ink, 36), knob(P('Mirror Time'), 'Mirror', ink, 36)),
      h('div', { class: 'hint', text: 'Call and Response: B repeats A’s echoes after the wait, on the beats Chance lets through. Mirror: B plays A backwards.' })));
  } else
    cards.push(card('width:170px;flex:none', hd2('TWO ENGINES'), knob(P('Slot Balance'), 'Balance', ink, 44), h('div', { class: 'hint', text: 'Left favours slot 1, right slot 2.' })));
  const freeBox = h('div', { class: 'row', style: 'gap:8px' }, labelled('Tempo', stepper(P('Free Tempo'))), toggle(P('Free Play'), 'Play'));
  bind(P('Sync'), (p) => { freeBox.style.opacity = Math.round(p.v) === 1 ? 1 : 0.35; });
  const m = mode();
  cards.push(card('width:210px;flex:none', hd2('TRANSPORT'), seg(P('Sync'), ['Host', 'Free']), freeBox,
    h('div', { class: 'row', style: 'gap:10px' }, knob(P(m + ' Input'), 'Input', ink, 34), knob(P(m + ' Output'), 'Output', ink, 34))));
  return h('div', {}, ...cards);
}
function tapoCard() {
  const both = tapoBoth(), pat = P(tapoName('Tapo Pattern')), grid = h('div', { class: 'pat', role: 'group', 'aria-label': 'Tapo pattern' });
  S.patBtns = pat.lab.map((l, i) => { const b = h('button', { type: 'button', text: l === 'Tapped' ? 'Tap' : l, onclick: () => setMany([[pat, i]]) }); grid.appendChild(b); return b; });
  bind(pat, () => S.patBtns.forEach((b, i) => b.classList.toggle('on', Math.round(pat.v) === i)));
  const randTapo = () => setMany([[pat, Math.floor(Math.random() * 24)], [P(tapoName('Tapo Morph')), Math.log2(rnd(20, 3000))], [P(tapoName('Tapo Morph Sync')), Math.floor(Math.random() * 7)], [P(tapoName('Tapo Velocity')), rnd(0.1, 0.7)], [P(tapoName('Tapo Mod Amount')), chance(0.6) ? 0 : rnd(0, 0.4)], [P(tapoName('Tapo Mod Rate')), rnd(0, 1)]]);
  const head = h('div', { class: 'row' }, hd2('TAPOGRAPHIC'));
  if (both) {
    const sw = h('div', { class: 'seg sm' });
    [1, 2].forEach((k) => sw.appendChild(h('button', { type: 'button', class: S.tapoEdit === k ? 'on' : '', text: 'Tapo ' + k, onclick: () => { S.tapoEdit = k; buildPages(); } })));
    head.append(tip(sw, 'Tapo 1 / Tapo 2', 'Both slots are Tapographic: pick which one this card sets. Each has its own pattern, morph, dynamics and wobble.'));
  }
  head.append(h('div', { class: 'sp' }), dice(ink, 'Random: pattern, morph and tap movement', randTapo));
  return card('flex:1.5', head, grid,
    h('div', { class: 'row', style: 'gap:8px;align-items:flex-end' }, morphBox(), knob(P(tapoName('Tapo Velocity')), 'Dynamics', ink, 34), knob(P(tapoName('Tapo Mod Amount')), 'Wobble', ink, 34), knob(P(tapoName('Tapo Mod Rate')), 'Rate', ink, 34)),
    tapPad(pat),
    h('div', { class: 'row', style: 'gap:8px' }, tip(toggle(P('MIDI Picks Patterns'), 'MIDI picks'), 'MIDI picks patterns', 'Notes C1–B2 (Bitwig’s C1 = MIDI note 36) and Program Change 1–24 pick slot 1’s pattern.'),
      h('span', { class: 'hint', text: 'Tapo Pattern is a mod target too.' })));
}
// tap a pattern with the mouse: the first click starts, then 4, 8 or 16 taps; it plays back as you tapped it
function tapPad(pat) {
  const cnt = P('Tapo Tap Count'), status = h('span', { class: 'hint' }), pad = h('button', { type: 'button', class: 'btn', text: 'TAP', style: 'width:84px;height:34px;font-weight:800;letter-spacing:.1em' });
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
  return h('div', { class: 'row', style: 'gap:8px' }, tip(pad, 'Tap', 'Click to start, then click in the rhythm you want: each click is an echo. The pattern goes to Tap (the last slot) and plays back as you tapped it.'), sg, status);
}
function morphBox() {
  const tm = P(tapoName('Tapo Morph Timing')), box = h('div', { class: 'col', style: 'gap:4px;align-items:center' }), inner = h('div');
  const sg = seg(tm, ['Free', 'Sync']); sg.classList.add('sm');
  bind(tm, () => inner.replaceChildren(Math.round(tm.v) === 1 ? labelled('Morph', sel(P(tapoName('Tapo Morph Sync')), 72)) : knob(P(tapoName('Tapo Morph')), 'Morph', ink, 40)));
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
    const sh = P(BAND[b] + ' Shift'), md = P(BAND[b] + ' Shift Mode'), sg = seg(md, ['Off', 'Once', 'Each']); sg.classList.add('sm');
    sg.style.setProperty('--segc', bandFill(b)); sg.style.setProperty('--segt', textOn(bandFill(b)));
    return h('div', { class: 'row', style: 'gap:8px' }, btag(b), h('div', { class: 'hk' }, knob(sh, 'Shift', () => bandColour(b), 24)), sg);
  };
  const keyTxt = h('span', { class: 'secs' }); S.keyTxt = keyTxt;
  const shift = card('width:300px;flex:none;gap:7px', h('div', { class: 'row' }, hd2('SHIFT'), h('div', { class: 'sp' }), dice(ink, 'Random: shifts', () => setMany([0, 1, 2].flatMap((b) => [[P(BAND[b] + ' Shift'), pick([0, 0, 5, 7, 12, -12, -5])], [P(BAND[b] + ' Shift Mode'), pick([0, 1, 2])]])))),
    row(0), row(1), row(2),
    hymnShapeRow(),
    tip(toggle(P('Tune Only Ringing'), 'Only the ringing'), 'Only the ringing', 'With Tune on, you hear only what Tune rings out of your sound (and its echoes), never the sound itself: at Amount 100 % a clap becomes just its rhythm, played by the notes of the key.'),
    h('div', { class: 'row', style: 'gap:8px' }, tip(toggle(P('Shift Settle'), 'Settle'), 'Settle', 'Each repeat on short echoes: the shift waits until the echoes are long enough to hear each step, so fast repeats don’t smear into noise.'),
      tip(toggle(P('MIDI Sets Key'), 'MIDI key'), 'MIDI sets the key', 'Hold notes on Gullay’s MIDI input: the lowest one becomes Hymn’s root (Tune and the shifts snap to it). Letting go keeps the last key.'), keyTxt),
    h('div', { class: 'hint', text: 'Shift: semitones in the key. Once: one shift. Each: every repeat goes further.' }));
  d.appendChild(shift);
  return d;
}

function hymnShapeRow() {
  const sh = P('Hymn Shape'), sg = seg(sh, ['Free', 'Gate', 'Decay']); sg.classList.add('sm');
  const knobs = h('div', { class: 'row', style: 'gap:6px;align-items:flex-end' }, knob(P('Hymn Threshold'), 'Thresh', ink, 26), knob(P('Hymn Hold'), 'Hold', ink, 26), knob(P('Hymn Decay'), 'Decay', ink, 26));
  bind(sh, () => { knobs.style.opacity = Math.round(sh.v) === 0 ? 0.35 : 1; });
  return h('div', { class: 'col', style: 'gap:4px' }, tip(h('div', { class: 'row', style: 'gap:6px' }, h('div', { class: 'lbl', text: 'Shape' }), sg), 'Shape',
    'Free: the ringing rings for Ring time. Gate: it sounds only while your sound is over Threshold, then Hold, then fades over Decay: a gated layer over the original. Decay: every hit rings and fades over Decay, so you can make it plucky or long.'), knobs);
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
    h('div', { class: 'row', style: 'gap:12px;align-items:flex-end' }, labelled('Timing', seg(P('River Timing'))), rate, knob(P('River Meander'), 'Meander', ink, 40), knob(P('River Spread'), 'Spread', ink, 40),
      tip(toggle(P('River Tide'), 'Tide'), 'Tide', 'A tidal reach: every few bars the flow turns and runs back up the river.'), labelled('Turns every', sel(P('Tide Turns Every'), 80))));
  const c2 = card('width:440px;flex:none;gap:6px', h('div', { class: 'row' }, hd2('SENDS'), h('span', { class: 'hint', text: 'four routes for the currents (Mod slots 13–16)' })),
    ...[13, 14, 15, 16].map((k) => riverSend(k)),
    h('div', { class: 'row', style: 'gap:8px' }, knob(P('Depth River A'), 'A depth', () => bandColour(0), 26), knob(P('Depth River Lead'), 'Lead depth', () => bandColour(1), 26), knob(P('Depth River B'), 'B depth', () => bandColour(2), 26),
      h('span', { class: 'hint', text: 'The river’s course is a stand-in until the Ogeechee’s own trace is added.' })));
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
  const mea = P('River Meander').v, spr = P('River Spread').v, pos = (u && u.gy) ? u.gy.riverPos : 0;
  g.strokeStyle = riverColour(); g.lineWidth = 10; g.lineCap = 'round'; g.beginPath();
  for (let i = 0; i <= 300; ++i) { const x = i / 300 * W, y = H / 2 - riverValue(i / 300, mea) * H * 0.38; i ? g.lineTo(x, y) : g.moveTo(x, y); }
  g.stroke(); g.lineWidth = 1;
  for (let b = 0; b < 3; ++b) {
    const p = (pos + spr * b * 0.25) % 1, x = p * W, y = H / 2 - riverValue(p, mea) * H * 0.38;
    g.fillStyle = bandFill(b); g.beginPath(); g.arc(x, y, 7, 0, Math.PI * 2); g.fill(); g.strokeStyle = T.graph; g.lineWidth = 2; g.stroke();
    g.fillStyle = T.ink; g.font = 'italic 800 10px "Avenir Next", -apple-system, sans-serif'; g.textAlign = 'center'; g.fillText(BAND[b], x, y - 11);
  }
  g.fillStyle = T.label; g.font = '10px -apple-system, sans-serif'; g.textAlign = 'left'; g.fillText('upstream', 6, H - 6); g.textAlign = 'right'; g.fillText('to the coast', W - 6, H - 6);
}

// ----- MIDI: every learnt control, its kind, behaviour, pickup and range
const MIDI_KINDS = ['Absolute', '14-bit', 'NRPN', 'Relative 1', 'Relative 2', 'Relative 3'];
const MIDI_BEH = ['Knob', 'Toggle', 'Momentary'];
function mapLabel(m) { return 'ch ' + (m.ch + 1) + ' · ' + (m.kind === 2 ? 'NRPN ' : 'CC ') + m.num; }
function startLearn(p) {
  S.learning = p.id; post('learn ' + p.id + ' ' + S.learnKind);
  toast('MIDI Learn: move a control for ' + p.n + ' (as ' + MIDI_KINDS[S.learnKind] + ')');
}
function pageMidi() {
  const kind = h('div', { class: 'seg sm' });
  const kbs = MIDI_KINDS.map((t, i) => { const b = h('button', { type: 'button', text: t, onclick: () => { S.learnKind = i; kbs.forEach((x, k) => x.classList.toggle('on', k === i)); } }); kind.appendChild(b); return b; });
  kbs.forEach((x, k) => x.classList.toggle('on', k === S.learnKind));
  const list = h('div', { class: 'col', style: 'gap:4px;overflow:auto;min-height:0;flex:1' }); S.mapList = list;
  const learnCard = card('flex:1;min-height:0', h('div', { class: 'row' }, hd2('MIDI LEARN'), h('span', { class: 'hint', text: 'Right-click any knob → MIDI Learn…, then move a control.' }), h('div', { class: 'sp' }),
      h('button', { type: 'button', class: 'btn', text: 'Save as default', onclick: () => post('mapdefault save') }), h('button', { type: 'button', class: 'btn', text: 'Load default', onclick: () => post('mapdefault load') }),
      h('button', { type: 'button', class: 'btn', text: 'Clear all', onclick: () => confirmBox('Forget every MIDI mapping in this project?', 'Clear', 'Cancel', () => post('mapdel -1')) })),
    h('div', { class: 'row', style: 'gap:8px' }, h('div', { class: 'lbl', text: 'Learn as' }), kind),
    h('div', { class: 'hint', text: 'Faderfox EC4: match its encoder setting (CC Abs, CC Rel 1 / Rel 2, 14-bit or NRPN). NRPN is found by itself. Relative 1 = two’s complement, 2 = offset 64, 3 = sign bit.' }),
    h('div', { class: 'maprow maphead' }, h('span', { text: 'Control' }), h('span', { text: 'Ch' }), h('span', { text: 'Number' }), h('span', { text: 'Kind' }), h('span', { text: 'Behaviour' }), h('span', { text: 'Pickup' }), h('span', { text: 'Range %' }), h('span')),
    list);
  const ext = card('width:260px;flex:none', hd2('MIDI IN'), tip(toggle(P('MIDI Sets Key'), 'Held notes set the key'), 'MIDI sets the key', 'The lowest held note is Hymn’s root.'),
    tip(toggle(P('MIDI Picks Patterns'), 'Notes / PC pick patterns'), 'MIDI picks patterns', 'C1–B2 and Program Change 1–24 pick the Tapo pattern.'),
    h('div', { class: 'hint', text: 'Mappings are saved with the project. Save as default: new copies of Gullay start with them.' }),
    h('div', { style: 'height:1px;background:var(--line)' }), hd2('SNAPSHOT'),
    h('div', { class: 'hint', text: 'Writes every setting, your mappings and a note to a text file, for feedback.' }), h('button', { type: 'button', class: 'btn', text: '✎ Make a snapshot…', onclick: snapshotDialog }));
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
  snapshot(path) { toast(path ? 'Snapshot saved: ' + path.split(/[\\/]/).pop() : 'Could not write the snapshot'); },
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
  if (S.learning !== null && g.learn < 0) { S.learning = null; toast('Learnt'); post('maps'); }
  S.riverPos = g.riverPos; S.riverAt = performance.now();
  if (g.lvl) { const w = S.wl || (S.wl = { a: [], t: [] }); w.a.push(g.lvl); w.t.push(performance.now()); while (w.a.length > 420) { w.a.shift(); w.t.shift(); } }
  drawRiverPage(u); drawSpectrum(u);
}

// ----- the water line: your sound above, the echoes below, each part's echo time marked
function drawGraph() {
  const cv = document.getElementById('graph'); if (!cv || !S.ready) return;
  if (spectralOn()) return drawSplit(cv);
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
    if (S.ready && !spectralOn()) { const gc = document.getElementById('graph'); if (gc) drawEcho(gc); }
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
  const names = ['Seq A', 'Seq Lead', 'Seq B', 'LFO 1', 'LFO 2', 'LFO 3', 'Follower', 'River A', 'River Lead', 'River B'];
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
  const s1 = Math.round(P('Slot 1').v), s2 = Math.round(P('Slot 2').v), m2 = Math.round(P('One Engine: Part B').v);
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
  const [g, W, H] = prep(cv); g.fillStyle = T.graph; g.fillRect(0, 0, W, H);
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
