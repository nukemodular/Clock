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
        const int bgH = 150;
        const int trackW = 6;
        const int trackH = 144;
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
        const float corner = 5.0f;
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
        g.setFont(juce::Font(juce::FontOptions("Arial", 13.0f, juce::Font::bold)));
        g.drawFittedText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, 1);
    }

    void drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        juce::ignoreUnused(width, height);
        auto r = box.getLocalBounds().toFloat();
        g.setColour(kBase);
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(kAccent);
        g.drawRoundedRectangle(r, 4.0f, 1.0f);
    }

    juce::Font getPopupMenuFont() override
    {
        return juce::Font(juce::FontOptions("Arial", 13.0f, juce::Font::bold));
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
    setSize(300, 300);
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

    auto& apvts = processor.getAPVTS();
    // Rate buttons manually control choice param
    // Attachments
    clickEnableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, ClockSyncAudioProcessor::paramClickEnable, clickButton);
    clickLevelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, ClockSyncAudioProcessor::paramClickLevelDb, clickLevelSlider);
    runAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, ClockSyncAudioProcessor::paramRun, runToggle);
    keepClockAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, ClockSyncAudioProcessor::paramClockWhileStopped, keepClockButton);

    // Hide the run toggle in favour of ring click
    runToggle.setVisible(false);

    themeLNF = std::make_unique<ThemeLNF>();
    // Initialise cached param states for first paint
    if (auto* rp = apvts.getRawParameterValue(ClockSyncAudioProcessor::paramRun))
        runParamCached = rp->load() > 0.5f;
    if (auto* cp = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(ClockSyncAudioProcessor::paramClockRateIndex)))
        rateIndexCached = cp->getIndex();
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
    g.setColour(kAccent);
    g.setFont(juce::Font(juce::FontOptions("Arial", 18.0f, juce::Font::bold)));
    g.drawFittedText("ClockSync", header.toNearestInt(), juce::Justification::centred, 1);

    // Small status text near the LED: STOP / ARM / RUN
    const bool isRunning = processor.getUiIsRunning();
    const bool isArmed = processor.getUiPendingStart();
    juce::String status = isRunning ? "RUN" : (isArmed ? "ARM" : "STOP");
    g.setFont(juce::Font(juce::FontOptions("Arial", 11.0f, juce::Font::bold)));
    g.setColour(isRunning ? kCyan : (isArmed ? kAccent : kAccent));
    g.drawFittedText(status, juce::Rectangle<int>(getWidth() - 90, (int)header.getY(), 50, (int)header.getHeight()), juce::Justification::centredRight, 1);

    // LED indicator (top-right)
    const float ledRadius = 6.0f;
    const auto ledCenter = juce::Point<float>(getWidth() - 18.0f, header.getCentreY());
    const float a = juce::jlimit(0.0f, 1.0f, ledLevel);
    juce::Colour ledOn = kCyan.withAlpha(0.95f);
    juce::Colour ledOff = kCyan.withAlpha(0.20f);
    auto ledColour = ledOff.interpolatedWith(ledOn, a);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.fillEllipse(ledCenter.x - ledRadius - 1.5f, ledCenter.y - ledRadius + 1.5f, ledRadius * 2.0f, ledRadius * 2.0f);
    g.setColour(ledColour);
    g.fillEllipse(ledCenter.x - ledRadius, ledCenter.y - ledRadius, ledRadius * 2.0f, ledRadius * 2.0f);
    g.setColour(juce::Colours::white.withAlpha(0.15f));
    g.drawEllipse(ledCenter.x - ledRadius, ledCenter.y - ledRadius, ledRadius * 2.0f, ledRadius * 2.0f, 1.0f);
    // No 'CLK' label per request

    // Step ring visualisation (16 slices), centered in ringArea
    if (! ringArea.isEmpty())
    {
        const int outerD = 160;
        const int innerD = 120;
        const int cx = ringArea.getCentreX();
        const int cy = ringArea.getCentreY();

        juce::Rectangle<float> outer((float) (cx - outerD / 2), (float) (cy - outerD / 2), (float) outerD, (float) outerD);
        juce::Rectangle<float> inner((float) (cx - innerD / 2), (float) (cy - innerD / 2), (float) innerD, (float) innerD);

        // Base ring: red outer, black inner
        g.setColour(kAccent);
        g.fillEllipse(outer);
        g.setColour(kBase);
        g.fillEllipse(inner);
        // Add a thin red frame around the inner circle to hide seam with outer ring
        g.setColour(kAccent);
        g.drawEllipse(inner, 2.0f);

        // Stop indicator: diagonal red bar when stopped
        if (! runParamCached)
        {
            juce::Graphics::ScopedSaveState ss(g);
            // Clip to inner circle
            juce::Path clip; clip.addEllipse(inner);
            g.reduceClipRegion(clip);
            // Draw a wider bar through the centre, rotated 135 degrees (turned 90 degrees from prior)
            const float barW = 28.0f;
            juce::Rectangle<float> bar((float)cx - barW * 0.5f, (float)cy - (float)innerD, barW, (float)innerD * 2.0f);
            g.addTransform(juce::AffineTransform::rotation(juce::MathConstants<float>::pi * 0.75f, (float)cx, (float)cy));
            g.setColour(kAccent);
            g.fillRect(bar);
        }

        // Active slice (1..16), 12 o'clock is slice 1
        const int active = juce::jlimit(1, 16, processor.getUiStep16());
        const float sliceAngle = juce::MathConstants<float>::twoPi / 16.0f;
        const float startAt12 = -juce::MathConstants<float>::halfPi; // 12 o'clock baseline
        // Apply a +4-slice rotation to correct observed -4-slice offset (move 9 o'clock -> 12 o'clock)
        const int rotSlices = 4; // clockwise rotation by 90 degrees
        const float startAngle = startAt12 + (float) ((active - 1 + rotSlices) % 16) * sliceAngle;
        const float endAngle   = startAngle + sliceAngle;

        juce::Path wedge;
        // innerCircleProportion = innerRadius / outerRadius = 60/80 = 0.75
        wedge.addPieSegment(outer, startAngle, endAngle, 0.75f);
        g.setColour(runParamCached ? kCyan : kCyan.withAlpha(0.5f));
        g.fillPath(wedge);

        // Numbers 1..16 around the ring
        g.setColour(kAccent);
        g.setFont(juce::Font(juce::FontOptions("Arial", 11.0f, juce::Font::bold)));
        const float numRadius = (float) outerD * 0.5f + 10.0f; // a little outside the ring
        for (int i = 0; i < 16; ++i)
        {
            const int label = i + 1;
            // Place label 1 exactly at 12 o'clock, then 2,3,... clockwise on slice boundaries
            const float angMid = startAt12 + (float) i * sliceAngle;
            const float tx = (float) cx + numRadius * std::cos(angMid);
            const float ty = (float) cy + numRadius * std::sin(angMid);
            juce::Rectangle<int> tb((int) tx - 10, (int) ty - 7, 20, 14);
            g.drawFittedText(juce::String(label), tb, juce::Justification::centred, 1);
        }
    }

    // Trigger circle (50x50): red -> blue on click, then fades back to red
    if (! triggerRect.isEmpty())
    {
        juce::Colour base = kAccent; // red
        juce::Colour active = kCyan; // blue
        juce::Colour fill = base.interpolatedWith(active, juce::jlimit(0.0f, 1.0f, triggerFade));
        auto rf = triggerRect.toFloat();
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillEllipse(rf.withX(rf.getX() - 1.5f).withY(rf.getY() + 1.5f));
        g.setColour(fill);
        g.fillEllipse(rf);
        g.setColour(kBase);
        g.drawEllipse(rf, 1.5f);
    }
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

    // MIDI device row with side buttons (IDLE CLK on left, RESET on right)
    {
        const int y = 30;
        const int sideW = 70; // compact side buttons for 300px width
        const int gap = 10;
        keepClockButton.setBounds(10, y, sideW, 24);
        //const int comboX = 10 + sideW + gap;

        const int comboW = 140;
        //const int comboX = comboW * 0.5f 
        deviceBox.setBounds(90, y, comboW, 24);
        refreshButton.setBounds(230 + gap, y, 50, 24);
    }

    // Click button above slider and slider on right
    clickButton.setBounds(240, 60, 50, 24);
    clickLevelSlider.setBounds(250, 90, 40, 160);

    // Step ring area (centered region for 160x160 ring + labels margin)
    ringArea = juce::Rectangle<int>(70, 90, 160, 160);

    // Trigger circle in bottom-right corner, 6px margin
    triggerRect = juce::Rectangle<int>(getWidth() - 56, getHeight() - 56, 50, 50);
}

