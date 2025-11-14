#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "GridScaleButton.h"
#include <array>

namespace
{
    // Theme colours
    const juce::Colour kAccent  = juce::Colour::fromRGB(0xFF, 0x4E, 0x5B); // FF4E5B
    const juce::Colour kBase    = juce::Colour::fromRGB(0x26, 0x26, 0x26); // 262626
    const juce::Colour kCyan    = juce::Colour::fromRGB(0x00, 0xD7, 0xFF); // 00D7FF
    const juce::Colour kCyan2   = juce::Colour(0xAA00D7FF); // 00D7FF with alpha
    const juce::Colour kBaseHi  = juce::Colour::fromRGB(0x33, 0x33, 0x33);
    const juce::Colour kBaseLo  = juce::Colour::fromRGB(0x1e, 0x1e, 0x1e);

    // Arc size button definitions (absolute positions from canvas origin)
    // Previously positioned algorithmically with baseY/xStart/gap; converted to fixed rects for easier manual tweaking.
    // Each rect: {x, y, w, h}. Derived from former layout: sizes {22,25,28,31,34,37,40} and y-offsets {0,16,22,26,28,26,16} added to baseY=215.
    // Visual shallow arc preserved. Adjust these directly as needed.
    // juce::Rectangle isn't a literal type; use static const (not constexpr)
    static const std::array<juce::Rectangle<int>, 7> kArcButtonRects = {
        juce::Rectangle<int>( 93, 221, 22, 22),
        juce::Rectangle<int>(107, 234, 25, 25),
        juce::Rectangle<int>(120, 242, 28, 28),
        juce::Rectangle<int>(138, 248, 31, 31),
        juce::Rectangle<int>(161, 249, 34, 34),
        juce::Rectangle<int>(182, 244, 37, 37),
        juce::Rectangle<int>(209, 230, 40, 40)
    };
}

class ClockSyncAudioProcessorEditor::ClickRotaryLNF : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider) override
    {
        juce::ignoreUnused(slider);
        auto bounds = juce::Rectangle<float>(x, y, width, height).reduced(4.0f);
        const float radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const juce::Point<float> centre(bounds.getCentre());
        const float ringThickness = 10.0f;

        // Accent ring (outer) with base inner fill
        g.setColour(kAccent);
        g.fillEllipse(bounds);
        g.setColour(kBase);
        g.fillEllipse(bounds.reduced(ringThickness));

        // Angle for needle
        // Rotate the dial by -90 degrees (subtract halfPi)
        const float angle = (rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle))
                    - juce::MathConstants<float>::halfPi;
        const float needleLen = radius - ringThickness * 0.5f - 2.0f;
        const float nx = centre.x + needleLen * std::cos(angle);
        const float ny = centre.y + needleLen * std::sin(angle);

        // Reverse colouring: accent -> cyan as value increases
        juce::Colour needleCol = kAccent.interpolatedWith(kCyan, sliderPosProportional);
        g.setColour(needleCol);
        g.drawLine(centre.x, centre.y, nx, ny, 3.0f);

        // Small hub
        g.setColour(kAccent);
        g.fillEllipse(centre.x - 5.0f, centre.y - 5.0f, 10.0f, 10.0f);
    }
};

