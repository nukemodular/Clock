#include <utility>
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "build_info.h"
#include "BinaryData.h"
#include "LookAndFeels.h" // theme colours
#include "UiLayoutConstants.h"
#include <array>
#include <optional>
#include "PopupMenuRing.h"

namespace
{
    // Accent background variant
    const juce::Colour kBaseLo  = UiThemeColours::base().darker(0.12f);
}

// (internal namespace only holds accent colour variant)


ClockSyncAudioProcessorEditor::ClockSyncAudioProcessorEditor(ClockSyncAudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processor(p)
{
    setResizable(false, false);
    setSize(300, 240);
    startTimerHz(60);

    // Component setup
    {
        idleClockToggle.setColours(UiThemeColours::accent(), UiThemeColours::base(), UiThemeColours::cyan());
        clickButton.setColours(UiThemeColours::accent(), UiThemeColours::cyan());
        
    



    themeLNF = std::make_unique<ThemeLNF>();

    // Assign theme LNF
    nameBox.setLookAndFeel(themeLNF.get());
    nameMidiSwitch.setLookAndFeel(themeLNF.get());
    setupButton.setLookAndFeel(themeLNF.get());
    idleModeButton.setLookAndFeel(themeLNF.get());
    legacyModernButton.setLookAndFeel(themeLNF.get());
    sppButton.setLookAndFeel(themeLNF.get());
    // Enable toggle state
    nameMidiSwitch.setClickingTogglesState(true);
    setupButton.setClickingTogglesState(true);
    idleModeButton.setClickingTogglesState(true);
    legacyModernButton.setClickingTogglesState(true);
    sppButton.setClickingTogglesState(true);
    // Dynamic colouring per label
    

    // APVTS alias
    auto& apvts = processor.getAPVTS();

    if (auto* rp = apvts.getRawParameterValue(ClockSyncAudioProcessor::paramRun))
        runParamCached = rp->load() > 0.5f;
    rateParam = dynamic_cast<juce::AudioParameterChoice*>(
        apvts.getParameter(ClockSyncAudioProcessor::paramClockRateIndex));
    if (rateParam != nullptr)
        rateIndexCached = rateParam->getIndex();

    auto setRateIndex = [this](int idx)
    {
        if (auto* p = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramClockRateIndex))
            p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1((float) idx));
    };

    refreshButton.setLookAndFeel(themeLNF.get());
    clickButton.setColours(UiThemeColours::accent(), UiThemeColours::cyan());
    // Rate now via playground idx6
    
    // Drop shadows helper
    auto addDropShadowTo = [](juce::Component& c, int radius = 12, juce::Colour col = juce::Colours::black.withAlpha(0.4f), int xOff = 4, int yOff = 4)
    {
        auto eff = std::make_unique<juce::DropShadowEffect>();
        juce::DropShadow ds(col, radius, juce::Point<int>(xOff, yOff));
        eff->setShadowProperties(ds);
        // Component takes ownership of the raw pointer
        c.setComponentEffect(eff.release());
    };
    
    addDropShadowTo(idleClockToggle, 8, juce::Colours::black.withAlpha(0.4f));
    addDropShadowTo(helpToggle, 4, juce::Colours::black.withAlpha(0.4f));
    addDropShadowTo(clickButton, 4, juce::Colours::black.withAlpha(0.4f));
    

    // Popup ring (only 0 retained)

    popupRing0 = std::make_unique<PopupMenuRing>();
    popupRing0->setValues({1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16});
    popupRing0->setLabels({"1","2","3","4","5","6","7","8","9","10","11","12","13","14","15","16"});
    popupRing0->setAngles(-juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi * 1.5f);
    popupRing0->setCircleRadius(16.0f);
    popupRing0->setExpansionRadius(62.0f);
    

    // Create playground component
    playgroundComp = std::make_unique<PlaygroundComponent>();
    addAndMakeVisible(*playgroundComp);
    // Full-canvas playground
    playgroundComp->setBounds(0, 0, getWidth(), getHeight());
    playgroundComp->setHeaderHeight(30);
    playgroundComp->toBack();

    // Header widgets on top
    addAndMakeVisible(refreshButton);
    addAndMakeVisible(setupButton);
    addAndMakeVisible(idleModeButton);
    addAndMakeVisible(legacyModernButton);
    addAndMakeVisible(sppButton);
    addAndMakeVisible(deviceBox);
    addAndMakeVisible(nameBox);
    addAndMakeVisible(nameMidiSwitch);
    refreshButton.toFront(true);
    deviceBox.toFront(true);
    nameBox.toFront(true);
    nameMidiSwitch.toFront(true);
    
    clickButton.setVisible(false);
    // Keep other small toggles non-intercepting so they don't block playground hovers
    

    // --- Wire PlaygroundComponent callbacks to APVTS so user interactions
    // in the playground update plugin parameters (editor owns the APVTS).
    if (playgroundComp)
    {
        // Resync step selected (1..16)
        playgroundComp->onResyncStepRequested = [this](int step)
        {
            if (auto* p = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramResyncOffsetStep))
            {
                const auto& range = p->getNormalisableRange();
                p->beginChangeGesture();
                p->setValueNotifyingHost(range.convertTo0to1((float) juce::jlimit(1, 16, step)));
                p->endChangeGesture();
            }
            processor.notifyResyncOffsetChanged();
        };

        // Toggle Run (play/stop)
        playgroundComp->onRunToggleRequested = [this](bool on)
        {
            if (auto* p = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramRun))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost(on ? 1.0f : 0.0f);
                p->endChangeGesture();
            }
        };

        // One-shot trigger
        playgroundComp->onTriggerOnceRequested = [this]()
        {
            // Stronger LED pulse to acknowledge manual trigger (playground animates idx1)
            ledPulseTarget = 1.0f;
            ledAnimator.start();
            // Defer restart to processor (quantized 1/16; bar+offset if trigger mode enabled)
            processor.requestTriggerOnce();
            // Manual trigger chase-light offset: apply visual offset immediately.
            // Compute delta from current relative step to selected offset step.
            manualTriggerOffsetActive = true;
            manualTriggerRelativeStepAtTrigger = juce::jlimit(1,16, relativeStepCached);
            if (selectedResyncStepCached > 0)
            {
                manualTriggerPlayheadDelta = (selectedResyncStepCached - manualTriggerRelativeStepAtTrigger + 16) % 16; // 0..15
            }
            else
            {
                manualTriggerPlayheadDelta = 0;
            }
            // Flash the segment corresponding to the current playhead step at trigger time (not the offset preview target)
            if (playgroundComp)
            {
                playgroundComp->flashSegmentLogical(manualTriggerRelativeStepAtTrigger);
            }
            repaint();
        };

        // Clock rate requested (playground sends division value: 4/8/16/32)
        playgroundComp->onClockRateIndexRequested = [this](int val)
        {
            // Map value -> index 0..3 and mimic GridScaleMenu's onGridChanged by
            // setting the choice value. Processor will defer applying until next
            // bar+offset boundary and show NEXT while pending.
            int idx = 1; // default -> 16
            if (val == 32) idx = 0;
            else if (val == 16) idx = 1;
            else if (val == 8)  idx = 2;
            else if (val == 4)  idx = 3;
            // Use the same helper used by GridScaleMenu wiring
            if (auto* p = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramClockRateIndex))
                p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1((float) idx));
        };

        // Shuffle amount (1..7)
        playgroundComp->onShuffleStepRequested = [this](int v)
        {
            if (auto* p = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramShuffleStep))
            {
                const auto& range = p->getNormalisableRange();
                p->beginChangeGesture();
                p->setValueNotifyingHost(range.convertTo0to1((float) juce::jlimit(1, 7, v)));
                p->endChangeGesture();
            }
        };

        // Clock while stopped (boolean)
        playgroundComp->onClockWhileStoppedRequested = [this](bool on)
        {
            if (auto* p = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramClockWhileStopped))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost(on ? 1.0f : 0.0f);
                p->endChangeGesture();
            }
        };

        // Click rate (0..4) and click pulse variant (bool)
        playgroundComp->onClickRateRequested = [this](int v)
        {
            if (auto* p = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramClickRate))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1((float) juce::jlimit(0, 4, v)));
                p->endChangeGesture();
            }
        };
        playgroundComp->onClickPulseRequested = [this](bool on)
        {
            if (auto* p = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramClickPulse))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost(on ? 1.0f : 0.0f);
                p->endChangeGesture();
            }
        };

        // Trigger mode toggle (idx 8)
        playgroundComp->onTriggerModeRequested = [this](bool on)
        {
            if (auto* p = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramTriggerModeEnabled))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost(on ? 1.0f : 0.0f);
                p->endChangeGesture();
            }
            // Do not force a restart when toggling mode; processor uses this only to decide
            // whether manual triggers schedule a bar+offset restart.
        };

        // Popup3 selection: not mapped to an APVTS param yet — leave as a no-op
        playgroundComp->onPopup3Selected = [this](int){ /* no-op for now */ };

        // Initialise playground visual state from current parameters
        playgroundComp->setRunState(runParamCached);
        if (auto* pi = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter(ClockSyncAudioProcessor::paramResyncOffsetStep)))
            playgroundComp->setResyncStepSelected(pi->get());
        // Map rate index -> displayed division value
        int displayed = 16;
        switch (rateIndexCached)
        {
            case 0: displayed = 32; break;
            case 1: displayed = 16; break;
            case 2: displayed = 8;  break;
            case 3: displayed = 4;  break;
            default: displayed = 16; break;
        }
        playgroundComp->setClockRateIndexValue(displayed);
        if (auto* cp = apvts.getRawParameterValue(ClockSyncAudioProcessor::paramClickPulse))
            playgroundComp->setClickPulseState(cp->load() > 0.5f);
        if (auto* tm = apvts.getRawParameterValue(ClockSyncAudioProcessor::paramTriggerModeEnabled))
            playgroundComp->setTriggerModeState(tm->load() > 0.5f);
    }

    // Shuffle step handled via playground idx4.

    refreshButton.onClick = [this]{ refreshDeviceList(); };
    deviceBox.onChange = [this]
    {
        const int selId = deviceBox.getSelectedId();
        if (selId == 1) processor.setExternalDeviceId({});
        else if (selId > 1 && selId - 2 < (int) midiOutputs.size())
            processor.setExternalDeviceId(midiOutputs[(size_t) (selId - 2)].identifier);
    };
    deviceBox.setLookAndFeel(themeLNF.get());
    deviceBox.setJustificationType(juce::Justification::centred);
    deviceBox.setColour(juce::ComboBox::arrowColourId, juce::Colours::transparentBlack);
    deviceBox.setEditableText(false);
    deviceBox.setColour(juce::ComboBox::textColourId, UiThemeColours::accent());
    // Provide explicit placeholder when no selection is made.
    deviceBox.setTextWhenNothingSelected("select midi-out...");
    // Name combo: centred cyan text, hide arrow to center the label text like deviceBox
    nameBox.setJustificationType(juce::Justification::centred);
    nameBox.setEditableText(false);
    nameBox.setColour(juce::ComboBox::arrowColourId, juce::Colours::transparentBlack);
    nameBox.setColour(juce::ComboBox::textColourId, UiThemeColours::cyan());
    nameBox.setVisible(nameMidiSwitch.getToggleState());
    deviceBox.setVisible(!nameMidiSwitch.getToggleState());
    // Show S in NAME mode and R in MIDI mode
    setupButton.setVisible(nameMidiSwitch.getToggleState());
    idleModeButton.setVisible(false);
    legacyModernButton.setVisible(false);
    sppButton.setVisible(false);
    refreshButton.setVisible(!nameMidiSwitch.getToggleState());
    // Ensure appearance of NAME/MIDI toggle reflects current mode
    {
        const bool showName = nameMidiSwitch.getToggleState();
        nameMidiSwitch.setButtonText(showName ? "MIDI" : "NAME");
        auto txtCol = (showName ? UiThemeColours::accent() : UiThemeColours::cyan());
        nameMidiSwitch.setColour(juce::TextButton::textColourOffId, txtCol);
        nameMidiSwitch.setColour(juce::TextButton::textColourOnId,  txtCol);
    }
    if (nameMidiSwitch.getToggleState())
    {
        loadInstrumentNamesFromState();
        populateNameBox();
    }
    nameMidiSwitch.onClick = [this]
    {
        const bool showName = nameMidiSwitch.getToggleState();
        if (showName) nameMidiSwitch.setButtonText("MIDI"); else nameMidiSwitch.setButtonText("NAME");
        // Colour depends on label: NAME -> cyan, MIDI -> accent
        {
            auto txtCol = (showName ? UiThemeColours::accent() : UiThemeColours::cyan());
            nameMidiSwitch.setColour(juce::TextButton::textColourOffId, txtCol);
            nameMidiSwitch.setColour(juce::TextButton::textColourOnId,  txtCol);
        }
        nameBox.setVisible(showName);
        deviceBox.setVisible(!showName);
        setupButton.setVisible(showName);
        refreshButton.setVisible(!showName);
        if (showName)
        {
            // Load & populate instrument names each time NAME mode becomes active
            loadInstrumentNamesFromState();
            populateNameBox();
            nameBox.toFront(true);
            deviceBox.toBack();
        }
        else
        {
            deviceBox.toFront(true);
            nameBox.toBack();
            // Leaving NAME mode: collapse setup if active
            if (setupButton.getToggleState())
                setupButton.setToggleState(false, juce::dontSendNotification);
            // Force submenu hidden state (avoid stale targetOn causing reappear on return)
            setupSubmenuTargetOn = false;
            setupSubmenuProgress = 0.0f;
            setupSubmenuAnimatingHide = true;
        }
        // Re-layout. Keep PlaygroundComponent header height fixed at 30 so controls don't shift.
        const int headerH = 30 ;//+ ((showName && setupButton.getToggleState()) ? 30 : 0);
        if (playgroundComp) playgroundComp->setHeaderHeight(30);
        resized();
        repaint(0,0,getWidth(), headerH + 4);
    };
    // Setup button toggles extra header area (visible only in NAME mode)
    // Animated setup submenu: we slide a 50px rectangle containing the setup buttons below the header (fixed header height 30).
    // Submenu animates between hidden (top=-25) and shown (top=25). We track progress 0..1 with an Animator.
    // Setup submenu animation initialisation
    setupSubmenuProgress = 0.0f; // hidden
    hoveredSetupIndex = -1;
    setupAnimator = std::make_unique<juce::Animator>(juce::ValueAnimatorBuilder{}
        .withDurationMs(166.0f)
        .withValueChangedCallback([this](float progress){
            const float p = juce::jlimit(0.0f, 1.0f, progress);
            // When hiding, invert progress so animation plays reverse without needing direction API
            setupSubmenuProgress = setupSubmenuAnimatingHide ? (1.0f - p) : p;
            updateSetupSubmenuLayout();
            // Update interactivity of submenu buttons & block playground hover while active
            const bool active = nameMidiSwitch.getToggleState() && (setupSubmenuTargetOn || setupSubmenuProgress > 0.0f);
            idleModeButton.setVisible(active);
            legacyModernButton.setVisible(active);
            sppButton.setVisible(active);
            idleModeButton.setInterceptsMouseClicks(active, active);
            legacyModernButton.setInterceptsMouseClicks(active, active);
            sppButton.setInterceptsMouseClicks(active, active);
            if (playgroundComp)
            {
                playgroundComp->setExternalHoverBlocked(active);
                if (! active) playgroundComp->clearForcedHoverIndex();
            }
            repaint(0,0,getWidth(), 80);
        })
        .build());
    if (! vblankUpdater)
    {
        vblankUpdater = std::make_unique<juce::VBlankAnimatorUpdater>(this);
    }
    vblankUpdater->addAnimator(*setupAnimator);
    setupButton.onClick = [this]
    {
        const bool showName = nameMidiSwitch.getToggleState();
        if (! showName) { // If leaving NAME mode, force hidden state (no animator stop API)
            setupSubmenuTargetOn = false; setupSubmenuAnimatingHide = true; setupSubmenuProgress = 0.0f; updateSetupSubmenuLayout(); repaint(); return; }
        setupSubmenuTargetOn = setupButton.getToggleState();
        setupSubmenuAnimatingHide = ! setupSubmenuTargetOn;
        setupAnimator->start();
    };
    // Placeholder toggles: change text + colour when toggled (no backend wiring yet)
    auto configureSetupToggle = [](juce::TextButton& b){
        b.setColour(juce::TextButton::textColourOffId, UiThemeColours::cyan());
        b.setColour(juce::TextButton::textColourOnId, UiThemeColours::cyan());
    };
    configureSetupToggle(idleModeButton);
    configureSetupToggle(legacyModernButton);
    configureSetupToggle(sppButton);
    // Initially keep buttons visible (they slide under header when hidden)
    idleModeButton.setVisible(true);
    legacyModernButton.setVisible(true);
    sppButton.setVisible(true);
    // Initialise processor legacy mode from current toggle state
    processor.setLegacyMode(!legacyModernButton.getToggleState());
    legacyModernButton.setButtonText(legacyModernButton.getToggleState() ? "MODERN" : "LEGACY");
    idleModeButton.onClick = [this]
    {
        const bool on = idleModeButton.getToggleState();
        idleModeButton.setButtonText(on ? "IDLE ON" : "IDLE OFF");
    };
    legacyModernButton.onClick = [this]
    {
        const bool on = legacyModernButton.getToggleState();
        legacyModernButton.setButtonText(on ? "MODERN" : "LEGACY");
            // Update processor legacy gating mode: true => MODERN (continuous clock), false => LEGACY (emit Stop pre-Start & gate clocks)
            processor.setLegacyMode(!on);
    };
    sppButton.onClick = [this]
    {
        const bool on = sppButton.getToggleState();
        sppButton.setButtonText(on ? "S.P.P. ON" : "S.P.P. OFF");
    };
        nameBox.onChange = [this]
    {
        const int id = nameBox.getSelectedId();
        if (id == 1000) // new...
            showNewNameDialog();
        else if (id == 1001) // clear all
        {
            instrumentNames.clear();
            saveInstrumentNamesToState();
            populateNameBox();
        }
    };

    // Trigger mode toggle handled by playground idx8.

    // Populate device list immediately to recall last-used MIDI port.
    refreshDeviceList();

    

    // Drive visual animators from a VBlank-synchronised updater so animations
    // are in-step with the display refresh rate. Keep the 60Hz Timer for
    // non-animation tasks (host polling, parameter sync, tooltip timing, etc.).
    if (! vblankUpdater)
        vblankUpdater = std::make_unique<juce::VBlankAnimatorUpdater>(this);
    // Add LED animator to existing updater (do not recreate; preserves setupAnimator)
    vblankUpdater->addAnimator(ledAnimator);
    // Create backdrop animators (one per decorative ring) and register with vblank updater
    for (size_t i = 0; i < backdropAnimators.size(); ++i)
    {
        backdropAnimators[i] = std::make_unique<juce::Animator>(juce::ValueAnimatorBuilder{}
            .withDurationMs((float) UiLayout::kBackdropPulseDurationMs)
            .withValueChangedCallback([this, i](float progress){
                backdropPulseProgress[i] = 1.0f - juce::jlimit(0.0f, 1.0f, progress);
                repaint();
            })
            .build());
        vblankUpdater->addAnimator(*backdropAnimators[i]);
    }

    // Load dancer PNG frame sequence (placed inside idx0 donut interior)
    loadDancerFrames();
}
  
}

void ClockSyncAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll (UiThemeColours::accent());
    auto bounds = getLocalBounds().toFloat();
    juce::ColourGradient bgGrad(UiThemeColours::base(), bounds.getTopLeft(), kBaseLo, bounds.getBottomLeft(), false);
    g.setGradientFill(bgGrad);
    g.fillRect(bounds);

    // Decorative background circles: larger circles use a darker variant and pulse on beats
    {
        auto centre = bounds.getCentre();
        const auto& sizes = UiLayout::kBackdropSizes;
        for (size_t i = 0; i < sizes.size(); ++i)
        {
            const float baseSz = (float) sizes[i];
            //const float darkFactor = 0.3f + 0.75f * (float) i; // bigger -> darker
            const float darkFactor = 0.888f + 0.888f * (float) i;
            // Apply pulse scale (progress goes 1->0) to briefly enlarge the circle on its beat
            const float progress = backdropPulseProgress[i];
            const float extraScale = backdropPulseScale[i] * progress; // e.g. 0.02 -> +2%
            const float sz = baseSz * (UiLayout::kBackdropMultiplier + extraScale);

            // Slight alpha boost on pulse so it looks like a brief flash
            const float baseAlpha = 1.0f; // fully opaque base colouring
            const float alphaBoost = 0.45f * progress; // up to +0.45 alpha

            // Original behaviour: use the accent colour darkened per-ring.
            // No cyan interpolation and no alpha boost applied.
            juce::Colour col = UiThemeColours::accent().darker(darkFactor);

            // single vertical offset for backdrop positioning
            juce::Rectangle<float> rc(centre.x - sz * 0.5f, centre.y - sz * 0.5f + 10.0f, sz, sz);
            g.setColour(col);
            g.fillEllipse(rc);
        }

        // Draw dancer (behind ring visuals) clipped to inner donut
        drawDancer(g);
    }

    // Draw a donut-shaped mask behind the ring (circle index 0) to hide thin aliasing lines.
    // Use the ring's own radius values so the visual mask stays in sync if the ring constants change.
    {
        float maskOuter = (float) (kRingOuterD / 2);
        float maskInner = (float) (kRingInnerD / 2);
        juce::Path donutPath;
        donutPath.addEllipse((float) (ringArea.getCentreX() - maskOuter), (float) (ringArea.getCentreY() - maskOuter), maskOuter * 2.0f, maskOuter * 2.0f);
        donutPath.addEllipse((float) (ringArea.getCentreX() - maskInner), (float) (ringArea.getCentreY() - maskInner), maskInner * 2.0f, maskInner * 2.0f);
        // Use even-odd rule so the second ellipse becomes a hole. Do NOT draw
        // anything here; the playground is responsible for ensuring its own
        // cutout so the editor should leave the centre transparent.
        donutPath.setUsingNonZeroWinding(false);
    }

    // Header background now drawn in paintOverChildren to ensure it sits above playground visuals.
}

