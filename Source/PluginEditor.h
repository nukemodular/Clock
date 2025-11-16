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
    // ComboBox that paints its text centered across the full control width
    class FullWidthComboBox : public juce::ComboBox
    {
    public:
        void paint(juce::Graphics& g) override
        {
            // Draw background using the active LookAndFeel only. Do not draw text here
            // — the ComboBox may contain an internal label/editor which we'll stretch
            // to occupy the full width in `resized()` to prevent arrow-area shifting.
            getLookAndFeel().drawComboBox(g, getWidth(), getHeight(), false, 0, 0, 0, 0, *this);

            // If nothing is selected, show the placeholder text (e.g. "new...")
            // because the LookAndFeel's combo drawing path no longer renders text.
            if (getSelectedId() == 0)
            {
                juce::String placeholder = getTextWhenNothingSelected();
                if (placeholder.isNotEmpty())
                {
                    g.setColour(findColour(juce::ComboBox::textColourId));
                    g.setFont(juce::Font(juce::FontOptions("Arial", 13.0f, juce::Font::bold)));
                    g.drawFittedText(placeholder, getLocalBounds().reduced(6, 0), juce::Justification::centred, 1);
                }
            }
        }

        void resized() override
        {
            // Let base class perform any placement it needs
            juce::ComboBox::resized();

            // Stretch any internal label or text editor child to the full width so
            // the displayed text is truly centred across the whole control and not
            // constrained by a reserved arrow area.
            for (auto* c : getChildren())
            {
                if (auto* te = dynamic_cast<juce::TextEditor*>(c))
                {
                    te->setBounds(getLocalBounds().reduced(6, 0));
                }
                else if (auto* lb = dynamic_cast<juce::Label*>(c))
                {
                    lb->setBounds(getLocalBounds().reduced(6, 0));
                    lb->setJustificationType(juce::Justification::centred);
                }
            }
        }
    } deviceBox;
    juce::TextButton refreshButton { "RESET" };
    // New: instrument name combo + NAME/MIDI toggle
    FullWidthComboBox nameBox;
    juce::TextButton nameMidiSwitch { "NAME" };
    // Name editor helper
    void toggleNameEditorOrCommit();
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
    void loadInstrumentNamesFromState();
    void saveInstrumentNamesToState();
    void populateNameBox();
    void showNewNameDialog();
    std::vector<juce::MidiDeviceInfo> midiOutputs;
    // Persisted instrument name list (stored in APVTS state as newline-separated string)
    juce::StringArray instrumentNames;
    // Inline entry widget for adding/editing a new instrument name
    std::unique_ptr<juce::TextEditor> nameEntryEditor;

    // Simple LED animation based on clock ticks
    unsigned long long lastSeenClockCounter { 0 };
    float ledLevel { 0.0f }; // 0..1 (driven by ledAnimator)
    // Animator-driven LED pulse target (set before starting the animator)
    float ledPulseTarget { 1.0f };
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
    // Visual step used for the outer ring wedge animation. This is gated by
    // the `idleClockToggle` so the wedge can freeze while the internal clock
    // and dancer animations continue.
    int visualStepCached { 1 };
    // Selected arc size (1..7) used as shuffleValue
    int shuffleValue { 4 }; // default middle

    // Cached rate parameter pointer to avoid repeated dynamic_cast in timer
    juce::AudioParameterChoice* rateParam { nullptr };

    // Drawing helpers
    
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

    // Backdrop pulse animation: six circles pulse in sequence then two-beat pause.
    std::array<float, 6> backdropPulseProgress {{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }}; // 1.0 => full pulse
    std::array<float, 6> backdropPulseScale {{ 0.02f, 0.03f, 0.04f, 0.05f, 0.06f, 0.07f }}; // scale increments per circle
    int lastBackdropBeatIndex { -1 };
    int lastLedBeatIndex { -1 };

    
    

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

    // LED pulse animator: on each 16th step we start this animator to produce
    // a brief pulse. The animator supplies a 0..1 progress; we multiply the
    // configured `ledPulseTarget` by (1-progress) to create a falling pulse.
    juce::Animator ledAnimator { juce::ValueAnimatorBuilder{}
        .withDurationMs(140.0)
        .withValueChangedCallback([this](float progress){
            const float p = juce::jlimit(0.0f, 1.0f, progress);
            ledLevel = ledPulseTarget * (1.0f - p);
            // repaint header area where LED is drawn (top 28px)
            repaint(0, 0, getWidth(), 28);
        })
        .build() };
};