class ClockSyncAudioProcessorEditor::ThemeLNF : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& /*bgColor*/, bool isHighlighted, bool isDown) override
    {
        auto b = button.getLocalBounds().toFloat();
        const float corner = 3.0f;
        // Default looks like a hover; down or toggled = blue
        juce::Colour fill = kAccent.brighter(0.15f);
        if (button.getToggleState() || isDown)
            fill = kCyan;
        else if (isHighlighted)
            fill = kAccent.brighter(0.25f);
        g.setColour(kBase);
        g.fillRoundedRectangle(b.translated(2.0f, 2.0f), corner);
        g.setColour(fill);
        g.fillRoundedRectangle(b, corner);
        // g.setColour(kBase);
        // g.drawRoundedRectangle(b, corner, 1.0f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setColour(kBase);
        g.setFont(juce::Font(juce::FontOptions("Arial", 11.0f, juce::Font::bold)));
        g.drawFittedText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, 1);
    }

    void drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        juce::ignoreUnused(width, height);
        auto r = box.getLocalBounds().toFloat();
        g.setColour(kAccent);
        g.fillRoundedRectangle(r, 5.0f);
        g.setColour(kBase);
        g.fillRoundedRectangle(r.reduced(2.0f), 4.0f);
         
        // g.drawRoundedRectangle(r, 5.0f, 1.0f);
    }

    juce::Font getPopupMenuFont() override
    {
        return juce::Font(juce::FontOptions("Arial", 11.0f, juce::Font::bold));
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override
    {
        return juce::Font(juce::FontOptions("Arial", 11.0f, juce::Font::bold));
    }

    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override
    {
        g.fillAll(kBase); // black-ish background
        g.setColour(kAccent);
        g.drawRect(0, 0, width, height, 1);
    }

    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive, bool isHighlighted, bool isTicked, bool /*hasSubMenu*/,
                            const juce::String& text, const juce::String& shortcutKeyText,
                            const juce::Drawable* /*icon*/, const juce::Colour* /*textColour*/) override
    {
        auto r = area.reduced(4);
        if (isSeparator)
        {
            g.setColour(kAccent.darker(0.2f));
            g.fillRect(r.removeFromTop(1));
            return;
        }

        // Highlight background red, otherwise black
        if (isHighlighted && isActive)
        {
            g.setColour(kAccent);
            g.fillRoundedRectangle(area.reduced(2).toFloat(), 3.0f);
        }

        // Text color: highlighted->black, else if ticked->blue, else red; disabled dims
        juce::Colour textCol;
        if (! isActive)
            textCol = kAccent.withAlpha(0.5f);
        else if (isHighlighted)
            textCol = juce::Colours::black;
        else if (isTicked)
            textCol = kCyan;
        else
            textCol = kAccent;

        g.setColour(textCol);
        g.setFont(getPopupMenuFont());

        auto textArea = r;
        g.drawFittedText(text, textArea, juce::Justification::centredLeft, 1);
        if (shortcutKeyText.isNotEmpty())
        {
            g.setColour(textCol.withAlpha(0.8f));
            g.drawFittedText(shortcutKeyText, r, juce::Justification::centredRight, 1);
        }
    }
};

