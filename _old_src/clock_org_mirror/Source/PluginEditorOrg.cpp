#include <utility>
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "build_info.h"
#include "GridScaleMenu.h"
#include "BinaryData.h"
#include "LookAndFeels.h" // Use centralised LookAndFeel & theme colours
#include "Tooltips.h"
#include "UiLayoutConstants.h"
#include <array>
#include <optional>
#include "LayoutOffsets.h"

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

            // Help toggle (small '?' square) - visible by default off; toggles tooltips
            addAndMakeVisible(helpToggle);
            helpToggle.setButtonText("?");
            helpToggle.setClickingTogglesState(true);
            // text colours: cyan when enabled, slightly darker cyan when disabled
            helpToggle.setColour(juce::TextButton::textColourOffId, UiThemeColours::cyan().darker(0.45f));
            helpToggle.setColour(juce::TextButton::textColourOnId, UiThemeColours::cyan());
                    // Create a tiny LookAndFeel so the help button only draws text (no background/borders)
                    struct HelpBtnLNF : public juce::LookAndFeel_V4
                    {
                        void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override {}
                        void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override
                        {
                            const float fontSize = UiLayout::kFontMedium;
                            // Show colour based on toggle state so the button clearly
                            // indicates whether help/tooltips are enabled.
                            if (b.getToggleState())
                                g.setColour(b.findColour(juce::TextButton::textColourOnId));
                            else
                                g.setColour(b.findColour(juce::TextButton::textColourOffId));
                            g.setFont(juce::Font(juce::FontOptions("Arial", (float)fontSize, juce::Font::bold)));
                            g.drawFittedText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, 1);
                        }
                    };
                    helpButtonLNF = std::make_unique<HelpBtnLNF>();
                    helpToggle.setLookAndFeel(helpButtonLNF.get());
                    helpToggle.setRepeatSpeed(0, 0);
                    helpToggle.onClick = [this]() { applyTooltips(helpToggle.getToggleState()); };
                    // ensure the component background is not painted by the editor
                    helpToggle.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
            helpToggle.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);

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

    // Ensure the top-level tooltip window uses our ThemeLNF so drawTooltip is used
    tooltipWindow.setLookAndFeel(themeLNF.get());
    tooltipWindow.setColour(juce::TooltipWindow::backgroundColourId, UiThemeColours::base().withAlpha(0.666f));
    tooltipWindow.setColour(juce::TooltipWindow::textColourId, UiThemeColours::cyan());
    tooltipWindow.setColour(juce::TooltipWindow::outlineColourId, juce::Colours::transparentBlack);

    // Create a transient tooltip label (fallback for hosts that restrict
    // top-level transient windows). Hidden by default; shown by overlays.
    transientTooltip = std::make_unique<juce::Label>();
    transientTooltip->setVisible(false);
    transientTooltip->setJustificationType(juce::Justification::centredLeft);
    transientTooltip->setColour(juce::Label::backgroundColourId, UiThemeColours::base().withAlpha(0.85f));
    transientTooltip->setColour(juce::Label::textColourId, UiThemeColours::cyan());
    transientTooltip->setFont(juce::Font(juce::FontOptions("Arial", UiLayout::kFontTooltip, juce::Font::bold)));
    addAndMakeVisible(*transientTooltip);

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
    
    // Add soft drop shadows to a set of prominent controls for depth
    auto addDropShadowTo = [](juce::Component& c, int radius = 12, juce::Colour col = juce::Colours::black.withAlpha(0.4f), int xOff = 4, int yOff = 4)
    {
        auto eff = std::make_unique<juce::DropShadowEffect>();
        juce::DropShadow ds(col, radius, juce::Point<int>(xOff, yOff));
        eff->setShadowProperties(ds);
        // Component takes ownership of the raw pointer
        c.setComponentEffect(eff.release());
    };
    
    addDropShadowTo(gridScaleMenu);
    addDropShadowTo(shuffleModeMenu);
    addDropShadowTo(idleClockToggle, 8, juce::Colours::black.withAlpha(0.4f));
    addDropShadowTo(helpToggle, 4, juce::Colours::black.withAlpha(0.4f));
    addDropShadowTo(triggerModeToggle, 8, juce::Colours::black.withAlpha(0.4f));
    addDropShadowTo(clickButton, 4, juce::Colours::black.withAlpha(0.4f));
    addDropShadowTo(clickLevelSlider, 8, juce::Colours::black.withAlpha(0.4f));

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

    // Populate tooltip map (tooltips are off by default until helpToggle enabled)
    tooltipRegistry = getTooltipTextRegistry();
    tooltipMap.clear();
    // map components to registry keys (edit text in Source/Tooltips.h)
    tooltipMap.push_back({ &refreshButton, "refreshButton" });
    tooltipMap.push_back({ &deviceBox, "deviceBox" });
    tooltipMap.push_back({ &nameBox, "nameBox" });
    tooltipMap.push_back({ &nameMidiSwitch, "nameMidiSwitch" });
    tooltipMap.push_back({ &clickButton, "clickButton" });
    tooltipMap.push_back({ &clickLevelSlider, "clickLevelSlider" });
    tooltipMap.push_back({ &triggerModeToggle, "triggerModeToggle" });
    tooltipMap.push_back({ &idleClockToggle, "idleClockToggle" });
    tooltipMap.push_back({ &shuffleScaleToggle, "shuffleScaleToggle" });
    tooltipMap.push_back({ &stepOffsetMenu, "stepOffsetMenu" });
    tooltipMap.push_back({ &gridScaleMenu, "gridScaleMenu" });
    tooltipMap.push_back({ &shuffleModeMenu, "shuffleModeMenu" });
    tooltipMap.push_back({ &helpToggle, "helpToggle" });
    // Note: run/trigger areas are not native Components; create dedicated overlays below.
    // Create overlay tooltip components for items that don't implement
    // SettableTooltipClient (custom menus). The overlays are non-intercepting
    // so they won't block mouse interaction with the underlying controls.
    tooltipOverlays.clear();
    for (auto& p : tooltipMap)
    {
        if (p.first == nullptr) continue;
        const juce::String key = p.second;
        const juce::String text = tooltipRegistry.count(key) ? tooltipRegistry[key] : juce::String();
        // If the component doesn't implement SettableTooltipClient, add an overlay
        if (dynamic_cast<juce::SettableTooltipClient*>(p.first) == nullptr)
        {
            auto overlay = std::make_unique<OverlayTooltip>();
            overlay->setTooltip(text);
            overlay->setComponentID(key);
            addAndMakeVisible(*overlay);
            // ensure overlay stays above other child components
            overlay->setAlwaysOnTop(true);
            overlay->toFront(true);
            overlay->updateBoundsRelativeTo(p.first);
            tooltipOverlays.push_back(std::move(overlay));
        }
        else
        {
            // For native SettableTooltipClient components, assign tooltip text directly
            if (text.isNotEmpty())
                dynamic_cast<juce::SettableTooltipClient*>(p.first)->setTooltip(text);
        }
    }

    // Create overlays for non-component interactive regions: Run ring and Trigger area.
    // These overlays use componentID keys that correspond to entries in the tooltip registry.
    {
        auto overlayRun = std::make_unique<OverlayTooltip>();
        overlayRun->setComponentID("runButton");
        const juce::String runText = tooltipRegistry.count("runButton") ? tooltipRegistry["runButton"] : juce::String();
        overlayRun->setTooltip(runText);
        addAndMakeVisible(*overlayRun);
        overlayRun->setAlwaysOnTop(true);
        overlayRun->toFront(true);
        // Bounds are initially zero; resized() will place this overlay to `ringArea`.
        tooltipOverlays.push_back(std::move(overlayRun));

        auto overlayTrig = std::make_unique<OverlayTooltip>();
        overlayTrig->setComponentID("triggerArea");
        const juce::String trigText = tooltipRegistry.count("triggerArea") ? tooltipRegistry["triggerArea"] : juce::String();
        overlayTrig->setTooltip(trigText);
        addAndMakeVisible(*overlayTrig);
        overlayTrig->setAlwaysOnTop(true);
        overlayTrig->toFront(true);
        tooltipOverlays.push_back(std::move(overlayTrig));
    }

    // Tooltips inactive initially
    applyTooltips(false);

    // Drive visual animators from a VBlank-synchronised updater so animations
    // are in-step with the display refresh rate. Keep the 60Hz Timer for
    // non-animation tasks (host polling, parameter sync, tooltip timing, etc.).
    vblankUpdater = std::make_unique<juce::VBlankAnimatorUpdater>(this);
    vblankUpdater->addAnimator(triggerFadeAnimator);
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
        const auto& sizes = UiLayout::kBackdropSizes;
        for (size_t i = 0; i < sizes.size(); ++i)
        {
            const float baseSz = (float) sizes[i];
            const float darkFactor = 0.444f + 0.555f * (float) i; // bigger -> darker

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

            juce::Rectangle<float> rc(centre.x - sz * 0.5f, centre.y - sz * 0.5f + 5.0f + 5.0f , sz, sz);
            g.setColour(col);
            g.fillEllipse(rc);
        }
    }

    auto header = getLocalBounds().removeFromTop(30).reduced(8, 4).toFloat();
    juce::Path headerPath; headerPath.addRoundedRectangle(header, UiLayout::kCornerRadius);
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
    // Shift status text slightly right by shared offset
    g.drawFittedText(status, juce::Rectangle<int>(getWidth() - 73 + kHeaderShiftX, (int)header.getY(), 50, (int)header.getHeight()),
                     juce::Justification::centred, 1);

    const float ledRadius = UiLayout::kLedRadius;
    // Move LED slightly right by shared offset
    auto ledCenter = juce::Point<float>(getWidth() - 17.0f + kHeaderShiftX, header.getCentreY());
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
    const int sliderExtra = 15;
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
    // Keep the same top-left position but limit the interactive area to 100x100
    stepOffsetMenu.setBounds(195 + offX , 150 + offY, 120, 120);
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
    shuffleModeMenu.setBounds(102 + offX, 220 + offY, 155, 65);
    shuffleModeMenu.setManualBounds(kArcButtonRects);
    triggerModeToggle.toFront(true);
    shuffleModeMenu.toFront(true);
    stepOffsetMenu.toFront(true);

    // Help toggle position: lower-left 24x24
    const int helpSize = 25;
    helpToggle.setBounds(0, 210, helpSize, helpSize);

    // Keep any overlay tooltip components aligned with their targets
    for (auto& ov : tooltipOverlays)
    {
        if (! ov) continue;
        // find matching target from tooltipMap by comparing componentID keys
        for (auto& p : tooltipMap)
        {
            if (p.first == nullptr) continue;
            if (ov->getComponentID() == p.second)
            {
                ov->updateBoundsRelativeTo(p.first);
                // keep overlays above everything
                ov->setAlwaysOnTop(true);
                ov->toFront(true);
                break;
            }
        }
        // If the overlay corresponds to a non-component region (run/trigger),
        // position it explicitly using the cached rects.
        if (ov->getComponentID() == "runButton")
        {
            // Position overlay to cover the ring area
            if (! ringArea.isEmpty())
            {
                ov->setBounds(ringArea.getX(), ringArea.getY(), ringArea.getWidth(), ringArea.getHeight());
                ov->setAlwaysOnTop(true);
                ov->toFront(true);
            }
        }
        else if (ov->getComponentID() == "triggerArea")
        {
            if (! triggerRect.isEmpty())
            {
                ov->setBounds(triggerRect.getX(), triggerRect.getY(), triggerRect.getWidth(), triggerRect.getHeight());
                ov->setAlwaysOnTop(true);
                ov->toFront(true);
            }
        }
    }

    // Ensure transient tooltip sits above children but below native top-level
    if (transientTooltip)
    {
        transientTooltip->toFront(true);
        transientTooltip->setBounds(8, getHeight() - 48, getWidth() - 16, 36);
    }
}

