# Gullay window: the edits to Three's Company's index.html (run by tx.py)

def replace_range(start_prefix, end_fn):
    """from the line starting with start_prefix through the end of function end_fn"""
    global L
    i = next(k for k, l in enumerate(L) if l.startswith(start_prefix))
    a, b = find_fn(end_fn)
    assert a > i
    return i, b

def put_range(i, j, new):
    global L
    L = L[:i] + new.strip('\n').split('\n') + L[j + 1:]

# ---------------------------------------------------------------- CSS + markup
replace_block(':root{', '}', r'''
:root{
  --bg:#E9F1EF; --panel:#F4F8F7; --raised:#E0EBE9; --line:#CADBD8; --off:#D8E5E3;
  --ink:#26393E; --dim:#566C6F; --graph:#FBFDFC; --card:#FBFDFC; --onink:#FFFFFF;
  --low:#5D68B0; --hi:#786FA3; --hifill:#DCD8EA; --hitext:#4A4175; --mid:#80C2B9;
}''')
replace_block('[data-theme=dark]{', '}', '')
SUBS = []
SUBS.append(('</style>', r'''#river{position:absolute;left:0;top:0;width:100%;height:100%;pointer-events:none;z-index:0}
#app{position:relative;z-index:1}
nav .tabs button{padding:0 8px;font-size:12px}
.pat{display:grid;grid-template-columns:repeat(6,1fr);gap:4px}
.pat button{height:26px;border:1px solid var(--line);border-radius:5px;background:var(--card);font-size:11px;font-weight:700;color:var(--dim);padding:0}
.pat button.on{background:var(--ink);color:var(--onink);border-color:var(--ink)}
.pat button.now{box-shadow:inset 0 -3px 0 var(--mid)}
.rmeter{flex:1;height:5px;border-radius:3px;background:var(--off);overflow:hidden;min-width:40px}
.rmeter i{display:block;height:5px;width:0;background:var(--ink)}
.maprow{display:grid;grid-template-columns:minmax(120px,1fr) 44px 60px 150px 104px 70px 132px 26px;gap:6px;align-items:center;font-size:12px}
.maprow input{width:58px;font:inherit;font-size:12px;padding:3px 4px;border:1px solid var(--line);border-radius:5px;background:var(--card);color:var(--ink)}
.maphead{font-size:10px;letter-spacing:.07em;text-transform:uppercase;color:var(--dim);font-weight:700}
.secs{font-variant-numeric:tabular-nums;font-size:11.5px;font-weight:700}
.strip .top .secs{color:var(--ct);opacity:.9}
</style>'''))
SUBS.append(("<title>Three's Company</title>", '<title>Gullay</title>'))
replace_line_starting('      <h1><span class="logow"', '      <h1><span class="vname">Gullay</span></h1>')
SUBS.append(('<div class="seg" id="routing"></div>', '<div class="row" id="routing" style="gap:6px"></div>'))
replace_line_starting('  <div class="panel" id="graphBox"', '  <div class="panel" id="graphBox" style="padding:6px 10px;flex:1 1 92px;min-height:80px;display:flex;position:relative"><canvas id="graph" style="flex:1;width:100%;height:100%" aria-label="The water line: your sound above, its echoes below"></canvas></div>')
SUBS.append(('<div id="stage"><div id="app">', '<div id="stage"><canvas id="river" aria-hidden="true"></canvas><div id="app">'))
SUBS.append(('<footer><b>Three’s Company</b><span class="fh">v0.18</span><button type="button" class="fbtn" id="setBtn">⚙ Settings</button>',
             '<footer><b>Gullay</b><span class="fh">v0.4 · test build</span><button type="button" class="fbtn" id="setBtn">⚙ Settings</button><button type="button" class="fbtn" id="snapBtn">✎ Snapshot</button>'))

# ---------------------------------------------------------------- themes
replace_block('const THEMES = {', '};', r'''
// the looks: Porch Ceiling (the default) and its dark side, Light and Dark, then the named palettes (colours only). part = A, Lead, B.
const THEMES = {
  porch: { label: 'Porch Ceiling', dark: 0, bg: '#DCEFF1', panel: '#EDF8F9', raised: '#D2E8EA', line: '#B9D9DC', off: '#CCE3E5', ink: '#1F3A44', dim: '#4E6C74', card: '#F7FCFC', graph: '#F7FCFC', foot: '#BFE3E6', footInk: '#123038', part: ['#4C8AA6', '#78C3C6', '#6A7DB6'], river: '#9FD3DA' },
  porchdark: { label: 'Porch Ceiling Dark', dark: 1, bg: '#0F2026', panel: '#152B32', raised: '#1E3940', line: '#2C4C54', off: '#24414A', ink: '#E2F2F4', dim: '#9DBBC1', card: '#19323A', graph: '#11252B', foot: '#1E4A52', footInk: '#E3F6F8', part: ['#6FB0CC', '#78C3C6', '#8C9BD6'], river: '#2A5A63' },
  bw: { label: 'Black and White', dark: 0, bg: '#E9E9E9', panel: '#F6F6F6', raised: '#DEDEDE', line: '#C4C4C4', off: '#D6D6D6', ink: '#111111', dim: '#4A4A4A', card: '#FFFFFF', graph: '#FFFFFF', foot: '#1A1A1A', footInk: '#F2F2F2', part: ['#3A3A3A', '#8C8C8C', '#151515'], river: '#CFCFCF' },
  jax: { label: 'Jax', dark: 1, bg: '#101014', panel: '#18171E', raised: '#24222C', line: '#3A3646', off: '#2C2936', ink: '#ECEBF2', dim: '#BDB8CC', card: '#1D1B24', graph: '#141319', foot: '#3B2A5E', footInk: '#EDE6FA', part: ['#B9BEC9', '#8F6BD8', '#6B45C2'], river: '#352C4D' },
  prince: { label: 'Prince', dark: 1, bg: '#160C20', panel: '#21122F', raised: '#2E1A40', line: '#48305E', off: '#3A2350', ink: '#F4EAFB', dim: '#CDB8DE', card: '#271637', graph: '#1B0F28', foot: '#6A2FA0', footInk: '#F8F0FF', part: ['#7B3FB8', '#B57EE6', '#E2C7F7'], river: '#3B1F57' },
  persona: { label: 'Persona 3', dark: 1, bg: '#081329', panel: '#0D1C3B', raised: '#13284F', line: '#25427A', off: '#1A3263', ink: '#EAF5FF', dim: '#B4CCE8', card: '#0F2246', graph: '#0A1733', foot: '#1E6FD9', footInk: '#F2F9FF', part: ['#2E7EEB', '#4FD0F5', '#B9E8FF'], river: '#173A6E' },
  panther: {label: "Black Panther", dark: 1, bg: "#0B0A10", panel: "#16151C", raised: "#222027", line: "#34323A", off: "#26252C", ink: "#ECE7F7", dim: "#A8A5B2", card: "#1B1920", graph: "#08080C", foot: "#442F75", footInk: "#ECE7F7", part: ["#8A5CF0", "#C9C4D8", "#B48CFF"], river: "#312353"},
  storm: {label: "Storm", dark: 1, bg: "#13161D", panel: "#1E2128", raised: "#292C33", line: "#3B3E45", off: "#2E3137", ink: "#F3F5F9", dim: "#B0B2B7", card: "#23262C", graph: "#0E1016", foot: "#515D6C", footInk: "#F3F5F9", part: ["#9DB3CD", "#EEF1F6", "#4FA8FF"], river: "#3C4552"},
  static: {label: "Static Shock", dark: 1, bg: "#0C0E13", panel: "#181A1F", raised: "#23252A", line: "#35383D", off: "#282A2F", ink: "#F2F5FA", dim: "#ADB0B5", card: "#1C1E23", graph: "#090A0E", foot: "#1C417D", footInk: "#F2F5FA", part: ["#2F7FFF", "#F5D90A", "#E6ECF5"], river: "#16305A"},
  miles: {label: "Miles Morales", dark: 1, bg: "#0B0B0D", panel: "#171618", raised: "#222224", line: "#353436", off: "#272728", ink: "#F4F1F1", dim: "#AEACAD", card: "#1B1B1D", graph: "#08080A", foot: "#6E1D22", footInk: "#F4F1F1", part: ["#E8343C", "#F2F2F2", "#B01020"], river: "#4D171B"},
  eva00: {label: "EVA-00", dark: 0, bg: "#E1EAF4", panel: "#F0F4FA", raised: "#D5DEE9", line: "#BCC6D3", off: "#CCD6E1", ink: "#14233A", dim: "#5C697B", card: "#F8FAFC", graph: "#F9FBFD", foot: "#A1BCE7", footInk: "#14233A", part: ["#2B66CF", "#E0A800", "#5F82B0"], river: "#AAC2E9"},
  eva01: {label: "EVA-01", dark: 1, bg: "#130C1E", panel: "#1E1729", raised: "#292234", line: "#3B3446", off: "#2E2739", ink: "#F0EAFB", dim: "#AEA7B9", card: "#221C2D", graph: "#0E0916", foot: "#462771", footInk: "#F0EAFB", part: ["#8447D6", "#7CE84A", "#F28C28"], river: "#351E55"},
  eva02: {label: "EVA-02", dark: 1, bg: "#1A0B0C", panel: "#251617", raised: "#302222", line: "#423434", off: "#352627", ink: "#FBEDEA", dim: "#B8A9A7", card: "#2A1B1C", graph: "#140809", foot: "#73171C", footInk: "#FBEDEA", part: ["#E0262F", "#F28A1E", "#2FB37A"], river: "#551316"},
  thanos: {label: "Thanos", dark: 1, bg: "#120C1C", panel: "#1D1727", raised: "#282232", line: "#3A3444", off: "#2D2737", ink: "#F3EDFB", dim: "#B0AAB8", card: "#221C2C", graph: "#0E0915", foot: "#472C67", footInk: "#F3EDFB", part: ["#8752C2", "#E3B53A", "#3F74DA"], river: "#35214E"},
  spawn: {label: "Spawn", dark: 1, bg: "#09090A", panel: "#151516", raised: "#202021", line: "#333334", off: "#252526", ink: "#F1F1F1", dim: "#ABABAC", card: "#19191A", graph: "#070708", foot: "#630D1B", footInk: "#F1F1F1", part: ["#D0112F", "#E9E9E9", "#4FD866"], river: "#450B15"},
  livingsingle: {label: "Living Single", dark: 0, bg: "#FBEBD6", panel: "#FDF5EA", raised: "#EFDECC", line: "#D6C5B8", off: "#E6D6C5", ink: "#2B1830", dim: "#74626A", card: "#FEFAF5", graph: "#FEFBF7", foot: "#CEAEC2", footInk: "#2B1830", part: ["#7A3D9E", "#E27A1A", "#C9A20C"], river: "#D4B7C5"},
  martin: {label: "Martin", dark: 0, bg: "#FDF0DC", panel: "#FEF8EE", raised: "#F0E3D0", line: "#D5C9B8", off: "#E7DBC8", ink: "#1F1A14", dim: "#6D655A", card: "#FEFBF6", graph: "#FFFCF8", foot: "#EEB09E", footInk: "#1F1A14", part: ["#D23A2A", "#D99A00", "#2E5FC4"], river: "#F0B9A7"},
  shutter: { label: 'Shutter', dark: 0, bg: '#D3E6E3', panel: '#E6F2F0', raised: '#C8DEDA', line: '#AFCBC6', off: '#C1D8D4', ink: '#183634', dim: '#45625F', card: '#F3F9F8', graph: '#F3F9F8', foot: '#A8CFC9', footInk: '#0F2B29', part: ['#2F6F73', '#5EA79F', '#4D6D99'], river: '#8CC2BA' },
  deep: { label: 'Low Tide', dark: 1, bg: '#0E1B28', panel: '#152636', raised: '#1E3347', line: '#2B4560', off: '#22384D', ink: '#E2EEF7', dim: '#9FB6C8', card: '#182C3E', graph: '#11212F', foot: '#21456A', footInk: '#E6F2FF', part: ['#6EA6DA', '#7ED0C6', '#9C8ED6'], river: '#1F4E6E' },
  sugar: { label: 'Sugar and Spice', dark: 0, bg: '#FBEFF4', panel: '#FFF7FA', raised: '#F5E2EA', line: '#EBCFDB', off: '#F1DDE6', ink: '#3A2531', dim: '#7A5E6B', card: '#FFFCFD', graph: '#FFFCFD', foot: '#F7CFE0', footInk: '#3A1F2C', part: ['#E2729F', '#63B6E6', '#73C27E'], river: '#F3C2D6' },
};''')
replace_fn('applyTheme', r'''
function applyTheme(name) {
  if (name === true) name = 'porchdark'; if (name === false) name = 'porch';
  const th = THEMES[name] || THEMES.porch; S.themeName = THEMES[name] ? name : 'porch'; S.dark = !!th.dark;
  const rgb = (c) => [1, 3, 5].map((i) => parseInt(c.substr(i, 2), 16)).join(',');
  T = { ink: th.ink, inkRGB: rgb(th.ink), onink: th.dark ? th.bg : '#FFFFFF', card: th.card, graph: th.graph, grid: th.raised, line: th.line, label: th.dim, panel2: th.panel, off: th.off,
    low: th.part[0], part: th.part, river: th.river, foot: th.foot };
  T.inkA = (a) => `rgba(${T.inkRGB},${a})`;
  const r = document.documentElement, set = (k, v) => r.style.setProperty(k, v);
  r.dataset.theme = S.dark ? 'dark' : 'light';
  set('--bg', th.bg); set('--panel', th.panel); set('--raised', th.raised); set('--line', th.line); set('--off', th.off); set('--ink', th.ink); set('--dim', th.dim);
  set('--graph', th.graph); set('--card', th.card); set('--onink', T.onink); set('--hifill', th.foot); set('--hitext', th.part[2]); set('--low', th.part[0]); set('--hi', th.part[2]); set('--mid', th.part[1]);
  const ft = document.querySelector('footer'); if (ft) ft.style.color = th.footInk;
}''')
SUBS.append(("applyTheme(d.theme === 'dark');", "applyTheme(d.theme || 'porch');"))

