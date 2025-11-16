#include <utility>
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "build_info.h"
#include "GridScaleMenu.h"
#include "BinaryData.h"
#include "LookAndFeels.h" // Use centralised LookAndFeel & theme colours
#include <array>
#include <optional>

namespace
{
    // Derived theme colour variant (slightly lighter/darker base) while core colours come from UiThemeColours
    const juce::Colour kBaseLo  = UiThemeColours::base().darker(0.12f);

    // Arc size button definitions (absolute positions from canvas origin)
    // Previously positioned algorithmically with baseY/xStart/gap; converted to fixed rects for easier manual tweaking.
    // Each rect: {x, y, w, h}. Derived from former layout: sizes {22,25,28,31,34,37,40} and y-offsets {0,16,22,26,28,26,16} added to baseY=215.
    // Visual shallow arc preserved. Adjust these directly as needed.
    // juce::Rectangle isn't a literal type; use static const (not constexpr)
    // Global offset for non-excluded components (exclude: reset/refreshButton, deviceBox, header, LED, status text)
    // Requested shift: move content (excluding header) by x -5, y -10.
    const int offX = -20;
    const int offY = -40;

    static const std::array<juce::Rectangle<int>, 7> kArcButtonRects = {
        juce::Rectangle<int>( 107 + offX, 220 + offY, 22, 22),
        juce::Rectangle<int>(118 + offX, 230 + offY, 25, 25),
        juce::Rectangle<int>(131 + offX, 236 + offY, 28, 28),
        // Adjusted Y for value 4 button (index 3) to sit along the drag path and avoid being skipped (was 220)
        juce::Rectangle<int>(147 + offX, 239 + offY, 31, 31),
        juce::Rectangle<int>(166 + offX, 238 + offY, 34, 34),
        juce::Rectangle<int>(187 + offX, 231 + offY, 37, 37),
        juce::Rectangle<int>(204 + offX, 221 + offY, 40, 40)
    };
}

