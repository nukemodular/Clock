#include <utility>
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "GridScaleMenu.h"
#include "BinaryData.h"
#include "LookAndFeels.h" // Use centralised LookAndFeel & theme colours
#include <array>

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
        clickLevelSlider.setRange(-18.0, 0.0, 0.1);

        addAndMakeVisible(gridScaleMenu);
        addAndMakeVisible(shuffleModeMenu);
        addAndMakeVisible(deviceBox);
        addAndMakeVisible(refreshButton);
        addAndMakeVisible(clickButton);
        addAndMakeVisible(clickLevelSlider);
        addAndMakeVisible(triggerModeToggle);
        addAndMakeVisible(idleClockToggle);
        addAndMakeVisible(shuffleScaleToggle);
        addAndMakeVisible(stepOffsetMenu);

        gridScaleMenu.toBack();
        clickLevelSlider.toBack();
        idleClockToggle.toBack();
        stepOffsetMenu.toFront(true);
    }

    auto& apvts = processor.getAPVTS();

    clickEnableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, ClockSyncAudioProcessor::paramClickEnable, clickButton);
    clickLevelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ClockSyncAudioProcessor::paramClickLevelDb, clickLevelSlider);
    idleClockAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, ClockSyncAudioProcessor::paramClockWhileStopped, idleClockToggle);
    triggerModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, ClockSyncAudioProcessor::paramTriggerModeEnabled, triggerModeToggle);

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
    // Make the combo box label use the accent colour but darker for contrast
    deviceBox.setColour(juce::ComboBox::textColourId, UiThemeColours::accent().darker(0.5f));

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

    // Decorative background circles: larger circles use a darker variant
    {
        auto centre = bounds.getCentre();
        const std::array<int, 6> sizes = { 420 ,340, 280, 230, 190, 160 };
        for (size_t i = 0; i < sizes.size(); ++i)
        {
            const float sz = (float) sizes[i];
            const float darkFactor = 0.11f + 0.66f * (float) i; // bigger -> darker
            //const float alpha = 0.02f + 0.1f * (float) i;      // subtle alpha increase
            juce::Colour col = UiThemeColours::accent().darker(darkFactor);//.withAlpha(alpha);
            juce::Rectangle<float> rc(centre.x - sz * 0.5f, centre.y - sz * 0.5f + 10, sz, sz);
            g.setColour(col);
            g.fillEllipse(rc);
        }
    }

    auto header = getLocalBounds().removeFromTop(28).reduced(18, 4).toFloat();
    juce::Path headerPath; headerPath.addRoundedRectangle(header, 6.0f);
    g.setColour(UiThemeColours::base());
    g.fillPath(headerPath);

    const bool isRunning = processor.getUiIsRunning();
    const bool isArmed = processor.getUiPendingStart();
    const bool hasNext = processor.getUiNextRestartPending();
    juce::String status = hasNext ? "NEXT" : (isRunning ? "RUN" : (isArmed ? "ARM" : "STOP"));
    g.setFont(juce::Font(juce::FontOptions("Arial", 11.0f, juce::Font::bold)));
    g.setColour(UiThemeColours::cyan());
    g.drawFittedText(status, juce::Rectangle<int>(getWidth() - 90, (int)header.getY(), 50, (int)header.getHeight()),
                     juce::Justification::centredRight, 1);

    const float ledRadius = 6.0f;
    auto ledCenter = juce::Point<float>(getWidth() - 28.0f, header.getCentreY());
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
    // Curved label next so trigger ellipse can sit visually on top if overlapping.
    //drawIdleClockCurvedLabel(g);
    // Draw trigger ellipse last to ensure it is front-most.
    drawTrigger(g);
}

