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
  setTimeout(() => { const d = document.createElement('pre'); d.id = 'errs'; d.textContent = 'ERRS:' + JSON.stringify(window.__err); document.body.appendChild(d); }, 300);
})();
</script>''' % (page, page)
run = run.replace('THEME_HOOK', extra + ';' + 'THEME_HOOK') if extra else run
run = run.replace('THEME_HOOK', ("try { applyTheme('%s'); build(); } catch (e) { __err.push('theme: ' + e.message); }" % theme) if theme else '')
html = html.replace('<script>\n"use strict";', shim + '\n<script>\n"use strict";', 1)
html = html.replace('</body>', run + '\n</body>', 1)
open('/tmp/gui/h/t_%s%s.html' % (page, theme), 'w').write(html)
