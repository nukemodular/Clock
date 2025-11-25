# Clock Plugin Operational Workflow

This document summarizes the functional roles of UI indices (idx0–idx8), MIDI message semantics, timing grid, resync rules, and pattern sequencing behavior as specified.

## Core MIDI Messages
- 248 (Clock): Sent continuously after a Start (250) until a Stop (252). 24 ticks per quarter note; 6 ticks per 16th step ("grid unit"). Paused only when 252 (Stop) is sent. Exception: global IDLE ON (header-submenu) can suspend clocks.
- 250 (Start): Emitted (quantized) at the first tick of a 16th step. May be triggered by manual action, pattern start steps, or resync firing. When emitted and idx8 (gate resync) is TRUE, it automatically sets a resync flag (Rule 1) unless already pending. If idx8 is FALSE, Start still sends but does NOT create/allow resync (ignored gate).
- 252 (Stop): Halts clock (248) emission.

Terminology:
- tick: 1 MIDI clock pulse (message 248). There are 24 ticks per quarter note.
- step (16th): 6 ticks. Steps numbered 1–16 within a bar. Bars are counted starting at 1 (no bar 0).
- NOW: Current step number within the active bar (1–16).

## Counting & Synchronization
- At DAW transport Start: internal bar counter begins at Bar 1, Step 1.
- Steady progression: Steps 1→16 repeat each bar. Bar sync reference is always Step 1 of the bar.

## Pattern Sequencer
- 16 steps (1 bar). Active steps (set while editmode = idx2 ON) each emit Start (250) at the first tick of their step (one Start per active step) then clocks (248 continue).
- Steps toggled only while editmode (idx2) is ON.
- Interval / activation controlled by idx3 (pattern interval / BAR selection):
	* OFF: No pattern activity (active steps ignored for Start emissions).
	* 1 (Loop mode): Pattern runs every bar. SPECIAL: Pattern completion does NOT set a resync flag; resync from pattern is ignored until idx3 changes away from 1.
	* N (>1): Pattern executes ONLY on bars N, 2N, 3N, ... (each execution occupies exactly one bar). Between those bars pattern is inactive.
	* 32 (example large N): Waits N-1 bars then runs on the Nth; repeats.
	* RND: Pattern fires on randomly selected bars (one bar per selection). After firing, next random bar is picked; still respects mono resync rules.
- Completion resync: After the pattern bar finishes & idx8 TRUE & idx3 ≠ 1: schedule resync (subject to mono/shortest precedence). If idx8 FALSE or idx3 == 1: no resync from completion.
- When inactive (OFF or waiting for interval), pattern does not emit Start even if steps are marked active.

## Resync Mechanics
- Purpose: Schedule a Start (250) at the offset step for time realignment.
- next-bar target logic (offset from idx0, 1–16):
	* If offset step > NOW when flag set: target is that step in the current bar (shortest path).
	* If offset step <= NOW when flag set: target is that step in the next bar.
- Flag sources: Start events (Rule 1), rate changes (idx6 always sets), pattern completion (idx8 TRUE and idx3 ≠ 1). Manual trigger counts as a Start event and can set flag if idx8 TRUE.

Rules:
1. If 250 was sent and idx8 == TRUE, set (or attempt to update) resync flag.
2. Mono semantics: Only one pending flag may exist; new attempts can replace the target ONLY if they produce an earlier firing time than the current pending target (shortest precedence). Otherwise they are ignored.
3. Shortest path scheduling: Example: flag at NOW=10, offset=12 → fires this bar at step 12. Flag at NOW=14, offset=12 → fires next bar at step 12.

Lifecycle:
- (IDLE) → (PENDING[targetStep]) via qualifying event.
- While PENDING: further events may attempt earlier target replacement (Rule 2 precedence check).
- Fire: NOW == targetStep & idx8 TRUE → send 250, clear flag.
- Suppress: NOW == targetStep & idx8 FALSE → clear flag without 250.
- No retroactive firing; target computation fixed to shortest future occurrence.

