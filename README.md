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

## Quantized Start, Resync Offset & Run/Stop Behavior

The plugin schedules all Starts (whether from the Run toggle or a manual trigger in Trigger Mode) on a quantized boundary: the next bar at the EXACT selected Resync Offset Step (1..16). Selecting step 1 means the restart occurs at bar position 1 (no offset). Selecting step 3 means the restart occurs when the bar reaches 16th step 3 (or wraps to that step in the next bar if already passed). No "1 + offset" arithmetic is applied; the value selected is the absolute 1..16 target within a bar.

Key points:
- Resync Offset Step (idx6 popup in the playground UI) chooses which 1/16 slice inside the bar the next restart will land on. Step 1 = bar start; Step 5 = one quarter-note (4 sixteenths) into the bar, etc. (If the chosen step has already passed in the current bar, scheduling wraps to that step in the next bar.)
- When you click Run ON while transport is already playing, we arm a restart and freeze outgoing clock pulses until the scheduled boundary is reached. At that boundary a MIDI Stop (gap) then MIDI Start is emitted, and clock pulses resume aligned to the chosen step.
- When Run is OFF (Stop engaged) we immediately emit a MIDI Stop and suppress outgoing realtime Clock pulses, but we continue internal position & step tracking (using host transport position) so the next scheduled Start still quantizes correctly.
- Clock pulses are emitted ONLY while Run is ON. The Clock While Stopped toggle (idx5) allows Start/Stop messages to still be sent, but does not force clock pulses while stopped—this avoids external devices drifting.
- Trigger Mode (idx8) makes a manual trigger (circle idx1) schedule a full bar+offset restart instead of a plain grid-aligned Start. Manual triggers always wrap to the next bar, consuming any prior pending restart.
- Changing the offset step re-arms a bar restart (NEXT indicator) modulo within the current bar: if the new step lies ahead this bar it uses it; otherwise it wraps to the next bar.

Visual / chase light semantics:
- The ring wedges/chase light advance only while actually running (post Start boundary). Before the restart is applied (ARMED/PENDING) the wedge is frozen to show the latched state.
- After a restart the relative playhead resets so wedge numbering starts at logical step 1 again regardless of offset.

Diagnostics:
- Enable the Diagnostics parameter to log `[restart-pending]` lines (with sampleOffset and remaining delta) and a single `[restart-applied]` line when the boundary occurs. Extra `[restart-pending-remaining]` entries show the countdown in samples & quarter-notes.
- Swing pulse distribution pairs are logged as `pair N (pre/post)` entries for a limited capture window.

Edge cases & guarantees:
- If the offset step is 1 and Run is toggled during the first block of a bar, restart may apply immediately (deltaQ=0.0) and clocks resume without a visible freeze.
- Manual triggers while already pending simply update the restart target; only one restart boundary applies.
- Clock pulses are never emitted on the same sample as a MIDI Start when suppression is active (avoids double-clock at boundary).

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
