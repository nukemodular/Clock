#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace
{
    // Theme colours
    const juce::Colour kAccent  = juce::Colour::fromRGB(0xFF, 0x4E, 0x5B); // FF4E5B
    const juce::Colour kBase    = juce::Colour::fromRGB(0x26, 0x26, 0x26); // 262626
    const juce::Colour kCyan    = juce::Colour::fromRGB(0x00, 0xD7, 0xFF); // 00D7FF
    const juce::Colour kBaseHi  = juce::Colour::fromRGB(0x33, 0x33, 0x33);
    const juce::Colour kBaseLo  = juce::Colour::fromRGB(0x1e, 0x1e, 0x1e);
}

class ClockSyncAudioProcessorEditor::SimpleSliderLNF : public juce::LookAndFeel_V4
{
public:
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
                          const juce::Slider::SliderStyle /*style*/, juce::Slider& slider) override
    {
        juce::ignoreUnused(slider);
        // Center a 12x150 background inside the given bounds
        const int bgW = 12;
        const int bgH = 100;
        const int trackW = 6;
        const int trackH = 94;
        const int knobW = 6;
        const int knobH = 9;

        const int cx = x + width / 2;
        const int cy = y + height / 2;
        juce::Rectangle<int> bgRect(cx - bgW / 2, cy - bgH / 2, bgW, bgH);
        // Background rounded rect
        g.setColour(kAccent);
        g.fillRoundedRectangle(bgRect.toFloat(), 6.0f);

        // Track centered within background
        juce::Rectangle<int> trackRect(bgRect.getCentreX() - trackW / 2,
                                       bgRect.getY() + (bgRect.getHeight() - trackH) / 2,
                                       trackW, trackH);
        g.setColour(kBase);
        g.fillRoundedRectangle(trackRect.toFloat(), 3.0f);

        // sliderPos is a y position in pixels; clamp to bgRect
        const int knobCenterY = juce::jlimit(bgRect.getY(), bgRect.getBottom(), (int) std::round(sliderPos));
        juce::Rectangle<int> knobRect(trackRect.getCentreX() - knobW / 2,
                                      juce::jlimit(trackRect.getY(), trackRect.getBottom() - knobH, knobCenterY - knobH / 2),
                                      knobW, knobH);
        g.setColour(kCyan);
        g.fillRoundedRectangle(knobRect.toFloat(), 3.0f);
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
        g.setColour(fill);
        g.fillRoundedRectangle(b, corner);
        g.setColour(kBase);
        g.drawRoundedRectangle(b, corner, 1.0f);
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
        g.setColour(kBase);
        g.fillRoundedRectangle(r, 3.0f);
        g.setColour(kAccent);
        g.drawRoundedRectangle(r, 5.0f, 1.0f);
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

    // Rate selection (radio buttons)
    // Rate buttons (vertical stack on the left; label removed)
    addAndMakeVisible(rateBtn32);
    addAndMakeVisible(rateBtn16);
    addAndMakeVisible(rateBtn8);
    addAndMakeVisible(rateBtn4);
    const int groupId = 1001;
    rateBtn32.setRadioGroupId(groupId);
    rateBtn16.setRadioGroupId(groupId);
    rateBtn8.setRadioGroupId(groupId);
    rateBtn4.setRadioGroupId(groupId);

    // Device selection UI
    addAndMakeVisible(deviceBox);
    addAndMakeVisible(refreshButton);

    // Click button (replaces label), styled and toggling
    addAndMakeVisible(clickButton);
    // Run and keep-clock toggles
    // runToggle is replaced by clicking the ring; keep parameter/attachment but hide the control
    // addAndMakeVisible(runToggle);
    addAndMakeVisible(keepClockButton);

    // Click level: styled simple vertical slider
    clickLevelSlider.setSliderStyle(juce::Slider::LinearVertical);
    clickLevelSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    clickLNF = std::make_unique<SimpleSliderLNF>();
    clickLevelSlider.setLookAndFeel(clickLNF.get());
    clickLevelSlider.setRange(-12.0, 0.0, 0.1);
    addAndMakeVisible(clickLevelSlider);

    // Step offset selector (animated button) - configured after APVTS reference below

    auto& apvts = processor.getAPVTS();
    // Rate buttons manually control choice param
    // Attachments
    clickEnableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, ClockSyncAudioProcessor::paramClickEnable, clickButton);
    clickLevelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, ClockSyncAudioProcessor::paramClickLevelDb, clickLevelSlider);
    runAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, ClockSyncAudioProcessor::paramRun, runToggle);
    keepClockAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, ClockSyncAudioProcessor::paramClockWhileStopped, keepClockButton);

    // Step offset selector (animated button)
    addAndMakeVisible(stepOffsetButton);
    stepOffsetButton.setColours(kAccent, kBase, kCyan);
    {
        int initialStep = 1;
        if (auto* pi = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter(ClockSyncAudioProcessor::paramResyncOffsetStep)))
            initialStep = pi->get();
        stepOffsetButton.setStep(initialStep);
    }
    stepOffsetButton.onStepChanged = [this](int step)
    {
        if (auto* p = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramResyncOffsetStep))
        {
            if (auto* pi = dynamic_cast<juce::AudioParameterInt*>(p))
            {
                pi->beginChangeGesture();
                // AudioParameterInt::setValueNotifyingHost expects raw value for ints in JUCE 7
                pi->setValueNotifyingHost((float) juce::jlimit(1, 16, step));
                pi->endChangeGesture();
            }
            else
            {
                const auto& range = p->getNormalisableRange();
                const float norm = range.convertTo0to1((float) juce::jlimit(1, 16, step));
                p->setValueNotifyingHost(norm);
            }
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
    for (auto* b : { &rateBtn32, &rateBtn16, &rateBtn8, &rateBtn4 })
    {
        addAndMakeVisible(*b);
        b->setLookAndFeel(themeLNF.get());
        b->setClickingTogglesState(false);
    }
    // Style other themed buttons
    refreshButton.setLookAndFeel(themeLNF.get());
    keepClockButton.setLookAndFeel(themeLNF.get());
    keepClockButton.setClickingTogglesState(true);
    clickButton.setLookAndFeel(themeLNF.get());
    clickButton.setClickingTogglesState(true);
    // Reflect initial selection
    rateBtn32.setToggleState(rateIndexCached == 0, juce::dontSendNotification);
    rateBtn16.setToggleState(rateIndexCached == 1, juce::dontSendNotification);
    rateBtn8.setToggleState (rateIndexCached == 2, juce::dontSendNotification);
    rateBtn4.setToggleState (rateIndexCached == 3, juce::dontSendNotification);
    rateBtn32.onClick = [setRateIndex]{ setRateIndex(0); };
    rateBtn16.onClick = [setRateIndex]{ setRateIndex(1); };
    rateBtn8.onClick  = [setRateIndex]{ setRateIndex(2); };
    rateBtn4.onClick  = [setRateIndex]{ setRateIndex(3); };

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

    // Background gradient using base tones
    g.setGradientFill(juce::ColourGradient(kBaseHi, bounds.getCentreX(), bounds.getY(), kBaseLo, bounds.getCentreX(), bounds.getBottom(), false));
    g.fillAll();

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

    drawRing(g);

    drawTrigger(g);
}

