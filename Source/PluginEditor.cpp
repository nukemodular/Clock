// -------------------------------------------------------------------------------------------------
// Restored, de-corrupted PluginEditor.cpp
// -------------------------------------------------------------------------------------------------

#include <utility>
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "build_info.h"
#include "BinaryData.h"
#include "UiTheme.h"
#include "UiComponents.h"
#include "HitRouting.h"
#include <array>
#include <optional>


namespace
{
    // Accent background variant
    const juce::Colour kBaseLo  = UiThemeColours::base().darker(0.12f);
}



ClockSyncAudioProcessorEditor::ClockSyncAudioProcessorEditor(ClockSyncAudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processor(p)
{
    setResizable(false, false);
    setSize(300, 240);
    startTimerHz(60);

    themeLNF = std::make_unique<ThemeLNF>();

    // Assign theme LNF
    nameBox.setLookAndFeel(themeLNF.get());
    nameMidiSwitch.setLookAndFeel(themeLNF.get());
    setupButton.setLookAndFeel(themeLNF.get());
    idleModeButton.setLookAndFeel(themeLNF.get());
    legacyModernButton.setLookAndFeel(themeLNF.get());
    sppButton.setLookAndFeel(themeLNF.get());
    refreshButton.setLookAndFeel(themeLNF.get());
    // Enable toggle state
    nameMidiSwitch.setClickingTogglesState(true);
    setupButton.setClickingTogglesState(true);
    idleModeButton.setClickingTogglesState(true);
    legacyModernButton.setClickingTogglesState(true);
    sppButton.setClickingTogglesState(true);

    // APVTS alias
    auto& apvts = processor.getAPVTS();
    if (auto* rp = apvts.getRawParameterValue(ClockSyncAudioProcessor::paramRun))
        runParamCached = rp->load() > 0.5f;
    rateParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(ClockSyncAudioProcessor::paramClockRateIndex));
    if (rateParam) rateIndexCached = rateParam->getIndex();

    // Load persisted UI-only flags from APVTS state (if present). These are
    // non-parameter values we store in the same ValueTree so the editor
    // appearance (pattern-edit, name/midi mode, selected instrument, submenu)
    // is restored when a project is reloaded.
    {
        auto& st = apvts.state;
        // NAME/MIDI toggle (default: current widget state)
        if (st.hasProperty("ui.showNameMode"))
            nameMidiSwitch.setToggleState((bool) st.getProperty("ui.showNameMode"), juce::dontSendNotification);
        // Pattern edit mode (default: false)
        if (st.hasProperty("ui.patternEditMode"))
        {
            patternEditMode = (bool) st.getProperty("ui.patternEditMode");
            if (playgroundComp) playgroundComp->setPatternEditButtonState(patternEditMode);
            if (playgroundComp) playgroundComp->setInterceptsMouseClicks(!patternEditMode, !patternEditMode);
        }
        // Setup submenu open state (default: false)
        if (st.hasProperty("ui.setupSubmenuOn"))
        {
            setupSubmenuTargetOn = (bool) st.getProperty("ui.setupSubmenuOn");
            setupButton.setToggleState(setupSubmenuTargetOn, juce::dontSendNotification);
            setupSubmenuProgress = setupSubmenuTargetOn ? 1.0f : 0.0f;
            setupSubmenuAnimatingHide = ! setupSubmenuTargetOn;
        }
        // Load instrument names early so populateNameBox can restore selection
        loadInstrumentNamesFromState();
    }



    // Create playground component (owns popups & ring visuals)
    playgroundComp = std::make_unique<PlaygroundComponent>();
    addAndMakeVisible(*playgroundComp);
    playgroundComp->setBounds(0, 0, getWidth(), getHeight());
    playgroundComp->setHeaderHeight(30);
    playgroundComp->toBack();

    // Ensure playground reflects any persisted pattern-edit state loaded earlier
    if (playgroundComp)
    {
        playgroundComp->setPatternEditButtonState(patternEditMode);
        playgroundComp->setInterceptsMouseClicks(!patternEditMode, !patternEditMode);
    }

    // Help toggle ("?") - restore visibility and behaviour so users can enable/disable tooltips.
    // Wire directly to the playground's hover text API rather than a separate applyTooltips helper.
    addAndMakeVisible(helpToggle);
    helpToggle.setClickingTogglesState(true);
    helpToggle.setColour(juce::TextButton::textColourOffId, UiThemeColours::cyan().darker(0.45f));
    helpToggle.setColour(juce::TextButton::textColourOnId, UiThemeColours::cyan());
    helpToggle.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    helpToggle.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    // Initialize toggle from playground state if available
    if (playgroundComp)
        helpToggle.setToggleState(playgroundComp->getHoverTextEnabled(), juce::dontSendNotification);
    helpToggle.onClick = [this]() {
        const bool on = helpToggle.getToggleState();
        if (playgroundComp) playgroundComp->setHoverTextEnabled(on);
        repaint(0, getHeight()-40, 120, 40);
    };

    // Restore selected instrument index if present in state
    {
        auto& st = processor.getAPVTS().state;
        int sel = (int) st.getProperty("ui.selectedInstrument", 0);
        if (sel > 0 && sel <= (int) instrumentNames.size())
            nameBox.setSelectedId(sel, juce::dontSendNotification);
    }

    // Pattern start fine-tune slider removed

    // Status bar (LED + status string) – painted above playground
    statusBar = std::make_unique<StatusBarComponent>();
    addAndMakeVisible(*statusBar);
    // Ensure statusBar has an initial geometry so it is visible immediately
    // (resized() will update this later). This prevents the header background
    // from covering the status area before the first explicit resized()/repaint().
    {
        const int headerH = 30;
        const int w = 50;
        statusBar->setBounds(getWidth() - w - 5, 0, w, headerH);
        // Initialize visible content without changing colour/alpha.
        juce::String st = idleModeButton.getToggleState() ? "IDLE" : "STOP";
        statusBar->setStatusText(st);
        statusBar->setLedLevel(ledLevel);
    }

    // Header widgets on top
   
    addAndMakeVisible(refreshButton);
    addAndMakeVisible(setupButton);
    addAndMakeVisible(idleModeButton);
    addAndMakeVisible(legacyModernButton);
    addAndMakeVisible(sppButton);
    addAndMakeVisible(deviceBox);
    addAndMakeVisible(nameBox);
    addAndMakeVisible(nameMidiSwitch);
    // Header switch toggle (created once)
    headerSwitchToggle = std::make_unique<HeaderSwitchToggle>();
    addAndMakeVisible(*headerSwitchToggle);
    headerSwitchToggle->setSize(12, 20);
    headerSwitchToggle->setVisible(true);
    // Force absolute placement to match design: x=286,y=5,w=10,h=20
    headerSwitchToggle->setBounds(286, 5, 10, 20);
    headerSwitchToggle->setInterceptsMouseClicks(true, true);
    headerSwitchToggle->toFront(true);
    headerSwitchToggle->setAlwaysOnTop(true);
    // Toggle behaviour: adjust editor canvas height and arrow visibility
    headerSwitchToggle->onToggle = [this](bool on){
        if (arrowDown) arrowDown->setVisible(on && (setupSubmenuTargetOn || setupSubmenuProgress > 0.0f));
        int baseH = on ? 240 : 30;
        int extra = (!on && (setupSubmenuTargetOn || setupSubmenuProgress > 0.0f)) ? (int) std::round(setupSubmenuProgress * 25.0f) : 0;
        setSize(getWidth(), baseH + extra);
    };
    nameMidiSwitch.setVisible(true);
    refreshButton.setVisible(true);
    setupButton.setVisible(true);
    // Ensure pulseWidthValueLabel is always constructed before use
    if (!pulseWidthValueLabel)
    {
        pulseWidthValueLabel = std::make_unique<juce::Label>();
        pulseWidthValueLabel->setFont(juce::Font(juce::FontOptions("Arial", 12.0f, juce::Font::bold)));
        pulseWidthValueLabel->setColour(juce::Label::textColourId, UiThemeColours::cyan());
        pulseWidthValueLabel->setJustificationType(juce::Justification::centredLeft);
    }
    addAndMakeVisible(*pulseWidthValueLabel);

    // Ensure pulseWidthSlider is always constructed before use
    if (!pulseWidthSlider)
    {
        pulseWidthSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
        pulseWidthSlider->setRange(1, 20, 1);
        pulseWidthSlider->setTextValueSuffix(" ms");
        pulseWidthSlider->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        pulseWidthSlider->setColour(juce::Slider::textBoxTextColourId, UiThemeColours::cyan());
        pulseWidthSlider->setColour(juce::Slider::thumbColourId, UiThemeColours::cyan());
        pulseWidthSlider->setColour(juce::Slider::trackColourId, UiThemeColours::accent());
        pulseWidthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.getAPVTS(), "pulseWidthMs", *pulseWidthSlider);
        addAndMakeVisible(*pulseWidthSlider);
        pulseWidthSlider->setVisible(false);
    }

    // Ensure arrowDown is always constructed before use
    if (!arrowDown)
    {
        arrowDown = std::make_unique<ArrowDownComponent>();
        addAndMakeVisible(*arrowDown);
        arrowDown->setVisible(false);
    }
    refreshButton.toFront(true);
    deviceBox.toFront(true);
    nameBox.toFront(true);
    nameMidiSwitch.toFront(true);
    statusBar->toFront(true);

    // Initialise pattern ring visual state from persisted parameter
        if (auto* psi = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter(ClockSyncAudioProcessor::paramPatternSteps)))
        {
            int v = psi->get();
            patternParamCached = v;
            // Always update UI after parameter load
            if (playgroundComp) playgroundComp->setPatternBitmask((uint16_t) v);
            else pattern.setBitmask((uint16_t) v);
        }
        // (Parameter listener code removed: handled elsewhere)

    // --- Wire PlaygroundComponent callbacks to APVTS ---
    if (playgroundComp)
    {
        playgroundComp->onResyncStepRequested = [this](int step){
            if (auto* pParam = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramResyncOffsetStep))
            {
                const auto& range = pParam->getNormalisableRange();
                pParam->beginChangeGesture();
                pParam->setValueNotifyingHost(range.convertTo0to1((float) juce::jlimit(1, 16, step)));
                pParam->endChangeGesture();
            }
            processor.notifyResyncOffsetChanged();
        };
        playgroundComp->onRunToggleRequested = [this](bool on){
            if (auto* pParam = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramRun))
            {
                pParam->beginChangeGesture();
                pParam->setValueNotifyingHost(on ? 1.0f : 0.0f);
                pParam->endChangeGesture();
            }
            // Schedule a resync immediately when Run is toggled ON at the currently selected OFFSET step
            if (on && playgroundComp && playgroundComp->onResyncStepRequested)
            {
                int offsetStep = 1;
                if (auto* pi = dynamic_cast<juce::AudioParameterInt*>(processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramResyncOffsetStep)))
                    offsetStep = juce::jlimit(1, 16, pi->get());
                playgroundComp->onResyncStepRequested(offsetStep);
            }
        };
        playgroundComp->onTriggerOnceRequested = [this](){
            ledPulseTarget = 1.0f; ledAnimator.start(); processor.requestTriggerOnce();
            manualTriggerOffsetActive = true;
            manualTriggerRelativeStepAtTrigger = juce::jlimit(1,16, relativeStepCached);
            manualTriggerPlayheadDelta = (selectedResyncStepCached > 0)
                ? (selectedResyncStepCached - manualTriggerRelativeStepAtTrigger + 16) % 16 : 0;
            if (playgroundComp) playgroundComp->flashSegmentLogical(manualTriggerRelativeStepAtTrigger);
            repaint(ringArea);
        };
        // Automatic pattern playback should NOT call into `processor.requestTriggerOnce()`
        // which would arm processor state. Map auto playback to a no-op/visual-only
        // callback so the editor handles preview without mutating processor flags.
        playgroundComp->onAutoTriggerRequested = [this]() {
            // Visual preview only: flash the logical segment but do not arm processor.
            int step = juce::jlimit(1,16, (int) (playgroundComp ? playgroundComp->getPatternBitmask() : 1));
            // flashSegmentLogical expects 1..16 logical step; caller already triggers visual elsewhere
            // Keep this intentionally minimal to avoid side-effects.
        };
        playgroundComp->onClockRateIndexRequested = [this](int val){
            int idx = 1; if (val == 32) idx = 0; else if (val == 16) idx = 1; else if (val == 8) idx = 2; else if (val == 4) idx = 3;
            if (auto* pParam = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramClockRateIndex))
                pParam->setValueNotifyingHost(pParam->getNormalisableRange().convertTo0to1((float) idx));
            // Persist main circle 6 / clock-rate UI choice so it is restored on reload
            processor.getAPVTS().state.setProperty("ui.mainCircle6Value", val, nullptr);
        };
        playgroundComp->onShuffleStepRequested = [this](int v){
            if (auto* pParam = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramShuffleStep))
            {
                const auto& range = pParam->getNormalisableRange();
                pParam->beginChangeGesture();
                pParam->setValueNotifyingHost(range.convertTo0to1((float) juce::jlimit(1,7,v)));
                pParam->endChangeGesture();
                // Remember last selected shuffle (visual) so we can restore it
                processor.getAPVTS().state.setProperty("ui.selectedShuffle", v, nullptr);
            }
        };
        playgroundComp->onClockWhileStoppedRequested = [this](bool on){
            if (auto* pParam = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramClockWhileStopped))
            {
                pParam->beginChangeGesture();
                pParam->setValueNotifyingHost(on ? 1.0f : 0.0f);
                pParam->endChangeGesture();
            }
        };
        playgroundComp->onClickRateRequested = [this](int v){
            if (auto* pParam = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramClickRate))
            {
                pParam->beginChangeGesture();
                pParam->setValueNotifyingHost(pParam->getNormalisableRange().convertTo0to1((float) juce::jlimit(0,4,v)));
                pParam->endChangeGesture();
            }
        };
        playgroundComp->onClickPulseRequested = [this](bool on){
            if (auto* pParam = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramClickPulse))
            {
                pParam->beginChangeGesture(); pParam->setValueNotifyingHost(on ? 1.0f : 0.0f); pParam->endChangeGesture();
            }
        };
        playgroundComp->onTriggerModeRequested = [this](bool on){
            if (auto* pParam = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramTriggerModeEnabled))
            {
                pParam->beginChangeGesture(); pParam->setValueNotifyingHost(on ? 1.0f : 0.0f); pParam->endChangeGesture();
            }
        };
        playgroundComp->onPopup3Selected = [this](int idx){ updatePatternParamFromPopup3(idx); };
        playgroundComp->onPatternEditToggled = [this](bool on){
            patternEditMode = on;
            if (playgroundComp) playgroundComp->setPatternEditButtonState(on);
            if (on)
            {
                if (auto* psi = dynamic_cast<juce::AudioParameterInt*>(processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramPatternSteps))) {
                    uint16_t m = (uint16_t) juce::jlimit(0, 65535, psi->get());
                    if (playgroundComp) playgroundComp->setPatternBitmask(m);
                    else pattern.setBitmask(m);
                }
            }
            // Persist editor-only pattern edit flag
            processor.getAPVTS().state.setProperty("ui.patternEditMode", on, nullptr);
            repaint(ringArea);
        };
        playgroundComp->onPatternChanged = [this](uint16_t m){
            patternParamCached = m;
            pushPatternStateToProcessor();
        };
        // Initialise playground visual state
        playgroundComp->setRunState(runParamCached);
        if (auto* pi = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter(ClockSyncAudioProcessor::paramResyncOffsetStep)))
        {
            const int offsetStep = juce::jlimit(1, 16, pi->get());
            playgroundComp->setResyncStepSelected(offsetStep);
            // Schedule a resync at the next bar using the selected OFFSET step
            if (playgroundComp->onResyncStepRequested)
                playgroundComp->onResyncStepRequested(offsetStep);
        }
        int displayed = 16; switch (rateIndexCached){ case 0: displayed = 32; break; case 1: displayed = 16; break; case 2: displayed = 8; break; case 3: displayed = 4; break; default: break; }
        playgroundComp->setClockRateIndexValue(displayed);
        if (auto* cp = apvts.getRawParameterValue(ClockSyncAudioProcessor::paramClickPulse))
            playgroundComp->setClickPulseState(cp->load() > 0.5f);
        if (auto* tm = apvts.getRawParameterValue(ClockSyncAudioProcessor::paramTriggerModeEnabled))
            playgroundComp->setTriggerModeState(tm->load() > 0.5f);

        // Restore popup3 (autofill interval) visual label from the parameter
        if (auto* pchoice = dynamic_cast<juce::AudioParameterChoice*>(processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramPatternBars)))
        {
            // paramPatternBars maps: 0=OFF,1=1,2=2,3=4,4=8,5=16,6=32,7=64,8=RND
            // Playground popup ordering is RND(0),64(1),32(2),16(3),8(4),4(5),2(6),1(7),OFF(8)
            int paramIdx = pchoice->getIndex();
            int popupIndex = 8 - paramIdx; // inverse of map used in updatePatternParamFromPopup3
            playgroundComp->setPopup3Index(popupIndex);
            processor.getAPVTS().state.setProperty("ui.popup3Index", popupIndex, nullptr);
        }
        // Persist linear shuffle toggle when changed in the playground
        playgroundComp->onLinearShuffleModeChanged = [this](bool on){
            processor.getAPVTS().state.setProperty("ui.linearShuffleMode", on, nullptr);
        };
        // Restore shuffle visual selection from parameter (if present)
        if (auto* sh = dynamic_cast<juce::AudioParameterInt*>(processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramShuffleStep)))
        {
            playgroundComp->setSelectedShuffle(sh->get());
            processor.getAPVTS().state.setProperty("ui.selectedShuffle", sh->get(), nullptr);
        }
        // Restore linear shuffle UI-only toggle (persisted in state)
        {
            juce::var v = processor.getAPVTS().state.getProperty("ui.linearShuffleMode", juce::var(false));
            if (v.isBool()) playgroundComp->setLinearShuffleModeState((bool) v);
        }
        // Restore main circle6 value (click / extra6) if saved
        {
            juce::var v = processor.getAPVTS().state.getProperty("ui.mainCircle6Value", juce::var());
            if (! v.isVoid() && v.isDouble()) playgroundComp->setMainCircle6Value((int) v);
        }
    }
    // --- Pulse Width Slider (1–20 ms, no label, visible in setup submenu) ---
    // Pulse Width Slider (1–20 ms, no label, visible in setup submenu)
    if (!pulseWidthSlider)
    {
        pulseWidthSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
        pulseWidthSlider->setRange(1, 20, 1);
        pulseWidthSlider->setTextValueSuffix(" ms");
        pulseWidthSlider->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        pulseWidthSlider->setColour(juce::Slider::textBoxTextColourId, UiThemeColours::cyan());
        pulseWidthSlider->setColour(juce::Slider::thumbColourId, UiThemeColours::cyan());
        pulseWidthSlider->setColour(juce::Slider::trackColourId, UiThemeColours::accent());
        pulseWidthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.getAPVTS(), "pulseWidthMs", *pulseWidthSlider);
        addAndMakeVisible(*pulseWidthSlider);
        pulseWidthSlider->setVisible(false);
    }
    // Device list
    refreshButton.onClick = [this]{ refreshDeviceList(); };
    deviceBox.setLookAndFeel(themeLNF.get());
    deviceBox.setJustificationType(juce::Justification::centred);
    deviceBox.setColour(juce::ComboBox::arrowColourId, juce::Colours::transparentBlack);
    deviceBox.setEditableText(false);
    // Ensure default ComboBox label doesn't draw; use FullWidthComboBox overlay colour instead
    deviceBox.setColour(juce::ComboBox::textColourId, juce::Colours::transparentBlack);
    deviceBox.setColour(FullWidthComboBox::overlayTextColourId, UiThemeColours::accent());
    deviceBox.setTextWhenNothingSelected("select midi-out...");

    // Move refreshButton to the right of deviceBox
    // const int deviceBoxW = 140;
    // const int refreshW = 28;
    // const int deviceBoxH = 24;
    // const int deviceBoxY = 36;
    // const int deviceBoxX = 16;
    // deviceBox.setBounds(deviceBoxX, deviceBoxY, deviceBoxW, deviceBoxH);
    // refreshButton.setBounds(deviceBoxX + deviceBoxW + 4, deviceBoxY, refreshW, deviceBoxH);

    nameBox.setJustificationType(juce::Justification::centred);
    nameBox.setEditableText(false);
    nameBox.setColour(juce::ComboBox::arrowColourId, juce::Colours::transparentBlack);
    // Ensure default ComboBox label doesn't draw; use FullWidthComboBox overlay colour instead
    nameBox.setColour(juce::ComboBox::textColourId, juce::Colours::transparentBlack);
    nameBox.setColour(FullWidthComboBox::overlayTextColourId, UiThemeColours::cyan());
    nameBox.setVisible(nameMidiSwitch.getToggleState());
    deviceBox.setVisible(!nameMidiSwitch.getToggleState());
    // refreshButton and setupButton are always visible, do not change their visibility here
    idleModeButton.setVisible(false);
    legacyModernButton.setVisible(false);
    sppButton.setVisible(false);
    pulseWidthSlider->setVisible(false);
    pulseWidthValueLabel->setVisible(false);
    // NAME/MIDI toggle initial appearance
    {
        const bool showName = nameMidiSwitch.getToggleState();
        nameMidiSwitch.setButtonText(showName ? "MIDI" : "NAME");
        auto txtCol = (showName ? UiThemeColours::accent() : UiThemeColours::cyan());
        nameMidiSwitch.setColour(juce::TextButton::textColourOffId, txtCol);
        nameMidiSwitch.setColour(juce::TextButton::textColourOnId,  txtCol);
    }
    if (nameMidiSwitch.getToggleState()) { populateNameBox(); }
    // Persist NAME/MIDI toggle into APVTS state so it survives project save/load
    nameMidiSwitch.onClick = [this]{
        // If submenu is open, close it by toggling setupButton and setting animation flags
        // if (setupButton.getToggleState())
        // {   
             
            
        //     setupSubmenuTargetOn = false;
        //     setupSubmenuAnimatingHide = true;
        //     setupButton.setToggleState(false, juce::sendNotification);
        //     //  updateSetupSubmenuLayout(); repaint(); return;
        //     if (setupAnimator)
        //         setupAnimator->start();
               
        // }
        const bool showName = nameMidiSwitch.getToggleState();
        nameMidiSwitch.setButtonText(showName ? "MIDI" : "NAME");
        auto txtCol = (showName ? UiThemeColours::accent() : UiThemeColours::cyan());
        nameMidiSwitch.setColour(juce::TextButton::textColourOffId, txtCol);
        nameMidiSwitch.setColour(juce::TextButton::textColourOnId,  txtCol);
        nameBox.setVisible(showName);
        deviceBox.setVisible(!showName);
        // setupButton and refreshButton are always visible, do not change their visibility here
        if (showName) { loadInstrumentNamesFromState(); populateNameBox(); nameBox.toFront(true); }
        else { deviceBox.toFront(true); }
        if (playgroundComp) playgroundComp->setHeaderHeight(30);
        resized(); repaint(0,0,getWidth(), 34);
        processor.getAPVTS().state.setProperty("ui.showNameMode", showName, nullptr);
    };

    // Setup submenu animator
    setupAnimator = std::make_unique<juce::Animator>(juce::ValueAnimatorBuilder{}
        .withDurationMs(166.0f)
        .withValueChangedCallback([this](float progress){
            const float p = juce::jlimit(0.0f, 1.0f, progress);
            setupSubmenuProgress = setupSubmenuAnimatingHide ? (1.0f - p) : p;
            updateSetupSubmenuLayout();
            const bool active = (setupSubmenuTargetOn || setupSubmenuProgress > 0.0f);
            idleModeButton.setVisible(active);
            legacyModernButton.setVisible(active);
            sppButton.setVisible(active);
            pulseWidthSlider->setVisible(active);
            if (pulseWidthValueLabel) pulseWidthValueLabel->setVisible(active);
            idleModeButton.setInterceptsMouseClicks(active, active);
            legacyModernButton.setInterceptsMouseClicks(active, active);
            sppButton.setInterceptsMouseClicks(active, active);
            pulseWidthSlider->setInterceptsMouseClicks(active, active);
            if (playgroundComp) { playgroundComp->setExternalHoverBlocked(active); if (!active) playgroundComp->clearForcedHoverIndex(); }
            // Arrow visibility depends on header toggle: hide arrowDown when headerSwitchToggle is OFF
            if (arrowDown) arrowDown->setVisible(active && (!headerSwitchToggle || headerSwitchToggle->getToggleState()));
            // Animate canvas height along with submenu slide: when header toggle is ON, base height is 240 and submenu adds 0; when OFF base is 30 and submenu adds up to 30px
            int baseH = (headerSwitchToggle && headerSwitchToggle->getToggleState()) ? 240 : 30;
            int extraH = (headerSwitchToggle && headerSwitchToggle->getToggleState()) ? 0 : 25;
            int newH = baseH + (int) std::round(setupSubmenuProgress * (float) extraH);
            if (newH != getHeight()) setSize(getWidth(), newH);
            repaint(0,0,getWidth(),80);
        }).build());
    vblankUpdater = std::make_unique<juce::VBlankAnimatorUpdater>(this);
    vblankUpdater->addAnimator(*setupAnimator);
    vblankUpdater->addAnimator(ledAnimator);




    setupButton.onClick = [this]{
        setupSubmenuTargetOn = setupButton.getToggleState();
        setupSubmenuAnimatingHide = ! setupSubmenuTargetOn;
        setupAnimator->start();
        // Persist submenu open state
        processor.getAPVTS().state.setProperty("ui.setupSubmenuOn", setupSubmenuTargetOn, nullptr);
        // canvas resizing is handled by the setupAnimator animation callback
    };
    auto configureSetupToggle = [](juce::TextButton& b){
        b.setColour(juce::TextButton::textColourOffId, UiThemeColours::cyan());
        b.setColour(juce::TextButton::textColourOnId, UiThemeColours::cyan());
    };
    configureSetupToggle(idleModeButton); configureSetupToggle(legacyModernButton); configureSetupToggle(sppButton);
    // Default the setup to MODERN behaviour so hosts that start immediately will
    // not receive a pre-Stop message (instant play on DAW start). The button
    // visual state maps true->MODERN, false->LEGACY. Keep processor legacy flag
    // as the inverse of the button toggle so existing onClick logic remains.
    legacyModernButton.setToggleState(true, juce::dontSendNotification);
    processor.setLegacyMode(!legacyModernButton.getToggleState());
    legacyModernButton.setButtonText(legacyModernButton.getToggleState() ? "MODERN" : "LEGACY");
    idleModeButton.onClick = [this]{ const bool on = idleModeButton.getToggleState(); idleModeButton.setButtonText(on ? "IDLE ON" : "IDLE OFF"); };
    legacyModernButton.onClick = [this]{ const bool on = legacyModernButton.getToggleState(); legacyModernButton.setButtonText(on ? "MODERN" : "LEGACY"); processor.setLegacyMode(!on); };
    sppButton.onClick = [this]{ const bool on = sppButton.getToggleState(); sppButton.setButtonText(on ? "S.P.P. ON" : "S.P.P. OFF"); };
    nameBox.onChange = [this]{ const int id = nameBox.getSelectedId(); if (id == 1000) showNewNameDialog(); else if (id == 1001){ instrumentNames.clear(); saveInstrumentNamesToState(); populateNameBox(); processor.getAPVTS().state.setProperty("ui.selectedInstrument", 0, nullptr); } else { if (id >= 1) processor.getAPVTS().state.setProperty("ui.selectedInstrument", id, nullptr); } };

    refreshDeviceList();

    // When the user selects an item from the device combo, map selection id -> device identifier
    // Selection id mapping: 1 => no device selected (placeholder), 2.. => midiOutputs[selId-2]
    deviceBox.onChange = [this]() {
        const int selId = deviceBox.getSelectedId();
        if (selId <= 1)
        {
            processor.setExternalDeviceId({});
        }
        else if (selId - 2 >= 0 && selId - 2 < (int) midiOutputs.size())
        {
            processor.setExternalDeviceId(midiOutputs[(size_t) (selId - 2)].identifier);
        }
        // Schedule a resync when a MIDI port is selected
        if (playgroundComp && playgroundComp->onResyncStepRequested)
            playgroundComp->onResyncStepRequested(1); // step 1 = bar start
    };

    // Backdrop animators (decorative circles)
    for (size_t i = 0; i < backdropAnimators.size(); ++i)
    {
        backdropAnimators[i] = std::make_unique<juce::Animator>(juce::ValueAnimatorBuilder{}
            .withDurationMs((float) UiLayout::kBackdropPulseDurationMs)
            .withValueChangedCallback([this,i](float progress){ backdropPulseProgress[i] = 1.0f - juce::jlimit(0.0f,1.0f,progress); repaint(); })
            .build());
        vblankUpdater->addAnimator(*backdropAnimators[i]);
    }

    loadDancerFrames();

    // Ensure submenu controls are laid out and visibility is correct after construction
    updateSetupSubmenuLayout();
}

void ClockSyncAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll (UiThemeColours::accent());
    auto bounds = getLocalBounds().toFloat();
    juce::ColourGradient bgGrad(UiThemeColours::base(), bounds.getTopLeft(), kBaseLo, bounds.getBottomLeft(), false);
    g.setGradientFill(bgGrad); g.fillRect(bounds);
    // Decorative backdrop circles
    {
        auto centre = bounds.getCentre();
        const auto& sizes = UiLayout::kBackdropSizes;
        for (size_t i = 0; i < sizes.size(); ++i)
        {
            const float baseSz = (float) sizes[i];
            const float darkFactor = 0.888f + 0.888f * (float) i;
            const float progress = backdropPulseProgress[i];
            const float extraScale = backdropPulseScale[i] * progress;
            const float sz = baseSz * (UiLayout::kBackdropMultiplier + extraScale);
            juce::Colour col = UiThemeColours::accent().darker(darkFactor);
            juce::Rectangle<float> rc(centre.x - sz * 0.5f, centre.y - sz * 0.5f + 10.0f, sz, sz);
            g.setColour(col); g.fillEllipse(rc);
        }
        // Hide dancer while pattern edit mode is active to reduce visual clutter.
        if (!patternEditMode)
            drawDancer(g);
        // Run-state indicator is now drawn by the PlaygroundComponent so it can
        // sit between the dancer and the ring wedges with correct z-order. The
        // editor no longer draws it here to avoid duplicate/ misplaced renders.
    }
    // Pattern ring drawn in paintOverChildren when edit mode active (to appear above playground).
}

