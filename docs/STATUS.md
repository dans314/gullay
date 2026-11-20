# Gullay: where things stand

Read this first when picking the project back up. The brief and mockups:
- Brief (Claude Doc): https://claude.ai/artifact/A98dnDQKk2viRmeL59cgSV
- Mockups (Design canvas): https://claude.ai/artifact/RyWL7NFeusoFwMmqrZNpJm

## The plan for the first full test build (in progress)

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