void ClockSyncAudioProcessorEditor::applyTooltips(bool enabled)
{
    for (auto& p : tooltipMap)
    {
        if (p.first == nullptr) continue;
        const juce::String key = p.second;
        const juce::String text = tooltipRegistry.count(key) ? tooltipRegistry[key] : juce::String();
        if (auto* tc = dynamic_cast<juce::SettableTooltipClient*>(p.first))
        {
            if (enabled)
                tc->setTooltip(text);
            else
                tc->setTooltip(juce::String());
        }
        else
        {
            // for overlays, find the overlay by componentID and enable/disable its tooltip
            for (auto& ov : tooltipOverlays)
            {
                if (! ov) continue;
                if (ov->getComponentID() == key)
                {
                        if (enabled) {
                            ov->setTooltip(text);
                            ov->setAlwaysOnTop(true);
                            ov->toFront(true);
                        }
                        else {
                            ov->setTooltip(juce::String());
                        }
                        break;
                    }
            }
        }
    }

    // Also ensure overlays that represent non-component regions (run/trigger)
    // are enabled/disabled when tooltips are toggled.
    for (auto& ov : tooltipOverlays)
    {
        if (! ov) continue;
        const juce::String id = ov->getComponentID();
        if (id.isEmpty()) continue;
        const juce::String text = tooltipRegistry.count(id) ? tooltipRegistry[id] : juce::String();
        if (enabled)
        {
            ov->setTooltip(text);
            ov->setAlwaysOnTop(true);
            ov->toFront(true);
            ov->setVisible(true);
            // intercept mouse events so overlays get enter/move/exit to show tooltips;
            // clicks are forwarded by OverlayTooltip implementation so interaction remains.
            ov->setInterceptsMouseClicks(true, true);
        }
        else
        {
            ov->setTooltip(juce::String());
            ov->setVisible(false);
            ov->setInterceptsMouseClicks(false, true);
        }
    }

    // Hide transient tooltip when disabling tooltips
    if (! enabled && transientTooltip)
        transientTooltip->setVisible(false);
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
                                    if (backdropAnimators.size() > 1) backdropAnimators[1]->start();
                                    if (backdropAnimators.size() > 5) backdropAnimators[5]->start();
                                    break;
                                case 1:
                                    if (backdropAnimators.size() > 2) backdropAnimators[2]->start();
                                    if (backdropAnimators.size() > 5) backdropAnimators[5]->start();
                                    break;
                                case 2:
                                    if (backdropAnimators.size() > 3) backdropAnimators[3]->start();
                                    break;
                                case 3:
                                    if (backdropAnimators.size() > 4) backdropAnimators[4]->start();
                                    break;
                                default:
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
    // Trigger fade animator is now driven by the VBlankAnimatorUpdater; the
    // value-changed callback repaints the trigger area when necessary.

    // Update LED animator so pulses decay (ledAnimator callbacks repaint header)
    // LED animator is driven by the VBlankAnimatorUpdater; its callback
    // repaints the header when active.

    // Transient tooltip fade / auto-hide handling (driven from the editor timer)
    if (transientTooltip)
    {
        const unsigned long long now = juce::Time::getMillisecondCounter();
        // If the tooltip has passed its hide time, start fading out
        if (transientTooltipTargetAlpha > 0.0f && transientTooltipHideAt > 0 && now >= transientTooltipHideAt)
            transientTooltipTargetAlpha = 0.0f;

        // Smoothly approach target alpha
        const float fadeStep = 0.12f; // per-frame lerp towards target
        transientTooltipAlpha += (transientTooltipTargetAlpha - transientTooltipAlpha) * fadeStep;
        transientTooltipAlpha = juce::jlimit(0.0f, 1.0f, transientTooltipAlpha);
        transientTooltip->setAlpha(transientTooltipAlpha);

        if (transientTooltipAlpha > 0.001f)
            needAll = true; // ensure repaint while tooltip is visible/fading

        // When fully faded out, hide the component to avoid blocking inputs
        if (transientTooltipAlpha <= 0.001f && transientTooltip->isVisible())
        {
            transientTooltip->setVisible(false);
            transientTooltipHideAt = 0;
        }
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
                if (backdropSequencePos >= 0)
                {
                    const int idx = backdropSequencePos % (int)backdropAnimators.size();
                    if (idx >= 0 && idx < (int)backdropAnimators.size())
                        backdropAnimators[idx]->start();
                    if (backdropSequencePos == 4 && backdropAnimators.size() > 0)
                        backdropAnimators[0]->start();
                    needAll = true;
                }
            }
        }
    }

    // Decay backdrop pulse progress (frame-based) so pulses scale/fade back over time
    // Backdrop pulse decay is now driven by per-ring animators (VBlank-driven)

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

