#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
ClockSyncAudioProcessor::ClockSyncAudioProcessor()
    : juce::AudioProcessor(
#if JucePlugin_IsMidiEffect
          BusesProperties() // MIDI effect: no audio buses
#else
          BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true) // instrument: stereo out
#endif
      ),
      parameters(*this, nullptr, juce::Identifier("ClockSyncParams"), createParameterLayout())
{
}

void ClockSyncAudioProcessor::requestTriggerOnce()
{
    // Arm an immediate retrigger at the next 1/16 boundary; a bar restart will follow
    pendingRetrigger16th.store(true, std::memory_order_relaxed);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout ClockSyncAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        paramClockRateIndex, "Clock Rate", juce::StringArray{ "1/32", "1/16", "1/8", "1/4" }, 1));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        paramClickEnable, "Enable Audio Click", false));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramClickLevelDb, "Click Level (dB)", juce::NormalisableRange<float>(-12.0f, 0.0f), -6.0f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        paramRun, "Run", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        paramClockWhileStopped, "Clock While Stopped", false));

    return { params.begin(), params.end() };
}

//==============================================================================
void ClockSyncAudioProcessor::prepareToPlay(double sr, int /*samplesPerBlock*/)
{
    currentSampleRate = sr > 0.0 ? sr : 44100.0;
    lastWasPlaying = false;
    lastTickIndex = std::numeric_limits<long long>::min();
    pendingRateIndex = -1;
    currentRateIndex = 1; // 1/16 => normal speed (24)
    pendingStart = false;
    runActive = true;
    lastRunParam = 1;
    clickEnv = 0.0f;
    uiClockCounter.store(0, std::memory_order_relaxed);
    uiIsRunning.store(true, std::memory_order_relaxed);
    uiPendingStart.store(false, std::memory_order_relaxed);
    pendingRetrigger16th.store(false, std::memory_order_relaxed);
    pendingBarRestart.store(false, std::memory_order_relaxed);
    updateDerivedParams();
    updateExternalOut();
}

void ClockSyncAudioProcessor::releaseResources() {}

bool ClockSyncAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    // MIDI effect: no audio buses
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::disabled()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::disabled();
#else
    // Instrument: no audio input; allow mono or stereo output for host compatibility
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::disabled())
        return false;

    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return true;
#endif
}

//==============================================================================
int ClockSyncAudioProcessor::getClockResolution() const
{
    // Index mapping: 0->48 (1/32), 1->24 (1/16), 2->12 (1/8), 3->6 (1/4)
    switch (currentRateIndex)
    {
        case 0: return 48;
        case 1: return 24;
        case 2: return 12;
        case 3: return 6;
        default: return 24;
    }
}

void ClockSyncAudioProcessor::updateDerivedParams()
{
    const auto levelDb = parameters.getRawParameterValue(paramClickLevelDb)->load();
    clickGainLinear = juce::Decibels::decibelsToGain(levelDb);
}

void ClockSyncAudioProcessor::updateExternalOut()
{
    // Tear down existing
    if (externalMidiOut)
    {
        externalMidiOut->stopBackgroundThread();
        externalMidiOut.reset();
    }

    // Always use selected device if set
    if (externalDeviceId.isNotEmpty())
    {
        externalMidiOut = juce::MidiOutput::openDevice(externalDeviceId);
        if (externalMidiOut)
            externalMidiOut->startBackgroundThread();
    }
}

void ClockSyncAudioProcessor::setExternalDeviceId(const juce::String& id)
{
    if (externalDeviceId == id)
        return;

    externalDeviceId = id;
    // Persist into the state tree for recall
    parameters.state.setProperty("externalDeviceId", externalDeviceId, nullptr);
    // Reopen according to new device selection
    updateExternalOut();
}