ClockSyncAudioProcessorEditor::ClockSyncAudioProcessorEditor(ClockSyncAudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processor(p)
{
    setResizable(false, false);
    setSize(300, 240);
    startTimerHz(60);

    // Component setup (colour, style, add, z-order)
    {
        gridScaleMenu.setColours(UiThemeColours::accent(), UiThemeColours::base(), UiThemeColours::cyan());
        shuffleModeMenu.setColours(UiThemeColours::accent(), UiThemeColours::base(), UiThemeColours::cyan());
        triggerModeToggle.setColours(UiThemeColours::accent(), UiThemeColours::base(), UiThemeColours::cyan());
        idleClockToggle.setColours(UiThemeColours::accent(), UiThemeColours::base(), UiThemeColours::cyan());
        shuffleScaleToggle.setColours(UiThemeColours::accent(), UiThemeColours::base(), UiThemeColours::cyan());
        stepOffsetMenu.setColours(UiThemeColours::accent(), UiThemeColours::base(), UiThemeColours::cyan());
        clickButton.setColours(UiThemeColours::accent(), UiThemeColours::cyan());

        clickLevelSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        clickLevelSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        clickRotaryLNF = std::make_unique<ClickRotaryLNF>();
        clickLevelSlider.setLookAndFeel(clickRotaryLNF.get());
        // Repurposed rotary: discrete 0..4 stages for click rate (0=off,1=4th,2=8th,3=16th,4=24ppq)
        clickLevelSlider.setRange(0, 4, 1);
        clickLevelSlider.setTooltip("Off / 4th / 8th / 16th / 24ppq");

        addAndMakeVisible(gridScaleMenu);
        addAndMakeVisible(shuffleModeMenu);
        addAndMakeVisible(deviceBox);
        addAndMakeVisible(refreshButton);
        addAndMakeVisible(nameBox);
        addAndMakeVisible(nameMidiSwitch);
        // attach a mouse listener so double-clicking the name combo opens an inline editor
        struct NameBoxMouseListener : public juce::MouseListener
        {
            ClockSyncAudioProcessorEditor* owner;
            NameBoxMouseListener(ClockSyncAudioProcessorEditor* o) : owner(o) {}
            void mouseDoubleClick(const juce::MouseEvent&) override { owner->toggleNameEditorOrCommit(); }
        };
        nameBox.addMouseListener(new NameBoxMouseListener(this), true);
        addAndMakeVisible(clickButton);
        addAndMakeVisible(clickLevelSlider);
        addAndMakeVisible(triggerModeToggle);
        addAndMakeVisible(idleClockToggle);
        addAndMakeVisible(shuffleScaleToggle);
        addAndMakeVisible(stepOffsetMenu);

        gridScaleMenu.toBack();
        // Keep slider and its small click button in front so they are visually prominent
        clickLevelSlider.toFront(true);
        clickButton.toFront(true);
        idleClockToggle.toBack();
        stepOffsetMenu.toFront(true);
    }

    auto& apvts = processor.getAPVTS();

    // clickButton now toggles click variant (sample spike vs 1ms pulse)
    clickEnableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, ClockSyncAudioProcessor::paramClickPulse, clickButton);
    // clickLevelSlider repurposed to select click rate stages (0..4)
    clickLevelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ClockSyncAudioProcessor::paramClickRate, clickLevelSlider);
    idleClockAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, ClockSyncAudioProcessor::paramClockWhileStopped, idleClockToggle);
    triggerModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, ClockSyncAudioProcessor::paramTriggerModeEnabled, triggerModeToggle);

    // Name/Instrument combo setup (look-and-feel assigned after ThemeLNF is created)
    nameBox.setColour(juce::ComboBox::textColourId, UiThemeColours::cyan());
    nameBox.setJustificationType(juce::Justification::centred);
    // hide the arrow for nameBox so clicks are handled by overlay
    nameBox.setColour(juce::ComboBox::arrowColourId, juce::Colours::transparentBlack);
    // Ensure the internal text component does not allow editing (prevents extra text widgets)
    nameBox.setEditableText(false);
    // Load saved instrument names from state
    loadInstrumentNamesFromState();
    populateNameBox();

    // Default: NAME mode off => show deviceBox
    nameMidiSwitch.setToggleState(false, juce::dontSendNotification);
    nameMidiSwitch.setButtonText("NAME");
    nameMidiSwitch.onClick = [this]
    {
        const bool isOn = nameMidiSwitch.getToggleState();
        if (isOn) nameMidiSwitch.setButtonText("MIDI"); else nameMidiSwitch.setButtonText("NAME");
        // Show only the active combo and ensure z-order to avoid accidental overlap
        nameBox.setVisible(isOn);
        deviceBox.setVisible(! isOn);
        if (isOn)
        {
            nameBox.toFront(true);
            deviceBox.toBack();
        }
        else
        {
            deviceBox.toFront(true);
            nameBox.toBack();
        }
        // nothing else needed; editing trigger handled by double-click on the combo
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

    // editing/commit handled by double-click listener which calls toggleNameEditorOrCommit()

    {
        int initialStep = 1;
        if (auto* pi = dynamic_cast<juce::AudioParameterInt*>(
                apvts.getParameter(ClockSyncAudioProcessor::paramResyncOffsetStep)))
            initialStep = pi->get();
        stepOffsetMenu.setStep(initialStep);
    }

    stepOffsetMenu.onStepChanged = [this](int step)
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



    themeLNF = std::make_unique<ThemeLNF>();

    // Ensure controls use our theme look-and-feel instance
    nameBox.setLookAndFeel(themeLNF.get());
    nameMidiSwitch.setLookAndFeel(themeLNF.get());
    // Make the TextButton behave like a toggle
    nameMidiSwitch.setClickingTogglesState(true);
    // Flip colouring: when OFF show base; when ON show accent (swapped per request)
    nameMidiSwitch.setColour(juce::TextButton::textColourOffId, UiThemeColours::accent());
    nameMidiSwitch.setColour(juce::TextButton::textColourOnId,  UiThemeColours::base());

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
    gridScaleMenu.setIndex(rateIndexCached);
    gridScaleMenu.onGridChanged = [setRateIndex](int idx){ setRateIndex(idx); };

    // Re-connect shuffle arc control to paramShuffleStep (1..7)
    if (auto* shuffleParam = apvts.getParameter(ClockSyncAudioProcessor::paramShuffleStep))
    {
        auto shuffleRange = shuffleParam->getNormalisableRange();
        // Continuous feedback while dragging / changing
        shuffleModeMenu.onValueChanged = [shuffleParam, shuffleRange](int v)
        {
            v = juce::jlimit(1, 7, v);
            shuffleParam->beginChangeGesture();
            shuffleParam->setValueNotifyingHost(shuffleRange.convertTo0to1((float) v));
        };
        // Finalize gesture on mouse-up click callback
        shuffleModeMenu.onButtonClicked = [shuffleParam, shuffleRange](int v)
        {
            v = juce::jlimit(1, 7, v);
            shuffleParam->setValueNotifyingHost(shuffleRange.convertTo0to1((float) v));
            shuffleParam->endChangeGesture();
        };
    }

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
    // hide the arrow so the text isn't shifted by an arrow-area
    deviceBox.setColour(juce::ComboBox::arrowColourId, juce::Colours::transparentBlack);
    // Ensure the internal text component does not allow editing (prevents extra text widgets)
    deviceBox.setEditableText(false);
    // Make the combo box label use the accent colour but darker for contrast
    deviceBox.setColour(juce::ComboBox::textColourId, UiThemeColours::accent().darker(0.5f));

    // Ensure nameBox visibility mirrors switch state initially
    nameBox.setVisible(nameMidiSwitch.getToggleState());
    triggerModeToggle.onClick = [this]
    {
        if (triggerModeToggle.getToggleState())
            processor.notifyResyncOffsetChanged();
    };

    loadDancerFrames();
    triggerFade = 0.0f;
    // Populate device list immediately so the editor recalls last-used port on open
    refreshDeviceList();
}