void ClockSyncAudioProcessorEditor::resized()
{
    // Static absolute positions for fixed 300x300 canvas
    // Header area is painted, no components there (top 28px)

    {
        const int headerH = 28;
        const int marginX = 5;
        const int marginY = 3;
        const int contentH = headerH - marginY * 2; // 20px
        const int buttonW = 40; // compact refresh button
        const int gap = 3; // retained gap but refresh stays at left
        const int fullComboOriginal = getWidth() - marginX * 2 - buttonW - gap; // previous wide combo width
        int comboW = fullComboOriginal - 90; // reduce width by 60 as requested
        comboW = std::max(80, comboW); // safety minimum (use std::max to avoid template deduction issues)
        const int centerX = getWidth() / 2;
        const int comboX = centerX - comboW / 2;
        refreshButton.setBounds(marginX + 18, marginY + 5, buttonW, contentH - 10);
        deviceBox.setBounds(comboX, marginY + 2, comboW, contentH - 4);
    }

    clickLevelSlider.setBounds(75 + offX, 96 + offY, 46, 46); // rotary position (offset)
    {
        auto sb = clickLevelSlider.getBounds();
        auto c = sb.getCentre();
        clickButton.setBounds(c.x - 6, c.y - 6, 12, 12);
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
    
    // LED update
    const auto counter = processor.getUiClockCounter();
    if (counter != lastSeenClockCounter)
    {
        lastSeenClockCounter = counter;
        ledLevel = 1.0f;
        needAll = true; // header LED small; simplest to repaint all
    }
    else if (ledLevel > 0.01f)
    {
        ledLevel *= 0.33f;
        needAll = true;
    }

    // Run param / engine flags / pending start
    if (auto* rp = processor.getAPVTS().getRawParameterValue(ClockSyncAudioProcessor::paramRun))
    {
        const bool runNow = rp->load() > 0.5f;
        if (runNow != runParamCached) { runParamCached = runNow; needRing = true; }
    }
    const bool running = processor.getUiIsRunning();
    if (running != engineRunningCached) { engineRunningCached = running; needAll = true; }
    const bool armed = processor.getUiPendingStart();
    if (armed != pendingStartCached) { pendingStartCached = armed; needAll = true; }

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

    // Step number update tied to 16th-note changes (no fade)
    {
        const int stepNow = juce::jlimit(1, 16, processor.getUiStep16());
        if (stepNow != stepNumberCached)
        {
            stepNumberCached = stepNow;
            needTrigger = true;
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
    if (needTrigger) repaint(triggerRect);
}

ClockSyncAudioProcessorEditor::~ClockSyncAudioProcessorEditor()
{
    clickLevelSlider.setLookAndFeel(nullptr);
    deviceBox.setLookAndFeel(nullptr);
    refreshButton.setLookAndFeel(nullptr);
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

    // If click wasn't inside ring inner circle, check for label click along the bezier
    // Quadratic bezier control points used for the decorative curve (must match drawing)
    {
        const juce::Point<float> p0 (52.0f, 85.0f);
        const juce::Point<float> p1 (127.0f, 22.0f);
        const juce::Point<float> p2 (198.0f, 46.0f);
        const auto posf = juce::Point<float>((float)e.x, (float)e.y);
        // Quick bbox reject (inflated)
        float minx = std::min(std::min(p0.getX(), p1.getX()), p2.getX()) - 20.0f;
        float maxx = std::max(std::max(p0.getX(), p1.getX()), p2.getX()) + 20.0f;
        float miny = std::min(std::min(p0.getY(), p1.getY()), p2.getY()) - 20.0f;
        float maxy = std::max(std::max(p0.getY(), p1.getY()), p2.getY()) + 20.0f;
        if (posf.getX() >= minx && posf.getX() <= maxx && posf.getY() >= miny && posf.getY() <= maxy)
        {
            // sample bezier and find nearest distance
            const int samples = 40;
            float bestDist2 = std::numeric_limits<float>::max();
            for (int i = 0; i <= samples; ++i)
            {
                const float t = (float) i / (float) samples;
                const float mt = 1.0f - t;
                const juce::Point<float> sample = mt*mt * p0 + 2.0f * mt * t * p1 + t*t * p2;
                const float d2 = (sample.getX() - posf.getX())*(sample.getX() - posf.getX()) + (sample.getY() - posf.getY())*(sample.getY() - posf.getY());
                bestDist2 = std::min(bestDist2, d2);
            }
            const float thresh = 20.0f * 20.0f; // squared threshold
            if (bestDist2 <= thresh)
            {
                openCurvedLabelEditor();
                return;
            }
        }
    }
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
        // Always request trigger; processor decides bar restart based on trigger mode
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

    const int active = juce::jlimit(1, 16, processor.getUiStep16());
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

void ClockSyncAudioProcessorEditor::drawIdleClockCurvedLabel(juce::Graphics& g)
{
    if (ringArea.isEmpty()) return;
    // Subtle inner halo under the dancer
    juce::Rectangle<float> inner((float) (ringArea.getCentreX() - kRingInnerD / 2),
                                 (float) (ringArea.getCentreY() - kRingInnerD / 2),
                                 (float) kRingInnerD, (float) kRingInnerD);
    auto r = inner.reduced(18.0f);
    g.setColour(UiThemeColours::cyan().withAlpha(0.18f));
    g.fillEllipse(r);
    g.setColour(UiThemeColours::cyan().withAlpha(0.4f));
    g.drawEllipse(r, 1.5f);

    // Draw upward-bending decorative curve (start: 83,83  end:197,53)
    // {
    //     juce::Path curve;
    //     curve.startNewSubPath(82.0f, 85.0f);
    //     // Quadratic control point placed above the endpoints to bend upwards
    //     curve.quadraticTo(127.0f, 22.0f, 198.0f, 52.0f);
    //     g.setColour(UiThemeColours::accent());
    //     g.strokePath(curve, juce::PathStrokeType(3.0f));
    // }
    // Draw label following the same quadratic curve
    {
        const juce::String text = curvedLabelText;
        // Quadratic bezier control points (match the curve drawn above)
        const juce::Point<float> p0 (50.0f, 70.0f);
        const juce::Point<float> p1 (145.0f, 20.0f);
        const juce::Point<float> p2 (210.0f, 50.0f);

        juce::Font baseFont(juce::FontOptions("Arial", 40.0f, juce::Font::bold));
        g.setColour(UiThemeColours::cyan());

        //const float skewX = -0.05f; // shear (skew) in X
        const float shrinkFactor = 0.18f; // amount to shrink towards end (0..1)
        const float horizCompress = 0.66f; // horizontal compression (narrower text)

        const int n = (int) text.length();
        for (int i = 0; i < n; ++i)
        {
            // parameter t along curve [0..1], sample slightly offset so chars are distributed along curve
            const float t = (n == 1) ? 0.5f : (float) i / (float) (n - 1);

            // Quadratic Bezier point
            const float mt = 1.0f - t;
            const juce::Point<float> pos = mt*mt * p0 + 2.0f * mt * t * p1 + t*t * p2;

            // Tangent (derivative) to compute rotation
            const juce::Point<float> tangent = 2.0f * mt * (p1 - p0) + 2.0f * t * (p2 - p1);
            const float angle = std::atan2(tangent.y, tangent.x);

            // Compute per-character scale (shrink towards end non-linearly)
            const float s = 1.0f - shrinkFactor * std::sqrt(t);

            // Get glyph size for this character at base font
            const juce::String ch = text.substring(i, i+1);
            const float charW = baseFont.getStringWidthFloat(ch);
            const float charH = baseFont.getHeight();

            juce::Graphics::ScopedSaveState ss(g);

            // Build transform in local character space: horizontal compress, scale, shear, rotate, then translate
            juce::AffineTransform xf;
            xf = xf.scaled(s * horizCompress, s);
            //xf = xf.sheared(skewX, 0.0f);
            xf = xf.rotated(angle);
            xf = xf.translated(pos.getX(), pos.getY());

            g.addTransform(xf);

            // Draw the character centered at the origin
            g.setFont(baseFont);
            g.drawFittedText(ch, (int) -charW * 0.5f, (int) -charH * 0.5f,
                             (int) std::ceil(charW), (int) std::ceil(charH),
                             juce::Justification::centred, 1);
        }
    }
}

void ClockSyncAudioProcessorEditor::refreshDeviceList()
{
    midiOutputs.clear();
    auto arr = juce::MidiOutput::getAvailableDevices();
    for (auto& d : arr) midiOutputs.push_back(d);
    deviceBox.clear(juce::dontSendNotification);
    int idx = 1;
    deviceBox.addItem("None", idx++);
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

void ClockSyncAudioProcessorEditor::openCurvedLabelEditor()
{
    if (curvedLabelEditor) return; // already open

    // Quadratic bezier control points (same as used for drawing)
    const juce::Point<float> p0 (90.0f, 70.0f);
    const juce::Point<float> p1 (145.0f, 20.0f);
    const juce::Point<float> p2 (210.0f, 57.0f);

    const float t = 0.5f;
    const float mt = 1.0f - t;
    const juce::Point<float> pos = mt*mt * p0 + 2.0f * mt * t * p1 + t*t * p2;

    // Create editor and buttons
    curvedLabelEditor = std::make_unique<juce::TextEditor>("curvedEditor");
    curvedLabelEditor->setText(curvedLabelText);
    curvedLabelEditor->setFont(juce::Font(36.0f));
    curvedLabelEditor->setReturnKeyStartsNewLine(false);
    curvedLabelEditor->onReturnKey = [this]() { closeCurvedLabelEditor(true); };
    curvedLabelEditor->onEscapeKey = [this]() { closeCurvedLabelEditor(false); };
    addAndMakeVisible(*curvedLabelEditor);

    const int editorW = 220;
    const int editorH = 36;
    curvedLabelEditor->setBounds((int)pos.getX() - editorW/2, (int)pos.getY() - editorH/2, editorW, editorH);
    curvedLabelEditor->grabKeyboardFocus();

    curvedLabelOkButton = std::make_unique<juce::TextButton>("OK");
    curvedLabelOkButton->onClick = [this]() { closeCurvedLabelEditor(true); };
    addAndMakeVisible(*curvedLabelOkButton);
    curvedLabelOkButton->setBounds((int)pos.getX() + editorW/2 + 6, (int)pos.getY() - editorH/2, 40, editorH);

    curvedLabelCancelButton = std::make_unique<juce::TextButton>("Cancel");
    curvedLabelCancelButton->onClick = [this]() { closeCurvedLabelEditor(false); };
    addAndMakeVisible(*curvedLabelCancelButton);
    curvedLabelCancelButton->setBounds((int)pos.getX() + editorW/2 + 50, (int)pos.getY() - editorH/2, 60, editorH);
}

void ClockSyncAudioProcessorEditor::closeCurvedLabelEditor(bool commit)
{
    if (!curvedLabelEditor) return;
    if (commit)
        curvedLabelText = curvedLabelEditor->getText();

    curvedLabelEditor.reset();
    curvedLabelOkButton.reset();
    curvedLabelCancelButton.reset();
    repaint();
}

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