## Offset Step (idx0)
- Big centered ring with selectable wedges 1–16: defines offset step used for resync target.
- Inner circle acts as Run button (transport start/stop). Run start sends 250 (quantized) and clocks; run stop sends 252.

## Manual Trigger (idx1)
- Medium ring showing current step. Clicking manually schedules a Start (250) quantized to the next grid step.
- Emission sequence for a manual Start: On the next step boundary, send 250 followed immediately (same tick) by the first clock 248 of that step, then remaining 5 clocks (total 6 ticks for the step).
- This manual 250 also sets resync flag if idx8 TRUE (Rule 1). If idx8 FALSE, no resync flag is set.

## Pattern Control (idx3)
- Governs interval logic described above (OFF, 1 loop, N recurring, RND random). Overrides any previous one-shot assumptions.

## Shuffle Intensity (idx4) & Mode (idx5)
- idx4: Shuffle amount. Either quantized discrete 1–7 (TR-909 style) or continuous linear 50%–75% timing bend.
- idx5: Toggle shuffle mode. ON = linear, OFF = 909 quantized. (Deferred details — not modifying for now.)
- Shuffle bends grid tick timing but fundamental count (6 ticks per step) reference remains. Adjusted clock dispatch timing must preserve quantization for Start at first tick of the step post-shuffle.

## Clock Rate (idx6)
- Selects resolution scaling: 32 (double tempo), 16 (normal), 8 (half), 4 (quarter).
- On ANY change of idx6 value: a resync flag is set unconditionally (even if idx8 FALSE). This ensures temporal realignment. (Note: Gate idx8 only influences firing at the target moment, not the setting.)

## Audio Click (idx7)
- Provides metronome choices: OFF, BEAT, 8TH, 16TH, 24ppq.
- Small button in center of idx8 toggles click length between 1 sample vs 1 ms pulse.

## Gate Resync (idx8)
- Boolean gate controlling whether pending resync flags are honored.
- If scheduled and gate == 0 at target step: resync ignored (flag cleared without sending 250).
- If gate == 1: resync fires (250) at target step.
- Also influences automatic setting of resync on Start events and pattern completion.

## Timing & Quantization Details
- Grid base: 6 ticks per 16th step. Start (250) always aligned to first tick of its destination step.
- Manual Start fusion: 250 + immediate first clock (248) at same tick, then remaining 5 clocks of the step.
- Pattern: Each active step emits Start (250) at its boundary (one per active step) then standard clocks.

## Edge Cases & Conflict Resolution
1. Multiple flag sources same bar: Shortest future target wins; later attempts only replace if earlier.
2. Offset wrap: If offset <= NOW when set, target scheduled for next bar (same step number).
3. Rate change vs existing flag: Apply shortest precedence (Rule 3); replaces only if new target earlier than current.
4. Gate OFF at target: flag consumed silently; no auto requeue.
5. Pattern empty: No Start from pattern; no completion resync source.
6. Loop mode (idx3=1): Ignores pattern completion resync; only other Start sources may set resync.
7. Random mode: Multiple random selections cannot stack in same bar; next selection occurs only after current random bar completes.

## State Machine (Resync Flag) [Textual]
States: IDLE → PENDING(targetStep) → (FIRE 250 | IGNORE) → IDLE
Transitions:
- IDLE → PENDING: Start event (250 & idx8 TRUE), idx6 rate change (always), pattern completion (idx8 TRUE)
- PENDING → FIRE: NOW == targetStep & idx8 TRUE → send 250, clear flag.
- PENDING → IGNORE: NOW == targetStep & idx8 FALSE → clear flag without 250.
- PENDING → PENDING (no change): Other events attempting set within same bar ignored (Rule 2).

## Current Clarification Status
All previously open points resolved per latest user specification:
- idx2 = editmode (step toggle grid), idx3 = pattern loop menu.
- Pattern steps emit Start (250).
- Offset wrap uses next bar when offset <= NOW.
- Rate change applies shortest precedence (may replace if earlier target).