# ---------------------------------------------------------------- colours: the parts
i, j = replace_range("const BAND = ['Low', 'Mid', 'High'];", 'paintMid')
put_range(i, j, r'''
// ---------------------------------------------------------------------------
// colours: the three parts (A, Lead, B) take the theme's part colours
// ---------------------------------------------------------------------------
const BAND = ['A', 'Lead', 'B'];
function mixRGB(a, b, t) {
  const p = (c) => c[0] === '#' ? [1, 3, 5].map((i) => parseInt(c.substr(i, 2), 16)) : c.match(/\d+/g).map(Number);
  const x = p(a), y = p(b); return 'rgb(' + x.map((v, i) => Math.round(v + (y[i] - v) * t)).join(',') + ')';
}
// Lead's colour follows the Chaperone's Lean, as Mid's did in Three's Company: even = the theme's Lead colour,
// leaning toward A takes it toward A's colour, toward B toward B's
function leanAmount() { const l = P('Chaperone Lean') ? P('Chaperone Lean').v : 0.5; return Math.max(-1, Math.min(1, (l - 0.5) * 2)); }
function midColour() { const pt = T.part || ['#5D68B0', '#80C2B9', '#786FA3'], d = leanAmount(); return mixRGB(pt[1], d < 0 ? pt[0] : pt[2], Math.abs(d) * 0.85); }
function midLine() { return midColour(); }
function leanInk() { return midColour(); }
function bandColour(b) { return b === 1 ? midColour() : (T.part ? T.part[b] : ['#5D68B0', '#80C2B9', '#786FA3'][b]); }
function bandFill(b) { return bandColour(b); }
function riverColour() { return mixRGB(T.river || '#A9D6D0', midColour(), 0.45); }
function goldShare() { return 0.5; }
function paintMix() { return [128, 194, 185]; }
function paintName() { return 'Lead'; }
// text that reads on a fill: white on dark, the ink on light
function textOn(c) {
  let r, g, b;
  if (c[0] === '#') { r = parseInt(c.substr(1, 2), 16); g = parseInt(c.substr(3, 2), 16); b = parseInt(c.substr(5, 2), 16); }
  else[r, g, b] = c.match(/\d+/g).map(Number);
  return 0.299 * r + 0.587 * g + 0.114 * b > 150 ? '#1F2E32' : '#FFFFFF';
}
function logoColour() { return T.ink; }
function paintMid() {
  const c = midColour(), r = document.documentElement, tt = textOn(c);
  r.style.setProperty('--mid', c); r.style.setProperty('--tabc', c); r.style.setProperty('--tabt', tt); r.style.setProperty('--leanbar', c);
  r.style.setProperty('--namec', c);
  (S.midPainters || []).forEach((f) => f());
  (S.navKnobs || []).forEach((k) => k.draw());
  document.querySelectorAll('[data-midc]').forEach((el) => { const k = el.dataset.midc; if (k.startsWith('--')) el.style.setProperty(k, c); else el.style[k] = c; });
  for (let i = DICE.length - 1; i >= 0; --i) { if (!DICE[i].isConnected) DICE.splice(i, 1); else DICE[i].paint(); }
  document.querySelectorAll('[data-midt]').forEach((el) => { el.style.setProperty(el.dataset.midt, tt); });
  S.riverStill = 0;
}''')

# ---------------------------------------------------------------- locks, targets, sources
replace_line_starting("const PLN = ['Cutoff'", "const PLN = ['Time', 'Feedback', 'Tone', 'Drive', 'Level', 'Pan'];")
replace_line_starting("const PL_KNOB = {", "const PL_KNOB = { Time: 0, Feedback: 1, Tone: 2, Drive: 3, Level: 4, Pan: 5 };")
SUBS.append(("(Low|Mid|High) (Cutoff|Resonance|Morph|Drive|Level|Pan)", "(A|Lead|B) (Time|Feedback|Tone|Drive|Level|Pan)"))
replace_fn('fmtLock', r'''
function fmtLock(k, f) {
  if (k === 0) { const r = Math.pow(2, (-48 + 96 * f) / 12); return (r >= 1 ? r.toFixed(r >= 4 ? 1 : 2) : r.toFixed(3)) + '×'; }
  if (k === 1) return Math.round(f * 120) + ' %';
  if (k === 4) { const db = -60 + 72 * f; return (db >= 0 ? '+' : '') + db.toFixed(1) + ' dB'; }
  if (k === 5) { const v = -1 + 2 * f; return Math.abs(v) < 0.02 ? 'C' : (v < 0 ? 'L' : 'R') + Math.round(Math.abs(v) * 100); }
  return Math.round(f * 100) + ' %';
}''')
replace_line_starting("const SOURCES = [", "const SOURCES = ['Random', 'Mod Wheel', 'Velocity', 'LFO 1', 'LFO 2', 'LFO 3', 'Hymn Env', 'River A', 'River Lead', 'River B'];")
replace_fn('targetIndexFor', r'''
// a knob's place in the mod target list (-1: not a target, or not in the wiring now showing)
const TARGET_ALIAS = { 'Engine Balance': 'Balance', 'Runaway Tame': 'Runaway Tame', 'Crusher Mix': 'Crusher Mix', 'Reverb Mod Depth': 'Reverb Mod' };
function targetIndexFor(p) {
  const tl = P('Mod 1 Target').lab, n = p.n || '';
  let m = /^(Par|Ser) (A|Lead|B) (Time|Feedback|Tone|Drive|Level|Pan)$/.exec(n);
  if (m) { if (m[1] !== mode()) return -1; return tl.indexOf(m[2] + ' ' + m[3]); }
  m = /^(Par|Ser) Mix$/.exec(n);
  if (m) return m[1] === mode() ? tl.indexOf('Mix') : -1;
  m = /^(Par|Ser) (A|Lead|B) Mod Depth$/.exec(n);
  if (m) return m[1] === mode() ? tl.indexOf(m[2] + ' Mod Depth') : -1;
  if (n === 'Spectral Balance' || n === 'Spectral Sharpness') return spectralOn() ? tl.indexOf(n) : -1;
  const k = tl.indexOf(TARGET_ALIAS[n] || n);
  return k > 0 ? k : -1;
}''')
SUBS.append(("const band = m[2], kind = m[3] === 'Resonance' ? 'Reso' : m[3];", "const band = m[2], kind = m[3];"))
SUBS.append(("(kind === 'Cutoff' || on('Sway to ' + kind))", "on('Sway to ' + kind)"))
SUBS.append(("if (kind === 'Cutoff' && band !== 'Mid' && on('Chaperone Influence')) out.push(['Chaperone: passes on Mid’s moves', 'conductor']);",
             "if (band !== 'Lead' && on('Chaperone Influence')) out.push(['Chaperone: passes on Lead’s moves', 'conductor']);"))
