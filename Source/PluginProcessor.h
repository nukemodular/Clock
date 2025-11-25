#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

class ClockSyncAudioProcessor : public juce::AudioProcessor
{
public:
    //==============================================================================
    ClockSyncAudioProcessor();
    ~ClockSyncAudioProcessor() override = default;

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==============================================================================
    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    //==============================================================================
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState& getAPVTS() { return parameters; }
    
    // One-shot trigger request from the editor: retrigger at next 1/16, then restart at next bar
    void requestTriggerOnce(); // (modified) schedule Start at next BAR + offset step (never mid-bar)
    void setTriggerModeEnabled(bool enabled);
    void notifyResyncOffsetChanged();

    // UI helpers
    unsigned long long getUiClockCounter() const { return uiClockCounter.load(std::memory_order_relaxed); }
    int getUiStep16() const { return uiStep16.load(std::memory_order_relaxed); }
    bool getUiIsRunning() const { return uiIsRunning.load(std::memory_order_relaxed); }
    bool getUiPendingStart() const { return uiPendingStart.load(std::memory_order_relaxed); }
    bool getUiNextRestartPending() const { return uiNextRestartPending.load(std::memory_order_relaxed); }
    double getUiBpm() const { return uiBpm.load(std::memory_order_relaxed); }
    // Expose cached host bar number to UI/editor (-1 when unknown)
    int getUiExternalBarNumber() const { return uiExternalBarNumber.load(std::memory_order_relaxed); }
    // Expose cached time-signature and ppq-last-bar-start to UI/editor
    int getUiTimeSigNumerator() const;
    int getUiTimeSigDenominator() const;
    double getUiPpqPositionOfLastBarStart() const;
    // Consume and clear the host-start pending flag (atomic fetch-and-clear)
    bool consumeUiHostStartPending() { return uiHostStartPending.exchange(false, std::memory_order_acq_rel); }
    

    // Legacy mode: when enabled we emit a MIDI Stop a few ticks before each scheduled Start
    // (trigger restart, bar restart, rate change) and gate MIDI clock pulses between the Stop
    // and Start. Modern mode skips the pre‑Stop and allows continuous clock.
    void setLegacyMode(bool legacy) { legacyModeEnabled.store(legacy, std::memory_order_relaxed); }

    // External MIDI device selection API (used by editor)
    void setExternalDeviceId(const juce::String& id);
    juce::String getExternalDeviceId() const { return externalDeviceId; }

    // Parameter IDs
    static inline const juce::String paramClockRateIndex { "clockRateIndex" }; // choice index: 0..3 => 1/32,1/16,1/8,1/4
    static inline const juce::String paramClickLevelDb    { "clickLevelDb" }; // -12..0 dB
    // New click parameters:
    //  - paramClickRate: 0..4 discrete stages (0=off,1=4th,2=8th,3=16th,4=24ppq)
    //  - paramClickPulse: false=sample click (1-sample), true=1ms pulse
    static inline const juce::String paramClickRate      { "clickRate" };
    static inline const juce::String paramClickPulse     { "clickPulse" };
    static inline const juce::String paramRun             { "run" };            // true=running, false=stopped
    static inline const juce::String paramClockWhileStopped { "clockWhileStopped" }; // keep sending F8 while run==false
    static inline const juce::String paramTriggerModeEnabled { "triggerModeEnabled" }; // follow trigger with bar restart
    static inline const juce::String paramResyncOffsetStep { "resyncOffsetStep" }; // 1..16, step within bar where (re)start happens
    static inline const juce::String paramShuffleStep { "shuffleStep" }; // 1..7 discrete swing amount (1 none .. 7 heavy)
    static inline const juce::String paramDiagnostics { "diagnostics" }; // enable brief timing logs
    static inline const juce::String paramPatternBars { "patternBars" }; // OFF,1,2,4,8,16,32,64,RND
    static inline const juce::String paramPatternSteps { "patternSteps" }; // 16-bit bitmask persisted (default non-zero for first logical step)
    // Pattern start fine-tune parameter removed

        std::atomic<int> lastPatternBarsMode { 0 }; // track previous interval mode to detect changes
        std::atomic<bool> intervalModeChangedThisBlock { false }; // one-shot suppression flag: changing interval never emits Start

