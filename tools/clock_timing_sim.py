#!/usr/bin/env python3
"""
clock_timing_sim.py

Simulate per-block rounding vs fractional-sample accumulator for scheduled PPQ events.

Usage:
  python3 tools/clock_timing_sim.py

This script prints comparison statistics and (optionally) dumps CSVs you can plot.

The simulation models:
- BPM and sample rate (maps PPQ -> samples)
- Events at step positions (e.g. 16th steps: eventPPQ = n * (1/16))
- DAW process blocks of varying sizes
- Optional shuffle that offsets every second step by a fraction of the step length (applied in PPQ)

It compares two strategies:
- per-block rounding: each event offset is rounded per-block (the typical approach that can accumulate error)
- accumulator: keeps a fractional-sample accumulator carried across events & blocks

"""

import math
import csv

# Simulation parameters
SAMPLE_RATE = 44100.0
BPM = 120.0
STEPS_PER_BAR = 16  # typical 16-step pattern
EVENTS_TO_SIMULATE = 2000  # number of events to schedule (will be overridden when using clocks_per_step)
BLOCK_SIZES = [64, 128, 256, 512]  # will cycle through these block sizes to simulate variable blocks
SHUFFLE_ENABLED = True
SHUFFLE_AMOUNT = 0.15  # fraction of step length to delay the 'off' step (0..0.5 recommended)
CLOCKS_PER_STEP = 6  # user specified: each step has 6 clocks (e.g. 24 PPQ -> 6 per step when step is 1/4?)

# Derived
samples_per_quarter = SAMPLE_RATE * 60.0 / BPM
step_ppq = 1.0 / STEPS_PER_BAR  # each step is this many quarter-notes


def generate_event_ppqs(num_events, step_ppq, shuffle=False, shuffle_amount=0.0):
    # This generator now supports multiple clocks per step.
    ppqs = []
    # num_events is number of steps when clocks_per_step > 1; we'll generate clocks_per_step events per step
    steps = num_events
    sub_ppq = step_ppq / CLOCKS_PER_STEP
    for step in range(steps):
        step_index = step % STEPS_PER_BAR
        base_ppq = step * step_ppq
        # simple shuffle: delay every second step in a pair (the 'even' steps are shuffled)
        step_add = 0.0
        if shuffle and (step_index % 2 == 1):
            step_add = step_ppq * shuffle_amount

        for sub in range(CLOCKS_PER_STEP):
            # first clock at sub==0 is the primary clock for the step
            ppqs.append(base_ppq + step_add + sub * sub_ppq)
    return ppqs


def simulate_per_block_rounding(ppqs, samples_per_quarter, block_sizes):
    events_samples = []
    block_start_sample = 0
    block_index = 0
    n = 0
    total_events = len(ppqs)

    while n < total_events:
        block_size = block_sizes[block_index % len(block_sizes)]
        block_len_q = (block_size / samples_per_quarter)  # block length in quarters
        block_start_ppq = (block_start_sample / samples_per_quarter)
        block_end_ppq = block_start_ppq + block_len_q

        # collect events inside this block
        while n < total_events and ppqs[n] < block_end_ppq:
            delta_q = ppqs[n] - block_start_ppq
            exact_offset_samples = delta_q * samples_per_quarter
            int_offset = int(round(exact_offset_samples))
            event_sample = block_start_sample + int_offset
            events_samples.append((n, ppqs[n], event_sample, exact_offset_samples))
            n += 1

        block_start_sample += block_size
        block_index += 1

        # Safety: if no events fell in block (possible for sparse events), still advance
        if block_start_sample > samples_per_quarter * 100000:
            break

    return events_samples


