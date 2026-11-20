import re, sys
src = open('/home/claude/gullay/gui/index.html').read()
L = src.split('\n')

def find_fn(name):
    for i, l in enumerate(L):
        if l.startswith('function ' + name + '(') :
            if l.count('{') == l.count('}'):
                return i, i
            for j in range(i, len(L)):
                if L[j] == '}':
                    return i, j
    raise SystemExit('no fn ' + name)

def replace_fn(name, new):
    global L
    i, j = find_fn(name)
    L = L[:i] + new.strip('\n').split('\n') + L[j + 1:]

def delete_fn(name):
    replace_fn(name, '')

def replace_line_starting(prefix, new, count=1):
    global L
    n = 0
    for i, l in enumerate(L):
        if l.startswith(prefix):
            L[i] = new; n += 1
            if n == count: return
    if n == 0: raise SystemExit('no line ' + prefix)

def replace_block(start_prefix, end_line_exact, new):
    """replace lines from the one starting with start_prefix through the next line equal to end_line_exact"""
    global L
    for i, l in enumerate(L):
        if l.startswith(start_prefix):
            for j in range(i, len(L)):
                if L[j] == end_line_exact:
                    L = L[:i] + new.strip('\n').split('\n') + L[j + 1:]
                    return
    raise SystemExit('no block ' + start_prefix)

exec(open('/tmp/gui/parts.py').read())
out = '\n'.join(L)
for a, b in SUBS:
    if a not in out:
        print('WARN missing sub:', a[:70])
    out = out.replace(a, b)
# Three's Company's gold and black: Gullay's parts' colours instead
import re as _re
out = out.replace("'#FFC107'", "bandColour(2)").replace("'#E0A000'", "bandColour(2)").replace("'#2A1E00'", "textOn(bandColour(2))")
out = out.replace("#FFC107", "var(--hi)").replace("color:#2A1E00", "color:var(--ink)")
out = out.replace("'Low (darker)' : 'High (golder)'", "'A (toward A’s colour)' : 'B (toward B’s colour)'")
for bad in ['#FFC107', '#E0A000', '#2A1E00', 'golder']:
    if bad in out: print('still has', bad)
# v0.5 names: Chaperone is Black Herman (after the magician), 3rd Wheel is Legerdemain
for x, y in [("the Chaperone", "Black Herman"), ("The Chaperone", "Black Herman"), ("CHAPERONE", "BLACK HERMAN"), ("3RD WHEEL", "LEGERDEMAIN"), ("3rd Wheel", "Legerdemain"), ("Third wheel", "Legerdemain"), ("third wheel", "Legerdemain")]:
    out = out.replace(x, y)
out = _re.sub(r"(?<![A-Za-z_])Chaperone", "Black Herman", out)
open('/home/claude/gullay/gui/index.html', 'w').write(out)
print('ok', len(out))