void ClockSyncAudioProcessorEditor::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    juce::ColourGradient bgGrad(UiThemeColours::base(), bounds.getTopLeft(), kBaseLo, bounds.getBottomLeft(), false);
    g.setGradientFill(bgGrad);
    g.fillRect(bounds);

    // Decorative background circles: larger circles use a darker variant and pulse on beats
    {
        auto centre = bounds.getCentre();
        const std::array<int, 6> sizes = { 420 ,340, 280, 230, 190, 160 };
        for (size_t i = 0; i < sizes.size(); ++i)
        {
            const float baseSz = (float) sizes[i];
            const float darkFactor = 0.1f + 0.75f * (float) i; // bigger -> darker

            // Apply pulse scale (progress goes 1->0) to briefly enlarge the circle on its beat
            const float progress = backdropPulseProgress[i];
            const float extraScale = backdropPulseScale[i] * progress; // e.g. 0.02 -> +2%
            const float sz = baseSz * (1.0f + extraScale);

            // Slight alpha boost on pulse so it looks like a brief flash
            const float baseAlpha = 1.0f; // fully opaque base colouring
            const float alphaBoost = 0.45f * progress; // up to +0.45 alpha

            // Original behaviour: use the accent colour darkened per-ring.
            // No cyan interpolation and no alpha boost applied.
            juce::Colour col = UiThemeColours::accent().darker(darkFactor);

            juce::Rectangle<float> rc(centre.x - sz * 0.5f, centre.y - sz * 0.5f + 10, sz, sz);
            g.setColour(col);
            g.fillEllipse(rc);
        }
    }

    auto header = getLocalBounds().removeFromTop(30).reduced(8, 4).toFloat();
    juce::Path headerPath; headerPath.addRoundedRectangle(header, 6.0f);
    g.setColour(UiThemeColours::base());
    g.fillPath(headerPath);

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
            status = idleClockToggle.getToggleState() ? "IDLE" : "STOP";
    g.setFont(juce::Font(juce::FontOptions("Arial", 11.0f, juce::Font::bold)));
    g.setColour(UiThemeColours::cyan());
    // Shift status text slightly right (+4px) per request
    g.drawFittedText(status, juce::Rectangle<int>(getWidth() - 73, (int)header.getY(), 50, (int)header.getHeight()),
                     juce::Justification::centred, 1);

    const float ledRadius = 6.0f;
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

