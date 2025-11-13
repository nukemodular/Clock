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
    void requestTriggerOnce();

    // UI helpers
    unsigned long long getUiClockCounter() const { return uiClockCounter.load(std::memory_order_relaxed); }
    int getUiStep16() const { return uiStep16.load(std::memory_order_relaxed); }
    bool getUiIsRunning() const { return uiIsRunning.load(std::memory_order_relaxed); }
    bool getUiPendingStart() const { return uiPendingStart.load(std::memory_order_relaxed); }

    // External MIDI device selection API (used by editor)
    void setExternalDeviceId(const juce::String& id);
    juce::String getExternalDeviceId() const { return externalDeviceId; }

    // Parameter IDs
    static inline const juce::String paramClockRateIndex { "clockRateIndex" }; // choice index: 0..3 => 1/32,1/16,1/8,1/4
    static inline const juce::String paramClickEnable     { "clickEnable" };
    static inline const juce::String paramClickLevelDb    { "clickLevelDb" }; // -12..0 dB
    static inline const juce::String paramRun             { "run" };            // true=running, false=stopped
    static inline const juce::String paramClockWhileStopped { "clockWhileStopped" }; // keep sending F8 while run==false

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
    int lastRunParam { 1 }; // 1=true, 0=false
    // Trigger and restart flags
    std::atomic<bool> pendingRetrigger16th { false }; // schedule immediate retrigger on next 1/16 boundary
    std::atomic<bool> pendingBarRestart { false };    // after retrigger, also restart at next bar
    bool suppressClockAtBoundaryOnce { false };       // when true, skip sending clock on the exact Start frame once

    // Click generator
    float clickEnv { 0.0f };
    float clickGainLinear { 0.5f };

    // External MIDI output (device only)
    std::unique_ptr<juce::MidiOutput> externalMidiOut;
    juce::String externalDeviceId;   // identifier of selected external MIDI device
    void updateExternalOut();
    
    // Lightweight UI signal: incremented on each emitted MIDI clock
    std::atomic<unsigned long long> uiClockCounter { 0 };
    std::atomic<int> uiStep16 { 1 }; // 1..16 current step in bar
    std::atomic<bool> uiIsRunning { true };
    std::atomic<bool> uiPendingStart { false };

    // Helpers
    int getClockResolution() const; // returns 48/24/12/6 based on currentRateIndex
    void updateDerivedParams();
    void generateClockAndClick(const juce::AudioPlayHead::CurrentPositionInfo& pos,
                               juce::AudioBuffer<float>&, juce::MidiBuffer&);
    void handleBarAlignedChanges(const juce::AudioPlayHead::CurrentPositionInfo& pos,
                                 int numSamples,
                                 juce::MidiBuffer& midi,
                                 juce::MidiBuffer& extBuffer,
                                 int& gapStartSample,
                                 int& gapEndSample,
                                 int& startSampleOut);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClockSyncAudioProcessor)
};