void ClockSyncAudioProcessorEditor::paintOverChildren(juce::Graphics& g)
{
    // Submenu should draw first (behind header) so header masks its top portion.
    const bool inNameMode = nameMidiSwitch.getToggleState();
    const bool submenuActive = (inNameMode && (setupSubmenuTargetOn || setupSubmenuProgress > 0.0f));
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
        const int btnW = 60, btnH = 14, gap = 6, marginX = 8;
        const int yButtons = (int) std::round(submenuRect.getBottom() - btnH - 4.0f);
        idleModeButton.setBounds(marginX + 6, yButtons, btnW, btnH);
        legacyModernButton.setBounds(marginX + 6 + (btnW + gap), yButtons, btnW, btnH);
        sppButton.setBounds(marginX + 6 + 2*(btnW + gap), yButtons, btnW, btnH);
        auto repaintComp = [&g](juce::Component& c){ if (! c.isVisible()) return; juce::Graphics::ScopedSaveState ss(g); g.setOrigin(c.getX(), c.getY()); c.paint(g); g.setOrigin(0,0); };
        repaintComp(idleModeButton);
        repaintComp(legacyModernButton);
        repaintComp(sppButton);

        // Version label (right-aligned)
        const juce::String ver = PLUGIN_VERSION_WITH_BUILD;
        juce::Font vf(juce::FontOptions("Arial", 11.0f, juce::Font::bold));
        g.setFont(vf);
        g.setColour(UiThemeColours::cyan());
        // Version label alignment uses button geometry (already computed above)
        juce::Rectangle<int> verArea((int)getWidth() - 156, yButtons, 150, btnH);
        g.drawFittedText(ver, verArea, juce::Justification::centredRight, 1);

        // Hover tooltips (avoid drawing above header region 0..30)
        if (hoveredSetupIndex >= 0)
        {
            g.setColour(UiThemeColours::accent());
            g.setFont(vf);
            auto drawTip = [&](const juce::String& text, const juce::Component& c){
                juce::Rectangle<int> r = c.getBounds();
                juce::Rectangle<int> tipR(r.getX(), r.getY() - 18, r.getWidth(), r.getHeight());
                if (tipR.getBottom() < 30) return; // clipped by header
                g.drawFittedText(text, tipR, juce::Justification::centred, 1);
            };
            if (hoveredSetupIndex == 0) drawTip("send midi-clocks when idle/stopped", idleModeButton);
            else if (hoveredSetupIndex == 1) drawTip("legacy sends always stop before start", legacyModernButton);
            else if (hoveredSetupIndex == 2) drawTip("send song position pointer", sppButton);
        }
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
    if (refreshButton.isVisible()) repaintHeaderComp(refreshButton);
    if (setupButton.isVisible()) repaintHeaderComp(setupButton);
    repaintHeaderComp(deviceBox);
    repaintHeaderComp(nameBox);
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

    // If PlaygroundComponent is present, prefer its internal popups and
    // skip drawing the legacy popup-ring overlays which depend on small
    // component bounds and can visually clip expansion outside those bounds.
    if (! playgroundComp)
    {
        // Only draw remaining popup rings for non-playground mode.
    }

    // Always draw header status and LED above all child components (keep within the base 30px header area)
    {
        auto header = juce::Rectangle<float>(0,0,(float)getWidth(), 30.0f).reduced(8,4);
        const bool isRunning = processor.getUiIsRunning();
        const bool isArmed = processor.getUiPendingStart();
        const bool hasNext = processor.getUiNextRestartPending();
        juce::String status;
        if (hasNext)
            status = "PENDING";
        else if (isRunning)
            status = "LOCKED";
        else if (isArmed)
            status = "ARMED";
        else
            status = idleModeButton.getToggleState() ? "IDLE" : "STOP";

        g.setFont(juce::Font(juce::FontOptions("Arial", 11.0f, juce::Font::bold)));
        g.setColour(UiThemeColours::cyan());
        // Shift status text slightly right (+4px) per request
        g.drawFittedText(status, juce::Rectangle<int>(getWidth() - 73, (int)header.getY(), 50, (int)header.getHeight()),
                         juce::Justification::centred, 1);

        const float ledRadius = UiLayout::kLedRadius;
        // Move LED slightly right (+8px)
        auto ledCenter = juce::Point<float>(getWidth() - 17.0f, header.getCentreY());
        const float a = juce::jlimit(0.0f, 1.0f, ledLevel);
        auto ledColour = UiThemeColours::cyan().withAlpha(0.10f).interpolatedWith(UiThemeColours::cyan().withAlpha(0.97f), a);
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillEllipse(ledCenter.x - ledRadius - 1.5f, ledCenter.y - ledRadius + 1.5f, ledRadius * 2, ledRadius * 2);
        g.setColour(ledColour);
        g.fillEllipse(ledCenter.x - ledRadius, ledCenter.y - ledRadius, ledRadius * 2, ledRadius * 2);
        g.setColour(juce::Colours::white.withAlpha(0.15f));
        g.drawEllipse(ledCenter.x - ledRadius, ledCenter.y - ledRadius, ledRadius * 2, ledRadius * 2, 1.0f);
    }
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
        refreshButton.setBounds(comboX - (toggleW + 4), marginY + 5, toggleW, contentH - 10);
        setupButton.setBounds(refreshButton.getBounds());
        // place NAME/MIDI toggle left of refresh button (moved further left to avoid clipping)
        const int extraLeft = 17; // nudge left
        nameMidiSwitch.setBounds(refreshButton.getX() - (toggleW + 13 + extraLeft), marginY + 5, 40, contentH - 10);
        // comboboxes share same bounds; only one visible at a time
        deviceBox.setBounds(comboX, marginY + 3, comboW, contentH - 2);
        nameBox.setBounds(comboX, marginY + 3, comboW, contentH - 2);

        // Setup submenu layout now handled in paintOverChildren via animation; initial hidden position set here.
        updateSetupSubmenuLayout();
        
        // Combo remains interactive (double-click to edit).
        // make refresh button small single-letter
        refreshButton.setButtonText("R");
        setupButton.setButtonText("S");
    }

    // Position imported components exactly as authored in LayoutPlayground (use canonical baseCircles)
    if (playgroundComp && playgroundComp->baseCircles.size() >= 9)
    {
        // Use canonical positions from Main.cpp (baseCircles). Do NOT apply
        // any vertical shift; the header is a Z-overlay.

        // idx7 (click rate rotary + inner click-to-pulse button) is fully handled
        // by the PlaygroundComponent. Keep legacy slider/button hidden and do not
        // lay them out when the playground is active.

        // circle 0 -> ring area
        {
            const auto& c0 = playgroundComp->baseCircles.getReference(0);
            const int d = (int) std::round(c0.r * 2.0f);
            ringArea = juce::Rectangle<int>((int)std::round(c0.x - c0.r), (int)std::round(c0.y - c0.r), d, d);
        }

        // circle 1 handled by playground visuals.

        // circle 2 -> idleClockToggle
        {
            const auto& c2 = playgroundComp->baseCircles.getReference(2);
            const int w = (int) std::round(c2.r * 2.0f);
            idleClockToggle.setBounds((int)std::round(c2.x - c2.r), (int)std::round(c2.y - c2.r), w, w);
        }

        // circle 5: shuffle / clockWhileStopped visuals.

        // Resync step selection handled by ring segments.

        // Help toggle: keep its authored placement relative to canvas (use previous explicit position)
        const int helpSize = 25;
        helpToggle.setBounds(0, 210, helpSize, helpSize);
    }
    else
    {
        // Fallback positions if playground absent; keep clickButton hidden.
        ringArea = juce::Rectangle<int>(150 - 70, 130 - 70, 140, 140);
        //idleClockToggle.setBounds(234 - 14, 132 - 14, 28, 28);
        // shuffleScaleToggle removed.
        // no legacy triggerRect fallback
        // stepOffsetMenu no longer present.
        const int helpSize = 25; helpToggle.setBounds(0, 210, helpSize, helpSize);
    }

    
}