ClockSyncAudioProcessorEditor::ClockSyncAudioProcessorEditor(ClockSyncAudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processor(p)
{
    setResizable(false, false);
    setSize(340, 300);
    startTimerHz(60); // smooth enough for LED/animations

    // Grid scale radial selector (replaces four rate buttons)
    gridScaleButton.setColours(kAccent, kBase, kCyan);
    addAndMakeVisible(gridScaleButton);
    // Keep grid selector visually behind the main ring
    gridScaleButton.toBack();

    // Arc size buttons (7 positions, slider-like) below the ring
    arcSizeButtons.setColours(kAccent, kBase, kCyan);
    addAndMakeVisible(arcSizeButtons);
    arcSizeButtons.setValue(shuffleValue);
    arcSizeButtons.onValueChanged = [this](int v)
    {
        shuffleValue = juce::jlimit(1, 7, v);
        // TODO: hook to processor parameter if needed
    };

    // Device selection UI
    addAndMakeVisible(deviceBox);
    addAndMakeVisible(refreshButton);

    // Click button (replaces label), styled and toggling
    addAndMakeVisible(clickButton);
    // Run toggle is replaced by clicking the ring; keepClockButton removed
    // addAndMakeVisible(runToggle);

    // Click level: rotary slider with accent ring and cyan->accent fading needle
    clickLevelSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    clickLevelSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    clickRotaryLNF = std::make_unique<ClickRotaryLNF>();
    clickLevelSlider.setLookAndFeel(clickRotaryLNF.get());
    clickLevelSlider.setRange(-12.0, 0.0, 0.1);
    addAndMakeVisible(clickLevelSlider);
    // Keep rotary visually behind the main ring
    clickLevelSlider.toBack();

    // Step offset selector (animated button) - configured after APVTS reference below

    auto& apvts = processor.getAPVTS();
    // Rate buttons manually control choice param
    // Attachments
    clickEnableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, ClockSyncAudioProcessor::paramClickEnable, clickButton);
    clickLevelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, ClockSyncAudioProcessor::paramClickLevelDb, clickLevelSlider);
    runAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, ClockSyncAudioProcessor::paramRun, runToggle);
    idleClockAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, ClockSyncAudioProcessor::paramClockWhileStopped, idleClockToggle);

    addAndMakeVisible(triggerModeToggle);
        // Ensure triggerModeToggle is on top of stepOffsetButton for proper click hit-testing when overlapping
   // triggerModeToggle.toFront(true);
        // Idle clock toggle
        addAndMakeVisible(idleClockToggle);
        idleClockToggle.setColours(kAccent, kBase, kCyan);
        idleClockToggle.setClickingTogglesState(true);
        // Keep this toggle visually behind others
        idleClockToggle.toBack();
    // Step offset selector (animated button)
    addAndMakeVisible(stepOffsetButton);
    stepOffsetButton.setColours(kAccent, kBase, kCyan);
    {
        int initialStep = 1;
        if (auto* pi = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter(ClockSyncAudioProcessor::paramResyncOffsetStep)))
            initialStep = pi->get();
        stepOffsetButton.setStep(initialStep);
    }
    // Keep step offset control visually on top among child components
    stepOffsetButton.toFront(true);
    stepOffsetButton.onStepChanged = [this](int step)
    {
        if (auto* p = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramResyncOffsetStep))
        {
            const auto& range = p->getNormalisableRange();
            const float norm = range.convertTo0to1((float) juce::jlimit(1, 16, step));
            p->beginChangeGesture();
            p->setValueNotifyingHost(norm);
            p->endChangeGesture();
        }
    };
    



    // Hide the run toggle in favour of ring click
    runToggle.setVisible(false);

    themeLNF = std::make_unique<ThemeLNF>();
    // Initialise cached param states for first paint
    if (auto* rp = apvts.getRawParameterValue(ClockSyncAudioProcessor::paramRun))
        runParamCached = rp->load() > 0.5f;
    rateParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(ClockSyncAudioProcessor::paramClockRateIndex));
    if (rateParam != nullptr)
        rateIndexCached = rateParam->getIndex();
    auto setRateIndex = [this](int idx)
    {
        if (auto* p = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramClockRateIndex))
            p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1((float) idx));
    };
    // Legacy individual rate buttons removed; radial control supersedes them
    // Style other themed buttons
    refreshButton.setLookAndFeel(themeLNF.get());
    clickButton.setLookAndFeel(themeLNF.get());
    clickButton.setClickingTogglesState(true);
    // Reflect initial selection
    gridScaleButton.setIndex(rateIndexCached); // reflect initial selection
    gridScaleButton.onGridChanged = [setRateIndex](int idx){ setRateIndex(idx); };

    // Wire up UI interactions not covered by attachments
    refreshButton.onClick = [this]
    {
        refreshDeviceList();
    };
    deviceBox.onChange = [this]
    {
        const int selId = deviceBox.getSelectedId();
        if (selId == 1)
        {
            processor.setExternalDeviceId({});
        }
        else if (selId > 1 && selId - 2 < (int) midiOutputs.size())
        {
            const auto& info = midiOutputs[(size_t) (selId - 2)];
            processor.setExternalDeviceId(info.identifier);
        }
    };
    deviceBox.setLookAndFeel(themeLNF.get());

    // runToggle hidden; clicking the ring handles start/stop

    // Initial device list
    refreshDeviceList();
}

void ClockSyncAudioProcessorEditor::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    // Background gradient
    juce::ColourGradient bgGrad(kBase, bounds.getTopLeft(), kBaseLo, bounds.getBottomLeft(), false);
    g.setGradientFill(bgGrad);
    g.fillRect(bounds);

    // Header bar
    auto header = getLocalBounds().removeFromTop(28).reduced(8, 4).toFloat();
    juce::Path headerPath; headerPath.addRoundedRectangle(header, 6.0f);
    g.setColour(kBase);
    g.fillPath(headerPath);
    // Label removed; header now hosts deviceBox + refreshButton components.

    // Small status text near the LED: STOP / ARM / RUN
    const bool isRunning = processor.getUiIsRunning();
    const bool isArmed = processor.getUiPendingStart();
    juce::String status = isRunning ? "RUN" : (isArmed ? "ARM" : "STOP");
    g.setFont(juce::Font(juce::FontOptions("Arial", 11.0f, juce::Font::bold)));
    g.setColour(isRunning ? kCyan : (isArmed ? kAccent : kAccent));
    g.drawFittedText(status, juce::Rectangle<int>(getWidth() - 80, (int)header.getY(), 50, (int)header.getHeight()), juce::Justification::centredRight, 1);

    // LED indicator (top-right)
    const float ledRadius = 6.0f;
    const auto ledCenter = juce::Point<float>(getWidth() - 18.0f, header.getCentreY());
    const float a = juce::jlimit(0.0f, 1.0f, ledLevel);
    juce::Colour ledOn = kCyan.withAlpha(0.97f);
    juce::Colour ledOff = kCyan.withAlpha(0.10f);
    auto ledColour = ledOff.interpolatedWith(ledOn, a);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.fillEllipse(ledCenter.x - ledRadius - 1.5f, ledCenter.y - ledRadius + 1.5f, ledRadius * 2.0f, ledRadius * 2.0f);
    g.setColour(ledColour);
    g.fillEllipse(ledCenter.x - ledRadius, ledCenter.y - ledRadius, ledRadius * 2.0f, ledRadius * 2.0f);
    g.setColour(juce::Colours::white.withAlpha(0.15f));
    g.drawEllipse(ledCenter.x - ledRadius, ledCenter.y - ledRadius, ledRadius * 2.0f, ledRadius * 2.0f, 1.0f);
    // No 'CLK' label per request

    // No ring, trigger or curved label here; those are drawn in paintOverChildren so the ring sits above child components and the trigger ellipse is top-most.
}