void ClockSyncAudioProcessorEditor::paintOverChildren(juce::Graphics& g)
{
    // Draw ring first (outer + inner base); then overlay dancer so it remains visible.
    drawRing(g);
    {
        juce::Graphics::ScopedSaveState ss(g);
        // Clip to inner circle to avoid drawing over ring outline.
        juce::Path clip;
        clip.addEllipse((float) (ringArea.getCentreX() - kRingInnerD / 2),
                        (float) (ringArea.getCentreY() - kRingInnerD / 2),
                        (float) kRingInnerD, (float) kRingInnerD);
        g.reduceClipRegion(clip);
        drawDancer(g);
    }
    {
        juce::Graphics::ScopedSaveState ss(g);
        auto r = stepOffsetMenu.getBounds().toFloat();
        g.reduceClipRegion(r.toNearestInt());
        g.setOrigin(stepOffsetMenu.getPosition());
        stepOffsetMenu.paintEntireComponent(g, true);
        g.setOrigin(0, 0);
    }
    // Repaint the small click toggle above the ring as well
    {
        juce::Graphics::ScopedSaveState ss(g);
        auto r = clickButton.getBounds().toFloat();
        g.reduceClipRegion(r.toNearestInt());
        g.setOrigin(clickButton.getPosition());
        clickButton.paintEntireComponent(g, true);
        g.setOrigin(0, 0);
    }
    // Curved label feature removed.
    // Draw trigger ellipse last to ensure it is front-most.
    drawTrigger(g);

    // Small version/build label at bottom-right
    {
        const juce::String ver = PLUGIN_VERSION_WITH_BUILD;
        const int padding = 6;
        juce::Font f(juce::FontOptions("Arial", 11.0f, juce::Font::bold));
        g.setFont(f);
        g.setColour(UiThemeColours::cyan());
        auto area = getLocalBounds().reduced(padding);
        auto labelArea = area.removeFromBottom(10).removeFromRight(200);
        g.drawFittedText(ver, labelArea, juce::Justification::centredRight, 1);
    }
}

void ClockSyncAudioProcessorEditor::resized()
{
    // Static absolute positions for fixed 300x300 canvas
    // Header area is painted, no components there (top 28px)

    {
        const int headerH = 30;
        const int marginX = 5;
        const int marginY = 3;
        const int contentH = headerH - marginY * 2; // 20px
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
        // place NAME/MIDI toggle left of refresh button (moved further left to avoid clipping)
        const int extraLeft = 17; // nudge left
        nameMidiSwitch.setBounds(refreshButton.getX() - (toggleW + 13 + extraLeft), marginY + 5, 40, contentH - 10);
        // comboboxes share same bounds; only one visible at a time
        deviceBox.setBounds(comboX, marginY + 3, comboW, contentH - 2);
        nameBox.setBounds(comboX, marginY + 3, comboW, contentH - 2);
        // nameEdit overlay removed; combo remains interactive (double-click to edit)
        // make refresh button small single-letter
        refreshButton.setButtonText("R");
    }

    // Expand slider bounds slightly to increase drag detection area without visibly changing layout
    const int sliderExtra = 12;
    clickLevelSlider.setBounds(70 + offX - sliderExtra/2, 96 + offY - sliderExtra/2, 46 + sliderExtra, 46 + sliderExtra); // rotary position (offset)
    {
        auto sb = clickLevelSlider.getBounds();
        auto c = sb.getCentre();
        clickButton.setBounds(c.x - 8, c.y - 8, 16, 16);
        // ensure slider and click button remain front-most after layout
        clickLevelSlider.toFront(true);
        clickButton.toFront(true);
    }
    // Step ring area (centered region for ring + labels margin)
    ringArea = juce::Rectangle<int>(104 + offX, 104 + offY, 132, 132);
    // Step offset button area (expanded to avoid hover scaling clipping)
    triggerModeToggle.setBounds(240 + offX, 156 + offY, 30, 30);
    stepOffsetMenu.setBounds(216 + offX - 30, 140 + offY , 140, 140);
    idleClockToggle.setBounds(120 + offX, 80 + offY, 30, 30);
    shuffleScaleToggle.setBounds(83 + offX, 200 + offY, 30, 30);
    // Declare click-through holes in stepOffsetMenu so controls behind remain clickable when its menu is closed
    {
        auto holeToggle = stepOffsetMenu.getLocalArea(&triggerModeToggle, triggerModeToggle.getLocalBounds());
        auto holeArcRow = stepOffsetMenu.getLocalArea(&shuffleModeMenu, shuffleModeMenu.getLocalBounds());
        auto combined   = holeToggle.getUnion(holeArcRow);
        stepOffsetMenu.setClickThroughRect(combined);
    }
    gridScaleMenu.setBounds(20 + offX, 128 + offY, 110, 110);
    triggerRect = juce::Rectangle<int>(221 + offX, 80 + offY, 85, 85);
    shuffleModeMenu.setBounds(82 + offX, 200 + offY, 166, 80);
    shuffleModeMenu.setManualBounds(kArcButtonRects);
    stepOffsetMenu.toFront(true);
}