// Update setup submenu button positions based on current animation progress (without repaint).
void ClockSyncAudioProcessorEditor::updateSetupSubmenuLayout()
{
    if (! nameMidiSwitch.getToggleState()) return; // submenu only relevant in NAME mode
    if (! (setupSubmenuTargetOn || setupSubmenuProgress > 0.0f)) return; // not active
    const float hiddenTop = -10.0f;
    const float shownTop  = 25.0f; // adjusted per request (less downward slide)
    const float topY = hiddenTop + (shownTop - hiddenTop) * setupSubmenuProgress;
    const float submenuH = 30.0f;
    const int btnW = 60;
    const int btnH = 14;
    const int gap = 6;
    const int marginX = 8;
    const int yButtons = (int) std::round(topY + submenuH - btnH - 3.0f);
    idleModeButton.setBounds(marginX, yButtons, btnW, btnH);
    legacyModernButton.setBounds(marginX + (btnW + gap), yButtons, btnW, btnH);
    sppButton.setBounds(marginX + 2*(btnW + gap), yButtons, btnW, btnH);
}

// Track hover state for setup submenu buttons to show tips
// Disabled duplicate mouseMove block
#if 0
void ClockSyncAudioProcessorEditor::mouseMove(const juce::MouseEvent& e)
{
    // Preserve existing hover forwarding behaviour by calling base logic first (duplicated excerpt simplified)
    juce::Point<float> pf((float) e.x, (float) e.y);
    // Existing playground hover code omitted for brevity; retain original via call to base implementation if refactored.
    // Setup submenu hover detection
    int newHover = -1;
    if (idleModeButton.getBounds().contains(e.getPosition())) newHover = 0;
    else if (legacyModernButton.getBounds().contains(e.getPosition())) newHover = 1;
    else if (sppButton.getBounds().contains(e.getPosition())) newHover = 2;
    if (newHover != hoveredSetupIndex)
    {
        hoveredSetupIndex = newHover;
        // Repaint submenu area only
        repaint(0, 0, getWidth(), 80);
    }
    // Forward hover info to playground (minimal) -- replicate original behaviour
    if (playgroundComp && playgroundComp->baseCircles.size() > 0)
    {
        bool found = false;
        for (int i = 0; i < playgroundComp->baseCircles.size(); ++i)
        {
            const auto& c = playgroundComp->baseCircles.getReference(i);
            juce::Rectangle<int> r((int)std::round(c.x - c.r), (int)std::round(c.y - c.r), (int)std::round(c.r * 2.0f), (int)std::round(c.r * 2.0f));
            if (r.contains(e.getPosition())) { playgroundComp->setHoverIndexFromEditor(i); found = true; break; }
        }
        if (! found) playgroundComp->clearHoverFromEditor();
    }
}
#endif // disabled duplicate mouseMove

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
        if (playgroundComp) playgroundComp->setRunState(running);
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
        const int pulsesInQuarter = 24; // fixed PPQ sync
        const int pInQuarter = (int) (pulses % (unsigned long long) pulsesInQuarter);
        int frameIdx = (pInQuarter * dancerFrameCount) / pulsesInQuarter;
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
    
    deviceBox.setLookAndFeel(nullptr);
    refreshButton.setLookAndFeel(nullptr);
    nameBox.setLookAndFeel(nullptr);
    nameMidiSwitch.setLookAndFeel(nullptr);
    // Clear help button look-and-feel
    helpToggle.setLookAndFeel(nullptr);
}