SUBS.append(("    if (band !== 'Mid') { const pre = 'Mutation ' + band + ' ',", "    if (band !== 'Lead') { const pre = 'Mutation ' + band + ' ',"))
SUBS.append(("'Mutation: answers Mid '", "'Mutation: answers Lead '"))
replace_fn('targetColumns', r'''
// the targets (None, A ×6, Lead ×6, B ×6, then the rest) as columns
function targetColumns(p) {
  const L = p.lab, it = (i) => ({ label: i === 0 ? 'None' : L[i].replace(/^(A|Lead|B) /, ''), value: i }), at = (n) => L.indexOf(n);
  const col = (b, from) => [{ head: BAND[b], colour: bandFill(b) }].concat([0, 1, 2, 3, 4, 5].map((k) => it(from + k))).concat(at(BAND[b] + ' Mod Depth') > 0 ? [it(at(BAND[b] + ' Mod Depth'))] : [], at(BAND[b] + ' Shift') > 0 ? [it(at(BAND[b] + ' Shift'))] : [], ['Filter Cutoff', 'Filter Reso', 'Filter Morph'].filter((n) => at(BAND[b] + ' ' + n) > 0).map((n) => it(at(BAND[b] + ' ' + n))));
  const g1 = ['Mix', 'Balance', 'Sway', 'Mod Depth', 'Juba Pattern', 'Juba Morph', 'DLD Degrade', 'Runaway Tame', 'Runaway Ceiling'].concat(spectralOn() ? ['Spectral Split', 'Spectral Balance', 'Spectral Sharpness'] : []);
  const g2 = [];
  const g3 = ['Tune Amount', 'Tune Ring Time', 'Tune Tilt', 'Tune Slope', 'Spectral Low/Mid', 'Spectral Mid/High', 'Spectral Low Level', 'Spectral Mid Level', 'Spectral High Level'];
  const pickAll = (list) => list.filter((n) => at(n) > 0).map((n) => it(at(n)));
  return [[{ head: 'Off' }, it(0)], col(0, 1), col(1, 7), col(2, 13), [{ head: 'Engines' }].concat(pickAll(g1)), [{ head: 'Hymn & Spectral' }].concat(pickAll(g3))];
}''')
SUBS.append(("const isTargetList = (p) => p.lab && p.lab.length >= 25 && p.lab[1] === 'Low Cutoff';", "const isTargetList = (p) => p.lab && p.lab.length >= 25 && p.lab[1] === 'A Time';"))
SUBS.append(("openPop(b, null, Math.round(p.v), pick, { columns: targetColumns(p), colWidth: [70, 104, 104, 104, 150] });", "openPop(b, null, Math.round(p.v), pick, { columns: targetColumns(p), colWidth: [60, 96, 96, 96, 128, 112, 128] });"))

# knob menu: MIDI Learn
SUBS.append(("  items.push({ label: 'Reset to default', value: 'reset' }, { label: 'Copy value', value: 'copy' },",
             "  const mapped = (S.maps || []).find((x) => x.id === p.id);\n  items.push({ label: S.learning === p.id ? 'Cancel MIDI Learn' : 'MIDI Learn…', value: S.learning === p.id ? 'unlearnarm' : 'learn', note: mapped ? 'CC' : '' });\n  if (mapped) items.push({ label: 'Forget MIDI (' + mapLabel(mapped) + ')', value: 'unlearn' });\n  items.push({ sep: true });\n  items.push({ label: 'Reset to default', value: 'reset' }, { label: 'Copy value', value: 'copy' },"))
SUBS.append(("    if (v === 'none') return;\n", "    if (v === 'none') return;\n    if (v === 'learn') { startLearn(p); return; }\n    if (v === 'unlearnarm') { post('learn off'); S.learning = null; toast('MIDI Learn cancelled'); return; }\n    if (v === 'unlearn') { const mp = (S.maps || []).find((x) => x.id === p.id); if (mp) post('mapdel ' + mp.slot); toast('Forgot the MIDI control for ' + p.n); return; }\n"))

# ---------------------------------------------------------------- randomizers and copy / paste
replace_fn('randomStrip', r'''
// one part: time (on the musical ratios), feedback, tone, a little drive now and then, pan
function randomStrip(pre) {
  const lead = /Lead /.test(pre), ch = [];
  ch.push([P(pre + 'Time'), lead ? pick([-12, 0, 0, 0, 7.02, 12]) : pick([-24, -12, -7.02, -4.98, 0, 0, 7.02, 12, 19.02])]);
  ch.push([P(pre + 'Feedback'), lead ? rnd(0.7, 1.05) : rnd(0.2, 0.75)], [P(pre + 'Tone'), rnd(0.3, 0.75)], [P(pre + 'Drive'), chance(0.3) ? rnd(0.05, 0.4) : 0]);
  if (!lead) ch.push([P(pre + 'Pan'), chance(0.3) ? 0 : rnd(-0.7, 0.7)]);
  setMany(ch);
}''')
delete_fn('randomBank')
SUBS.append(("strip: { name: (k) => BAND[k], pre: (k) => mode() + ' ' + BAND[k] + ' ', n: 3, fields: ['Filter On', 'Filter Type', 'Filter Variant', 'Cutoff', 'Resonance', 'Morph', 'Pan', 'Level', 'Mix', 'Mod Dept",
             "strip: { name: (k) => BAND[k], pre: (k) => mode() + ' ' + BAND[k] + ' ', n: 3, fields: ['Time', 'Feedback', 'Tone', 'Drive', 'Level', 'Pan', 'Time Sync', 'INF', 'Reverse', 'Mod Depth'] },\n  stripOld: { name: (k) => BAND[k], pre: (k) => mode() + ' ' + BAND[k] + ' ', n: 3, fields: ['Mod Dept"))
SUBS.append(("lock: { name: (k) => BAND[k] + ' locks', pre: (k) => 'Lock ' + BAND[k] + ' ', n: 3, fields: ['Cutoff', 'Reso', 'Morph', 'Drive', 'Level', 'Pan']",
             "lock: { name: (k) => BAND[k] + ' locks', pre: (k) => 'Lock ' + BAND[k] + ' ', n: 3, fields: ['Time', 'Feedback', 'Tone', 'Drive', 'Level', 'Pan']"))
SUBS.append(("mut: { name: (k) => ['Low', 'High'][k] + ' answer', pre: (k) => 'Mutation ' + ['Low', 'High'][k] + ' ',", "mut: { name: (k) => ['A', 'B'][k] + ' answer', pre: (k) => 'Mutation ' + ['A', 'B'][k] + ' ',"))
SUBS.append(("  ['strip', 'bank', 'seq', 'lock', 'timing'].forEach((kind) => {", "  ['strip', 'seq', 'lock', 'timing'].forEach((kind) => {"))
SUBS.append(("toast('Swapped ' + BAND[a] + ' and ' + BAND[b] + ' (filters, drives, banks, lanes, locks)');", "toast('Swapped ' + BAND[a] + ' and ' + BAND[b] + ' (knobs, lanes, locks, sway)');"))
SUBS.append(("const strip = mode() + ' ' + BAND[b] + ' ', knobs = ['Cutoff', 'Resonance', 'Morph', 'Drive', 'Level', 'Pan'];", "const strip = mode() + ' ' + BAND[b] + ' ', knobs = ['Time', 'Feedback', 'Tone', 'Drive', 'Level', 'Pan'];"))
SUBS.append(("  ['Low', 'High'].forEach((b, i) => {\n    const pre = 'Mutation ' + b + ' ';", "  ['A', 'B'].forEach((b, i) => {\n    const pre = 'Mutation ' + b + ' ';"))
SUBS.append(("  return /Cutoff/.test(lbl) ? rnd(-1, 1) : /Reso/.test(lbl) ? rnd(-0.3, 0.3) : /Morph/.test(lbl) ? rnd(-0.5, 0.5)",
             "  return /Time/.test(lbl) ? rnd(-0.15, 0.15) : /Feedback/.test(lbl) ? rnd(-0.3, 0.3) : /Tone/.test(lbl) ? rnd(-0.5, 0.5) : /Pattern/.test(lbl) ? rnd(-0.5, 0.5)"))
SUBS.append(("P('Routing')", "P('Wiring')"))
SUBS.append(("p.n === 'Routing'", "p.n === 'Wiring'"))

