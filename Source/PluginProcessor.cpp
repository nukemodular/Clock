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
}

void ClockSyncAudioProcessor::setTriggerModeEnabled(bool enabled)
{
    triggerModeEnabled.store(enabled, std::memory_order_relaxed);
}

void ClockSyncAudioProcessor::notifyResyncOffsetChanged()
{
    pendingBarRestart.store(true, std::memory_order_relaxed);
    uiNextRestartPending.store(true, std::memory_order_relaxed);
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

    // Optional diagnostics (off by default). When enabled, logs a few swing timing lines to ~/Documents/ClockSync_Log.txt
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        paramDiagnostics, "Diagnostics", false));

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
    if (auto* tv = parameters.getRawParameterValue(paramTriggerModeEnabled))
    {
        const bool tm = tv->load() > 0.5f;
        triggerModeEnabled.store(tm, std::memory_order_relaxed);
        lastTriggerModeEnabled = tm;
    }
    else
    {
        triggerModeEnabled.store(true, std::memory_order_relaxed);
        lastTriggerModeEnabled = true;
    }
    suppressUntilRestart = false;
    firstBlock = true;
    if (auto* pi = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter(paramResyncOffsetStep)))
        lastOffsetStep = juce::jlimit(1, 16, pi->get());
    if (auto* si = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter(paramShuffleStep)))
        currentShuffleStep = juce::jlimit(1, 7, si->get());
    pendingShuffleStep = -1;
    applyShuffleAtPPQ = -1.0;
    diagnosticsEnabled = false;
    diagPairsRemaining = 0;
    diagLastLoggedPair = std::numeric_limits<long long>::min();
    diagLastSample = -1;
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

