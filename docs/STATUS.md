# Gullay: where things stand

Read this first when picking the project back up. The brief and mockups:
- Brief (Claude Doc): https://claude.ai/artifact/A98dnDQKk2viRmeL59cgSV
- Mockups (Design canvas): https://claude.ai/artifact/RyWL7NFeusoFwMmqrZNpJm

## First full test build (v0.1, built and tested 2026-11-19)

Gullay is a fork of Three's Company's code (v0.18 from /Volumes/Mementos/Threes Company, read only).
TC's three strips (Low, Mid, High) become three parts (A, Lead, B). Each part keeps TC's six
knobs, with new meanings: Cutoff -> Time (semitones of time, 12 = double), Reso -> Feedback
(0..1.2; over 1.0 is Runaway), Morph -> Tone, Drive -> Drive (repeats, up to +21 dB),
Level, Pan. So TC's lanes, P-locks, LFOs, matrix, follower, Sway, Chaperone (Conductor) and
Mutation work unchanged on the delays. Lead's knobs are masters over both A and B.

- One engine: DLD -> A = channel A, B = channel B. Tapo -> A = early taps, B = late taps.
- Two engines: A = slot 1, B = slot 2, Lead = both. Wiring Series / Parallel / Spectral (TC's split).
- One engine, slot 2 = Call and Response or Mirror (replaces part B).
- Audio flow: in -> Tilt In -> filter (In) -> engines (64-sample FIFO, Tapo works in 64s) ->
  parts (tone, shift once, level, pan) -> filter (Out) -> Hymn Tune (TC's TuneBank) ->
  reverb (Plateau or Cloud Seed) -> crusher (Out) -> Tilt Out -> mix -> limiter.
- Loop effects (inside each part's feedback): Runaway (drive, tame, loop cuts), crusher (Loop),
  filter (Loop), Hymn shift Each repeat.

## Files
- src/gy_dld.h: float port of 4ms LoopingDelay (DLD), stereo, 16-bit + Degrade option, loop hook.
- src/gy_tapo.h: 4ms Tapo (MultitapDelay re-done: A/B groups, loop hook, factory patterns).
- src/gy_fx.h: tilt, part tone, Runaway, crusher, clean shifter.
- src/gy_reverb.h/.cpp: Plateau (Valley) and Cloud Seed (Ghost Note) behind one control set.
- src/gy_engine.h: TC's engine, forked; the audio core replaced by the delays.
- src/gy_clap.cpp: TC's plugin layer, forked; params renamed and extended.
- gui/index.html: TC's window, forked.
- libs/: TC's libs (sst-filters, sst-basic-blocks, simde, clap), fourms (4ms Tapo + DLD
  sources, MIT/GPL), valley (Plateau core, GPL), cloudseed (MIT, small local changes).

## Verified so far
- DLD port: an impulse comes back at exactly the set time; feedback 0.5 halves each repeat.
- Tapo: factory pattern 1 (16 taps) plays in groups A and B.
- Reverbs: both make sound; moving size/decay allocates nothing (Cloud Seed seeds cached).

## Built and tested for v0.1 (on Linux x86-64; the Mac build is yours to run)
- `sh run-tests.sh`: engine tests (no allocation on the audio path across 12 layouts; 60 random
  settings finite and under 0 dBFS with the safety net never stepping in; the heaviest layout,
  two engines + Spectral + Cloud Seed + everything moving, about 17 % of one x86 core) and the
  plugin host test (48 checks: every layout, Runaway held under the ceiling, Hold, crusher, filter
  places, tilts, both reverbs, shifts, Tune, reverse/INF, MIDI Program Change and note pattern
  picks, held-note key, MIDI Learn absolute / relative / 14-bit NRPN, mappings saved with the
  project, all 20 presets, state save/load, Snapshot file, two instances).
- The same host test passes under AddressSanitizer + UBSan, and on the CMake-built CLAP.
- CMake (clap-wrapper) builds CLAP and VST3 on Linux. The AU and the macOS window can only be
  built on the Mac: `sh build-all-mac.sh`.
- The window: every page rendered in headless Chrome with real data from the plugin, no
  script errors, in Light, Dark, Session and Unity themes.

## Bugs found and fixed while testing
- Ramps (gains, mix, fade) were ticked 64 times per FIFO block but set for 8, so they ran 8x
  past their targets: overshoot, then NaN. Ramps now stop at their target.
- The filter in Out and Loop places ran 64 samples on coefficients prepared for 8 (extrapolated,
  unstable). Its coefficients now step once per FIFO block there.
- Runaway's rider let single clicks through; it now catches the same sample and Tame sets how
  slowly the loop comes back.

## Known gaps in v0.1 (tell me which matter most)
- The River's course is a stand-in curve: the Ogeechee's USGS / OpenStreetMap trace could not be
  downloaded from here. Same controls; the shape changes when the real trace goes in.
- Hymn guards: Settle is in; Range and Every (limits on how far / how often Each repeat shifts)
  are not yet.
- INF, Reverse and the Tapo pattern are switches / a mod target, not step events on the lanes.
- Reverb levels are not matched between Plateau and Cloud Seed (Plateau is louder early on).
- Latency: 64 samples always (the Tapo block), plus the FFT frame in Spectral. Changing
  Spectral on/off asks Bitwig to restart the plugin so it hears the new delay.
- MIDI: "held notes set the key" uses notes outside C1-B2 when pattern picking is on.
