#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_animation/juce_animation.h>
#include "PluginProcessor.h"
#include "RingToggle.h"
#include "UiTheme.h"
#include "UiComponents.h"
#include "Pattern.h"

#include "PlaygroundComponent.h"
#include "HitRouting.h" // unified hit-test
#include "SvgDancerComponent.h"

class ClockEditorPaintLayer;
#if ! CLOCKV3_DEMO
namespace toolboy_license { class LicenseDialog; }
#endif

class ColorPaletteToggle : public juce::Component
{
    class PopupCalloutLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        explicit PopupCalloutLookAndFeel(UiThemeColours& t) : theme(t) {}

        void setScaleFactor(float newScale) { scaleFactor = juce::jmax(0.5f, newScale); }

        void drawCallOutBoxBackground(juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&) override
        {
            g.setColour(theme.base().withAlpha(0.97f));
            g.fillPath(path);
        }

        int getCallOutBoxBorderSize(const juce::CallOutBox&) override { return (int) std::round(8.0f * scaleFactor); }
        float getCallOutBoxCornerSize(const juce::CallOutBox&) override { return 3.0f * scaleFactor; }

    private:
        UiThemeColours& theme;
        float scaleFactor { 1.0f };
    };

    UiThemeColours& theme;
    juce::Component::SafePointer<juce::CallOutBox> colorPickerBox;
    PopupCalloutLookAndFeel popupLookAndFeel;

public:
    static constexpr int kDotSize = 15;
    static constexpr int kDotGap = 4;
    static constexpr int kActiveDotExpand = 2;

    std::function<void()> onColorChanged;

    ColorPaletteToggle(UiThemeColours& t) : theme(t), popupLookAndFeel(t)
    {
        // No button setup needed
    }

    void setPopupScale(float newScale)
    {
        popupScale = juce::jmax(0.5f, newScale);
        popupLookAndFeel.setScaleFactor(popupScale);
    }

    ~ColorPaletteToggle() override
    {
        if (colorPickerBox)
            colorPickerBox->dismiss();
    }
    
    void paint(juce::Graphics& g) override
    {
        if (activeDotIndex >= 0 && colorPickerBox == nullptr)
            activeDotIndex = -1;

        const auto accentBounds = getDotPaintBounds(0).toFloat();
        const auto cyanBounds = getDotPaintBounds(1).toFloat();
        const auto baseBounds = getDotPaintBounds(2).toFloat();

        g.setColour(theme.accent());
        g.fillEllipse(accentBounds);

        g.setColour(theme.cyan());
        g.fillEllipse(cyanBounds);

        g.setColour(theme.base());
        g.fillEllipse(baseBounds);
    }

    juce::Rectangle<int> getDotBounds(int dotIndex) const
    {
        const int step = kDotSize + kDotGap;
        const int totalH = 3 * kDotSize + 2 * kDotGap;
        const int leftX = (getWidth() - kDotSize - step) / 2;
        const int rightX = leftX + step;
        const int startY = (getHeight() - totalH) / 2;

        switch (dotIndex)
        {
            case 0: return { leftX, startY + step, kDotSize, kDotSize }; // accent
            case 1: return { rightX, startY, kDotSize, kDotSize }; // cyan
            case 2: return { leftX, startY, kDotSize, kDotSize }; // base in old accent slot
            default: break;
        }

        return { leftX, startY, kDotSize, kDotSize };
    }

    juce::Rectangle<int> getDotPaintBounds(int dotIndex) const
    {
        auto bounds = getDotBounds(dotIndex);

        if (dotIndex == activeDotIndex)
            bounds = bounds.expanded(kActiveDotExpand, kActiveDotExpand);

        return bounds;
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        for (int dotIndex = 0; dotIndex < 3; ++dotIndex)
        {
            if (! getDotBounds(dotIndex).contains(e.getPosition()))
                continue;

            juce::Component::SafePointer<ColorPaletteToggle> safeThis (this);

            if (dotIndex == 0)
            {
                if (e.mods.isShiftDown())
                {
                    theme.setAccent(juce::Colour::fromRGB(0xFF, 0x4E, 0x5B));
                    if (onColorChanged) onColorChanged();
                    repaint();
                }
                else
                {
                    showColourPicker(theme.accent(), [safeThis](juce::Colour c){ if (safeThis) safeThis->theme.setAccent(c); }, 0);
                }
            }
            else if (dotIndex == 1)
            {
                if (e.mods.isShiftDown())
                {
                    theme.setCyan(juce::Colour::fromRGB(0x00, 0xD7, 0xFF));
                    if (onColorChanged) onColorChanged();
                    repaint();
                }
                else
                {
                    showColourPicker(theme.cyan(), [safeThis](juce::Colour c){ if (safeThis) safeThis->theme.setCyan(c); }, 1);
                }
            }
            else if (dotIndex == 2)
            {
                if (e.mods.isShiftDown())
                {
                    theme.setBase(juce::Colour::fromRGB(0x26, 0x26, 0x26));
                    if (onColorChanged) onColorChanged();
                    repaint();
                }
                else
                {
                    showColourPicker(theme.base(), [safeThis](juce::Colour c){ if (safeThis) safeThis->theme.setBase(c); }, 2);
                }
            }

            return;
        }
    }

