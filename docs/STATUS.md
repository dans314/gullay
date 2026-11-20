# Gullay: where things stand

## v0.3.1 (2026-11-20): end of the second test session
- Hymn: Amount at 100 % is "only the ringing" whether the switch is on or not: the resonators replace your
  sound everywhere, so at any Mix you hear the ringing and its echoes, never the dry sound.
- Memory: Rate (Memory Rate), 44.1 kHz (off) down to 5 kHz, on both engines' echoes: a sample-and-hold to
  the lower rate, then a 2-pole low-pass at 0.42 x the rate, like a sampler's output filter. Degrade is
  now labelled Bits.
- Sync on Time: note values, straight, dotted (D) and triplet (T), 1/64 to 4 bars (the knob shows "1/4",
  "1/8D", "1/4T" ...; typing them works too). Lead's Sync snaps its knob to musical stretches (x1/2, x2/3,
  x3/4, x1, x1.5 ...); its modulation still moves smoothly. Fixed: the old ratio list had no 3/4, so
  Dotted Eighth, Tape Echo and Dub Delay played triplets.
- Runaway tab removed from the window (Runaway, Tilt, Crusher, spectrum). Their engine settings stay at
  their defaults and remain automatable; Auto Kill still runs at 30 s and Kill stays in the header.
- Tapographic card fits (Tapped beside TAP, MIDI picks in its header).
- Not reproduced: "Tapo doesn't show the spectral graph". The window shows the split display and the
  Spectral tab with DLD, one Tapo and two Tapos. Note: one Tapo in Spectral hears every band summed (one
  tap memory can't be split by band), so Spectral changes nothing audible there. Ask for a screenshot.

## v0.3 (2026-11-20): second test session
- Init is a plain quarter-note delay (both parts one beat, 40 % feedback, centred, Mix 35 %). The
  older presets keep their own starting point. New "Bread and Butter" group: 10 everyday delays.
- Reverb removed (engine kept out of the signal path; its parameter ids are retired, not reused).
- Each path has its own filter, under its knobs: A, Lead, B each pick a type, shape, cutoff, reso,
  morph and place. A/B: In = before their engine, Out = on their echoes, Loop = in their repeats.
  Lead's is the old shared filter (same ids): In before both, Out on all echoes, Loop in every
  repeat, so old projects sound the same. Mod targets: A/Lead/B Filter Cutoff, Reso, Morph.
  The Tone tab is now Runaway (Runaway, Tilt, Crusher, spectrum).
- Kill: Auto Kill After defaults to 30 s (5-60); the Kill button pulses red once a runaway has run
  10 s. The detector looks at the loop itself (input quiet, feedback at 99 %+ or Hold), and counts
  down instead of resetting on a single quiet block.
- Mod: a Depth knob for each source (Seq A/Lead/B, LFO 1-3, Follower, River A/Lead/B) scales it
  everywhere it is used. River page: four sends (Mod slots 13-16), each Off / A / Lead / B.
- Chaperone: Lock in keeps the last 4/8/16/32 steps it played and loops them.
- Hymn: Shape Free / Gate / Decay (Threshold, Hold, Decay): a gated or plucky ringing layer.
- Mutation can move Tone, Tapo Pattern, Filter, Crusher, Hymn, Shift, Degrade, Tilt, Spectral band.
- Engines: Memory and Degrade apply to both engines. The Tapographic card shows only when a slot is
  Tapo; with two, Tapo 1 / Tapo 2 switch (each its own pattern, morph, dynamics, wobble). Tap a
  pattern with the mouse: TAP, then 4/8/16 taps; it plays back as tapped (pattern "Tap", saved).
  One Engine card only with one engine; Two Engines card only with two.
- P-Locks: a step's locks can last 1-16 steps (Elektron style); held steps are hatched.
- Rates: Sequencer and P-Lock rates, the Chaperone grid and Tapo Morph Sync go up to 8 bars.
- The top display is an echo map: where each echo lands on a beat grid, how loud, A above the
  water and B below, Tapo taps and repeats, and ripples from your playing passing through them.
- Themes: Porch Ceiling is the default, with a new Porch Ceiling Dark; the light/dark switch in
  Settings keeps you in the theme's family. Three's Company's gold no longer shows anywhere.
- Fixed: after a reset the DLD ran at half the set time (a cached time target).

## v0.2 (2026-11-19): fixes from the first test session
- Tapo sounded wrong: its tap wobble was passed to the module unscaled (at 10 % every tap's time
  jumped about 50 ms every few ms). Now scaled to musical ranges and off by default (Wobble / Rate).
  The tank's soft limiter is now the clamped one (it folded back on loud feedback).
- Tapo patterns keep their own lengths (as the module stores them, at 120 bpm, following the
  tempo), so pattern buttons, the dice and MIDI picks change the timing. Time stretches the
  pattern; Sync rounds its length to the DLD's ratios. New: Dynamics (lifts quiet taps).
- Tapo Morph: Free (ms) or Sync (1/16 .. 4 bars).
- Kill (header): empties every loop and the reverb tail. Auto Kill (Tone > Runaway, on by default):
  does it when a runaway held by the ceiling lasts longer than After (5 s).
- Hymn: "Only the ringing": with Tune on, at Amount 100 % you hear only the ringing, never the sound.
- Spectral: a three-band split. Low to slot 1 (A), Mid to both (Lead), High to slot 2 (B); one DLD:
  channel A gets Low + Mid, B gets Mid + High. Two split points and three band levels, all mod
  targets; the top display shows the split (drag the lines) while Spectral is on. Split By / Balance /
  Sharpness / FFT quality now actually reach the splitter (they did nothing in v0.1).
- Window: Lead's colour (and the tabs, the name, the river) follow the Chaperone's Lean, from A's
  colour to B's. The river is stronger and shows through the panels; its motion moved to Settings.
  Timing page removed. Init preset; ‹ › step through the presets.


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
