#include "PluginProcessor.h"
#include "PluginEditor.h"






namespace {
    constexpr double kSixteenthQ = 0.25;      // 1/16th in quarter-notes
    constexpr double kBarEps     = 1.0e-6;    // bar-start epsilon

    // Fast rounding for known non-negative values (avoids std::llround overhead)
    inline int fastRoundPositive(double x) { return (int) (x + 0.5); }

    inline void addBoth(juce::MidiBuffer& mainBuf, juce::MidiBuffer& extBuf, const juce::MidiMessage& msg, int sample)
    {
        mainBuf.addEvent(msg, sample);
        extBuf.addEvent(msg, sample);
    }
    
    // Compute logical 1..16 step within a bar for an absolute PPQ position,
    // respecting the TR-909-style shuffle mapping when active.
    inline int computeLogicalStepFromPPQ(double absolutePPQ, double barLenQ, int shuffleStep)
    {
        if (!(barLenQ > 0.0)) return 1;
        double posInBar = std::fmod(absolutePPQ, barLenQ);
        if (posInBar < 0.0) posInBar += barLenQ;
        // Snap near-zero positions to exact bar start to avoid 1-sample rounding
        // producing step 2 for values that should be step 1.
        if (std::fabs(posInBar) < 1e-9)
            posInBar = 0.0;
        if (shuffleStep <= 1 || std::fabs(barLenQ - 4.0) > 1e-6)
        {
            int step = (int) std::floor((posInBar / barLenQ) * 16.0) + 1;
            if (step < 1) step = 1; if (step > 16) step = 16;
            return step;
        }
        // Build the shuffled positions for each logical step (0..15) and pick the closest.
        const double shiftQ = (double)(shuffleStep - 1) * (1.0 / 48.0);
        double bestDiff = 1e9; int bestIdx = 0;
        for (int logical = 0; logical < 16; ++logical)
        {
            int pair = logical / 2;
            bool second = (logical & 1) == 1;
            double pairStart = pair * 0.5;
            double stepPos = second ? (pairStart + 0.25 + shiftQ) : pairStart;
            double d = std::fabs(posInBar - stepPos);
            if (d < bestDiff)
            {
                bestDiff = d; bestIdx = logical;
            }
        }
        return bestIdx + 1;
    }

    // Compute absolute PPQ position for a logical 1..16 step inside the given bar.
    // Keeps the same TR-909-style shuffle mapping as used by computeLogicalStepFromPPQ.
    inline double computeStepPPQ(double barStartPPQ, double barLenQ, int logicalStep1to16, int shuffleStep)
    {
        if (logicalStep1to16 < 1) logicalStep1to16 = 1;
        if (logicalStep1to16 > 16) logicalStep1to16 = 16;
        const int idx = logicalStep1to16 - 1; // 0..15
        double posInBar;
        if (shuffleStep <= 1 || std::fabs(barLenQ - 4.0) > 1e-6)
        {
            posInBar = (barLenQ / 16.0) * (double) idx;
        }
        else
        {
            const double shiftQ = (double)(shuffleStep - 1) * (1.0 / 48.0);
            int pair = idx / 2;
            bool second = (idx & 1) == 1;
            double pairStart = pair * 0.5;
            posInBar = second ? (pairStart + 0.25 + shiftQ) : pairStart;
        }
        // snap tiny values to exact bar start for step 1
        if (std::fabs(posInBar) < 1e-9)
            posInBar = 0.0;
        return barStartPPQ + posInBar;
    }
}

//==============================================================================
ClockSyncAudioProcessor::ClockSyncAudioProcessor()
    : juce::AudioProcessor(
#if JucePlugin_IsMidiEffect
          BusesProperties() // MIDI effect: no audio buses
#elif JucePlugin_IsSynth
          // Synth/instrument: only output bus
          BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)
#else
          // Effect plugin (FX): provide both input and output buses
          BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
      ),
      parameters(*this, nullptr, juce::Identifier("ClockSyncParams"), createParameterLayout())
{
}

void ClockSyncAudioProcessor::requestTriggerOnce()
{
    // Original behaviour (clock_org): Arm Start at next 1/16 grid boundary (immediate retrigger).
    // Whether a bar+offset restart follows is decided later based on triggerModeEnabled.
    triggerArmedForNextSixteenth.store(true, std::memory_order_relaxed);
    // Manual trigger: allow bar restarts immediately
    deferBarRestartUntilPattern.store(false, std::memory_order_relaxed);
    // diagnostics logging removed per user request
}

void ClockSyncAudioProcessor::setTriggerModeEnabled(bool enabled)
{
    triggerModeEnabled.store(enabled, std::memory_order_relaxed);
    // When disabling the trigger-mode gate, clear any pending restart/resync
    // so idx8 acts as the master gate for resync behaviour.
    if (! enabled)
    {
        pendingBarRestart.store(false, std::memory_order_relaxed);
        uiNextRestartPending.store(false, std::memory_order_relaxed);
        // Clear any already-scheduled resync target
        resyncPending.store(false, std::memory_order_relaxed);
        resyncTargetBar.store(-1, std::memory_order_relaxed);
        resyncTargetStep.store(-1, std::memory_order_relaxed);
    }
}

void ClockSyncAudioProcessor::notifyResyncOffsetChanged()
{
    // Only schedule a pending bar restart if the trigger-mode gate (idx8)
    // is enabled; otherwise treat the offset change as non-actionable.
    if (triggerModeEnabled.load(std::memory_order_relaxed))
    {
        pendingBarRestart.store(true, std::memory_order_relaxed);
        uiNextRestartPending.store(true, std::memory_order_relaxed);
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout ClockSyncAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        paramClockRateIndex, "Clock Rate", juce::StringArray{ "1/32", "1/16", "1/8", "1/4" }, 1));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramClickLevelDb, "Click Level (dB)", juce::NormalisableRange<float>(-12.0f, 0.0f), -6.0f));

    // Click rate: discrete stages 0..4 (0 = off). Default changed to 0 so rotary starts at OFF.
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        paramClickRate, "Click Rate", 0, 4, 0));

    // Pulse width (ms): 1–20, automatable
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        "pulseWidthMs", "Pulse Width (ms)", 1, 20, 1));

    // Click variant: false = 1-sample spike, true = 1ms pulse
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        paramClickPulse, "Click Pulse Variant", false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        paramRun, "Run", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        paramClockWhileStopped, "Clock While Stopped", false));

    // Trigger mode: when enabled, a manual trigger also schedules a bar/offset restart
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        paramTriggerModeEnabled, "Trigger Mode", true));

    // Step offset within the bar where restarts occur (1..16). 1 means at bar start; 5 means 4/16 into next bar.
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        paramResyncOffsetStep, "Resync Offset Step", 1, 16, 1));

    // Shuffle (swing) intensity: 1..7 (TR-909 style). 1 = none, 7 = maximum (2nd 16th shifted to next 32nd).
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        paramShuffleStep, "Shuffle Step", 1, 7, 1));



    // Pattern bars choice: OFF,1,2,4,8,16,32,64,RND (store as indices 0..8)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        paramPatternBars, "Pattern Bars", juce::StringArray{ "OFF","1","2","4","8","16","32","64","RND" }, 0));
    // Pattern steps bitmask (0..65535) persisted; editor updates via setPatternSteps
    // Default changed to 0x0000 so pattern starts fully empty; previously 0x0010 enabled storedIndex=4 (logical step 1) implicitly.
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        paramPatternSteps, "Pattern Steps Mask", 0, 65535, 0x0000)); // default empty, user must activate steps explicitly

    // Pattern start fine-tune parameter removed

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
    // Default: armed but not running; will emit Start at scheduled offset step (or bar start)
    pendingStart = true;
    runActive = false;
    // No forced wrap to next bar in original behaviour
    forceNextBarStart = false;
    // removed unused lastRunParam
    clickEnv = 0.0f;
    uiClockCounter.store(0, std::memory_order_relaxed);
    uiIsRunning.store(false, std::memory_order_relaxed);
    uiPendingStart.store(true, std::memory_order_relaxed);
    uiNextRestartPending.store(false, std::memory_order_relaxed);
    triggerArmedForNextSixteenth.store(false, std::memory_order_relaxed);
    pendingBarRestart.store(false, std::memory_order_relaxed);
    // Initial trigger mode from parameter
    if (auto* triggerVal = parameters.getRawParameterValue(paramTriggerModeEnabled))
    {
        const bool triggerMode = triggerVal->load() > 0.5f;
        triggerModeEnabled.store(triggerMode, std::memory_order_relaxed);
        lastTriggerModeEnabled = triggerMode;
    }
    else
    {
        triggerModeEnabled.store(true, std::memory_order_relaxed);
        lastTriggerModeEnabled = true;
    }

    // Pulse width param pointer (raw, not unique_ptr)
    pulseWidthParam = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter("pulseWidthMs"));
    if (pulseWidthParam)
        pulseWidthMs.store(pulseWidthParam->get(), std::memory_order_relaxed);

    suppressUntilRestart = false;
    firstBlock = true;
    if (auto* pi = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter(paramResyncOffsetStep)))
        lastOffsetStep = juce::jlimit(1, 16, pi->get());
    if (auto* si = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter(paramShuffleStep)))
        currentShuffleStep = juce::jlimit(1, 7, si->get());
    pendingShuffleStep = -1;
    applyShuffleAtPPQ = -1.0;

    updateDerivedParams();
    updateExternalOut();
    // Reset persistent accumulators on prepare (sample-rate change / reinitialise)
    resetAccumulators();
}

void ClockSyncAudioProcessor::releaseResources() {}

bool ClockSyncAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    // MIDI effect: no audio buses
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::disabled()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::disabled();
#elif JucePlugin_IsSynth
    // Synth/instrument: no audio input; allow mono or stereo output
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::disabled())
        return false;

    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return true;
#else
    // Effect: require matching input/output channel layouts (mono or stereo)
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (in == juce::AudioChannelSet::disabled() || out == juce::AudioChannelSet::disabled())
        return false;

    if (in != out)
        return false;

    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;

    return true;
#endif
}

int ClockSyncAudioProcessor::getClockResolution() const
{
    // Map current rate index (choice param) to pulses-per-quarter note.
    // Index mapping (clockRateIndex):
    // 0 => 1/32 (double speed)  => 48 PPQN
    // 1 => 1/16 (normal speed)  => 24 PPQN
    // 2 => 1/8  (half speed)    => 12 PPQN
    // 3 => 1/4  (quarter speed) => 6 PPQN
    switch (currentRateIndex)
    {
        case 0: return 48;
        case 1: return 24;
        case 2: return 12;
        case 3: return 6;
        default: return 24;
    }
}