private:
    juce::Rectangle<int> makePopupTargetArea(int dotIndex) const
    {
        return localAreaToGlobal(getDotBounds(dotIndex).expanded((int) std::round(3.0f * popupScale),
                                                                 (int) std::round(2.0f * popupScale)));
    }

    void positionPopupBox(juce::CallOutBox& box, const juce::Rectangle<int>& targetArea, int dotIndex) const
    {
        auto popupBounds = box.getBounds();
        const int popupGap = (int) std::round(-4.0f * popupScale);
        const int screenGap = juce::jmax(2, (int) std::round(2.0f * popupScale));
        const bool openToRight = getDotBounds(dotIndex).getCentreX() >= (getWidth() / 2);

        if (openToRight)
            popupBounds.setX(targetArea.getRight() + popupGap);
        else
            popupBounds.setX(targetArea.getX() - popupBounds.getWidth() - popupGap);

        popupBounds.setY(targetArea.getCentreY() - popupBounds.getHeight() / 2);

        if (auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(targetArea))
            popupBounds = popupBounds.constrainedWithin(display->userBounds.toNearestInt().reduced(screenGap));

        box.setBounds(popupBounds);
    }

    void showColourPicker(juce::Colour currentColour, std::function<void(juce::Colour)> setter, int dotIndex)
    {
        if (colorPickerBox)
            colorPickerBox->dismiss();

        activeDotIndex = dotIndex;
        repaint();

        juce::Component::SafePointer<ColorPaletteToggle> safeThis (this);
        auto* content = new SimpleColorPicker(theme, popupScale);
        content->setCurrentColour(currentColour);
        content->onColorChanged = [setter, safeThis](juce::Colour c) {
            if (setter) setter(c);
            if (safeThis != nullptr)
            {
                if (safeThis->colorPickerBox != nullptr)
                {
                    if (auto* picker = safeThis->colorPickerBox->getChildComponent(0))
                        picker->repaint();
                    safeThis->colorPickerBox->repaint();
                }

                if(safeThis->onColorChanged) safeThis->onColorChanged(); 
                safeThis->repaint(); 
            }
        };
        
        auto targetArea = makePopupTargetArea(dotIndex);

        auto& box = juce::CallOutBox::launchAsynchronously(std::unique_ptr<juce::Component>(content), targetArea, nullptr);
        box.setLookAndFeel(&popupLookAndFeel);
        box.setArrowSize(9.0f * popupScale);
        positionPopupBox(box, targetArea, dotIndex);
        colorPickerBox = &box;
    }

    int activeDotIndex { -1 };
    float popupScale { 1.0f };
};

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
    friend class ClockEditorPaintLayer;

    // Base design size for scalable UI (all layout authored for this size)
    static constexpr int kBaseW = 300;
    static constexpr int kBaseH = 240;
    static constexpr int kHeaderBaseH = 30;
    static constexpr int kCompactExtraH = 25; // matches legacy submenu extra height when compact

    bool canvasExpanded { true };

    // Root container that is uniformly scaled to fit the host-provided editor size.
    juce::Component uiRoot;
    // Custom bottom-right resize triangle (direct child of editor, not uiRoot)
    std::unique_ptr<juce::Component> cornerResizer;
    double uiScale { 1.0 };
    juce::Point<int> uiOffset { 0, 0 };

    // Painter layers (children of uiRoot). These keep custom painting scalable.
    std::unique_ptr<juce::Component> backdropLayer;
    std::unique_ptr<juce::Component> overlayBgLayer; // submenu background
    std::unique_ptr<juce::Component> headerBgLayer;  // header background (covers submenu when sliding under)
    std::unique_ptr<juce::Component> overlayFgLayer;

    // Ring area in base coords (for child bounds) and in editor coords (for hit tests / repaint).
    juce::Rectangle<int> ringAreaBase;

    void paintBackdropLayer(juce::Graphics&);
    void paintOverlayBackgroundLayer(juce::Graphics&); // submenu bg
    void paintHeaderBackgroundLayer(juce::Graphics&);
    void paintOverlayForegroundLayer(juce::Graphics&);

    // Header switch toggle (custom accent toggle in header)
    std::unique_ptr<HeaderSwitchToggle> headerSwitchToggle;

    // Pulse width slider (1–20 ms)
    std::unique_ptr<juce::Slider> pulseWidthSlider;
    std::unique_ptr<juce::Label> pulseWidthLabel;
    std::unique_ptr<juce::Label> pulseWidthValueLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pulseWidthAttachment;
    std::unique_ptr<TriggerOffsetNumSlider> triggerOffsetSlider;
    int triggerOffsetSamplesCached { 0 };
    ClockSyncAudioProcessor& processor;

    // Timer
    void timerCallback() override;

    // UI components
    FullWidthComboBox deviceBox;
    juce::TextButton refreshButton { "RESET" };
    juce::TextButton setupButton { "SETUP" };
    // Setup-mode toggles
    juce::TextButton idleModeButton { "IDLE OFF" };
    juce::TextButton legacyModernButton { "LEGACY" };
    juce::TextButton sppButton { "S.P.P. OFF" };
    // New: instrument name combo + NAME/MIDI toggle
    NameComboBox nameBox;
    juce::TextButton nameMidiSwitch { "NAME" };
    // Sync Latch toggle
    juce::TextButton syncLatchButton { "SYNC" };

    // Name editor helper
    void toggleNameEditorOrCommit();
    void showInlineNameEditor(const juce::String& initialText, int editingId);
    void updateInlineNameEditorBounds();
    // Small center-dot click toggle
    SmallDotToggle clickButton;
    // Pattern start fine-tune UI removed
    // Idle clock toggle
    RingToggle idleClockToggle;
    // New: shuffle scale toggle at (65,195) size 40x40

    // Legacy popup ring components removed – PlaygroundComponent owns all popup menu UI now.
    // (If a non-playground fallback mode is reintroduced, restore PopupMenuRing members.)
    // Shared playground component (migrated UI)
    std::unique_ptr<PlaygroundComponent> playgroundComp;
    std::unique_ptr<ArrowDownComponent> arrowDown;

    // UI timing
    double lastUiUpdateMs { 0.0 };

    // Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> clickEnableAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> idleClockAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncLatchAttachment;

    // Helpers
    void refreshDeviceList();
    void loadInstrumentNamesFromState();
    void saveInstrumentNamesToState();
    void populateNameBox();
    void showNewNameDialog();
    std::vector<juce::MidiDeviceInfo> midiOutputs;
    // Cached MIDI input devices for the MIDI Remote overlay (gear / "system settings").
    // Populated by refreshDeviceList(); the overlay reuses this cache to avoid
    // CoreMIDI enumeration glitches while playing.
    std::vector<juce::MidiDeviceInfo> remoteMidiInputs;
    // Persisted instrument names
    juce::StringArray instrumentNames;
    // Inline entry editor
    std::unique_ptr<juce::TextEditor> nameEntryEditor;
    int nameEntryEditingId { 0 };

    // SVG help toggle button
    HelpButton helpToggle;
    std::unique_ptr<juce::Drawable> helpDrawable;
    std::unique_ptr<juce::Drawable> helpOffDrawable;
    std::unique_ptr<juce::Drawable> helpOnDrawable;
    void updateHelpButtonImages();
    ColorPaletteToggle colorPaletteToggle;
    juce::TextButton demoExpiredButton { "NOOO...DON'T STOP!!" };
    juce::TextButton demoLabelButton { "DEMO" }; // clickable DEMO label (bottom-right, opens Gumroad)
    // Bottom-right setup corner button (SVG icon)
    std::unique_ptr<juce::DrawableButton> setupCornerButton;
    std::unique_ptr<juce::Drawable> setupCornerDrawable; // keep SVG drawable alive
    // Tinted variants for OFF/ON states (keep alive for DrawableButton images)
    std::unique_ptr<juce::Drawable> setupCornerOffDrawable;
    std::unique_ptr<juce::Drawable> setupCornerOnDrawable;
    bool setupSvgLoaded { false }; // diagnostic: true if assets/setup.svg loaded

    void updateSetupButtonImages();


    // Editor-level hover text (overrides playground text when hovering editor controls)
    juce::String editorHoverText;

    // Canvas overlay toggled by setup.svg (independent of header submenu)
    bool setupOverlayVisible { false };
    std::unique_ptr<juce::Component> setupOverlayComp; // consumes mouse inside overlay bounds