void ClockSyncAudioProcessorEditor::mouseUp(const juce::MouseEvent& e)
{
    // No-op: ringArea (run) toggling handled on mouseDown for more immediate response.

    
}

void ClockSyncAudioProcessorEditor::mouseMove(const juce::MouseEvent& e)
{
    juce::Point<float> pf((float) e.x, (float) e.y);
    bool did = false;
    const bool submenuActive = (nameMidiSwitch.getToggleState() && (setupSubmenuTargetOn || setupSubmenuProgress > 0.0f));
    if (submenuActive)
    {
        int forced = -1;
        if (idleModeButton.isVisible() && idleModeButton.getBounds().contains(e.getPosition())) forced = 9; // maps to added hoverTexts entry
        else if (legacyModernButton.isVisible() && legacyModernButton.getBounds().contains(e.getPosition())) forced = 10;
        else if (sppButton.isVisible() && sppButton.getBounds().contains(e.getPosition())) forced = 11;
        if (playgroundComp)
        {
            if (forced >= 0) playgroundComp->setForcedHoverIndex(forced); else playgroundComp->clearForcedHoverIndex();
        }
        // Block further hover propagation when submenu active
        return;
    }
    else if (playgroundComp)
    {
        playgroundComp->clearForcedHoverIndex();
        playgroundComp->setExternalHoverBlocked(false);
    }
    // Playground provides hover/tooltips; skip deprecated popup-ring hover handling.
    if (! playgroundComp)
    {
        auto checkHoverComp = [&](const std::unique_ptr<PopupMenuRing>& pr, const juce::Component& c)
        {
            if (! pr) return;
            auto b = c.getBounds().toFloat();
            const float baseX = b.getCentreX();
            const float baseY = b.getCentreY();
            const float baseR = std::min(b.getWidth(), b.getHeight()) * 0.5f;
            const int hi = pr->handleMouseMove(pf, baseX, baseY, baseR);
            if (hi >= 0 || pr->isAnimating())
            {
                repaint(c.getBounds().expanded(80, 80));
                did = true;
            }
        };

        
    }
    if (popupRing0) { }

    

    // Forward hover info to the playground so its hoverText reflects the
    // canonical visual circles even when native components sit on top.
    if (playgroundComp && playgroundComp->baseCircles.size() > 0)
    {
        bool found = false;
        for (int i = 0; i < playgroundComp->baseCircles.size(); ++i)
        {
            const auto& c = playgroundComp->baseCircles.getReference(i);
            juce::Rectangle<int> r((int)std::round(c.x - c.r), (int)std::round(c.y - c.r),
                                   (int)std::round(c.r * 2.0f), (int)std::round(c.r * 2.0f));
            if (r.contains(e.getPosition())) { playgroundComp->setHoverIndexFromEditor(i); found = true; break; }
        }
        if (! found) playgroundComp->clearHoverFromEditor();
    }
}



void ClockSyncAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    // Route clicks to any visible popup rings first.
    juce::Point<float> pf((float) e.x, (float) e.y);
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

    // Handle segment clicks on the main ring (circle idx 0): selecting a segment
    // sets the resync offset step (1..16). Clicking the inner circle toggles Run.
    if (ringArea.contains(e.getPosition()))
    {
        const int cx = ringArea.getCentreX();
        const int cy = ringArea.getCentreY();
        // same shrink values used by drawRing()
        constexpr int outerShrink = 20;
        constexpr int innerShrink = 20;
        const float outerD = (float)(kRingOuterD - outerShrink);
        const float innerD = (float)(kRingInnerD - (innerShrink - 8));
        const float outerR = outerD * 0.5f;
        const float innerR = innerD * 0.5f;
        const float dx = (float)e.x - (float)cx;
        const float dy = (float)e.y - (float)cy;
        const float dist2 = dx*dx + dy*dy;
        if (dist2 <= innerR * innerR)
        {
            // Center click: toggle Run (chaselight). Use a proper gesture so hosts record automation cleanly.
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
                    // Brief header LED pulse for feedback on click (stronger when starting, softer when stopping)
                    ledPulseTarget = next ? 1.0f : 0.6f;
                    ledAnimator.start();
                    repaint(ringArea);
                }
            }
            return;
        }
        // Clicked in the ring donut -> map to a 1..16 LOGICAL segment (apply playground offset)
        if (dist2 <= outerR * outerR && dist2 >= innerR * innerR)
        {
            const float angle = std::atan2(dy, dx); // -pi..pi
            const float startAt12 = -juce::MathConstants<float>::halfPi;
            const float twoPi = juce::MathConstants<float>::twoPi;
            float rel = angle - startAt12;
            while (rel < 0.0f) rel += twoPi;
            const float slice = twoPi / 16.0f;
            int rawIdx = (int) std::floor(rel / slice); // 0..15 (raw index as wedges are drawn)
            // Convert raw index -> logical index (playground used offset=4)
            const int offset = 4; // quarter-turn offset to match playground mapping
            int logical0 = (rawIdx - offset + 16) % 16; // 0-based logical
            int step = logical0 + 1; // 1..16
            // Apply parameter change (set logical step)
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
        // else fall through (click outside donut but inside bounding box)
    }

    // If no popup consumed the click, allow clicks on the base components to toggle their popups.
    // This mirrors the playground behaviour: clicking a small circle opens the associated popup.
    // Forward clicks that land on the playground's visual circles (indices 3 and 6)
    // to the playground so its internal popups (`popup3` and `popup6`) can
    // expand/collapse. The playground is rendered underneath native controls,
    // so clicks may be intercepted — forward them explicitly.
    if (playgroundComp)
    {
        if (playgroundComp->baseCircles.size() > 3)
        {
            const auto& c3 = playgroundComp->baseCircles.getReference(3);
            juce::Rectangle<int> r3((int)std::round(c3.x - c3.r), (int)std::round(c3.y - c3.r),
                                    (int)std::round(c3.r * 2.0f), (int)std::round(c3.r * 2.0f));
            if (r3.contains(e.getPosition())) { playgroundComp->togglePopup3(); repaint(); return; }
        }
        // Trigger circle is canonical index 1 in the playground; clicking it
        // should arm and request a one-shot trigger (same as triggerRect handler).
        if (playgroundComp->baseCircles.size() > 1)
        {
            const auto& c1 = playgroundComp->baseCircles.getReference(1);
            juce::Rectangle<int> r1((int)std::round(c1.x - c1.r), (int)std::round(c1.y - c1.r),
                                    (int)std::round(c1.r * 2.0f), (int)std::round(c1.r * 2.0f));
            if (r1.contains(e.getPosition()))
            {
                // Original behaviour: manual trigger does NOT auto-enable Run.
                processor.requestTriggerOnce();
                if (playgroundComp) (void) playgroundComp->handleExternalClickIndex(1);
                // Manual trigger chase-light offset: compute & apply immediately
                manualTriggerOffsetActive = true;
                manualTriggerRelativeStepAtTrigger = juce::jlimit(1,16, relativeStepCached);
                if (selectedResyncStepCached > 0)
                    manualTriggerPlayheadDelta = (selectedResyncStepCached - manualTriggerRelativeStepAtTrigger + 16) % 16;
                else
                    manualTriggerPlayheadDelta = 0;
                // Flash the segment corresponding to the current playhead step at trigger time (not the offset preview target)
                if (playgroundComp)
                {
                    playgroundComp->flashSegmentLogical(manualTriggerRelativeStepAtTrigger);
                }
                repaint();
                return;
            }
        }
        if (playgroundComp->baseCircles.size() > 6)
        {
            const auto& c6 = playgroundComp->baseCircles.getReference(6);
            juce::Rectangle<int> r6((int)std::round(c6.x - c6.r), (int)std::round(c6.y - c6.r),
                                    (int)std::round(c6.r * 2.0f), (int)std::round(c6.r * 2.0f));
            if (r6.contains(e.getPosition())) { playgroundComp->togglePopup6(); repaint(); return; }
        }
        // Forward clicks that land on other canonical circle areas to the playground
        // so it can perform the expected visual/behavioral response (flashes,
        // resync, small popups, etc.). Skip indices 3 and 6 because they're
        // handled explicitly above.
        const int bc = (int) playgroundComp->baseCircles.size();
        for (int i = 0; i < bc; ++i)
        {
            if (i == 3 || i == 6) continue;
            const auto& ci = playgroundComp->baseCircles.getReference(i);
            juce::Rectangle<int> ri((int)std::round(ci.x - ci.r), (int)std::round(ci.y - ci.r),
                                    (int)std::round(ci.r * 2.0f), (int)std::round(ci.r * 2.0f));
            if (ri.contains(e.getPosition()))
            {
                if (playgroundComp->handleExternalClickIndex(i)) { repaint(); return; }
            }
        }
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
    nameBox.addItem("new...", 1000);
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
        nameBox.setTextWhenNothingSelected("new...");
    }
}