# ---------------------------------------------------------------- header
replace_fn('buildHeader', r'''
function buildHeader() {
  const r = document.getElementById('routing'); r.innerHTML = '';
  const s1 = P('Engine 1'), s2 = P('Engine 2'), wp = P('Wiring'), sp = P('Parallel Split'), m2 = P('B Plays');
  const e1 = seg(s1, ['Echo', 'Juba']), e2 = seg(s2, ['Off', 'Echo', 'Juba']);
  e1.classList.add('sm'); e2.classList.add('sm');
  const wiring = h('div', { class: 'seg sm' });
  const wb = [['Series', 1], ['Parallel', 0], ['Spectral', 2]].map(([t, v]) => {
    const b = h('button', { type: 'button', text: { Series: 'Chain', Parallel: 'Side by side', Spectral: 'Split' }[t] });
    b.onclick = () => { setMany(v === 1 ? [[wp, 1]] : [[wp, 0], [sp, v === 2 ? 1 : 0]]); };
    tip(b, { Series: 'Chain', Parallel: 'Side by side', Spectral: 'Split' }[t], TIPS[t]); wiring.appendChild(b); return [b, v];
  });
  const paintW = () => { const ser = Math.round(wp.v) === 1, spec = !ser && sp && sp.v >= 0.5; wb.forEach(([b, v]) => b.classList.toggle('on', v === 1 ? ser : v === 2 ? spec : !ser && !spec)); };
  bind(wp, paintW); bind(sp, paintW);
  const one = seg(m2, ['Separate', 'Repeat A', 'Reverse A']); one.classList.add('sm'); one.style.marginLeft = '6px';
  // one engine: no Chain, and B plays its own / answers / mirrors; one Juba can't be split by band (one tap memory)
  const swap = () => { const two = Math.round(s2.v) > 0, oneJuba = !two && Math.round(s1.v) === 1; one.style.display = 'none'; pathLbl.textContent = 'Path'; [...wiring.children].forEach((b, i) => { b.disabled = (!two && i === 0) || (oneJuba && i === 2); b.title = oneJuba && i === 2 ? 'One Juba can’t be split by band: it has one tap memory' : ''; }); };
  const pathLbl = h('div', { class: 'lbl', text: 'Path', style: 'font-size:9px' });
  bind(s2, swap); bind(s1, swap);
  r.append(h('div', { class: 'col', style: 'gap:1px' }, h('div', { class: 'lbl', text: 'Engine 1', style: 'font-size:9px' }), tip(e1, 'Engine 1', TIPS.Slot1)),
    h('div', { class: 'col', style: 'gap:1px' }, h('div', { class: 'lbl', text: 'Engine 2', style: 'font-size:9px' }), tip(e2, 'Engine 2', TIPS.Slot2)),
    h('div', { class: 'col', style: 'gap:1px' }, pathLbl, h('div', { class: 'row', style: 'gap:0' }, wiring, tip(one, 'B plays', TIPS.PartB))));
  document.querySelectorAll('.specOnly').forEach((e) => e.remove());
  const hr = document.getElementById('hdrRight'); hr.innerHTML = '';
  const th = h('button', { type: 'button', class: 'dd', style: 'width:112px', 'aria-haspopup': 'listbox' }, h('span', { class: 't', text: (THEMES[S.themeName] || THEMES.porch).label }), h('span', { class: 'car', text: '▾' }));
  th.onclick = (e) => { e.stopPropagation(); const names = Object.keys(THEMES); openPop(th, names.map((n, i) => ({ label: THEMES[n].label, value: i })), names.indexOf(S.themeName), (i) => { applyTheme(names[i]); post('theme ' + names[i]); build(); }, { width: 160 }); };
  const mot = h('div', { class: 'seg sm' }), motions = ['Full', 'Calm', 'Still'];
  const mbs = motions.map((t) => { const b = h('button', { type: 'button', text: t, onclick: () => { S.motion = t; try { localStorage.setItem('gullay.motion', t); } catch (e) {} mbs.forEach((x) => x.classList.toggle('on', x.textContent === t)); } }); mot.appendChild(b); return b; });
  mbs.forEach((x) => x.classList.toggle('on', x.textContent === S.motion));
  S.motionSeg = tip(mot, 'The river', 'The water flowing behind the window, at the River’s pass length. Full flows, Calm flows slowly and faintly, Still holds it.');
  S.themeBtn = th; hr.append(tip(killButton(), 'Kill', TIPS.Kill));
  const lb = document.getElementById('limBox'); if (lb) { lb.innerHTML = ''; lb.append(tip(limiterToggle(), 'Limiter', TIPS.Limiter)); }
  const hd = document.getElementById('hdrDice'); hd.innerHTML = '';
  DICE.length = 0;
  hd.append(tip(dice(() => T.ink, 'Random: all three parts (time, feedback, tone, drive, pan)', () => [0, 1, 2].forEach((b) => randomStrip(mode() + ' ' + BAND[b] + ' '))), 'Random parts', TIPS.Dice));
  buildPresets();
  const nr = document.getElementById('navr'); nr.innerHTML = '';
  const mixBox = h('div', { class: 'hk' });
  const fillMix = () => { mixBox.replaceChildren(knob(P(mode() + ' Mix'), 'Mix', leanInk, 22)); S.navMix = KNOBS[KNOBS.length - 1]; S.navKnobs = [S.navDepth, S.navMix].filter(Boolean); };
  bind(wp, fillMix);
  const depthBox = h('div', { class: 'hk' }, knob(P('Mod Depth'), 'Mod depth', leanInk, 22)); S.navDepth = KNOBS[KNOBS.length - 1];
  nr.append(tip(depthBox, 'Mod depth', TIPS.ModDepth), tip(mixBox, 'Mix', TIPS.Mix));
  S.navKnobs = [S.navDepth, S.navMix].filter(Boolean); S.navKnobs.forEach((k) => k.draw());
}''')
SUBS.append(("const TIPS = {", r"""const TIPS = {
  Slot1: 'The first engine: Echo (the Dual Looping Delay: two delays, A and B, with INF loops and reverse) or Juba (echoes in a rhythm: 24 patterns or your own taps). With one engine, A and B are its two halves.',
  Slot2: 'A second engine. On: engine 1 plays A and engine 2 plays B, on a Path: Chain, Side by side or Split. Off: one engine, and B can play its own half, answer A or mirror it.',
  Kill: 'Empties every echo and loop at once (and the reverb tail): the way out of a runaway. Auto kill (Tone page, Runaway) does it for you when a runaway goes on past its time.',
  PartB: 'With one engine, what B plays. Separate: the engine’s own second delay (Echo’s delay B, or Juba’s late taps). Repeat A: B replays A’s echoes after a wait, on the beats Chance lets through. Reverse A: B plays A’s echoes backwards.',
  Echo: 'Time: semitones of time (+12 doubles it). A and B count beats (0 = one beat); Lead stretches both. Sync snaps to the DLD’s ratios.',"""))
SUBS.append(("  Parallel: 'Splits the sound into Low, Mid and High at the two split points. Each strip works on its own part, then the three are added back together.',",
             "  Parallel: 'Side by side: both engines hear your sound and play next to each other. Balance (Engines page) tips the level between them.',"))
SUBS.append(("  Series: 'The whole sound runs through Low, then Mid, then High, one after another. Series keeps its own settings, separate from Parallel.',",
             "  Series: 'Chain: engine 1’s echoes feed engine 2, so B echoes A’s echoes. Chain keeps its own path settings, separate from Side by side.',"))
SUBS.append(("  Spectral: 'Parallel strips fed by an FFT split instead of crossover filters: by frequency (steep, no phase shift) or Tonal (tones / hits / air).",
             "  Spectral: 'The two engines get different parts of your sound from an FFT split: by frequency (lows to slot 1, the rest to slot 2) or Tonal (tones to slot 1, hits and air to slot 2)."))
SUBS.append(("  Dice: 'Random for the three filters: type (sometimes), cutoff, resonance, morph and pan, in safe ranges. Press ↶ to go back.',",
             "  Dice: 'Random for the three parts: time (on musical ratios), feedback, tone, a little drive and pan, in safe ranges. Press ↶ to go back.',"))

# ---------------------------------------------------------------- the parts (were the strips)
replace_fn('stripSub', r'''
function stripSub(b) {
  const s1 = P('Engine 1') ? Math.round(P('Engine 1').v) : 0, s2 = P('Engine 2') ? Math.round(P('Engine 2').v) : 0, m2 = P('B Plays') ? Math.round(P('B Plays').v) : 0;
  const eng = (i) => ['DLD', 'Juba'][i];
  if (b === 1) return 'all the echoes';
  if (s2 > 0) return (b === 0 ? 'slot 1 · ' + eng(s1) : 'slot 2 · ' + eng(s2 - 1));
  if (b === 2 && m2 === 1) return 'answers A';
  if (b === 2 && m2 === 2) return 'A backwards';
  return s1 === 1 ? (b === 0 ? 'early taps' : 'late taps') : (b === 0 ? 'DLD channel A' : 'DLD channel B');
}''')
replace_fn('buildStrips', r'''
function buildStrips() {
  const box = document.getElementById('strips'); box.innerHTML = ''; KNOBS.length = 0;
  for (let i = DICE.length - 1; i >= 0; --i) if (DICE[i].closest && DICE[i].closest('#strips')) DICE.splice(i, 1);
  const m = mode(); S.partUi = [];
  for (let b = 0; b < 3; ++b) {
    const pre = m + ' ' + BAND[b] + ' ', col = () => bandColour(b);
    const el = h('section', { class: 'strip', style: `--c:${bandFill(b)};--ct:${textOn(bandFill(b))}` });
    if (b === 1) { el.dataset.midc = '--c'; el.dataset.midt = '--ct'; }
    const sub = h('div', { class: 'sub' }), secs = h('span', { class: 'secs' });
    const minis = h('div', { class: 'row', style: 'gap:4px' });
    if (b !== 1) minis.append(miniTog(P(pre + 'Solo'), 'S', 'Solo ' + BAND[b]));
    minis.append(miniTog(P(pre + 'Mute'), 'M', 'Mute ' + BAND[b]));
    const dc = dice(() => textOn(bandFill(b)) === '#FFFFFF' ? '#FFFFFF' : '#1F2E32', 'Random: ' + BAND[b] + ' (time, feedback, tone, drive, pan)', () => randomStrip(pre));
    dc.classList.add('ibtn'); dc.style.background = 'transparent'; dc.style.borderColor = 'transparent';
    const cp = clipButton('strip', b); cp.style.background = 'transparent'; cp.style.borderColor = 'transparent'; cp.style.color = 'var(--ct)';
    const lockTag = h('span', { class: 'locktag' });
    el.append(h('div', { class: 'top' }, h('h2', { text: BAND[b].toUpperCase() }), sub, lockTag, h('div', { class: 'sp' }), secs, minis, dc, cp));
    el.lockTag = lockTag; if (b === 1) S.midStrip = el;
    el.append(h('div', { class: 'row', style: 'justify-content:space-between;align-items:flex-end' },
      tip(knob(P(pre + 'Time'), b === 1 ? 'Stretch' : 'Time', col, 46), BAND[b] + ' time', TIPS.Echo),
      knob(P(pre + 'Feedback'), b === 1 ? 'Feedback ×' : 'Feedback', col, 40), knob(P(pre + 'Tone'), 'Tone', col, 38), knob(P(pre + 'Drive'), 'Drive', col, 38),
      knob(P(pre + 'Level'), 'Level', col, 38), knob(P(pre + 'Pan'), 'Pan', col, 38)));
    const rm = h('i'), meter = h('div', { class: 'rmeter', title: 'Runaway: how hard the ceiling is holding this part’s loop' }, rm);
    rm.style.background = bandFill(b); if (b === 1) rm.dataset.midc = 'background';
    const infName = b === 1 ? 'Hold All' : 'INF';
    el.append(h('div', { class: 'row', style: 'gap:6px' }, tip(toggle(P(pre + 'Time Sync'), 'Sync'), 'Sync', 'Snaps Time to the Dual Looping Delay’s ratios: 1/16 … 2/3, 1, 1.5, 2, 3 … 16 beats.'),
      tip(toggle(P(pre + infName), b === 1 ? 'Hold all' : 'INF'), 'INF', 'DLD: holds the loop and plays it round and round (feedback stops mattering). Juba: Repeat, the taps loop. Lead holds both.'),
      tip(toggle(P(pre + 'Reverse'), 'Rev'), 'Reverse', 'DLD: plays this part’s memory backwards.'),
      tip(meter, 'Runaway', 'How hard the ceiling is holding this part’s loop right now (Tone page).'), tip(h('div', { class: 'hk' }, knob(P(pre + 'Mod Depth'), 'Mod', col, 22)), BAND[b] + ' mod depth', TIPS.StripMod)));
    el.sub = sub; S.partUi.push({ secs, rm }); box.appendChild(el);
  }
  refreshSubs();
}''')
SUBS.append(("  StripMod: 'How much the movement (Sway, Chaperone, Mutation, lanes, LFOs, matrix) moves this strip: 0% holds it still, 200% doubles it. Its P-locks stay as drawn.'",
             "  StripMod: 'How much the movement (Sway, Chaperone, Mutation, lanes, LFOs, matrix, River) moves this part: 0% holds it still, 200% doubles it. Its P-locks stay as drawn.'"))
