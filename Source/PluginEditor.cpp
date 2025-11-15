#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "GridScaleButton.h"
#include "BinaryData.h" // embedded dancer SVG frames
#include "SvgUtils.h"   // layered SVG extraction
#include <array>

namespace
{
    // Theme colours
    const juce::Colour kAccent  = juce::Colour::fromRGB(0xFF, 0x4E, 0x5B); // FF4E5B
    const juce::Colour kBase    = juce::Colour::fromRGB(0x26, 0x26, 0x26); // 262626
    const juce::Colour kCyan    = juce::Colour::fromRGB(0x00, 0xD7, 0xFF); // 00D7FF
    const juce::Colour kBaseLo  = juce::Colour::fromRGB(0x1e, 0x1e, 0x1e);

    // Arc size button definitions (absolute positions from canvas origin)
    // Previously positioned algorithmically with baseY/xStart/gap; converted to fixed rects for easier manual tweaking.
    // Each rect: {x, y, w, h}. Derived from former layout: sizes {22,25,28,31,34,37,40} and y-offsets {0,16,22,26,28,26,16} added to baseY=215.
    // Visual shallow arc preserved. Adjust these directly as needed.
    // juce::Rectangle isn't a literal type; use static const (not constexpr)
    // Global offset for non-excluded components (exclude: reset/refreshButton, deviceBox, header, LED, status text)
    const int offX = -5;
    const int offY = -30;

    static const std::array<juce::Rectangle<int>, 7> kArcButtonRects = {
        juce::Rectangle<int>( 89 + offX, 216 + offY, 22, 22),
        juce::Rectangle<int>(102 + offX, 230 + offY, 25, 25),
        juce::Rectangle<int>(120 + offX, 242 + offY, 28, 28),
        juce::Rectangle<int>(138 + offX, 248 + offY, 31, 31),
        juce::Rectangle<int>(161 + offX, 249 + offY, 34, 34),
        juce::Rectangle<int>(182 + offX, 244 + offY, 37, 37),
        juce::Rectangle<int>(209 + offX, 230 + offY, 40, 40)
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
        const float ringThickness = 8.0f; // nominal ring width
        const float innerReduce   = ringThickness ;//- 2.5f; // enlarge inner ellipse by ~5px diameter

        // Accent ring (outer) with base inner fill
        g.setColour(kAccent);
        g.fillEllipse(bounds);
        g.setColour(kBase);
        g.fillEllipse(bounds.reduced(innerReduce));

        // Angle for needle
        // Rotate the dial by -45 degrees
        const float angle = (rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle))
                - (juce::MathConstants<float>::pi * 0.75f);
        const float needleLen = radius - innerReduce * 0.5f - 2.0f;
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
    setSize(320, 260);
    //setOpaque(false); // component is non-opaque, was incorrectly attempted via Graphics
    startTimerHz(60); // smooth enough for LED/animations

    // Grid scale radial selector (replaces four rate buttons)
    gridScaleButton.setColours(kAccent, kBase, kCyan);
    addAndMakeVisible(gridScaleButton);
    // Keep grid selector visually behind the main ring
    gridScaleButton.toBack();