// --- Unified HitRouting helpers ---
HitContext ClockSyncAudioProcessorEditor::buildHitContext() const
{
    HitContext hc; hc.playground = playgroundComp.get(); hc.pattern = const_cast<PatternRing*>(&pattern); hc.ringArea = ringArea; hc.patternEditMode = patternEditMode; hc.submenuActive = (nameMidiSwitch.getToggleState() && (setupSubmenuTargetOn || setupSubmenuProgress > 0.0f)); hc.outerDiameter = kRingOuterD; hc.innerDiameter = kRingInnerD; return hc;
}

InteractionMode ClockSyncAudioProcessorEditor::getInteractionMode() const
{
    if (patternEditMode) return InteractionMode::PatternEdit;
    if (playgroundComp)
    {
        if (playgroundComp->isPopup3Active()) return InteractionMode::Popup3Open;
        if (playgroundComp->isPopup6Active()) return InteractionMode::Popup6Open;
    }
    if (nameMidiSwitch.getToggleState() && (setupSubmenuTargetOn || setupSubmenuProgress > 0.0f)) return InteractionMode::SubmenuActive;
    return InteractionMode::Normal;
}

HitResult ClockSyncAudioProcessorEditor::routeHit(const juce::MouseEvent& e, bool isHover)
{
    auto hc = buildHitContext();
    auto mode = getInteractionMode();
    return performHitTest(e.position.toFloat(), hc, mode);
}

void ClockSyncAudioProcessorEditor::paintOverChildren(juce::Graphics& g)
{
    // Submenu should draw first (behind header) so header masks its top portion.
    const bool submenuActive = (setupSubmenuTargetOn || setupSubmenuProgress > 0.0f);
    if (submenuActive)
    {
        const float hiddenTop = -10.0f; // fully hidden behind header
        const float shownTop  = 25.0f;  // final revealed position (shift upward by 20px per request)
        const float submenuH  =  30.0f;
        const float topY = hiddenTop + (shownTop - hiddenTop) * setupSubmenuProgress;
        juce::Rectangle<float> submenuRect(0.0f, topY, (float)getWidth(), submenuH);
        // Drop shadow for submenu (lighter & smaller than header shadow). Draw first, then fill.
        {
            juce::DropShadow ds(juce::Colours::black.withAlpha(0.30f), 10, juce::Point<int>(0, 4));
            // Slightly shrink width to avoid horizontal bleed; allow a couple px extra height for blur.
            auto shadowInt = submenuRect.toNearestInt().expanded(-2, 2);
            ds.drawForRectangle(g, shadowInt);
        }
        g.setColour(UiThemeColours::base());
        g.fillRect(submenuRect);
        // Repaint buttons manually AFTER fill so they appear above submenu background (normal child paint occurred earlier & is covered).
        // Only repaint the components; layout is handled in updateSetupSubmenuLayout
        auto repaintComp = [&g](juce::Component* c){ if (!c || !c->isVisible()) return; juce::Graphics::ScopedSaveState ss(g); g.setOrigin(c->getX(), c->getY()); c->paint(g); g.setOrigin(0,0); };
        repaintComp(pulseWidthSlider ? pulseWidthSlider.get() : nullptr);
        repaintComp(pulseWidthValueLabel ? pulseWidthValueLabel.get() : nullptr);
        repaintComp(arrowDown ? arrowDown.get() : nullptr);
        repaintComp(&idleModeButton);
        repaintComp(&legacyModernButton);
        repaintComp(&sppButton);

        // Hover tooltips (avoid drawing above header region 0..30)
        juce::Font vf(juce::FontOptions("Arial", 11.0f, juce::Font::bold));
        if (hoveredSetupIndex >= 0)
        {
            g.setColour(UiThemeColours::accent());
            g.setFont(vf);
            auto drawTip = [&](const juce::String& text, const juce::Component& c){
                juce::Rectangle<int> r = c.getBounds();
                juce::Rectangle<int> tipR(r.getX(), r.getY() - 12, r.getWidth(), r.getHeight());
                if (tipR.getBottom() < 30) return; // clipped by header
                g.drawFittedText(text, tipR, juce::Justification::centred, 1);
            };
            if (hoveredSetupIndex == 0) drawTip("send midi-clocks when idle/stopped", idleModeButton);
            else if (hoveredSetupIndex == 1) drawTip("legacy sends always stop before start", legacyModernButton);
            else if (hoveredSetupIndex == 2) drawTip("send song position pointer", sppButton);
        }
    }

    // Pattern edit drawing moved fully into PlaygroundComponent; suppress duplicate backdrop here.
    // (Previous pattern.draw caused second semi-transparent wedge layer.)
    if (patternEditMode)
    {
        // No-op: intentional removal to avoid double rendering.
    }

    // Draw fixed header (30px) at top (on top of submenu).
    const int headerPaintH = 30;
    juce::Rectangle<float> headerRect(0.0f, 0.0f, (float) getWidth(), (float) headerPaintH);
    // Draw drop shadow (using juce::DropShadow) first so base fill sits on top.
    {
        juce::DropShadow ds(juce::Colours::black.withAlpha(0.45f), 14, juce::Point<int>(0, 6));
        // Use integer rect for shadow drawing; shrink width slightly to avoid horizontal bleed
        auto shadowInt = headerRect.toNearestInt().withWidth(headerRect.getWidth() - 2);
        ds.drawForRectangle(g, shadowInt);
    }
    // Base header fill rectangle
    g.setColour(UiThemeColours::base());
    g.fillRect(headerRect);
    // Repaint visible header child components manually so they appear above the header background fill.
    auto repaintHeaderComp = [&g](juce::Component& c)
    {
        if (! c.isVisible()) return;
        juce::Graphics::ScopedSaveState ss(g);
        g.setOrigin(c.getX(), c.getY());
        c.paint(g);
        g.setOrigin(0,0);
    };
    repaintHeaderComp(nameMidiSwitch);
    //if (refreshButton.isVisible()) repaintHeaderComp(refreshButton);
    //if (setupButton.isVisible()) repaintHeaderComp(setupButton);
     repaintHeaderComp(refreshButton);
     repaintHeaderComp(setupButton);
    repaintHeaderComp(deviceBox);
    repaintHeaderComp(nameBox);
    // Ensure the status bar (LED + text) is repainted above the header fill so
    // it remains visible — it is a child component but the header is painted
    // in paintOverChildren which would otherwise cover children drawn earlier.
    if (statusBar && statusBar->isVisible()) repaintHeaderComp(*statusBar);
    // Draw the header toggle above the header background, right of status bar
    if (headerSwitchToggle && headerSwitchToggle->isVisible())
        repaintHeaderComp(*headerSwitchToggle);
    // Setup submenu drawn separately below; buttons repainted there.

    // Skip fallback ring/dancer drawing when playground is active.


    // Draw popup menu rings (if configured). Place them at the centres of
    // the components they replace so they visually substitute the legacy UI.
    auto drawAtComp = [&](const std::unique_ptr<PopupMenuRing>& pr, const juce::Component& c)
    {
        if (! pr) return;
        auto b = c.getBounds().toFloat();
        const float baseX = b.getCentreX();
        const float baseY = b.getCentreY();
        const float baseR = std::min(b.getWidth(), b.getHeight()) * 0.5f;
        pr->draw(g, baseX, baseY, baseR);
    };

    // Legacy popup rings fully removed (PlaygroundComponent provides internal popup UI).

    // StatusBarComponent now paints status + LED.

    // Debug offset panel removed per user request to declutter lower area during shuffle hit testing.
    // Draw small build number at lower-right (same vertical region as hoverText)
    {
        // Compute build-only string from PLUGIN_VERSION_WITH_BUILD macro (extract trailing "build N").
        juce::String ver = PLUGIN_VERSION_WITH_BUILD;
        juce::String buildStr = ver;
        const int idx = ver.indexOf("build ");
        if (idx >= 0)
            buildStr = ver.substring(idx + 6); // number and trailing
        g.setColour(UiThemeColours::accent().darker(3.33f));
        // Use the newer FontOptions-based constructor to avoid deprecated API warnings
        g.setFont(juce::Font(juce::FontOptions("Arial", 12.0f, juce::Font::plain)));
        const int pad = 6;
        juce::Rectangle<int> r(getWidth() - 80, 224, 74, 16);
        g.drawFittedText("3.0." + buildStr.trim(), r, juce::Justification::centredRight, 1);
    }

    // Debug stack (left side): show last clicked stored step, logical mapping, host bar and scheduled targets
    // {
    //     const int leftX = 6;
    //     int y = 40;
    //     const int lineH = 14;
    //     g.setColour(UiThemeColours::cyan().withAlpha(0.95f));
    //     g.setFont(juce::Font(juce::FontOptions("Arial", 11.0f, juce::Font::plain)));

    //     int clicked = lastClickedStoredIndex;
    //     const int visualRotation = 4;
    //     juce::String clickedLine;
    //     if (clicked >= 0)
    //     {
    //         int logical = ((clicked - visualRotation) & 15) + 1; // 1..16
    //         clickedLine = "Clicked stored: " + juce::String(clicked) + "  (logical " + juce::String(logical) + ")";
    //     }
    //     else clickedLine = "Clicked stored: N/A";
    //     g.drawFittedText(clickedLine, juce::Rectangle<int>(leftX, y, 200, lineH), juce::Justification::left, 1);
    //     y += lineH + 2;

    //     int hostBar = processor.getUiExternalBarNumber();
    //     juce::String hostLine = "Host bar: "; hostLine += (hostBar >= 0) ? juce::String(hostBar) : juce::String("N/A");
    //     g.drawFittedText(hostLine, juce::Rectangle<int>(leftX, y, 200, lineH), juce::Justification::left, 1);
    //     y += lineH + 2;

    //     long long rtBar = processor.getResyncTargetBar();
    //     int rtStep = processor.getResyncTargetStep();
    //     juce::String resyncLine = "Resync target: ";
    //     if (rtBar >= 0) resyncLine += "bar " + juce::String(rtBar) + " step " + juce::String(rtStep);
    //     else resyncLine += "none";
    //     g.drawFittedText(resyncLine, juce::Rectangle<int>(leftX, y, 240, lineH), juce::Justification::left, 1);
    //     y += lineH + 2;

    //     long long pendingBar = processor.getPendingPatternRestartTargetBar();
    //     juce::String pendingLine = "Pending pat restart bar: "; pendingLine += (pendingBar >= 0) ? juce::String(pendingBar) : juce::String("N/A");
    //     g.drawFittedText(pendingLine, juce::Rectangle<int>(leftX, y, 240, lineH), juce::Justification::left, 1);
    // }

}