def simulate_accumulator(ppqs, samples_per_quarter, block_sizes):
    events_samples = []
    block_start_sample = 0
    block_index = 0
    n = 0
    total_events = len(ppqs)
    frac = 0.0  # fractional-sample accumulator

    while n < total_events:
        block_size = block_sizes[block_index % len(block_sizes)]
        block_len_q = (block_size / samples_per_quarter)  # block length in quarters
        block_start_ppq = (block_start_sample / samples_per_quarter)
        block_end_ppq = block_start_ppq + block_len_q
        block_start_samples = block_start_sample

        # collect events inside this block
        while n < total_events and ppqs[n] < block_end_ppq:
            exact_sample = ppqs[n] * samples_per_quarter
            exact_offset_from_block_start = exact_sample - block_start_samples
            offset_with_carry = exact_offset_from_block_start + frac
            int_offset = int(round(offset_with_carry))
            event_sample = block_start_samples + int_offset
            new_frac = offset_with_carry - int_offset
            events_samples.append((n, ppqs[n], event_sample, exact_sample, frac, new_frac))
            frac = new_frac
            n += 1

        block_start_sample += block_size
        block_index += 1

        # Safety guard
        if block_start_sample > samples_per_quarter * 100000:
            break

    return events_samples


def simulate_primary_locked_interpolated(ppqs, samples_per_quarter, block_sizes, clocks_per_step):
    """
    Primary-locked + interpolated secondaries.
    - Lock the first sub-clock (sub==0) of every odd-numbered step (1-based) as a primary by rounding its exact sample.
    - Place other sub-clocks relative to that primary by rounding their offset from the primary's exact time.
    - Ensure monotonic non-decreasing emitted samples by nudging secondaries forward if necessary.
    """
    total = len(ppqs)
    exact_samples = [ppq * samples_per_quarter for ppq in ppqs]
    emitted = [None] * total
    role = ["secondary"] * total

    steps = total // clocks_per_step

    # Mark primaries: first sub (sub==0) of steps 0,2,4,... (user requested steps 1,3,5.. as primaries)
    for step in range(steps):
        base = step * clocks_per_step
        if (step % 2) == 0:
            idx = base
            emitted[idx] = int(round(exact_samples[idx]))
            role[idx] = 'primary'

    # Fill remaining events
    last_emitted = -10**12
    for step in range(steps):
        base = step * clocks_per_step
        # primary exact for this step (may be None if non-priority step)
        primary_idx = base
        primary_exact = exact_samples[primary_idx]
        primary_emitted = emitted[primary_idx]

        for sub in range(clocks_per_step):
            idx = base + sub
            if emitted[idx] is not None:
                # enforce monotonicity
                if emitted[idx] <= last_emitted:
                    # nudge forward to keep order (avoid negative jumps)
                    emitted[idx] = last_emitted + 1
                last_emitted = emitted[idx]
                continue

            exact = exact_samples[idx]
            if primary_emitted is not None:
                # compute offset from primary exact and round relative to primary_emitted
                delta = exact - primary_exact
                candidate = primary_emitted + int(round(delta))
            else:
                # no primary for this step; fall back to per-event rounding
                candidate = int(round(exact))

            # enforce monotonicity (do not move primaries, only secondaries)
            if candidate <= last_emitted:
                candidate = last_emitted + 1

            emitted[idx] = candidate
            role[idx] = 'secondary'
            last_emitted = candidate

    # Build result list (index, ppq, emitted_sample, exact_sample, role)
    result = []
    for i, ppq in enumerate(ppqs):
        result.append((i, ppq, emitted[i], exact_samples[i], role[i]))
    return result


def analyze_mixed_results(ppq_list, mixed_results, clocks_per_step):
    # separate primary vs secondary diffs
    prim_diffs = []
    sec_diffs = []
    for (i, ppq, emitted, exact, role) in mixed_results:
        d = emitted - exact
        if role == 'primary':
            prim_diffs.append(d)
        else:
            sec_diffs.append(d)

    import statistics
    def stats(arr):
        return {
            'count': len(arr),
            'mean': statistics.mean(arr) if arr else 0.0,
            'stdev': statistics.pstdev(arr) if arr else 0.0,
            'min': min(arr) if arr else 0.0,
            'max': max(arr) if arr else 0.0,
            'abs_max': max(abs(x) for x in arr) if arr else 0.0,
        }

    return {'primary': stats(prim_diffs), 'secondary': stats(sec_diffs), 'all': stats(prim_diffs + sec_diffs)}