void ClockSyncAudioProcessorEditor::timerCallback()
{
    bool needAll = false;
    bool needRing = false;
    bool needTrigger = false;
    
    // LED update: do NOT set ledLevel on every MIDI clock tick (uiClockCounter)
    // — that was causing the LED to blink at clock resolution. Instead we
    // allow the quarter-beat logic below to set `ledLevel` on beat boundaries
    // (so the LED blinks in step with the backdrop pulses). We still perform
    // decay each timer tick so the LED falls off between beats.
    const auto counter = processor.getUiClockCounter();
    if (counter != lastSeenClockCounter)
    {
        lastSeenClockCounter = counter;
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
            else if (! runParamCached && idleClockToggle.getToggleState())
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

            // Backdrop pulse sequencing: advance on quarter-note boundaries
                        if (beatIndex != lastBackdropBeatIndex)
                        {
                            lastBackdropBeatIndex = beatIndex;
                            switch (beatIndex)
                            {
                                case 0:
                                    backdropPulseProgress[0] = 1.0f;
                                    backdropPulseProgress[4] = 1.0f;
                                    break;
                                case 1:
                                    backdropPulseProgress[1] = 1.0f;
                                    backdropPulseProgress[5] = 1.0f;
                                    break;
                                case 2:
                                    backdropPulseProgress[2] = 1.0f;
                                    break;
                                case 3:
                                    backdropPulseProgress[3] = 1.0f;
                                    break;
                            }
                            // Backdrop circles occupy the full canvas; request a full repaint.
                            needAll = true;
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
    const bool running = processor.getUiIsRunning();
    if (running != engineRunningCached) { engineRunningCached = running; needAll = true; }
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
            gridScaleMenu.setIndex(idx);
            needRing = true;
        }
    }

    // Update trigger fade animator (drives repaint via value changed callback)
    {
        const double ts = juce::Time::getMillisecondCounterHiRes();
        auto status = triggerFadeAnimator.update(ts);
        if (status == juce::Animator::Status::inProgress)
            needTrigger = true; // ensure region gets repainted if callback missed
    }

    // Update LED animator so pulses decay (ledAnimator callbacks repaint header)
    {
        const double ts2 = juce::Time::getMillisecondCounterHiRes();
        auto ledStatus = ledAnimator.update(ts2);
        if (ledStatus == juce::Animator::Status::inProgress)
            needAll = true; // ensure header/full repaint if animator is active
    }

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
            else if (! runParamCached && idleClockToggle.getToggleState())
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
                static int backdropSequencePos = -1;
                backdropSequencePos = (backdropSequencePos + 1) % 8;
                if (backdropSequencePos >= 0 && backdropSequencePos < 6)
                {
                    backdropPulseProgress[(size_t)backdropSequencePos] = 1.0f;
                    if (backdropSequencePos == 4)
                        backdropPulseProgress[0] = 1.0f;
                    needAll = true;
                }
            }
        }
    }

    // Decay backdrop pulse progress (frame-based) so pulses scale/fade back over time
    const float pulseDecay = 0.10f; // per-frame decrement at ~60Hz (~0.12 -> ~8-9 frames)
    for (size_t i = 0; i < backdropPulseProgress.size(); ++i)
    {
        if (backdropPulseProgress[i] > 0.0f)
        {
            backdropPulseProgress[i] = juce::jmax(0.0f, backdropPulseProgress[i] - pulseDecay);
            // Decay affects the full decorative background, repaint whole area.
            needAll = true;
        }
    }

    // Step number update tied to 16th-note changes (no fade).
    // The numeric step shown in the trigger updates always, but the visual
    // wedge (`visualStepCached`) only advances when idleClockToggle permits
    // it (or when the plugin is actively running).
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
            else if (! runParamCached && idleClockToggle.getToggleState())
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
        // Advance visual wedge only if idleClockToggle allows or the engine is running
        if ((idleClockToggle.getToggleState() || runParamCached) && stepNow != visualStepCached)
        {
            visualStepCached = stepNow;
            needRing = true; // ring contains the wedge
        }
    }

    // Sync step offset button with parameter (automation support)
    {
        int paramStep = -1;
        if (auto* pi = dynamic_cast<juce::AudioParameterInt*>(processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramResyncOffsetStep)))
            paramStep = juce::jlimit(1, 16, pi->get());
        if (paramStep > 0 && paramStep != stepOffsetMenu.getStep())
        {
            stepOffsetMenu.setStep(paramStep);
            // no repaint flag needed; component repaints itself
        }
    }

    // Sync shuffle arc buttons with parameter (automation support & external automation)
    {
        int shuffleParam = -1;
        if (auto* si = dynamic_cast<juce::AudioParameterInt*>(processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramShuffleStep)))
            shuffleParam = juce::jlimit(1, 7, si->get());
        if (shuffleParam > 0 && shuffleParam != shuffleValue)
        {
            shuffleValue = shuffleParam;
            shuffleModeMenu.setValue(shuffleValue);
        }
    }

    // Dispatch minimal repaints
    if (needAll) { repaint(); return; }
    if (needRing) repaint(ringArea);
    // Trigger updates can affect the ring drawing (visual slice near trigger). Ensure
    // we repaint both the trigger rect and the ring area to avoid leftover artefacts.
    if (needTrigger)
    {
        // repaint the union so overlapping pixels are correctly refreshed
        repaint(triggerRect.getUnion(ringArea));
    }
}

