# ClockSync (JUCE VST3)

ClockSync is a JUCE-based VST3 instrument plugin that generates MIDI realtime clock synced to the DAW transport, with an optional audio click for monitoring.

- MIDI realtime messages: Start (FA), Continue (FB), Stop (FC), and Clock (F8)
- Clock resolution: 24 / 48 / 96 PPQ (default 96)
- Optional audio click on quarter-notes
- Stereo audio output (no audio input), produces MIDI output

## Build (macOS)

Prerequisites:
- Xcode command line tools or a C++ toolchain
- CMake 3.21+
- Git (to fetch JUCE submodule)

Setup:

```sh
# From the project root
git submodule update --init --recursive
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug --config Debug
```

The resulting plugin (VST3 bundle) will be copied into the build output directory (and optionally to the system VST3 dir if configured by JUCE).

## Usage

- Load the plugin on an instrument track. Ensure your DAW allows MIDI out from instrument plugins (DAW-dependent).
- Use the UI to select clock resolution, enable audio click if desired, and set the click level.
- Press Play in the DAW; the plugin will emit Start/Continue and then Clock messages aligned to the transport. On Stop, it emits Stop.

DAW routing caveats:
- Some hosts restrict VST3 MIDI output or require special routing to capture plugin MIDI (e.g., virtual MIDI ports or routing to another track).
- The plugin must be placed on a track type that accepts instrument plugins to advertise MIDI output in many hosts.

## MIDI Clock Notes

- Clock pulses per quarter note (PPQ) can be 24 (classic), 48, or 96. Higher PPQ provides finer timing resolution but hosts may quantize timestamping per audio block.
- Start (FA) is sent when playback begins from the start; Continue (FB) is sent on resume from a non-zero position; Stop (FC) when playback stops.
- Song Position Pointer (F2) is not emitted by default, but can be added if needed.

## License

This project uses JUCE (MIT/Commercial). Consult the JUCE license for details.