//==============================================================================
void ClockSyncAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    // Clear buffer if we have audio outputs; we'll add click if enabled
    if (getTotalNumOutputChannels() > 0)
        buffer.clear();

    juce::AudioPlayHead::CurrentPositionInfo pos;
    bool hasPos = false;
    if (auto* ph = getPlayHead())
    {
        if (auto opt = ph->getPosition())
        {
            hasPos = true;
            pos.resetToDefault();

            if (auto sig = opt->getTimeSignature())
            {
                pos.timeSigNumerator   = sig->numerator;
                pos.timeSigDenominator = sig->denominator;
            }

            if (auto loop = opt->getLoopPoints())
            {
                pos.ppqLoopStart = loop->ppqStart;
                pos.ppqLoopEnd   = loop->ppqEnd;
            }

            if (auto frame = opt->getFrameRate())
                pos.frameRate = *frame;

            if (auto timeInSeconds = opt->getTimeInSeconds())
                pos.timeInSeconds = *timeInSeconds;

            if (auto lastBarStartPpq = opt->getPpqPositionOfLastBarStart())
                pos.ppqPositionOfLastBarStart = *lastBarStartPpq;

            if (auto ppqPosition = opt->getPpqPosition())
                pos.ppqPosition = *ppqPosition;

            if (auto originTime = opt->getEditOriginTime())
                pos.editOriginTime = *originTime;

            if (auto bpm = opt->getBpm())
                pos.bpm = *bpm;

            if (auto timeInSamples = opt->getTimeInSamples())
                pos.timeInSamples = (juce::int64) *timeInSamples;

            pos.isPlaying   = opt->getIsPlaying();
            pos.isRecording = opt->getIsRecording();
            pos.isLooping   = opt->getIsLooping();
        }
    }

    if (! hasPos)
    {
        // No transport info; just decay any existing click
        if (getTotalNumOutputChannels() > 0 && parameters.getRawParameterValue(paramClickEnable)->load() > 0.5f)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                float s = clickEnv;
                clickEnv *= 0.995f;
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    buffer.addSample(ch, i, s * clickGainLinear);
            }
        }
        return;
    }

    generateClockAndClick(pos, buffer, midi);
}