void ClockSyncAudioProcessorEditor::timerCallback()
{
    const auto counter = processor.getUiClockCounter();
    if (counter != lastSeenClockCounter)
    {
        lastSeenClockCounter = counter;
        ledLevel = 1.0f;
        repaint();
    }
    else if (ledLevel > 0.01f)
    {
        ledLevel *= 0.90f; // decay
        repaint();
    }

    // Poll Run param for bar visibility (hide immediately on arm), and engine flags for status text
    if (auto* rp = processor.getAPVTS().getRawParameterValue(ClockSyncAudioProcessor::paramRun))
    {
        const bool runNow = rp->load() > 0.5f;
        if (runNow != runParamCached) { runParamCached = runNow; repaint(ringArea); }
    }
    {
        const bool running = processor.getUiIsRunning();
        if (running != engineRunningCached) { engineRunningCached = running; repaint(); }
        const bool armed = processor.getUiPendingStart();
        if (armed != pendingStartCached) { pendingStartCached = armed; repaint(); }
    }
    if (auto* cp = dynamic_cast<juce::AudioParameterChoice*>(processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramClockRateIndex)))
    {
        const int idx = cp->getIndex();
        if (idx != rateIndexCached)
        {
            rateIndexCached = idx;
            rateBtn32.setToggleState(idx == 0, juce::dontSendNotification);
            rateBtn16.setToggleState(idx == 1, juce::dontSendNotification);
            rateBtn8.setToggleState (idx == 2, juce::dontSendNotification);
            rateBtn4.setToggleState (idx == 3, juce::dontSendNotification);
            repaint();
        }
    }

    // Fade the trigger back to red
    if (triggerFade > 0.01f)
    {
        triggerFade *= 0.92f;
        repaint(triggerRect);
    }
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
    

