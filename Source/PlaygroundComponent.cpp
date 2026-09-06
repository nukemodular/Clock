#include "PlaygroundComponent.h"
#include "HitRouting.h"
PlaygroundComponent::PlaygroundComponent(UiThemeColours& t) : theme(t)
{
    setSize(300, 240);
    // Allow this component to receive keyboard focus (for future shortcuts if needed).
    setWantsKeyboardFocus(true);
    baseCircles.add({150.0f, 135.0f, 70.0f, juce::Colours::orangered});
    baseCircles.add({244.0f, 90.0f, 35.0f, juce::Colours::goldenrod});
    baseCircles.add({233.0f, 137.0f, 14.0f, juce::Colours::turquoise});
    baseCircles.add({235.0f, 175.0f, 25.0f, juce::Colours::mediumpurple});
    baseCircles.add({96.0f, 194.0f, 11.0f, juce::Colours::yellowgreen});
    baseCircles.add({78.0f, 177.0f, 14.0f, juce::Colours::pink});
    baseCircles.add({56.0f, 145.0f, 25.0f, juce::Colours::seagreen});
    baseCircles.add({70.0f, 85.0f, 25.0f, juce::Colours::greenyellow});
    baseCircles.add({201.0f, 69.0f, 14.0f, juce::Colours::blueviolet});
    circles = baseCircles;
    flashes.resize(circles.size());
    scheduledPulseAtMs.resize(circles.size());
    for (int i = 0; i < flashes.size(); ++i)
    {
        flashes.set(i, 0.0f);
        scheduledPulseAtMs.set(i, 0.0);
    }
    hoverTexts = {"Stop/Re-sync next bar + offset", "Re-trigger quantized", "Autofill pattern edit", "Autofill play interval", "Shuffle Amount", "Shuffle type 909 (1-7) or linear", "Set scale 1/32, 1/16, 1/8, 1/4", "Audio Click: off, beat, 8th, 16th, 24ppq", "Re-sync + offset on/off", "Send midi-clocks while idle/stopped", "Legacy send always stop before start", "Send song position pointer (SPP)"};
    ring.reset(new Ring16Component(theme));
    if (circles.size() > 0)
    {
        const auto &c0 = circles.getReference(0);
        ring->setBounds((int)(c0.x - 70.0f), (int)(c0.y - 70.0f), 140, 140);
        addAndMakeVisible(ring.get());
    }
    ring->onHoverChanged = [this](int logical)
    { ringHoverSegment=logical; repaint(); };
    ring->onInnerHover = [this](bool in)
    { if (patternEditMode) return; if (in){ hoverIndex=0; ringHoverSegment=-1; } else if (hoverIndex==0) hoverIndex=-1; repaint(); };
    ring->onClicked = [this](int logical)
    { ringSelectedSegment=logical; ring->setGlobalRestartLogical(logical); if (onResyncStepRequested) onResyncStepRequested(logical+1); repaint(); };
    // Ring playback state changes should NOT automatically toggle the Run parameter.
    // User interactions (clicking idx0) drive parameter changes; host transport should
    // only affect the engine/playhead without mutating UI button state.
    ring->onPlayStateChanged = [this](bool on)
    { if (patternEditMode) return; runState = on; /* do not call onRunToggleRequested here */ repaint(); };
    // Forward center clicks through the playground so editor can perform the
    // authoritative APVTS toggle (host-aware). If pattern edit active, the
    // playground will ignore this callback.
    ring->onCenterClicked = [this]()
    {
        if (patternEditMode)
            return; // block center toggles while editing
        // Reuse existing external click handler for idx0 which toggles runState,
        // updates the ring playing state and invokes onRunToggleRequested (editor-wired).
        (void)handleExternalClickIndex(0);
        repaint();
    };
    ring->onStepChanged = [this](int logical)
    {
        // Beat pulse every quarter note
        if ((logical % 4) == 0)
            startBeatPulse();

        // Pattern scheduling & per-step triggering (now active even while editing to audition changes)
        if (runState)
        {
            // If the host is supplying explicit bar numbers we rely on
            // setExternalBarNumber() to drive auto-trigger decisions. Only
            // fall back to step==0 logic when no external bar number exists.
            if (lastExternalBarNumber == -1 && logical == 0)
            {
                patternBarActive = false; // default reset each bar
                if (autoTriggerRandom)
                {
                    // RND: probability scaled by rndPatternAmount (1..100)
                    if (autoTriggerRng.nextFloat() < (rndPatternAmount / 100.0f))
                    {
                        patternBarActive = true;
                        barsSinceAutoTrigger = 0; // reset for informational purposes
                    }
                    else
                    {
                        ++barsSinceAutoTrigger; // accumulate idle bars for potential future tuning
                    }
                }
                else if (autoTriggerIntervalBars == 1)
                {
                    // Every bar continuous pattern
                    patternBarActive = true;
                }
                else if (autoTriggerIntervalBars > 1)
                {
                    ++barsSinceAutoTrigger;
                    if (barsSinceAutoTrigger >= autoTriggerIntervalBars)
                    {
                        barsSinceAutoTrigger = 0;
                        patternBarActive = true;
                    }
                }
                // OFF => remains false
                if (patternBarActive)
                {
                    if (autoRandomizePattern)
                        randomizePatternSteps();
                    flashFullRing(1.1f); // visual cue that a fill bar has begun
                }
            }

                // Fire start trigger for each active pattern step in this bar
                // Playback should trigger the step configured for the current
                // logical index (no artificial +1 real-step offset). Map the
                // logical index through the same visual rotation used by
                // pattern editing so stored indices line up with playback.
                int triggerIdx = logical & 15; // current logical step
                int rotatedTriggerIdx = triggerIdx; // account for visual +4 rotation
                if (patternBarActive && pattern.getStep(rotatedTriggerIdx))
                {
                    // Automatic pattern playback should not directly arm or
                    // mutate processor state; prefer a separate callback so
                    // the editor can handle preview/visual only behaviour.
                    if (onAutoTriggerRequested)
                        onAutoTriggerRequested();
                }

            // End of bar: the current pattern bar has just finished.
            // Reset the global bar counter so the cyan progress starts at 0
            // for the next bar, regardless of interval mode.
            if (patternBarActive && logical == 15)
            {
                barsSinceAutoTrigger = 0; // begin counting fresh next bar
                if (autoTriggerRandom || autoTriggerIntervalBars != 1)
                    patternBarActive = false;
            }
        }
        repaint(); };
    shufflePositions.add({1, 96.0f, 194.0f, 11.0f});
    shufflePositions.add({2, 109.0f, 205.0f, 12.0f});
    shufflePositions.add({3, 124.0f, 213.0f, 13.0f});
    shufflePositions.add({4, 143.0f, 218.0f, 14.0f});
    shufflePositions.add({5, 163.0f, 218.0f, 15.0f});
    shufflePositions.add({6, 184.0f, 213.0f, 16.0f});
    shufflePositions.add({7, 204.0f, 202.0f, 17.0f});
    selectedShuffle = 1;
    shuffleTextOffsets.add({0.3f, 0.1f});
    shuffleTextOffsets.add({0.6f, 0.2f});
    shuffleTextOffsets.add({1.0f, 0.0f});
    shuffleTextOffsets.add({1.0f, 0.0f});
    shuffleTextOffsets.add({0.2f, 0.0f});
    shuffleTextOffsets.add({0.7f, 0.0f});
    shuffleTextOffsets.add({1.3f, 0.2f});
    extra6Values.add(4);
    extra6Values.add(8);
    extra6Values.add(16);
    extra6Values.add(32);
    extra6Labels.add("4");
    extra6Labels.add("8");
    extra6Labels.add("16");
    extra6Labels.add("32");
    mainCircle6Value = 16;
    popup6.setValues(extra6Values);
    popup6.setLabels(extra6Labels);
    popup6.setAngles(extra6StartAngleDeg, extra6EndAngleDeg);
    popup6.setCircleRadius(extra6R);
    popup6.setExpansionRadius(extra6R - 1.0f);
    popup6.setAnimDurationMs(extra6AnimDurationMs);
    popup6.setStaggerMs(extra6StaggerMs);
    popup3.setLabels({"64", "32", "16", "8", "4", "2", "1", "OFF"});
    popup3.setAngles(150.0f, -420.0f);
    popup3.setCircleRadius(11.0f);
    popup3.setExpansionRadius(15.0f);
    popup3.setAnimDurationMs(extra6AnimDurationMs / 1.5f);
    popup3.setStaggerMs(extra6StaggerMs / 1.5f);
    extra6Progress.resize(extra6Count);
    extra6HoverScale.resize(extra6Count);
    extra6HoverTarget.resize(extra6Count);
    for (int i = 0; i < extra6Count; ++i)
    {
        extra6Progress.set(i, 0.0f);
        extra6HoverScale.set(i, 1.0f);
        extra6HoverTarget.set(i, 1.0f);
    }
    startTimerHz(60);
    makeButton(2);
    makeButton(5);
    makeButton(8);
    // Attempt initial focus grab (may be overridden by host). User can click to re-focus.
    grabKeyboardFocus();
}
PlaygroundComponent::~PlaygroundComponent()
{
    if (ring)
        removeChildComponent(ring.get());
}
void PlaygroundComponent::setRunState(bool on, bool notifyParam)
{
    // When notifyParam==true this is a user-driven toggle (click) and we update
    // the user-visible runState; when false (host-driven) we only update the
    // engine play state without mutating the stored user toggle.
    if (notifyParam)
    {
        runState = on;
        if (ring)
            ring->setPlaying(on);
    }
    else
    {
        // Host-driven: do not change `runState` which represents the user button state.
        if (ring)
            ring->setPlaying(on);
    }
    if (on && ring)
        ring->triggerFullRingFlash(ringSelectedSegment >= 0 ? 1.25f : 1.0f);
    if (runIndicator)
        runIndicator->setRunning(on);
    repaint();
}
void PlaygroundComponent::flashCurrentStepSegment()
{
    if (ring)
        ring->flashCurrentSegment();
}
void PlaygroundComponent::flashSegmentLogical(int s)
{
    if (ring)
        ring->flashSegmentLogical(s);
}
void PlaygroundComponent::flashFullRing(float i)
{
    if (ring)
        ring->triggerFullRingFlash(i);
}
void PlaygroundComponent::setResyncStepSelected(int s)
{
    if (ring)
        ring->setGlobalRestartLogical(juce::jlimit(1, 16, s) - 1);
    repaint();
}
void PlaygroundComponent::setHostTempo(double bpm)
{
    if (ring)
        ring->setBpm(bpm);
}
void PlaygroundComponent::setClickPulseState(bool on)
{
    clickToPulseOn = on;
    repaint();
}
void PlaygroundComponent::setExternalPlayheadStep(int s)
{
    externalStepForDisplay = juce::jlimit(1, 16, s);
    if (ring)
    {
        // Update ring to use external playhead and notify its onStepChanged
        ring->setExternalPlayheadStepLogical(externalStepForDisplay);
        if (ring->onStepChanged)
            ring->onStepChanged((externalStepForDisplay - 1) & 15); // pass logical 0..15
    }
}

