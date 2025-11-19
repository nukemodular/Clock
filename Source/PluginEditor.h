#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_animation/juce_animation.h>
#include "PluginProcessor.h"
#include "RingToggle.h"
#include "LookAndFeels.h" // theme colours
#include "UiLayoutConstants.h"

#include "PlaygroundComponent.h"

// Forward-declare popup ring
class PopupMenuRing;

class ClockSyncAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ClockSyncAudioProcessorEditor(ClockSyncAudioProcessor&);
    ~ClockSyncAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    // Draw elements that must appear above child components
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;

private:
    ClockSyncAudioProcessor& processor;

    // Timer
    void timerCallback() override;

    // UI components
    class FullWidthComboBox : public juce::ComboBox
    {
    public:
        void paint(juce::Graphics& g) override
        {
            // Draw combo background only
            getLookAndFeel().drawComboBox(g, getWidth(), getHeight(), false, 0, 0, 0, 0, *this);

            // Show placeholder when no selection
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
            // Base resized
            juce::ComboBox::resized();

            // Stretch child label/editor to full width
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
    juce::TextButton setupButton { "SETUP" };
    // Setup-mode toggles
    juce::TextButton idleModeButton { "IDLE OFF" };
    juce::TextButton legacyModernButton { "LEGACY" };
    juce::TextButton sppButton { "S.P.P. OFF" };
    // New: instrument name combo + NAME/MIDI toggle
    FullWidthComboBox nameBox;
    juce::TextButton nameMidiSwitch { "NAME" };
    // Name editor helper
    void toggleNameEditorOrCommit();
    // Small center-dot click toggle
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
        void mouseDown(const juce::MouseEvent& /*e*/) override
        {
            setToggleState(! getToggleState(), juce::sendNotification);
        }
        void mouseUp(const juce::MouseEvent& /*e*/) override
        {
            // suppress default mouseUp behaviour; handled on mouseDown
        }
    private:
        juce::Colour off { juce::Colours::red };
        juce::Colour on  { juce::Colours::cyan };
    } clickButton;
    // Idle clock toggle
    RingToggle idleClockToggle;
    // New: shuffle scale toggle at (65,195) size 40x40

    // Popup ring(s)
    std::unique_ptr<PopupMenuRing> popupRing0;
    std::unique_ptr<PopupMenuRing> popupRing1;
    // Shared playground component (migrated UI)
    std::unique_ptr<PlaygroundComponent> playgroundComp;

    // UI timing
    double lastUiUpdateMs { 0.0 };

    // Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> clickEnableAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> idleClockAttachment;

    // Helpers
    void refreshDeviceList();
    void loadInstrumentNamesFromState();
    void saveInstrumentNamesToState();
    void populateNameBox();
    void showNewNameDialog();
    std::vector<juce::MidiDeviceInfo> midiOutputs;
    // Persisted instrument names
    juce::StringArray instrumentNames;
    // Inline entry editor
    std::unique_ptr<juce::TextEditor> nameEntryEditor;

    // Help toggle
    juce::TextButton helpToggle { "?" };
    

    // LED animation
    unsigned long long lastSeenClockCounter { 0 };
    float ledLevel { 0.0f }; // 0..1 (driven by ledAnimator)
    // LED pulse target
    float ledPulseTarget { 1.0f };
    bool runParamCached { true };
    int rateIndexCached { 1 };
    bool pendingStartCached { false };
    bool engineRunningCached { true };

    // Custom rotary & button LookAndFeels (now using global definitions from LookAndFeels.h)
    std::unique_ptr<ClickRotaryLNF> clickRotaryLNF;
    std::unique_ptr<ThemeLNF> themeLNF;
    // Small LookAndFeel for the help '?' button to avoid drawing any border
    std::unique_ptr<juce::LookAndFeel_V4> helpButtonLNF;

    // Layout cache for ring visualisation
    juce::Rectangle<int> ringArea;
    // Step number fade shown inside triggerRect, updates each 16th
    int stepNumberCached { 0 };
    // Visual step (chaselight)
    int visualStepCached { 1 };
    // Relative step 1..16 from last restart
    int relativeStepCached { 1 };
    // Selected resync step 1..16
    int selectedResyncStepCached { 1 };
    // Cycle start absolute step
    int cycleStartStepAbsolute { -1 }; // -1 => not initialised yet
    // Cached NEXT indicator
    bool nextRestartPendingCached { false };
    // Shuffle value
    int shuffleValue { 4 }; // default middle

    // Cached rate parameter pointer
    juce::AudioParameterChoice* rateParam { nullptr };

    // Drawing helpers
    
    // Legacy ring/dancer helpers removed (PlaygroundComponent owns visuals).
   

    // Ring constants
    static constexpr int kRingOuterD = 160;
    static constexpr int kRingInnerD = 120;

    // Backdrop pulse animation
    std::array<float, 8> backdropPulseProgress {{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }}; // 1.0 => full pulse
    std::array<float, 8> backdropPulseScale = UiLayout::kBackdropPulseScales; // scale increments per circle
    int lastBackdropBeatIndex { -1 };
    int lastLedBeatIndex { -1 };
    std::array<std::unique_ptr<juce::Animator>, 8> backdropAnimators;
    // Scheduled start times (ms since epoch high-res) for staggered backdrop pulses.
    // When a quarter-note beat occurs we populate this with now + i * kPerBackdropDelayMs
    // and the timerCallback will start each animator when its scheduled time arrives.
    std::array<double, 8> backdropScheduledStartMs {{ 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 }};

    // Setup submenu animation state
    float setupSubmenuProgress { 0.0f }; // 0 hidden (-25), 1 shown (+25)
    bool setupSubmenuTargetOn { false }; // current target (true = shown)
    bool setupSubmenuAnimatingHide { false }; // true when playing hide animation (invert progress)
    int hoveredSetupIndex { -1 }; // 0 idle,1 legacy,2 spp
    std::unique_ptr<juce::Animator> setupAnimator; // drives setupSubmenuProgress

    // Chase light segment fade trail: each raw wedge (0..15) holds a fade value decaying toward 0.
    // Legacy editor-side segment fade trail removed (Ring16Component now owns fade state).

    // Manual trigger offset preview state
    bool manualTriggerOffsetActive { false };      // true after manual trigger until restart applied
    int  manualTriggerRelativeStepAtTrigger { 1 }; // relativeStepCached snapshot at trigger time
    int  manualTriggerPlayheadDelta { 0 };         // (selectedResyncStepCached - snapshot) modulo 16

    // Layout helper for animated submenu
    void updateSetupSubmenuLayout();

    
    

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClockSyncAudioProcessorEditor)

    // Legacy dancer animation members removed.

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
    // VBlank-driven animator updater: used to drive visual animators at the
    // display refresh rate while leaving the 60Hz timer for non-animation tasks.
    std::unique_ptr<juce::VBlankAnimatorUpdater> vblankUpdater;
};
