#!/bin/sh
L=/tmp/mock/lay.sh
echo "== engines juba"; $L 0 engines porch "setP(P('Engine 1'),1); build();"
echo "== engines one echo"; $L 0 engines porch "setP(P('Shadow'),3); build();"
echo "== engines chain free"; $L 0 engines porch "setP(P('Engine 1'),0); setP(P('Engine 2'),2); setP(P('Wiring'),1); setP(P('DLD Memory'),2); setP(P('Sync'),1); setP(P('Varispeed'),1); build();"
echo "== engines split"; $L 19 engines porch ""
echo "== engines split freq two"; $L 0 engines porch "setP(P('Engine 2'),1); setP(P('Parallel Split'),1); setP(P('Spectral Split By'),0); build();"
echo "== engines split freq one"; $L 0 engines porch "setP(P('Parallel Split'),1); setP(P('Spectral Split By'),0); build();"
echo "== engines two echoes"; $L 0 engines porch "setP(P('Engine 2'),1); build();"
for pg in river sway conductor mutation snaps lfos mod tune midi; do echo "== $pg"; $L 2 $pg porch ""; done
echo "== settings"; $L 2 engines porch "setTimeout(()=>toggleSettings(),200);"
echo "== settings theme list"; $L 2 engines porch "setTimeout(()=>{toggleSettings(); setTimeout(()=>{const b=[...document.querySelectorAll('.setpop .dd')].find(x=>x.textContent.includes('Porch Ceiling')); b&&b.click();},150)},200);"
echo "== preset browser"; $L 2 engines porch "setTimeout(()=>openBrowser(),200);"
