# Gullay: credits and licences

Gullay is free software under the GNU GPL v3 (see LICENSE). It is built from:

- Three's Company (Otis Kanno, GPL v3): the modulation system, window, plugin layer.
  Its filters come from Surge's sst-filters and sst-basic-blocks (GPL v3), with SIMDe (MIT).
- 4ms Company: Dual Looping Delay and Tapographic Delay firmware (libs/fourms, see its LICENSE
  files). The DLD is ported to floating point in src/gy_dld.h; the Tapo tap allocator, tables
  and stmlib pieces are used as they are, the multitap delay re-done in src/gy_tapo.h.
- Valley Audio's Plateau (Dale Johnson, GPL v3): the Dattorro reverb tank (libs/valley).
- Cloud Seed (Ghost Note Audio, MIT): the second reverb (libs/cloudseed; small portability
  changes listed in libs/cloudseed/GULLAY-CHANGES.md).
- clap and clap-wrapper (MIT), downloaded by CMake; the VST3 SDK (MIT) and Apple's AudioUnitSDK
  (Apache 2.0), downloaded by clap-wrapper.
Each library keeps its own licence and notices in its folder.