void ClockSyncAudioProcessorEditor::paintOverChildren(juce::Graphics& g)
{
    // Draw ring above children, but clip out a circular hole matching the stepOffsetButton's base ring
    {
        juce::Graphics::ScopedSaveState ss(g);
        juce::Path clip;
        clip.addRectangle(getLocalBounds().toFloat());
        // Circular hole centered on the stepOffsetButton with ~60px diameter (outer base), add small margin
        auto sb = stepOffsetButton.getBounds().toFloat();
        auto sc = sb.getCentre();
        const float holeD = 64.0f; // 60 + small margin
        juce::Rectangle<float> hole(sc.x - holeD * 0.5f, sc.y - holeD * 0.5f, holeD, holeD);
        clip.addEllipse(hole);
        clip.setUsingNonZeroWinding(false); // even-odd: ellipse becomes a hole
        g.reduceClipRegion(clip);
        drawRing(g);
    }
    // Curved label next so trigger ellipse can sit visually on top if overlapping.
    drawIdleClockCurvedLabel(g);
    // Draw trigger ellipse last to ensure it is front-most.
    drawTrigger(g);
}

void ClockSyncAudioProcessorEditor::resized()
{
    // Static absolute positions for fixed 300x300 canvas
    // Header area is painted, no components there (top 28px)

    // Rate buttons arranged vertically to match slider height (160px)
    // Grid scale radial button positioned on left side
    gridScaleButton.setBounds(5, 110, 120, 120);

    // Top toggles
    // runToggle hidden

    // Device combo + refresh button now inside header bar
    {
        const int headerH = 28;
        const int marginX = 8;
        const int marginY = 4;
        const int contentH = headerH - marginY * 2; // 20px
        const int buttonW = 40; // compact refresh button
        const int gap = 6; // retained gap but refresh stays at left
        const int fullComboOriginal = getWidth() - marginX * 2 - buttonW - gap; // previous wide combo width
        int comboW = fullComboOriginal - 60; // reduce width by 60 as requested
        comboW = std::max(100, comboW); // safety minimum (use std::max to avoid template deduction issues)
        const int centerX = getWidth() / 2;
        const int comboX = centerX - comboW / 2;
        // Place refresh ("reset") button on the left side of header
        refreshButton.setBounds(marginX + 3, marginY + 3, buttonW, contentH - 6);
        // Center combo box horizontally in header
        deviceBox.setBounds(comboX, marginY + 2, comboW, contentH - 4);
    }

    // Keep-clock button removed; curved label will be drawn around idleClockToggle

    // Click button above slider and slider on right
    clickButton.setBounds(250, 30, 40, 16);
    // Move rotary slider 200px left (x: 300 -> 100) and scale size 1.5x (40 -> 60)
    clickLevelSlider.setBounds(100, 45, 60, 60); // moved up by 10px
    // Step ring area (centered region for 160x160 ring + labels margin)
    ringArea = juce::Rectangle<int>(90, 90, 160, 160);
    // Step offset button area (expanded to avoid hover scaling clipping)
    // Place on right side, below ring and left of slider, with ample space for enlarged option circles
    // Move toggle down so it doesn't overlap triggerRect area
    triggerModeToggle.setBounds(247, 136, 45, 45);
    stepOffsetButton.setBounds(215, 155, 110 , 110);
        // Idle clock toggle near top-left
        idleClockToggle.setBounds(55, 75, 60, 60);
    // Declare click-through holes in stepOffsetButton so controls behind remain clickable when its menu is closed
    {
        auto holeToggle = stepOffsetButton.getLocalArea(&triggerModeToggle, triggerModeToggle.getLocalBounds());
        auto holeArcRow = stepOffsetButton.getLocalArea(&arcSizeButtons, arcSizeButtons.getLocalBounds());
        auto combined   = holeToggle.getUnion(holeArcRow);
        stepOffsetButton.setClickThroughRect(combined);
    }
    // Trigger circle in bottom-right corner, 6px margin
    triggerRect = juce::Rectangle<int>(220, 60, 85, 85);
    // Arc buttons area placed below ring area; allocate extra height to reduce overlap.
    // hitTest only captures inside circles, so it's safe if it overlaps other regions visually.
    // Arc size buttons: switch to manual absolute placement (fixed Y with subtle arc offsets)
    arcSizeButtons.setBounds(90, 215, 160, 80);
    // Use absolute canvas-origin rectangles (kArcButtonRects) instead of algorithmic layout.
    // Width is fixed (editor not resizable); no overflow adjustment needed.
    arcSizeButtons.setManualBounds(kArcButtonRects);
    // Ensure step offset button is at front among components
    stepOffsetButton.toFront(true);
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
        ledLevel *= 0.90f;
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
            gridScaleButton.setIndex(idx);
            needRing = true;
        }
    }

    // Trigger fade
    if (triggerFade > 0.01f)
    {
        triggerFade *= 0.92f;
        needTrigger = true;
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
        if (paramStep > 0 && paramStep != stepOffsetButton.getStep())
        {
            stepOffsetButton.setStep(paramStep);
            // no repaint flag needed; component repaints itself
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
}

void ClockSyncAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    // Only respond to mouse-down inside the trigger circle
    if (triggerRect.contains(e.getPosition()))
    {
        triggerFade = 1.0f; // turn blue immediately
        repaint(triggerRect);
        // Respect trigger mode: only arm restart when toggle is ON
        if (triggerModeToggle.getToggleState())
            processor.requestTriggerOnce();
    }
}