void ClockSyncAudioProcessor::generateClockAndClick(const juce::AudioPlayHead::CurrentPositionInfo& pos,
                                                    juce::AudioBuffer<float>& buffer,
                                                    juce::MidiBuffer& midi)
{
    const int numSamples = buffer.getNumSamples();

    // Update derived params (fast, read atomics)
    updateDerivedParams();
    const bool clickEnabled = parameters.getRawParameterValue(paramClickEnable)->load() > 0.5f;
    // Pull desired rate index from param and schedule for next bar if changed
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(parameters.getParameter(paramClockRateIndex)))
    {
        const int desired = choice->getIndex();
        if (desired != currentRateIndex)
            pendingRateIndex = desired;
    }
    // Capture the resolution at the start of this block (may change at bar boundary later)
    const int resolutionBefore = getClockResolution();

    // Run control param
    const bool runParam = parameters.getRawParameterValue(paramRun)->load() > 0.5f;
    const bool keepClockStopped = parameters.getRawParameterValue(paramClockWhileStopped)->load() > 0.5f;

    const double bpm = (pos.bpm > 0.0 ? pos.bpm : 120.0);
    samplesPerQuarter = currentSampleRate * 60.0 / juce::jmax(1e-6, bpm);

    const double ppqStart = pos.ppqPosition;
    const bool isPlaying = pos.isPlaying;

    // Robust run state handling: don't miss edges that occur between blocks
    if (! runParam)
    {
        if (runActive || pendingStart)
        {
            // Stop immediately and cancel any pending start
            runActive = false;
            pendingStart = false;
            uiIsRunning.store(false, std::memory_order_relaxed);
            uiPendingStart.store(false, std::memory_order_relaxed);
            const auto stopMsg = juce::MidiMessage::midiStop();
            midi.addEvent(stopMsg, 0);
            if (externalMidiOut)
            {
                juce::MidiBuffer ext;
                ext.addEvent(stopMsg, 0);
                externalMidiOut->sendBlockOfMessages(ext, juce::Time::getMillisecondCounterHiRes(), currentSampleRate);
            }
        }
    }
    else // runParam == true
    {
        if (! runActive && ! pendingStart)
        {
            // Arm a start at the next bar if we're currently stopped
            pendingStart = true;
            uiPendingStart.store(true, std::memory_order_relaxed);
        }
    }
    lastRunParam = runParam ? 1 : 0;

    // Transport state transitions: emit Start/Continue/Stop when host play toggles
    int hostStartSampleThisBlock = -1; // if host started this block, mark sample 0 as a boundary (we'll allow a clock on this frame)
    bool haveHostStartMsg = false;
    juce::MidiMessage hostStartMsg;
    if (isPlaying && ! lastWasPlaying)
    {
        // On play start, decide Start vs Continue based on position
        const bool atStart = (ppqStart < 1e-6);
        const auto startMsg = atStart ? juce::MidiMessage::midiStart() : juce::MidiMessage::midiContinue();
        midi.addEvent(startMsg, 0);
        // Defer external send and merge into the extClock buffer later to ensure same-frame Start+Clock ordering
        haveHostStartMsg = true;
        hostStartMsg = startMsg;

        // Ensure external clock runs when host starts
        runActive = true;
        pendingStart = false;
        uiIsRunning.store(true, std::memory_order_relaxed);
        uiPendingStart.store(false, std::memory_order_relaxed);

        // Suppress any clock at the exact sample where Start/Continue was sent
        hostStartSampleThisBlock = 0;
        // Force UI step ring to step 1 at transport start
        uiStep16.store(1, std::memory_order_relaxed);

        // Optional: could send Song Position Pointer here based on ppqStart
        // const int sppUnits = (int) juce::jmax(0, (int) std::floor(ppqStart * 4.0));
        // midi.addEvent(juce::MidiMessage::songPositionPointer(sppUnits), 0);
    }
    else if (! isPlaying && lastWasPlaying)
    {
        const auto stopMsg = juce::MidiMessage::midiStop();
        midi.addEvent(stopMsg, 0);
        if (externalMidiOut)
        {
            juce::MidiBuffer extStop;
            extStop.addEvent(stopMsg, 0);
            externalMidiOut->sendBlockOfMessages(extStop, juce::Time::getMillisecondCounterHiRes(), currentSampleRate);
        }
        // When host stops, also clear run-active state
        runActive = false;
        pendingStart = false;
        uiIsRunning.store(false, std::memory_order_relaxed);
        uiPendingStart.store(false, std::memory_order_relaxed);
    }

    // Generate clock ticks aligned to transport when playing
    if (isPlaying && std::isfinite(ppqStart))
    {
        const double ppqPerSample = 1.0 / samplesPerQuarter; // quarter-notes per sample
        // Update UI step (1..16) based on position within current bar.
        // If the next bar boundary falls within this block, force the UI to show step 1
        // so the visual starts at 12 o'clock on bar transitions that occur mid-block.
        const double lastBar = pos.ppqPositionOfLastBarStart;
        const double barLenQ = (pos.timeSigNumerator > 0 && pos.timeSigDenominator > 0)
            ? (4.0 * (double) pos.timeSigNumerator / (double) pos.timeSigDenominator)
            : 4.0;
        const double nextBar = lastBar + barLenQ;
        const double uiPpqEnd = ppqStart + (double) (numSamples - 1) * ppqPerSample;
        if (ppqStart < nextBar && nextBar <= uiPpqEnd)
        {
            uiStep16.store(1, std::memory_order_relaxed);
        }
        else
        {
            const double withinBar = juce::jlimit(0.0, barLenQ, ppqStart - lastBar);
            const double frac = (barLenQ > 0.0 ? withinBar / barLenQ : 0.0);
            int step = (int) std::floor(frac * 16.0) + 1;
            if (step < 1) step = 1; if (step > 16) step = 16;
            uiStep16.store(step, std::memory_order_relaxed);
        }

        // Helpers for computing tick indices under a given resolution
        auto tickAt = [](double ppq, int res) -> long long { return (long long) std::floor(ppq * (double) res); };

        // Handle immediate retrigger (1/16) and bar-aligned changes
        juce::MidiBuffer extClock;
        int gapStart = -1, gapEnd = -1, startSampleAtBoundary = -1;
        int gapStartImmediate = -1, gapEndImmediate = -1;
        int forbiddenClockSample = -1; // do not emit clock on this exact sample (Start sample)

        // Immediate retrigger while running: Start-only at the next 1/16 boundary (no Stop here)
        if (runActive && pendingRetrigger16th.load(std::memory_order_relaxed))
        {
            const double gridQ = 0.25; // 1/16 note
            const double eps = 1.0e-6;
            const double mod = std::fmod(ppqStart, gridQ);
            const bool onGrid = std::fabs(mod) <= eps || std::fabs(mod - gridQ) <= eps;
            const double nextGrid = onGrid ? ppqStart : (std::floor(ppqStart / gridQ) + 1.0) * gridQ;
            const double deltaQ = juce::jmax(0.0, nextGrid - ppqStart);
            const int retrigOffset = juce::jlimit(0, numSamples - 1, (int) std::llround(deltaQ * samplesPerQuarter));
            if (retrigOffset <= numSamples - 1)
            {
                // Place Start exactly on the computed 1/16 grid sample, so it aligns with the clock tick grid
                const int startOffset = retrigOffset;
                const auto startMsg = juce::MidiMessage::midiStart();
                midi.addEvent(startMsg, startOffset);
                extClock.addEvent(startMsg, startOffset);

                // Clear immediate flag and arm a bar restart
                pendingRetrigger16th.store(false, std::memory_order_relaxed);
                pendingBarRestart.store(true, std::memory_order_relaxed);
                // Do NOT forbid the clock at Start; we want Start to coincide with the grid clock
            }
        }

        handleBarAlignedChanges(pos, numSamples, midi, extClock, gapStart, gapEnd, startSampleAtBoundary);

        // If host just started this block (sample 0), mark the boundary at sample 0
        if (startSampleAtBoundary < 0 && hostStartSampleThisBlock >= 0)
            startSampleAtBoundary = hostStartSampleThisBlock;

        // Also merge host Start/Continue into extClock at sample 0 so the following clock can share the same frame
        if (haveHostStartMsg)
            extClock.addEvent(hostStartMsg, 0);

        // If a plugin-driven boundary Start requires suppression, set forbidden sample (not set for bar-restart/change-rate)
        if (suppressClockAtBoundaryOnce && startSampleAtBoundary >= 0)
            forbiddenClockSample = startSampleAtBoundary;

        const bool boundaryInBlock = (startSampleAtBoundary >= 0);
        const int resolutionAfter = getClockResolution(); // may change if rate applied at boundary

        // Ensure lastTickIndex is initialised once per stream
        const double ppqBlockEnd = ppqStart + (double) (numSamples - 1) * ppqPerSample;
        if (lastTickIndex == std::numeric_limits<long long>::min())
            lastTickIndex = tickAt(ppqStart, resolutionBefore) - 1;

        // Phase 1: ticks before the boundary (or whole block if no boundary)
        const int preEndSample = boundaryInBlock ? (startSampleAtBoundary - 1) : (numSamples - 1);
        if (preEndSample >= 0)
        {
            const double ppqPreEnd = ppqStart + (double) preEndSample * ppqPerSample;
            const long long tickIndexStartPre = tickAt(ppqStart, resolutionBefore);
            const long long tickIndexEndPre   = tickAt(ppqPreEnd, resolutionBefore);
            for (long long t = juce::jmax(lastTickIndex + 1, tickIndexStartPre); t <= tickIndexEndPre; ++t)
            {
                const double tickPPQ = (double) t / (double) resolutionBefore;
                const double deltaQuarter = tickPPQ - ppqStart;
                const int sampleOffset = juce::jlimit(0, numSamples - 1, (int) std::llround(deltaQuarter * samplesPerQuarter));
                const bool inGapBar = (gapStart >= 0 && sampleOffset >= gapStart && sampleOffset < gapEnd);
                const bool inGapImmediate = (gapStartImmediate >= 0 && sampleOffset >= gapStartImmediate && sampleOffset < gapEndImmediate);
                const bool onForbidden = (forbiddenClockSample >= 0 && sampleOffset == forbiddenClockSample);
                const bool inGap = inGapBar || inGapImmediate;
                if (! inGap && ! onForbidden)
                {
                    midi.addEvent(juce::MidiMessage::midiClock(), sampleOffset);
                    uiClockCounter.fetch_add(1, std::memory_order_relaxed);
                    const bool shouldSendExternal = runActive || (! runActive && keepClockStopped);
                    if (externalMidiOut && shouldSendExternal)
                        extClock.addEvent(juce::MidiMessage::midiClock(), sampleOffset);
                }
                if (clickEnabled && (t % resolutionBefore) == 0)
                    clickEnv = 1.0f;
            }
            lastTickIndex = tickIndexEndPre;
        }

        // Phase 2: ticks after the boundary using the (possibly) new resolution
        if (boundaryInBlock)
        {
            // Reset tick baseline to the boundary in "after" resolution so first tick lands exactly 1/res after Start
            const double boundaryPPQ = ppqStart + (double) startSampleAtBoundary * ppqPerSample;
            const long long baseIdx = tickAt(boundaryPPQ, resolutionAfter);
            if (suppressClockAtBoundaryOnce)
            {
                // Skip the clock at the exact Start frame; next tick will be at baseIdx + 1
                lastTickIndex = baseIdx;
            }
            else
            {
                lastTickIndex = baseIdx - 1;
            }

            const long long tickIndexStartPost = baseIdx;
            const long long tickIndexEndPost   = tickAt(ppqBlockEnd,  resolutionAfter);
            for (long long t = juce::jmax(lastTickIndex + 1, tickIndexStartPost); t <= tickIndexEndPost; ++t)
            {
                const double tickPPQ = (double) t / (double) resolutionAfter;
                const double deltaQuarter = tickPPQ - ppqStart;
                const int sampleOffset = juce::jlimit(0, numSamples - 1, (int) std::llround(deltaQuarter * samplesPerQuarter));
                // Past boundary; optionally suppress clock on the exact Start frame to avoid double clocks with Start
                if (!(forbiddenClockSample >= 0 && sampleOffset == forbiddenClockSample))
                {
                    midi.addEvent(juce::MidiMessage::midiClock(), sampleOffset);
                    uiClockCounter.fetch_add(1, std::memory_order_relaxed);
                    const bool shouldSendExternal = runActive || (! runActive && keepClockStopped);
                    if (externalMidiOut && shouldSendExternal)
                        extClock.addEvent(juce::MidiMessage::midiClock(), sampleOffset);
                }
                if (clickEnabled && (t % resolutionAfter) == 0)
                    clickEnv = 1.0f;
            }
            lastTickIndex = tickIndexEndPost;
            // Consume the one-shot suppression
            suppressClockAtBoundaryOnce = false;
        }

        if (externalMidiOut && extClock.getNumEvents() > 0)
            externalMidiOut->sendBlockOfMessages(extClock, juce::Time::getMillisecondCounterHiRes(), currentSampleRate);
    }

    // Audio click synthesis (simple decaying impulse)
    if (clickEnabled && getTotalNumOutputChannels() > 0)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float s = clickEnv;
            // Fast exponential decay for a sharp tick
            clickEnv *= 0.997f;

            s *= clickGainLinear;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.addSample(ch, i, s);
        }
    }

    lastWasPlaying = isPlaying;
}

