# Gullay quick start (v0.8.2 test build)

Gullay is a delay with three parts (A, Lead and B), two engines (Echo and Juba) and pages that keep the echoes moving. This page gets you playing in five minutes. The full manual with screenshots is [Gullay-Manual.pdf](Gullay-Manual.pdf).

This is a **test build**. Please report anything odd (see "Sending feedback" at the end).

## 1. Install

Gullay comes as CLAP, VST3 and AU (AU on Mac only). CLAP is the one tested in Bitwig.

### Mac (macOS 11 or later, Apple Silicon or Intel)

1. Download `Gullay-v0.8.2-mac.zip` from the repo's **Releases** page and double-click it to unzip.
2. Open Terminal, then paste this whole block and press Return. It copies the three plugins into place and clears the "downloaded from the internet" flag, so macOS lets them load:

   ```sh
   cd ~/Downloads/Gullay-v0.8.2-mac && \
   mkdir -p ~/Library/Audio/Plug-Ins/{CLAP,VST3,Components} && \
   cp -R Gullay.clap ~/Library/Audio/Plug-Ins/CLAP/ && \
   cp -R Gullay.vst3 ~/Library/Audio/Plug-Ins/VST3/ && \
   cp -R Gullay.component ~/Library/Audio/Plug-Ins/Components/ && \
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/CLAP/Gullay.clap ~/Library/Audio/Plug-Ins/VST3/Gullay.vst3 ~/Library/Audio/Plug-Ins/Components/Gullay.component
   ```

   If you unzipped it somewhere other than Downloads, change the first line to that folder.
3. **Bitwig:** Settings → Locations → rescan plug-ins, then add **Gullay** (CLAP) to a track. Other hosts: rescan, and use the VST3 or AU.
4. Look at the bottom of the window: the footer should read **v0.8.2 · test build**.

### Windows (10 or 11, 64-bit)