    // Arc size buttons (7 positions, slider-like) below the ring
    arcSizeButtons.setColours(kAccent, kBase, kCyan);
    addAndMakeVisible(arcSizeButtons);
    // Shuffle arc buttons map 1..7 to processor paramShuffleStep
    arcSizeButtons.setValue(shuffleValue);
    arcSizeButtons.onValueChanged = [this](int v)
    {
        const int clamped = juce::jlimit(1, 7, v);
        shuffleValue = clamped;
        if (auto* p = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramShuffleStep))
        {
            const auto& range = p->getNormalisableRange();
            // Convert discrete 1..7 into 0..1 using underlying int param (min=1 max=7)
            p->beginChangeGesture();
            p->setValueNotifyingHost(range.convertTo0to1((float) clamped));
            p->endChangeGesture();
        }
    };

    // Device selection UI
    addAndMakeVisible(deviceBox);
    addAndMakeVisible(refreshButton);

    // Click toggle: small 10x10 ellipse in centre of rotary
    addAndMakeVisible(clickButton);

    // Click level: rotary slider with accent ring and cyan->accent fading needle
    clickLevelSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    clickLevelSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    clickRotaryLNF = std::make_unique<ClickRotaryLNF>();
    clickLevelSlider.setLookAndFeel(clickRotaryLNF.get());
    clickLevelSlider.setRange(-18.0, 0.0, 0.1);
    addAndMakeVisible(clickLevelSlider);
    // Keep rotary visually behind the main ring
    clickLevelSlider.toBack();

    // Step offset selector (animated button) - configured after APVTS reference below

    auto& apvts = processor.getAPVTS();
    // Rate buttons manually control choice param
    // Attachments
    clickEnableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, ClockSyncAudioProcessor::paramClickEnable, clickButton);
    clickLevelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, ClockSyncAudioProcessor::paramClickLevelDb, clickLevelSlider);
    // No hidden run toggle attachment; ring click toggles the Run parameter directly
    idleClockAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, ClockSyncAudioProcessor::paramClockWhileStopped, idleClockToggle);
    // Bind trigger mode toggle to APVTS
    triggerModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, ClockSyncAudioProcessor::paramTriggerModeEnabled, triggerModeToggle);

    addAndMakeVisible(triggerModeToggle);
        triggerModeToggle.setColours(kAccent, kBase, kCyan);
        triggerModeToggle.setClickingTogglesState(true);
        triggerModeToggle.setToggleState(true, juce::dontSendNotification); // default enabled
    // Ensure triggerModeToggle is on top of stepOffsetButton for proper click hit-testing when overlapping
   // triggerModeToggle.toFront(true);
        // Idle clock toggle
        addAndMakeVisible(idleClockToggle);
        idleClockToggle.setColours(kAccent, kBase, kCyan);
        idleClockToggle.setClickingTogglesState(true);
        // Keep this toggle visually behind others
        idleClockToggle.toBack();
    // Shuffle scale toggle
    addAndMakeVisible(shuffleScaleToggle);
    shuffleScaleToggle.setColours(kAccent, kBase, kCyan);
    shuffleScaleToggle.setClickingTogglesState(true);
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
        processor.notifyResyncOffsetChanged();
    };
    



    // No hidden run toggle; ring click handles Run

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
    clickButton.setColours(kAccent, kCyan);
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

    // Clicking the ring handles start/stop

    // Initial device list
    refreshDeviceList();
    // Optional: when enabling trigger mode, schedule restart recompute
    triggerModeToggle.onClick = [this]
    {
        const bool enabled = triggerModeToggle.getToggleState();
        if (enabled)
            processor.notifyResyncOffsetChanged();
    };

    // Attempt to load dancer animation (prefer single layered SVG, fallback to per-frame set)
    loadDancerFrames();
}
void ClockSyncAudioProcessorEditor::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    // Background gradient
    juce::ColourGradient bgGrad(kBase, bounds.getTopLeft(), kBaseLo, bounds.getBottomLeft(), false);
    g.setGradientFill(bgGrad);
    g.fillRect(bounds);
    // Removed invalid g.setOpaque(false); (Graphics has no such method)

    // Header bar
    auto header = getLocalBounds().removeFromTop(28).reduced(8, 4).toFloat();
    juce::Path headerPath; headerPath.addRoundedRectangle(header, 6.0f);
    g.setColour(kBase);
    g.fillPath(headerPath);
    // Label removed; header now hosts deviceBox + refreshButton components.

    // Small status text near the LED: STOP / ARM / RUN
    const bool isRunning = processor.getUiIsRunning();
    const bool isArmed = processor.getUiPendingStart();
    const bool hasNext = processor.getUiNextRestartPending();
    juce::String status = hasNext ? "NEXT" : (isRunning ? "RUN" : (isArmed ? "ARM" : "STOP"));
    g.setFont(juce::Font(juce::FontOptions("Arial", 11.0f, juce::Font::bold)));
    g.setColour(hasNext ? kCyan : (isRunning ? kCyan : (isArmed ? kAccent : kAccent)));
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
    // Draw ring above children, then explicitly repaint stepOffsetButton over it to ensure it's in front
    drawRing(g);
    // Draw dancer inside inner circle
    drawDancer(g);
    {
        juce::Graphics::ScopedSaveState ss(g);
        auto r = stepOffsetButton.getBounds().toFloat();
        g.reduceClipRegion(r.toNearestInt());
        g.setOrigin(stepOffsetButton.getPosition());
        stepOffsetButton.paintEntireComponent(g, true);
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
    drawIdleClockCurvedLabel(g);
    // Draw trigger ellipse last to ensure it is front-most.
    drawTrigger(g);
}