        // Helper: returns true if internally generated (non-host) Start messages are allowed right now.
        // Rule: If interval (patternBarsMode>0) is active AND the pattern step mask is empty (no bits), then
        // no internally generated Starts (manual trigger, pattern steps, bar restarts) are allowed. Host transport
        // Start/Continue remains unaffected.
        bool shouldAllowGeneratedStart() const noexcept {
            int mode = patternBarsMode.load(std::memory_order_relaxed);
            if (mode > 0) {
                uint16_t mask = patternStepsMask.load(std::memory_order_relaxed);
                if (mask == 0) return false;
            }
            return true;
        }
private:
    //==============================================================================
    juce::AudioProcessorValueTreeState parameters;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Runtime state
    double currentSampleRate { 44100.0 };
    double samplesPerQuarter { 44100.0 * 60.0 / 120.0 };
    bool lastWasPlaying { false };
    long long lastTickIndex { std::numeric_limits<long long>::min() };
    // Bar-aligned scheduling
    int currentRateIndex { 1 }; // default 1 => 1/16 => 24
    int pendingRateIndex { -1 };
    bool runActive { true };
    bool pendingStart { false };
    bool forceNextBarStart { false }; // when true, pendingStart will target the NEXT bar (always wrap) + offset step
    // Trigger and restart flags
    std::atomic<bool> triggerArmedForNextSixteenth { false }; // when true, send a Start at next 1/16 grid boundary
    std::atomic<bool> pendingBarRestart { false };    // restart at selected step
    std::atomic<bool> triggerModeEnabled { true };    // governs whether bar restart follows trigger
    bool lastTriggerModeEnabled { true };             // track rising edge to schedule restart when enabling while running
        bool suppressUntilRestart = false; // suppress clock emission until first scheduled restart boundary
        bool firstBlock = true;            // distinguish plugin load vs later transport edges
        bool lastRunParam { false };       // track parameter transitions
    int lastOffsetStep { 1 }; // cached offset step
    int currentShuffleStep { 1 }; // active shuffle step (1..7)
    int pendingShuffleStep { -1 }; // scheduled shuffle change (apply at next 8th boundary)
    double applyShuffleAtPPQ { -1.0 }; // unswung PPQ position where pending shuffle becomes active
    bool suppressClockAtBoundaryOnce { false };       // when true, skip sending clock on the exact Start frame once

    // Diagnostics
    bool diagnosticsEnabled { false };
    int diagPairsRemaining { 0 };
    long long diagLastLoggedPair { std::numeric_limits<long long>::min() };
    int diagLastSample { -1 };
    void debugLog(const juce::String& s);

    // Click generator
    float clickEnv { 0.0f };
    float clickGainLinear { 0.5f };
    float lastClickLevelDbCached { std::numeric_limits<float>::quiet_NaN() };

    // External MIDI output (device only)
    std::unique_ptr<juce::MidiOutput> externalMidiOut;
    juce::String externalDeviceId;   // identifier of selected external MIDI device
    void updateExternalOut();
    