SUBS.append(("      if (m.name === 'Z-Plane') { const pre = fp.n.replace('Filter Type', ''); ch.push([P(pre + 'Cutoff'), 14.2131], [P(pre + 'Morph'), 0]); }",
             "      if (m.name === 'Z-Plane') { const pre = fp.n.replace('Type', ''); ch.push([P(pre + 'Cutoff'), 14.2131], [P(pre + 'Morph'), 0]); }"))

# ---------------------------------------------------------------- pages
replace_line_starting("const PAGES = [", "const PAGES = [['engines', 'Engines'], ['river', 'River'], ['sway', 'Sway'], ['conductor', 'Black Herman'], ['mutation', 'Mutation'], ['snaps', 'Snapshots'], ['lfos', 'LFOs'], ['mod', 'Mod'], ['tune', 'Hymn'], ['midi', 'MIDI']];")
replace_fn('showPage', r'''
function showPage() {
  if (S.page === 'spectral' && !spectralOn()) S.page = 'engines';
  if (!PAGES.some(([id]) => id === S.page)) S.page = 'engines';
  const st = document.querySelector('#tabs button[data-page="spectral"]'); if (st) st.style.display = spectralOn() ? '' : 'none';
  document.querySelectorAll('#tabs button').forEach((b) => { b.classList.toggle('on', b.dataset.page === S.page); b.setAttribute('aria-pressed', b.dataset.page === S.page); });
  document.querySelectorAll('.page').forEach((p) => p.classList.toggle('on', p.dataset.page === S.page));
  post('view ' + (S.page === 'tone' ? 1 : 0));
  if (S.page === 'lfos') drawCurve();
  if (S.page === 'midi') post('maps');
}''')
replace_fn('buildPages', r'''
function buildPages() {
  const keep = KNOBS.filter((k) => k.el.closest('#strips') || k.el.closest('nav')); KNOBS.length = 0; keep.forEach((k) => KNOBS.push(k));
  const box = document.getElementById('pages'); box.innerHTML = '';
  const add = (id, el) => { el.classList.add('page'); el.dataset.page = id; box.appendChild(el); };
  add('engines', pageEngines()); add('tune', pageHymn()); add('river', pageRiver());
  add('sway', pageSway()); add('conductor', pageConductor()); add('mutation', pageMutation()); add('snaps', pageSnaps()); add('lfos', pageLfos());
  add('mod', pageMod()); add('midi', pageMidi());
  showPage(); paintMid();
}''')
SUBS.append(("            h('div', { style: 'display:grid;grid-template-columns:repeat(3,auto);gap:4px 12px' }, ...['Cutoff', 'Reso', 'Morph', 'Drive', 'Level', 'Pan'].map(",
             "            h('div', { style: 'display:grid;grid-template-columns:repeat(3,auto);gap:4px 12px' }, ...['Time', 'Feedback', 'Tone', 'Drive', 'Level', 'Pan'].map("))
SUBS.append(("...(tonal ? [] : [knob(P('Par Split Low/Mid'), 'Low/Mid', ink, 46), knob(P('Par Split Mid/High'), 'Mid/High', ink, 46)]),", ""))
SUBS.append(("const MOVES = ['Cutoff', 'Reso', 'Drive', 'Level', 'Pan']", "const MOVES = ['Time', 'Feedback', 'Drive', 'Level', 'Pan', 'Tone', 'Juba Pattern', 'Filter', 'Crusher Bits', 'Hymn', 'Shift', 'Degrade', 'Tilt', 'Spectral Band']"))
SUBS.append(("'Mutation Low ", "'Mutation A "))
SUBS.append(("'Mutation High ", "'Mutation B "))
SUBS.append((" · oversample ' + lab(P('Oversample')) + '", ""))
SUBS.append(("return 'Three’s Company ' + (i.version || '')", "return 'Gullay ' + (i.version || '')"))
SUBS.append(("routing ' + (spectralOn() ? 'Spectral' : mode() === 'Par' ? 'Parallel' : 'Series')", "wiring ' + (spectralOn() ? 'Spectral' : mode() === 'Par' ? 'Parallel' : 'Series') + ' · slots ' + lab(P('Engine 1')) + ' / ' + lab(P('Engine 2'))"))
SUBS.append(("' · ' + (S.dark ? 'dark' : 'light') + ' · '", "' · ' + S.themeName + ' · '"))
for name in ['drawGraph', 'bankBars', 'rippleRow', 'pageBank', 'pageMix', 'bankResp', 'bankActivity', 'drawScope']:
    delete_fn(name)
replace_fn('buildViewSwitch', 'function buildViewSwitch() {}')
replace_block('// drag a split marker on the graph', '})();', '')
replace_line_starting('const ZOOMS = [', 'const ZOOMS = [0.75, 0.9, 1, 1.25, 1.5];')

# settings popover
SUBS.append(("h('b', { text: 'Three’s Company ' + ((S.info && S.info.version) || '') }), h('br'), 'by Otis Kanno', h('br'),", "h('b', { text: 'Gullay ' + ((S.info && S.info.version) || '') }), h('br'), 'by Otis Kanno · first test build', h('br'),"))
SUBS.append(("h('span', { class: 'hint', text: 'Free software (GPL v3): anyone you share it with can get its source code. Filters: Surge sst-filters. Started from Two Filters by BaconPaul.' })),",
             "h('span', { class: 'hint', text: 'Free software (GPL v3). Delays ported from the 4ms Dual Looping Delay and Juba Delay; reverbs from Valley’s Plateau (Dattorro) and Cloud Seed; filters from Surge sst-filters; modulation from Three’s Company.' })),"))
SUBS.append(("row(h('span', { text: 'Theme' }), choice([['Light', 0], ['Dark', 1]], S.prefs.dark ? 1 : 0, (v) => { S.prefs.dark = v; savePrefs(); if (!!v !== S.dark) { applyTheme(!!v); post('theme ' + (v ? 'dark' : 'light')); build(); box.remove(); toggleSettings()",
             "row(h('span', { text: 'Light or dark' }), choice([['Light', 0], ['Dark', 1]], S.prefs.dark ? 1 : 0, (v) => { S.prefs.dark = v; savePrefs(); if (!!v !== S.dark) { const pair = /^porch/.test(S.themeName) || !/^(light|dark)$/.test(S.themeName) ? ['porch', 'porchdark'] : ['light', 'dark']; applyTheme(pair[v ? 1 : 0]); post('theme ' + pair[v ? 1 : 0]); build(); box.remove(); toggleSettings()"))
SUBS.append(("choice([['80%', 80], ['100%', 100], ['125%', 125], ['150%', 150]], S.prefs.size,", "choice([['75%', 75], ['90%', 90], ['100%', 100], ['125%', 125], ['150%', 150]], S.prefs.size,"))

# ---------------------------------------------------------------- reacting to changes
replace_fn('afterChange', r'''
function afterChange(p) {
  if (!built) return;
  if (p.n === 'Wiring' || p.n === 'Parallel Split' || p.n === 'Spectral Split By' || p.n === 'Engine 1' || p.n === 'Engine 2' || p.n === 'B Plays') { refreshSpecBox(); onRoutingChange(); return; }
  if (/^LFO \d Shape$/.test(p.n)) refreshLfo();
  if (/ Filter Type$/.test(p.n) && S.fillFilters) S.fillFilters();
  if (/^Spectral /.test(p.n)) drawGraph();
  if (/^Lock .* Steps \d+$/.test(p.n) || /^Lock .* (Time|Feedback|Tone|Drive|Level|Pan) \d+$/.test(p.n)) markHeld();
  if (p.n === 'Chaperone Lean') { paintMid(); KNOBS.forEach((k) => k.draw()); }
}''')
replace_fn('onRoutingChange', 'function onRoutingChange() { buildStrips(); buildPages(); paintMid(); refreshSubs(); }')
SUBS.append(("  if (S.cutAt.some((t) => performance.now() - t < 1000) && S.view !== 2) drawGraph();\n", "  gyLive(u);\n"))
SUBS.append(("""  document.querySelectorAll('.strip').forEach((el, b) => {
    if (el.edge) el.edge.style.background = (u.edge && u.edge[b] > 0.05) ? bandFill(b) : '';
    if (el.guard) el.guard.style.width = Math.round(Math.min(1, Math.max(0, 1 - (u.guard ? u.guard[b] : 1))) * 100) + '%';
  });
""", ""))
SUBS.append(("s <= 6 ? u.lfo[s - 4] : u.env;", "s <= 6 ? u.lfo[s - 4] : s === 7 ? u.env : (u.gy ? u.gy.river[s - 8] : 0);"))

out_extra = open('/tmp/gui/new.js').read()
SUBS.append(("// size steps, like the - / + on Vulf", out_extra + "\n// size steps, like the - / + on Vulf"))

# smaller knobs on the Hymn (Tune) page so the Shift card fits beside it
a, b = find_fn('pageTune')
for k in range(a, b + 1):
    L[k] = L[k].replace(", ink, 50)", ", ink, 40)").replace(", leanInk, 50)", ", leanInk, 40)")

SUBS.append(("  setTimeout(() => { box.appendChild(histTip(ub, 'Undo',", "  const tok = S.presetTok = (S.presetTok || 0) + 1;\n  setTimeout(() => { if (tok !== S.presetTok) return; box.appendChild(histTip(ub, 'Undo',"))