// void ClockSyncAudioProcessor::debugLog(const juce::String& s)
// {
//     juce::File f = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
//                         .getChildFile("ClockSync_Log.txt");
//     if (auto stream = f.createOutputStream(1024))
//     {
//         stream->setPosition(stream->getFile().getSize());
//         stream->writeText(s + "\n", false, false, "UTF-8");
//     }
// }

void ClockSyncAudioProcessor::updateDerivedParams()
{
    const auto levelDb = parameters.getRawParameterValue(paramClickLevelDb)->load();
    if (! std::isfinite(lastClickLevelDbCached) || levelDb != lastClickLevelDbCached)
    {
        clickGainLinear = juce::Decibels::decibelsToGain(levelDb);
        lastClickLevelDbCached = levelDb;
    }
    // Mirror trigger mode parameter into fast atomic for RT checks
    if (auto* tv = parameters.getRawParameterValue(paramTriggerModeEnabled))
        triggerModeEnabled.store(tv->load() > 0.5f, std::memory_order_relaxed);

    updatePatternParams();
}

void ClockSyncAudioProcessor::resetAccumulators()
{
    fracAccPrimaryBefore = 0.0;
    fracAccPrimaryAfter = 0.0;
    lastBpmForAcc = -1.0;
}

bool ClockSyncAudioProcessor::isDuplicateStart(long long barForStart, int stepForStart, int sampleOffset, int toleranceSamples) const
{
    // Exact bar+step duplicate
    if (barForStart == lastStartBar.load(std::memory_order_relaxed) && stepForStart == lastStartStep.load(std::memory_order_relaxed))
        return true;
    // Per-block near-simultaneous emission tolerance (collapse duplicates within a few samples)
    if (sampleOffset >= 0)
    {
        if (lastPatternStartSampleInBlock >= 0 && std::abs(sampleOffset - lastPatternStartSampleInBlock) <= toleranceSamples)
            return true;
        if (lastBarRestartStartSampleInBlock >= 0 && std::abs(sampleOffset - lastBarRestartStartSampleInBlock) <= toleranceSamples)
            return true;
    }
    return false;
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
void ClockSyncAudioProcessor::setPulseWidthMs(int ms)
{
    if (pulseWidthParam)
        pulseWidthParam->setValueNotifyingHost((float)juce::jlimit(1, 20, ms) / 20.0f);
}

int ClockSyncAudioProcessor::getPulseWidthMs() const
{
    if (pulseWidthParam)
        return pulseWidthParam->get();
    return pulseWidthMs.load(std::memory_order_relaxed);
}
void ClockSyncAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    // Preserve incoming audio (passthrough). Only clear channels that have
    // no corresponding input (e.g. mismatched layouts) — current layout policy requires match.
    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    // If host gives mono in -> stereo out (future-proof), duplicate channel 0.
    if (numIn == 1 && numOut > 1)
    {
        for (int ch = 1; ch < numOut; ++ch)
            buffer.copyFrom(ch, 0, buffer, 0, 0, numSamples);
    }
    // If there are extra output channels beyond inputs, clear them to silence.
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear(ch, 0, numSamples);

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
            // Cache host-provided bar number for use by the UI/editor without
            // calling getPlayHead() from the message thread (avoid race with ScopedPlayHead).
            {
                int barNumber = -1;
                if (auto barOpt = opt->getBarCount())
                {
                    barNumber = (int) *barOpt;
                }
                else if (auto ppqBarStartOpt = opt->getPpqPositionOfLastBarStart())
                {
                    double ppqBarStart = *ppqBarStartOpt; // quarter-note units
                    double quartersPerBar = 4.0; // default assume 4/4
                    if (auto tsOpt = opt->getTimeSignature())
                    {
                        auto ts = *tsOpt;
                        if (ts.denominator > 0)
                            quartersPerBar = ts.numerator * (4.0 / (double) ts.denominator);
                    }
                    if (quartersPerBar > 0.0)
                    {
                        double barIndexD = std::floor(ppqBarStart / quartersPerBar);
                        barNumber = 1 + (int) std::max(0.0, barIndexD); // 1-based
                    }
                }
                uiExternalBarNumber.store(barNumber, std::memory_order_relaxed);
            }
            // Cache time signature and last bar start PPQ for UI/diagnostics (atomics for message-thread access)
            uiTimeSigNumerator.store(pos.timeSigNumerator, std::memory_order_relaxed);
            uiTimeSigDenominator.store(pos.timeSigDenominator, std::memory_order_relaxed);
            uiPpqPositionOfLastBarStart.store(pos.ppqPositionOfLastBarStart, std::memory_order_relaxed);
        }
    }

    if (! hasPos)
    {
        // No transport info; nothing to schedule for audio clicks.
        clickEnv = 0.0f;
        // Clear cached external bar number when no host transport info is available
        uiExternalBarNumber.store(-1, std::memory_order_relaxed);
        return;
    }

    generateClockAndClick(pos, buffer, midi);
}

