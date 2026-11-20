# Window build and check tools (kept here so work can resume)

The window page is built from Three's Company's page (`index.tc.html`, read only) plus Gullay's changes:

    cp tools/gui/index.tc.html gui/index.html     # then:
    python3 tools/gui/tx.py                        # applies parts.py (find/replace) and appends new.js
    python3 tools/embed_gui.py                     # embeds gui/index.html into src/gy_gui_html.h

`tx.py` expects its inputs in /tmp/gui (index.tc.html, parts.py, new.js). To resume:

    mkdir -p /tmp/gui/h /tmp/mock
    cp tools/gui/{index.tc.html,parts.py,new.js,tx.py} /tmp/gui/
    cp tools/gui/{dump.cpp,mk.py,mkq.py} /tmp/gui/h/
    cp tools/gui/{cap.sh,lay.sh,audit.sh} /tmp/mock/
    cp tools/gui/gysrcs.txt /tmp/gysrcs.txt

Screenshots and the layout audit (headless Chrome): build `dump` from dump.cpp against the test plugin, then
`/tmp/mock/cap.sh <name> <preset> <page> <theme> "<js>"` and `sh /tmp/mock/audit.sh` (every page and state).

Test plugin, host test and full suite: see docs/STATUS.md and run-tests.sh. `gysrcs.txt` lists the extra sources
for the one-line test build.

tools/canvas/project holds the mockup board's files (Design canvas; last published before v0.6).
