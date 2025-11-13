#pragma once

#include <JuceHeader.h>

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

    bool acceptsMidi() const override { return false; }
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

    // Parameter IDs
    static inline const juce::String paramClockResolution { "clockResolution" }; // choice: 24,48,96
    static inline const juce::String paramClickEnable     { "clickEnable" };
    static inline const juce::String paramClickLevelDb    { "clickLevelDb" }; // -12..0 dB

private:
    //==============================================================================
    juce::AudioProcessorValueTreeState parameters;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Runtime state
    double currentSampleRate { 44100.0 };
    double samplesPerQuarter { 44100.0 * 60.0 / 120.0 };
    bool lastWasPlaying { false };
    double lastBlockPPQ { 0.0 };
    long long lastTickIndex { std::numeric_limits<long long>::min() };

    // Click generator
    float clickEnv { 0.0f };
    float clickGainLinear { 0.5f };

    // Helpers
    int getClockResolution() const; // returns 24/48/96
    void updateDerivedParams();
    void generateClockAndClick(const juce::AudioPlayHead::CurrentPositionInfo& pos,
                               juce::AudioBuffer<float>&, juce::MidiBuffer&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClockSyncAudioProcessor)
};