void ClockSyncAudioProcessor::generateClockAndClick(const juce::AudioPlayHead::CurrentPositionInfo& pos,
                                                    juce::AudioBuffer<float>& buffer,
                                                    juce::MidiBuffer& midi)
{
    const int numSamples = buffer.getNumSamples();
    // Apply any carried-over pulse tail from the previous block at the start of this block.
    if (clickHoldRemainingSamples > 0)
    {
        const int carry = juce::jmin(clickHoldRemainingSamples, numSamples);
        for (int s = 0; s < carry; ++s)
        {
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.addSample(ch, s, 1.0f);
        }
        clickHoldRemainingSamples -= carry;
    }
    // Clear per-block pattern/bar-start markers
    lastPatternStartSampleInBlock = -1;
    lastBarRestartStartSampleInBlock = -1;
    // Update derived params (fast, read atomics)
    updateDerivedParams();

    // Update runActive to reflect the current Run parameter value
    runActive = parameters.getRawParameterValue(paramRun)->load() > 0.5f;


    // Centralized external MIDI buffer: collect all external messages here and send once at function end.
    juce::MidiBuffer extClock;

    // Rising edge immediate pattern scheduling: when user activates first step (mask from 0->non-zero)
    // and interval mode is active, prepare current bar and attempt in-block emission so they hear
    // pattern Starts without waiting for next interval bar.
    if (patternMaskJustActivated.load(std::memory_order_relaxed))
    {
        patternMaskJustActivated.store(false, std::memory_order_relaxed);
        const int mode = patternBarsMode.load(std::memory_order_relaxed);
        if (mode > 0) // interval/RND active
        {
            const double barLenQ = (pos.timeSigNumerator > 0 && pos.timeSigDenominator > 0)
                ? (4.0 * (double) pos.timeSigNumerator / (double) pos.timeSigDenominator)
                : 4.0;
            const double barStartPPQ = pos.ppqPositionOfLastBarStart;
            preparePatternForBar(barStartPPQ, barLenQ);
            tryFirePattern(pos.ppqPosition, barLenQ, barStartPPQ, numSamples, midi, extClock, true, /*preview=*/true);

        }
    }

    // --- Cache parameter reads to avoid repeated dynamic_casts and lookups in inner loops ---
    auto* clickRateParam = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter(paramClickRate));
    const int clickRateCached = clickRateParam ? juce::jlimit(0, 4, clickRateParam->get()) : 0;
    const bool clickEnabled = (clickRateCached > 0);
    const bool clickPulseCached = parameters.getRawParameterValue(paramClickPulse)->load() > 0.5f;

    // Only gate click (audio pulse) output by runActive (Run button ON)


    auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*>(parameters.getParameter(paramClockRateIndex));
    if (choiceParam)
    {
        const int desired = choiceParam->getIndex();
        if (desired != currentRateIndex && pendingRateIndex != desired)
        {
            pendingRateIndex = desired;
            uiNextRestartPending.store(true, std::memory_order_relaxed);
            // Schedule a realign (one-shot Start at next bar start) on rate change instead of emitting a rateChange Start.
            // This ensures external devices re-sync cleanly without producing a non-pattern Start inside current bar.
            const double barLenQLoc = (pos.timeSigNumerator > 0 && pos.timeSigDenominator > 0)
                ? (4.0 * (double) pos.timeSigNumerator / (double) pos.timeSigDenominator)
                : 4.0;
            long long curBar = computeCurrentBar(pos.ppqPosition, barLenQLoc);
            // Guard: only schedule realign on rate change after at least one pattern firing and mask non-empty
            const uint16_t maskNow = patternStepsMask.load(std::memory_order_relaxed);
            if (lastPatternFiredBar >= 0 && maskNow != 0)
            {
                forceNextBarStart = true;
                uiNextRestartPending.store(true, std::memory_order_relaxed);

            }
        }
    }

    // Capture the resolution (locked to 24 for MIDI clock)
    const int resolutionBefore = getClockResolution();

    // Run control param
    const bool runParamCached = parameters.getRawParameterValue(paramRun)->load() > 0.5f;
    const bool keepClockStoppedCached = parameters.getRawParameterValue(paramClockWhileStopped)->load() > 0.5f;
    bool allowMidiOut = runParamCached || keepClockStoppedCached;

    

    // Detect run-param transitions (true -> false) so we can emit an immediate
    // MIDI Stop to external devices when user turns Run off. This makes the
    // UI Run toggle reliably stop external drum machines even if host
    // transport/clock settings would otherwise keep running.
    if (lastRunParam != runParamCached)
    {
        if (! runParamCached)
        {
            // Send an immediate Stop message at the start of this block.
            const auto stopMsg = juce::MidiMessage::midiStop();
            midi.addEvent(stopMsg, 0);
            // Queue Stop for external output; will be flushed once at end of this function.
            extClock.addEvent(stopMsg, 0);
            // Clear running state
            runActive = false;
            pendingStart = false;
            uiIsRunning.store(false, std::memory_order_relaxed);
            uiPendingStart.store(false, std::memory_order_relaxed);
        }
        // Keep original behaviour on Run==true: arm a pendingStart handled later in the main logic.
        lastRunParam = runParamCached;
    }

    const double bpm = (pos.bpm > 0.0 ? pos.bpm : 120.0);
    // If BPM changed since last block, conservatively reset accumulators to avoid stale carry.
    if (lastBpmForAcc < 0.0)
        lastBpmForAcc = bpm;
    else if (std::fabs(bpm - lastBpmForAcc) > 1e-6)
    {
        resetAccumulators();
        lastBpmForAcc = bpm;
    }
    uiBpm.store(bpm, std::memory_order_relaxed);
    samplesPerQuarter = currentSampleRate * 60.0 / juce::jmax(1e-6, bpm);

    const double ppqStart = pos.ppqPosition;
    bool isPlaying = pos.isPlaying;



    // Ensure we allow MIDI out when the host is playing so external gear follows the DAW
    allowMidiOut = allowMidiOut || isPlaying;

    // Robust run state handling: don't miss edges that occur between blocks
    if (! runParamCached)
    {
        if (runActive || pendingStart)
        {
            // Stop immediately and cancel any pending start
            runActive = false;
            pendingStart = false;
            uiIsRunning.store(false, std::memory_order_relaxed);
            uiPendingStart.store(false, std::memory_order_relaxed);
            // When user has Run==off, suppress sending any MIDI stop messages.
        }
    }
    else // runParamCached == true
    {
        if (! runActive && ! pendingStart)
        {
            // Arm a start at the next bar if we're currently stopped
            pendingStart = true;
            uiPendingStart.store(true, std::memory_order_relaxed);
        }
    }
    // removed unused lastRunParam

    // Trigger mode rising edge: when enabling the trigger-mode gate (idx8),
    // schedule a bar+offset restart (resync). This should happen when the
    // gate is switched ON; turning it OFF does not schedule a resync.
    {
        const bool tmNow = triggerModeEnabled.load(std::memory_order_relaxed);
        if (tmNow && ! lastTriggerModeEnabled)
        {
            // Show NEXT indicator and request a bar restart
            pendingBarRestart.store(true, std::memory_order_relaxed);
            uiNextRestartPending.store(true, std::memory_order_relaxed);

            // Compute current bar and step to schedule a resync target.
            int offsetStep = 1;
            if (auto* pi = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter(paramResyncOffsetStep)))
                offsetStep = juce::jlimit(1, 16, pi->get());
            const double barLenQNow = (pos.timeSigNumerator > 0 && pos.timeSigDenominator > 0)
                ? (4.0 * (double) pos.timeSigNumerator / (double) pos.timeSigDenominator)
                : 4.0;
            long long curBar = computeCurrentBar(ppqStart, barLenQNow);
            int currentStepApprox = juce::jlimit(1, 16, uiStep16.load(std::memory_order_relaxed));

            // Attempt to schedule a resync target (non-force; obey replacement rules).
            attemptScheduleResync(curBar, currentStepApprox, offsetStep, false);
        }
        lastTriggerModeEnabled = tmNow;
    }

    // Transport state transitions: emit Start/Continue/Stop when host play toggles
    int hostStartSampleThisBlock = -1; // if host started this block, mark sample 0 as a boundary (we'll allow a clock on this frame)
    bool hostForcedAllowMidiOut = false; // when host START occurs we temporarily allow MIDI clocks even if Run param is off
    bool haveHostStartMsg = false;
    juce::MidiMessage hostStartMsg;
    if (isPlaying && ! lastWasPlaying)
    {
        // Distinguish plugin load mid-transport vs genuine transport start edge.
        const double lastBarLocal = pos.ppqPositionOfLastBarStart;
        const double barLenQLocal = (pos.timeSigNumerator > 0 && pos.timeSigDenominator > 0)
            ? (4.0 * (double) pos.timeSigNumerator / (double) pos.timeSigDenominator)
            : 4.0;
        const double posInBar = juce::jlimit(0.0, barLenQLocal, ppqStart - lastBarLocal);
        int initialStep = (int) std::floor((barLenQLocal > 0.0 ? (posInBar / barLenQLocal) : 0.0) * 16.0) + 1;
        if (initialStep < 1) initialStep = 1; if (initialStep > 16) initialStep = 16;
        // If the plugin was created while the host was already mid-bar (firstBlock),
        // suppress emitting an immediate Start/Continue. Instead arm a pending start
        // so the engine will align at the configured offset without producing an
        // extra Start message that can desync multi-instance setups.
        const bool suppressStartOnLoad = firstBlock && initialStep > 1;
        if (suppressStartOnLoad)
        {
            // Arm restart at next configured offset step and suppress clocks until applied.
            pendingStart = true;
            runActive = false;
            uiIsRunning.store(false, std::memory_order_relaxed);
            uiPendingStart.store(true, std::memory_order_relaxed);
            uiNextRestartPending.store(true, std::memory_order_relaxed);
            suppressUntilRestart = true;
            firstBlock = false;
        }
        else
        {
            const bool atStart = (ppqStart < 1e-6);
            const auto startMsg = atStart ? juce::MidiMessage::midiStart() : juce::MidiMessage::midiContinue();
            // Coalesced start: host start always allowed; record bar/step for duplicate suppression.

            lastStartBar.store(computeCurrentBar(ppqStart, barLenQLocal), std::memory_order_relaxed);
            lastStartStep.store(1, std::memory_order_relaxed);
            midi.addEvent(startMsg, 0);
            haveHostStartMsg = true;
            hostStartMsg = startMsg;
            // Ensure clocks are allowed this block even if the user Run param is off.
            hostForcedAllowMidiOut = true;
            runActive = true;
            pendingStart = false;
            uiIsRunning.store(true, std::memory_order_relaxed);
            uiPendingStart.store(false, std::memory_order_relaxed);
            hostStartSampleThisBlock = 0;
            uiStep16.store(1, std::memory_order_relaxed);
            firstBlock = false;
            // Reset accumulators on a host transport START
            resetAccumulators();
            // Notify UI/editor that a host START edge occurred so UI can reset
            uiHostStartPending.store(true, std::memory_order_relaxed);
            // Reset internal pattern scheduling state so intervals realign with host bars
            lastPatternFiredBar = -1;
            nextRandomPatternTargetBar = -1;
            lastPatternRestartScheduledBar = -1;
            pendingPatternRestartTargetBar.store(-1, std::memory_order_relaxed);
            // Defer auto bar restarts until first pattern interval fires (prevents an early bar Start before intended interval)
            const int modeAtStart = patternBarsMode.load(std::memory_order_relaxed);
            deferBarRestartUntilPattern.store(modeAtStart > 0, std::memory_order_relaxed);
        }
    }
    else if (! isPlaying && lastWasPlaying)
    {
        const auto stopMsg = juce::MidiMessage::midiStop();
        if (allowMidiOut || hostForcedAllowMidiOut)
        {
            midi.addEvent(stopMsg, 0);
            // queue external stop for one-shot flush at end
            extClock.addEvent(stopMsg, 0);
        }
        // When host stops, also clear run-active state
        runActive = false;
        pendingStart = false;
        uiIsRunning.store(false, std::memory_order_relaxed);
        uiPendingStart.store(false, std::memory_order_relaxed);
        // Reset accumulators on host STOP
        resetAccumulators();
    }

    // Generate clock ticks aligned to transport when playing
    if (isPlaying && std::isfinite(ppqStart))
    {
        const double ppqPerSample = 1.0 / samplesPerQuarter; // quarter-notes per sample
        const auto clockMsg = juce::MidiMessage::midiClock();
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

        // Handle pending shuffle (swing) parameter change: schedule at next unswung 1/8 (0.5 QN) boundary.
        // This preserves clock continuity: we switch swing only at 8th boundaries which are always "straight".
        if (auto* sh = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter(paramShuffleStep)))
        {
            const int desiredShuffle = juce::jlimit(1, 7, sh->get());
            if (desiredShuffle != currentShuffleStep && pendingShuffleStep != desiredShuffle)
            {
                pendingShuffleStep = desiredShuffle;
                // Compute next unswung 1/8 boundary (0.5 quarter-note increments)
                const double nextUnswingPair = std::floor(ppqStart / 0.5 + 1.0) * 0.5;
                applyShuffleAtPPQ = nextUnswingPair;
                uiNextRestartPending.store(true, std::memory_order_relaxed); // show NEXT while waiting
            }
        }

        // Handle immediate retrigger (1/16) and bar-aligned changes (swing applied when computing sample offsets)
        // extClock is declared at function scope to collect external messages from all branches.
        int gapStart = -1, gapEnd = -1, startSampleAtBoundary = -1;
        int forbiddenClockSample = -1; // do not emit clock on this exact sample (Start sample)

        // Latent 1/16 grid Start: we always know next grid boundary; emit Start only if armed
        if (runActive)
        {
            // Swing-aware 16th boundaries: if shuffle active (>1) and bar length is 4/4, use shifted boundary set.
            const double eps = 1.0e-6;
                const double lastBar = pos.ppqPositionOfLastBarStart;
                const double barLenQ = (pos.timeSigNumerator > 0 && pos.timeSigDenominator > 0)
                    ? (4.0 * (double) pos.timeSigNumerator / (double) pos.timeSigDenominator)
                    : 4.0;
                double withinBar = juce::jlimit(0.0, barLenQ, ppqStart - lastBar);
                double nextBoundaryQ = -1.0;
                if (currentShuffleStep > 1 && std::fabs(barLenQ - 4.0) < 1e-6)
                {
                    const double shiftQ = (double)(currentShuffleStep - 1) * (1.0 / 48.0);
                    // Build 16 boundary positions inside a 4/4 bar considering swing pair lengths (pairLen remains 0.5 QN):
                    // Even index (0,2,4,...) => pairStart; odd => pairStart + 0.25 + shiftQ.
                    double found = -1.0;
                    for (int pair = 0; pair < 8; ++pair)
                    {
                        double pairStart = pair * 0.5; // cumulative (two 16ths total 0.5 QN)
                        double firstBoundary = pairStart;            // step 2*pair+1 (1-based)
                        double secondBoundary = pairStart + 0.25 + shiftQ; // step 2*pair+2 (1-based)
                        if (firstBoundary >= withinBar - eps && found < 0.0) found = firstBoundary;
                        if (secondBoundary >= withinBar - eps && found < 0.0) found = secondBoundary;
                    }
                    if (found < 0.0)
                        found = 4.0; // wrap to bar end (next bar start)
                    nextBoundaryQ = lastBar + found;
                }
                else
                {
                    // Unswing grid
                    const double gridQ = kSixteenthQ; // 0.25
                    // Compute previous and next 16th boundaries
                    const double prevGrid = std::floor(ppqStart / gridQ) * gridQ;
                    const double mod = ppqStart - prevGrid; // fraction into the 16th (in Q)
                    const bool onGrid = std::fabs(mod) <= eps || std::fabs(mod - gridQ) <= eps;
                    const double nextGrid = onGrid ? ppqStart : (prevGrid + gridQ);

                    // Tolerance: if we're only a few samples past the previous boundary, treat it as
                    // an immediate boundary (fire now) instead of waiting for the next 16th. This
                    // prevents borderline late triggers caused by tiny timing skews.
                    const int sampleTolerance = 8; // ~0.17ms at 48kHz, tweakable
                    const double samplesSincePrev = mod * samplesPerQuarter;
                    // Only treat a just-past previous 16th as the immediate boundary when a
                    // manual retrigger is armed. Otherwise prefer the next 16th to avoid
                    // perturbing normal clock alignment.
                    const bool armed = triggerArmedForNextSixteenth.load(std::memory_order_relaxed);
                    if (armed && samplesSincePrev <= (double) sampleTolerance)
                        nextBoundaryQ = prevGrid;
                    else
                        nextBoundaryQ = nextGrid;


                }
            const double deltaQ = juce::jmax(0.0, nextBoundaryQ - ppqStart);
            const int nextGridOffset = fastRoundPositive(deltaQ * samplesPerQuarter);
            if (triggerArmedForNextSixteenth.load(std::memory_order_relaxed))
            {
                if (nextGridOffset >= 0 && nextGridOffset < numSamples)
                {
                    const auto startMsg = juce::MidiMessage::midiStart();
                    long long barForStart = computeCurrentBar(ppqStart, barLenQ);
                    int stepForStart = computeLogicalStepFromPPQ(nextBoundaryQ, barLenQ, currentShuffleStep);
                    // Duplicate suppression: skip if same bar+step already emitted
                    // or if a near-simultaneous Start was already emitted this block.
                    if (! isDuplicateStart(barForStart, stepForStart, nextGridOffset))
                    {
                        addBoth(midi, extClock, startMsg, nextGridOffset);
                        lastStartBar.store(barForStart, std::memory_order_relaxed);
                        lastStartStep.store(stepForStart, std::memory_order_relaxed);

                    }
                        // record that we emitted a Start from a manual trigger at this sample
                        lastPatternStartSampleInBlock = nextGridOffset;
                    // triggerArmed diagnostic message removed per user request
                    // Consume arm
                    triggerArmedForNextSixteenth.store(false, std::memory_order_relaxed);
                    // Manual trigger should clear a stopped state: mark engine running
                    runActive = true;
                    pendingStart = false;
                    uiIsRunning.store(true, std::memory_order_relaxed);
                    uiPendingStart.store(false, std::memory_order_relaxed);
                    // Schedule resync if idx8 (gate) is enabled, per workflow.md rule 1
                    if (triggerModeEnabled.load(std::memory_order_relaxed))
                    {
                        // Schedule bar restart as before
                        pendingBarRestart.store(true, std::memory_order_relaxed);
                        uiNextRestartPending.store(true, std::memory_order_relaxed);
                        // Also schedule resync flag (mono, shortest precedence)
                        int offsetStep = 1;
                        if (auto* pi = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter(paramResyncOffsetStep)))
                            offsetStep = juce::jlimit(1,16, pi->get());
                        int currentStep = stepForStart;
                        attemptScheduleResync(barForStart, currentStep, offsetStep, false);
                    }
                    // Allow clock at same frame (no suppression)
                }
                // If offset beyond block, keep armed until next block (no change)
            }
        }

        // Pattern scheduling: first pass (full diagnostics). At bar boundaries decide if pattern fires; then emit Start events.
        tryFirePattern(ppqStart, barLenQ, lastBar, numSamples, midi, extClock, false, /*preview=*/false);

        auto barWindow = handleBarAlignedChanges(pos, numSamples, midi, extClock);
        gapStart = barWindow.gapStart;
        gapEnd = barWindow.gapEnd;
        startSampleAtBoundary = barWindow.startSample;
        // Second idempotent pass: suppress duplicate diagnostics unless new pattern targets appeared.
        tryFirePattern(ppqStart, barLenQ, lastBar, numSamples, midi, extClock, true, /*preview=*/false);

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

        // Precompute several constants used inside the tick loops to reduce per-tick overhead.
        const double ppqBlockEnd = ppqStart + (double) (numSamples - 1) * ppqPerSample;
        if (lastTickIndex == std::numeric_limits<long long>::min())
            lastTickIndex = tickAt(ppqStart, resolutionBefore) - 1;

        // Precompute swing/pulse mapping constants for the "before" resolution
        const double pairLenQBefore = 0.5;
        const double shiftQBefore = (currentShuffleStep <= 1) ? 0.0 : (double)(currentShuffleStep - 1) * (1.0 / 48.0);
        const long long pulsesPerPairBefore = (long long) std::llround(pairLenQBefore * (double) resolutionBefore);
        const int pulsesPer16thBefore = (int) (pulsesPerPairBefore / 2);
        const double firstLenBefore = 0.25 + shiftQBefore;
        const double secondLenBefore = 0.25 - shiftQBefore;
        const double spacingFirstBefore = pulsesPer16thBefore > 0 ? (firstLenBefore / (double) pulsesPer16thBefore) : 0.0;
        const double spacingSecondBefore = pulsesPer16thBefore > 0 ? (secondLenBefore / (double) pulsesPer16thBefore) : 0.0;

        // Use persistent per-primary fractional accumulator (declared in header).
        // Note: conservative persistence (carries across blocks) but reset on transport/bpm/sample-rate/rate changes.

        // Phase 1: ticks before a restart boundary (or whole block if no restart boundary). May include a mid-block shuffle activation.
        const int preEndSample = boundaryInBlock ? (startSampleAtBoundary - 1) : (numSamples - 1);
        if (preEndSample >= 0)
        {
            const double ppqPreEnd = ppqStart + (double) preEndSample * ppqPerSample;
            const long long tickIndexEndPre   = tickAt(ppqPreEnd, resolutionBefore);
            long long lastProcessedT = lastTickIndex;
            // Always continue from lastTickIndex+1 to preserve carried ticks across blocks
            for (long long t = lastTickIndex + 1; t <= tickIndexEndPre; ++t)
            {
                double rawTickPPQ = (double) t / (double) resolutionBefore;

                // Activate pending shuffle at unswung 1/8 boundary
                if (pendingShuffleStep > 0 && applyShuffleAtPPQ >= 0.0 && rawTickPPQ >= applyShuffleAtPPQ)
                {
                    currentShuffleStep = pendingShuffleStep;
                    pendingShuffleStep = -1;
                    applyShuffleAtPPQ = -1.0;
                    uiNextRestartPending.store(false, std::memory_order_relaxed);
                }

                // Optimised swing mapping using precomputed constants (reduces per-tick overhead)
                long long pairIndex = 0;
                int indexInPair = 0;
                if (pulsesPerPairBefore > 0)
                {
                    pairIndex = t / pulsesPerPairBefore;
                    indexInPair = (int) (t % pulsesPerPairBefore);
                }
                double tickPPQ;
                if (currentShuffleStep <= 1)
                {
                    tickPPQ = (double) t / (double) resolutionBefore;
                }
                else
                {
                    double pairStart = (double) pairIndex * pairLenQBefore;
                    if (indexInPair < pulsesPer16thBefore)
                        tickPPQ = pairStart + (double) indexInPair * spacingFirstBefore;
                    else
                    {
                        const int idxSecond = indexInPair - pulsesPer16thBefore;
                        tickPPQ = pairStart + firstLenBefore + (double) idxSecond * spacingSecondBefore;
                    }
                }
                const double deltaQuarter = tickPPQ - ppqStart;
                const double exactSample = deltaQuarter * samplesPerQuarter;

                // Treat the first pulse inside each pair as a primary and apply a tiny per-block fractional accumulator
                // to stabilise rounding on these primaries (clamped per-block, no carry across blocks).
                int mappedOffset;
                if (indexInPair == 0)
                {
                    double adj = exactSample + fracAccPrimaryBefore;
                    mappedOffset = fastRoundPositive(adj);
                    fracAccPrimaryBefore = adj - (double) mappedOffset;
                    // clamp carry to avoid large spikes
                    if (fracAccPrimaryBefore > 0.5) fracAccPrimaryBefore = 0.5;
                    if (fracAccPrimaryBefore < -0.5) fracAccPrimaryBefore = -0.5;
                }
                else
                {
                    mappedOffset = fastRoundPositive(exactSample);
                }
                // If mapped clock lands beyond this pre-boundary region, carry it to next block (do not clamp/emit)
                if (mappedOffset > preEndSample)
                {
                    break; // leave lastProcessedT unchanged so t will be retried next block
                }
                const int sampleOffset = juce::jlimit(0, numSamples - 1, mappedOffset);
                const bool inGapBar = (gapStart >= 0 && sampleOffset >= gapStart && sampleOffset < gapEnd);
                const bool onForbidden = (forbiddenClockSample >= 0 && sampleOffset == forbiddenClockSample);
                const bool inGap = inGapBar;
                if (! suppressUntilRestart && ! inGap && ! onForbidden)
                {
                    if (allowMidiOut || hostForcedAllowMidiOut)
                    {
                        midi.addEvent(clockMsg, sampleOffset);
                        uiClockCounter.fetch_add(1, std::memory_order_relaxed);
                        if (externalMidiOut)
                            extClock.addEvent(clockMsg, sampleOffset);
                        // pre-phase diag logging removed per user request
                    }
                }

                    if (clickEnabled && runActive)
                    {
                        int stepSpan = 1;
                        switch (clickRateCached)
                        {
                            case 1: stepSpan = resolutionBefore; break;                 // quarter note
                            case 2: stepSpan = juce::jmax(1, resolutionBefore / 2); break; // eighth
                            case 3: stepSpan = juce::jmax(1, resolutionBefore / 4); break; // sixteenth
                            case 4: stepSpan = 1; break; // every midi-clock pulse (24ppq)
                            default: stepSpan = juce::jmax(1, resolutionBefore / 4); break;
                        }
                        if ((t % stepSpan) == 0)
                        {
                            const float outLevel = 1.0f; // Always fixed 0 dB, all beats even
                            // Use user-selected pulse width (ms)
                            const double pulseMs = (double) pulseWidthMs;
                            const int pulseSamples = (int) juce::jmax(1, (int) std::round((pulseMs / 1000.0) * currentSampleRate));
                            if (! clickPulseCached)
                            {
                                // 1-sample spike
                                if (sampleOffset >= 0 && sampleOffset < numSamples)
                                {
                                    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                                        buffer.addSample(ch, sampleOffset, outLevel);
                                }
                            }
                            else
                            {
                                // Pulse: hold high for pulseWidthMs, no decay
                                int s = 0;
                                for (; s < pulseSamples; ++s)
                                {
                                    const int idx = sampleOffset + s;
                                    if (idx >= 0 && idx < numSamples)
                                    {
                                        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                                            buffer.addSample(ch, idx, outLevel);
                                    }
                                }
                                // If the pulse extends beyond the end of this block, carry the remaining tail.
                                const int tailBeyond = (sampleOffset + pulseSamples) - numSamples;
                                if (tailBeyond > 0)
                                    clickHoldRemainingSamples = juce::jmax(clickHoldRemainingSamples, tailBeyond);
                            }
                        }
                    }
                lastProcessedT = t;
            }
            lastTickIndex = lastProcessedT;
        }

        // Precompute 'after' resolution swing/pulse mapping constants for use in the post-boundary loop
        const double pairLenQAfter = 0.5;
        const double shiftQAfter = (currentShuffleStep <= 1) ? 0.0 : (double)(currentShuffleStep - 1) * (1.0 / 48.0);
        const long long pulsesPerPairAfter = (long long) std::llround(pairLenQAfter * (double) resolutionAfter);
        const int pulsesPer16thAfter = (int) (pulsesPerPairAfter > 0 ? (pulsesPerPairAfter / 2) : 0);
        const double firstLenAfter = 0.25 + shiftQAfter;
        const double secondLenAfter = 0.25 - shiftQAfter;
        const double spacingFirstAfter = pulsesPer16thAfter > 0 ? (firstLenAfter / (double) pulsesPer16thAfter) : 0.0;
        const double spacingSecondAfter = pulsesPer16thAfter > 0 ? (secondLenAfter / (double) pulsesPer16thAfter) : 0.0;

        // Phase 2: ticks after a restart boundary using the (possibly) new resolution.
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

            const long long tickIndexEndPost   = tickAt(ppqBlockEnd,  resolutionAfter);
            long long lastProcessedT = lastTickIndex;
            for (long long t = lastTickIndex + 1; t <= tickIndexEndPost; ++t)
            {
                double rawTickPPQ = (double) t / (double) resolutionAfter;
                if (pendingShuffleStep > 0 && applyShuffleAtPPQ >= 0.0 && rawTickPPQ >= applyShuffleAtPPQ)
                {
                    currentShuffleStep = pendingShuffleStep;
                    pendingShuffleStep = -1;
                    applyShuffleAtPPQ = -1.0;
                    uiNextRestartPending.store(false, std::memory_order_relaxed);
                }
                double tickPPQ = 0.0;
                int indexInPair = 0;
                if (currentShuffleStep <= 1 || pulsesPerPairAfter <= 0)
                {
                    tickPPQ = (double) t / (double) resolutionAfter;
                }
                else
                {
                    const long long pairIndex = t / pulsesPerPairAfter;
                    indexInPair = (int) (t % pulsesPerPairAfter);
                    double pairStart = (double) pairIndex * pairLenQAfter;
                    if (indexInPair < pulsesPer16thAfter)
                        tickPPQ = pairStart + (double) indexInPair * spacingFirstAfter;
                    else
                    {
                        const int idxSecond = indexInPair - pulsesPer16thAfter;
                        tickPPQ = pairStart + firstLenAfter + (double) idxSecond * spacingSecondAfter;
                    }
                }
                const double deltaQuarter = tickPPQ - ppqStart;
                const double exactSample = deltaQuarter * samplesPerQuarter;
                int mappedOffset;
                if (currentShuffleStep > 1 && pulsesPerPairAfter > 0 && indexInPair == 0)
                {
                    double adj = exactSample + fracAccPrimaryAfter;
                    mappedOffset = fastRoundPositive(adj);
                    fracAccPrimaryAfter = adj - (double) mappedOffset;
                    if (fracAccPrimaryAfter > 0.5) fracAccPrimaryAfter = 0.5;
                    if (fracAccPrimaryAfter < -0.5) fracAccPrimaryAfter = -0.5;
                }
                else
                {
                    mappedOffset = fastRoundPositive(exactSample);
                }
                if (mappedOffset > numSamples - 1)
                {
                    break; // carry into next block; do not advance lastTickIndex
                }
                const int sampleOffset = juce::jlimit(0, numSamples - 1, mappedOffset);
                // Past boundary; optionally suppress clock on the exact Start frame to avoid double clocks with Start
                if (! suppressUntilRestart && !(forbiddenClockSample >= 0 && sampleOffset == forbiddenClockSample))
                {
                    if (allowMidiOut || hostForcedAllowMidiOut)
                    {
                        midi.addEvent(clockMsg, sampleOffset);
                        uiClockCounter.fetch_add(1, std::memory_order_relaxed);
                        if (externalMidiOut)
                            extClock.addEvent(clockMsg, sampleOffset);
                        // post-phase diag logging removed per user request
                    }
                }

                    if (clickEnabled && runActive)
                    {
                        int stepSpan = 1;
                        switch (clickRateCached)
                        {
                            case 1: stepSpan = resolutionAfter; break;
                            case 2: stepSpan = juce::jmax(1, resolutionAfter / 2); break;
                            case 3: stepSpan = juce::jmax(1, resolutionAfter / 4); break;
                            case 4: stepSpan = 1; break;
                            default: stepSpan = juce::jmax(1, resolutionAfter / 4); break;
                        }
                        if ((t % stepSpan) == 0)
                        {
                            const float outLevel = 1.0f; // Always fixed 0 dB, all beats even
                            // Use user-selected pulse width (ms)
                            const double pulseMs = (double) pulseWidthMs;
                            const int pulseSamples = (int) juce::jmax(1, (int) std::round((pulseMs / 1000.0) * currentSampleRate));
                            if (! clickPulseCached)
                            {
                                if (mappedOffset >= 0 && mappedOffset < numSamples)
                                {
                                    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                                        buffer.addSample(ch, mappedOffset, outLevel);
                                }
                            }
                            else
                            {
                                int s = 0;
                                for (; s < pulseSamples; ++s)
                                {
                                    const int idx = mappedOffset + s;
                                    if (idx >= 0 && idx < numSamples)
                                    {
                                        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                                            buffer.addSample(ch, idx, outLevel);
                                    }
                                }
                                const int tailBeyond = (mappedOffset + pulseSamples) - numSamples;
                                if (tailBeyond > 0)
                                    clickHoldRemainingSamples = juce::jmax(clickHoldRemainingSamples, tailBeyond);
                            }
                        }
                    }
                lastProcessedT = t;
            }
            lastTickIndex = lastProcessedT;
            // Consume the one-shot suppression
            suppressClockAtBoundaryOnce = false;
        }

        // extClock will be flushed once after scheduling (below)
    }

    // Flush any queued external MIDI messages gathered while scheduling.
    if (externalMidiOut && extClock.getNumEvents() > 0)
        externalMidiOut->sendBlockOfMessages(extClock, juce::Time::getMillisecondCounterHiRes(), currentSampleRate);

    // Per-tick clicks are written directly into the audio buffer in the scheduling loops above.

    lastWasPlaying = isPlaying;
    // Offset step change: schedule restart at the next occurrence of the selected step (modulo bar)
    if (auto* pi = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter(paramResyncOffsetStep)))
    {
        const int cur = juce::jlimit(1, 16, pi->get());
        if (cur != lastOffsetStep)
        {
            lastOffsetStep = cur;
            pendingBarRestart.store(true, std::memory_order_relaxed);
            // no force to next bar; modulo handling in handleBarAlignedChanges
            uiNextRestartPending.store(true, std::memory_order_relaxed);
        }
    }
}

