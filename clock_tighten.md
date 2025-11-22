# Clock tightening: primary-locked + interpolated secondaries

Status
- Created: 22 Nov 2025
- Purpose: capture the chosen strategy (prioritize precision for the first clock in each odd step) and the simulation evidence. We will revisit for a plugin patch later.

Goal
- Make the first clock of each odd step (steps 1,3,5,...15) as precise as possible.
- Steps 2,4,6,... are shuffled and are less critical — they can be scheduled with relaxed precision.
- Minimize audible/functional timing errors for the prioritized clocks while accepting relaxed behavior for secondary clocks.

Recommended approach (chosen)
- Primary-locked + interpolated secondaries (simple, robust):
  1. Treat each step as containing `CLOCKS_PER_STEP` sub-clocks (here: 6 clocks per step → 24 PPQ when step is quarter-note divided accordingly).
  2. For each step where we want the primary (user requested steps 1,3,5,...): compute the primary's exact absolute sample time `sample_exact = ppq_time * samplesPerQuarter` and round it to the nearest integer sample (or use clamped-accum for primaries if you want reduced drift while keeping per-event bound ≤ 0.5 samples).
  3. Emit primaries at the computed integer sample timestamps (these are "locked").
  4. Place the remaining sub-clocks inside the step relative to the primary:
     - compute their exact offset from the primary and round that offset (or interpolate between this primary and the next primary's integer sample), then convert to integer samples relative to the primary sample.
     - enforce monotonic non-decreasing emitted sample indices (nudge secondaries forward if a collision would occur). Do not let secondary placement alter primary samples.

Why this helps
- The large spikes we observed with a naive accumulator come from fractional carry pushing an event over the rounding threshold (near ±1 sample spikes). By isolating the primaries and rounding them independently, primaries cannot be affected by carry from preceding secondaries.
- Secondaries may show larger per-event deviations (they are less critical); these deviations won't propagate to future primaries.

Simulator results (run 22 Nov 2025)
- Simulation parameters used (default in `tools/clock_timing_sim.py`):
  - sample_rate = 44100 Hz
  - BPM = 120
  - steps_per_bar = 16
  - CLOCKS_PER_STEP = 6
  - EVENTS_TO_SIMULATE = 2000 steps → total sub-events = 2000 * 6 = 12000 scheduled clocks
  - shuffle enabled, shuffle_amount = 0.15
  - block sizes cycled: [64,128,256,512]

- Baselines:
  - Per-event rounding (per-block rounding) — abs_max = 0.5 samples (guaranteed by independent rounding)
  - Fractional accumulator (naive) — abs_max ≈ 0.96875 samples (occasionally ~1-sample spikes)

- Mixed (primary-locked + interpolated) results:
  - total events: 12000
  - primary clocks (first sub-clock of steps 1,3,5,...): count = 1000
    - mean error ≈ 0.0 samples
    - stdev ≈ 0.3063 samples
    - min = -0.5, max = 0.5, abs_max = 0.5
  - secondary clocks (all other sub-clocks): count = 11000
    - mean ≈ -0.0170 samples
    - stdev ≈ 0.3643 samples
    - min ≈ -0.9375, max ≈ 0.8125, abs_max ≈ 0.9375

Interpretation
- Primaries: we achieve per-event bounds identical to per-event rounding (±0.5 samples) — this meets the requirement to make these clocks as precise as possible under integer-sample scheduling.
- Secondaries: can incur larger single-event errors (up to ~0.94 samples in this run). That’s acceptable if the secondary (shuffled) clocks are not critical to the external gear.
- Overall: mixed approach eliminates the large spikes on primaries while preserving a relaxed scheduling strategy for secondaries.

Edge cases and implementation notes for plugin
- Reset points: when transport position changes, the host toggles play/stop, BPM or sample-rate changes, or when the pattern epoch changes, reset any accumulators and recompute primaries for the upcoming block.
- Block boundaries: if a primary sample falls before the current block start (negative offset), clamp to block offset 0 or emit at earliest possible offset, but do not corrupt frac state.
- Collisions: when interpolation gives the same integer sample for two events, prefer to preserve primaries and nudge secondaries (+1 sample) rather than shifting primaries.
- Clamped-accumulator variant: If you want to combine low long-term drift with per-event bounds, use a clamped accumulator for primaries only: carry fractional remainder but ensure no primary emission deviates more than ±0.5 by rebasing/clamping the rounding decision.
- Threading: compute scheduling data ahead (prepare/transport callbacks) and only perform minimal integer arithmetic on the audio thread; avoid heavy work in the audio callback.

Next steps (pick one when ready)
- Prototype plugin patch: implement primary-locked + interpolated secondaries in `Source/PluginProcessor.cpp` (small, audio-thread-safe changes; must reset on transport/tempo changes). I can prepare the patch when you say "patch plugin".
- Clamped primaries: implement clamped-accumulator for primaries in simulator and plugin to reduce long-term drift while keeping per-event bounds; I can run the sim to show stats.
- More testing: run the simulator with different BPMs, sample rates, shuffle amounts, and block-size patterns and collect CSVs/plots.

How to reproduce the sim locally

```bash
python3 tools/clock_timing_sim.py
# CSVs written: tools/sim_per_block.csv, tools/sim_accum.csv, tools/sim_mixed.csv
```

Notes
- This document is a short reference; when you want, I can turn the content into a more formal design doc with diagrams and example event lists, or I can directly patch the plugin.

— end of `clock_tighten.md` (created by Copilot assistant)