void PlaygroundComponent::setExternalBarNumber(int barNumber)
{
    // Treat negative barNumber as a signal to reset our external-bar tracking
    if (barNumber < 0)
    {
        // Reset external tracking so the next host start will be treated as a fresh run
        lastExternalBarNumber = -1;
        barsSinceAutoTrigger = 0;
        patternBarActive = false;
        repaint();
        return;
    }

    // If unchanged, nothing to do
    if (barNumber == lastExternalBarNumber)
        return;

    int previous = lastExternalBarNumber;
    // Capture whether the previous bar was a pattern-active bar so we can
    // reset the progress counter right after it finishes.
    const bool prevBarWasActive = (previous != -1) ? patternBarActive : false;
    lastExternalBarNumber = barNumber;

    // First time initialization: don't trigger immediately, just align counters
    if (previous == -1)
    {
        barsSinceAutoTrigger = 0;
        patternBarActive = false;
        return;
    }

    // A new bar has begun (host-provided).
    // If the previous bar was a pattern bar, reset the progress counter now
    // so the cyan progress starts at 0 for the new bar.
    if (prevBarWasActive)
    {
        barsSinceAutoTrigger = 0;
    }

    // Decide whether this bar should be a pattern fill
    if (autoTriggerRandom)
    {
        if (autoTriggerRng.nextFloat() < (rndPatternAmount / 100.0f))
        {
            patternBarActive = true;
            barsSinceAutoTrigger = 0;
            if (autoRandomizePattern)
                randomizePatternSteps();
            flashFullRing(1.1f);
        }
        else
        {
            ++barsSinceAutoTrigger;
            patternBarActive = false;
        }
    }
    else if (autoTriggerIntervalBars == 1)
    {
        // Continuous: every bar is a pattern bar
        patternBarActive = true;
        if (autoRandomizePattern)
            randomizePatternSteps();
        flashFullRing(1.1f);
    }
    else if (autoTriggerIntervalBars > 1)
    {
        ++barsSinceAutoTrigger;
        if (barsSinceAutoTrigger >= autoTriggerIntervalBars)
        {
            barsSinceAutoTrigger = 0;
            patternBarActive = true;
            if (autoRandomizePattern)
                randomizePatternSteps();
            flashFullRing(1.1f);
        }
        else
        {
            patternBarActive = false;
        }
    }
    else // interval OFF
    {
        patternBarActive = false;
    }

    repaint();
}
void PlaygroundComponent::setTriggerModeState(bool on)
{
    for (int i = 0; i < onOffButtons.size(); ++i)
    {
        if (i < onOffButtonIdxs.size() && onOffButtonIdxs[i] == 8)
            if (onOffButtons[i])
                onOffButtons[i]->setState(on);
    }
    repaint();
}
void PlaygroundComponent::togglePopup3()
{
    if (popup3.isVisible() && !popup3.isAnimating())
        startCollapse3();
    else if (!popup3.isVisible() && !popup3.isAnimating())
        startExpand3();
}
void PlaygroundComponent::togglePopup6()
{
    if (popup6.isVisible() && !popup6.isAnimating())
        startCollapse6();
    else if (!popup6.isVisible() && !popup6.isAnimating())
        startExpand6();
}
void PlaygroundComponent::setHoverIndexFromEditor(int idx)
{
    if (externalHoverBlocked)
        return;
    if (idx < 0 || idx >= circles.size())
    {
        if (hoverIndex != -1)
        {
            hoverIndex = -1;
            repaint();
        }
        return;
    }
    if (hoverIndex != idx)
    {
        hoverIndex = idx;
        ringHoverSegment = -1;
        repaint();
    }
}
void PlaygroundComponent::clearHoverFromEditor()
{
    if (forcedHoverIndex >= 0)
        forcedHoverIndex = -1;
    if (hoverIndex != -1 || ringHoverSegment != -1)
    {
        hoverIndex = -1;
        ringHoverSegment = -1;
        repaint();
    }
}
void PlaygroundComponent::setForcedHoverIndex(int idx)
{
    if (idx < 0)
    {
        if (forcedHoverIndex != -1)
        {
            forcedHoverIndex = -1;
            repaint();
        }
        return;
    }
    if (forcedHoverIndex != idx)
    {
        forcedHoverIndex = idx;
        hoverIndex = -1;
        ringHoverSegment = -1;
        repaint();
    }
}
void PlaygroundComponent::clearForcedHoverIndex()
{
    if (forcedHoverIndex != -1)
    {
        forcedHoverIndex = -1;
        repaint();
    }
}
void PlaygroundComponent::setExternalHoverBlocked(bool b)
{
    if (externalHoverBlocked != b)
    {
        externalHoverBlocked = b;
        if (b)
        {
            hoverIndex = -1;
            ringHoverSegment = -1;
            forcedHoverIndex = -1;
            repaint();
        }
    }
}
bool PlaygroundComponent::handleExternalClickIndex(int idx)
{
    if (idx < 0 || idx >= circles.size())
        return false;
    if (idx == 0)
    {
        bool next = !runState;
        runState = next;
        if (ring)
            ring->setPlaying(next);
        if (onRunToggleRequested)
            onRunToggleRequested(next);
        repaint();
        return true;
    }
    if (idx == 1)
    {
        flashes.set(1, 1.0f);
        if (onRunToggleRequested)
            onRunToggleRequested(true);
        return true;
    }
    return false;
}
void PlaygroundComponent::setPatternEditButtonState(bool enabled)
{
    patternEditMode = enabled;
    for (int i = 0; i < onOffButtons.size(); ++i)
    {
        if (!onOffButtons[i])
            continue;
        int circleIdx = (i < onOffButtonIdxs.size()) ? onOffButtonIdxs[i] : i;
        if (circleIdx == 2)
        {
            onOffButtons[i]->setState(enabled);
            onOffButtons[i]->repaint();
        }
    }

    if (ring)
    {
        ring->setPatternEditMode(patternEditMode);
        // Ring remains visible: paint logic will suppress full grid but show chase & selected wedge in edit mode.
        ring->setVisible(true);
    }
}
void PlaygroundComponent::setHeaderHeight(int h)
{
    headerHeight = h;
    circles.clear();
    for (int i = 0; i < baseCircles.size(); ++i)
        circles.add(baseCircles[i]);
    if (ring && circles.size() > 0)
    {
        const auto &c0 = circles.getReference(0);
        ring->setBounds((int)std::round(c0.x - 70.0f), (int)std::round(c0.y - 70.0f), 140, 140);
    }
    for (int i = 0; i < onOffButtons.size(); ++i)
    {
        if (onOffButtons[i])
        {
            int circleIdx = (i < onOffButtonIdxs.size()) ? onOffButtonIdxs[i] : i;
            if (circleIdx >= 0 && circleIdx < circles.size())
            {
                const auto &c = circles.getReference(circleIdx);
                onOffButtons[i]->setBounds((int)std::round(c.x - 14.0f), (int)std::round(c.y - 14.0f), 28, 28);
            }
        }
    }
    repaint();
}
void PlaygroundComponent::setClockRateIndexValue(int val)
{
    mainCircle6Value = val;
    double mult = 1.0;
    if (val == 32)
        mult = 2.0;
    else if (val == 16)
        mult = 1.0;
    else if (val == 8)
        mult = 0.5;
    else if (val == 4)
        mult = 0.25;
    if (!speedCommitInitialised)
    {
        committedSpeedMultiplier = mult;
        pendingSpeedMultiplier = -1.0;
        speedCommitInitialised = true;
        if (ring)
            ring->setSpeedMultiplier(committedSpeedMultiplier);
    }
    else
        pendingSpeedMultiplier = mult;
    repaint();
}
void PlaygroundComponent::commitPendingSpeedMultiplier()
{
    if (pendingSpeedMultiplier > 0.0 && std::abs(pendingSpeedMultiplier - committedSpeedMultiplier) > 1e-6)
    {
        committedSpeedMultiplier = pendingSpeedMultiplier;
        pendingSpeedMultiplier = -1.0;
        if (ring)
            ring->setSpeedMultiplier(committedSpeedMultiplier);
    }
}
double PlaygroundComponent::getCommittedSpeedMultiplier() const noexcept { return committedSpeedMultiplier; }
double PlaygroundComponent::getPendingSpeedMultiplier() const noexcept { return pendingSpeedMultiplier; }
void PlaygroundComponent::setHoverTextEnabled(bool en)
{
    hoverTextEnabled = en;
    repaint();
}
bool PlaygroundComponent::getHoverTextEnabled() const noexcept { return hoverTextEnabled; }
juce::String PlaygroundComponent::getCurrentHoverText() const
{
    juce::String label;
    if (forcedHoverIndex >= 0 && forcedHoverIndex < hoverTexts.size())
    {
        if (forcedHoverIndex == 7)
            label = "Click or Pulse";
        else
            label = hoverTexts[forcedHoverIndex];
    }
    else if (ringHoverSegment >= 0)
        label = "Re-sync at step " + juce::String(ringHoverSegment + 1);
    else if (hoverIndex == 7)
    {
        if (hoverTexts.size() > 7)
            label = hoverTexts[7];
    }
    else if (clickToPulseHover)
    {
        label = "Click or Pulse";
    }
    else if (hoverIndex >= 0 && hoverIndex < hoverTexts.size())
    {
       // label = juce::String(hoverIndex + 1) + ": " + hoverTexts[hoverIndex];
        label = hoverTexts[hoverIndex];
    }
    else if (rotaryHandleHover && hoverTexts.size() > 7)
    {
        label = hoverTexts[7];
    }
    else if (hoverShuffleId != -1)
    {
        if (linearShuffleMode && shufflePositions.size() > 1)
            label = "Shuffle intensity 50% - 75%";
        else
            label = "Shuffle intensity 1 (off) - 7";
    }
    if (clickToPulseHover && hoverIndex != 7 && forcedHoverIndex != 7)
    {
        if (label.isNotEmpty())
            label += " — ";
        label += "Click or Pulse";
    }
    return label;
}
void PlaygroundComponent::setClickToPulseHoverFromEditor(bool hover)
{
    if (clickToPulseHover != hover)
    {
        clickToPulseHover = hover;
        repaint();
    }
}
void PlaygroundComponent::paint(juce::Graphics &g)
{
    // Use member fonts (constructed in component lifetime) to avoid static CoreText teardown crashes
    // Draw run-state indicator behind the donut: compute its bounds and draw
    // it here BEFORE the donut so the donut will paint on top (visually
    // occluding the indicator), which guarantees the indicator sits "behind"
    // the red donut in z-order. Keep the magenta diagnostic outline visible
    // in Release builds.
    juce::Rectangle<float> runRectFloat;
    // Draw stop diagonal only when stopped AND pattern edit mode is OFF.
    // In pattern edit mode, the diagonal is always hidden regardless of state.
    if (circles.size() > 0 && !runState && !patternEditMode)
    {
        const auto &c0 = circles.getReference(0);
        auto r = RunButton::suggestedBoundsForCentre((int)std::round(c0.x), (int)std::round(c0.y));
        runRectFloat = r.toFloat();
        // Draw the rotated run indicator here (static draw) so it is painted
        // before the donut and thus visually behind it.
        RunButton::drawAt(g, (int)std::round(c0.x), (int)std::round(c0.y), runState, theme);

        // (Diagnostics removed per user request)
    }
    // Draw the red donut (outer ring minus inner hole) so it paints on top of
    // the statically drawn run indicator above. This restores the original
    // visual where the run indicator sits behind the donut.
    // Always draw the red donut background (outer circle), even in pattern edit mode
    if (circles.size() > 0)
    {
        const auto &c0 = circles.getReference(0);
        float maskOuter = ring ? ring->getOuterRadius() : 70.0f;
        float maskInner = ring ? ring->getInnerRadius() : 55.0f;
        juce::Path donut;
        donut.addEllipse(c0.x - maskOuter, c0.y - maskOuter, maskOuter * 2.0f, maskOuter * 2.0f);
        donut.addEllipse(c0.x - maskInner, c0.y - maskInner, maskInner * 2.0f, maskInner * 2.0f);
        donut.setUsingNonZeroWinding(false);
        g.setColour(theme.accent());
        g.fillPath(donut);
    }
    // Pattern ring (inward) when edit mode active
    if (patternEditMode && ring && circles.size() > 0)
    {
        const auto &c0 = circles.getReference(0);
        float outerR = (ring->getInnerRadius() - PatternGeometry::kPatternInset) + PatternGeometry::kPatternOutwardShift;
        float innerR = outerR * PatternGeometry::kPatternThicknessRatio; // keep proportional thickness
        // Hide inactive wedges (only show active cyan ones) per user request.
        pattern.draw(g, {c0.x, c0.y}, outerR, innerR, theme.cyan(), theme.base(), patternHoverIndex, theme, true);
        // Center UI (RND amount number, AUTO toggle above, RND trigger below)
        float centreX = c0.x;
        float centreY = c0.y;
        float boxW = 40.0f;
        float boxH = 20.0f;
        float gapY = 2.0f;
        juce::Rectangle<float> amountRect(centreX - boxW * 0.5f, centreY - boxH * 0.5f, boxW, boxH);
        juce::Rectangle<float> autoRect(amountRect.withY(amountRect.getY() - boxH - gapY));
        juce::Rectangle<float> rndRect(amountRect.withY(amountRect.getY() + boxH + gapY));
        const juce::Font &fontSmall = fontSmall12;
        const juce::Font &fontMid = fontMid15;
        const float cornerR = 16.0f;
        // Draw AUTO with rounded TOP corners only (cyan styling when enabled)
        {
            juce::Path p;
            float x = autoRect.getX(), y = autoRect.getY(), w = autoRect.getWidth(), h = autoRect.getHeight();
            p.startNewSubPath(x, y + h); // bottom-left
            p.lineTo(x, y + cornerR);
            p.quadraticTo(x, y, x + cornerR, y); // top-left corner
            p.lineTo(x + w - cornerR, y);
            p.quadraticTo(x + w, y, x + w, y + cornerR); // top-right corner
            p.lineTo(x + w, y + h);
            p.closeSubPath();
            juce::Colour fillCol = autoRandomizePattern ? theme.cyan().withAlpha(0.33f) : theme.accent().withAlpha(0.33f);
            juce::Colour strokeCol = autoRandomizePattern ? theme.cyan().withAlpha(0.66f) : theme.accent().withAlpha(0.66f);
            g.setColour(fillCol);
            g.fillPath(p);
            g.setColour(autoRandomizePattern ? theme.cyan() : theme.accent());
            g.setFont(fontSmall);
            g.drawFittedText("AUTO", autoRect.toNearestInt(), juce::Justification::centred, 1);
            g.setColour(strokeCol);
            g.strokePath(p, juce::PathStrokeType(1.0f));
        }
        // Amount rectangle (no rounded corners)
        g.setColour(theme.accent().withAlpha(0.33f));
        g.fillRect(amountRect);
        // Display random amount as 1..16
        g.setColour(theme.cyan());
        g.setFont(fontMid);
        g.drawFittedText(juce::String(juce::jlimit(1, 16, rndPatternAmount)), amountRect.toNearestInt(), juce::Justification::centred, 1);
        g.setColour(theme.accent().withAlpha(0.66f));
        g.drawRect(amountRect);
        // Draw RND with rounded BOTTOM corners only (flash cyan when triggered)
        {
            juce::Path p;
            float x = rndRect.getX(), y = rndRect.getY(), w = rndRect.getWidth(), h = rndRect.getHeight();
            p.startNewSubPath(x, y); // top-left
            p.lineTo(x, y + h - cornerR);
            p.quadraticTo(x, y + h, x + cornerR, y + h); // bottom-left corner
            p.lineTo(x + w - cornerR, y + h);
            p.quadraticTo(x + w, y + h, x + w, y + h - cornerR); // bottom-right corner
            p.lineTo(x + w, y);
            p.closeSubPath();
            float flashAmt = juce::jlimit(0.0f, 1.0f, rndTriggerFlash);
            juce::Colour baseFill = theme.accent().withAlpha(0.33f);
            juce::Colour fillCol = baseFill.interpolatedWith(theme.cyan(), flashAmt);
            juce::Colour strokeCol = theme.accent().withAlpha(0.66f).interpolatedWith(theme.cyan(), flashAmt);
            // Fade text colour between accent and cyan using flashAmt instead of popping.
            juce::Colour textCol = theme.accent().interpolatedWith(theme.cyan(), flashAmt);
            g.setColour(fillCol);
            g.fillPath(p);
            g.setColour(textCol);
            g.setFont(fontSmall);
            g.drawFittedText("RND", rndRect.toNearestInt(), juce::Justification::centred, 1);
            g.setColour(strokeCol);
            g.strokePath(p, juce::PathStrokeType(1.0f));
        }
    }
    // Revised circle rendering order: draw larger circles first, smaller last so smaller appear "frontmost".
    // This visually reinforces new hit-test precedence (small wins when overlapping) and addresses
    // perceived occlusion where big circles seemed to swallow small circle hover zones.
    if (circles.size() > 1)
    {
        juce::Array<int> drawOrder;
        for (int i = 1; i < circles.size(); ++i)
        {
            if (i == 4)
                continue;
            drawOrder.add(i);
        }
        // Sort by radius descending (largest drawn first, smallest last on top)
        std::sort(drawOrder.begin(), drawOrder.end(), [this](int a, int b)
                  {
            float ra = circles.getReference(a).r; float rb = circles.getReference(b).r; return ra > rb; });
        for (int di = 0; di < drawOrder.size(); ++di)
        {
            int i = drawOrder[di];
            const auto &c = circles.getReference(i);
            float rr = c.r;
            float f = juce::jlimit(0.0f, 1.0f, flashes[i]);
            juce::Colour col = theme.accent().interpolatedWith(theme.cyan(), f);
            // Hover effect: restore colour-based hover (use darker(0.1f) per request)
            bool wantsHover = (hoverIndex == i) && (i >= 1 && i <= 8);
            if (wantsHover)
            {
                col = col.darker(0.1f);
            }
            float scale = (i == 1 ? 1.0f + 0.2f * f : 1.0f);
            float rs = rr * scale;
            g.setColour(col);
            g.fillEllipse(c.x - rs, c.y - rs, rs * 2, rs * 2);
        }
    }
    if (circles.size() > 7)
    {
        const auto &c = circles.getReference(7);
        float insetR = 17.0f;
        g.setColour(theme.base());
        g.fillEllipse(c.x - insetR, c.y - insetR, insetR * 2, insetR * 2);
        const float startAng = -juce::MathConstants<float>::pi * 1.2f;
        const float endAng = juce::MathConstants<float>::pi * 0.2f;
        float ang = startAng + offsetRotaryT * (endAng - startAng);
        float needleR = insetR + 2.0f;
        float x2 = c.x + std::cos(ang) * needleR;
        float y2 = c.y + std::sin(ang) * needleR;
        g.setColour(theme.accent().interpolatedWith(theme.cyan(), offsetRotaryT));
        g.drawLine(c.x, c.y, x2, y2, 4.0f);
    }
    if (shufflePositions.size() > 0)
    {
        if (linearShuffleMode && shufflePositions.size() > 1)
        {
            auto posPt = getPointAlongShufflePolyline(linearShufflePos);
            float Rstart = shufflePositions.getReference(0).r;
            float Rend = shufflePositions.getReference(shufflePositions.size() - 1).r;
            float R = Rstart + (Rend - Rstart) * linearShufflePos;
            bool hoverOnLinear = (hoverShuffleId != -1);

            // Hover effect for linear handle: darken fill slightly instead of scaling
            juce::Colour linearFill = theme.accent();
            if (hoverOnLinear)
                linearFill = linearFill.darker(0.1f);

            float innerR = R * 0.6f;
            g.setColour(linearFill);
            g.fillEllipse(posPt.x - R, posPt.y - R, R * 2, R * 2);
            g.setColour(theme.base());
            g.fillEllipse(posPt.x - innerR, posPt.y - innerR, innerR * 2, innerR * 2);

            int numeric = 50 + (int)std::round(linearShufflePos * 25.0f);
            g.setColour(theme.cyan());
            g.setFont(fontHover12);
            g.drawFittedText(juce::String(numeric), (int)(posPt.x - innerR), (int)(posPt.y - innerR), (int)(innerR * 2), (int)(innerR * 2), juce::Justification::centred, 1);
        }
        else if (selectedShuffle >= 1 && selectedShuffle <= shufflePositions.size())
        {
            auto pos = shufflePositions[selectedShuffle - 1];
            float R = pos.r;
            bool hoverSelected = (hoverShuffleId == selectedShuffle);

            juce::Colour shuffleFill = theme.accent();
            if (hoverSelected)
                shuffleFill = shuffleFill.darker(0.1f);

            float innerR = R * 0.6f;
            g.setColour(shuffleFill);
            g.fillEllipse(pos.x - R, pos.y - R, R * 2, R * 2);
            g.setColour(theme.base());
            g.fillEllipse(pos.x - innerR, pos.y - innerR, innerR * 2, innerR * 2);

            const juce::Font shuffleFonts[7] = {
                juce::Font(juce::FontOptions("Arial", 11.0f, juce::Font::bold)),
                juce::Font(juce::FontOptions("Arial", 12.0f, juce::Font::bold)),
                juce::Font(juce::FontOptions("Arial", 13.0f, juce::Font::bold)),
                juce::Font(juce::FontOptions("Arial", 14.0f, juce::Font::bold)),
                juce::Font(juce::FontOptions("Arial", 15.0f, juce::Font::bold)),
                juce::Font(juce::FontOptions("Arial", 16.0f, juce::Font::bold)),
                juce::Font(juce::FontOptions("Arial", 17.0f, juce::Font::bold))
            };
            g.setColour(theme.cyan());
            int fIdx = (pos.id >= 1 && pos.id <= 7) ? (pos.id - 1) : 1;
            g.setFont(shuffleFonts[fIdx]);

            float offX = 0, offY = 0;
            int si = selectedShuffle - 1;
            if (si >= 0 && si < shuffleTextOffsets.size())
            {
                offX = shuffleTextOffsets[si].dx;
                offY = shuffleTextOffsets[si].dy;
            }

            g.drawFittedText(juce::String(pos.id), (int)(pos.x - innerR + offX), (int)(pos.y - innerR + offY), (int)(innerR * 2), (int)(innerR * 2), juce::Justification::centred, 1);
        }
    }
    juce::String label = getCurrentHoverText();
    // Hover text drawing moved to PluginEditor::paintOverChildren to share space with build number
    /*
    if (hoverTextEnabled && label.isNotEmpty())
    {
        // move hover text up 1px for tighter layout
        auto area = juce::Rectangle<int>(8, getHeight() - 17, getWidth() - 16, 18);
        g.setColour(theme.cyan());
        g.setFont(fontHover12);
        g.drawFittedText(label, area, juce::Justification::centred, 1);
    }
    */
}
void PlaygroundComponent::paintOverChildren(juce::Graphics &g)
{
    // Use member fonts (constructed in component lifetime) to avoid static CoreText teardown crashes
    if (circles.size() <= 1)
        return;

    const auto &c1 = circles.getReference(1);
    float outerR = c1.r;
    float fOuter = juce::jlimit(0.0f, 1.0f, flashes[1]);
    float outerScale = 1.0f + 0.2f * fOuter;
    float outerRScaled = outerR * outerScale;

    juce::Colour col = theme.accent().interpolatedWith(theme.cyan(), fOuter);
    g.setColour(col);
    g.fillEllipse(c1.x - outerRScaled, c1.y - outerRScaled, outerRScaled * 2, outerRScaled * 2);

    float insetR = 24.0f;
    float insetRScaled = insetR * (1.0f + 0.2f * fOuter);
    g.setColour(theme.base());
    g.fillEllipse(c1.x - insetRScaled, c1.y - insetRScaled, insetRScaled * 2, insetRScaled * 2);

    int stepNum = externalStepForDisplay > 0 ? externalStepForDisplay : (ring ? ring->getCurrentStepLogical() : -1);
    juce::String stepText = stepNum > 0 ? juce::String(stepNum) : "-";
    g.setColour(theme.cyan());
    g.setFont(fontStep30);
    g.drawFittedText(stepText, (int)(c1.x - insetRScaled), (int)(c1.y - insetRScaled), (int)(insetRScaled * 2), (int)(insetRScaled * 2), juce::Justification::centred, 1);

    if (circles.size() > 7)
    {
        const auto &c7 = circles.getReference(7);
        float smallR = 8.0f;
        juce::Colour bcol = clickToPulseOn ? theme.cyan() : theme.accent();
        g.setColour(bcol);
        g.fillEllipse(c7.x - smallR, c7.y - smallR, smallR * 2, smallR * 2);
        if (clickToPulseHover)
        {
            g.setColour(theme.cyan().withAlpha(0.25f));
            g.drawEllipse(c7.x - smallR - 2.0f, c7.y - smallR - 2.0f, (smallR + 2.0f) * 2, (smallR + 2.0f) * 2, 2.0f);
        }
    }

    if (circles.size() > 6)
    {
        const auto &c6 = circles.getReference(6);
        float insetR6 = 17.0f;
        g.setColour(theme.base());
        g.fillEllipse(c6.x - insetR6, c6.y - insetR6, insetR6 * 2, insetR6 * 2);
        g.setColour(theme.cyan());
        g.setFont(fontMain6_22);
        g.drawFittedText(juce::String(mainCircle6Value), (int)(c6.x - insetR6), (int)(c6.y - insetR6), (int)(insetR6 * 2), (int)(insetR6 * 2), juce::Justification::centred, 1);
        if (popup6.isVisible() || popup6.isAnimating())
            popup6.draw(g, c6.x, c6.y, c6.r, theme);
    }

        if (circles.size() > 3)
        {
            const auto &c3 = circles.getReference(3);
            float insetR3 = 17.0f;
            g.setColour(theme.base());
            g.fillEllipse(c3.x - insetR3, c3.y - insetR3, insetR3 * 2, insetR3 * 2);
            g.setColour(theme.cyan());
            // If label is a numeric interval (e.g. "4","8","16",...), draw it larger
            juce::String lbl3 = mainCircle3Label.trim();
            bool smallText = lbl3.equalsIgnoreCase("RND") || lbl3.equalsIgnoreCase("OFF");
            bool isNumber = !smallText;
            if (!smallText)
            {
                if (lbl3.isEmpty()) isNumber = false;
                for (int i = 0; i < lbl3.length(); ++i)
                {
                    const juce::juce_wchar ch = lbl3[i];
                    if (! juce::CharacterFunctions::isDigit(ch)) { isNumber = false; break; }
                }
            }
            if (smallText)
                g.setFont(fontRndSmall14);
            else if (isNumber)
                g.setFont(fontRndNum24);
            else
                g.setFont(fontRndSmall14);
            g.drawFittedText(lbl3, (int)(c3.x - insetR3), (int)(c3.y - insetR3), (int)(insetR3 * 2), (int)(insetR3 * 2), juce::Justification::centred, 1);
            if (popup3.isVisible() || popup3.isAnimating())
                popup3.draw(g, c3.x, c3.y, c3.r, theme);
        }

    // Draw countdown that starts as a full circle and decreases counterclockwise.
    // For interval=4: full -> 3/4 after 1 bar -> 1/2 after 2 bars -> 1/4 at the start of the firing bar (3 o'clock to 12) -> 0 by the end of the firing bar.
    if (circles.size() > 3 && autoTriggerIntervalBars > 0 && !autoTriggerRandom)
    {
        const auto &c3 = circles.getReference(3);
        const float insetR3 = 17.0f;
        const int interval = juce::jmax(1, autoTriggerIntervalBars);
        // Compute elapsed progress across the interval so remaining = 1 - progress.
        const int currentBar = juce::jmax(1, lastExternalBarNumber);
        const int mod = (currentBar > 0) ? (currentBar % interval) : 0;
        // Bars elapsed before the current bar within the cycle:
        // For interval=4: mod=1 -> 0, mod=2 -> 1, mod=3 -> 2, mod=0 (firing bar) -> 3
        int elapsedBars = 0;
        if (interval > 0)
            elapsedBars = (mod == 0) ? (interval - 1) : (mod - 1);
        // Intra-bar progress from step (0..15 -> 0..15/16)
        int step = externalStepForDisplay > 0 ? externalStepForDisplay : (ring ? ring->getCurrentStepLogical() : -1);
        double intra = 0.0;
        if (step > 0)
            intra = (double)(juce::jlimit(1, 16, step) - 1) / 16.0;
        // Progress through cycle and remaining fraction
        double progress = ((double) elapsedBars + intra) / (double) interval; // 0..1
        const double eps = 1.0 / 512.0;
        if (progress < eps) progress = 0.0;
        if (progress > 1.0 - eps) progress = 1.0;
        const float remaining = juce::jlimit(0.0f, 1.0f, (float)(1.0 - progress));
        // Draw a subtracting CCW arc: start full at 12 o'clock, shrink to empty by end of firing bar.
        {
            const float strokeW = 2.0f;
            const float r = insetR3 - strokeW + 1.1f;
            const float sweep = remaining * juce::MathConstants<float>::twoPi;
            if (sweep > 0.0001f)
            {
                const float startAt12 = -juce::MathConstants<float>::halfPi; // 12 o'clock
                // Rotate +90° to correct visual offset from 9 → 12
                const float startAng = startAt12 + juce::MathConstants<float>::halfPi;
                // JUCE's coordinate system has +Y down; adding angle sweeps visually counterclockwise.
                const float endAng = startAng + sweep; // subtract CCW toward empty
                juce::Path arcPath;
                // Start a new subpath to avoid any line joining from previous path endpoints.
                arcPath.addArc(c3.x - r, c3.y - r, r * 2.0f, r * 2.0f, startAng, endAng, true);
                g.setColour(theme.cyan());
                g.strokePath(arcPath, juce::PathStrokeType(strokeW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }
    }

    // (Shuffle debug overlay removed)
    // Status LED (top-right)
    const float ledR = 3.0f;
    float cx = (float)getWidth() - 3.0f;
    float cy = 13.5f;
    juce::Colour ledColour;
    juce::String statusLabel;
    switch (statusState)
    {
    // Increase Idle alpha so the LED remains visible against dark backgrounds.
    case StatusState::Idle:
        ledColour = theme.accent().withAlpha(0.85f);
        statusLabel = "IDLE";
        break;
    case StatusState::Armed:
        ledColour = theme.cyan().withAlpha(0.95f);
        statusLabel = "ARM";
        break;
    case StatusState::Pending:
        ledColour = theme.accent().interpolatedWith(theme.cyan(), 0.6f).withAlpha(0.95f);
        statusLabel = "PENDING";
        break;
    }
    // Pulse effect for Pending
    if (statusState == StatusState::Pending)
    {
        float pulseA = 0.5f + 0.35f * std::sin(statusPulse);
        ledColour = ledColour.withAlpha(juce::jlimit(0.0f, 1.0f, pulseA));
    }
    // Draw subtle outer glow for contrast (LED and status text moved +5px in X)
    g.setColour(theme.cyan().withAlpha(0.12f));
    g.fillEllipse(cx - (ledR + 1.5f), cy - (ledR + 1.5f), (ledR + 1.5f) * 2, (ledR + 1.5f) * 2);
    g.setColour(ledColour);
    g.fillEllipse(cx - ledR, cy - ledR, ledR * 2, ledR * 2);
    // Thin outer stroke for crisp edge visibility
    g.setColour(theme.base().withAlpha(0.9f));
    g.drawEllipse(cx - ledR, cy - ledR, ledR * 2, ledR * 2, 1.2f);
    // Text to left of LED (moved right along with LED)
    g.setFont(fontStatus10);
    float textRight = cx - ledR + 2.0f;
    juce::Rectangle<float> txtArea(11.0f + 5.0f, cy - 8.0f, textRight - (11.0f + 5.0f), 16.0f);
    g.setColour(theme.cyan());
    g.drawFittedText(statusLabel, txtArea.toNearestInt(), juce::Justification::centredLeft, 1);
}
void PlaygroundComponent::mouseDown(const juce::MouseEvent &e)
{
    // Ensure we have focus when user interacts so subsequent key presses work.
    if (!hasKeyboardFocus(true))
        grabKeyboardFocus();
    // If popup3 is open and click happens outside a guard circle, collapse it immediately
    if ((popup3.isVisible() || popup3.isAnimating()))
    {
        float x3, y3, r3;
        if (getCircleInfo(3, x3, y3, r3))
        {
            const float guardR = 60.0f; // virtual mask radius requested
            const float dx = e.position.x - x3;
            const float dy = e.position.y - y3;
            if (dx * dx + dy * dy > guardR * guardR)
            {
                popup3.startCollapse();
                startFadeTimer();
                // Do not return; allow event to fall through to underlying hit mapping this frame
            }
        }
    }
    // If popup6 is open and click happens outside its guard circle, collapse it
    if ((popup6.isVisible() || popup6.isAnimating()))
    {
        float x6, y6, r6;
        if (getCircleInfo(6, x6, y6, r6))
        {
            const float guardR6 = 50.0f; // requested radius for idx6
            const float dx6 = e.position.x - x6;
            const float dy6 = e.position.y - y6;
            if (dx6 * dx6 + dy6 * dy6 > guardR6 * guardR6)
            {
                popup6.startCollapse();
                startFadeTimer();
            }
        }
    }
    HitContext ctx;
    ctx.playground = this;
    ctx.pattern = nullptr;
    ctx.ringArea = ring ? ring->getBounds() : juce::Rectangle<int>();
    ctx.patternEditMode = patternEditMode;
    ctx.submenuActive = false;
    ctx.outerDiameter = (int)(ring ? ring->getOuterRadius() * 2.0f : 140);
    ctx.innerDiameter = (int)(ring ? ring->getInnerRadius() * 2.0f : 110);
    if (patternEditMode)
    {
        // Use pattern in context
        ctx.pattern = &pattern;
    }
    auto hr = performHitTest(e.position, ctx, patternEditMode ? InteractionMode::PatternEdit : InteractionMode::Normal);
    // Pattern edit mode interactions
    if (patternEditMode && hr.type == ZoneType::PatternWedge)
    {
        // Modifier semantics:
        //  Shift: force paint ON
        //  Alt/Option: force paint OFF
        //  Command/Ctrl: invert entire pattern (ignores individual paint)
        if (e.mods.isCommandDown())
        {
            for (int i = 0; i < 16; ++i)
                pattern.toggleStep(i);
            repaint();
            return; // swallow
        }
        patternDragActive = true;
        lastPatternDragWedge = hr.index;
        if (e.mods.isShiftDown())
            patternDragPaintState = true;
        else if (e.mods.isAltDown())
            patternDragPaintState = false;
        else
            patternDragPaintState = !pattern.getStep(hr.index); // flip base state
        pattern.setStep(hr.index, patternDragPaintState);
        // Notify editor/host that pattern changed
        if (onPatternChanged)
            onPatternChanged((uint16_t) pattern.getBitmask());
        repaint();
        return; // swallow
    }
    // Center UI hit handling in pattern edit mode (when hr.type == None typically)
    if (patternEditMode && hr.type == ZoneType::None && ring && circles.size() > 0)
    {
        const auto &c0 = circles.getReference(0);
        float outerR = (ring->getInnerRadius() - PatternGeometry::kPatternInset) + PatternGeometry::kPatternOutwardShift;
        float innerR = outerR * PatternGeometry::kPatternThicknessRatio;
        float centreX = c0.x;
        float centreY = c0.y;
        float boxW = 46.0f;
        float boxH = 18.0f;
        float gapY = 4.0f;
        juce::Rectangle<float> amountRect(centreX - boxW * 0.5f, centreY - boxH * 0.5f, boxW, boxH);
        juce::Rectangle<float> autoRect(amountRect.withY(amountRect.getY() - boxH - gapY));
        juce::Rectangle<float> rndRect(amountRect.withY(amountRect.getY() + boxH + gapY));
        if (autoRect.contains(e.position))
        {
            autoRandomizePattern = !autoRandomizePattern;
            repaint();
            return;
        }
        if (rndRect.contains(e.position))
        {
            randomizePatternSteps();
            rndTriggerFlash = 1.0f;
            startFadeTimer();
            repaint();
            return;
        }
        if (amountRect.contains(e.position))
        {
            if (e.getNumberOfClicks() > 1)
            {
                // Reset to a sensible mid value within 1..16
                rndPatternAmount = 8;
                rndAmountDragging = false;
                repaint();
                return;
            }
            rndAmountDragging = true;
            rndDragOrigin = e.position;
            rndAmountStart = rndPatternAmount;
            repaint();
            return;
        }
    }
    switch (hr.type)
    {
    case ZoneType::RingWedge:
        if (onResyncStepRequested)
            onResyncStepRequested(hr.index);
        repaint();
        return;
    case ZoneType::ClickPulseButton:
        clickToPulseOn = !clickToPulseOn;
        if (onClickPulseRequested)
            onClickPulseRequested(clickToPulseOn);
        startFadeTimer();
        repaint();
        return;
    case ZoneType::PopupItem3:
    {
        if (popup3.isVisible() || popup3.isAnimating())
        {
            int clicked = hr.index;
            auto labs = popup3.getLabels();
            if (clicked >= 0 && clicked < labs.size())
                mainCircle3Label = labs[clicked];
            // Map popup3 selection to auto trigger scheduling.
            // Indices: 0:64 1:32 2:16 3:8 4:4 5:2 6:1 7:OFF
            // Reset progress counter on change so cyan progress respects the
            // new global bar count immediately.
            autoTriggerRandom = false;
            autoTriggerIntervalBars = 0;
            switch (clicked)
            {
            case 0:
                autoTriggerIntervalBars = 64;
                break;
            case 1:
                autoTriggerIntervalBars = 32;
                break;
            case 2:
                autoTriggerIntervalBars = 16;
                break;
            case 3:
                autoTriggerIntervalBars = 8;
                break;
            case 4:
                autoTriggerIntervalBars = 4;
                break;
            case 5:
                autoTriggerIntervalBars = 2;
                break;
            case 6:
                autoTriggerIntervalBars = 1;
                break;
            case 7:
            default:
                autoTriggerIntervalBars = 0;
                break; // OFF
            }
            // Changing interval while running should restart the progress
            // from 0 so the arc aligns with the new bar count immediately.
            barsSinceAutoTrigger = 0;
            popup3.startCollapse();
            flashes.set(3, 1.0f);
            startFadeTimer();
            if (onPopup3Selected)
                onPopup3Selected(clicked);
            repaint();
            return;
        }
        break;
    }
    case ZoneType::PopupItem6:
    {
        if (popup6.isVisible() || popup6.isAnimating())
        {
            int clicked = hr.index;
            auto vals = popup6.getValues();
            if (clicked >= 0 && clicked < vals.size())
                mainCircle6Value = vals[clicked];
            popup6.startCollapse();
            flashes.set(6, 1.0f);
            startFadeTimer();
            if (onClockRateIndexRequested)
                onClockRateIndexRequested(vals[clicked]);
            repaint();
            return;
        }
        break;
    }
    case ZoneType::SmallCircle:
        if (hr.index == 1)
        {
            flashes.set(1, 1.0f);
            if (onRunToggleRequested)
                onRunToggleRequested(true);
            if (onTriggerOnceRequested)
                onTriggerOnceRequested();
            if (ring)
            {
                int logical = (ringSelectedSegment >= 0) ? ringSelectedSegment : 0;
                ring->flashSegmentLogical(logical + 1);
            }
            startFadeTimer();
            repaint();
            return;
        }
        if (hr.index == 3)
        {
            togglePopup3();
            return;
        }
        if (hr.index == 6)
        {
            togglePopup6();
            return;
        }
        // Do NOT allow rotary to change on click, only on drag
        // if (hr.index == 7)
        // {
        //     isDraggingOffsetRotary = true;
        //     offsetRotaryT = angleToRotaryT(e.position);
        //     {
        //         onClickRateRequested(juce::jlimit(0, offsetRotarySteps - 1, nearest));
        //     }
        //     repaint();
        //     return;
        // }
        break;
    case ZoneType::ShufflePoint:
    {
        int sid = hr.index;
        // If in linear mode, clicking a shuffle point should reposition the linear handle
        // rather than forcibly switching back to discrete (909) mode.
        if (linearShuffleMode && shufflePositions.size() > 1)
        {
            // Map shuffle id (1..N) to linear position [0..1]
            int n = shufflePositions.size();
            linearShufflePos = juce::jlimit(0.0f, 1.0f, (sid - 1) / (float)(n - 1));
            linearShuffleAmount = 0.5f + 0.25f * linearShufflePos;
            // Update selectedShuffle for consistency (round to nearest)
            selectedShuffle = sid;
            isDraggingShuffle = true;
            isDraggingLinear = true;
            if (onLinearShuffleAmountChanged)
                onLinearShuffleAmountChanged(linearShuffleAmount);
            // Do not notify discrete shuffle step while in linear mode to avoid snapping
            repaint();
            return;
        }
        // Discrete 909 mode
        selectedShuffle = sid;
        isDraggingShuffle = true;
        isDraggingLinear = false;
        linearShuffleMode = false; // ensure discrete mode
        if (onShuffleStepRequested)
            onShuffleStepRequested(juce::jlimit(1, getShuffleCount(), selectedShuffle));
        repaint();
        return;
    }
    case ZoneType::RotaryHandle:
        // Only start drag, do not change value on click
        isDraggingOffsetRotary = true;
        offsetDragStart = e.position;
        offsetRotaryStartT = offsetRotaryT;
        // Do NOT update offsetRotaryT or call onClickRateRequested here
        repaint();
        return;
    default:
        break;
    }
}
void PlaygroundComponent::mouseDrag(const juce::MouseEvent &e)
{
    if (patternDragActive && patternEditMode)
    {
        // Angle-derived wedge index allows continuing drag outside pattern band.
        if (ring)
        {
            auto b = ring->getBounds().toFloat();
            juce::Point<float> centre(b.getCentreX(), b.getCentreY());
            float dx = e.position.x - centre.x;
            float dy = e.position.y - centre.y;
            float angle = std::atan2(dy, dx);
            // Hit testing start at 12 o'clock for identity mapping.
            // atan2 returns 0 at 3 o'clock, so 12 o'clock is -pi/2.
            const float startAt12 = -juce::MathConstants<float>::halfPi;
            float rel = angle - startAt12;
            while (rel < 0.0f)
                rel += juce::MathConstants<float>::twoPi;
            float slice = juce::MathConstants<float>::twoPi / 16.0f;
            int raw = (int)std::floor(rel / slice) & 15;
            // Apply same +4 rotation as click hit-test mapping so visual wedge under cursor matches drag index.
            static constexpr int kHighlightRotation = 0;
            int rotated = (raw + kHighlightRotation) & 15;
            if (rotated != lastPatternDragWedge)
            {
                lastPatternDragWedge = rotated;
                pattern.setStep(rotated, patternDragPaintState);
                if (onPatternChanged)
                    onPatternChanged((uint16_t) pattern.getBitmask());
                repaint();
            }
        }
        return; // swallow other drag behaviors while pattern editing
    }
    if (patternEditMode && rndAmountDragging)
    {
        // Map drag to 1..16 range; keep sensitivity reasonable.
        float dy = e.position.y - rndDragOrigin.y; // drag up decreases y -> increase amount
        int delta = (int)std::round(-dy / 4.0f);   // slightly slower sensitivity
        rndPatternAmount = juce::jlimit(1, 16, rndAmountStart + delta);
        repaint();
        return;
    }
    if (isDraggingShuffle && shufflePositions.size() > 0)
    {
        if (isDraggingLinear)
        {
            linearShufflePos = pointToShufflePolylineT(e.position);
            linearShuffleAmount = 0.5f + 0.25f * linearShufflePos;
            if (onLinearShuffleAmountChanged)
                onLinearShuffleAmountChanged(linearShuffleAmount);
            // Suppress discrete shuffle notifications in linear mode
            int n = shufflePositions.size();
            int near2 = 1 + (int)std::round(linearShufflePos * (n - 1));
            if (near2 != selectedShuffle)
                selectedShuffle = juce::jlimit(1, n, near2);
            repaint();
            return;
        }
        int bestId = selectedShuffle;
        float bestd = std::numeric_limits<float>::max();
        for (int i = 0; i < shufflePositions.size(); ++i)
        {
            const auto &p = shufflePositions.getReference(i);
            float dx = e.position.x - p.x, dy = e.position.y - p.y;
            float d2 = dx * dx + dy * dy;
            if (d2 < bestd)
            {
                bestd = d2;
                bestId = p.id;
            }
        }
        if (bestId != selectedShuffle)
        {
            selectedShuffle = bestId;
            if (onShuffleStepRequested)
                onShuffleStepRequested(juce::jlimit(1, shufflePositions.size(), selectedShuffle));
            repaint();
        }
        return;
    }
    if (isDraggingOffsetRotary)
    {
        float dx = e.position.x - offsetDragStart.x, dy = e.position.y - offsetDragStart.y;
        float absdx = std::fabs(dx), absdy = std::fabs(dy);
        float deltaT = (absdx > absdy) ? (dx / 100.0f) : (-dy / 100.0f);
        float t = juce::jlimit(0.0f, 1.0f, offsetRotaryStartT + deltaT);
        int steps = offsetRotarySteps;
        if (steps > 1)
        {
            int nearest = (int)std::round(t * (steps - 1));
            t = nearest / (float)(steps - 1);
        }
        offsetRotaryT = t;
        if (onClickRateRequested)
        {
            int nearest = (int)std::round(offsetRotaryT * (offsetRotarySteps - 1));
            onClickRateRequested(juce::jlimit(0, offsetRotarySteps - 1, nearest));
        }
        repaint();
        return;
    }
    mouseMove(e);
}
void PlaygroundComponent::mouseUp(const juce::MouseEvent &e)
{
    if (patternDragActive)
    {
        patternDragActive = false;
        lastPatternDragWedge = -1;
        repaint();
    }
    if (isDraggingShuffle)
    {
        if (isDraggingLinear)
        {
            isDraggingLinear = false;
            isDraggingShuffle = false;
            int n = shufflePositions.size();
            int nearest = 1 + (int)std::round(linearShufflePos * (n - 1));
            selectedShuffle = juce::jlimit(1, n, nearest);
            linearShuffleAmount = 0.5f + 0.25f * linearShufflePos;
            if (onLinearShuffleAmountChanged)
                onLinearShuffleAmountChanged(linearShuffleAmount);
            repaint();
            return;
        }
        isDraggingShuffle = false;
        repaint();
        return;
    }
    if (isDraggingOffsetRotary)
    {
        isDraggingOffsetRotary = false;
        int n = offsetRotarySteps;
        int nearest = (int)std::round(offsetRotaryT * (n - 1));
        offsetRotaryT = (n > 1) ? nearest / (float)(n - 1) : 0.0f;
        if (onClickRateRequested)
            onClickRateRequested(juce::jlimit(0, n - 1, nearest));
        repaint();
        return;
    }
    if (rndAmountDragging)
    {
        rndAmountDragging = false;
        repaint();
    }
    juce::ignoreUnused(e);
}
void PlaygroundComponent::mouseMove(const juce::MouseEvent &e)
{
    HitContext ctx;
    ctx.playground = this;
    ctx.pattern = patternEditMode ? &pattern : nullptr;
    ctx.ringArea = ring ? ring->getBounds() : juce::Rectangle<int>();
    ctx.patternEditMode = patternEditMode;
    ctx.submenuActive = false;
    ctx.outerDiameter = (int)(ring ? ring->getOuterRadius() * 2.0f : 140);
    ctx.innerDiameter = (int)(ring ? ring->getInnerRadius() * 2.0f : 110);
    // Auto-close popup3 when cursor leaves guard circle to stop it from lingering and stealing intent
    if (popup3.isVisible() && !popup3.isAnimating())
    {
        float x3, y3, r3;
        if (getCircleInfo(3, x3, y3, r3))
        {
            const float guardR = 60.0f;
            const float dx = e.position.x - x3;
            const float dy = e.position.y - y3;
            if (dx * dx + dy * dy > guardR * guardR)
            {
                popup3.startCollapse();
                startFadeTimer();
            }
        }
    }
    // Auto-close popup6 when cursor leaves its guard circle
    if (popup6.isVisible() && !popup6.isAnimating())
    {
        float x6, y6, r6;
        if (getCircleInfo(6, x6, y6, r6))
        {
            const float guardR6 = 50.0f;
            const float dx6 = e.position.x - x6;
            const float dy6 = e.position.y - y6;
            if (dx6 * dx6 + dy6 * dy6 > guardR6 * guardR6)
            {
                popup6.startCollapse();
                startFadeTimer();
            }
        }
    }
    auto hr = performHitTest(e.position, ctx, patternEditMode ? InteractionMode::PatternEdit : InteractionMode::Normal);
    if (patternEditMode)
    {
        int newPatternHover = (hr.type == ZoneType::PatternWedge) ? hr.index : -1;
        if (newPatternHover != patternHoverIndex)
        {
            patternHoverIndex = newPatternHover;
            repaint();
        }
        if (hr.type == ZoneType::None)
        {
            // When editing pattern, swallow other hover mapping (prevent underlying circle hover noise)
            juce::Component::mouseMove(e);
            return;
        }
    }
    int newHover = -1;
    // Track shuffle hover separately for glow (do not overwrite selection while dragging)
    if (!isDraggingShuffle)
    {
        if (hr.type == ZoneType::ShufflePoint)
        {
            hoverShuffleId = hr.index;
        }
        else
        {
            // If we're in linear shuffle mode, also consider hovering the linear handle
            // positioned along the shuffle polyline (so the user sees hover feedback when
            // near the movable handle between discrete points).
            if (linearShuffleMode && shufflePositions.size() > 1)
            {
                auto posPt = getPointAlongShufflePolyline(linearShufflePos);
                float Rstart = shufflePositions.getReference(0).r;
                float Rend = shufflePositions.getReference(shufflePositions.size() - 1).r;
                float R = Rstart + (Rend - Rstart) * linearShufflePos;
                float dx = e.position.x - posPt.x;
                float dy = e.position.y - posPt.y;
                float buf = 4.0f; // hover buffer so handle is easy to hit
                if (dx * dx + dy * dy <= (R + buf) * (R + buf))
                {
                    // Map to nearest discrete id for consistent drawing
                    int n = shufflePositions.size();
                    int near = 1 + (int)std::round(linearShufflePos * (n - 1));
                    hoverShuffleId = juce::jlimit(1, n, near);
                }
                else
                {
                    hoverShuffleId = -1;
                }
            }
            else
            {
                hoverShuffleId = -1;
            }
        }
    }
    // Separate hover for rotary and Click Pulse
    switch (hr.type)
    {
    case ZoneType::RingCenter:
        newHover = 0;
        break;
    case ZoneType::SmallCircle:
        newHover = hr.index;
        break;
    case ZoneType::ClickPulseButton:
        newHover = 8; // Unique index for Click Pulse
        break;
    case ZoneType::RotaryHandle:
        newHover = 7; // Unique index for rotary
        break;
    case ZoneType::PopupItem3:
        newHover = 3;
        break;
    case ZoneType::PopupItem6:
        newHover = 6;
        break;
    default:
        break;
    }
    // Track hover for rotary and Click Pulse independently
    bool rotaryHover = (hr.type == ZoneType::RotaryHandle);
    bool pulseHover = (hr.type == ZoneType::ClickPulseButton);
    if (pulseHover != clickToPulseHover)
    {
        clickToPulseHover = pulseHover;
        setMouseCursor(clickToPulseHover ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }
    if (rotaryHover != rotaryHandleHover)
    {
        rotaryHandleHover = rotaryHover;
        repaint();
    }
    if (newHover != hoverIndex)
    {
        hoverIndex = newHover;
        repaint();
    }
    juce::Component::mouseMove(e);
}
bool PlaygroundComponent::keyPressed(const juce::KeyPress &kp)
{
    // No key handling needed post cleanup; always allow other components/host to process.
    juce::ignoreUnused(kp);
    return false;
}

void PlaygroundComponent::timerCallback()
{
    double now = juce::Time::getMillisecondCounterHiRes();
    double dt = now - lastTickMs;
    lastTickMs = now;
    bool any = false;
    for (int i = 0; i < flashes.size(); ++i)
    {
        float v = flashes[i];
        if (v > 0.0f)
        {
            v *= 0.9f;
            if (v < 0.01f)
                v = 0.0f;
            flashes.set(i, v);
            if (v > 0.0f)
                any = true;
        }
    }
    if (popup3.isAnimating() || popup3.isVisible())
    {
        popup3.update(now, dt);
        any = true;
    }
    if (popup6.isAnimating() || popup6.isVisible())
    {
        popup6.update(now, dt);
        any = true;
    }
    if (rndTriggerFlash > 0.01f)
    {
        rndTriggerFlash *= 0.88f;
        if (rndTriggerFlash < 0.01f)
            rndTriggerFlash = 0.0f;
        any = true;
    }
    // Pending status pulse animation
    if (statusState == StatusState::Pending)
    {
        statusPulse += (float)(dt * 0.012f); // speed factor
        if (statusPulse > juce::MathConstants<float>::twoPi)
            statusPulse -= juce::MathConstants<float>::twoPi;
        any = true; // keep timer running while pulsing
    }
    if (!any)
    {
        stopTimer();
        isTimerRunning = false;
    }
    repaint();
}
void PlaygroundComponent::resized()
{
    // Position children; the run indicator is now drawn statically in paint()
    // so there is no persistent visible child to position. Keep ring frontmost
    // among children so wedges/chase render above other UI children.
    if (ring)
        ring->toFront(false);
}
void PlaygroundComponent::makeButton(int idx)
{
    if (idx < 0 || idx >= circles.size())
        return;
    const auto &c = circles.getReference(idx);
    auto *b = onOffButtons.add(new OnOffButton(theme));
    b->setBounds((int)(c.x - 14.0f), (int)(c.y - 14.0f), 28, 28);
    onOffButtonIdxs.add(idx);
    b->onHoverChanged = [this, idx](bool over)
    { hoverIndex=over? idx : (hoverIndex==idx? -1: hoverIndex); repaint(); };
    if (idx == 8)
    {
        b->onToggled = [this](bool on)
        { if (onTriggerModeRequested) onTriggerModeRequested(on); };
    }
    else if (idx == 5)
    {
        b->onToggled = [this](bool on)
        {
            int n = shufflePositions.size();
            linearShuffleMode = on;
            if (on)
            {
                // Enter linear: derive handle from current discrete selection.
                if (n > 1)
                    linearShufflePos = (selectedShuffle - 1) / (float)(n - 1);
                isDraggingLinear = false;
            }
            else
            {
                // Exit linear: choose discrete id by rounding UP so we never jump backwards.
                if (n > 1)
                {
                    float raw = linearShufflePos * (n - 1); // 0..(n-1)
                    int ceilIdx = (int)std::ceil(raw);      // 0..(n-1)
                    selectedShuffle = juce::jlimit(1, n, ceilIdx + 1);
                }
                isDraggingLinear = false;
            }
            if (onLinearShuffleModeChanged)
                onLinearShuffleModeChanged(on);
            repaint();
        };
        // Run indicator is drawn statically in PlaygroundComponent::paint()
        // handled via the ring center callback which forwards to the editor
    }
    else if (idx == 2)
    {
        b->onToggled = [this](bool on)
        { if (onPatternEditToggled) onPatternEditToggled(on); };
    }
    addAndMakeVisible(b);
}
juce::Point<float> PlaygroundComponent::getPointAlongShufflePolyline(float t) const
{
    if (shufflePositions.size() == 0)
        return {0, 0};
    int n = shufflePositions.size();
    juce::Array<juce::Point<float>> pts;
    for (int i = 0; i < n; ++i)
        pts.add({shufflePositions[i].x, shufflePositions[i].y});
    std::vector<float> segLen(std::max(0, n - 1));
    float total = 0.0f;
    for (int i = 0; i + 1 < n; ++i)
    {
        auto a = pts[i];
        auto b = pts[i + 1];
        float l = std::hypot(b.x - a.x, b.y - a.y);
        segLen[(size_t)i] = l;
        total += l;
    }
    if (total <= 0.0001f)
        return pts[0];
    float target = juce::jlimit(0.0f, 1.0f, t) * total;
    float acc = 0.0f;
    for (int i = 0; i + 1 < n; ++i)
    {
        float l = segLen[(size_t)i];
        if (target <= acc + l || i + 1 == n - 1)
        {
            float local = (l <= 0.0f) ? 0.0f : ((target - acc) / l);
            local = juce::jlimit(0.0f, 1.0f, local);
            auto a = pts[i];
            auto b = pts[i + 1];
            return a + (b - a) * local;
        }
        acc += l;
    }
    return pts[n - 1];
}
float PlaygroundComponent::pointToShufflePolylineT(juce::Point<float> p) const
{
    int n = shufflePositions.size();
    if (n == 0)
        return 0.0f;
    juce::Array<juce::Point<float>> pts;
    for (int i = 0; i < n; ++i)
        pts.add({shufflePositions[i].x, shufflePositions[i].y});
    std::vector<float> segLen(std::max(0, n - 1));
    float total = 0.0f;
    for (int i = 0; i + 1 < n; ++i)
    {
        auto a = pts[i];
        auto b = pts[i + 1];
        float l = std::hypot(b.x - a.x, b.y - a.y);
        segLen[(size_t)i] = l;
        total += l;
    }
    if (total <= 0.0001f)
        return 0.0f;
    float bestD = std::numeric_limits<float>::max();
    float bestT = 0.0f;
    float acc = 0.0f;
    for (int i = 0; i + 1 < n; ++i)
    {
        auto a = pts[i];
        auto b = pts[i + 1];
        auto ab = b - a;
        float denom = ab.x * ab.x + ab.y * ab.y;
        float u = 0.0f;
        if (denom > 0.00001f)
        {
            u = ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / denom;
            u = juce::jlimit(0.0f, 1.0f, u);
        }
        auto proj = a + ab * u;
        float d2 = (proj.x - p.x) * (proj.x - p.x) + (proj.y - p.y) * (proj.y - p.y);
        if (d2 < bestD)
        {
            bestD = d2;
            float segLenLocal = segLen[(size_t)i];
            float tAlong = (segLenLocal <= 0.0f) ? 0.0f : (acc + u * segLenLocal) / total;
            bestT = juce::jlimit(0.0f, 1.0f, tAlong);
        }
        acc += segLen[(size_t)i];
    }
    return bestT;
}
float PlaygroundComponent::angleToRotaryT(juce::Point<float> p) const
{
    if (circles.size() <= 7)
        return 0.0f;
    const auto &c = circles.getReference(7);
    float dx = p.x - c.x, dy = p.y - c.y;
    float ang = std::atan2(dy, dx);
    const float startAng = -juce::MathConstants<float>::pi * 0.75f;
    const float endAng = juce::MathConstants<float>::pi * 0.75f;
    float range = endAng - startAng;
    float clamped = juce::jlimit(startAng, endAng, ang);
    return juce::jlimit(0.0f, 1.0f, (clamped - startAng) / range);
}
// (legacy hitTestIndex removed; routing now centralised in performHitTest)
void PlaygroundComponent::startFadeTimer()
{
    if (!isTimerRunning)
    {
        isTimerRunning = true;
        lastTickMs = juce::Time::getMillisecondCounterHiRes();
        startTimerHz(60);
    }
}
void PlaygroundComponent::startExpand6()
{
    popup6.startExpand();
    startFadeTimer();
}
void PlaygroundComponent::startCollapse6()
{
    popup6.startCollapse();
    startFadeTimer();
}
void PlaygroundComponent::startExpand3()
{
    popup3.startExpand();
    startFadeTimer();
}
void PlaygroundComponent::startCollapse3()
{
    popup3.startCollapse();
    startFadeTimer();
}
void PlaygroundComponent::startBeatPulse()
{
    if (circles.size() == 0)
        return;
    if (scheduledPulseAtMs.size() != flashes.size())
    {
        scheduledPulseAtMs.resize(flashes.size());
        for (int i = 0; i < scheduledPulseAtMs.size(); ++i)
            scheduledPulseAtMs.set(i, 0.0);
    }
    double now = juce::Time::getMillisecondCounterHiRes();
    int n = circles.size();
    for (int i = 0; i < n; ++i)
    {
        double t = now + (double)i * basePulseDelayMs;
        scheduledPulseAtMs.set(i, t);
    }
    startFadeTimer();
}
void PlaygroundComponent::randomizePatternSteps()
{
    // Randomize exactly N UI steps (N = rndPatternAmount, clamped to 1..16).
    const int N = juce::jlimit(1, 16, rndPatternAmount);
    // Clear all steps first
    for (int i = 0; i < 16; ++i)
        pattern.setStep(i, false);
    // Pick N unique UI steps
    juce::Array<int> all;
    for (int ui = 1; ui <= 16; ++ui) all.add(ui);
    // Fisher-Yates shuffle using autoTriggerRng
    for (int i = all.size() - 1; i > 0; --i)
    {
        int j = autoTriggerRng.nextInt(i + 1);
        std::swap(all.getReference(i), all.getReference(j));
    }
    for (int k = 0; k < N && k < all.size(); ++k)
    {
        int ui = all[k];
        int stored = uiStep1to16ToStoredIndex(ui);
        pattern.setStep(stored, true);
    }
    if (onPatternChanged)
        onPatternChanged((uint16_t) pattern.getBitmask());
}

// ======= UI setters implemented inline =======
void PlaygroundComponent::setPopup3Index(int popupIndex)
{
    auto labs = popup3.getLabels();
    if (popupIndex >= 0 && popupIndex < labs.size())
        mainCircle3Label = labs.getReference(popupIndex);

    // Update interval and reset arc progress
    int newInterval = 0;
    juce::String lbl = mainCircle3Label.trim();
    if (lbl.equalsIgnoreCase("OFF"))
        newInterval = 0;
    else if (lbl.equalsIgnoreCase("RND"))
        newInterval = 0; // handled elsewhere
    else
        newInterval = lbl.getIntValue();
    if (autoTriggerIntervalBars != newInterval)
    {
        autoTriggerIntervalBars = newInterval;
        barsSinceAutoTrigger = 0;
    }
    repaint();
}

void PlaygroundComponent::setMainCircle6Value(int val)
{
    mainCircle6Value = val;
    repaint();
}

void PlaygroundComponent::setSelectedShuffle(int v)
{
    selectedShuffle = v;
    repaint();
}

void PlaygroundComponent::setLinearShuffleModeState(bool on)
{
    linearShuffleMode = on;
    repaint();
}

void PlaygroundComponent::setLinearShuffleAmount(float amount)
{
    linearShuffleAmount = juce::jlimit(0.5f, 0.75f, amount);
    linearShufflePos = juce::jlimit(0.0f, 1.0f, (linearShuffleAmount - 0.5f) / 0.25f);
    repaint();
}