// ---- Pattern helpers ----
void ClockSyncAudioProcessor::updatePatternParams()
{
    if (auto* pb = dynamic_cast<juce::AudioParameterChoice*>(parameters.getParameter(paramPatternBars)))
    {
        int newMode = pb->getIndex();
        int oldMode = patternBarsMode.load(std::memory_order_relaxed);
        if (newMode != oldMode)
        {
            patternBarsMode.store(newMode, std::memory_order_relaxed);
            // Cancel any pending resync on mode change (stale targets invalid)
            resyncPending.store(false, std::memory_order_relaxed);
            resyncTargetBar.store(-1, std::memory_order_relaxed);
            resyncTargetStep.store(-1, std::memory_order_relaxed);
            patternModeJustChanged.store(true, std::memory_order_relaxed);
                // Changing interval should clear any pending pattern scheduling so
                // the new interval takes effect immediately (avoid firing on the
                // previously computed bar for the old interval).
                pendingPatternPPQ.clear();
                pendingPatternRestartTargetBar.store(-1, std::memory_order_relaxed);
                lastPatternFiredBar = -1;
                nextRandomPatternTargetBar = -1;

        }
    }
    if (auto* ps = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter(paramPatternSteps)))
    {
        const uint16_t oldMask = patternStepsMask.load(std::memory_order_relaxed);
        const uint16_t newMask = (uint16_t) juce::jlimit(0, 65535, ps->get());
        if (oldMask == 0 && newMask != 0)
            patternMaskJustActivated.store(true, std::memory_order_relaxed);
        patternStepsMask.store(newMask, std::memory_order_relaxed);
    }
}