#if ! CLOCKV3_DEMO
    std::unique_ptr<toolboy_license::LicenseDialog> licenseDialog;
#endif
    // std::unique_ptr<juce::DrawableButton> setupOverlayClose; // Removed as per request

    // Handle Escape to close overlay when visible
    bool keyPressed(const juce::KeyPress& key) override;

public:
    // Midi Remote Settings are now stored in the Processor (std::atomic<int>)
    // int midiRemoteStartStop { 0 }; // Note number
    // int midiRemoteOffset { 0 };    // CC
    // int midiRemoteShuffle { 0 };   // CC
    // int midiRemoteClockDiv { 0 };  // CC
    // int midiRemoteTrigger { 0 };   // Note number
    // int midiRemoteResync { 0 };    // CC
private:
    

    // LED animation
    unsigned long long lastSeenClockCounter { 0 };
    float ledLevel { 0.0f }; // 0..1 (driven by ledAnimator)
    // LED pulse target
    float ledPulseTarget { 1.0f };
    bool runParamCached { true };
    int rateIndexCached { 1 };
    bool pendingStartCached { false };
    bool engineRunningCached { true };

    // Theme look-and-feel (ClickRotaryLNF unused; helpButtonLNF removed) – keep only ThemeLNF.
    std::unique_ptr<ThemeLNF> themeLNF;
    std::unique_ptr<juce::LookAndFeel> syncLatchLNF;
    // Status bar (LED + status string abstraction)
    std::unique_ptr<StatusBarComponent> statusBar;

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
    float linearShuffleAmountCached { -1.0f };
    bool linearShuffleModeCached { false };
    int autoFillIndexCached { -1 };

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
    // Centralized visibility helper for header components
    void updateHeaderVisibility();

    
    

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

    // --- Dancer SVG Component ---
    std::unique_ptr<SvgDancerComponent> svgDancer;
    int dancerLastFrame { 0 };               // last computed frame index
    unsigned long long dancerLastDrawnClockCounter { 0 }; // last clock counter used for frame selection
    int dancerFrameOffset { 0 };              // frame offset in frames; increments by 3 on triggers
    static constexpr int kDancerRefW = 355;  // reference design width

    // --- Toolboy Clock Logo (shown during submenu) ---
    std::unique_ptr<juce::Drawable> logoDrawable;
    float logoAlpha { 0.0f }; // 0 = hidden, 1 = fully visible
    static constexpr int kDancerRefH = 500;  // reference design height
    // Option: divisor for clock pulses per frame advance (1 => every pulse; 24 => quarter-note cycle)
    int dancerPulseCyclePPQ { 24 };          // use 24 PPQ quarter-note cycle mapping
    // Pattern ring (inner 16-step sequencer)
    PatternRing pattern;
    int patternHoverIndex { -1 };
    int patternParamCached { 0 }; // cached APVTS patternSteps value to keep UI in sync
    bool patternEditMode { false }; // idx2 ON enables editing & visibility
    bool patternDragActive { false }; // true while mouse dragging over pattern
    bool patternDragSetState { false }; // desired state (on/off) applied to dragged wedges
    bool patternDragTouched[16] { false,false,false,false,false,false,false,false,false,false,false,false,false,false,false,false }; // prevent re-applying
    void pushPatternStateToProcessor();
    void updatePatternParamFromPopup3(int popupIndex);
    void mouseDrag(const juce::MouseEvent& e) override; // implement pattern drag
    void toggleTriggerOffsetSlider();
    void exitPatternEditMode();

    // Build HitContext and derive InteractionMode for routing.
    HitContext buildHitContext() const;
    InteractionMode getInteractionMode() const;
    HitResult routeHit(const juce::MouseEvent& e, bool isHover);

    // Cached drop shadow images to avoid continuous CPU Gaussian blurs during animation
    juce::Image headerShadowImage;
    juce::Image submenuShadowImage;
    void initDropShadows();
};