void ClockSyncAudioProcessor::debugLog(const juce::String& s)
{
    juce::File f = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                        .getChildFile("ClockSync_Log.txt");
    if (auto stream = f.createOutputStream(1024))
    {
        stream->setPosition(stream->getFile().getSize());
        stream->writeText(s + "\n", false, false, "UTF-8");
    }
}

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
        // No transport info; nothing to schedule for audio clicks.
        clickEnv = 0.0f;
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
    int clickRate = 0;
    if (auto* cri = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter(paramClickRate)))
        clickRate = juce::jlimit(0, 4, cri->get());
    const bool clickEnabled = (clickRate > 0);
    const bool clickPulse = parameters.getRawParameterValue(paramClickPulse)->load() > 0.5f;
    // Diagnostics toggle (arm a short capture window when enabled)
    if (parameters.getRawParameterValue(paramDiagnostics)->load() > 0.5f)
    {
        if (! diagnosticsEnabled)
        {
            diagnosticsEnabled = true;
            diagPairsRemaining = 4; // capture next 4 pairs
            diagLastLoggedPair = std::numeric_limits<long long>::min();
            diagLastSample = -1;
            debugLog("--- Diagnostics armed ---");
        }
    }
    else
    {
        diagnosticsEnabled = false;
    }
    // Pull desired rate index; defer application to the next scheduled restart ("NEXT")
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(parameters.getParameter(paramClockRateIndex)))
    {
        const int desired = choice->getIndex();
        if (desired != currentRateIndex && pendingRateIndex != desired)
        {
            pendingRateIndex = desired;
            uiNextRestartPending.store(true, std::memory_order_relaxed);
        }
    }
    // Capture the resolution (locked to 24 for MIDI clock)
    const int resolutionBefore = getClockResolution();

    // Run control param
    const bool runParam = parameters.getRawParameterValue(paramRun)->load() > 0.5f;
    const bool keepClockStopped = parameters.getRawParameterValue(paramClockWhileStopped)->load() > 0.5f;
    // Allow MIDI emission when Run is on, or when the user explicitly enabled clock while stopped
    const bool allowMidiOut = runParam || keepClockStopped;

    // Detect run-param transitions (true -> false) so we can emit an immediate
    // MIDI Stop to external devices when user turns Run off. This makes the
    // UI Run toggle reliably stop external drum machines even if host
    // transport/clock settings would otherwise keep running.
    if (lastRunParam != runParam)
    {
        if (! runParam)
        {
            // Send an immediate Stop message at the start of this block.
            const auto stopMsg = juce::MidiMessage::midiStop();
            midi.addEvent(stopMsg, 0);
            if (externalMidiOut)
            {
                juce::MidiBuffer extStop;
                extStop.addEvent(stopMsg, 0);
                externalMidiOut->sendBlockOfMessages(extStop, juce::Time::getMillisecondCounterHiRes(), currentSampleRate);
            }
            // Clear running state
            runActive = false;
            pendingStart = false;
            uiIsRunning.store(false, std::memory_order_relaxed);
            uiPendingStart.store(false, std::memory_order_relaxed);
        }
        lastRunParam = runParam;
    }

    const double bpm = (pos.bpm > 0.0 ? pos.bpm : 120.0);
    uiBpm.store(bpm, std::memory_order_relaxed);
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
            // When user has Run==off, suppress sending any MIDI stop messages.
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
    // removed unused lastRunParam

    // Trigger mode rising edge while already running (engine active): schedule a bar+offset restart.
    // Placed before host transport edge handling so NEXT indicator can appear immediately after user toggles.
    {
        const bool tmNow = triggerModeEnabled.load(std::memory_order_relaxed);
        if (runActive && tmNow && ! lastTriggerModeEnabled)
        {
            pendingBarRestart.store(true, std::memory_order_relaxed);
            uiNextRestartPending.store(true, std::memory_order_relaxed);
        }
        lastTriggerModeEnabled = tmNow;
    }

    // Transport state transitions: emit Start/Continue/Stop when host play toggles
    int hostStartSampleThisBlock = -1; // if host started this block, mark sample 0 as a boundary (we'll allow a clock on this frame)
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
        const bool suppressStart = firstBlock && initialStep > 1; // plugin loaded while host mid-bar
        if (suppressStart)
        {
            // Arm restart at next configured offset step (default 1) and show NEXT; suppress clocks until applied.
            pendingStart = true;
            runActive = false;
            uiIsRunning.store(false, std::memory_order_relaxed);
            uiPendingStart.store(true, std::memory_order_relaxed);
            uiNextRestartPending.store(true, std::memory_order_relaxed);
            suppressUntilRestart = true;
        }
        else
        {
            // Normal Start/Continue emission at transport edge.
            const bool atStart = (ppqStart < 1e-6);
            const auto startMsg = atStart ? juce::MidiMessage::midiStart() : juce::MidiMessage::midiContinue();
            if (allowMidiOut)
            {
                midi.addEvent(startMsg, 0);
                haveHostStartMsg = true;
                hostStartMsg = startMsg;
                runActive = true;
                pendingStart = false;
                uiIsRunning.store(true, std::memory_order_relaxed);
                uiPendingStart.store(false, std::memory_order_relaxed);
                hostStartSampleThisBlock = 0;
                uiStep16.store(1, std::memory_order_relaxed);
            }
            else
            {
                // Run is off, suppress emitting Start/Continue and don't set runActive.
                runActive = false;
                pendingStart = false;
                uiIsRunning.store(false, std::memory_order_relaxed);
                uiPendingStart.store(false, std::memory_order_relaxed);
            }
        }
        firstBlock = false;
    }
    else if (! isPlaying && lastWasPlaying)
    {
        const auto stopMsg = juce::MidiMessage::midiStop();
        if (allowMidiOut)
        {
            midi.addEvent(stopMsg, 0);
            if (externalMidiOut)
            {
                juce::MidiBuffer extStop;
                extStop.addEvent(stopMsg, 0);
                externalMidiOut->sendBlockOfMessages(extStop, juce::Time::getMillisecondCounterHiRes(), currentSampleRate);
            }
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
        juce::MidiBuffer extClock;
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
                const double mod = std::fmod(ppqStart, gridQ);
                const bool onGrid = std::fabs(mod) <= eps || std::fabs(mod - gridQ) <= eps;
                const double nextGrid = onGrid ? ppqStart : (std::floor(ppqStart / gridQ) + 1.0) * gridQ;
                nextBoundaryQ = nextGrid;
            }
            const double deltaQ = juce::jmax(0.0, nextBoundaryQ - ppqStart);
            const int nextGridOffset = fastRoundPositive(deltaQ * samplesPerQuarter);
            if (triggerArmedForNextSixteenth.load(std::memory_order_relaxed))
            {
                if (nextGridOffset >= 0 && nextGridOffset < numSamples)
                {
                    const auto startMsg = juce::MidiMessage::midiStart();
                    addBoth(midi, extClock, startMsg, nextGridOffset);
                    // Consume arm; optional bar restart if trigger mode enabled
                    triggerArmedForNextSixteenth.store(false, std::memory_order_relaxed);
                    if (triggerModeEnabled.load(std::memory_order_relaxed))
                    {
                        pendingBarRestart.store(true, std::memory_order_relaxed);
                        // Schedule restart at next occurrence of step within bar (modulo)
                        uiNextRestartPending.store(true, std::memory_order_relaxed);
                    }
                    // Allow clock at same frame (no suppression)
                }
                // If offset beyond block, keep armed until next block (no change)
            }
        }

        auto barWindow = handleBarAlignedChanges(pos, numSamples, midi, extClock);
        gapStart = barWindow.gapStart;
        gapEnd = barWindow.gapEnd;
        startSampleAtBoundary = barWindow.startSample;

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

                // Pulse-distribution swing mapping:
                // We distribute the 12 clock pulses inside each 1/8 pair across two 16ths whose lengths vary with swing.
                // firstLen = 0.25 + shiftQ, secondLen = 0.25 - shiftQ, shiftQ max ~= 1/32 (0.03125 QN) for conservative timing.
                auto mapSwingPulse = [&](long long tickIndex, int step) -> double {
                    if (step <= 1) return (double) tickIndex / (double) resolutionBefore; // no swing
                    // Exact linear mapping: shiftQ = (step-1) * (1/48) QN so step 7 => 6/48 = 0.125
                    const double shiftQ = (double)(step - 1) * (1.0 / 48.0);
                    const double pairLenQ = 0.5; // length of two 16ths
                    const long long pulsesPerQuarter = resolutionBefore;
                    const long long pulsesPerPair = (long long) std::llround(pairLenQ * (double) pulsesPerQuarter); // 12 at 24 PPQN
                    const long long pairIndex = tickIndex / pulsesPerPair;
                    const long long indexInPair = tickIndex % pulsesPerPair; // 0..11
                    const double firstLen = 0.25 + shiftQ;
                    const double secondLen = 0.25 - shiftQ;
                    // Uniform spacing within each half
                    const int pulsesPer16th = pulsesPerPair / 2; // 6
                    double pairStart = pairIndex * pairLenQ;
                    if (indexInPair < pulsesPer16th)
                    {
                        // First 16th
                        const double spacing = firstLen / (double) pulsesPer16th;
                        return pairStart + (double) indexInPair * spacing;
                    }
                    else
                    {
                        // Second 16th
                        const double spacing = secondLen / (double) pulsesPer16th;
                        const long long idxSecond = indexInPair - pulsesPer16th; // 0..5
                        return pairStart + firstLen + (double) idxSecond * spacing;
                    }
                };

                double tickPPQ = mapSwingPulse(t, currentShuffleStep);
                const double deltaQuarter = tickPPQ - ppqStart;
                const int mappedOffset = fastRoundPositive(deltaQuarter * samplesPerQuarter);
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
                    if (allowMidiOut)
                    {
                        midi.addEvent(clockMsg, sampleOffset);
                        uiClockCounter.fetch_add(1, std::memory_order_relaxed);
                        if (externalMidiOut)
                            extClock.addEvent(clockMsg, sampleOffset);
                    }
                }
                if (diagnosticsEnabled && diagPairsRemaining > 0)
                {
                    const long long pairIdx = (long long) std::floor((tickPPQ - lastBar) / 0.5);
                    const double norm = (tickPPQ - lastBar) / juce::jmax(1e-9, barLenQ);
                    const double shiftQCur = currentShuffleStep <= 1 ? 0.0 : (double)(currentShuffleStep - 1) * (1.0 / 48.0);
                    if (pairIdx != diagLastLoggedPair)
                    {
                        debugLog("pair " + juce::String(pairIdx) + " (pre) step=" + juce::String(currentShuffleStep) + " shiftQ=" + juce::String(shiftQCur, 6));
                        diagLastLoggedPair = pairIdx;
                        --diagPairsRemaining;
                        diagLastSample = -1;
                    }
                    if (diagLastSample >= 0)
                        debugLog("  pulse sample=" + juce::String(sampleOffset) + " dt=" + juce::String(sampleOffset - diagLastSample) + " norm=" + juce::String(norm, 6));
                    else
                        debugLog("  pulse sample=" + juce::String(sampleOffset) + " norm=" + juce::String(norm, 6));
                    diagLastSample = sampleOffset;
                }
                    if (clickEnabled)
                    {
                        int stepSpan = 1;
                        switch (clickRate)
                        {
                            case 1: stepSpan = resolutionBefore; break;                 // quarter note
                            case 2: stepSpan = juce::jmax(1, resolutionBefore / 2); break; // eighth
                            case 3: stepSpan = juce::jmax(1, resolutionBefore / 4); break; // sixteenth
                            case 4: stepSpan = 1; break; // every midi-clock pulse (24ppq)
                            default: stepSpan = juce::jmax(1, resolutionBefore / 4); break;
                        }
                        if ((t % stepSpan) == 0)
                        {
                            const bool quarterBoundary = (t % resolutionBefore) == 0;
                            const float baseLevel = quarterBoundary ? 1.0f : 0.7f;
                            const float outLevel = baseLevel * clickGainLinear;
                            const int pulseSamples = (int) juce::jmax(1, (int) std::round(0.001 * currentSampleRate));
                            if (! clickPulse)
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
                                // 1ms pulse: distribute a decaying linear pulse across pulseSamples
                                for (int s = 0; s < pulseSamples; ++s)
                                {
                                    const int idx = sampleOffset + s;
                                    if (idx >= 0 && idx < numSamples)
                                    {
                                        const float env = 1.0f - (float) s / (float) pulseSamples;
                                        const float val = outLevel * env;
                                        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                                            buffer.addSample(ch, idx, val);
                                    }
                                }
                            }
                        }
                    }
                lastProcessedT = t;
            }
            lastTickIndex = lastProcessedT;
        }

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
                auto mapSwingPulsePost = [&](long long tickIndex, int step) -> double {
                    if (step <= 1) return (double) tickIndex / (double) resolutionAfter;
                    const double shiftQ = (double)(step - 1) * (1.0 / 48.0);
                    const double pairLenQ = 0.5;
                    const long long pulsesPerQuarter = resolutionAfter;
                    const long long pulsesPerPair = (long long) std::llround(pairLenQ * (double) pulsesPerQuarter);
                    const long long pairIndex = tickIndex / pulsesPerPair;
                    const long long indexInPair = tickIndex % pulsesPerPair;
                    const double firstLen = 0.25 + shiftQ;
                    const double secondLen = 0.25 - shiftQ;
                    const int pulsesPer16th = pulsesPerPair / 2;
                    double pairStart = pairIndex * pairLenQ;
                    if (indexInPair < pulsesPer16th)
                    {
                        const double spacing = firstLen / (double) pulsesPer16th;
                        return pairStart + (double) indexInPair * spacing;
                    }
                    else
                    {
                        const double spacing = secondLen / (double) pulsesPer16th;
                        const long long idxSecond = indexInPair - pulsesPer16th;
                        return pairStart + firstLen + (double) idxSecond * spacing;
                    }
                };
                double tickPPQ = mapSwingPulsePost(t, currentShuffleStep);
                const double deltaQuarter = tickPPQ - ppqStart;
                const int mappedOffset = fastRoundPositive(deltaQuarter * samplesPerQuarter);
                if (mappedOffset > numSamples - 1)
                {
                    break; // carry into next block; do not advance lastTickIndex
                }
                const int sampleOffset = juce::jlimit(0, numSamples - 1, mappedOffset);
                // Past boundary; optionally suppress clock on the exact Start frame to avoid double clocks with Start
                if (! suppressUntilRestart && !(forbiddenClockSample >= 0 && sampleOffset == forbiddenClockSample))
                {
                    if (allowMidiOut)
                    {
                        midi.addEvent(clockMsg, sampleOffset);
                        uiClockCounter.fetch_add(1, std::memory_order_relaxed);
                        if (externalMidiOut)
                            extClock.addEvent(clockMsg, sampleOffset);
                    }
                }
                if (diagnosticsEnabled && diagPairsRemaining > 0)
                {
                    const long long pairIdx = (long long) std::floor((tickPPQ - lastBar) / 0.5);
                    const double norm = (tickPPQ - lastBar) / juce::jmax(1e-9, barLenQ);
                    const double shiftQCur = currentShuffleStep <= 1 ? 0.0 : (double)(currentShuffleStep - 1) * (1.0 / 48.0);
                    if (pairIdx != diagLastLoggedPair)
                    {
                        debugLog("pair " + juce::String(pairIdx) + " (post) step=" + juce::String(currentShuffleStep) + " shiftQ=" + juce::String(shiftQCur, 6));
                        diagLastLoggedPair = pairIdx;
                        --diagPairsRemaining;
                        diagLastSample = -1;
                    }
                    if (diagLastSample >= 0)
                        debugLog("  pulse sample=" + juce::String(sampleOffset) + " dt=" + juce::String(sampleOffset - diagLastSample) + " norm=" + juce::String(norm, 6));
                    else
                        debugLog("  pulse sample=" + juce::String(sampleOffset) + " norm=" + juce::String(norm, 6));
                    diagLastSample = sampleOffset;
                }
                    if (clickEnabled)
                    {
                        int stepSpan = 1;
                        switch (clickRate)
                        {
                            case 1: stepSpan = resolutionAfter; break;
                            case 2: stepSpan = juce::jmax(1, resolutionAfter / 2); break;
                            case 3: stepSpan = juce::jmax(1, resolutionAfter / 4); break;
                            case 4: stepSpan = 1; break;
                            default: stepSpan = juce::jmax(1, resolutionAfter / 4); break;
                        }
                        if ((t % stepSpan) == 0)
                        {
                            const bool quarterBoundary = (t % resolutionAfter) == 0;
                            const float baseLevel = quarterBoundary ? 1.0f : 0.7f;
                            const float outLevel = baseLevel * clickGainLinear;
                            const int pulseSamples = (int) juce::jmax(1, (int) std::round(0.001 * currentSampleRate));
                            if (! clickPulse)
                            {
                                if (mappedOffset >= 0 && mappedOffset < numSamples)
                                {
                                    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                                        buffer.addSample(ch, mappedOffset, outLevel);
                                }
                            }
                            else
                            {
                                for (int s = 0; s < pulseSamples; ++s)
                                {
                                    const int idx = mappedOffset + s;
                                    if (idx >= 0 && idx < numSamples)
                                    {
                                        const float env = 1.0f - (float) s / (float) pulseSamples;
                                        const float val = outLevel * env;
                                        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                                            buffer.addSample(ch, idx, val);
                                    }
                                }
                            }
                        }
                    }
                lastProcessedT = t;
            }
            lastTickIndex = lastProcessedT;
            // Consume the one-shot suppression
            suppressClockAtBoundaryOnce = false;
        }

        if (externalMidiOut && extClock.getNumEvents() > 0)
            externalMidiOut->sendBlockOfMessages(extClock, juce::Time::getMillisecondCounterHiRes(), currentSampleRate);
    }

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
    const bool haveBarRestart = pendingBarRestart.load(std::memory_order_relaxed);
    const bool applyShuffleWithBoundary = haveShuffleChange && (haveRateChange || haveStart || haveBarRestart);

    // Compute the next occurrence of the selected step (1..16) at or after 'now'.
    // If the selected step lies ahead in the current bar, use it; otherwise target the same step in the next bar.
    int offsetStep = 1;
    if (auto* pi = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter(paramResyncOffsetStep)))
        offsetStep = juce::jlimit(1, 16, pi->get());
    else if (auto* p = parameters.getParameter(paramResyncOffsetStep))
        offsetStep = juce::jlimit(1, 16, (int) juce::roundToInt(p->getValue() * 15.0f + 1.0f));
    const int stepIndex0 = offsetStep - 1; // 0..15
    // Swing-aware mapping: if shuffle active (>1) and 4/4, recompute target boundary using shifted second 16th lengths.
    double stepQWithinBar = (barLenQ / 16.0) * (double) stepIndex0; // default unswung
    if (currentShuffleStep > 1 && std::fabs(barLenQ - 4.0) < 1e-6)
    {
        const double shiftQ = (double)(currentShuffleStep - 1) * (1.0 / 48.0);
        int pairIndex = stepIndex0 / 2; // 0..7
        bool secondHalf = (stepIndex0 % 2) == 1;
        double pairStart = pairIndex * 0.5; // pair length always 0.5 quarter
        stepQWithinBar = secondHalf ? (pairStart + 0.25 + shiftQ) : pairStart;
    }

    if (haveStart || haveBarRestart || haveRateChange) // exclude pure shuffle change
    {
        // Schedule at the next occurrence of the selected step within the bar (modulo behavior)
        double deltaQ = stepQWithinBar - posInBar;
        if (deltaQ < -kBarEps)
            deltaQ += barLenQ; // wrap into the next bar if passed
        if (posInBar <= kBarEps && stepIndex0 == 0)
            deltaQ = 0.0; // allow immediate restart at bar start when offset=1

        sampleOffset = fastRoundPositive(deltaQ * samplesPerQuarter);
    }

    if (sampleOffset <= numSamples - 1 && (haveRateChange || haveStart || haveBarRestart))
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
        const int gapSamples = juce::jmax(1, fastRoundPositive(prevSixteenthLenQ * samplesPerQuarter) - 1);
        int stopOffset = juce::jmax(0, sampleOffset - gapSamples);
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

        const auto startMsg = juce::MidiMessage::midiStart();
        if (sendMidi)
        {
            midi.addEvent(startMsg, sampleOffset);
            extBuffer.addEvent(startMsg, sampleOffset);
        }
        // Reflect selected step in UI at restart
        uiStep16.store(juce::jlimit(1, 16, offsetStep), std::memory_order_relaxed);

        if (haveRateChange)
        {
            currentRateIndex = pendingRateIndex;
            pendingRateIndex = -1;
        }
        if (applyShuffleWithBoundary)
        {
            currentShuffleStep = pendingShuffleStep;
            pendingShuffleStep = -1;
            // shuffle applied as part of boundary: clear NEXT indicator if no other pending restart remain
        }
        if (haveStart || haveBarRestart)
        {
            runActive = true;
            pendingStart = false;
            if (haveBarRestart)
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

        window.gapStart = stopOffset;
        window.gapEnd = sampleOffset;
        window.startSample = sampleOffset;

        if (runWasActive && haveStart && ! haveBarRestart && ! haveRateChange)
            suppressClockAtBoundaryOnce = true;
        // No extra diagnostics in original behaviour
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