int ClockSyncAudioProcessor::getPatternBarIntervalFromMode(int mode) const
{
    switch (mode)
    {
        case 1: return 1; // index 1 => bar interval 1
        case 2: return 2;
        case 3: return 4;
        case 4: return 8;
        case 5: return 16;
        case 6: return 32;
        case 7: return 64;
        default: return -1; // 0=OFF or RND (8) handled separately
    }
}

void ClockSyncAudioProcessor::preparePatternForBar(double barStartPPQ, double barLenQ)
{
    pendingPatternPPQ.clear();
    const uint16_t mask = patternStepsMask.load(std::memory_order_relaxed);
    if (mask != 0) {
        // Visual interaction originally applied a +4 step rotation (see PatternRing::hitTest).
        // The stored bitmask indices therefore represented (logicalIndex + 4) & 15.
        // Use the UI rotation so scheduled pattern events align with visual steps.
        // This preserves the +1-step alignment observed with hosts where UI uses
        // 1..16 visual numbering while storage uses rotated bit indices.
        constexpr int visualRotation = 4; // match UI rotation
        const bool applySwing = (currentShuffleStep > 1 && std::fabs(barLenQ - 4.0) < 1e-6);
        const double shiftQ = applySwing ? (double)(currentShuffleStep - 1) * (1.0 / 48.0) : 0.0; // second 16th of each pair delay
        juce::Array<int> activeLogicalSteps;
        for (int logicalIndex = 0; logicalIndex < 16; ++logicalIndex)
        {
            const int storedIndex = (logicalIndex + visualRotation) & 15; // how it's stored in the mask
            if ((mask & (uint16_t(1) << storedIndex)) == 0) continue;
            const int triggerLogical = logicalIndex; // exact logical index
            double stepPosQ;
            if (applySwing)
            {
                int pairIndex = triggerLogical / 2; // 0..7
                bool secondInPair = (triggerLogical & 1) == 1;
                double pairStart = pairIndex * 0.5; // each pair spans 0.5 QN
                stepPosQ = secondInPair ? (pairStart + 0.25 + shiftQ) : pairStart;
            }
            else
            {
                stepPosQ = (barLenQ / 16.0) * (double) triggerLogical;
            }
            // Snap exact bar-start triggers to the precise barStartPPQ to avoid
            // tiny FP rounding moving step 1 off the quantized boundary.
            double scheduledPPQ = barStartPPQ + stepPosQ;
            if (std::fabs(stepPosQ) < 1e-9)
                scheduledPPQ = barStartPPQ;
            pendingPatternPPQ.push_back(scheduledPPQ);
            activeLogicalSteps.add(triggerLogical   + 1); // store 1..16 for diagnostics

        }


    }
    std::sort(pendingPatternPPQ.begin(), pendingPatternPPQ.end());

   
}