# the Spectral page: the split and the three bands
replace_fn('pageSpectral', r"""
function pageSpectral() {
  const box = h('div', {});
  if (!P('Spectral Split By')) return box;
  const tonal = tonalOn();
  const split = card('flex:1.3', h('div', { class: 'row' }, h('h2', { class: 'hd', text: 'SPLIT', style: 'margin:0;font-size:15px' }), h('div', { class: 'sp' }), seg(P('Spectral Split By'), ['Freq', 'Tonal'])),
    h('div', { class: 'hint', text: tonal ? 'Tonal: steady tones go to A (slot 1), hits to both (Lead), the noisy air to B (slot 2).' : 'Freq: lows go to A (slot 1), mids to both (Lead), highs to B (slot 2). Drag the split lines in the display above.' }),
    h('div', { class: 'row', style: 'gap:14px;align-items:flex-end' }, ...(tonal ? [] : [knob(P('Spectral Low/Mid'), 'Low/Mid', ink, 46), knob(P('Spectral Mid/High'), 'Mid/High', ink, 46)]),
      knob(P('Spectral Balance'), 'Balance', ink, 40), knob(P('Spectral Sharpness'), 'Sharpness', ink, 40)));
  const bands = card('width:330px;flex:none', h('h2', { class: 'hd', text: 'BANDS', style: 'margin:0;font-size:15px' }),
    h('div', { class: 'row', style: 'gap:14px;align-items:flex-end' }, knob(P('Spectral Low Level'), tonal ? 'Tones' : 'Low', () => bandColour(0), 46), knob(P('Spectral Mid Level'), tonal ? 'Hits' : 'Mid', () => bandColour(1), 46), knob(P('Spectral High Level'), tonal ? 'Air' : 'High', () => bandColour(2), 46)),
    h('div', { class: 'hint', text: 'Every band level and both split points are mod targets: right-click a knob to send an LFO, a lane or the River to it.' }));
  const q = card('width:260px;flex:none', h('div', { class: 'row' }, h('h2', { class: 'hd', text: 'FFT QUALITY', style: 'margin:0;font-size:15px' }), h('div', { class: 'sp' }), sel(P('Spectral Quality'), 100)),
    h('div', { class: 'hint', text: 'Bigger frames split bass more cleanly but smear hits and add latency (Normal: 43 ms, which Bitwig compensates).' }));
  box.append(split, bands, q);
  return box;
}""")
SUBS.append(("  post('view ' + (S.page === 'tone' ? 1 : 0));", "  post('view ' + (S.page === 'tone' || spectralOn() ? 1 : 0));"))

# preset ‹ ›: step through the factory list in the order the menu shows it
SUBS.append(("  box.appendChild(tip(b, 'Presets', 'Factory presets, grouped by what they do.", """  const order = () => { const byName = (x, y) => x.label.localeCompare(y.label), o = [];
    (groups.Start || []).forEach((x) => o.push(x.value));
    Object.keys(groups).filter((g) => g !== 'Start').sort().forEach((g) => groups[g].slice().sort(byName).forEach((x) => o.push(x.value)));
    return o; };
  const stepPreset = (d) => { const o = order(); if (!o.length) return; const cur = list.findIndex(([n]) => n === S.presetName); let k = o.indexOf(cur); k = k < 0 ? (d > 0 ? 0 : o.length - 1) : (k + d + o.length) % o.length;
    const v = o[k]; histCommit(); H.expectUntil = performance.now() + 1500; S.presetName = list[v][0]; t.textContent = S.presetName; H.label = 'load ' + S.presetName; post('preset ' + v); toast('Loaded ' + S.presetName); };
  const prevB = h('button', { type: 'button', class: 'btn hbtn', text: '‹', 'aria-label': 'Previous preset', style: 'height:28px;width:26px;padding:0;margin-right:3px', onclick: (e) => { e.stopPropagation(); stepPreset(-1); } });
  const nextB = h('button', { type: 'button', class: 'btn hbtn', text: '›', 'aria-label': 'Next preset', style: 'height:28px;width:26px;padding:0;margin-left:3px', onclick: (e) => { e.stopPropagation(); stepPreset(1); } });
  box.appendChild(prevB);
  box.appendChild(tip(b, 'Presets', 'Factory presets, grouped by what they do."""))
SUBS.append(("Picking one replaces every setting; ↶ takes you back.'));", "Picking one replaces every setting; ↶ takes you back.'));\n  box.appendChild(nextB);"))

SUBS.append((".pdd{width:190px;flex:none}", ".pdd{width:160px;flex:none}"))

# the river shows through the panels (they are a little see-through)
SUBS.append(("#app{position:relative;z-index:1}", "#app{position:relative;z-index:1}\n.panel,.page{background:color-mix(in srgb,var(--panel) 72%,transparent)!important}\n.strip{background:color-mix(in srgb,var(--panel) 80%,transparent)}\n.card{background:color-mix(in srgb,var(--card) 84%,transparent)}"))

SUBS.append(("    row(h('span', { text: 'Hover tips' }),", "    row(h('span', { text: 'River motion' }), S.motionSeg || h('span')),\n    row(h('span', { text: 'Hover tips' }),"))

# P-locks: a step's locks last for its length (Elektron style)
SUBS.append(("  KNOBS.forEach((k) => k.draw()); refreshLockCells();\n}\ndocument.addEventListener('keydown', (e) => { if (e.key === 'Escape' && S.lockSel && !popFor) selectLock(null); });",
             "  KNOBS.forEach((k) => k.draw()); refreshLockCells(); if (S.fillLockLen) S.fillLockLen();\n}\ndocument.addEventListener('keydown', (e) => { if (e.key === 'Escape' && S.lockSel && !popFor) selectLock(null); });"))
SUBS.append(("    c.el.title = 'Step ' + (i + 1) + (ks.length ? ': ' + ks.map((k) => PLN[k] + ' ' + fmtLock(k, lockP(b, k, i).v)).join(', ') : ': no locks') + '. Click to select, right-click to copy or paste.';\n  }));\n}",
             "    c.el.title = 'Step ' + (i + 1) + (ks.length ? ': ' + ks.map((k) => PLN[k] + ' ' + fmtLock(k, lockP(b, k, i).v)).join(', ') : ': no locks') + '. Click to select, right-click to copy or paste.';\n  }));\n  markHeld();\n}"))
SUBS.append(("    h('span', { class: 'hint', text: 'Click a step, then turn that band’s knobs. Double-click a knob to clear its lock; Esc when done. Right-click a step to copy, paste or clear its params.' }),",
             "    h('span', { class: 'hint', text: 'Click a step, turn that part’s knobs. Lasts: how many steps the step’s locks hold (time changes need room to settle).' }), lockLenBox(),"))
SUBS.append(("const PL_SHORT = ['Cut', 'Res', 'Mor', 'Drv', 'Lvl', 'Pan'];", "const PL_SHORT = ['Time', 'Fb', 'Tone', 'Drv', 'Lvl', 'Pan'];"))
# Chaperone: lock-in
SUBS.append(("      dice(midLine, 'Random: the Chaperone’s timing, chaos, chance, bars, bursts, third wheel, snap and seed', randomConductor)),",
             "      lockInBox(), dice(midLine, 'Random: the Chaperone’s timing, chaos, chance, bars, bursts, third wheel, snap and seed', randomConductor)),"))
SUBS.append(("h('span', { class: 'hint', text: 'Sporadic, but always in time.' }), h('div', { class: 'sp' }),", "h('div', { class: 'sp' }),"))
# Mod page: the source depths
SUBS.append(("  return h('div', { style: 'flex-direction:column;gap:8px' }, h('div', { class: 'row' }, h('h2', { class: 'hd', text: 'MOD', style: 'margin:0;font-size:15px' }), h('div', { class: 'sp' }),",
             "  return h('div', { style: 'flex-direction:column;gap:8px' }, depthRow(), h('div', { class: 'row' }, h('h2', { class: 'hd', text: 'MOD', style: 'margin:0;font-size:15px' }), h('div', { class: 'sp' }),"))
SUBS.append(("row-gap:4px;flex:1;align-content:space-evenly' });\n  for (let k = 0; k < 16; ++k) {", "row-gap:2px;flex:1;align-content:space-evenly' });\n  for (let k = 0; k < 16; ++k) {"))

SUBS.append(("#app{position:relative;z-index:1}", "#app{position:relative;z-index:1}\n.btn.warn{background:#D9534F!important;color:#fff!important;border-color:#D9534F!important;animation:gywarn 0.9s ease-in-out infinite}\n@keyframes gywarn{50%{opacity:.55}}\n.tog.live{box-shadow:0 0 0 2px var(--mid)}\n.steps .st.held{background-image:repeating-linear-gradient(135deg,var(--off) 0 4px,transparent 4px 8px)}"))

SUBS.append(("labelled('Low every', stepper(P('Chaperone Low Every'))), labelled('High every', stepper(P('Chaperone High Every')))", "labelled('A every', stepper(P('Chaperone A Every'))), labelled('B every', stepper(P('Chaperone B Every')))"))
SUBS.append(("[P('Chaperone Low Every'), pick([1, 1, 2, 3, 4, 6, 8])], [P('Chaperone High Every'), pick([1, 1, 2, 3, 4, 6, 8])]", "[P('Chaperone A Every'), pick([1, 1, 2, 3, 4, 6, 8])], [P('Chaperone B Every'), pick([1, 1, 2, 3, 4, 6, 8])]"))

SUBS.append(("    el.sub = sub; S.partUi.push({ secs, rm }); box.appendChild(el);", "    el.append(stripFilter(b));\n    el.sub = sub; S.partUi.push({ secs, rm }); box.appendChild(el);"))
SUBS.append(("'How hard the ceiling is holding this part’s loop right now (Tone page).'", "'How hard the ceiling is holding this part’s loop right now. Kill (in the header) empties every loop; Auto Kill does it after 30 s of runaway.'"))
SUBS.append(("text-decoration-color:var(--mid,#E0A000)", "text-decoration-color:var(--mid)"))

SUBS.append(("'Snaps Time to the Dual Looping Delay’s ratios: 1/16 … 2/3, 1, 1.5, 2, 3 … 16 beats.'", "'Snaps Time to note values: straight, dotted (D) and triplet (T), from 1/64 to 4 bars. On Lead: musical stretches (x1/2, x2/3, x3/4, x1, x1.5, x2 …).'"))
SUBS.append(("Lead stretches both. Sync snaps to the DLD’s ratios.'", "Lead stretches both. Sync snaps to note values (1/4, 1/8D, 1/4T …).'"))

# v0.4: Hymn's Motion is Wave (its middle choice Swell)
SUBS.append(("'Tune Motion", "'Tune Wave"))
SUBS.append(("text: 'Motion ⓘ' }), 'Motion', motionTip)", "text: 'Wave ⓘ' }), 'Wave', motionTip)"))
SUBS.append(("seg(P('Tune Wave'))), 'Motion', motionTip)", "seg(P('Tune Wave'))), 'Wave', motionTip)"))
# the footer's Snapshot is a bug report now (Snapshots are the sequencer's)
SUBS.append(("id=\"snapBtn\">✎ Snapshot<", "id=\"snapBtn\">✎ Bug Report<"))
SUBS.append(("'Snapshot: every setting and your note, saved as a text file (the folder opens).'", "'Bug report: every setting and your note about what went wrong, saved as a text file to send in (the folder opens).'"))
SUBS.append(("text: 'Save snapshot', onclick:", "text: 'Save bug report', onclick:"))
# Settings: Auto Kill lives here now
SUBS.append(("    row(h('span', { text: 'River motion' }), S.motionSeg || h('span')),", "    row(h('span', { text: 'River motion' }), S.motionSeg || h('span')),\n    row(tip(h('span', { text: 'Auto Kill' }), 'Auto Kill', 'Empties every loop once a runaway has run this long. Kill in the header does it at once; it pulses red after 10 s.'), h('div', { class: 'row', style: 'gap:6px' }, toggle(P('Auto Kill'), 'On'), stepper(P('Auto Kill After')), h('span', { class: 'hint', text: 's' }))),"))
SUBS.append(("reverbs from Valley’s Plateau (Dattorro) and Cloud Seed; ", ""))
# dropdown options that are gone: Seq and Follower sources, the Follower trigger, Tilt as an answer
SUBS.append(("      let items = p.lab.map((l, i) => ({ label: l, value: i }));", "      let items = p.lab.map((l, i) => ({ label: l, value: i })).filter((it) => !hiddenOption(p, it.value));"))

