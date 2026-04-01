# Editor / Processor Integration Template

This is the minimal reusable wiring pattern.

## 1. Keep three layers separate

- `PatternStoreTemplate`: authoritative editable state on the message thread.
- `RuntimeSequencerTemplate`: plain runtime cache copied from the selected pattern for audio-thread use.
- `Editor`: writes edits immediately into the store and asks the processor to refresh runtime state when needed.

That split is the main reason the current project stays responsive while still behaving like autosave.

The helper files in this folder map directly to those layers:
- `PatternStoreTemplate.*`
- `RuntimeSequencerTemplate.h`
- `SlotControllerTemplate.h`
- `HostSyncShuffleTemplate.h`
- `JucePlayheadHostSyncTemplate.h`

## 2. Treat every edit as an immediate save

The project does not wait for a manual save button before persisting pattern edits. Grid edits write straight into the pattern store.

```cpp
gridSequencer.onCellValueChanged = [this](int visualRow, int absCol, int newValue)
{
    const int pattern = processor.getCurrentPatternIndex() - 1;
    const int track = mapVisualRowToStoreTrack(visualRow);

    processor.updateRuntimeCell(visualRow, absCol, newValue);
    patternStore.setStep(pattern, track, absCol, newValue);

    patternSlots.setSlotOccupied(processor.getCurrentPatternIndex(),
                                 patternStore.isPatternOccupied(pattern));
};
```

The same rule applies to track length, pattern shuffle, scale, flam, probability, and names:
- update the live processor/runtime state if audio needs the change now
- immediately mirror the same change into the pattern store

## 3. Slot recall should reapply runtime state immediately

Selecting a slot should do two things:
- switch the current selected pattern index
- copy that selected pattern from storage into the processor runtime cache

If you want the slot actions packaged instead of hand-wired, use `SlotControllerTemplate` as the UI-side helper.

```cpp
patternSlots.onSlotSelected = [this](int absOneBased)
{
    const int pattern = juce::jlimit(1, 64, absOneBased);

    processor.setCurrentPatternIndex(pattern);
    refreshGridFromCurrentPattern();
};
```

Inside the processor, keep that immediate copy explicit:

```cpp
void MyProcessor::setCurrentPatternIndex(int absOneBased) noexcept
{
    currentPatternIndex_ = juce::jlimit(1, 64, absOneBased);
    applyPatternToRuntime(currentPatternIndex_ - 1);
}
```

Equivalent with the helper:

```cpp
workflow_template::SlotControllerTemplate slots{patternStore};

patternSlots.onSlotSelected = [this, &slots](int absOneBased)
{
    slots.selectSlot(absOneBased, [this](int zeroBasedPattern)
    {
        processor.setCurrentPatternIndex(zeroBasedPattern + 1);
        refreshGridFromCurrentPattern();
    });
};
```

If you overwrite the currently active slot without changing selection, force a runtime reapply explicitly.

```cpp
void storeIntoSlot(int sourcePattern, int targetPattern)
{
    patternStore.setPatternData(targetPattern, patternStore.getPatternData(sourcePattern));

    if (targetPattern == processor.getCurrentPatternIndex() - 1)
        processor.applyPatternToRuntime(targetPattern);
}
```

## 4. Runtime state should be plain data

Do not sequence directly from `juce::ValueTree`.

```cpp
struct RuntimePatternState
{
    uint8_t steps[12][64]{};
    int lengths[12]{};
    int shuffle = 1;
    int scale = 3;
};

void MyProcessor::applyPatternToRuntime(int patternIndex)
{
    auto pattern = patternStore.getPatternData(patternIndex);

    const juce::ScopedLock sl(runtimeLock_);
    for (int row = 0; row < 12; ++row)
    {
        runtime_.lengths[row] = pattern.lengths[row];
        for (int step = 0; step < 64; ++step)
            runtime_.steps[row][step] = (uint8_t) juce::jlimit(0, 17, pattern.steps[row][step]);
    }

    runtime_.shuffle = pattern.shuffle;
    runtime_.scale = pattern.scale;
}
```

## 5. Slot occupancy rule

Use note data only.

Good:
- slot is occupied if any step value in the pattern is greater than zero

Avoid:
- treating name-only edits or metadata-only edits as “filled” patterns

That rule keeps slot visuals reliable across projects.

## 6. Persist the whole store as one subtree

The current project saves pattern state by attaching the pattern tree as a child of the plugin state root.

```cpp
void MyProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree root{"PLUGIN_STATE"};
    root.appendChild(apvts.copyState(), nullptr);
    root.appendChild(patternStore.copyState(), nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary(*xml, destData);
}

void MyProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        auto root = juce::ValueTree::fromXml(*xml);
        auto patterns = root.getChildWithName("PATTERNS");
        if (patterns.isValid())
            patternStore.restoreState(patterns);

        applyPatternToRuntime(getCurrentPatternIndex() - 1);
    }
}
```

## 7. Host sync with shuffle

Use `JucePlayheadHostSyncTemplate` together with `HostSyncShuffleTemplate` like this:

```cpp
workflow_template::HostSyncShuffleTemplate scheduler;
scheduler.setStepsPerBeat(stepsPerBeat);
scheduler.setShuffleAmount(runtime_.shuffle);
scheduler.setPhaseOffsetSteps(phaseOffset_);

workflow_template::JucePlayheadHostSyncTemplate::enumerateFromPlayhead(
    getPlayHead(),
    getSampleRate(),
    buffer.getNumSamples(),
    scheduler,
    [this](const workflow_template::ScheduledStepTemplate& step)
{
    const int wrappedStep = wrapToPatternLength((int) step.patternStep);
    triggerStepAtSampleOffset(wrappedStep, step.sampleOffset);

    if (step.atBarBoundary && pendingBarRealign_)
        phaseOffset_ = step.absoluteStep;
});
```

Important order:
1. derive step boundaries from host PPQ
2. decide whether the step is an offbeat
3. add shuffle delay in samples only after boundary detection
4. schedule the event inside the current block

That keeps sequencing deterministic and host-locked.