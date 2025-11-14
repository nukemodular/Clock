#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "StepOffsetButton.h"
#include "GridScaleButton.h"
#include "EllipseToggleButton.h"
#include "ArcSizeButtons.h"

class ClockSyncAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ClockSyncAudioProcessorEditor(ClockSyncAudioProcessor&);
    ~ClockSyncAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    // Draw elements that must appear above child components (e.g. trigger ellipse)
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    ClockSyncAudioProcessor& processor;

    // Timer
    void timerCallback() override;

    // UI components
    // Rate selection: 4 custom text buttons
    // Legacy rate buttons removed (replaced by gridScaleButton)
    juce::ComboBox deviceBox;
    juce::TextButton refreshButton { "RESET" };
    // Small center-dot toggle used to enable/disable click
    class SmallDotToggle : public juce::ToggleButton {
    public:
        void setColours(juce::Colour offCol, juce::Colour onCol) { off = offCol; on = onCol; repaint(); }
        void paintButton(juce::Graphics& g, bool, bool) override {
            auto b = getLocalBounds().toFloat();
            auto d = std::min(b.getWidth(), b.getHeight());
            auto r = juce::Rectangle<float>(b.getCentreX() - d * 0.5f, b.getCentreY() - d * 0.5f, d, d);
            g.setColour(getToggleState() ? on : off);
            g.fillEllipse(r);
        }
    private:
        juce::Colour off { juce::Colours::red };
        juce::Colour on  { juce::Colours::cyan };
    } clickButton;
    juce::ToggleButton runToggle { "Run" };
    // Replaces keepClockButton with a circular toggle
    juce::Slider clickLevelSlider;
    juce::Label resolutionLabel { {}, {} };
    juce::Label deviceLabel { {}, {} };
    juce::Label clickLevelLabel { {}, {} };
    AnimatedStepOffsetButton stepOffsetButton;
    // New grid scale radial selector replacing four rate buttons
    GridScaleButton gridScaleButton; // manages 1/32..1/4 selection
    // New: toggle to control whether trigger arms a bar-restart (+/- offset)
    EllipseToggleButton triggerModeToggle;
    // New: idle clock toggle (32x32) near top-left
    EllipseToggleButton idleClockToggle;
    // New: shuffle scale toggle at (65,195) size 40x40
    EllipseToggleButton shuffleScaleToggle;
    ArcSizeButtons arcSizeButtons; // 7 arc-arranged size-gradient buttons 1..7

    // Attachments
    // Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> runAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> clickEnableAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> clickLevelAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> idleClockAttachment;

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

    // Custom rotary look for the click level slider
    class ClickRotaryLNF;
    std::unique_ptr<ClickRotaryLNF> clickRotaryLNF;

    // Themed LNF for rate buttons, refresh button, and combo
    class ThemeLNF;
    std::unique_ptr<ThemeLNF> themeLNF;

    // Layout cache for ring visualisation
    juce::Rectangle<int> ringArea;
    juce::Rectangle<int> triggerRect; // 50x50 trigger circle in bottom-right
    float triggerFade { 0.0f };       // 0..1, blue -> red fade after click
    // Step number fade shown inside triggerRect, updates each 16th
    int stepNumberCached { 0 };
    // Selected arc size (1..7) used as shuffleValue
    int shuffleValue { 4 }; // default middle

    // Cached rate parameter pointer to avoid repeated dynamic_cast in timer
    juce::AudioParameterChoice* rateParam { nullptr };

    // Drawing helpers
    void drawIdleClockCurvedLabel(juce::Graphics& g);
    void drawRing(juce::Graphics& g);
    void drawTrigger(juce::Graphics& g);
   

    // Constants
    static constexpr int kRingOuterD = 160;
    static constexpr int kRingInnerD = 120;
    static constexpr int kTriggerSize = 50;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClockSyncAudioProcessorEditor)
};