# quick modulation: after "Modulate with ▸ source", dragging the knob sets that route's depth; Alt-drag edits an existing one
SUBS.append(("  el.addEventListener('pointerdown', (e) => {\n    if (e.button !== 0) return;\n    el.setPointerCapture(e.pointerId); y0 = e.clientY;",
             "  el.addEventListener('pointerdown', (e) => {\n    if (e.button !== 0) return;\n    if (e.altKey && !S.modDrag) { const tg = targetIndexFor(p); for (let i = 1; i <= 16 && tg > 0; ++i) if (Math.round(P('Mod ' + i + ' Target').v) === tg && Math.round(P('Mod ' + i + ' Source').v) > 0) { S.modDrag = { p, el, amt: P('Mod ' + i + ' Amount'), src: Math.round(P('Mod ' + i + ' Source').v) }; break; } }\n    if (S.modDrag && S.modDrag.p === p) { const a = S.modDrag.amt; el.setPointerCapture(e.pointerId); y0 = e.clientY; dragP = a; v0 = a.v; S.modDrag.drag = true; gesture(a, true); e.preventDefault(); return; }\n    el.setPointerCapture(e.pointerId); y0 = e.clientY;"))
SUBS.append(("    const range = dragP === p ? p.max - p.min : 1, dv = (y0 - e.clientY) / (e.shiftKey ? 900 : 180) * range;",
             "    if (S.modDrag && dragP === S.modDrag.amt) { setP(dragP, Math.max(-1, Math.min(1, v0 + (y0 - e.clientY) / (e.shiftKey ? 600 : 120)))); k.draw(); return; }\n    const range = dragP === p ? p.max - p.min : 1, dv = (y0 - e.clientY) / (e.shiftKey ? 900 : 180) * range;"))
SUBS.append(("  el.addEventListener('pointerup', (e) => { if (el.hasPointerCapture(e.pointerId)) { el.releasePointerCapture(e.pointerId); gesture(dragP, false); } });",
             "  el.addEventListener('pointerup', (e) => { if (el.hasPointerCapture(e.pointerId)) { el.releasePointerCapture(e.pointerId); gesture(dragP, false);\n    if (S.modDrag && dragP === S.modDrag.amt) { const a = S.modDrag.amt; toast(SOURCES[S.modDrag.src - 1] + ' moves ' + p.n + ' by ' + (a.v >= 0 ? '+' : '') + Math.round(a.v * 100) + '% (edit it on the Mod page)'); S.modDrag = null; el.classList.remove('moddrag'); dragP = p; k.draw(); } } });"))
SUBS.append(("    kv.textContent = lk ? (lk.v >= 0 ? '🔒 ' + fmtLock(lockInfo.k, lk.v) : 'no lock') : p.t; el.setAttribute('aria-valuetext', kv.textContent);",
             "    kv.textContent = lk ? (lk.v >= 0 ? '🔒 ' + fmtLock(lockInfo.k, lk.v) : 'no lock') : p.t; el.setAttribute('aria-valuetext', kv.textContent);\n    if (S.modDrag && S.modDrag.p === p) { kv.textContent = 'depth ' + (S.modDrag.amt.v >= 0 ? '+' : '') + Math.round(S.modDrag.amt.v * 100) + '%'; el.classList.add('moddrag'); }"))
SUBS.append(("    SOURCES.forEach((src, i) => items.push({ label: src, value: 'm' + (i + 1), note: mine.some(([sp]) => Math.round(sp.v) === i + 1) ? 'on' : '' }));",
             "    SOURCES.forEach((src, i) => { if (true) items.push({ label: src + ' ▸', value: 'm' + (i + 1), note: mine.some(([sp]) => Math.round(sp.v) === i + 1) ? 'on' : '' }); });\n    if (mine.length) items.push({ label: 'Alt-drag the knob to change a route’s depth', value: 'none', disabled: true });"))
SUBS.append(("      const src = +v.slice(1), free = slots.find(([sp]) => Math.round(sp.v) === 0);\n      if (!free) { toast('All 16 mod slots are in use: free one on the Mod page'); return; }\n      setMany([[free[0], src], [free[1], t], [free[2], 0.5]]); toast(SOURCES[src - 1] + ' → ' + P('Mod 1 Target').lab[t] + ' at 50% (fine-tune it on the Mod page)');",
             "      // the first free slot of 1-12 (13-16 are the River's sends); then drag the knob to set how far\n      const src = +v.slice(1), free = slots.slice(0, 12).find(([sp]) => Math.round(sp.v) === 0);\n      if (!free) { toast('Mod slots 1–12 are all in use: free one on the Mod page'); return; }\n      setMany([[free[0], src], [free[1], t], [free[2], 0]]); S.modDrag = { p, el: anchor, amt: free[2], src }; if (k) k.draw();\n      toast('Now drag ' + p.n + ' up or down: how far ' + SOURCES[src - 1] + ' moves it (Shift: fine)');"))
# the split panel sits on the right of the echo map now
SUBS.append(("  const fOf = (e) => { const r = cv.getBoundingClientRect(); return 20 * Math.pow(1000, (e.clientX - r.left) / r.width); };",
             "  const fOf = (e) => { const r = cv.getBoundingClientRect(), x0 = r.left + r.width * SPLIT_AT, w = r.width * (1 - SPLIT_AT); return 20 * Math.pow(1000, Math.max(0, (e.clientX - x0) / w)); };"))
SUBS.append(("    if (!S.ready || !spectralOn() || tonalOn()) return; const f = fOf(e);", "    if (!S.ready || !spectralOn() || tonalOn()) return; { const r = cv.getBoundingClientRect(); if (e.clientX < r.left + r.width * SPLIT_AT) return; } const f = fOf(e);"))
# Lean: say it while turning
SUBS.append(("  if (/^Spectral /.test(p.n)) drawGraph();", "  if (/^Spectral /.test(p.n)) drawGraph();\n  if (p.n === 'Chaperone Lean') { const d = Math.round((p.v - 0.5) * 200); toast('Lean ' + (d === 0 ? 'centred' : Math.abs(d) + ' % toward ' + (d < 0 ? 'A' : 'B'))); }"))
# styles
SUBS.append(("#app{position:relative;z-index:1}", "#app{position:relative;z-index:1}\n.knob.moddrag{outline:2px dashed var(--mid);outline-offset:2px;border-radius:8px}\n.snapstep{height:42px;border:1px solid var(--line);border-radius:6px;background:linear-gradient(to top,var(--off) var(--ch,100%),var(--card) var(--ch,100%));font-weight:800;font-size:12px;color:var(--ink);cursor:pointer}\n.snapstep.off{opacity:.3}.snapstep.sel{border-color:var(--ink);border-width:2px}.snapstep.now{background:var(--mid);color:var(--tabt)}\n.btn.snapslot{opacity:.55}.btn.snapslot.full{opacity:1;border-color:var(--ink)}.btn.snapslot.playing{background:var(--mid);color:var(--tabt)}"))

# v0.5: a calmer header, Theme in Settings, Limiter in the footer, dark readability, your presets last
SUBS.append(('  <header>\n    <div class="row" style="gap:16px">\n      <h1><span class="vname">Gullay</span></h1>\n      <div class="row" id="routing" style="gap:6px"></div>\n      <div id="hdrDice"></div>\n      <div id="presetBox"></div>\n    </div>\n    <div class="row" id="hdrRight"></div>\n  </header>', '  <header>\n    <div class="row" style="gap:14px;flex:1 0 auto">\n      <h1><span class="vname">Gullay</span></h1>\n      <div class="hgroup"><div class="row" id="routing" style="gap:8px"></div><div id="hdrDice"></div></div>\n    </div>\n    <div id="presetBox" class="hcentre"></div>\n    <div class="row" id="hdrRight" style="flex:1 0 auto;justify-content:flex-end;margin-left:12px"></div>\n  </header>'))
SUBS.append(('<span class="fh">Limiter</span><span class="lim" id="limTxt">0.0 dB</span></footer>', '<span id="limBox"></span><span class="lim" id="limTxt">0.0 dB</span></footer>'))
SUBS.append(("    row(h('span', { text: 'Window size' }),", "    row(h('span', { text: 'Theme' }), themePicker()),\n    row(h('span', { text: 'Window size' }),"))
SUBS.append(('#app{position:relative;z-index:1}', '#app{position:relative;z-index:1}\n.hgroup{display:flex;align-items:center;gap:10px;padding:3px 6px 3px 10px;border:1px solid var(--line);border-radius:10px;background:color-mix(in srgb,var(--card) 70%,transparent)}\n.hcentre{display:flex;align-items:center;gap:6px;padding:3px 8px;border-radius:10px;background:color-mix(in srgb,var(--card) 85%,transparent);border:1px solid var(--line)}\nfooter #limBox .tog{height:20px;font-size:11px;padding:0 8px}\n[data-theme=dark] nav{background:color-mix(in srgb,var(--panel) 92%,transparent);border-radius:8px}\n[data-theme=dark] nav .tabs button{color:var(--ink)}\n[data-theme=dark] .kl{color:color-mix(in srgb,var(--ink) 82%,transparent)}\n[data-theme=dark] .lbl,[data-theme=dark] .hint{color:var(--dim)}\n[data-theme=dark] [style*="opacity: 0.35"]{opacity:.6!important}\n[data-theme=dark] .seg button:not(.on){color:color-mix(in srgb,var(--ink) 80%,transparent)}\n[data-theme=dark] .dd,[data-theme=dark] .fbtn,[data-theme=dark] .btn{color:var(--ink)}'))
SUBS.append(("    cols.push([{ head: 'Yours' }].concat(yours.length ? yours : [{ label: 'Nothing saved yet: use Save', value: 'none', disabled: true }], [{ sep: true }, { label: '＋ Save the current sound…', value: 'save' }]));", "    const yoursCol = [{ head: 'Yours' }].concat(yours.length ? yours : [{ label: 'Nothing saved yet: use Save', value: 'none', disabled: true }], [{ sep: true }, { label: '＋ Save the current sound…', value: 'save' }]);"))
SUBS.append(('    if (cur.length) cols.push(cur);\n    openPop(b, null,', '    if (cur.length) cols.push(cur);\n    cols.push(yoursCol); // your own presets last\n    openPop(b, null,'))

