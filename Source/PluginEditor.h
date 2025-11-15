#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_animation/juce_animation.h>
#include "PluginProcessor.h"
#include "StepOffsetMenu.h"
#include "GridScaleMenu.h"
#include "RingToggle.h"
#include "ShuffleModeMenu.h"
#include "LookAndFeels.h" // Centralised LookAndFeel & theme colours

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
    // Legacy rate buttons removed (replaced by gridScaleMenu)
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
    // Replaces keepClockButton with a circular toggle
    juce::Slider clickLevelSlider;
    AnimatedStepOffsetMenu stepOffsetMenu;
    // New grid scale radial selector replacing four rate buttons
    GridScaleMenu gridScaleMenu; // manages 1/32..1/4 selection
    // New: toggle to control whether trigger arms a bar-restart (+/- offset)
    RingToggle triggerModeToggle;
    // New: idle clock toggle (32x32) near top-left
    RingToggle idleClockToggle;
    // New: shuffle scale toggle at (65,195) size 40x40
    RingToggle shuffleScaleToggle;
    ShuffleModeMenu shuffleModeMenu; // 7 arc-arranged size-gradient buttons 1..7

    // Attachments
    // Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> clickEnableAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> clickLevelAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> idleClockAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> triggerModeAttachment;

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

    // Custom rotary & button LookAndFeels (now using global definitions from LookAndFeels.h)
    std::unique_ptr<ClickRotaryLNF> clickRotaryLNF;
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
    void openCurvedLabelEditor();
    void drawRing(juce::Graphics& g);
    void drawTrigger(juce::Graphics& g);
    void drawDancer(juce::Graphics& g);
    void loadDancerFrames();
    void startAsyncDancerLoad();
    static juce::File findDancerFolder();
    static void scrubSvgColours(juce::XmlElement& el, juce::Colour accent);
   

    // Constants
    static constexpr int kRingOuterD = 160;
    static constexpr int kRingInnerD = 120;

    // Editable curved label text drawn along the decorative bezier
    juce::String curvedLabelText { "TB-303" };
    

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClockSyncAudioProcessorEditor)

    // Dancer animation
    std::vector<std::unique_ptr<juce::Drawable>> dancerFrames;
    int dancerFrameCount { 0 }; // Populated from layers in dancer_all.svg
    // Cached last dancer frame (freeze when stopped)
    int dancerLastFrame { 0 };
    std::atomic<bool> dancerLoading { false }; // true while background thread parsing SVG
    double dancerLoadStartMs { 0.0 }; // start timestamp for async load
    // Last clock counter value actually used to advance the dancer.
    // This lets us cheaply skip animation math & SVG transforms when no new
    // clock pulse has arrived (or when stopped), further ensuring that GUI
    // work never competes with MIDI clock timing. All animation is strictly
    // on the message thread and never touches audio thread resources.
    unsigned long long dancerLastDrawnClockCounter { std::numeric_limits<unsigned long long>::max() };

    // Trigger fade animator (JUCE animation module). Replaces manual exponential decay.
    juce::Animator triggerFadeAnimator { juce::ValueAnimatorBuilder{}
        .withDurationMs(240.0)
        .withValueChangedCallback([this](float progress){
            // progress 0..1 => fade from 1 -> 0
            triggerFade = 1.0f - juce::jlimit(0.0f, 1.0f, progress);
            repaint(triggerRect);
        })
        .build() };
};