// Attempt to schedule (or replace) resync target according to precedence rules.
// currentBar: 1-based bar where scheduling request occurs (first bar == 1).
// currentStep: 1..16 current step (approx) when request occurs.
// offsetStep: 1..16 target offset step (user selection).
// force: true for rate-change (sets regardless of gate idx8), false for Start-driven (only called when gate already TRUE).
void ClockSyncAudioProcessor::attemptScheduleResync(long long currentBar, int currentStep, int offsetStep, bool force)
{
    if (offsetStep < 1 || offsetStep > 16) return;
    // Honour the trigger-mode gate: if this is not a forced scheduling (rate change)
    // then require `triggerModeEnabled` (idx8) to be true. This centralises the
    // master resync gate so UI/parameter timing cannot accidentally override it.
    if (! force && ! triggerModeEnabled.load(std::memory_order_relaxed))
        return;
    // Determine target bar relative to current position
    long long targetBar = currentBar;
    if (offsetStep <= currentStep)
        targetBar = currentBar + 1; // wrap to next bar if offset already passed
    const bool already = resyncPending.load(std::memory_order_relaxed);
    long long existingBar = resyncTargetBar.load(std::memory_order_relaxed);
    int existingStep = resyncTargetStep.load(std::memory_order_relaxed);
    bool replace = false;
    if (!already)
        replace = true;
    else
    {
        // Shortest precedence: replace only if new target earlier than existing
        if (targetBar < existingBar || (targetBar == existingBar && offsetStep < existingStep))
            replace = true;
    }
    if (replace)
    {
        // If a pattern has already requested a deferred restart for this bar,
        // do not schedule an independent resync here — let the pattern's
        // deferred restart take precedence to avoid overlapping semantics
        // that can clear or shift the pattern stop/restart scheduling.
        const long long pendingTarget = pendingPatternRestartTargetBar.load(std::memory_order_relaxed);
        if (pendingTarget >= 0 && pendingTarget == targetBar)
        {
            return;
        }
        resyncTargetBar.store(targetBar, std::memory_order_relaxed);
        resyncTargetStep.store(offsetStep, std::memory_order_relaxed);
        resyncPending.store(true, std::memory_order_relaxed);

    }

}

void ClockSyncAudioProcessor::tryFirePattern(double ppqStart, double barLenQ, double barStartPPQ, int numSamples, juce::MidiBuffer& midi, juce::MidiBuffer& extClock, bool suppressDiag, bool preview)
{
    // Allow pattern-driven Starts to fire even when the internal `runActive` flag
    // is false. This makes pattern behaviour consistent with manual triggers: when
    // a pattern step issues a Start it should put the engine into the running
    // state immediately so any scheduled bar-aligned restarts take effect in
    // the same block. We still honour the Pattern Bars mode being OFF below.
    const int mode = patternBarsMode.load(std::memory_order_relaxed);
    if (mode == 0) return; // OFF
    const bool rnd = (mode == 8);
    // Determine whether the current or upcoming bar should trigger the pattern.
    // Previously pattern preparation only ran when the block began exactly at a bar start.
    // Prepare the pattern for the current bar if it should fire and hasn't been prepared yet
    // (this allows scheduling of steps that fall later in the same bar even if the block
    // started after the bar start).
    // Prefer host-provided bar count when available to sync across multiple plugin instances.
    // `uiExternalBarNumber` (1-based) is written in processBlock when the playhead provides barCount.
    // Previous logic used extBarNum-1 to convert to 0-based; now we use 1-based internal bar numbering.
    const int extBarNum = uiExternalBarNumber.load(std::memory_order_relaxed);
    // Prefer the host-provided bar number when available. Some hosts report
    // 0-based bar counts while our internal computation is 1-based. To avoid
    // a persistent off-by-one across hosts, compare the computed 1-based bar
    // and the host value; if they differ by exactly 1 assume the host is
    // 0-based and use the computed 1-based result instead.
    const long long computedBar = computeCurrentBar(ppqStart, barLenQ);
    long long currentBar = computedBar;
    if (extBarNum > 0)
    {
        if (computedBar - (long long) extBarNum == 1)
            currentBar = computedBar; // host appears 0-based, prefer computed 1-based
        else
            currentBar = (long long) extBarNum;
    }
    auto decideFireForBar = [&](long long candidateBar)->bool {
        if (rnd)
        {
            if (nextRandomPatternTargetBar < 0 || candidateBar >= nextRandomPatternTargetBar)
                return true;
            return false;
        }
        else
        {
            int interval = getPatternBarIntervalFromMode(mode);
            // Reverted semantics: fire ON every Nth DAW bar (interval), i.e. DAW bars N,2N,3N,...
            // candidateBar is 1-based internally, so check modulo directly.
            // Example: interval=4 -> fires when (candidateBar%4)==0 => candidateBar=4,8,12,...
            const bool res = (interval > 0 && ((candidateBar % interval) == 0));

            return res;
        }
    };

    if (decideFireForBar(currentBar) && lastPatternFiredBar != currentBar)
    {
        // Use the bar-start PPQ provided to this function (barStartPPQ) as authoritative
        // for scheduling. Update lastPatternFiredBar using the chosen bar index.
        lastPatternFiredBar = currentBar;
        preparePatternForBar(barStartPPQ, barLenQ);

    }

    // Determine whether a bar boundary (one or more) falls within this audio block.
    // The original implementation only prepared patterns when the block started exactly at a bar start
    // (ppqStart == barStartPPQ), which misses cases where the bar start is inside the block. We detect
    // candidate bar-start PPQ positions inside the block and prepare the pattern for the exact bar that fires.
    const double eps = 1e-6;
    const double ppqPerSample = 1.0 / samplesPerQuarter;
    const double blockEndPPQ = ppqStart + (double)(numSamples - 1) * ppqPerSample;

    // Use an explicit candidateBar index (when available from the host) so
    // that pattern firing decisions align across plugin instances that
    // receive the same DAW-provided bar count. The outer loop increments
    // the candidateBar value and passes it here rather than recomputing
    // from PPQ which could diverge from host bar numbering.
    auto considerBarStart = [&](double candidateBarStartPPQ, long long candidateBar)
    {
        // Guard: if we've already prepared/fired for this candidateBar in this block, skip duplicate preparation.
        if (candidateBar == lastPatternFiredBar)
            return;
        bool fire = false;
        if (rnd)
        {
            // Random mode: pick a future target bar; fire when reached then choose a new random interval.
            if (nextRandomPatternTargetBar < 0 || candidateBar >= nextRandomPatternTargetBar)
            {
                int intervalRnd = 1 + (int) (juce::Random::getSystemRandom().nextInt(16));
                nextRandomPatternTargetBar = candidateBar + intervalRnd;
                fire = true;
            }
        }
        else
        {
            // Recurring modulo gating: fire only on bars that are multiples of the selected interval.
            int interval = getPatternBarIntervalFromMode(mode); // returns -1 for OFF or RND
            if (interval < 0)
            {
                // OFF: do not auto-fire pattern.
            }
            else
            {
                if ((candidateBar % interval) == 0)
                {
                    // Suppress firing in the same block where mode changed (avoid immediate unintended Start)
                    if (patternModeJustChanged.load(std::memory_order_relaxed))
                    {
                        fire = false;
                    }
                    else
                    {
                        fire = true;
                    }
                }
            }
        }
        if (fire)
        {

            lastPatternFiredBar = candidateBar;
            // First pattern firing observed: allow bar restarts again.
            deferBarRestartUntilPattern.store(false, std::memory_order_relaxed);
            preparePatternForBar(candidateBarStartPPQ, barLenQ);
        }
    };

    // If the block begins exactly at a bar start, consider that bar.
    if (std::fabs(ppqStart - barStartPPQ) < eps)
        considerBarStart(barStartPPQ, currentBar);

    // Consider any future bar starts that fall within this block (typically only the next bar).
    // Use an explicit candidateBar counter so that bar indices align with host-provided bar counts
    // when available (we derived `currentBar` above possibly from uiExternalBarNumber).
    double nextBar = barStartPPQ + barLenQ;
    long long candidateBar = currentBar + 1;
    while (nextBar <= blockEndPPQ + eps)
    {
        if (nextBar >= ppqStart - eps)
            considerBarStart(nextBar, candidateBar);
        nextBar += barLenQ;
        ++candidateBar;
    }
    // Clear mode-change suppression flag after evaluating all candidate bars in this block.
    if (patternModeJustChanged.load(std::memory_order_relaxed))
        patternModeJustChanged.store(false, std::memory_order_relaxed);
    if (pendingPatternPPQ.empty()) return;
    // Emit Start messages for pattern PPQ times that fall within this block; remove them after emission.
    auto clockResolution = getClockResolution(); // not strictly needed
    std::vector<double> remaining;
    remaining.reserve(pendingPatternPPQ.size());
    int firedCount = 0;
    long long lastBarEmitted = std::numeric_limits<long long>::min();
    for (double targetPPQ : pendingPatternPPQ)
    {
        if (targetPPQ < ppqStart - 1e-9)
        {

            continue; // already passed (late)
        }
        if (targetPPQ > blockEndPPQ + 1e-9)
        {

            { remaining.push_back(targetPPQ); continue; }
        }
        // Note: pendingPatternPPQ entries are already stored including any ms fine-tune
        // at prepare time. Use the stored targetPPQ directly as the emit point.
        double deltaQ = targetPPQ - ppqStart;
        int sampleOffset = fastRoundPositive(deltaQ * samplesPerQuarter);
        if (sampleOffset < 0 || sampleOffset >= numSamples) { remaining.push_back(targetPPQ); continue; }
        const auto startMsg = juce::MidiMessage::midiStart();
        // If the bar-restart path already emitted/consumed a Start at this sample,
        // avoid sending a duplicate and consume the pending indicators so UI/state
        // reflect the applied restart.
        if (sampleOffset == lastBarRestartStartSampleInBlock)
        {

            pendingBarRestart.store(false, std::memory_order_relaxed);
            uiNextRestartPending.store(false, std::memory_order_relaxed);
            // Clear any deferred pattern restart target to avoid re-scheduling.
            pendingPatternRestartTargetBar.store(-1, std::memory_order_relaxed);
            continue;
        }
        // Additional duplicate guard: if this pattern target already emitted earlier in this block (e.g. second tryFirePattern invocation), skip.
        if (sampleOffset == lastPatternStartSampleInBlock)
        {

            continue;
        }
        // Compute logical step for duplicate suppression before emission
        long long barForStart = computeCurrentBar(targetPPQ, barLenQ);
        int stepForStart = computeLogicalStepFromPPQ(targetPPQ, barLenQ, currentShuffleStep);
        bool duplicate = isDuplicateStart(barForStart, stepForStart, sampleOffset);
        bool allowPatternStart = shouldAllowGeneratedStart();
        if (!duplicate && allowPatternStart)
        {
            midi.addEvent(startMsg, sampleOffset);
            extClock.addEvent(startMsg, sampleOffset);
            lastStartBar.store(barForStart, std::memory_order_relaxed);
            lastStartStep.store(stepForStart, std::memory_order_relaxed);
            // For preview-only emissions (editor mask activation) do not modify
            // processor run/resync state or diagnostic counts. Only record
            // real pattern emissions.
            if (! preview)
            {
                lastStartSource.store(1, std::memory_order_relaxed); // pattern
                patternStartCount.fetch_add(1, std::memory_order_relaxed);
            }
            lastBarEmitted = std::max(lastBarEmitted, barForStart);

            // If a deferred pattern-requested restart targets this same bar,
            // consume it now to avoid the bar-restart codepath re-emitting
            // the same Start later in this or the next block. This prevents
            // duplicate restart emissions when both scheduling paths coexist.
            {
                const long long pendingTarget = pendingPatternRestartTargetBar.load(std::memory_order_relaxed);
                if (pendingTarget >= 0 && pendingTarget == barForStart)
                {
                    pendingPatternRestartTargetBar.store(-1, std::memory_order_relaxed);
                    uiNextRestartPending.store(false, std::memory_order_relaxed);
                }
            }

        }

        ++firedCount; // count successful pattern Start emission for this block
        // record this sample so the bar-restart handler can avoid emitting a duplicate Start
        lastPatternStartSampleInBlock = sampleOffset;
        // When a pattern issues a Start we want the processor/UI internal
        // state to reflect that Start immediately so the visual step and
        // subsequent scheduling align with the emitted Start (matches manual trigger).
        // Compute the logical step (1..16) inside the bar for this targetPPQ and
        // update UI/run state accordingly.
        if (barLenQ > 0.0)
        {
            // Use centralized helper so shuffle/quantization match emission and duplicate suppression.
            int stepNumber = computeLogicalStepFromPPQ(targetPPQ, barLenQ, currentShuffleStep);
            uiStep16.store(stepNumber, std::memory_order_relaxed);
        }
        // Make the processor consider itself running so any restart/modifiers take effect.
        // For preview emissions, do not change run state.
        if (! preview)
        {
            runActive = true;
            pendingStart = false;
            uiIsRunning.store(true, std::memory_order_relaxed);
            uiPendingStart.store(false, std::memory_order_relaxed);
        }
        // tryFirePattern diagnostic logging removed per user request
        // Respect trigger mode: schedule a bar+offset restart (same semantics as manual trigger idx1)
        const long long patternBar = computeCurrentBar(targetPPQ, barLenQ);
        if (!preview && triggerModeEnabled.load(std::memory_order_relaxed) && lastPatternRestartScheduledBar != patternBar)
        {
            // NOTE: previously we scheduled a deferred bar-aligned restart here
            // (pendingPatternRestartTargetBar) so that a pattern Start would also
            // request a restart at the next selected step (same semantics as an
            // explicit manual trigger). However this caused duplicate/late
            // restarts in some host/device setups because the pattern's own
            // step-trigger path already schedules retriggers. To avoid that
            // double-scheduling and the observed "one 16th late" behaviour,
            // disable the automatic deferred restart here. The explicit
            // trigger/retrigger path remains unchanged and will still schedule
            // any required restarts.
            // (Disabled: pendingPatternRestartTargetBar.store(...); uiNextRestartPending.store(...);)
        }
    }
    // Pattern completion resync scheduling (non-loop mode only).
    if (lastBarEmitted != std::numeric_limits<long long>::min())
    {
        const int modeNow = patternBarsMode.load(std::memory_order_relaxed);
        const bool loopMode = (modeNow == 1);
        const bool gate = triggerModeEnabled.load(std::memory_order_relaxed);
        if (!loopMode && gate)
        {
            int offsetStep = 1;
            if (auto* pi = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter(paramResyncOffsetStep)))
                offsetStep = juce::jlimit(1,16, pi->get());
            // Prefer the exact last emitted step when available to avoid
            // off-by-one scheduling caused by UI/storage index conversions.
            int lastEmitted = lastStartStep.load(std::memory_order_relaxed);
            int currentStepApprox = (lastEmitted >= 1 && lastEmitted <= 16)
                ? lastEmitted
                : juce::jlimit(1,16, uiStep16.load(std::memory_order_relaxed));
            // Do not schedule a completion resync while a shuffle change is pending.
            // Shuffle shifts second-16th PPQ positions and is applied at the next
            // 1/8 boundary; scheduling a resync while pendingShuffleStep is set
            // can cause duplicate restart emissions. Defer resync until shuffle
            // has been applied in `handleBarAlignedChanges`.
            if (pendingShuffleStep <= 0)
                attemptScheduleResync(lastBarEmitted, currentStepApprox, offsetStep, false);
        }
    }

    pendingPatternPPQ.swap(remaining);
}

