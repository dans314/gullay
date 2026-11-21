# Gullay: credits and licences

Gullay is free software under the GNU GPL v3 (see LICENSE). It is built from:

- Three's Company (Otis Kanno, GPL v3): the modulation system, window, plugin layer.
  Its filters come from Surge's sst-filters and sst-basic-blocks (GPL v3), with SIMDe (MIT).
- 4ms Company: Dual Looping Delay and Tapographic Delay firmware (libs/fourms, see its LICENSE
  files). The DLD is ported to floating point in src/gy_dld.h; the Tapo tap allocator, tables
  and stmlib pieces are used as they are, the multitap delay re-done in src/gy_tapo.h. Three changes to
  their code: libs/fourms/tapo/audio_buffer.hh's Write() wrapped one sample late and wrote one past the
  end of the memory; Gullay's copy wraps straight after the last cell (marked in the file). v0.8: a second
  change, in libs/fourms/tapo/fader.hh: the tap fader keeps its volume within 0..1 on every sample (the
  original clamped once per block, so a fade shorter than a block overshot; marked in the file). v0.8.1: a third, in
  libs/fourms/tapo/tap.hh: a ResetFilter() method so Kill can empty a tap's filter (marked in the file).
- CHOW Tape Model (Jatin Chowdhury, GPL v3): the magnetic hysteresis model behind Memory's Tape mode.
  The original files are in libs/chowtape (HysteresisOps.h, HysteresisProcessing.h/.cpp) with the
  project's LICENSE; Gullay's scalar, RK2-only adaptation is src/gy_tape.h.
  https://github.com/jatinchowdhury18/AnalogTapeModel
- Valley Audio's Plateau (Dale Johnson, GPL v3): the Dattorro reverb tank (libs/valley).
- Cloud Seed (Ghost Note Audio, MIT): the second reverb (libs/cloudseed; small portability
  changes listed in libs/cloudseed/GULLAY-CHANGES.md).
- clap and clap-wrapper (MIT), downloaded by CMake; the VST3 SDK (MIT) and Apple's AudioUnitSDK
  (Apache 2.0), downloaded by clap-wrapper.
Each library keeps its own licence and notices in its folder.
