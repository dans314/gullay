#!/bin/sh
# cap <name> <preset> <page> <theme> <extraJS> [demo]
name=$1; pre=$2; pg=$3; th=$4; js=$5; demo=$6
cd /tmp/gui/h
GY_DEMO="$demo" HOME=/tmp/gyhome ./dump /home/claude/gullay/build/gullay-test.clap msgs.js $pre >/dev/null
python3 mkq.py $pg $th "$js" >/dev/null
timeout 60 google-chrome --headless=new --no-sandbox --disable-gpu --hide-scrollbars --virtual-time-budget=3500 --window-size=1280,940 --screenshot=/tmp/mock/$name.png file:///tmp/gui/h/t_${pg}${th}.html >/dev/null 2>&1
timeout 60 google-chrome --headless=new --no-sandbox --disable-gpu --virtual-time-budget=3500 --dump-dom file:///tmp/gui/h/t_${pg}${th}.html 2>/dev/null | grep -o 'ERRS:\[[^<]*' | head -c 200; echo " $name"
