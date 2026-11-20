#!/bin/sh
# lay <preset> <page> <theme> <extraJS> [demo] : print layout problems
pre=$1; pg=$2; th=$3; js=$4; demo=$5
cd /tmp/gui/h
GY_DEMO="$demo" HOME=/tmp/gyhome ./dump /home/claude/gullay/build/gullay-test.clap msgs.js $pre >/dev/null
python3 mkq.py $pg $th "$js" >/dev/null
timeout 60 google-chrome --headless=new --no-sandbox --disable-gpu --virtual-time-budget=4000 --window-size=1280,940 --dump-dom file:///tmp/gui/h/t_${pg}${th}.html 2>/dev/null | grep -o 'LAY:\[[^<]*' | tail -1 | python3 -c "
import sys,json,html
s=html.unescape(sys.stdin.read().strip())[4:]
try:
  for x in json.loads(s): print('   ',x)
except Exception as e: print(s[:500])"