void ClockSyncAudioProcessor::handleBarAlignedChanges(const juce::AudioPlayHead::CurrentPositionInfo& pos,
                                                      int numSamples,
                                                      juce::MidiBuffer& midi,
                                                      juce::MidiBuffer& extBuffer,
                                                      int& gapStartSample,
                                                      int& gapEndSample,
                                                      int& startSampleOut)
{
    const bool runWasActive = runActive; // capture current running state
    gapStartSample = -1;
    gapEndSample = -1;
    startSampleOut = -1;
    // Compute current position within bar and the sample offset to the next bar boundary
    const double lastBar = pos.ppqPositionOfLastBarStart;
    const double barLenQ = (pos.timeSigNumerator > 0 && pos.timeSigDenominator > 0)
        ? (4.0 * (double) pos.timeSigNumerator / (double) pos.timeSigDenominator)
        : 4.0;
    const double ppqStart = pos.ppqPosition;
    const double ppqSpan = (double) (numSamples - 1) / samplesPerQuarter;
    const double posInBar = juce::jlimit(0.0, barLenQ, ppqStart - lastBar);
    double remainQ = barLenQ - posInBar;
    if (remainQ < 0.0) remainQ += barLenQ; // safety
    int sampleOffset = (int) std::llround(remainQ * samplesPerQuarter);
    if (sampleOffset < 0) sampleOffset = 0;

    // Determine if a rate change, a plugin-initiated start, or a bar-restart is scheduled
    const bool haveRateChange = (pendingRateIndex >= 0);
    const bool haveStart = pendingStart;
    const bool haveBarRestart = pendingBarRestart.load(std::memory_order_relaxed);
    const double barEps = 1.0e-6;
    // Special case: if we're exactly at a bar start and either a Start or a rate-change is pending,
    // fire at sample 0. This covers the case where the boundary fell exactly at the end of the previous block.
    if ((haveStart || haveRateChange || haveBarRestart) && posInBar <= barEps)
        sampleOffset = 0;

    if (sampleOffset <= numSamples - 1)
    {
        // haveRateChange and haveStart already captured above

        // If either event is pending, schedule a small gap and stop/start sequence
        if (haveRateChange || haveStart || haveBarRestart)
        {
            // Define a musical gap: nearly 1/16th note before the bar boundary
            // 1/16 note = 0.25 quarter-notes. Subtract 1 sample to avoid rounding to same sample.
            const double gapPPQ = 0.25; // 1/16th note in quarters
            const int gapSamples = juce::jmax(1, (int) std::llround(gapPPQ * samplesPerQuarter) - 1);
            // sampleOffset already computed as the boundary sample within this block
            int stopOffset = juce::jmax(0, sampleOffset - gapSamples);
            // If rounding collapses stop/start to same sample, ensure separation
            if (stopOffset >= sampleOffset)
                stopOffset = juce::jmax(0, sampleOffset - 1);

            // Send Stop at the beginning of the gap if it precedes Start within this block
            if (stopOffset < sampleOffset)
            {
                const auto stopMsg = juce::MidiMessage::midiStop();
                midi.addEvent(stopMsg, stopOffset);
                extBuffer.addEvent(stopMsg, stopOffset);
            }

            // At the boundary, send Start (never Continue)
            const auto startMsg = juce::MidiMessage::midiStart();
            midi.addEvent(startMsg, sampleOffset);
            extBuffer.addEvent(startMsg, sampleOffset);
            // Force UI step ring to show step 1 when we start at the bar boundary
            uiStep16.store(1, std::memory_order_relaxed);

            // Apply pending changes at the boundary
            if (haveRateChange)
            {
                currentRateIndex = pendingRateIndex;
                pendingRateIndex = -1;
            }
            if (haveStart || haveBarRestart)
            {
                runActive = true;
                pendingStart = false;
                if (haveBarRestart)
                    pendingBarRestart.store(false, std::memory_order_relaxed);
                uiIsRunning.store(true, std::memory_order_relaxed);
                uiPendingStart.store(false, std::memory_order_relaxed);
            }

            // Communicate gap window to suppress clocks (up to the Start sample)
            gapStartSample = stopOffset;
            gapEndSample = sampleOffset;
            startSampleOut = sampleOffset;

            // If this was a direct Run-param Start while already running, we may suppress boundary clock.
            // Do NOT suppress for bar-restart or rate-change: we want Start+Clock on the same frame.
            if (runWasActive && haveStart && ! haveBarRestart && ! haveRateChange)
                suppressClockAtBoundaryOnce = true;
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* ClockSyncAudioProcessor::createEditor()
{
    return new ClockSyncAudioProcessorEditor(*this);
}

//==============================================================================
void ClockSyncAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    // Persist the selected external device id
    if (externalDeviceId.isNotEmpty())
        state.setProperty("externalDeviceId", externalDeviceId, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void ClockSyncAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        if (xml->hasTagName(parameters.state.getType()))
        {
            auto vt = juce::ValueTree::fromXml(*xml);
            parameters.replaceState(vt);
            // Restore external device id if present
            externalDeviceId = vt.getProperty("externalDeviceId").toString();
            updateDerivedParams();
            updateExternalOut();
        }
    }
}

//==============================================================================
// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ClockSyncAudioProcessor();
}