// Helpers
void ClockSyncAudioProcessorEditor::drawRing(juce::Graphics& g)
{
    if (ringArea.isEmpty()) return;
    const int cx = ringArea.getCentreX();
    const int cy = ringArea.getCentreY();
    juce::Rectangle<float> outer((float) (cx - kRingOuterD / 2), (float) (cy - kRingOuterD / 2), (float) kRingOuterD, (float) kRingOuterD);
    juce::Rectangle<float> inner((float) (cx - kRingInnerD / 2), (float) (cy - kRingInnerD / 2), (float) kRingInnerD, (float) kRingInnerD);

    // Base ring
    g.setColour(kAccent); g.fillEllipse(outer);
    g.setColour(kBase);   g.fillEllipse(inner);
    // No inner stroke; we handle edge cleanliness by slightly overlapping the stop bar into the ring

    // Stop indicator
    if (! runParamCached)
    {
        juce::Graphics::ScopedSaveState ss(g);
        // Expand clip by one device pixel so the bar can overlap the ring a hair, avoiding a dark AA seam
        const float px = 1.0f / getDesktopScaleFactor();
        juce::Path clip; clip.addEllipse(inner.expanded(px)); g.reduceClipRegion(clip);
        const float barW = 26.0f;
        juce::Rectangle<float> bar((float)cx - barW * 0.5f, (float)cy - (float)kRingInnerD, barW, (float)kRingInnerD * 2.0f);
        g.addTransform(juce::AffineTransform::rotation(juce::MathConstants<float>::pi * 0.75f, (float)cx, (float)cy));
        g.setColour(kAccent); g.fillRect(bar);
    }

    const int active = juce::jlimit(1, 16, processor.getUiStep16());
    const float sliceAngle = juce::MathConstants<float>::twoPi / 16.0f;
    const float startAt12 = -juce::MathConstants<float>::halfPi;
    const int rotSlices = 4;
    const float startAngle = startAt12 + (float) ((active - 1 + rotSlices) % 16) * sliceAngle;
    const float endAngle   = startAngle + sliceAngle;
    juce::Path wedge; wedge.addPieSegment(outer, startAngle, endAngle, (float) kRingInnerD / (float) kRingOuterD);
    g.setColour(runParamCached ? kCyan : kCyan.withAlpha(0.5f)); g.fillPath(wedge);

    // Numbers hidden per request
}