ClockSyncAudioProcessorEditor::~ClockSyncAudioProcessorEditor()
{
    clickLevelSlider.setLookAndFeel(nullptr);
    deviceBox.setLookAndFeel(nullptr);
    refreshButton.setLookAndFeel(nullptr);
    nameBox.setLookAndFeel(nullptr);
    nameMidiSwitch.setLookAndFeel(nullptr);
}

void ClockSyncAudioProcessorEditor::mouseUp(const juce::MouseEvent& e)
{
    // Toggle run when clicking within the inner black circle
    if (ringArea.contains(e.getPosition()))
    {
        const int cx = ringArea.getCentreX();
        const int cy = ringArea.getCentreY();
        const int innerRadius = 60; // innerD/2
        const float dx = (float) e.x - (float) cx;
        const float dy = (float) e.y - (float) cy;
        if ((dx*dx + dy*dy) <= (float) (innerRadius * innerRadius))
        {
            if (auto* p = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramRun))
            {
                // Read the live param value to avoid races with timer cache
                if (auto* rp = processor.getAPVTS().getRawParameterValue(ClockSyncAudioProcessor::paramRun))
                {
                    const bool current = rp->load() > 0.5f;
                    const bool next = ! current;
                    p->setValueNotifyingHost(next ? 1.0f : 0.0f);
                    runParamCached = next; // reflect immediately in UI
                    repaint(ringArea);
                }
            }
        }
    }

    // Curved-label feature removed: no click handling here.
}



void ClockSyncAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    // Only respond to mouse-down inside the trigger circle
    if (triggerRect.contains(e.getPosition()))
    {
        // Restart fade animator from full intensity
        triggerFade = 1.0f;
        triggerFadeAnimator.start();
        repaint(triggerRect);
        // If Run is currently disabled, enable it so the armed trigger will
        // actually send a Start at the next grid; this arms/run without sending
        // a Stop.
        if (! runParamCached)
        {
            if (auto* p = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramRun))
            {
                p->setValueNotifyingHost(1.0f);
                runParamCached = true;
            }
        }
        // Request the one-shot trigger; the processor will only emit the Start
        // at the next 1/16 if Run is enabled (or if ClockWhileStopped is allowed).
        processor.requestTriggerOnce();
    }
}