1. Download `Gullay-v0.8.2-windows.zip` from the **Releases** page and unzip it (right-click → Extract All).
2. Copy `Gullay.clap` into `C:\Program Files\Common Files\CLAP\` (create the CLAP folder if it isn't there). Copy the `Gullay.vst3` folder into `C:\Program Files\Common Files\VST3\`. Windows asks for administrator permission; click Continue.
3. Rescan plug-ins in your host (Bitwig: Settings → Locations), add **Gullay** (CLAP), and check the footer reads **v0.8.2 · test build**.

The window needs Microsoft's WebView2 Runtime. Windows 11 has it, and so do most up-to-date Windows 10 PCs. If Gullay shows a message about it instead of its window, install the "Evergreen Bootstrapper" from Microsoft's WebView2 page and open the plugin again. The sound works either way.

**Building it yourself instead (Mac).** Install the Xcode command line tools and CMake (`brew install cmake`), then run `sh build-all-mac.sh` in the repo folder. It builds all three formats and installs them.

## 2. First sound

1. Put Gullay on a track with something short and plucky: a piano, a guitar, a drum loop.
2. Click the **preset pill** (top centre). One click loads a preset and the menu stays open, so you can audition several in a row. Esc closes it.
3. Good places to start:
   - **Basic Delays**: everyday quarter, dotted-eighth, slapback, tape and dub echoes.
   - **Showcase**: each preset shows off two features where you can hear them.
   - **1 Note and Go**: play one note and let it run; arps and soundscapes that last about four bars.
   - **Performance**: these bring their own snapshot sequence. Press play in Bitwig to hear them change.
4. **Mix** (right end of the tab bar) sets dry/wet. **Kill** (top right) empties every echo at once.

## 3. The window at a glance

| Where | What it is |
|---|---|
| Header | Engine 1 and 2, Path (Chain, Side by side, Split), presets, Undo/Redo, Save, **Kill** |
| Echo graph | Every echo of A (above the line) and B (below) against the bars |
| A · Lead · B | A and B are the two delays; Lead scales both and colours all echoes |
| Page tabs | Engines, Reshape, River, Sway, Black Herman, Mutation, LFOs, Snapshots, Mod, Hymn, MIDI |
| Tab bar, right | Oscillate, Tails/Cut, Mod Depth, Mix |
| Footer | Settings, Bug Report, window size, Limiter, output meter |

## 4. Controls everywhere

| Do this | To |
|---|---|
| Double-click a knob | Reset it |
| Shift-drag | Fine control |
| Right-click a knob | Modulate with (LFO, Random, River…), MIDI Learn, Reset, Copy, Paste |
| Copy button on a panel | Copy and paste the whole panel |
| Die on a panel | Randomize just that panel |
| Copy on a filter row | Copy that filter to another part |
| Shift-click Oscillate | Kill, and Oscillate goes off |
| ‹ › by the preset pill, or ↑ ↓ in the menu | Step through presets |

## 5. The parts (A, Lead, B)

| Control | A and B | Lead |
|---|---|---|
| Time / Stretch | Delay time (Sync snaps to note values) | Stretches A and B together |
| Feedback | How much each echo repeats | Multiplies A's and B's feedback |
| Tone, Drive, Level, Pan | Colour, saturation, volume, side | The same, for all echoes |
| INF / Hold all | Hold that loop forever | Hold every loop |
| Rev | Echoes backwards | Reverse all |
| Filter | Type, place, Cutoff, Reso, Morph, Send, Mix | The same |

## 6. The pages, in one line each

- **Engines**: Memory (Clean or Tape, bits and rate grit), Juba (rhythmic multi-tap patterns, with your own pattern editor), Shadow (what B does: Answer, Reverse, Stutter, Freeze, Octave, Swell, Smear, Ghost, Ping-pong), Transport (Host or Free tempo, Varispeed) and Path.
- **Reshape**: splits the echoes into low, mid and high, each with its own level, pan and tone.
- **River**: three slow currents read off a river map. Use the **Sends** to point them at any setting, and push the amount slider up, because a send at 0 % moves nothing.
- **Sway**: slow LFOs that never line up, for gentle drift.
- **Black Herman**: a magician that nudges A and B. Influence, Lean, Moods, and In the Pocket for stepped moves.
- **Mutation**: Lead "calls" and A and B answer. The default is a call on every step; watch the call light, and press **Call now** to hear one. **Ancestors** grows a family tree of echoes.
- **LFOs**: three drawable LFOs.
- **Snapshots**: eight whole-sound snapshots and a step sequencer (chance per step, Order, Glide). It follows Bitwig's play position.
- **Mod**: 16 routes from any source to any target.
- **Hymn**: rings the notes of your key out of whatever comes in.
- **MIDI**: MIDI Learn mappings, profiles, and notes that set Hymn's key or pick Juba patterns.

## 7. Saving

- **Save** (header) stores the sound in your own **Yours** group. Settings → Your presets chooses the folder; a Dropbox or iCloud folder works.
- Everything, including your Juba patterns and snapshots, is saved with the Bitwig project.

## 8. If something seems wrong

- **No sound from a page:** check Mix and the part's Level. On River, check the send's amount slider. On Mutation, check that A's or B's response isn't Off.
- **Runaway feedback:** press **Kill**. The **Limiter** in the footer guards the output.
- **Footer doesn't say v0.8.2:** an older copy is loaded. Reinstall, then restart Bitwig.
- **The plugin doesn't show up or macOS blocks it:** run the `xattr` line from step 1 again, then rescan.
- **Windows: it doesn't show up:** check that `Gullay.clap` sits directly in `C:\Program Files\Common Files\CLAP\`, then rescan.

## Sending feedback

Click **Bug Report** in the footer. It writes a text file of every setting. Send it along with a note on what you did, what you expected and what you heard, plus the preset name.

Gullay is free software under the GNU GPL v3. See [LICENSE](../LICENSE) and [NOTICES.md](../NOTICES.md) for the projects it's built from.
