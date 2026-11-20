import sys, json
page = sys.argv[1]; theme = sys.argv[2] if len(sys.argv) > 2 else ''; extra = sys.argv[3] if len(sys.argv) > 3 else ''
html = open('/home/claude/gullay/gui/index.html').read()
msgs = open('/tmp/gui/h/msgs.js').read()
shim = '''<script>
window.__err = [];
window.addEventListener('error', (e) => { window.__err.push(String(e.message) + ' @' + e.lineno); });
window.webkit = { messageHandlers: { tc: { postMessage: (m) => { (window.__out = window.__out || []).push(m); } } } };
''' + msgs + '''
</script>'''
run = '''<script>
(function () {
  try { eval(window.__msgs[0]); } catch (e) { __err.push('init: ' + e.message + ' ' + (e.stack||'').split('\\n')[1]); }
  THEME_HOOK
  try { S.page = '%s'; showPage(); } catch (e) { __err.push('page: ' + e.message + ' ' + (e.stack||'').split('\\n')[1]); }
  for (let i = 1; i < window.__msgs.length; ++i) { try { eval(window.__msgs[i]); } catch (e) { __err.push('upd' + i + ': ' + e.message + ' ' + (e.stack||'').split('\\n')[1]); break; } }
  // every tab, then back
  try { PAGES.forEach(([id]) => { S.page = id; showPage(); eval(window.__msgs[window.__msgs.length - 1]); }); S.page = '%s'; showPage(); eval(window.__msgs[window.__msgs.length - 1]); } catch (e) { __err.push('tabs: ' + e.message + ' ' + (e.stack||'').split('\\n')[1]); }

  setTimeout(() => { try {
    const rep = [], pages = document.getElementById('pages'), pr = pages.getBoundingClientRect(), foot = document.querySelector('footer').getBoundingClientRect();
    const txt = (e) => (e.getAttribute('aria-label') || e.textContent || e.className || e.tagName).replace(/\s+/g, ' ').trim().slice(0, 40);
    const cards = [...document.querySelectorAll('#pages .card, .strip, .setpop')].filter((c) => c.offsetParent && c.getBoundingClientRect().width > 0);
    cards.forEach((c) => {
      const cr = c.getBoundingClientRect(), name = txt(c.querySelector('h2,h3,.hd,.lbl') || c);
      if (cr.bottom > foot.top + 1) rep.push('PAGE-OVERFLOW ' + name + ' by ' + Math.round(cr.bottom - foot.top));
      [...c.querySelectorAll('*')].forEach((e) => {
        if (!e.offsetParent || e.closest('.pop')) return; const r = e.getBoundingClientRect(); if (r.width < 2 || r.height < 2) return;
        if (r.right > cr.right + 1 || r.bottom > cr.bottom + 1 || r.left < cr.left - 1) rep.push('OUTSIDE ' + name + ': ' + txt(e) + ' (' + Math.round(Math.max(r.right - cr.right, r.bottom - cr.bottom, cr.left - r.left)) + 'px)');
        const cs = getComputedStyle(e); if ((cs.overflow === 'hidden' || cs.overflowX === 'hidden' || cs.textOverflow === 'ellipsis') && e.scrollWidth > e.clientWidth + 2 && !/^(CANVAS|svg|INPUT)$/i.test(e.tagName)) rep.push('CLIPPED ' + name + ': ' + txt(e));
      });
      if (c.classList.contains('card') && !c.classList.contains('setpop')) {
        let bottom = cr.top; [...c.children].forEach((k) => { if (k.offsetParent) bottom = Math.max(bottom, k.getBoundingClientRect().bottom); });
        const gap = cr.bottom - 12 - bottom; if (gap > 46) rep.push('EMPTY ' + name + ': ' + Math.round(gap) + 'px at the bottom');
        const ks = [...c.children].filter((k) => k.offsetParent).map((k) => k.getBoundingClientRect()).sort((x, y) => x.top - y.top);
        for (let i = 1; i < ks.length; ++i) { const g = ks[i].top - Math.max(...ks.slice(0, i).map((q) => q.bottom)); if (g > 46) rep.push('GAP ' + name + ': ' + Math.round(g) + 'px between parts'); }
      }
    });
    const pcs = cards.filter((c) => c.closest('#pages') && c.classList.contains('card')); if (pcs.length > 1) { pcs.forEach((c) => { const top = c.getBoundingClientRect().top, row = pcs.filter((o) => Math.abs(o.getBoundingClientRect().top - top) < 24), bot = Math.max(...row.map((o) => o.getBoundingClientRect().bottom)); const g = bot - c.getBoundingClientRect().bottom; if (g > 40) rep.push('SHORT ' + txt(c.querySelector('h2,h3,.hd,.lbl') || c) + ': ends ' + Math.round(g) + 'px above the others'); }); }
    document.querySelectorAll('#pages .seg, .setpop .seg').forEach((sg) => { if (!sg.offsetParent) return; const r = sg.getBoundingClientRect(), bs = [...sg.children].filter((b) => b.offsetParent); if (!bs.length) return; const last = Math.max(...bs.map((b) => b.getBoundingClientRect().right)); if (r.right - last > 12) rep.push('SLACK switch ' + txt(sg) + ': ' + Math.round(r.right - last) + 'px empty after the last button'); });
    // empty space to the right of the last card in each page row
    const rows = [...document.querySelectorAll('#pages > div > div, #pages > div')].filter((r) => r.offsetParent);
    const pc = cards.filter((c) => c.closest('#pages')); if (pc.length) { const right = Math.max(...pc.map((c) => c.getBoundingClientRect().right)); if (pr.right - right > 40) rep.push('EMPTY-RIGHT page: ' + Math.round(pr.right - right) + 'px'); }
    const d = document.createElement('pre'); d.id = 'lay'; d.style.display = 'none'; d.textContent = 'LAY:' + JSON.stringify([...new Set(rep)]); document.body.appendChild(d);
  } catch (e) { const d = document.createElement('pre'); d.id = 'lay'; d.style.display = 'none'; d.textContent = 'LAY:err ' + e.message; document.body.appendChild(d); } }, 1500);
  setTimeout(() => { const d = document.createElement('pre'); d.id = "errs"; d.style.display = "none"; d.textContent = 'ERRS:' + JSON.stringify(window.__err); document.body.appendChild(d); }, 300);
})();
</script>''' % (page, page)
run = run.replace('THEME_HOOK', extra + ';' + 'THEME_HOOK') if extra else run
run = run.replace('THEME_HOOK', ("try { applyTheme('%s'); build(); } catch (e) { __err.push('theme: ' + e.message); }" % theme) if theme else '')
html = html.replace('<script>\n"use strict";', shim + '\n<script>\n"use strict";', 1)
html = html.replace('</body>', run + '\n</body>', 1)
open('/tmp/gui/h/t_%s%s.html' % (page, theme), 'w').write(html)
