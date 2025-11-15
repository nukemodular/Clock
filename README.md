# ClockSync (JUCE VST3)

ClockSync is a JUCE-based VST3 instrument plugin that generates MIDI realtime clock synced to the DAW transport, with an optional audio click for monitoring.

- MIDI realtime messages: Start (FA), Continue (FB), Stop (FC), and Clock (F8)
- Clock resolution: 24 / 48 / 96 PPQ (default 96)
- Optional audio click on quarter-notes
- Stereo audio output (no audio input), produces MIDI output
- Alternate clock modes: CC pulses or Note pulses (for hosts that filter realtime)
- External Clock Out: optional virtual MIDI port ("ClockSync Out") duplicating realtime outside the host

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

cmake --build  --config Release
```

The resulting plugin (VST3 bundle) will be copied into the build output directory (and optionally to the system VST3 dir if configured by JUCE).

## Usage

- Load the plugin on an instrument track. Ensure your DAW allows MIDI out from instrument plugins (DAW-dependent).
- Use the UI to select clock resolution, enable audio click if desired, and set the click level.
- Choose an Output Mode:
	- Realtime: Sends FA/FB/FC/F8. Some DAWs filter these on tracks.
	- CC: Sends a short CC pulse per tick (configurable CC# and MIDI channel).
	- Note: Sends a short note-on/off per tick (configurable note and channel).
- External Clock Out:
	- Set to "Virtual Port" to create a macOS virtual MIDI destination named "ClockSync Out" that always carries realtime (Start/Continue/Stop/Clock) for external apps/devices.
	- Leave "Off" to disable the virtual port.
- Press Play in the DAW; the plugin will emit Start/Continue and then Clock messages aligned to the transport. On Stop, it emits Stop.

DAW routing caveats:
- Some hosts restrict VST3 MIDI output or require special routing to capture plugin MIDI (e.g., virtual MIDI ports or routing to another track).
- Many hosts (e.g., Ableton Live) filter realtime clock messages from plugins on tracks. Use CC/Note mode to route clock internally, or enable External Clock Out to send realtime to other apps/devices.
- The plugin must be placed on a track type that accepts instrument plugins to advertise MIDI output in many hosts.

## MIDI Clock Notes

- Clock pulses per quarter note (PPQ) can be 24 (classic), 48, or 96. Higher PPQ provides finer timing resolution but hosts may quantize timestamping per audio block.
- Start (FA) is sent when playback begins from the start; Continue (FB) is sent on resume from a non-zero position; Stop (FC) when playback stops.
- Song Position Pointer (F2) is not emitted by default, but can be added if needed.

## Swing / Shuffle

- Shuffle intensity has 7 discrete steps. Step 1 = straight; Step 7 = maximum.
- At normal MIDI clock rate (24 PPQN), each 1/8 note contains 12 pulses. Swing redistributes these pulses:
	- First 16th length = 0.25 + shiftQ
	- Second 16th length = 0.25 − shiftQ
	- For step `n` in [1..7], `shiftQ = (n - 1) * (1/48)` quarter-notes, so step 7 shifts the second half start from 0.25 to 0.375 PPQ.
- Changing shuffle while running is applied safely at the next straight 1/8 boundary; no restart or clock loss.

## Roadmap / TODO

- Curved swing response for steps 2–6 (perceptual mapping while keeping step 1/7 anchors).
- Classic swing mode that delays even 16ths (TR-style) without pulse redistribution.
- Option to offer constant 24 PPQN with pulse downsampling for alternate rates.

## License

This project uses JUCE (MIT/Commercial). Consult the JUCE license for details.