void ClockSyncAudioProcessorEditor::resized()
{
    // Static absolute positions for fixed 300x300 canvas
    // Header area is painted, no components there (top 28px)

    // Rate buttons arranged vertically to match slider height (160px)
    {
        const int x = 8;
        const int top = 90;
        const int totalH = 160;
        const int btnH = 30;
        const int gaps = 3;
        const int gap = (totalH - 4 * btnH) / gaps; // 13
        rateBtn32.setBounds(x, top, 30, btnH);
        rateBtn16.setBounds(x, top + btnH + gap, 30, btnH);
        rateBtn8.setBounds (x, top + (btnH + gap) * 2, 30, btnH);
        rateBtn4.setBounds (x, top + (btnH + gap) * 3, 30, btnH);
    }

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

    // Keep-clock button remains below header
    keepClockButton.setBounds(11, 30, 60, 16);

    // Click button above slider and slider on right
    clickButton.setBounds(250, 30, 40, 16);
    clickLevelSlider.setBounds(302, 30, 40, 100);

    // Step offset button area (allows room for animated options)
    // Place on right side below trigger; component is larger than base (for animation), base remains centred inside
    stepOffsetButton.setBounds(230, 180, 100, 100);

    // Step ring area (centered region for 160x160 ring + labels margin)
    ringArea = juce::Rectangle<int>(90, 90, 160, 160);

    // Trigger circle in bottom-right corner, 6px margin
    triggerRect = juce::Rectangle<int>(240, 70, 70, 70);
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
            rateBtn32.setToggleState(idx == 0, juce::dontSendNotification);
            rateBtn16.setToggleState(idx == 1, juce::dontSendNotification);
            rateBtn8.setToggleState (idx == 2, juce::dontSendNotification);
            rateBtn4.setToggleState (idx == 3, juce::dontSendNotification);
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
    for (auto* b : { &rateBtn32, &rateBtn16, &rateBtn8, &rateBtn4 }) b->setLookAndFeel(nullptr);
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
        const float barW = 28.0f;
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
    auto rf = triggerRect.toFloat();
    // g.setColour(juce::Colours::black.withAlpha(0.5f));
    // g.fillEllipse(rf.withX(rf.getX() - 1.5f).withY(rf.getY() + 1.5f));
    g.setColour(fill); g.fillEllipse(rf);
    g.setColour(kBase); //g.drawEllipse(rf, 1.5f);
    g.fillEllipse(rf.reduced(12.0f));
    // Cyan step number (1-16), centred inside triggerRect
    const int stepNow = juce::jlimit(1, 16, stepNumberCached > 0 ? stepNumberCached : processor.getUiStep16());
    g.setColour(kCyan);
    g.setFont(juce::Font(juce::FontOptions("Arial", 33.0f, juce::Font::bold)));
    g.drawFittedText(juce::String(stepNow), triggerRect, juce::Justification::centred, 1);
   
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
    

