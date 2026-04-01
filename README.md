# toolBoy Clock v3

toolBoy Clock v3 is a transport-locked MIDI clock and restart controller for AU and VST3 hosts. It runs as an audio effect with audio passthrough, emits MIDI, follows the DAW grid strictly, and adds pattern-based restart logic, gated resync, shuffle, remote MIDI control, and an integrated Gumroad activation flow.

## Main features

- Strict DAW-locked timing with no free-run clock.
- AU and VST3 builds from one universal macOS project.
- Audio passthrough plus MIDI output for host routing compatibility.
- Run, trigger, resync, and latch behavior quantized to the DAW grid.
- Selectable restart offset across 16 steps.
- Pattern restart lane with interval modes: OFF, 1, 2, 4, 8, 16, 32, 64, and RND.
- Shuffle in classic stepped mode or linear mode.
- Clock-rate modes: 32, 16, 8, and 4.
- Audio click modes: OFF, BEAT, 8TH, 16TH, and 24 PPQ.
- MIDI remote control for Start, Stop, Trigger, Resync, and Latch.
- Remote note-thru plus visual trigger feedback from remote trigger events.
- Theme color editing with scalable color pickers.
- Built-in Gumroad registration overlay with machine-bound local license storage.

## Quick start

1. Insert Clock v3 as an AU or VST3 effect on a track that can pass MIDI output.
2. Start the DAW transport.
3. Use the large center ring to choose the resync target step.
4. Use the run control to arm or stop clock output.
5. Use Trigger when you want a quantized restart.
6. Open Setup to configure remote MIDI input, note mappings, and licensing.

## Control guide

### Header and setup

- `SETUP`: opens MIDI remote configuration and shows the current licensed user.
- `LATCH`: gates whether scheduled resyncs are allowed to fire.
- `?`: shows contextual help text.
- Color dots: adjust accent, cyan, and base theme colors.

### Ring controls

- `Offset ring`: selects the step inside the bar where the next resync or armed restart will fire.
- `Step ring`: shows the running step and supports manual trigger.
- `Pattern interval`: sets how often the 16-step pattern lane takes over for one full bar.
- `Shuffle`: sets timing bend amount.
- `Shuffle mode`: toggles stepped 909-style shuffle vs linear shuffle.
- `Rate`: chooses clock rate scaling.
- `Click`: chooses metronome mode and pulse style.
- `Gate/Latch`: controls whether pending resync events are allowed through.

## How it behaves

- `Run` sends clock only while active.
- `Trigger` quantizes to the next 16th boundary and can arm a bar restart depending on the current mode.
- `Resync` always targets the shortest valid future occurrence of the selected offset step.
- `Pattern` steps send Start events only when the pattern interval mode schedules that bar.
- `Rate` changes always arm a resync so the output realigns cleanly.
- `Remote MIDI` can drive Start, Stop, Trigger, Resync, and Latch from notes or CCs.

## Remote MIDI defaults

- Start: `C#-2`
- Stop: `D#-2`
- Trigger: `F#-2`
- Resync: `G#-2`
- Latch: `A#-2`

## Licensing

- On first launch, the plugin shows a full-editor Gumroad activation overlay.
- Activation validates against Gumroad product `eaWDZgauhj9vCNxpyiBoIQ==`.
- A successful activation stores a machine-bound local license in:
  - `~/Library/Application Support/toolBoy/Clock v3/clock_v3.lic`
  - `~/Library/Application Support/toolBoy/clock_v3.lic`

## Build

Prerequisites:

- CMake 3.21+
- Xcode command line tools
- JUCE submodule checked out
- Developer ID signing identity and notarytool profile for release packaging

Build the universal release:

```sh
git submodule update --init --recursive
cmake --preset release
cmake --build --preset release
```

The release preset builds universal macOS binaries for both `arm64` and `x86_64`.

## Release packaging

Create a signed, notarized, stapled installer DMG:

```sh
./package_release.sh
```

The script:

- rebuilds the universal release preset
- verifies `arm64` and `x86_64` are present
- signs AU and VST3 bundles
- builds a DMG with drag-and-drop install links
- signs the DMG
- submits it for notarization
- staples and validates the notarized DMG

## Install from the DMG

1. Drag `toolBoy Clock v3.component` onto the `Components` link.
2. Drag `toolBoy Clock v3.vst3` onto the `VST3` link.
3. Or open the `Plug-Ins` link and copy the bundles manually.

## License

This project uses JUCE. See the JUCE license files in [JUCE/README.md](JUCE/README.md) and the JUCE repository documentation for licensing details.