void ClockSyncAudioProcessorEditor::resized()
{
    // Static absolute positions for fixed 300x300 canvas
    // Ensure shared playground component occupies the full editor. The header
    // overlays in Z, not by shifting content in Y.
    if (playgroundComp)
    {
        playgroundComp->setBounds(0, 0, getWidth(), getHeight());
        playgroundComp->setHeaderHeight(30); // keep fixed; Setup expansion only paints extra base area
    }
    // Header area is painted, no components there (top 28px)

    {
        const bool showName = nameMidiSwitch.getToggleState();
        // Painted header height (base 30 + optional 20 for Setup row)
        const int headerH = 30; // fixed header height now
        const int marginX = 5;
        const int marginY = 3;
        // First-row content height is fixed (decoupled from Setup expansion)
        const int baseHeaderH = 30;
        const int contentH = baseHeaderH - marginY * 2; // stay constant at 24px
        const int gap = 4;
        const int buttonW = std::max(16, contentH - 6); // square-ish refresh button (width ~= height)
        const int toggleW = 14; // same width for name/midi toggle
        const int fullComboOriginal = getWidth() - marginX * 2 - buttonW - gap - toggleW - gap; // account for toggle + refresh
        int comboW = fullComboOriginal - 90; // keep prior shrink
        // Reduce all combo widths by 10px as requested, but keep a sensible minimum
        comboW = std::max(60, comboW - 10);
        const int centerX = getWidth() / 2;
        const int comboX = centerX - comboW / 2;
        // place refresh button immediately left of the combo
        refreshButton.setBounds(comboX + comboW -1, marginY + 5, toggleW, contentH - 10);
        nameMidiSwitch.setBounds(comboX - 44, marginY + 5, 40, contentH - 10);
        // place setupButton left of nameMidiSwitch (moved further left to avoid clipping)
        const int extraLeft = 17; // nudge left
        setupButton.setBounds(nameMidiSwitch.getX() - (toggleW + 4), marginY + 5, toggleW, contentH - 10);
        // comboboxes share same bounds; only one visible at a time
        deviceBox.setBounds(comboX, marginY + 1 , comboW, contentH - 2);
        nameBox.setBounds(comboX, marginY + 1, comboW, contentH - 2);

        // Setup submenu layout now handled in paintOverChildren via animation; initial hidden position set here.
        updateSetupSubmenuLayout();
        
        // Combo remains interactive (double-click to edit).
        // make refresh button small single-letter
        refreshButton.setButtonText("R");
        setupButton.setButtonText("S");
    }

    // Position imported components exactly as authored in LayoutPlayground (use canonical baseCircles)
    if (playgroundComp && playgroundComp->getCircleCount() >= 9)
    {
        // Use canonical positions from Main.cpp (baseCircles). Do NOT apply
        // any vertical shift; the header is a Z-overlay.

        // idx7 (click rate rotary + inner click-to-pulse button) is fully handled
        // by the PlaygroundComponent. Keep legacy slider/button hidden and do not
        // lay them out when the playground is active.

        // circle 0 -> ring area
        {
            float x0,y0,r0; if (playgroundComp->getCircleInfo(0,x0,y0,r0))
            {
                const int d = (int) std::round(r0 * 2.0f);
                ringArea = juce::Rectangle<int>((int)std::round(x0 - r0), (int)std::round(y0 - r0), d, d);
            }
        }

        // circle 1 handled by playground visuals.

        // circle 2 -> idleClockToggle
        {
            float x2,y2,r2; if (playgroundComp->getCircleInfo(2,x2,y2,r2))
            {
                const int w = (int) std::round(r2 * 2.0f);
                idleClockToggle.setBounds((int)std::round(x2 - r2), (int)std::round(y2 - r2), w, w);
            }
        }

        // circle 5: shuffle / clockWhileStopped visuals.

        // Resync step selection handled by ring segments.

        // Help toggle: keep its authored placement relative to canvas (use previous explicit position)
        const int helpSize = 25;
        helpToggle.setBounds(0, 215, helpSize, helpSize); // moved +5px in Y
    }
    else
    {
        // Fallback positions if playground absent; keep clickButton hidden.
        ringArea = juce::Rectangle<int>(150 - 70, 130 - 70, 140, 140);
        //idleClockToggle.setBounds(234 - 14, 132 - 14, 28, 28);
        // shuffleScaleToggle removed.
        // no legacy triggerRect fallback
        // stepOffsetMenu no longer present.
        const int helpSize = 25; helpToggle.setBounds(0, 215, helpSize, helpSize); // moved +5px in Y (fallback)
    }

    // Position status bar inside header (right-aligned region)
    if (statusBar)
    {
        // Provide area matching previous manual drawing region (right segment of header minus margins)
        const int headerH = 30;
        const int w = 50; // width for text + LED
        statusBar->setBounds(getWidth() - w - 5, 0, w, headerH);
    }

    // Force absolute placement to keep toggle at exact design coordinates.
    if (headerSwitchToggle) {
        headerSwitchToggle->setBounds(286, 5, 10, 20);
        headerSwitchToggle->setInterceptsMouseClicks(true, true);
        headerSwitchToggle->toFront(true);
        headerSwitchToggle->setAlwaysOnTop(true);
    }
}

// Update setup submenu button positions based on current animation progress (without repaint).
void ClockSyncAudioProcessorEditor::updateSetupSubmenuLayout()
{
    // Defensive: check all required pointers before use
    if (!pulseWidthSlider || !pulseWidthValueLabel || !arrowDown)
        return;

    // Only show and position controls if submenu is open or animating
    if (!(setupSubmenuTargetOn || setupSubmenuProgress > 0.0f)) {
        idleModeButton.setVisible(false);
        legacyModernButton.setVisible(false);
        sppButton.setVisible(false);
        pulseWidthSlider->setVisible(false);
        arrowDown->setVisible(false);
        if (pulseWidthValueLabel) pulseWidthValueLabel->setVisible(false);
        return;
    }
    const bool active = (setupSubmenuTargetOn || setupSubmenuProgress > 0.0f);
    // Layout: slider on the left, then buttons, all horizontally aligned
    const float hiddenTop = -10.0f;
    const float shownTop  = 25.0f;
    const float topY = hiddenTop + (shownTop - hiddenTop) * setupSubmenuProgress;
    const float submenuH = 30.0f;
    const int sliderW = 46;
    const int btnW = 60;
    const int btnH = 14;
    const int gap = 4;
    const int totalW = sliderW + btnW * 3 + gap * 4;
    const int x0 = (getWidth() - totalW) / 2;
    // Move slider down a bit and make it taller so value box is not hidden
    const int yButtons = (int) std::round(topY + 10); // 6px padding from top of submenu

    int x = x0;
    pulseWidthSlider->setBounds(x - 20, yButtons, sliderW, btnH);
    if (themeLNF)
        pulseWidthSlider->setLookAndFeel(themeLNF.get());

    // Place the value label immediately to the right of the slider
    x += sliderW ;
    const int valueLabelW = 40;
    pulseWidthValueLabel->setBounds(x - 25, yButtons, valueLabelW, btnH);
    pulseWidthValueLabel->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    pulseWidthValueLabel->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    pulseWidthValueLabel->setVisible(true);

    // Position the arrow centered below the value label
    const int arrowW = 22;
    const int arrowH = 15;
    int arrowX = pulseWidthValueLabel->getX() + (pulseWidthValueLabel->getWidth() - arrowW) / 2;
    int arrowY = pulseWidthValueLabel->getBottom() + 2;
    arrowDown->setBounds(arrowX, arrowY, arrowW, arrowH);
    arrowDown->setVisible(active && (!headerSwitchToggle || headerSwitchToggle->getToggleState()));

    x += gap + 20 ;

    idleModeButton.setBounds(x, yButtons, btnW, btnH);
    x += btnW + gap;
    legacyModernButton.setBounds(x, yButtons, btnW, btnH);
    x += btnW + gap;
    sppButton.setBounds(x, yButtons, btnW, btnH);
    pulseWidthSlider->setVisible(true);
    pulseWidthValueLabel->setVisible(true);
        arrowDown->setVisible(active && (!headerSwitchToggle || headerSwitchToggle->getToggleState()));
    idleModeButton.setVisible(true);
    legacyModernButton.setVisible(true);
    sppButton.setVisible(true);

    // Keep label in sync with slider value
    if (pulseWidthSlider)
    {
        pulseWidthSlider->onValueChange = [this]()
        {
            if (pulseWidthValueLabel && pulseWidthSlider)
                pulseWidthValueLabel->setText(juce::String((int)pulseWidthSlider->getValue()) + " ms", juce::dontSendNotification);
        };
        // Set initial value
        pulseWidthSlider->onValueChange();
    }

    // --- Adjust layout: slider + label + legacyModernButton + idleModeButton + sppButton ---
    // (updateSetupSubmenuLayout handles positioning)
    // Add label to layout after slider, before legacyModernButton

}

// Removed duplicate legacy mouseMove (#if 0 block) – only one active handler remains.

// Tooltips handled by PlaygroundComponent hoverText.

