# Gullay

A delay for Bitwig and other hosts (CLAP, VST3, AU on Mac). It runs the 4ms Dual Looping Delay and Tapographic Delay under Three's Company's modulation, and adds Runaway feedback, a crusher, Hymn's tuning and two reverbs.

**v0.8.2 is a test build.**

- **Start here:** [Quick start](docs/QUICKSTART.md) covers install, first sound and the controls on one page.
- **Full manual with screenshots:** [docs/Gullay-Manual.pdf](docs/Gullay-Manual.pdf).
- **Ready-made Mac plugins:** see Releases (`Gullay-v0.8.2-mac.zip`).

## Building

- Mac (CLAP, VST3, AU, Apple Silicon and Intel): `sh build-all-mac.sh`. Needs `brew install cmake`, and an internet connection the first time. It builds and installs all three formats.
- Tests: `sh run-tests.sh`.
- Where things stand and known gaps: [docs/STATUS.md](docs/STATUS.md).

## Licence

GNU GPL v3 (see [LICENSE](LICENSE)). Credits for the projects Gullay is built from are in [NOTICES.md](NOTICES.md). Each library in `libs/` keeps its own licence.