void ClockSyncAudioProcessorEditor::showNewNameDialog()
{
    if (nameEntryEditor) return;
    // open an inline empty editor for new name (commit on Enter, cancel on blur)
    auto rc = nameBox.getBounds();
    const int editorH = 22;
    const int editorW = std::max(160, rc.getWidth());
    const int x = rc.getX();
    const int y = rc.getBottom() + 4;

    nameEntryEditor = std::make_unique<juce::TextEditor>();
    nameEntryEditor->setText(juce::String());
    nameEntryEditor->setFont(juce::Font(juce::FontOptions("Arial", 14.0f, juce::Font::plain)));
    nameEntryEditor->setBounds(x, y, editorW, editorH);
    addAndMakeVisible(*nameEntryEditor);
    nameEntryEditor->grabKeyboardFocus();

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
    const int editorH = 22;
    const int editorW = std::max(160, rc.getWidth());
    const int x = rc.getX();
    const int y = rc.getBottom() + 4;

    nameEntryEditor = std::make_unique<juce::TextEditor>();
    nameEntryEditor->setText(cur);
    nameEntryEditor->setFont(juce::Font(juce::FontOptions("Arial", 14.0f, juce::Font::plain)));
    nameEntryEditor->setBounds(x, y, editorW, editorH);
    addAndMakeVisible(*nameEntryEditor);
    nameEntryEditor->grabKeyboardFocus();

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