void ClockSyncAudioProcessorEditor::timerCallback()
{
    bool needAll = false;
    bool needRing = false;
    bool needTrigger = false;
    // Process any scheduled backdrop animator starts (staggered starts placed by beat detection).
    const double nowMsTop = juce::Time::getMillisecondCounterHiRes();
    constexpr double kPerBackdropDelayMs = 88.0; // ~66ms stagger between successive backdrop circles
    for (size_t si = 0; si < backdropScheduledStartMs.size(); ++si)
    {
        double s = backdropScheduledStartMs[si];
        if (s > 0.0 && s <= nowMsTop)
        {
            if (si < backdropAnimators.size() && backdropAnimators[si])
                backdropAnimators[si]->start();
            backdropScheduledStartMs[si] = 0.0;
            needAll = true; // scheduled starts affect full-canvas backdrop
        }
    }
    
    // LED update: do NOT set ledLevel on every MIDI clock tick (uiClockCounter)
    // — that was causing the LED to blink at clock resolution. Instead we
    // allow the quarter-beat logic below to set `ledLevel` on beat boundaries
    // (so the LED blinks in step with the backdrop pulses). We still perform
    // decay each timer tick so the LED falls off between beats.
    const auto counter = processor.getUiClockCounter();
    if (counter != lastSeenClockCounter)
    {
        lastSeenClockCounter = counter;
        // Ensure dancer advances every 24PPQ pulse by repainting ring area
        needRing = true;
        // When a new MIDI clock tick arrives, check whether the 1/16 step
        // has advanced and drive any step-aligned UI updates (LED pulse,
        // backdrop sequencing) from this branch. This is more robust than
        // relying on a separate read later in the function where races can
        // cause missed updates.
        const int step16_now = juce::jlimit(1, 16, processor.getUiStep16());
        if (step16_now != stepNumberCached)
        {
            stepNumberCached = step16_now;
            needTrigger = true;
            const int beatIndex = (step16_now - 1) / 4; // 0..3

            // Decide LED pulse level for this 16th step and start the animator
            const bool hasNext = processor.getUiNextRestartPending();
            const bool armed = processor.getUiPendingStart();
            if (hasNext || armed)
                ledPulseTarget = 1.0f;
            else if (! runParamCached && idleModeButton.getToggleState())
                ledPulseTarget = 0.5f;
            else if (runParamCached)
                ledPulseTarget = 1.0f;
            else
                ledPulseTarget = 0.0f;

            if (ledPulseTarget > 0.0f)
            {
                // If we're in PENDING/ARMED mode, only pulse once per quarter-note
                // (beatIndex reflects quarter boundaries). For normal running mode
                // pulse on every 16th-step.
                if (hasNext || armed)
                {
                    if (beatIndex != lastLedBeatIndex)
                    {
                        lastLedBeatIndex = beatIndex;
                        ledAnimator.start();
                    }
                }
                else
                {
                    ledAnimator.start();
                }
            }

            // Backdrop pulse sequencing: on quarter-note boundaries schedule staggered starts
            if (beatIndex != lastBackdropBeatIndex)
            {
                lastBackdropBeatIndex = beatIndex;
                const double nowMs = juce::Time::getMillisecondCounterHiRes();
                // Invert pulse sequence: previously index 0 pulsed first. Now highest index
                // starts immediately and lower indices are staggered later so the visual
                // ripple direction reverses.
                const size_t total = backdropScheduledStartMs.size();
                for (size_t i = 0; i < total; ++i)
                {
                    const size_t reversedIdx = total  - 2 - i; // total-1 -> 0
                    backdropScheduledStartMs[i] = nowMs + (double)reversedIdx * kPerBackdropDelayMs;
                }
                // request full repaint when pulses begin
                needAll = true;
                // Also ask the playground to schedule its per-circle pulses so
                // the UI circles (not just the decorative backdrop) pulse in
                // sync with the editor-driven beat. Playground exposes a
                // wrapper that schedules pulses for all circles.
                // Do NOT request playground pulses: only decorative backdrop
                // animators in the editor should pulse on beats. Avoid invoking
                // playground scheduling so UI elements do not animate on the beat.
            }
        }
    }

    // Run param / engine flags / pending start
    if (auto* rp = processor.getAPVTS().getRawParameterValue(ClockSyncAudioProcessor::paramRun))
    {
        const bool runNow = rp->load() > 0.5f;
        if (runNow != runParamCached) {
            runParamCached = runNow;
            needRing = true;
            // Visual feedback: pulse LED when Run toggles on/off so user sees state change
            if (runParamCached)
                ledPulseTarget = 1.0f;
            else
                ledPulseTarget = 0.6f; // softer pulse for stop
            if (ledPulseTarget > 0.0f) ledAnimator.start();
        }
    }
    // Mirror trigger mode param into playground idx8 button so automation updates UI
    if (playgroundComp)
    {
        if (auto* tm = processor.getAPVTS().getRawParameterValue(ClockSyncAudioProcessor::paramTriggerModeEnabled))
        {
            playgroundComp->setTriggerModeState(tm->load() > 0.5f);
        }
    }
    // Cache NEXT state to detect restart application (edge: true -> false)
    const bool uiNextPendingNow = processor.getUiNextRestartPending();
    const bool running = processor.getUiIsRunning();
    if (running != engineRunningCached) {
        engineRunningCached = running;
        needAll = true;
        if (playgroundComp) playgroundComp->setRunState(running, false); // host-driven: don't toggle user Run parameter
            // Ensure ring internal progression uses host tempo + rate multiplier (clear external playhead usage implicitly)
            if (playgroundComp && running) {
                playgroundComp->setHostTempo(processor.getUiBpm());
            }
        // When transport/run starts, initialise cycle start using selected offset so
        // visual relative step reflects offset immediately (selected step becomes relative target)
        if (running && cycleStartStepAbsolute < 0) {
            if (selectedResyncStepCached > 1) {
                // Shift cycle start backwards so the first visual relative step aligns with selected offset.
                int stepNow = juce::jlimit(1,16, processor.getUiStep16());
                int cs = stepNow - (selectedResyncStepCached - 1);
                while (cs <= 0) cs += 16;
                cycleStartStepAbsolute = cs; // 1..16
            }
        }
    }
    const bool armed = processor.getUiPendingStart();
    if (armed != pendingStartCached) {
        pendingStartCached = armed;
        needAll = true;
        // pulse LED when armed status changes
        ledPulseTarget = armed ? 1.0f : 0.5f;
        if (ledPulseTarget > 0.0f) ledAnimator.start();
    }

    // Rate choice
    if (rateParam)
    {
        const int idx = rateParam->getIndex();
        if (idx != rateIndexCached)
        {
            rateIndexCached = idx;
            // Keep Playground idx6 (rate popup) in sync with APVTS, same as GridScaleMenu
            if (playgroundComp)
            {
                int displayed = 16; // map index -> division label
                switch (idx)
                {
                    case 0: displayed = 32; break;
                    case 1: displayed = 16; break;
                    case 2: displayed = 8;  break;
                    case 3: displayed = 4;  break;
                    default: displayed = 16; break;
                }
                playgroundComp->setClockRateIndexValue(displayed);
                    // Update host tempo each rate change to ensure internal progression uses latest BPM
                    playgroundComp->setHostTempo(processor.getUiBpm());
            }
            needRing = true;
        }
    }

    // Update trigger fade animator (drives repaint via value changed callback)
    // Trigger fade animator is now driven by the VBlankAnimatorUpdater; the
    // value-changed callback repaints the trigger area when necessary.

    // Update LED animator so pulses decay (ledAnimator callbacks repaint header)
    // LED animator is driven by the VBlankAnimatorUpdater; its callback
    // repaints the header when active.

    

    // (Previously backdrop sequencing and LED start logic ran here based on
    // reading `getUiStep16()`. That could miss updates due to timing races;
    // the step-driven logic now lives in the clock-counter branch above.)

    // Fallback: if for any reason the MIDI clock counter didn't advance but
    // the reported 1/16 `uiStep16` has changed (e.g. host/processor timing),
    // perform the same step-aligned updates here to avoid missing pulses.
    {
        const int step16_now = juce::jlimit(1, 16, processor.getUiStep16());
        if (step16_now != stepNumberCached)
        {
            stepNumberCached = step16_now;
            needTrigger = true;
            const int beatIndex = (step16_now - 1) / 4; // 0..3

            const bool hasNext = processor.getUiNextRestartPending();
            const bool armed = processor.getUiPendingStart();
            if (hasNext || armed)
                ledPulseTarget = 1.0f;
            else if (! runParamCached && idleModeButton.getToggleState())
                ledPulseTarget = 0.5f;
            else if (runParamCached)
                ledPulseTarget = 1.0f;
            else
                ledPulseTarget = 0.0f;

            if (ledPulseTarget > 0.0f)
            {
                // If pending/armed, only pulse once per quarter
                if (hasNext || armed)
                {
                    if (beatIndex != lastLedBeatIndex)
                    {
                        lastLedBeatIndex = beatIndex;
                        ledAnimator.start();
                    }
                }
                else
                {
                    ledAnimator.start();
                }
            }

            if (beatIndex != lastBackdropBeatIndex)
            {
                lastBackdropBeatIndex = beatIndex;
                const double nowMs = juce::Time::getMillisecondCounterHiRes();
                constexpr double kPerBackdropDelayMs = 22.0;
                for (size_t i = 0; i < backdropScheduledStartMs.size(); ++i)
                    backdropScheduledStartMs[i] = nowMs + (double)i * kPerBackdropDelayMs;
                needAll = true;
            }
        }
    }

    // Decay backdrop pulse progress (frame-based) so pulses scale/fade back over time
    // Backdrop pulse decay is now driven by per-ring animators (VBlank-driven)

    // Step number update tied to 16th-note changes (no fade).
    // The numeric step is tracked internally, but the visual playhead
    // wedge (`visualStepCached`) should advance ONLY while actually running
    // (no fallback 120 BPM chaser when idle). This mirrors the playground’s
    // canonical behaviour.
        {
            const int stepNow = juce::jlimit(1, 16, processor.getUiStep16());
            // Forward host bar information when available so playground's
            // auto-trigger interval counting is driven by explicit bar
            // boundaries (more robust than relying purely on step==0).
            if (playgroundComp)
            {
                // Use cached bar number written by the audio thread in processBlock
                // to avoid calling playHead->getPosition() from the message thread
                // (the host playhead wrapper is only valid during audio callbacks).
                int barNumber = processor.getUiExternalBarNumber();
                // If the audio thread just detected a host START edge, ask the
                // playground to reset its external-bar tracking first so the
                // next forwarded bar number is treated as a fresh first bar.
                if (processor.consumeUiHostStartPending())
                {
                    playgroundComp->setExternalBarNumber(-1);
                }
                // Forward cached bar number (may be -1 when host transport is unavailable)
                // so the playground can reset its autofill counters when the DAW stops.
                playgroundComp->setExternalBarNumber(barNumber);

                // Always forward DAW step to the playground after bar info so
                // `patternBarActive` is set correctly before onStepChanged runs.
                playgroundComp->setExternalPlayheadStep(stepNow);
            }

            if (stepNow != stepNumberCached)
        {
            stepNumberCached = stepNow;
            needTrigger = true;

            // Decide LED pulse level for this 16th step and start the animator
            const bool hasNext = processor.getUiNextRestartPending();
            const bool armed = processor.getUiPendingStart();
            if (hasNext || armed)
                ledPulseTarget = 1.0f;
            else if (! runParamCached && idleModeButton.getToggleState())
                ledPulseTarget = 0.5f;
            else if (runParamCached)
                ledPulseTarget = 1.0f;
            else
                ledPulseTarget = 0.0f;

            if (ledPulseTarget > 0.0f)
            {
                // When we reach this 16th-step, determine whether we should
                // start the LED animator now. For pending/armed, only start
                // at quarter-note boundaries to achieve a slower beat-rate blink.
                const int beatIndex = (stepNow - 1) / 4;
                if (hasNext || armed)
                {
                    if (beatIndex != lastLedBeatIndex)
                    {
                        lastLedBeatIndex = beatIndex;
                        ledAnimator.start();
                    }
                }
                else
                {
                    ledAnimator.start();
                }
            }
        }
        // Advance visual wedge only when the engine is actually running; do not
        // advance on the idle 120 BPM fallback. This ensures the chaselights
        // are driven by the real playhead (visualStepCached), consistent with
        // the segment colouring.
        if (engineRunningCached && stepNow != visualStepCached)
        {
            // Keep visual wedge loosely in sync with real step (no rate scaling here; ring handles scaled progression).
            visualStepCached = ((stepNow % 16) + 1);
            needRing = true; // ring contains the wedge
            // External playhead sync deferred until relativeStepCached updated below.
        }
        // Detect restart application: either NEXT just cleared or pendingStart just cleared at a running state.
        // Restart applied when NEXT indicator clears (true -> false). PendingStart edge is already handled by processor flags.
        const bool restartApplied = (nextRestartPendingCached && ! uiNextPendingNow);
        // If NEXT just cleared and we're running, capture the absolute step as cycle start.
        if (restartApplied && engineRunningCached)
        {
            // If a manual trigger with an offset preview was active, adjust the
            // recorded cycle start so the relative step matches the selected
            // resync offset. Otherwise use the reported absolute step.
            if (manualTriggerOffsetActive)
            {
                int cs = stepNow - manualTriggerPlayheadDelta; // may underflow
                while (cs <= 0) cs += 16;
                cycleStartStepAbsolute = cs; // 1..16
            }
            else
            {
                cycleStartStepAbsolute = stepNow; // absolute bar step where restart applied
            }
            // Clear manual trigger offset state – the real restart just applied.
            manualTriggerOffsetActive = false;
            manualTriggerPlayheadDelta = 0;
            // Full ring flash to acknowledge restart application (intensity boost if offset selected)
            if (playgroundComp) playgroundComp->flashFullRing(selectedResyncStepCached > 1 ? 1.5f : 1.0f);
            // Commit any pending speed multiplier change now so visual chase speed updates ONLY on restart.
            if (playgroundComp) playgroundComp->commitPendingSpeedMultiplier();
        }
        // Initialise cycle start if not set yet once running
        if (cycleStartStepAbsolute < 0 && engineRunningCached)
            cycleStartStepAbsolute = stepNow;
        // Compute relative step counting from cycleStartStepAbsolute (1..16)
        if (cycleStartStepAbsolute > 0)
        {
            relativeStepCached = ((stepNow - cycleStartStepAbsolute + 16) % 16) + 1; // 1..16
        }
        nextRestartPendingCached = uiNextPendingNow;
        // Do not override the playground's running playhead here. The chase-light
        // must continue running uninterrupted. Manual-trigger previews flash a
        // single segment via flashSegmentLogical without setting the external
        // playhead, and the actual restart alignment is applied when the restart
        // event is seen (cycleStartStepAbsolute adjusted above).
    }

    // Cache selected resync offset step from parameter.
    {
        int paramStep = -1;
        if (auto* pi = dynamic_cast<juce::AudioParameterInt*>(processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramResyncOffsetStep)))
            paramStep = juce::jlimit(1, 16, pi->get());
        if (paramStep > 0)
            selectedResyncStepCached = paramStep;
    }

    // Keep pattern visual in sync with the parameter in case it changed externally
    if (auto* psi = dynamic_cast<juce::AudioParameterInt*>(processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramPatternSteps)))
    {
        int v = (int) juce::jlimit(0, 65535, psi->get());
        if (v != patternParamCached)
        {
            patternParamCached = v;
            if (playgroundComp)
                playgroundComp->setPatternBitmask((uint16_t) v);
            else
                pattern.setBitmask((uint16_t) v);
            needRing = true;
        }
    }

    // Playground reflects shuffle amount visually.

    // Dispatch minimal repaints
    // Update popup animations (driven from editor timer so they pause when editor is suspended)
    {
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        double dtMs = 16.6667;
        if (lastUiUpdateMs > 0.0) dtMs = nowMs - lastUiUpdateMs;
        lastUiUpdateMs = nowMs;
        // Decay segment fade trail (~600ms to fully fade)
        ///const float decayMs = 600.0f;
        // Slow the decay slightly to improve visibility of the cyan trail.
        // (Previous value 600ms made fades too brief; new 900ms extends trail.)
        // NOTE: retain variable name for minimal change; adjust value only.
        // (We keep original line for context; override below.)
        
    }
    // Update status bar text + LED level each timer tick if changed
    if (statusBar)
    {
        const bool isRunning = processor.getUiIsRunning();
        const bool isArmed = processor.getUiPendingStart();
        const bool hasNext = processor.getUiNextRestartPending();
        juce::String st;
        if (hasNext) st = "WAIT";
        else if (isRunning) st = "LOCK";
        else if (isArmed) st = "ARM'D'";
        else st = idleModeButton.getToggleState() ? "IDLE" : "STOP";
        statusBar->setStatusText(st);
        statusBar->setLedLevel(ledLevel);
    }
    if (needAll) { repaint(); return; }
    if (needRing) repaint(ringArea);
    // Trigger updates can affect the ring drawing (visual slice near trigger). Ensure
    // we repaint both the trigger rect and the ring area to avoid leftover artefacts.
    if (needTrigger)
    {
        // Trigger updates can affect the ring drawing; repaint ring.
        repaint(ringArea);
    }
}