void ClockSyncAudioProcessorEditor::drawTrigger(juce::Graphics& g)
{
    if (triggerRect.isEmpty()) return;
    juce::Colour base = kAccent;
    juce::Colour active = kCyan;
    juce::Colour fill = base.interpolatedWith(active, juce::jlimit(0.0f, 1.0f, triggerFade));
    // Scale outward when freshly triggered (triggerFade near 1), then ease back as it fades.
    const float fadeNorm = juce::jlimit(0.0f, 1.0f, triggerFade);
    const float scale = 0.833f + 0.166f * fadeNorm; // 1.2 at full fade -> 1.0 when done
    auto rfBase = triggerRect.toFloat();
    auto c = rfBase.getCentre();
    juce::Rectangle<float> rfScaled(c.x - rfBase.getWidth() * 0.5f * scale,
                                    c.y - rfBase.getHeight() * 0.5f * scale,
                                    rfBase.getWidth() * scale,
                                    rfBase.getHeight() * scale);

    // Outer scaled ellipse (with subtle shadow optional commented)
    g.setColour(fill);
    g.fillEllipse(rfScaled);
    // Inner core shrinks proportionally so ring thickness stays visually similar
    g.setColour(kBase);
    // Further reduce by an additional 6px (radial) to make inner circle smaller
    g.fillEllipse(rfScaled.reduced((12.0f * scale - 2.0f) + 6.0f));

    // Step number centered; don't scale with trigger animation
    const int stepNow = juce::jlimit(1, 16, stepNumberCached > 0 ? stepNumberCached : processor.getUiStep16());
    g.setColour(kCyan);
    g.setFont(juce::Font(juce::FontOptions("Arial", 33.0f, juce::Font::bold)));
    g.drawFittedText(juce::String(stepNow), rfBase.toNearestInt(), juce::Justification::centred, 1);
   
}

void ClockSyncAudioProcessorEditor::drawIdleClockCurvedLabel(juce::Graphics& g)
{
    // Draw "IDLE CLOCK" text curved inside the accent ring of idleClockToggle, coloured kBase
    auto r = idleClockToggle.getBounds().toFloat();
    if (r.isEmpty()) return;

    // EllipseToggleButton draws:
    // outer accent disk (diameter d)
    // inner base disk reduced(10) => diameter d-20
    // We want the text centered in the accent ring: radius = outerRadius - 5
    const float d = std::min(r.getWidth(), r.getHeight());
    const juce::Point<float> c = r.getCentre();
    const float outerRadius = d * 0.5f;
    const float ringMidRadius = outerRadius - 5.0f; // middle of 10px thick accent ring

    const juce::String text = "IDLE CLOCK";
    const int n = text.length();
    if (n <= 0) return;

    // Ensure Arial Bold as requested
    juce::Font font(juce::FontOptions("Arial", 10.0f, juce::Font::bold));
    g.setColour(kBase);

    // Lay out characters along a vertical arc with widened kerning
    const float baseSpan = 1.30f; // radians (~74.5 deg) vertical spread
    const float kerningMultiplier = 1.25f; // widen spacing between letters
    const float totalSpan = baseSpan * kerningMultiplier;
    const float startAngle = -totalSpan * 0.5f; // centered about horizontal axis
    // Apply -90 degree rotation to the entire label layout
    const float rotationOffset = -juce::MathConstants<float>::halfPi; // -90 degrees
    const float delta = n > 1 ? (totalSpan / (float)(n - 1)) : 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const float a = startAngle + delta * (float)i;
        const float aRot = a + rotationOffset;
        const float gx = c.x + ringMidRadius * std::cos(aRot);
        const float gy = c.y + ringMidRadius * std::sin(aRot);

        juce::String ch = text.substring(i, i + 1);
        juce::GlyphArrangement ga; ga.addLineOfText(font, ch, 0.0f, 0.0f);
        juce::Path p; ga.createPath(p);
        auto pb = p.getBounds();
        p.applyTransform(juce::AffineTransform::translation(-pb.getCentreX(), -pb.getCentreY()));
        // Rotate so baseline is tangent to arc (add 90deg)
        auto T = juce::AffineTransform::rotation(aRot + juce::MathConstants<float>::halfPi)
                                     .translated(gx, gy);
        p.applyTransform(T);
        g.fillPath(p);
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
    