SUBS.append(("  if (/^Spectral /.test(p.n)) drawGraph();", "  if (/^Spectral /.test(p.n)) drawGraph();\n  if (S.snapParamChanged) S.snapParamChanged(p);"))
SUBS.append(("#app{position:relative;z-index:1}", "#app{position:relative;z-index:1}\n.presetshelf{flex:1;min-height:0;max-height:118px;overflow:auto;display:flex;flex-wrap:wrap;gap:3px;align-content:flex-start;padding-right:4px}\n.presetshelf .lbl{width:100%}\n.pshelf{font-size:11px;padding:2px 7px;border:1px solid var(--line);border-radius:10px;background:var(--card);cursor:grab;color:var(--ink)}\n.snapstep.blank{color:var(--dim);font-weight:600}.snapstep.drop{outline:2px dashed var(--mid)}\n.btn.snapslot.loaded{box-shadow:0 0 0 2px var(--mid)}\ninput.chance{width:100%;height:14px;margin:0;accent-color:var(--mid)}\n.chpc{font-size:9.5px;text-align:center;color:var(--dim);font-variant-numeric:tabular-nums}"))

SUBS.append(("let T = THEMES.light; T.inkA", "let T = THEMES.porch; T.inkA"))

SUBS.append(("#app{position:relative;z-index:1}", "#app{position:relative;z-index:1}\n.pat button.patcard{height:30px;min-width:0;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:1px;padding:2px 2px}\n.patcard .pl{font-size:11px;font-weight:800;line-height:1}.patcard .pw{font-size:8.5px;font-weight:600;opacity:.75;line-height:1;letter-spacing:.03em}\n.patcard.empty:not(.on){opacity:.55;border-style:dashed}\n.patcard canvas{width:100%;max-width:56px;height:11px}\n.pat{grid-template-columns:repeat(6,minmax(0,1fr))!important}"))

SUBS.append(("#app{position:relative;z-index:1}", "#app{position:relative;z-index:1}\nheader{height:46px!important}\n.hgroup{padding:4px 10px 4px 14px;gap:14px}\n.hgroup #routing{gap:16px!important}\n.hgroup .seg button{height:26px;padding:0 12px;font-size:12px}\n.hgroup .lbl{font-size:10px!important;margin-bottom:1px}\n.hcentre{background:none;border:none;padding:0;gap:6px}\n.pdd{border-radius:999px!important;height:30px;width:200px!important;padding:0 14px!important}\n.pdd .lbl{display:none}\n.hcentre .btn{border-radius:999px}\n#presetRand{height:30px;width:34px;padding:0;border-radius:999px;display:flex;align-items:center;justify-content:center}\nnav{background:color-mix(in srgb,var(--panel) 90%,transparent);border-radius:8px;position:relative;z-index:2}\n.lbl,.kl,.hint{text-shadow:0 0 3px var(--bg),0 0 1px var(--bg)}\n.card .lbl,.card .kl,.card .hint{text-shadow:none}\n[data-theme=dark] .hint{color:color-mix(in srgb,var(--ink) 82%,transparent)!important}"))

SUBS.append(("    h('div', { class: 'row', style: 'gap:14px' }, knob(P('Tune Amount'), 'Amount', leanInk, 40), knob(P('Tune Ring Time'), 'Ring time', ink, 40), knob(P('Tune Lowest'), 'Lowest', ink, 40), knob(P('Tune Highest'), 'Highest', ink, 40),", "    h('div', { class: 'row', style: 'gap:4px;flex-wrap:nowrap' }, knob(P('Tune Amount'), 'Amount', leanInk, 38), knob(P('Tune Ring Time'), 'Ring time', ink, 38),"))
SUBS.append(("      col('Place', tip(seg(P('Tune Place')),", "      knob(P('Tune Lowest'), 'Lowest', ink, 24), knob(P('Tune Highest'), 'Highest', ink, 24),\n      col('Place', tip(seg(P('Tune Place')),"))
SUBS.append(("tip(knob(P('Tune Tilt'), 'Tilt', ink, 40)", "tip(knob(P('Tune Tilt'), 'Tilt', ink, 38)"))
SUBS.append(("knob(P('Tune Detune'), 'Detune', ink, 40), knob(P('Tune Width'), 'Width', ink, 40),", "knob(P('Tune Detune'), 'Detune', ink, 38), knob(P('Tune Width'), 'Width', ink, 38),"))
SUBS.append(("tip(knob(P('Tune Slope'), 'Slope', ink, 40)", "tip(knob(P('Tune Slope'), 'Slope', ink, 38)"))
SUBS.append(("tip(knob(P('Tune Slope Pivot'), 'Pivot', ink, 40)", "tip(knob(P('Tune Slope Pivot'), 'Pivot', ink, 38)"))

SUBS.append(("#app{position:relative;z-index:1}", "#app{position:relative;z-index:1}\n.seg.tight button{padding:0 6px;font-size:11px}"))

SUBS.append(("  const right = card('width:250px;gap:10px', h('div', { class: 'row' }, tip(h('div', { class: 'lbl', text: 'Wave ⓘ' })", "  const right = card('width:224px;flex:none;gap:10px', h('div', { class: 'row' }, tip(h('div', { class: 'lbl', text: 'Wave ⓘ' })"))

SUBS.append(("#app{position:relative;z-index:1}", "#app{position:relative;z-index:1}\n.tapebtn{display:flex;flex-direction:column;align-items:center;gap:2px;padding:4px;border:1px solid var(--line);border-radius:10px;background:var(--card);cursor:pointer;width:100%}\n.tapebtn canvas{width:168px;height:64px}\n.tapebtn .tapelbl{font-size:10.5px;font-weight:700;color:var(--dim);letter-spacing:.04em}\n.tapebtn.on .tapelbl{color:var(--ink)}\n.oscbtn{height:26px;border-radius:999px;padding:0 12px;font-weight:700}\n.oscbtn.on{background:var(--ink);color:var(--bg)}\n.pdd{width:156px!important}\n[data-theme=dark] .strip .kl,[data-theme=dark] .strip .lbl{color:color-mix(in srgb,var(--ink) 88%,transparent)!important}\n[data-theme=dark] [style*=\"opacity: 0.35\"] .kl{color:var(--ink)!important}"))

SUBS.append(("text: 'ON THE BEAT', style: 'margin:0;font-size:15px'", "text: 'IN THE POCKET', style: 'margin:0;font-size:15px'"))
SUBS.append(("    h('div', { class: 'row', style: 'gap:6px;align-items:flex-start' }, h('div', { class: 'lbl', text: 'Moods', style: 'padding-top:5px' }), moodBox));", "    h('div', { class: 'col', style: 'gap:6px;margin-top:auto' }, h('div', { class: 'lbl', text: 'Moods' }), moodBox));"))
SUBS.append(("const cond = card('width:350px;flex:none;gap:6px'", "const cond = card('width:350px;flex:none;gap:10px'"))
SUBS.append(("  return h('div', {}, cond, beat, jack);\n}", "  jack.appendChild(magicianArt());\n  return h('div', {}, cond, beat, jack);\n}"))
SUBS.append(("#app{position:relative;z-index:1}", "#app{position:relative;z-index:1}\n.moods{display:grid!important;grid-template-columns:repeat(3,1fr);gap:6px}\n.moods .btn{width:100%}"))

SUBS.append(("#app{position:relative;z-index:1}", "#app{position:relative;z-index:1}\n.setpop{background:var(--card)!important;opacity:1!important;backdrop-filter:none}"))

SUBS.append(("#app{position:relative;z-index:1}", "#app{position:relative;z-index:1}\n.card svg{max-width:100%;height:auto}"))

SUBS.append(("const mcv = h('canvas', { style: 'width:132px;height:62px;", "const mcv = h('canvas', { style: 'width:112px;height:62px;"))

SUBS.append(("""      h('div', { style: 'width:1px;align-self:stretch;background:var(--line)' }),
      tip(knob(P('Tune Slope'), 'Slope', ink, 38), 'Slope', 'A tilt EQ on the ringing: left makes it darker, right makes it brighter, up to 9 dB each way around the Pivot. Only the ringing is changed; your dry sound is left alone. The dashed line over the notes shows it.'),
      tip(knob(P('Tune Slope Pivot'), 'Pivot', ink, 38), 'Pivot', 'Where Slope tips over: below this the sound goes one way, above it the other. The dot on the dashed line marks it.')));""", """));"""))
SUBS.append(("""'Before strips: Tune rings first and the strips filter the ringing. After strips: the ringing comes last.'))),""", """'Before strips: Tune rings first and the strips filter the ringing. After strips: the ringing comes last.')),
      tip(knob(P('Tune Slope'), 'Slope', ink, 24), 'Slope', 'A tilt EQ on the ringing: left makes it darker, right makes it brighter, up to 9 dB each way around the Pivot. Only the ringing is changed; your dry sound is left alone.'),
      tip(knob(P('Tune Slope Pivot'), 'Pivot', ink, 24), 'Pivot', 'Where Slope tips over: below this the sound goes one way, above it the other.')),"""))
SUBS.append(("tip(h('div', { class: 'row', style: 'flex:none' }, seg(P('Tune Wave'))), 'Wave', motionTip),", "tip(h('div', { class: 'row', style: 'flex:none' }, sel(P('Tune Wave'), 120)), 'Wave', motionTip),"))

SUBS.append(("Drift: each note fades up and down at random, like a choir breathing. Free:", "Drift: each note fades up and down at random, like a choir breathing. Pulse: every note swells together. Arp: one note at a time, low to high. Fall: the swell runs down through the notes. Sparkle: a few notes flash on each step. Free:"))

SUBS.append(("#app{position:relative;z-index:1}", "#app{position:relative;z-index:1}\n[data-theme=dark] .kl{color:var(--ink)!important;opacity:1}\n[data-theme=dark] .lbl{color:color-mix(in srgb,var(--ink) 90%,transparent)!important}\n[data-theme=dark] .hint{color:color-mix(in srgb,var(--ink) 88%,transparent)!important}\n[data-theme=dark] .seg button:not(.on){color:var(--ink)!important}"))

SUBS.append(("  const left = card('flex:1;gap:10px',", "  const left = card('flex:1;gap:5px',"))