ClockSyncAudioProcessor::BarRestartWindow ClockSyncAudioProcessor::handleBarAlignedChanges(const juce::AudioPlayHead::CurrentPositionInfo& pos,
                                                                                           int numSamples,
                                                                                           juce::MidiBuffer& midi,
                                                                                           juce::MidiBuffer& extBuffer)
{
    BarRestartWindow window;
    const bool runWasActive = runActive;

    // Local snapshot: only send MIDI messages for restarts if Run is enabled or ClockWhileStopped is enabled
    const bool sendMidi = (parameters.getRawParameterValue(paramRun)->load() > 0.5f)
                          || (parameters.getRawParameterValue(paramClockWhileStopped)->load() > 0.5f);

    const double lastBar = pos.ppqPositionOfLastBarStart;
    const double barLenQ = (pos.timeSigNumerator > 0 && pos.timeSigDenominator > 0)
        ? (4.0 * (double) pos.timeSigNumerator / (double) pos.timeSigDenominator)
        : 4.0;
    const double ppqStart = pos.ppqPosition;
    const double posInBar = juce::jlimit(0.0, barLenQ, ppqStart - lastBar);
    double remainQ = barLenQ - posInBar;
    if (remainQ < 0.0) remainQ += barLenQ;
    // Default boundary at next bar start
    int sampleOffset = fastRoundPositive(remainQ * samplesPerQuarter);
    if (sampleOffset < 0) sampleOffset = 0;

    const bool haveRateChange = (pendingRateIndex >= 0);
    const bool haveShuffleChange = (pendingShuffleStep > 0); // shuffle by itself should NOT force a restart now
    const bool haveStart = pendingStart;
    bool haveBarRestart = pendingBarRestart.load(std::memory_order_relaxed);
    // Suppress automatic bar restart until the first pattern firing after host START when interval mode active
    if (haveBarRestart && deferBarRestartUntilPattern.load(std::memory_order_relaxed))
    {
        haveBarRestart = false;
    }
    const bool havePatternDeferred = (pendingPatternRestartTargetBar.load(std::memory_order_relaxed) >= 0);
    const bool applyShuffleWithBoundary = haveShuffleChange && (haveRateChange || haveStart || haveBarRestart || havePatternDeferred);

    // Compute the next occurrence of the selected step (1..16) at or after 'now'.
    // If the selected step lies ahead in the current bar, use it; otherwise target the same step in the next bar.
    int offsetStep = 1;
    if (auto* pi = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter(paramResyncOffsetStep)))
        offsetStep = juce::jlimit(1, 16, pi->get());
    else if (auto* p = parameters.getParameter(paramResyncOffsetStep))
        offsetStep = juce::jlimit(1, 16, (int) juce::roundToInt(p->getValue() * 15.0f + 1.0f));
    const int stepIndex0 = offsetStep - 1; // 0..15
    // Swing-aware mapping: if shuffle active (>1) and 4/4, recompute target boundary using shifted second 16th lengths.
    // Use master grid to compute target step position so all scheduling
    // paths (pattern, resync) agree exactly on swung/unswung positions.
    double stepQWithinBar = computeStepPPQ(lastBar, barLenQ, offsetStep, currentShuffleStep) - lastBar;

    // Local flag to record whether a deferred pattern-requested restart matches
    // the scheduled boundary calculated below.
    bool deferredMatches = false;
    long long scheduledRestartBar = -1;

    if (haveStart || haveBarRestart || haveRateChange || havePatternDeferred) // exclude pure shuffle change
    {
        // Schedule at the next occurrence of the selected step within the bar (modulo behavior)
        double deltaQ = stepQWithinBar - posInBar;
        if (deltaQ < -kBarEps)
            deltaQ += barLenQ; // wrap into the next bar if passed
        if (posInBar <= kBarEps && stepIndex0 == 0)
            deltaQ = 0.0; // allow immediate restart at bar start when offset=1

        sampleOffset = fastRoundPositive(deltaQ * samplesPerQuarter);
        // Determine which bar this scheduled restart will fall into. If a pattern
        // previously requested a deferred restart for that bar, mark the deferred
        // match so the restart is applied (and consume the deferred request).
        const long long scheduledRestartBar = computeCurrentBar(ppqStart + deltaQ + 1e-9, barLenQ);
        const long long pendingTarget = pendingPatternRestartTargetBar.load(std::memory_order_relaxed);
        const bool deferredMatches = (pendingTarget >= 0 && pendingTarget == scheduledRestartBar);
        if (deferredMatches)
            pendingPatternRestartTargetBar.store(-1, std::memory_order_relaxed);
    }

    // Resync firing: check whether pending target falls within this block.
    bool resyncMatches = false;
    int resyncStepForStart = -1;
    if (resyncPending.load(std::memory_order_relaxed))
    {
        const long long curBarForOffset = computeCurrentBar(ppqStart + (double) sampleOffset / samplesPerQuarter + 1e-9, barLenQ);
        const long long targetBar = resyncTargetBar.load(std::memory_order_relaxed);
        const int targetStep = resyncTargetStep.load(std::memory_order_relaxed);
        if (targetBar >= 0 && targetStep >= 1)
        {
            const double targetBarStartPPQ = (double) (targetBar - 1) * barLenQ;
            const double targetPPQ = computeStepPPQ(targetBarStartPPQ, barLenQ, targetStep, currentShuffleStep);
            if (targetPPQ >= ppqStart - 1e-9)
            {
                double deltaQ = targetPPQ - ppqStart;
                int candidateOffset = fastRoundPositive(deltaQ * samplesPerQuarter);
                if (candidateOffset >= 0 && candidateOffset <= numSamples - 1)
                {
                    sampleOffset = candidateOffset;
                    resyncMatches = true;
                    resyncStepForStart = targetStep;
                }
            }
            // Late consumption: target already passed before this block end; clear.
            const double blockEndPPQ = ppqStart + (double)(numSamples - 1) / samplesPerQuarter;
            if (targetPPQ < ppqStart - 1e-9 || targetPPQ < blockEndPPQ - 1e-9 && targetPPQ < ppqStart)
            {
                resyncPending.store(false, std::memory_order_relaxed);
                resyncTargetBar.store(-1, std::memory_order_relaxed);
                resyncTargetStep.store(-1, std::memory_order_relaxed);
            }
        }
    }

    // If Run is OFF, suppress resync/auto starts. Manual triggers re-enable runActive
    if (sampleOffset <= numSamples - 1 && (haveRateChange || haveStart || haveBarRestart || deferredMatches || resyncMatches))
    {
        jassert(sampleOffset >= 0); // debug-only: ensure non-negative scheduling
        // Gap length: use actual preceding 16th length under swing if possible (approx samples spanning previous half).
        double prevSixteenthLenQ = kSixteenthQ;
        if (currentShuffleStep > 1 && std::fabs(barLenQ - 4.0) < 1e-6)
        {
            // Determine which 16th we restart at; previous length depends on whether this is second or first in pair.
            bool secondHalf = (stepIndex0 % 2) == 1;
            const double shiftQ = (double)(currentShuffleStep - 1) * (1.0 / 48.0);
            prevSixteenthLenQ = secondHalf ? (0.25 + shiftQ) : (0.25 - shiftQ); // length of the preceding 16th segment
        }
        // Decide whether a Start/Stop should be emitted for this boundary.
        // We do NOT want to emit a Start when the only pending change is a
        // rate or shuffle parameter change (that would schedule an audible
        // restart and break bar-sync in hosts). Only emit Start when a real
        // start/restart was requested (haveStart/haveBarRestart/deferredMatches).
        const bool legacy = legacyModeEnabled.load(std::memory_order_relaxed);
        // Emit Start for explicit reasons and realign only (rate change no longer directly emits a Start).
        bool shouldEmitStart = (haveStart || haveBarRestart || deferredMatches || resyncMatches);
        // Additional suppression: if this boundary is reached due purely to a pending initial start
        // (haveStart without barRestart/deferred) AND pattern interval mode is active but the pattern
        // has no active steps (mask==0), skip emitting a Start at the bar boundary. This prevents a
        // spurious Start at the first interval bar when the pattern is empty (user expectation: empty
        // pattern yields no Start events).
        // Suppress ANY bar-aligned Start emission (pendingStart, barRestart, deferred) when interval mode active and pattern empty.
        // Interval should only schedule potential pattern step evaluation; it must never itself cause a Start.
        // Empty pattern suppression applies only to pure pattern-related starts; allow rateChange or realign even if mask empty.
        if (shouldEmitStart && !resyncMatches)
        {
            int patternModeNow = patternBarsMode.load(std::memory_order_relaxed);
            if (patternModeNow > 0)
            {
                const uint16_t maskNow = patternStepsMask.load(std::memory_order_relaxed);
                if (maskNow == 0)
                {
                    shouldEmitStart = false;
   
                }
            }
        }
        int stopOffset = -1;
        if (shouldEmitStart && legacy)
        {
            const int gapSamples = juce::jmax(1, fastRoundPositive(prevSixteenthLenQ * samplesPerQuarter) - 1);
            stopOffset = juce::jmax(0, sampleOffset - gapSamples);
            if (stopOffset >= sampleOffset)
                stopOffset = juce::jmax(0, sampleOffset - 1);
            if (stopOffset < sampleOffset)
            {
                const auto stopMsg = juce::MidiMessage::midiStop();
                if (sendMidi)
                {
                    midi.addEvent(stopMsg, stopOffset);
                    extBuffer.addEvent(stopMsg, stopOffset);
                }
            }
        }

        // Only allow resyncMatches to bypass generated-start gating when the engine is running.
        const bool allowResyncBypass = resyncMatches && runActive;
        if (shouldEmitStart && (shouldAllowGeneratedStart() || allowResyncBypass))
        {
            const auto startMsg = juce::MidiMessage::midiStart();
            // If a pattern already emitted a Start at this exact sample earlier in
            // the block, avoid sending a duplicate and consume the pending
            // restart flags so the UI/state reflects the applied restart.
            if (sampleOffset == lastPatternStartSampleInBlock)
            {
                lastBarRestartStartSampleInBlock = sampleOffset;
                pendingBarRestart.store(false, std::memory_order_relaxed);
                uiNextRestartPending.store(false, std::memory_order_relaxed);
                pendingPatternRestartTargetBar.store(-1, std::memory_order_relaxed);
                // pattern already emitted the Start; record that the last source was pattern
                lastStartSource.store(1, std::memory_order_relaxed);
            }
            else
            {
                // Additional guard: if interval mode active and pattern empty, Start already suppressed above; no extra conditions.
                long long barForStart = computeCurrentBar(ppqStart + (double) sampleOffset / samplesPerQuarter, barLenQ);
                int stepForStart = resyncMatches ? resyncStepForStart : offsetStep;
                bool duplicate = isDuplicateStart(barForStart, stepForStart, sampleOffset);
                if (!duplicate && sendMidi)
                {
                    midi.addEvent(startMsg, sampleOffset);
                    extBuffer.addEvent(startMsg, sampleOffset);
                    lastStartBar.store(barForStart, std::memory_order_relaxed);
                    lastStartStep.store(stepForStart, std::memory_order_relaxed);

                    // Record source: resyncMatches => resync, otherwise bar-aligned restart
                    if (resyncMatches)
                    {
                        lastStartSource.store(3, std::memory_order_relaxed); // resync
                        resyncStartCount.fetch_add(1, std::memory_order_relaxed);
                    }
                    else
                    {
                        lastStartSource.store(2, std::memory_order_relaxed); // barRestart
                        barRestartStartCount.fetch_add(1, std::memory_order_relaxed);
                    }

                }

                lastBarRestartStartSampleInBlock = sampleOffset;
                if (resyncMatches)
                {
                    resyncPending.store(false, std::memory_order_relaxed);
                    resyncTargetBar.store(-1, std::memory_order_relaxed);
                    resyncTargetStep.store(-1, std::memory_order_relaxed);
                }
            }
        }

        else
        {
            // No Start/Stop emitted for rate-only or shuffle-only parameter changes.
            // Clear the marker so pattern path can still detect duplicates if needed.
            lastBarRestartStartSampleInBlock = -1;
        }
AfterBarRestartEmit:
        // Reflect selected step in UI at restart
        uiStep16.store(juce::jlimit(1, 16, offsetStep), std::memory_order_relaxed);

        // Schedule resync on rate change (unconditional) or Start events (Rule 1 when gate TRUE).
        if (haveRateChange)
        {
            long long curBar = computeCurrentBar(ppqStart, barLenQ);
            int currentStepApprox = juce::jlimit(1,16, uiStep16.load(std::memory_order_relaxed));
            attemptScheduleResync(curBar, currentStepApprox, offsetStep, true);
        }
        if ((haveStart || haveBarRestart || deferredMatches) && triggerModeEnabled.load(std::memory_order_relaxed) && !resyncMatches)
        {
            long long curBar = computeCurrentBar(ppqStart, barLenQ);
            int currentStepApprox = juce::jlimit(1,16, uiStep16.load(std::memory_order_relaxed));
            attemptScheduleResync(curBar, currentStepApprox, offsetStep, false);
        }

        if (haveRateChange)
        {
            currentRateIndex = pendingRateIndex;
            pendingRateIndex = -1;
            // changing resolution invalidates prior fractional carries -> reset
            resetAccumulators();
        }
        if (applyShuffleWithBoundary)
        {
            currentShuffleStep = pendingShuffleStep;
            pendingShuffleStep = -1;
            // shuffle applied as part of boundary: clear NEXT indicator if no other pending restart remain
        }
        if (haveStart || haveBarRestart || deferredMatches)
        {
            runActive = true;
            pendingStart = false;
            if (haveBarRestart || deferredMatches)
                pendingBarRestart.store(false, std::memory_order_relaxed);
            
            uiIsRunning.store(true, std::memory_order_relaxed);
            uiPendingStart.store(false, std::memory_order_relaxed);
            uiNextRestartPending.store(false, std::memory_order_relaxed);
            suppressUntilRestart = false; // allow clocks after scheduled restart
        }
        else if (haveRateChange || applyShuffleWithBoundary)
        {
            // Clear NEXT when changes were applied without a separate start request
            uiNextRestartPending.store(false, std::memory_order_relaxed);
        }

        if (legacy && stopOffset >= 0)
        {
            window.gapStart = stopOffset;
            window.gapEnd = sampleOffset;
        }
        else
        {
            window.gapStart = -1;
            window.gapEnd = -1;
        }
        window.startSample = sampleOffset;

        if (runWasActive && haveStart && ! haveBarRestart && ! haveRateChange)
            suppressClockAtBoundaryOnce = true;

    }
    return window;
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
    // Ensure editor-only UI properties are included in the saved state.
    // Some hosts may only persist parameter children — merge a small set of
    // known UI properties from the live APVTS state so they are always stored
    // with the plugin state (pattern-edit toggle, name/midi toggle, submenu,
    // selected instrument and instrumentNames list).
    {
        auto& live = parameters.state;
        if (live.hasProperty("ui.patternEditMode"))
            state.setProperty("ui.patternEditMode", live.getProperty("ui.patternEditMode"), nullptr);
        if (live.hasProperty("ui.showNameMode"))
            state.setProperty("ui.showNameMode", live.getProperty("ui.showNameMode"), nullptr);
        if (live.hasProperty("ui.setupSubmenuOn"))
            state.setProperty("ui.setupSubmenuOn", live.getProperty("ui.setupSubmenuOn"), nullptr);
        if (live.hasProperty("ui.selectedInstrument"))
            state.setProperty("ui.selectedInstrument", live.getProperty("ui.selectedInstrument"), nullptr);
        if (live.hasProperty("instrumentNames"))
            state.setProperty("instrumentNames", live.getProperty("instrumentNames"), nullptr);
    }
    if (auto xml = state.createXml()) {
        // pulseWidthMs is now stored in APVTS, no need to save manually
        copyXmlToBinary(*xml, destData);
    }
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

int ClockSyncAudioProcessor::getPendingPatternCount() const
{
    return (int) pendingPatternPPQ.size();
}

int ClockSyncAudioProcessor::getPendingShuffleStep() const
{
    return pendingShuffleStep;
}