// Helpers
void ClockSyncAudioProcessorEditor::drawRing(juce::Graphics& g)
{
    if (ringArea.isEmpty()) return;
    const int cx = ringArea.getCentreX();
    const int cy = ringArea.getCentreY();

    // Reduce big ring diameter by 20px (10px radius) relative to kRingOuterD/InnerD.
    constexpr int outerShrink = 20;
    constexpr int innerShrink = 20;

    juce::Rectangle<float> outer(
        (float) (cx - (kRingOuterD - outerShrink) / 2),
        (float) (cy - (kRingOuterD - outerShrink) / 2),
        (float) (kRingOuterD - outerShrink),
        (float) (kRingOuterD - outerShrink));

    // Expand inner circle by 10px diameter (5px radius) compared to the previous shrink,
    // so the visible ring thickness is reduced by 5px.
    juce::Rectangle<float> inner(
        (float) (cx - (kRingInnerD - (innerShrink - 8)) / 2),
        (float) (cy - (kRingInnerD - (innerShrink - 8)) / 2),
        (float) (kRingInnerD - (innerShrink - 8)),
        (float) (kRingInnerD - (innerShrink - 8)));

    // Base ring
    g.setColour(UiThemeColours::accent()); g.fillEllipse(outer);
    g.setColour(UiThemeColours::base());   g.fillEllipse(inner);

    // Stop indicator
    if (! runParamCached)
    {
        juce::Graphics::ScopedSaveState ss(g);
        const float px = 1.0f / getDesktopScaleFactor();
        juce::Path clip; clip.addEllipse(inner.expanded(px)); g.reduceClipRegion(clip);

        const float barW = 18.0f;
        juce::Rectangle<float> bar(
            (float) cx - barW * 0.5f,
            (float) cy - inner.getHeight() * 0.5f,
            barW,
            inner.getHeight());

        g.addTransform(juce::AffineTransform::rotation(
            juce::MathConstants<float>::pi * 0.75f,
            (float) cx, (float) cy));

        g.setColour(UiThemeColours::accent()); g.fillRect(bar);
    }

    // Use the visualStepCached which is updated only when allowed by the
    // idleClockToggle or when the plugin is actually running. This allows
    // the wedge to freeze while background pulses continue.
    const int active = juce::jlimit(1, 16, visualStepCached);
    const float sliceAngle = juce::MathConstants<float>::twoPi / 16.0f;
    const float startAt12  = -juce::MathConstants<float>::halfPi;
    const int rotSlices    = 4;
    const float startAngle = startAt12 + (float) ((active - 1 + rotSlices) % 16) * sliceAngle;
    const float endAngle   = startAngle + sliceAngle;

    const float innerRatio = inner.getWidth() / outer.getWidth();

    juce::Path wedge;
    wedge.addPieSegment(outer, startAngle, endAngle, innerRatio);
    g.setColour(runParamCached ? UiThemeColours::cyan() : UiThemeColours::cyan().withAlpha(0.5f));
    g.fillPath(wedge);

    // Numbers hidden per request
}

static int mapRateIndexToPPQ(int idx)
{
    switch (idx)
    {
        case 0: return 48; // 1/32
        case 1: return 24; // 1/16
        case 2: return 12; // 1/8
        case 3: return 6;  // 1/4
        default: return 24;
    }
}

void ClockSyncAudioProcessorEditor::drawDancer(juce::Graphics& g)
{
    if (ringArea.isEmpty()) return;
    if (dancerFrames.empty()) return;

    int frameIdx = dancerLastFrame;
    const int count = (int) dancerFrames.size();

    if (runParamCached && count > 1)
    {
        const auto pulses = processor.getUiClockCounter();
        if (pulses != dancerLastDrawnClockCounter)
        {
            const int ppq = mapRateIndexToPPQ(rateIndexCached);
            const int pulsesPerTwoBeats = ppq * 2;
            if (pulsesPerTwoBeats > 0)
            {
                const int pInCycle = (int) (pulses % (unsigned long long) pulsesPerTwoBeats);
                frameIdx = (pInCycle * count) / pulsesPerTwoBeats;
                frameIdx = juce::jlimit(0, count - 1, frameIdx);
                dancerLastFrame = frameIdx;
                dancerLastDrawnClockCounter = pulses;
            }
        }
    }

    juce::Drawable* drawable = dancerFrames[(size_t) frameIdx].get();
    if (drawable == nullptr)
        return;

    juce::Rectangle<float> inner((float) (ringArea.getCentreX() - kRingInnerD / 2),
                                 (float) (ringArea.getCentreY() - kRingInnerD / 2),
                                 (float) kRingInnerD, (float) kRingInnerD);
    auto dest = inner.reduced(6.0f);

    // PNG layout scaling (original PNG canvas assumption kept for sizing math)
    constexpr float dancerCanvasW = 355.0f;
    constexpr float dancerCanvasH = 500.0f;
    const float sxFull = dest.getWidth()  / dancerCanvasW;
    const float syFull = dest.getHeight() / dancerCanvasH;
    const float scale = std::min(sxFull  * 0.9f, syFull * 0.9f);
    const float canvasScaledW = dancerCanvasW * scale;
    const float canvasScaledH = dancerCanvasH * scale;
    const float baseTx = dest.getCentreX() - canvasScaledW * 0.5f;
    const float baseTy = dest.getCentreY() - canvasScaledH * 0.5f;

    drawable->draw(g, 1.0f, juce::AffineTransform::scale(scale).translated(baseTx, baseTy));
}

