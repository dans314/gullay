#!/bin/sh
L=/tmp/mock/lay.sh
echo "== engines juba"; $L 2 engines porch ""
echo "== engines one echo"; $L 0 engines porch "setP(P('B Plays'),1); build();"
echo "== engines chain free"; $L 0 engines porch "setP(P('Engine 1'),0); setP(P('Engine 2'),2); setP(P('Wiring'),1); setP(P('DLD Memory'),2); setP(P('Sync'),1); setP(P('Varispeed'),1); build();"
echo "== engines split"; $L 19 engines porch ""
echo "== engines two echoes"; $L 0 engines porch "setP(P('Engine 2'),1); build();"
for pg in river sway conductor mutation snaps lfos mod tune midi; do echo "== $pg"; $L 2 $pg porch ""; done
echo "== settings"; $L 2 engines porch "setTimeout(()=>toggleSettings(),200);"