// ---------------- Dancer PNG sequence ----------------
void ClockSyncAudioProcessorEditor::loadDancerFrames()
{
    dancerFrames.clear();
    dancerFrames.reserve(24);
    auto addFrame = [this](const void* data, int size){
        if (! data || size <= 0) return;
       
        if (auto d = juce::Drawable::createFromImageData(data, size))
            dancerFrames.push_back(std::move(d));
    };
    addFrame(BinaryData::dancer_0_png,  BinaryData::dancer_0_pngSize);
    addFrame(BinaryData::dancer_1_png,  BinaryData::dancer_1_pngSize);
    addFrame(BinaryData::dancer_2_png,  BinaryData::dancer_2_pngSize);
    addFrame(BinaryData::dancer_3_png,  BinaryData::dancer_3_pngSize);
    addFrame(BinaryData::dancer_4_png,  BinaryData::dancer_4_pngSize);
    addFrame(BinaryData::dancer_5_png,  BinaryData::dancer_5_pngSize);
    addFrame(BinaryData::dancer_6_png,  BinaryData::dancer_6_pngSize);
    addFrame(BinaryData::dancer_7_png,  BinaryData::dancer_7_pngSize);
    addFrame(BinaryData::dancer_8_png,  BinaryData::dancer_8_pngSize);
    addFrame(BinaryData::dancer_9_png,  BinaryData::dancer_9_pngSize);
    addFrame(BinaryData::dancer_10_png, BinaryData::dancer_10_pngSize);
    addFrame(BinaryData::dancer_11_png, BinaryData::dancer_11_pngSize);
    addFrame(BinaryData::dancer_12_png, BinaryData::dancer_12_pngSize);
    addFrame(BinaryData::dancer_13_png, BinaryData::dancer_13_pngSize);
    addFrame(BinaryData::dancer_14_png, BinaryData::dancer_14_pngSize);
    addFrame(BinaryData::dancer_15_png, BinaryData::dancer_15_pngSize);
    addFrame(BinaryData::dancer_16_png, BinaryData::dancer_16_pngSize);
    addFrame(BinaryData::dancer_17_png, BinaryData::dancer_17_pngSize);
    addFrame(BinaryData::dancer_18_png, BinaryData::dancer_18_pngSize);
    addFrame(BinaryData::dancer_19_png, BinaryData::dancer_19_pngSize);
    addFrame(BinaryData::dancer_20_png, BinaryData::dancer_20_pngSize);
    dancerFrameCount = (int) dancerFrames.size();
    dancerLastFrame = 0;
    dancerLastDrawnClockCounter = 0;
}

void ClockSyncAudioProcessorEditor::drawDancer(juce::Graphics& g)
{
    if (dancerFrameCount <= 0 || dancerFrames.empty() || ringArea.isEmpty()) return;
    const unsigned long long pulses = processor.getUiClockCounter();
    if (runParamCached && dancerFrameCount > 1)
    {
        // Map clock division (rateIndexCached) to dancer pulses-per-quarter:
        // 32 -> 24 PPQ, 16 -> 12 PPQ, 8 -> 6 PPQ, 4 -> 6 PPQ
        int pulsesInQuarter = 24; // default for 32
        switch (rateIndexCached)
        {
            case 0: pulsesInQuarter = 24; break; // division 32
            case 1: pulsesInQuarter = 12; break; // division 16
            case 2: pulsesInQuarter = 6;  break; // division 8
            case 3: pulsesInQuarter = 6;  break; // division 4
            default: pulsesInQuarter = 24; break;
        }
        // Apply global dancer speed scale (0.25x) by enlarging the effective cycle length.
        // Equivalent to dividing frame advancement by 4.
        const int scaledCycle = pulsesInQuarter * 4; // slower cycle
        const int pInCycle = (int) (pulses % (unsigned long long) scaledCycle);
        int frameIdx = (pInCycle * dancerFrameCount) / scaledCycle;
        frameIdx = juce::jlimit(0, dancerFrameCount - 1, frameIdx);
        dancerLastFrame = frameIdx;
        dancerLastDrawnClockCounter = pulses;
    }
    juce::Drawable* drawable = dancerFrames[(size_t) dancerLastFrame].get();
    if (! drawable) return;
    juce::Rectangle<float> inner((float)(ringArea.getCentreX() - kRingInnerD / 2),
                                 (float)(ringArea.getCentreY() - kRingInnerD / 2),
                                 (float) kRingInnerD, (float) kRingInnerD);
    auto dest = inner.reduced(6.0f);
    const float refW = (float) kDancerRefW;
    const float refH = (float) kDancerRefH;
    const float sx = dest.getWidth() / refW;
    const float sy = dest.getHeight() / refH;
    const float scale = std::min(sx, sy) * 0.92f; // padding
    const float scaledW = refW * scale;
    const float scaledH = refH * scale;
    const float tx = dest.getCentreX() - scaledW * 0.5f;
    const float ty = dest.getCentreY() - scaledH * 0.5f;
    juce::Path clip; clip.addEllipse(dest);
    g.reduceClipRegion(clip);
    drawable->draw(g, 1.0f, juce::AffineTransform::scale(scale).translated(tx, ty));
}

// OverlayTooltip handlers unused.

ClockSyncAudioProcessorEditor::~ClockSyncAudioProcessorEditor()
{
    // Ensure vblank updater is destroyed before member animators go away
    vblankUpdater.reset();
    arrowDown.reset();
    deviceBox.setLookAndFeel(nullptr);
    refreshButton.setLookAndFeel(nullptr);
    nameBox.setLookAndFeel(nullptr);
    nameMidiSwitch.setLookAndFeel(nullptr);
    // Clear help button look-and-feel
    helpToggle.setLookAndFeel(nullptr);
}

// Existing mouseUp earlier (original stub) removed; consolidated into final mouseUp below.

void ClockSyncAudioProcessorEditor::mouseMove(const juce::MouseEvent& e)
{
    juce::Point<float> pf((float) e.x, (float) e.y);
    bool did = false;
    const bool submenuActive = (setupSubmenuTargetOn || setupSubmenuProgress > 0.0f);
    if (submenuActive)
    {
        // Use editor-relative coordinates when testing child bounds, because events may
        // arrive from nested components with their own local coordinate spaces.
        auto ep = e.getEventRelativeTo(this);
        juce::Point<int> posEditor = ep.getPosition();

        int forced = -1;
        int newHover = -1;
        if (idleModeButton.isVisible() && idleModeButton.getBounds().contains(posEditor)) { forced = 9; newHover = 0; }
        else if (legacyModernButton.isVisible() && legacyModernButton.getBounds().contains(posEditor)) { forced = 10; newHover = 1; }
        else if (sppButton.isVisible() && sppButton.getBounds().contains(posEditor)) { forced = 11; newHover = 2; }

        if (newHover != hoveredSetupIndex)
        {
            hoveredSetupIndex = newHover;
            // Repaint the header/submenu region only
            repaint(0, 0, getWidth(), 80);
        }
        if (playgroundComp)
        {
            if (forced >= 0)
            {
                playgroundComp->setForcedHoverIndex(forced);
                // When hovering submenu controls, don't forward circle hover.
                return;
            }
            // Not over submenu control: clear forced label and allow normal circle hover forwarding below.
            playgroundComp->clearForcedHoverIndex();
        }
    }
    else if (playgroundComp)
    {
        playgroundComp->clearForcedHoverIndex();
        playgroundComp->setExternalHoverBlocked(false);
    }
    // If either popup menu (idx3 or idx6) is active (visible or animating),
    // don't override Playground's own hover mapping. This ensures that hovering
    // expanded popup items keeps the correct tooltip (3/6) visible and that
    // per-item hover animations are fed reliably.
    if (playgroundComp)
    {
        const bool popup3Active = playgroundComp->isPopup3Active();
        const bool popup6Active = playgroundComp->isPopup6Active();
        if (popup3Active || popup6Active)
        {
            // Feed popup hover directly so expanded items get proper hoverText even if overlays are on top.
            bool consumedPopupHover = false;
            if (popup6Active && playgroundComp->getCircleCount() > 6)
            {
                float x6,y6,r6; if (! playgroundComp->getCircleInfo(6,x6,y6,r6)) {}
                const int hi6 = playgroundComp->popup6Hit(pf, x6, y6, r6);
                if (hi6 >= 0)
                {
                    playgroundComp->setHoverIndexFromEditor(6);
                    repaint(juce::Rectangle<int>((int) std::round(x6 - 90.0f), (int) std::round(y6 - 90.0f), 180, 180));
                    return; // item hover mapped to circle 6
                }
                // No item: if pointer over base circle, still show 6
                const float dx6 = pf.x - x6, dy6 = pf.y - y6;
                if (dx6*dx6 + dy6*dy6 <= r6 * r6)
                {
                    playgroundComp->setHoverIndexFromEditor(6);
                    repaint(juce::Rectangle<int>((int) std::round(x6 - 60.0f), (int) std::round(y6 - 60.0f), 120, 120));
                    return;
                }
            }
            if (popup3Active && playgroundComp->getCircleCount() > 3)
            {
                float x3,y3,r3; if (! playgroundComp->getCircleInfo(3,x3,y3,r3)) {}
                const int hi3 = playgroundComp->popup3Hit(pf, x3, y3, r3);
                if (hi3 >= 0)
                {
                    playgroundComp->setHoverIndexFromEditor(3);
                    repaint(juce::Rectangle<int>((int) std::round(x3 - 90.0f), (int) std::round(y3 - 90.0f), 180, 180));
                    return; // item hover mapped to circle 3
                }
                const float dx3 = pf.x - x3, dy3 = pf.y - y3;
                if (dx3*dx3 + dy3*dy3 <= r3 * r3)
                {
                    playgroundComp->setHoverIndexFromEditor(3);
                    repaint(juce::Rectangle<int>((int) std::round(x3 - 60.0f), (int) std::round(y3 - 60.0f), 120, 120));
                    return;
                }
            }
            // Not over popup items or base circles: clear editor-driven hover; let generic logic continue below
            playgroundComp->clearHoverFromEditor();
        }
    }
    // Playground provides hover/tooltips; skip deprecated popup-ring hover handling.
    if (! playgroundComp)
    {
        auto checkHoverComp = [&](const std::unique_ptr<PopupMenuRing>& pr, const juce::Component& c) -> int
        {
            if (! pr) return 0;
            auto b = c.getBounds().toFloat();
            const float baseX = b.getCentreX();
            const float baseY = b.getCentreY();
            const float baseR = std::min(b.getWidth(), b.getHeight()) * 0.5f;
            return pr->handleMouseMove(pf, baseX, baseY, baseR);
        };

        
    }
    // Legacy popupRing0 (run-offset selection) was superseded by PlaygroundComponent's
    // internal ring + popup menus. This no-op check is a leftover and has been
    // removed to avoid confusion.

    

    // Improved hover hit-test: prefer the ring donut (idx0) when pointer is inside
    // the annulus, unless the pointer is truly inside a smaller circle by a margin.
    // This avoids the ring edge showing idx6/others just because their disks overlap
    // the donut slightly. Also provide explicit hover for idx7's tiny inner button.
    if (playgroundComp && playgroundComp->getCircleCount() > 0)
    {
        const int pgx = playgroundComp->getX();
        const int pgy = playgroundComp->getY();
        const int n = playgroundComp->getCircleCount();
        int bestIdx = -1;
        float bestR = 1e9f;
        juce::Point<int> pos = e.getPosition(); // (x,y) public members
        // Precompute ring annulus state using the same geometry we paint with
        bool inDonut = false;
        float dx = 0.0f, dy = 0.0f, dist2 = 0.0f, innerR = 0.0f, outerR = 0.0f;
        if (ringArea.contains(pos))
        {
            const int cx = ringArea.getCentreX();
            const int cy = ringArea.getCentreY();
            dx = (float) pos.x - (float) cx;
            dy = (float) pos.y - (float) cy;
            dist2 = dx*dx + dy*dy;
            outerR = (float) kRingOuterD * 0.5f;
            innerR = (float) kRingInnerD * 0.5f;
            inDonut = (dist2 <= outerR*outerR && dist2 >= innerR*innerR);

            // All code using dx, dy, dist2, innerR, outerR must be inside this block
            for (int i = 0; i < n; ++i)
            {
                float cx,cy,cr; if (! playgroundComp->getCircleInfo(i,cx,cy,cr)) continue; const float gx = cx + (float) pgx; const float gy = cy + (float) pgy; const float dx = (float) pos.x - gx; const float dy = (float) pos.y - gy; if (dx*dx + dy*dy <= cr*cr + 0.0001f) { if (cr < bestR) { bestR = cr; bestIdx = i; } }
            }

            bool consumed = false;
            // Special-case idx7 tiny inner button hover so its label shows when pointer is
            // inside the small dot even if overlays block Playground's own mouseMove.
            bool innerBtnHover = false;
            if (playgroundComp->getCircleCount() > 7)
            {
                float cx7,cy7,cr7; if (playgroundComp->getCircleInfo(7,cx7,cy7,cr7))
                {
                    const float smallR = 8.0f; const float dx7 = (float) pos.x - (cx7 + (float) pgx); const float dy7 = (float) pos.y - (cy7 + (float) pgy); innerBtnHover = (dx7*dx7 + dy7*dy7) <= (smallR*smallR);
                }
                playgroundComp->setClickToPulseHoverFromEditor(innerBtnHover);
                if (innerBtnHover)
                {
                    playgroundComp->setHoverIndexFromEditor(7);
                    consumed = true;
                }
            }

            if (!consumed && bestIdx >= 0)
            {
                // Smallest containing circle wins; only fall back to ring when no circle matches.
                playgroundComp->setHoverIndexFromEditor(bestIdx);
                consumed = true;
            }
            else if (inDonut)
            {
                // Ring donut fallback: pointer in annulus (between inner & outer radii) -> idx0 hover.
                playgroundComp->setHoverIndexFromEditor(0);
                consumed = true;
            }

            if (! consumed)
            {
                // Clear circle/ring hover if nothing matched.
                playgroundComp->clearHoverFromEditor();
            }

            // If popup menus are visible, propagate their internal item hover by forcing the parent
            // circle index (3 or 6) so tooltip stays active even when pointer is over expanded items.
            // We only do this when the popup itself reports a hover >= 0. (Requires PopupMenuRing API.)
            // Access underlying rings through playgroundComp public members (popup3/popup6).
            // NOTE: We rely on PopupMenuRing::getHoverIndex(); if not hovered returns -1.
            // Popup item hover mapping handled internally in PlaygroundComponent; avoid forcing here
            // to prevent overriding a smaller circle selection.
        }
    }
}



void ClockSyncAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    // Route clicks to any visible popup rings first.
    juce::Point<float> pf((float) e.x, (float) e.y);
    // If pattern edit is active and user clicks circle idx3 region, close edit mode immediately
    // so subsequent click can open its menu without interference.
    if (patternEditMode && playgroundComp && playgroundComp->getCircleCount() > 3)
    {
        float cx3,cy3,cr3; if (playgroundComp->getCircleInfo(3,cx3,cy3,cr3))
        {
            juce::Rectangle<float> r3(cx3 - cr3, cy3 - cr3, cr3 * 2.0f, cr3 * 2.0f);
            if (r3.contains(pf))
            {
                patternEditMode = false; playgroundComp->setInterceptsMouseClicks(true, true); playgroundComp->setPatternEditButtonState(false); patternHoverIndex = -1; repaint(ringArea); return; }
        }
    }
    auto handlePopupClickAtComp = [&](const std::unique_ptr<PopupMenuRing>& pr, const juce::Component& c)->int
    {
        if (! pr) return -1;
        auto b = c.getBounds().toFloat();
        const float baseX = b.getCentreX();
        const float baseY = b.getCentreY();
        const float baseR = std::min(b.getWidth(), b.getHeight()) * 0.5f;
        return pr->handleMouseDown(pf, baseX, baseY, baseR);
    };

    // Check popups in a reasonable visual order. If any handles the click,
    // apply the corresponding parameter change and collapse the popup.
    // Skip popup-ring click handling when playground active.
    if (! playgroundComp)
    {

        
    }

    // Defer ring clicks until after small circle click handling to avoid accidental
    // idx0 toggles when clicking overlapping small circles (e.g., idx5).

    // If no popup consumed the click, allow clicks on the base components to toggle their popups.
    // This mirrors the playground behaviour: clicking a small circle opens the associated popup.
    // Forward clicks that land on the playground's visual circles (indices 3 and 6)
    // to the playground so its internal popups (`popup3` and `popup6`) can
    // expand/collapse. The playground is rendered underneath native controls,
    // so clicks may be intercepted — forward them explicitly.
    if (playgroundComp)
    {
        bool inAnySmallCircle = false; // track if click is within any small circle disk (excludes idx0)
        // Pattern edit toggle circle (idx2) should still be clickable while in edit mode to exit.
        if (playgroundComp->getCircleCount() > 2)
        {
            float c2x,c2y,c2r; if (playgroundComp->getCircleInfo(2,c2x,c2y,c2r))
            {
                juce::Rectangle<int> r2((int)std::round(c2x - c2r), (int)std::round(c2y - c2r), (int)std::round(c2r * 2.0f), (int)std::round(c2r * 2.0f));
                if (r2.contains(e.getPosition()))
                {
                    bool next = ! patternEditMode;
                    patternEditMode = next;
                    playgroundComp->setInterceptsMouseClicks(!next, !next);
                    playgroundComp->setPatternEditButtonState(next);
                    if (next)
                        if (auto* psi = dynamic_cast<juce::AudioParameterInt*>(processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramPatternSteps)))
                        {
                            uint16_t m = (uint16_t) juce::jlimit(0, 65535, psi->get());
                            if (playgroundComp)
                                playgroundComp->setPatternBitmask(m);
                            else
                                pattern.setBitmask(m);
                        }
                    repaint(ringArea);
                    return;
                }
                else if (r2.contains(e.getPosition())) inAnySmallCircle = true;
            }
        }
        // Popup3 (idx3)
        if (playgroundComp->getCircleCount() > 3)
        {
            float c3x,c3y,c3r; if (playgroundComp->getCircleInfo(3,c3x,c3y,c3r))
            {
                juce::Rectangle<int> r3((int)std::round(c3x - c3r), (int)std::round(c3y - c3r), (int)std::round(c3r * 2.0f), (int)std::round(c3r * 2.0f));
                if (r3.contains(e.getPosition())) { playgroundComp->togglePopup3(); repaint(); return; }
                else if (r3.contains(e.getPosition())) inAnySmallCircle = true;
            }
        }
        // Trigger circle (idx1)
        if (playgroundComp->getCircleCount() > 1)
        {
            float c1x,c1y,c1r; if (playgroundComp->getCircleInfo(1,c1x,c1y,c1r))
            {
                juce::Rectangle<int> r1((int)std::round(c1x - c1r), (int)std::round(c1y - c1r), (int)std::round(c1r * 2.0f), (int)std::round(c1r * 2.0f));
                if (r1.contains(e.getPosition()))
                {
                    if (patternEditMode)
                    {
                        int logicalStep = juce::jlimit(1,16, relativeStepCached);
                        constexpr int visualRotation = 4;
                        int storedIndex = ((logicalStep - 1) + visualRotation) & 15;
                        uint16_t mask = playgroundComp ? playgroundComp->getPatternBitmask() : pattern.getBitmask();
                        if ((mask & (1u << storedIndex)) == 0)
                        {
                            mask |= (uint16_t)(1u << storedIndex);
                            if (playgroundComp) playgroundComp->setPatternBitmask(mask);
                            else pattern.setStep(storedIndex, true);
                            lastClickedStoredIndex = storedIndex;
                            pushPatternStateToProcessor();
                            repaint(ringArea);
                        }
                        playgroundComp->flashSegmentLogical(logicalStep);
                    }
                    processor.requestTriggerOnce();
                    (void) playgroundComp->handleExternalClickIndex(1);
                    manualTriggerOffsetActive = true;
                    manualTriggerRelativeStepAtTrigger = juce::jlimit(1,16, relativeStepCached);
                    if (selectedResyncStepCached > 0)
                        manualTriggerPlayheadDelta = (selectedResyncStepCached - manualTriggerRelativeStepAtTrigger + 16) % 16;
                    else
                        manualTriggerPlayheadDelta = 0;
                    playgroundComp->flashSegmentLogical(manualTriggerRelativeStepAtTrigger);
                    repaint();
                    return;
                }
                else if (r1.contains(e.getPosition())) inAnySmallCircle = true;
            }
        }
        // Popup6 (idx6) guarded against ring wedge clicks
        if (playgroundComp->getCircleCount() > 6)
        {
            const float outerRGuard = (float) kRingOuterD * 0.5f;
            const float innerRGuard = (float) kRingInnerD * 0.5f;
            const int rcx = ringArea.getCentreX();
            const int rcy = ringArea.getCentreY();
            const float gdx = (float) e.x - (float) rcx;
            const float gdy = (float) e.y - (float) rcy;
            const float gd2 = gdx*gdx + gdy*gdy;
            // Only toggle Run when pointer is well inside the centre (apply margin so near-boundary clicks don't toggle).
            // Reduce run-toggle radius while in pattern edit mode so wedge clicks are not misinterpreted.
            const float innerToggleR = patternEditMode ? (innerRGuard * 0.60f) : (innerRGuard * 0.82f);
            if (gd2 <= innerToggleR * innerToggleR)
            {
                if (patternEditMode) return; // Block run toggle while editing pattern
                if (auto* p = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramRun))
                {
                    if (auto* rp = processor.getAPVTS().getRawParameterValue(ClockSyncAudioProcessor::paramRun))
                    {
                        const bool current = rp->load() > 0.5f;
                        const bool next = ! current;
                        p->beginChangeGesture();
                        p->setValueNotifyingHost(next ? 1.0f : 0.0f);
                        p->endChangeGesture();
                        runParamCached = next;
                        ledPulseTarget = next ? 1.0f : 0.6f;
                        ledAnimator.start();
                        repaint(ringArea);
                    }
                }
                return;
            }
            if (gd2 <= outerRGuard * outerRGuard && gd2 >= innerRGuard * innerRGuard)
            {
                if (patternEditMode)
                {
                    // Fallback wedge activation: if edit mode and wedge hitTest failed earlier but pointer lies
                    // within pattern ring band, map to wedge index and toggle.
                    float innerHoleR = (float) kRingInnerD * 0.5f;
                    float patternOuterR = innerHoleR - 8.0f;
                    float patternInnerR = patternOuterR * 0.775f;
                    const float d = std::sqrt(gd2);
                        if (d >= patternInnerR && d <= patternOuterR)
                    {
                        const float angle = std::atan2(gdy, gdx); // -pi..pi
                        const float startAt12 = -juce::MathConstants<float>::halfPi;
                        float rel = angle - startAt12;
                        while (rel < 0.0f) rel += juce::MathConstants<float>::twoPi;
                        const float slice = juce::MathConstants<float>::twoPi / 16.0f;
                        int rawIdx = (int) std::floor(rel / slice);
                        const int offset = 4;
                        int storedIdx = (rawIdx + offset) & 15;
                        if (storedIdx >= 0 && storedIdx < 16)
                        {
                            uint16_t mask = playgroundComp ? playgroundComp->getPatternBitmask() : pattern.getBitmask();
                            mask ^= (uint16_t)(1u << storedIdx);
                            if (playgroundComp) playgroundComp->setPatternBitmask(mask);
                            else pattern.toggleStep(storedIdx);
                            lastClickedStoredIndex = storedIdx;
                            pushPatternStateToProcessor();
                            repaint(ringArea);
                        }
                    }
                    return; // swallow during edit mode
                }
                const float angle = std::atan2(gdy, gdx); // -pi..pi
                const float startAt12 = -juce::MathConstants<float>::halfPi;
                const float twoPi = juce::MathConstants<float>::twoPi;
                float rel = angle - startAt12;
                while (rel < 0.0f) rel += twoPi;
                const float slice = twoPi / 16.0f;
                int rawIdx = (int) std::floor(rel / slice); // 0..15
                // Use the raw angular sector as the logical step (0..15) so
                // clicking the top wedge maps to step 1. The PlaygroundComponent
                // already applies the visual rotation when rendering and when
                // scheduling pattern steps, so we should treat the ring sector
                // index as the canonical logical index here.
                int logical0 = rawIdx;
                int step = logical0 + 1; // 1..16
                if (auto* p = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramResyncOffsetStep))
                {
                    const auto& range = p->getNormalisableRange();
                    p->beginChangeGesture();
                    p->setValueNotifyingHost(range.convertTo0to1((float) juce::jlimit(1, 16, step)));
                    p->endChangeGesture();
                }
                processor.notifyResyncOffsetChanged();
                selectedResyncStepCached = step;
                repaint(ringArea.expanded(120, 120));
                return;
            }
        }
    }
    
}

void ClockSyncAudioProcessorEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (! patternEditMode || ! patternDragActive) return;
    if (! ringArea.contains(e.getPosition())) return;
    float innerHoleR = (float) kRingInnerD * 0.5f;
    float patternOuterR = innerHoleR - 8.0f;
    float patternInnerR = patternOuterR * 0.775f;
    juce::Point<float> centre((float) ringArea.getCentreX(), (float) ringArea.getCentreY());
    int pw = pattern.hitTest(e.position.toFloat(), centre, patternOuterR, patternInnerR);
    if (pw >= 0 && ! patternDragTouched[pw])
    {
        patternDragTouched[pw] = true;
        if (playgroundComp)
        {
            uint16_t mask = playgroundComp->getPatternBitmask();
            if (patternDragSetState)
                mask |= (uint16_t)(1u << pw);
            else
                mask &= (uint16_t)~(1u << pw);
            playgroundComp->setPatternBitmask(mask);
        }
        else
        {
            pattern.setStep(pw, patternDragSetState);
        }
        lastClickedStoredIndex = pw;
        pushPatternStateToProcessor();
        repaint(ringArea);
    }
}

void ClockSyncAudioProcessorEditor::mouseUp(const juce::MouseEvent& e)
{
    if (patternDragActive)
    {
        patternDragActive = false;
        patternDragSetState = false;
        std::fill(std::begin(patternDragTouched), std::end(patternDragTouched), false);
    }
}

// Map popup3 selection index -> patternBars parameter index (OFF,1,2,4,8,16,32,64,RND)
void ClockSyncAudioProcessorEditor::updatePatternParamFromPopup3(int popupIndex)
{
    // popup order: RND(0),64(1),32(2),16(3),8(4),4(5),2(6),1(7),OFF(8)
    static const int map[9] = { 8,7,6,5,4,3,2,1,0 };
    if (popupIndex < 0 || popupIndex > 8) return;
    int paramIdx = map[popupIndex];
    if (auto* p = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramPatternBars))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1((float) paramIdx));
        p->endChangeGesture();
        // Persist the popup3 index (visual) so it can be restored even if the playground
        // visual state isn't created before parameters are applied by the host.
        int popupIndex = 8 - paramIdx;
        processor.getAPVTS().state.setProperty("ui.popup3Index", popupIndex, nullptr);
    }
}

void ClockSyncAudioProcessorEditor::pushPatternStateToProcessor()
{
    uint16_t mask = pattern.getBitmask();
    if (playgroundComp)
        mask = playgroundComp->getPatternBitmask();
    if (auto* p = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramPatternSteps))
    {
        float norm = p->getNormalisableRange().convertTo0to1((float) mask);
        p->setValueNotifyingHost(norm);
    }
}

// PlaygroundComponent ring & animations are authoritative.

void ClockSyncAudioProcessorEditor::refreshDeviceList()
{
    midiOutputs.clear();
    auto arr = juce::MidiOutput::getAvailableDevices();
    for (auto& d : arr) midiOutputs.push_back(d);
    deviceBox.clear(juce::dontSendNotification);
    int idx = 1;
    // First item acts as a placeholder; selection id 1 corresponds to 'no external port selected'.
    deviceBox.addItem("select midi-out...", idx++);
    int selectionToSet = 1;
    const auto currentId = processor.getExternalDeviceId();
    for (const auto& d : midiOutputs)
    {
        deviceBox.addItem(d.name, idx);
        if (currentId.isNotEmpty() && d.identifier == currentId)
            selectionToSet = idx;
        ++idx;
    }
    deviceBox.setSelectedId(selectionToSet, juce::dontSendNotification);
}

// Instrument name persistence: stored in APVTS state under property "instrumentNames"
void ClockSyncAudioProcessorEditor::loadInstrumentNamesFromState()
{
    instrumentNames.clear();
    auto& st = processor.getAPVTS().state;
    juce::var v = st.getProperty("instrumentNames", juce::var());
    if (v.isString())
    {
        juce::String s = v.toString();
        instrumentNames.addLines(s);
        instrumentNames.removeEmptyStrings(true);
    }
}

// Restored instrument name persistence helpers.
void ClockSyncAudioProcessorEditor::saveInstrumentNamesToState()
{
    juce::String s = instrumentNames.joinIntoString("\n");
    processor.getAPVTS().state.setProperty("instrumentNames", s, nullptr);
}

void ClockSyncAudioProcessorEditor::populateNameBox()
{
    nameBox.clear(juce::dontSendNotification);
    int id = 1;
    for (auto& n : instrumentNames)
        nameBox.addItem(n, id++);
    // separator and actions
    nameBox.addSeparator();
    nameBox.addItem("name...", 1000);
    nameBox.addItem("clear all", 1001);
    // preserve selection if possible; when there are no names show placeholder text
    if (instrumentNames.size() > 0)
    {
        nameBox.setSelectedId(1, juce::dontSendNotification);
        nameBox.setTextWhenNothingSelected(juce::String());
    }
    else
    {
        // Show actionable hint when empty
        nameBox.setSelectedId(0, juce::dontSendNotification);
        nameBox.setTextWhenNothingSelected("name...");
    }
    // Ensure the displayed text uses the cyan accent via overlay colour and is centred
    nameBox.setColour(FullWidthComboBox::overlayTextColourId, UiThemeColours::cyan());
    nameBox.setJustificationType(juce::Justification::centred);
    nameBox.toFront(true);
    // Ensure any internal editor uses the accent cyan, but always hide any
    // internal Label children so `FullWidthComboBox::paint()` is the single
    // source of visible text (prevents doubled rendering).
    for (auto* c : nameBox.getChildren())
    {
        if (auto* te = dynamic_cast<juce::TextEditor*>(c))
        {
            te->setColour(juce::TextEditor::textColourId, UiThemeColours::cyan());
            te->setBounds(nameBox.getLocalBounds().reduced(6, 0));
        }
        else if (auto* lb = dynamic_cast<juce::Label*>(c))
        {
            lb->setVisible(false);
            lb->toBack();
            lb->setJustificationType(juce::Justification::centred);
        }
    }
    // Additionally hide any sibling Label components that overlap the nameBox
    // — some hosts or look-and-feel implementations may create separate labels
    // that draw the selected text; hide them to avoid doubled rendering.
    if (auto* p = nameBox.getParentComponent())
    {
        for (auto* sc : p->getChildren())
        {
            if (sc == &nameBox) continue;
            if (auto* lb = dynamic_cast<juce::Label*>(sc))
            {
                if (lb->isVisible() && lb->getBounds().intersects(nameBox.getBounds()))
                {
                    lb->setVisible(false);
                    lb->toBack();
                }
            }
        }
    }
}

void ClockSyncAudioProcessorEditor::showNewNameDialog()
{
    if (nameEntryEditor) return;
    // open an inline empty editor for new name (commit on Enter, cancel on blur)
    auto rc = nameBox.getBounds();
    const int editorH = rc.getHeight();
    const int editorW = std::max(160, rc.getWidth());
    const int x = rc.getX();
    // Position editor over the combobox (same Y) so it appears in-place
    const int y = rc.getY();

    nameEntryEditor = std::make_unique<juce::TextEditor>();
    nameEntryEditor->setText(juce::String());
    nameEntryEditor->setFont(juce::Font(juce::FontOptions("Arial", 14.0f, juce::Font::plain)));
    // Parent the inline editor to the top-level window so it appears above
    // other UI (including host-drawn overlays). Convert nameBox coords.
    nameEntryEditor->setColour(juce::TextEditor::textColourId, UiThemeColours::cyan());
    if (auto* top = getTopLevelComponent())
    {
        auto topLeftInTop = top->getLocalPoint(this, nameBox.getBounds().getTopLeft());
        juce::Rectangle<int> editorBounds(topLeftInTop.x + 6, topLeftInTop.y, std::max(160, nameBox.getWidth()) - 12, nameBox.getHeight());
        top->addAndMakeVisible(*nameEntryEditor);
        nameEntryEditor->setBounds(editorBounds);
    }
    else
    {
        addAndMakeVisible(*nameEntryEditor);
        nameEntryEditor->setBounds(nameBox.getBounds().reduced(6, 0));
    }
    nameEntryEditor->grabKeyboardFocus();
    nameEntryEditor->toFront(true);

    nameEntryEditor->onReturnKey = [this]()
    {
        if (! nameEntryEditor) return;
        juce::String name = nameEntryEditor->getText().trim();
        if (name.isNotEmpty())
        {
            if (! instrumentNames.contains(name))
                instrumentNames.add(name);
            saveInstrumentNamesToState();
            populateNameBox();
            nameBox.setSelectedId(instrumentNames.indexOf(name) + 1, juce::dontSendNotification);
        }
        nameEntryEditor.reset();
        repaint();
    };
    nameEntryEditor->onEscapeKey = [this]() { nameEntryEditor.reset(); repaint(); };
    nameEntryEditor->onFocusLost = [this]() { nameEntryEditor.reset(); repaint(); };
}

void ClockSyncAudioProcessorEditor::toggleNameEditorOrCommit()
{
    if (nameEntryEditor)
    {
        // commit
        juce::String name = nameEntryEditor->getText().trim();
        if (name.isNotEmpty())
        {
            if (! instrumentNames.contains(name))
                instrumentNames.add(name);
            saveInstrumentNamesToState();
            populateNameBox();
            nameBox.setSelectedId(instrumentNames.indexOf(name) + 1, juce::dontSendNotification);
        }
        nameEntryEditor.reset();
        repaint();
        return;
    }

    // open editor for current selection
    const int sel = nameBox.getSelectedId();
    juce::String cur;
    if (sel >= 1 && sel <= instrumentNames.size()) cur = instrumentNames[(size_t) (sel - 1)];
    auto rc = nameBox.getBounds();
    const int editorH = rc.getHeight();
    const int editorW = std::max(160, rc.getWidth());
    const int x = rc.getX();
    const int y = rc.getY();

    nameEntryEditor = std::make_unique<juce::TextEditor>();
    nameEntryEditor->setText(cur);
    nameEntryEditor->setFont(juce::Font(juce::FontOptions("Arial", 14.0f, juce::Font::plain)));
    // Parent the inline editor to the top-level window so it appears above
    // other UI (including host-drawn overlays). Convert nameBox coords.
    nameEntryEditor->setColour(juce::TextEditor::textColourId, UiThemeColours::cyan());
    if (auto* top = getTopLevelComponent())
    {
        auto topLeftInTop = top->getLocalPoint(this, nameBox.getBounds().getTopLeft());
        juce::Rectangle<int> editorBounds(topLeftInTop.x + 6, topLeftInTop.y, std::max(160, nameBox.getWidth()) - 12, nameBox.getHeight());
        top->addAndMakeVisible(*nameEntryEditor);
        nameEntryEditor->setBounds(editorBounds);
    }
    else
    {
        addAndMakeVisible(*nameEntryEditor);
        nameEntryEditor->setBounds(nameBox.getBounds().reduced(6, 0));
    }
    nameEntryEditor->grabKeyboardFocus();
    nameEntryEditor->toFront(true);

    // commit on Enter; cancel on focus lost or Escape
    nameEntryEditor->onReturnKey = [this]()
    {
        if (! nameEntryEditor) return;
        juce::String name = nameEntryEditor->getText().trim();
        if (name.isNotEmpty())
        {
            if (! instrumentNames.contains(name))
                instrumentNames.add(name);
            saveInstrumentNamesToState();
            populateNameBox();
            nameBox.setSelectedId(instrumentNames.indexOf(name) + 1, juce::dontSendNotification);
        }
        nameEntryEditor.reset();
        repaint();
    };
    nameEntryEditor->onEscapeKey = [this]() { nameEntryEditor.reset(); repaint(); };
    nameEntryEditor->onFocusLost = [this]() { nameEntryEditor.reset(); repaint(); };
}

// Dancer frame loading no longer used.


