#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

class ClockSyncAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ClockSyncAudioProcessorEditor(ClockSyncAudioProcessor&);
    ~ClockSyncAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    ClockSyncAudioProcessor& processor;

    // Timer
    void timerCallback() override;

    // UI components
    // Rate selection: 4 custom text buttons
    juce::TextButton rateBtn32 { "32" };
    juce::TextButton rateBtn16 { "16" };
    juce::TextButton rateBtn8  { "8" };
    juce::TextButton rateBtn4  { "4" };
    juce::ComboBox deviceBox;
    juce::TextButton refreshButton { "RESET" };
    juce::TextButton clickButton { "CLICK" };
    juce::ToggleButton runToggle { "Run" };
    juce::TextButton keepClockButton { "IDLE CLK" };
    juce::Slider clickLevelSlider;
    juce::Label resolutionLabel { {}, {} };
    juce::Label deviceLabel { {}, {} };
    juce::Label clickLevelLabel { {}, {} };

    // Attachments
    // Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> runAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> clickEnableAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> clickLevelAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> keepClockAttachment;

    // Helpers
    void refreshDeviceList();
    std::vector<juce::MidiDeviceInfo> midiOutputs;

    // Simple LED animation based on clock ticks
    unsigned long long lastSeenClockCounter { 0 };
    float ledLevel { 0.0f }; // 0..1 decays over time
    bool runParamCached { true };
    int rateIndexCached { 1 };
    bool pendingStartCached { false };
    bool engineRunningCached { true };

    // Custom look for the click level slider
    class SimpleSliderLNF;
    std::unique_ptr<SimpleSliderLNF> clickLNF;

    // Themed LNF for rate buttons, refresh button, and combo
    class ThemeLNF;
    std::unique_ptr<ThemeLNF> themeLNF;

    // Layout cache for ring visualisation
    juce::Rectangle<int> ringArea;
    juce::Rectangle<int> triggerRect; // 50x50 trigger circle in bottom-right
    float triggerFade { 0.0f };       // 0..1, blue -> red fade after click

    // Cached rate parameter pointer to avoid repeated dynamic_cast in timer
    juce::AudioParameterChoice* rateParam { nullptr };

    // Drawing helpers
    void drawRing(juce::Graphics& g);
    void drawTrigger(juce::Graphics& g);

    // Constants
    static constexpr int kRingOuterD = 160;
    static constexpr int kRingInnerD = 120;
    static constexpr int kTriggerSize = 50;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClockSyncAudioProcessorEditor)
};