def analyze_results(ppq_list, per_block, accum):
    # Build exact sample doubles for each event
    exact_samples = [ppq * samples_per_quarter for ppq in ppq_list]

    # Map event index to emitted sample
    per_block_map = {i: s for (i, ppq, s, exact) in per_block}
    accum_map = {i: s for (i, ppq, s, exact, frac_in, frac_out) in accum}

    diffs_per_block = []
    diffs_accum = []

    for i, exact in enumerate(exact_samples):
        ideal_round = round(exact)
        pb = per_block_map.get(i)
        ac = accum_map.get(i)
        if pb is None or ac is None:
            # missing event (shouldn't happen here)
            continue
        diffs_per_block.append(pb - exact)
        diffs_accum.append(ac - exact)

    import statistics
    def stats(arr):
        return {
            'count': len(arr),
            'mean': statistics.mean(arr) if arr else 0.0,
            'stdev': statistics.pstdev(arr) if arr else 0.0,
            'min': min(arr) if arr else 0.0,
            'max': max(arr) if arr else 0.0,
            'abs_max': max(abs(x) for x in arr) if arr else 0.0,
        }

    return stats(diffs_per_block), stats(diffs_accum)


def main():
    print('Simulation parameters:')
    print(f'  sample_rate={SAMPLE_RATE}, bpm={BPM}, steps_per_bar={STEPS_PER_BAR}')
    print(f'  samples_per_quarter={samples_per_quarter:.6f}, step_ppq={step_ppq:.6f}')
    print(f'  events={EVENTS_TO_SIMULATE}, block_sizes={BLOCK_SIZES}')
    print(f'  shuffle={SHUFFLE_ENABLED}, shuffle_amount={SHUFFLE_AMOUNT}')

    # EVENTS_TO_SIMULATE is interpreted as number of steps; expand to per-subclock events
    steps_to_sim = EVENTS_TO_SIMULATE
    ppqs = generate_event_ppqs(steps_to_sim, step_ppq, shuffle=SHUFFLE_ENABLED, shuffle_amount=SHUFFLE_AMOUNT)

    per_block = simulate_per_block_rounding(ppqs, samples_per_quarter, BLOCK_SIZES)
    accum = simulate_accumulator(ppqs, samples_per_quarter, BLOCK_SIZES)
    mixed = simulate_primary_locked_interpolated(ppqs, samples_per_quarter, BLOCK_SIZES, CLOCKS_PER_STEP)

    stats_pb, stats_ac = analyze_results(ppqs, per_block, accum)
    stats_mixed = analyze_mixed_results(ppqs, mixed, CLOCKS_PER_STEP)

    print('\nResults (emitted_sample - exact_double_sample) in samples:')
    print('\nPer-block rounding stats:')
    for k, v in stats_pb.items():
        print(f'  {k}: {v}')

    print('\nAccumulator stats:')
    for k, v in stats_ac.items():
        print(f'  {k}: {v}')

    print('\nMixed (primary-locked + interpolated) stats:')
    for group, s in stats_mixed.items():
        print(f'  {group}:')
        for k, v in s.items():
            print(f'    {k}: {v}')

    print('\nInterpretation: means near 0 and smaller abs_max indicate better alignment with exact sample timeline.')

    # Optionally write a CSV
    try:
        with open('tools/sim_per_block.csv', 'w', newline='') as f:
            w = csv.writer(f)
            w.writerow(['idx', 'ppq', 'per_block_sample', 'per_block_exact_offset'])
            for (i, ppq, s, exact_offset) in per_block:
                w.writerow([i, ppq, s, exact_offset])
        with open('tools/sim_accum.csv', 'w', newline='') as f:
            w = csv.writer(f)
            w.writerow(['idx', 'ppq', 'accum_sample', 'exact_sample', 'frac_in', 'frac_out'])
            for (i, ppq, s, exact, frac_in, frac_out) in accum:
                w.writerow([i, ppq, s, exact, frac_in, frac_out])
        with open('tools/sim_mixed.csv', 'w', newline='') as f:
            w = csv.writer(f)
            w.writerow(['idx', 'ppq', 'mixed_sample', 'exact_sample', 'role'])
            for (i, ppq, s, exact, role) in mixed:
                w.writerow([i, ppq, s, exact, role])
        print('\nCSV output written to tools/sim_per_block.csv and tools/sim_accum.csv')
    except Exception as e:
        print('Could not write CSVs:', e)


if __name__ == '__main__':
    main()