void ClockSyncAudioProcessorEditor::resized()
{
    // Static absolute positions for fixed 300x300 canvas
    // Header area is painted, no components there (top 28px)

    

    // Top toggles
    // Ring handles run toggling

    // Device combo + refresh button now inside header bar
    {
        const int headerH = 28;
        const int marginX = 5;
        const int marginY = 3;
        const int contentH = headerH - marginY * 2; // 20px
        const int buttonW = 40; // compact refresh button
        const int gap = 3; // retained gap but refresh stays at left
        const int fullComboOriginal = getWidth() - marginX * 2 - buttonW - gap; // previous wide combo width
        int comboW = fullComboOriginal - 80; // reduce width by 60 as requested
        comboW = std::max(100, comboW); // safety minimum (use std::max to avoid template deduction issues)
        const int centerX = getWidth() / 2;
        const int comboX = centerX - comboW / 2;
        // Place refresh ("reset") button on the left side of header
        refreshButton.setBounds(marginX + 3, marginY + 3, buttonW, contentH - 6);
        // Center combo box horizontally in header
        deviceBox.setBounds(comboX, marginY + 2, comboW, contentH - 4);
    }



    // Keep-clock button removed; curved label will be drawn around idleClockToggle

    // Click toggle centered within rotary slider
    // Move rotary slider 200px left (x: 300 -> 100) and scale size 1.5x (40 -> 60)
    clickLevelSlider.setBounds(78 + offX, 80 + offY, 46, 46); // rotary position (offset)
    {
        auto sb = clickLevelSlider.getBounds();
        auto c = sb.getCentre();
        clickButton.setBounds(c.x - 6, c.y - 6, 12, 12);
    }
    // Step ring area (centered region for 160x160 ring + labels margin)
    ringArea = juce::Rectangle<int>(90 + offX, 90 + offY, 160, 160);
    // Step offset button area (expanded to avoid hover scaling clipping)
    // Place on right side, below ring and left of slider, with ample space for enlarged option circles
    // Move toggle down so it doesn't overlap triggerRect area
    triggerModeToggle.setBounds(247 + offX, 136 + offY, 30, 30);
    stepOffsetButton.setBounds(223 + offX, 139 + offY, 100 , 100);
        // Idle clock toggle near top-left (offset)
        idleClockToggle.setBounds(114 + offX, 70 + offY, 30, 30);
        // Shuffle scale toggle at requested position (offset)
        shuffleScaleToggle.setBounds(69 + offX, 193 + offY, 30, 30);
    // Declare click-through holes in stepOffsetButton so controls behind remain clickable when its menu is closed
    {
        auto holeToggle = stepOffsetButton.getLocalArea(&triggerModeToggle, triggerModeToggle.getLocalBounds());
        auto holeArcRow = stepOffsetButton.getLocalArea(&arcSizeButtons, arcSizeButtons.getLocalBounds());
        auto combined   = holeToggle.getUnion(holeArcRow);
        stepOffsetButton.setClickThroughRect(combined);
    }
    // Rate buttons arranged vertically to match slider height (160px)
    // Grid scale radial button positioned on left side
    gridScaleButton.setBounds(2 + offX, 110 + offY, 120, 120);
    // Trigger circle in bottom-right corner, 6px margin
    triggerRect = juce::Rectangle<int>(220 + offX, 60 + offY, 85, 85);
    // Arc buttons area placed below ring area; allocate extra height to reduce overlap.
    // hitTest only captures inside circles, so it's safe if it overlaps other regions visually.
    // Arc size buttons: switch to manual absolute placement (fixed Y with subtle arc offsets)
    arcSizeButtons.setBounds(88 + offX, 215 + offY, 166, 80);
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
        ledLevel *= 0.5f;
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

    // Sync shuffle arc buttons with parameter (automation support & external automation)
    {
        int shuffleParam = -1;
        if (auto* si = dynamic_cast<juce::AudioParameterInt*>(processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramShuffleStep)))
            shuffleParam = juce::jlimit(1, 7, si->get());
        if (shuffleParam > 0 && shuffleParam != shuffleValue)
        {
            shuffleValue = shuffleParam;
            arcSizeButtons.setValue(shuffleValue);
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
        const float barW = 22.0f;
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
    if (dancerFrames.empty() || ringArea.isEmpty()) return;

    // Compute frame index from clock counter, scaled to 2 beats cycle length irrespective of PPQ
    const auto pulses = processor.getUiClockCounter();
    const int ppq = mapRateIndexToPPQ(rateIndexCached);
    const int pulsesPerTwoBeats = ppq * 2; // 2 quarters
    const int frames = dancerFrameCount;
    if (pulsesPerTwoBeats <= 0 || frames <= 0) return;
    const int pInCycle = (int) (pulses % (unsigned long long) pulsesPerTwoBeats);
    const int frameIdx = (pInCycle * frames) / pulsesPerTwoBeats; // integer scaling
    jassert(frameIdx >= 0 && frameIdx < frames);

    auto* drawable = dancerFrames[(size_t) frameIdx].get();
    if (drawable == nullptr) return;

    // Destination inner circle area minus margin
    juce::Rectangle<float> inner((float) (ringArea.getCentreX() - kRingInnerD / 2),
                                 (float) (ringArea.getCentreY() - kRingInnerD / 2),
                                 (float) kRingInnerD, (float) kRingInnerD);
    auto dest = inner.reduced(6.0f);

    // Stabilised canvas: original animation exported on 355x500 canvas.
    constexpr float dancerCanvasW = 355.0f;
    constexpr float dancerCanvasH = 500.0f;
    // Scale to fit entire nominal canvas into dest (use root SVG box, not per-layer bounds).
    const float sxFull = dest.getWidth() / dancerCanvasW;
    const float syFull = dest.getHeight() / dancerCanvasH;
    const float scale = std::min(sxFull, syFull);
    // Base translation to center full canvas.
    const float canvasScaledW = dancerCanvasW * scale;
    const float canvasScaledH = dancerCanvasH * scale;
    const float baseTx = dest.getCentreX() - canvasScaledW * 0.5f;
    const float baseTy = dest.getCentreY() - canvasScaledH * 0.5f;
    // Draw using the root canvas origin to prevent lateral jitter (no per-layer bounds alignment).
    drawable->draw(g, 1.0f, juce::AffineTransform::scale(scale).translated(baseTx, baseTy));
}

juce::File ClockSyncAudioProcessorEditor::findDancerFolder()
{
    // Start from the plugin binary folder and walk up looking for a child named "dancer"
    juce::File here = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
    juce::File dir = here.getParentDirectory();
    for (int i = 0; i < 8; ++i)
    {
        juce::File candidate = dir.getChildFile("dancer");
        if (candidate.isDirectory() && candidate.getChildFile("Dancer_01.svg").existsAsFile())
            return candidate;
        // also accept lowercase names
        if (candidate.exists() == false)
        {
            juce::File candidate2 = dir.getChildFile("Dancer_01.svg");
            if (candidate2.existsAsFile())
                return dir; // frames directly in this folder
        }
        dir = dir.getParentDirectory();
        if (! dir.exists()) break;
    }
    // Fallback: project root relative during development
    juce::File cwd = juce::File::getCurrentWorkingDirectory();
    juce::File fallback = cwd.getChildFile("dancer");
    if (fallback.isDirectory()) return fallback;
    return {};
}

void ClockSyncAudioProcessorEditor::scrubSvgColours(juce::XmlElement& el, juce::Colour accent)
{
    const juce::String hex = accent.toDisplayString(false);
    // Replace fill/stroke attributes if present
    if (el.hasAttribute("fill"))
    {
        auto v = el.getStringAttribute("fill");
        if (v.isNotEmpty() && v.compareIgnoreCase("none") != 0)
            el.setAttribute("fill", hex);
        else if (v.compareIgnoreCase("none") == 0)
            el.setAttribute("fill", hex); // force accent even if 'none'
    }
    else
    {
        // If no fill attribute, add one for drawable shape tags
        if (el.hasTagName("path") || el.hasTagName("rect") || el.hasTagName("circle") || el.hasTagName("ellipse") || el.hasTagName("polygon") || el.hasTagName("polyline"))
            el.setAttribute("fill", hex);
    }
    // Always set stroke if absent or not none
    if (el.hasAttribute("stroke"))
    {
        auto sv = el.getStringAttribute("stroke");
        if (sv.isNotEmpty() && sv.compareIgnoreCase("none") != 0)
            el.setAttribute("stroke", hex);
    }
    else
    {
        el.setAttribute("stroke", hex);
    }
    if (el.hasAttribute("style"))
    {
        auto style = el.getStringAttribute("style");
        if (style.isNotEmpty())
        {
            auto replaceEntry = [](juce::String s, const juce::String& key, const juce::String& val) -> juce::String
            {
                auto lower = s.toLowerCase();
                juce::String k = key.toLowerCase() + ":";
                int pos = lower.indexOf(k);
                if (pos < 0) return s;
                int end = s.indexOfChar(pos, ';');
                if (end < 0) end = s.length();
                juce::String replacement = key + ":" + val;
                // Keep trailing ';' if it existed
                if (end < s.length() && s[end] == ';')
                    replacement << ";";
                return s.replaceSection(pos, end - pos, replacement);
            };
            style = replaceEntry(style, "fill", hex);
            style = replaceEntry(style, "stroke", hex);
            el.setAttribute("style", style);
        }
    }
    forEachXmlChildElement(el, child)
        scrubSvgColours(*child, accent);
}

void ClockSyncAudioProcessorEditor::loadDancerFrames()
{
    dancerFrames.clear();
    // 1) Preferred path: load single layered SVG from embedded BinaryData
    {
        const char* ptr = BinaryData::dancer_neu_all_svg;
        const size_t sz = BinaryData::dancer_neu_all_svgSize;
        if (ptr != nullptr && sz > 0)
        {
            juce::String svgText = juce::String::fromUTF8(ptr, (int) sz);
            juce::XmlDocument doc(svgText);
            if (auto root = std::unique_ptr<juce::XmlElement>(doc.getDocumentElement()))
            {
                // Force kAccent colouring on the full document before splitting to layers
                scrubSvgColours(*root, kAccent);
                auto layers = SvgUtils::extractLayersAsDrawables(*root);
                if (! layers.empty())
                {
                    dancerFrames.reserve(layers.size());
                    for (auto& pair : layers)
                        dancerFrames.push_back(std::move(pair.second));
                    dancerFrameCount = (int) dancerFrames.size();
                    return; // success
                }
            }
        }
    }

    // 2) Fallback path: try layered SVG from filesystem (dev convenience)
    {
        juce::File folder = findDancerFolder();
        juce::File layered = folder.getChildFile("dancer_neu_all.svg");
        if (layered.existsAsFile())
        {
            juce::XmlDocument doc(layered);
            if (auto root = std::unique_ptr<juce::XmlElement>(doc.getDocumentElement()))
            {
                scrubSvgColours(*root, kAccent);
                auto layers = SvgUtils::extractLayersAsDrawables(*root);
                if (! layers.empty())
                {
                    dancerFrames.reserve(layers.size());
                    for (auto& pair : layers)
                        dancerFrames.push_back(std::move(pair.second));
                    dancerFrameCount = (int) dancerFrames.size();
                    return; // success
                }
            }
        }
    }

    // 3) Final fallback: load discrete frames (legacy path)
    {
        bool usedBinary = false;
        for (int i = 1; i <= dancerFrameCount; ++i)
        {
            const char* dataPtr = nullptr;
            size_t dataSize = 0;
            switch (i)
            {
                case 1:  dataPtr = BinaryData::Dancer_01_svg; dataSize = BinaryData::Dancer_01_svgSize; break;
                case 2:  dataPtr = BinaryData::Dancer_02_svg; dataSize = BinaryData::Dancer_02_svgSize; break;
                case 3:  dataPtr = BinaryData::Dancer_03_svg; dataSize = BinaryData::Dancer_03_svgSize; break;
                case 4:  dataPtr = BinaryData::Dancer_04_svg; dataSize = BinaryData::Dancer_04_svgSize; break;
                case 5:  dataPtr = BinaryData::Dancer_05_svg; dataSize = BinaryData::Dancer_05_svgSize; break;
                case 6:  dataPtr = BinaryData::Dancer_06_svg; dataSize = BinaryData::Dancer_06_svgSize; break;
                case 7:  dataPtr = BinaryData::Dancer_07_svg; dataSize = BinaryData::Dancer_07_svgSize; break;
                case 8:  dataPtr = BinaryData::Dancer_08_svg; dataSize = BinaryData::Dancer_08_svgSize; break;
                case 9:  dataPtr = BinaryData::Dancer_09_svg; dataSize = BinaryData::Dancer_09_svgSize; break;
                case 10: dataPtr = BinaryData::Dancer_10_svg; dataSize = BinaryData::Dancer_10_svgSize; break;
                case 11: dataPtr = BinaryData::Dancer_11_svg; dataSize = BinaryData::Dancer_11_svgSize; break;
                case 12: dataPtr = BinaryData::Dancer_12_svg; dataSize = BinaryData::Dancer_12_svgSize; break;
                case 13: dataPtr = BinaryData::Dancer_13_svg; dataSize = BinaryData::Dancer_13_svgSize; break;
                case 14: dataPtr = BinaryData::Dancer_14_svg; dataSize = BinaryData::Dancer_14_svgSize; break;
                case 15: dataPtr = BinaryData::Dancer_15_svg; dataSize = BinaryData::Dancer_15_svgSize; break;
                case 16: dataPtr = BinaryData::Dancer_16_svg; dataSize = BinaryData::Dancer_16_svgSize; break;
                case 17: dataPtr = BinaryData::Dancer_17_svg; dataSize = BinaryData::Dancer_17_svgSize; break;
                case 18: dataPtr = BinaryData::Dancer_18_svg; dataSize = BinaryData::Dancer_18_svgSize; break;
                case 19: dataPtr = BinaryData::Dancer_19_svg; dataSize = BinaryData::Dancer_19_svgSize; break;
                case 20: dataPtr = BinaryData::Dancer_20_svg; dataSize = BinaryData::Dancer_20_svgSize; break;
                case 21: dataPtr = BinaryData::Dancer_21_svg; dataSize = BinaryData::Dancer_21_svgSize; break;
                case 22: dataPtr = BinaryData::Dancer_22_svg; dataSize = BinaryData::Dancer_22_svgSize; break;
                case 23: dataPtr = BinaryData::Dancer_23_svg; dataSize = BinaryData::Dancer_23_svgSize; break;
                case 24: dataPtr = BinaryData::Dancer_24_svg; dataSize = BinaryData::Dancer_24_svgSize; break;
                default: break;
            }
            if (dataPtr != nullptr && dataSize > 0)
            {
                usedBinary = true;
                juce::String svgText = juce::String::fromUTF8(dataPtr, (int) dataSize);
                juce::XmlDocument doc(svgText);
                std::unique_ptr<juce::XmlElement> xml(doc.getDocumentElement());
                if (! xml) { dancerFrames.emplace_back(); continue; }
                scrubSvgColours(*xml, kAccent);
                std::unique_ptr<juce::Drawable> d(juce::Drawable::createFromSVG(*xml));
                dancerFrames.push_back(std::move(d));
                continue;
            }
            // Filesystem fallback per discrete frame
            juce::File folder = findDancerFolder();
            if (! folder.exists()) { dancerFrames.emplace_back(); continue; }
            juce::String name = juce::String::formatted("Dancer_%02d.svg", i);
            juce::File f = folder.getChildFile(name);
            if (! f.existsAsFile()) { dancerFrames.emplace_back(); continue; }
            std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument(f).getDocumentElement());
            if (! xml) { dancerFrames.emplace_back(); continue; }
            scrubSvgColours(*xml, kAccent);
            std::unique_ptr<juce::Drawable> d(juce::Drawable::createFromSVG(*xml));
            dancerFrames.push_back(std::move(d));
        }
        juce::ignoreUnused(usedBinary);
    }
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
    g.fillEllipse(rfScaled.reduced((12.0f * scale - 2.0f) + 5.0f));

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
    const float ringMidRadius = outerRadius - 4.0f; // middle of 10px thick accent ring

    const juce::String text = "IDLE CLOCK";
    const int n = text.length();
    if (n <= 0) return;

    // Ensure Arial Bold and widen kerning using FontOptions
    juce::FontOptions fontOpts("Arial", 8.0f, juce::Font::bold);
    fontOpts = fontOpts.withKerningFactor(3.3f); // widen spacing between letters
    juce::Font font(fontOpts);
    g.setColour(kBase);

    // Lay out characters along a vertical arc with widened kerning
    const float baseSpan = 3.3f; // radians (~74.5 deg) vertical spread
    const float kerningMultiplier = 1.20f; // mild extra angular spacing
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
    