// Overlay tooltip mouse handlers (defined after timerCallback to keep header tidy)
void ClockSyncAudioProcessorEditor::OverlayTooltip::mouseEnter(const juce::MouseEvent& e)
{
    if (tooltipStr.isNotEmpty())
    {
        if (auto* p = dynamic_cast<ClockSyncAudioProcessorEditor*>(getParentComponent()))
            p->showTransientTooltip(tooltipStr, e.getScreenPosition());
    }
}

void ClockSyncAudioProcessorEditor::OverlayTooltip::mouseMove(const juce::MouseEvent& e)
{
    if (tooltipStr.isNotEmpty())
    {
        if (auto* p = dynamic_cast<ClockSyncAudioProcessorEditor*>(getParentComponent()))
            p->showTransientTooltip(tooltipStr, e.getScreenPosition());
    }
}

void ClockSyncAudioProcessorEditor::OverlayTooltip::mouseExit(const juce::MouseEvent&)
{
    if (auto* p = dynamic_cast<ClockSyncAudioProcessorEditor*>(getParentComponent()))
        p->hideTransientTooltip();
}

ClockSyncAudioProcessorEditor::~ClockSyncAudioProcessorEditor()
{
    // Ensure vblank updater is destroyed before member animators go away
    vblankUpdater.reset();
    clickLevelSlider.setLookAndFeel(nullptr);
    deviceBox.setLookAndFeel(nullptr);
    refreshButton.setLookAndFeel(nullptr);
    nameBox.setLookAndFeel(nullptr);
    nameMidiSwitch.setLookAndFeel(nullptr);
    // Clear tooltip look-and-feel to avoid dangling reference to themeLNF
    tooltipWindow.setLookAndFeel(nullptr);
    // Clear help button look-and-feel
    helpToggle.setLookAndFeel(nullptr);
}