    // Lightweight UI signal: incremented on each emitted MIDI clock
    std::atomic<unsigned long long> uiClockCounter { 0 };
    std::atomic<int> uiStep16 { 1 }; // 1..16 current step in bar
    std::atomic<bool> uiIsRunning { true };
    std::atomic<bool> uiPendingStart { false };
    std::atomic<bool> uiNextRestartPending { false }; // show "NEXT" when a bar+offset restart is scheduled
    std::atomic<double> uiBpm { 120.0 }; // host tempo (fallback 120)
    // Cached host-provided bar number (1-based). -1 means unknown/not provided.
    std::atomic<int> uiExternalBarNumber { -1 };
    // Cached host-provided time signature and last-bar PPQ. Denominator==0 => unknown.
    std::atomic<int> uiTimeSigNumerator { 0 };
    std::atomic<int> uiTimeSigDenominator { 0 };
    std::atomic<double> uiPpqPositionOfLastBarStart { -1.0 };
    // When true, suppress bar-aligned restarts (haveBarRestart) until the first
    // pattern interval has fired after a host START. This avoids an early
    // unrequested Start at the first few bars (e.g. bar 4) before the pattern
    // itself fires (e.g. interval=4).
    std::atomic<bool> deferBarRestartUntilPattern { false };
    std::atomic<long long> lastStartBar { -1 };
    std::atomic<int> lastStartStep { -1 }; // 1..16 when last Start emitted
    // Flag set when host transport START edge observed; editor may consume to trigger UI reset
    std::atomic<bool> uiHostStartPending { false };
    // Behaviour mode
    std::atomic<bool> legacyModeEnabled { false }; // false => modern (default), true => legacy pre-stop gating
    // Pattern sequencer state
    std::atomic<uint16_t> patternStepsMask { 0 }; // 16 bits
    std::atomic<int> patternBarsMode { 0 }; // 0=OFF,1=1,2=2,3=4,4=8,5=16,6=32,7=64,8=RND
    // Flag: set true when pattern mask transitions from empty (0) to non-empty (>0) so we can
    // schedule in-bar pattern targets immediately instead of waiting for the next interval bar.
    std::atomic<bool> patternMaskJustActivated { false };
    std::atomic<bool> patternModeJustChanged { false }; // one-block flag to suppress immediate pattern firing on interval change
    // Resync flag (rule set per workflow.md): mono pending target (bar,step). When set by a Start (and gate idx8 TRUE)
    // or by rate change (unconditional), or pattern completion (idx3≠1 & idx8 TRUE), it schedules a future Start at
    // offset step either in current bar (if ahead) or next bar (if behind). Replacement only if new target earlier.
    std::atomic<bool> resyncPending { false };            // true when a target is scheduled
    std::atomic<long long> resyncTargetBar { -1 };        // bar index (0-based) where resync should fire
    std::atomic<int> resyncTargetStep { -1 };             // 1..16 step within target bar
    long long lastPatternFiredBar { -1 }; // last bar number pattern fired
    long long nextRandomPatternTargetBar { -1 }; // target bar for random firing
    long long lastPatternRestartScheduledBar { -1 }; // bar number where a trigger-mode restart was scheduled from pattern
    std::vector<double> pendingPatternPPQ; // PPQ positions inside current bar to emit Start events for active steps
    // When a pattern requests a restart while the pattern is playing we defer the
    // actual bar+offset restart until the pattern finishes; this atomic stores
    // the bar number where the restart should be applied. -1 means none.
    std::atomic<long long> pendingPatternRestartTargetBar { -1 };
    // Per-block scratch: sample index where the last pattern-originated Start was emitted.
    // Used to avoid emitting duplicate Starts from both pattern and bar-restart codepaths.
    int lastPatternStartSampleInBlock { -1 };
    // Per-block scratch: sample index where the last bar-restart Start was emitted
    // (or consumed). Used to avoid emitting duplicate Starts from both codepaths.
    int lastBarRestartStartSampleInBlock { -1 };
    void updatePatternParams();
    void preparePatternForBar(double barStartPPQ, double barLenQ);
    // suppressDiag: when true, internal PATTERN_DIAG logging is reduced for idempotent second pass.
    void tryFirePattern(double ppqStart, double barLenQ, double barStartPPQ, int numSamples, juce::MidiBuffer& midi, juce::MidiBuffer& extClock, bool suppressDiag = false);
    int getPatternBarIntervalFromMode(int mode) const;
    // Attempt to (re)schedule resync target obeying shortest precedence.
    void attemptScheduleResync(long long currentBar, int currentStep, int offsetStep, bool force /* rate change */);
    long long computeCurrentBar(double ppqStart, double barLenQ) const { return (long long) std::floor(ppqStart / juce::jmax(1e-9, barLenQ)); }
    void setPatternSteps(uint16_t mask) { patternStepsMask.store(mask, std::memory_order_relaxed); }
    void setPatternBarsMode(int mode) { patternBarsMode.store(mode, std::memory_order_relaxed); }

    // Helpers
    int getClockResolution() const; // returns 48/24/12/6 based on currentRateIndex
    void updateDerivedParams();
    void generateClockAndClick(const juce::AudioPlayHead::CurrentPositionInfo& pos,
                               juce::AudioBuffer<float>&, juce::MidiBuffer&);
    // Persistent fractional accumulators for primary pulses (conservative persistence)
    // These carry fractional rounding remainders across blocks to reduce ±1-sample jitter
    // but are reset conservatively on transport/tempo/sample-rate/rate changes.
    double fracAccPrimaryBefore { 0.0 };
    double fracAccPrimaryAfter  { 0.0 };
    double lastBpmForAcc { -1.0 };
    void resetAccumulators();
    struct BarRestartWindow { int gapStart{-1}; int gapEnd{-1}; int startSample{-1}; bool hasBoundary() const { return startSample >= 0; } };
    BarRestartWindow handleBarAlignedChanges(const juce::AudioPlayHead::CurrentPositionInfo& pos,
                                             int numSamples,
                                             juce::MidiBuffer& midi,
                                             juce::MidiBuffer& extBuffer);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClockSyncAudioProcessor)
};

// Out-of-line definitions so the member atomics are declared before these are parsed.
inline int ClockSyncAudioProcessor::getUiTimeSigNumerator() const { return uiTimeSigNumerator.load(std::memory_order_relaxed); }
inline int ClockSyncAudioProcessor::getUiTimeSigDenominator() const { return uiTimeSigDenominator.load(std::memory_order_relaxed); }
inline double ClockSyncAudioProcessor::getUiPpqPositionOfLastBarStart() const { return uiPpqPositionOfLastBarStart.load(std::memory_order_relaxed); }