void ClockSyncAudioProcessorEditor::drawTrigger(juce::Graphics& g)
{
    if (triggerRect.isEmpty()) return;
    juce::Colour base = UiThemeColours::accent();
    juce::Colour active = UiThemeColours::cyan();
    const float fadeNorm = juce::jlimit(0.0f, 1.0f, triggerFade);
    juce::Colour fill = base.interpolatedWith(active, fadeNorm);

    // Scale outward when freshly triggered (fadeNorm near 1), then ease back.
    const float scale = 0.833f + 0.166f * fadeNorm; // ≈1.0 at rest, ~1.0 + 0.166 when bright
    auto rfBase = triggerRect.toFloat();
    auto c = rfBase.getCentre();
    juce::Rectangle<float> rfScaled(c.x - rfBase.getWidth() * 0.5f * scale,
                                    c.y - rfBase.getHeight() * 0.5f * scale,
                                    rfBase.getWidth() * scale,
                                    rfBase.getHeight() * scale);

    // Outer scaled ellipse
    g.setColour(fill);
    g.fillEllipse(rfScaled);

    // Inner core shrinks proportionally so ring thickness stays visually similar
    g.setColour(UiThemeColours::base());
    g.fillEllipse(rfScaled.reduced((12.0f * scale - 2.0f) + 5.0f));

    // Step number centered; don't scale with trigger animation
    const int stepNow = juce::jlimit(1, 16, stepNumberCached > 0 ? stepNumberCached : processor.getUiStep16());
    g.setColour(UiThemeColours::cyan());
    g.setFont(juce::Font(juce::FontOptions("Arial", 33.0f, juce::Font::bold)));
    g.drawFittedText(juce::String(stepNow), rfBase.toNearestInt(), juce::Justification::centred, 1);
}

// Curved-label feature removed: no curved-label drawing function.

void ClockSyncAudioProcessorEditor::refreshDeviceList()
{
    midiOutputs.clear();
    auto arr = juce::MidiOutput::getAvailableDevices();
    for (auto& d : arr) midiOutputs.push_back(d);
    deviceBox.clear(juce::dontSendNotification);
    int idx = 1;
    deviceBox.addItem("Select...", idx++);
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
        // Do not pre-select the 'new...' action (id 1000) because selecting the same id
        // won't emit onChange. Instead show a placeholder and let the user select 'new...'
        // which will change the selected id and trigger the onChange handler.
        nameBox.setSelectedId(0, juce::dontSendNotification);
        // When there are no saved names show the actionable hint 'new...' so
        // users know they can add a name via the dropdown item.
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

// Curved-label editor removed.

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

// Curved-label editor removal: no-op.

// NOTE: PNG dancer frames are loaded here from BinaryData (dancer_0_png ... dancer_20_png).
// This function is still used (called in the editor constructor). Only SVG-related helpers were removed.
void ClockSyncAudioProcessorEditor::loadDancerFrames()
{
    dancerFrames.clear();
    dancerFrames.reserve(21);

    auto addFrame = [this](const void* data, int size)
    {
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
    dancerLastDrawnClockCounter = std::numeric_limits<unsigned long long>::max();
}