void ClockSyncAudioProcessorEditor::showTransientTooltip(const juce::String& text, juce::Point<int> screenPos)
{
    if (! transientTooltip) return;
    if (! helpToggle.getToggleState()) return; // only show when help is enabled

    // First try to show the real TooltipWindow; some hosts prevent top-level
    // windows from appearing. If TooltipWindow becomes visible we won't show
    // the transient in-window label.
    tooltipWindow.displayTip(screenPos, text);
    if (tooltipWindow.isVisible())
    {
        // ensure our transient fallback is hidden
        transientTooltip->setVisible(false);
        transientTooltipAlpha = transientTooltipTargetAlpha = 0.0f;
        return;
    }

    // Fallback: use the editor transient label positioned near the cursor.
    transientTooltip->setText(text, juce::dontSendNotification);
    const juce::Point<int> local = getLocalPoint(nullptr, screenPos);
    const int w = std::min(getWidth() - 16, 360);
    const int h = 36;
    int x = juce::jlimit(8, getWidth() - w - 8, local.x + 12);
    int y = juce::jlimit(8, getHeight() - h - 8, local.y + 12);
    transientTooltip->setBounds(x, y, w, h);
    transientTooltipTargetAlpha = 1.0f;
    transientTooltipHideAt = juce::Time::getMillisecondCounter() + 3000; // auto-hide after 3s
    transientTooltip->setVisible(true);
    transientTooltip->toFront(true);
}

void ClockSyncAudioProcessorEditor::hideTransientTooltip()
{
    if (transientTooltip) transientTooltip->setVisible(false);
}

void ClockSyncAudioProcessorEditor::mouseUp(const juce::MouseEvent& e)
{
    // No-op: ringArea (run) toggling handled on mouseDown for more immediate response.

    // Curved-label feature removed: no click handling here.
}




void ClockSyncAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    // Toggle run when clicking within the inner black circle (ringArea) on mouseDown
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
            // If the click was inside the inner ring we don't want to treat it as a trigger region as well
            return;
        }
    }
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

        const float barW = UiLayout::kBarWidth;
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
    auto dest = inner.reduced(UiLayout::kInnerReduce);

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


