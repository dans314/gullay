#!/bin/sh
# Builds and runs Gullay's tests: the engine (allocation, random settings, CPU), then the plugin
# loaded the way a DAW loads it (every layout, Runaway, effects, MIDI Learn, presets, state).
#   sh run-tests.sh
# Needs a C++ compiler (on a Mac: the command line developer tools).
set -e
cd "$(dirname "$0")"
OUT="${GY_BUILD_DIR:-build}"
mkdir -p "$OUT"
CXX="${CXX:-c++}"
INC="-Isrc -Ilibs/sst-filters-include -Ilibs/sst-basic-blocks-include -Ilibs/simde -Ilibs/fourms -Ilibs/fourms/tapo -Ilibs/valley -Ilibs/cloudseed"
SRCS="src/gy_reverb.cpp libs/valley/Plateau/Dattorro.cpp libs/valley/dsp/filters/OnePoleFilters.cpp libs/valley/utilities/Utilities.cpp
 libs/cloudseed/Parameters.cpp libs/cloudseed/DSP/*.cpp libs/fourms/tapo/tap_allocator.cc libs/fourms/tapo/resources.cc
 libs/fourms/tapo/stmlib/utils/random.cc libs/fourms/tapo/stmlib/dsp/units.cc"
python3 tools/embed_gui.py >/dev/null 2>&1 || true
echo "== engine"
"$CXX" -std=c++20 -O2 -DTEST -o "$OUT/gy_engine_tests" tests/gy_engine_tests.cpp $SRCS $INC
"$OUT/gy_engine_tests"
echo "== plugin"
if [ "$(uname)" = "Darwin" ]; then SHARED="-bundle"; LIBS=""; else SHARED="-shared -fPIC"; LIBS="-ldl"; fi
"$CXX" -std=c++20 -O2 -DTEST -DGY_NO_WINDOW -fvisibility=hidden $SHARED -o "$OUT/gullay-test.clap" src/gy_clap.cpp $SRCS $INC -Ilibs/clap-main/include $LIBS
"$CXX" -std=c++20 -O2 -o "$OUT/gy_host_test" tests/gy_host_test.cpp -Ilibs/clap-main/include $LIBS
"$OUT/gy_host_test" "$OUT/gullay-test.clap"
