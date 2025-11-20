#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <cmath>
#include "LookAndFeels.h"
#include "PopupMenuRing.h"
#include <vector>

// OnOffButton copied from apps/layout_playground/Main.cpp (small circular toggle)
class OnOffButton : public juce::Component
{
public:
    OnOffButton()
    {
        setSize (diameter, diameter);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        auto c = bounds.getCentre();
        g.setColour (UiThemeColours::accent());
        g.fillEllipse (bounds);

        const float innerR = 8.0f;
        g.setColour (UiThemeColours::base());
        g.fillEllipse (c.x - innerR, c.y - innerR, innerR*2.0f, innerR*2.0f);

        if (isOn)
        {
            const float dotR = 5.0f;
            g.setColour (UiThemeColours::cyan());
            g.fillEllipse (c.x - dotR, c.y - dotR, dotR*2.0f, dotR*2.0f);
        }
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        isOn = ! isOn;
        if (onToggled) onToggled (isOn);
        repaint();
    }

    void mouseEnter (const juce::MouseEvent&) override { if (onHoverChanged) onHoverChanged (true); }
    void mouseExit  (const juce::MouseEvent&) override { if (onHoverChanged) onHoverChanged (false); }

    void setState (bool on) { isOn = on; repaint(); }
    bool getState() const noexcept { return isOn; }

    std::function<void(bool)> onToggled;
    std::function<void(bool)> onHoverChanged;

private:
    static constexpr int diameter = 28;
    bool isOn = false;
};

// Ring16Component copied and simplified from apps/layout_playground/Main.cpp
class Ring16Component : public juce::Component, private juce::Timer
{
public:
    Ring16Component()
    {
        setSize ((int)(outerR*2), (int)(outerR*2));
        masterStartMs = juce::Time::getMillisecondCounterHiRes();
        startTimerHz (60);
        globalRestartLogical = 0; // default
        selected = logicalToRaw (globalRestartLogical);
        if (selected >= 0 && selected < ringFlashes.size())
            ringFlashes.set (selected, 1.0f);
        // Initialise fade trail values (all zero)
        segmentFade.resize(16);
        for (int i = 0; i < segmentFade.size(); ++i) segmentFade.set(i, 0.0f);
        // Initialise manual-flash hold timers (all zero -> no hold)
        segmentHoldUntilMs.resize(16);
        for (int i = 0; i < segmentHoldUntilMs.size(); ++i) segmentHoldUntilMs.set(i, 0.0);
        fullRingFlash = 0.0f;
    }

    // Centralised flash/fade helper: applies flash intensity, seeds fade trail, and optional hold window.
    void flashSegmentRaw(int rawIndex, float flashIntensity = 1.0f, bool seedFade = true, bool applyHold = true)
    {
        if (rawIndex < 0 || rawIndex >= 16) return;
        if (rawIndex < ringFlashes.size()) ringFlashes.set(rawIndex, juce::jlimit(0.0f, 1.5f, flashIntensity));
        if (seedFade && rawIndex < segmentFade.size()) segmentFade.set(rawIndex, 1.0f);
        if (applyHold && rawIndex < segmentHoldUntilMs.size())
        {
            const double now = juce::Time::getMillisecondCounterHiRes();
            segmentHoldUntilMs.set(rawIndex, now + kFlashHoldMs);
        }
        repaint();
    }

    void setBpm(double b) { if (b > 1.0) bpm = b; }
    void setSpeedMultiplier(double m) { speedMultiplier = juce::jlimit(0.25, 4.0, m); }

    void paint (juce::Graphics& g) override
    {
        auto boundsF = getLocalBounds().toFloat();
        const float innerProp = innerR / outerR;
        const float step = juce::MathConstants<float>::twoPi / 16.0f;
        const float start0 = -juce::MathConstants<float>::halfPi;

        // Determine current playhead raw index (external overrides internal)
        int playheadRaw = -1;
        if (useExternalPlayhead)
            playheadRaw = externalPlayheadRaw;
        else if (lastStepIndex >= 0)
            playheadRaw = lastStepIndex;

        for (int i = 0; i < 16; ++i)
        {
            const float a0 = start0 + i*step;
            const float a1 = a0 + step;
            juce::Path seg; seg.addPieSegment(boundsF, a0, a1, innerProp);

            const bool isSelected = (i == selected);
            const bool isPlayhead = (i == playheadRaw);
            const float fade = juce::jlimit(0.0f, 1.0f, (i >= 0 && i < segmentFade.size()) ? segmentFade[i] : 0.0f);
            const float flash = (i >= 0 && i < ringFlashes.size()) ? ringFlashes[i] : 0.0f;

            // Base: nothing drawn (transparent) so backdrop donut shows unless state below applies.
            if (isSelected)
            {
                // Selected offset segment baseline at 0.5 alpha cyan.
                float alpha = 0.5f;
                // When playhead passes through the selected segment, elevate to full cyan.
                if (isPlayhead)
                    alpha = 1.0f;
                g.setColour(UiThemeColours::cyan().withAlpha(alpha));
                g.fillPath(seg);
                // Continue layering: allow flash/fade trail on top (no early continue)
            }
            else if (isPlayhead)
            {
                // Non-selected playhead segment: full cyan while playing, dimmer if not.
                g.setColour(UiThemeColours::cyan().withAlpha(isPlaying ? 1.0f : 0.5f));
                g.fillPath(seg);
            }

            // Overlay flash/fade trail if present (ensure visibility even on selected segment).
            if (flash > 0.01f)
            {
                // Primary cyan flash
                g.setColour(UiThemeColours::cyan().withAlpha(juce::jlimit(0.0f, 1.0f, flash)));
                g.fillPath(seg);
                // Subtle white highlight so flashes are visible even when the segment is already full cyan
                const float whiteAlpha = juce::jlimit(0.0f, 0.45f, flash * 0.35f);
                if (whiteAlpha > 0.01f)
                {
                    g.setColour(juce::Colours::white.withAlpha(whiteAlpha));
                    g.fillPath(seg);
                }
            }
            else if (fade > 0.01f && ! isPlayhead)
            {
                // Fade trail (skip if already full playhead fill overlies it).
                g.setColour(UiThemeColours::cyan().withAlpha(fade));
                g.fillPath(seg);
            }
            else if (! isSelected && ! isPlayhead && i == hovered)
            {
                // Subtle hover highlight only when no other state applied.
                g.setColour(UiThemeColours::accent().darker(0.25f).withAlpha(0.25f));
                g.fillPath(seg);
            }
        }

        // Full-ring flash overlay (restart applied). Draw subtle cyan ring behind wedges.
        if (fullRingFlash > 0.01f)
        {
            juce::Path ringPath; // outer donut minus inner circle (same geometry as segments container)
            ringPath.addEllipse(boundsF.getCentreX() - outerR, boundsF.getCentreY() - outerR, outerR*2.0f, outerR*2.0f);
            ringPath.addEllipse(boundsF.getCentreX() - innerR, boundsF.getCentreY() - innerR, innerR*2.0f, innerR*2.0f);
            ringPath.setUsingNonZeroWinding(false);
            g.setColour(UiThemeColours::cyan().withAlpha(juce::jlimit(0.0f, 0.85f, fullRingFlash)));
            g.fillPath(ringPath);
        }

        // Do not fill inner circle to keep the donut transparent; remove text overlay for a clean look
        // Optionally draw a subtle outline to hint the inner boundary
        const auto c = boundsF.getCentre();
        const float innerD = innerR*2.0f;
        juce::Rectangle<float> innerCircle (c.x - innerR, c.y - innerR, innerD, innerD);
        g.setColour (UiThemeColours::accent().withAlpha (0.15f));
        g.drawEllipse (innerCircle, 1.0f);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const bool nowInner = isInsideInner (e.position);
        if (nowInner != innerHovered)
        {
            innerHovered = nowInner;
            if (onInnerHover) onInnerHover (innerHovered);
        }

        const int h = hitSegment (e.position);
        if (h != hovered)
        {
            hovered = h;
            setMouseCursor (hovered >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            if (onHoverChanged) onHoverChanged (rawToLogical (hovered));
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hovered != -1)
        {
            hovered = -1;
            if (onHoverChanged) onHoverChanged (-1);
        }
        if (innerHovered)
        {
            innerHovered = false;
            if (onInnerHover) onInnerHover (false);
        }
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (isInsideInner (e.position)) { togglePlay(); return; }
        const int s = hitSegment (e.position);
        if (s >= 0)
        {
            selected = s;
            if (onClicked) onClicked (rawToLogical (selected));
            repaint();
        }
    }

    std::function<void(int)> onHoverChanged;
    std::function<void(int)> onClicked;
    std::function<void(bool)> onPlayStateChanged;
    std::function<void(int)> onStepChanged;
    std::function<void(bool)> onInnerHover;

    int getCurrentStepLogical() const
    {
        if (lastStepIndex < 0) return -1;
        return rawToLogical (lastStepIndex) + 1;
    }

    float getOuterRadius() const noexcept { return outerR; }
    float getInnerRadius() const noexcept { return innerR; }

    void requestRestartAtNextMasterBoundary() { pendingRestartAtNextBoundary = true; }
    void setResyncOnBarOneEnabled (bool en) { resyncOnBarOneEnabled = en; }
    void setGlobalRestartLogical (int logical) { if (logical >= 0 && logical < 16) globalRestartLogical = logical; }
    int getGlobalRestartLogical() const noexcept { return globalRestartLogical; }
    // Allow external control of play state
    void setPlaying (bool on)
    {
        isPlaying = on;
        if (isPlaying)
        {
            lastTickMs = juce::Time::getMillisecondCounterHiRes();
            playStartMs = lastTickMs;
            lastStepIndex = -1;
        }
        repaint();
    }

    // Host-driven playhead: when enabled, the ring does not advance steps on its own.
    void setExternalPlayheadStepLogical (int step1to16)
    {
        const int s = juce::jlimit(1, 16, step1to16);
        externalPlayheadRaw = logicalToRaw (s - 1);
        useExternalPlayhead = true;
        // Light flash (0.4) with fade seed but no hold (keep motion subtle)
        flashSegmentRaw(externalPlayheadRaw, 0.4f, true, false);
    }

    void flashCurrentSegment()
    {
        // Determine current playhead raw index (external overrides internal)
        int playheadRaw = -1;
        if (useExternalPlayhead)
            playheadRaw = externalPlayheadRaw;
        else if (lastStepIndex >= 0)
            playheadRaw = lastStepIndex;
        flashSegmentRaw(playheadRaw, 1.0f, true, true);
    }
    // Flash a specific logical step (1..16) without changing the ring's
    // running playhead or external playhead state. This creates a single
    // segment flash/fade independent of the chase-light progression.
    void flashSegmentLogical(int step1to16)
    {
        const int s = juce::jlimit(1, 16, step1to16);
        const int logical0 = s - 1;
        const int raw = logicalToRaw(logical0);
        flashSegmentRaw(raw, 1.0f, true, true);
    }
    // Trigger a transient full-ring flash (restart applied or transport/run start)
    void triggerFullRingFlash(float intensity = 1.0f)
    {
        fullRingFlash = juce::jlimit(0.0f, 1.5f, intensity); // allow a bit >1 for initial brightness, clamp in paint
        repaint();
    }
    void clearExternalPlayhead() { useExternalPlayhead = false; externalPlayheadRaw = -1; repaint(); }

private:
    int hitSegment (juce::Point<float> p) const
    {
        auto boundsF = getLocalBounds().toFloat();
        const float innerProp = innerR / outerR;
        const float step = juce::MathConstants<float>::twoPi / 16.0f;
        const float start0 = -juce::MathConstants<float>::halfPi;

        const auto c = boundsF.getCentre();
        const float dx = p.x - c.x;
        const float dy = p.y - c.y;
        const float d2 = dx*dx + dy*dy;
        const float R2 = outerR*outerR;
        const float r2 = innerR*innerR;
        if (d2 < r2 || d2 > R2) return -1;

        for (int i = 0; i < 16; ++i)
        {
            const float a0 = start0 + i*step;
            const float a1 = a0 + step;
            juce::Path seg; seg.addPieSegment (boundsF, a0, a1, innerProp);
            if (seg.contains (p)) return i;
        }
        return -1;
    }

    bool isInsideInner (juce::Point<float> p) const
    {
        const auto c = getLocalBounds().toFloat().getCentre();
        const float dx = p.x - c.x; const float dy = p.y - c.y;
        return (dx*dx + dy*dy) <= (innerR*innerR);
    }

    bool hitTest (int x, int y) override
    {
        juce::Point<float> p ((float) x, (float) y);
        if (isInsideInner (p)) return true;
        return (hitSegment (p) >= 0);
    }

    // Visual rotation mapping: apply +4 offset so the chase/playhead starts at 12 o'clock instead of 9 o'clock.
    // raw indices (0..15) are rotated forward by 4 when converted to logical step numbers.
    // logical step 1 now appears at the 12 o'clock position for improved orientation.
    static int rawToLogical (int raw) { if (raw < 0) return -1; constexpr int offset = 4; return (raw - offset + 16) % 16; }
    static int logicalToRaw (int logical) { if (logical < 0) return -1; constexpr int offset = 4; return (logical + offset) % 16; }

    void togglePlay()
    {
        isPlaying = ! isPlaying;
        if (onPlayStateChanged) onPlayStateChanged (isPlaying);
        if (isPlaying)
        {
            lastTickMs = juce::Time::getMillisecondCounterHiRes();
            playStartMs = lastTickMs;
            lastStepIndex = -1;
        }
        repaint();
    }

    void timerCallback() override
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        const double dtMs = now - lastTickMs; lastTickMs = now;

        const double barMs = (60.0 / bpm) * 4.0 * 1000.0;
        const double masterElapsed = now - masterStartMs;
        const double masterPhase = std::fmod (masterElapsed, barMs) / barMs;
        int masterSeg = (int) std::floor (masterPhase * 16.0);
        masterSeg = juce::jlimit (0, 15, masterSeg);
        const bool masterAdvanced = (masterSeg != lastMasterSeg);

        if (masterAdvanced)
        {
            const int restartLogical = (globalRestartLogical >= 0 && globalRestartLogical < 16) ? globalRestartLogical : 0;
            if (pendingRestartAtNextBoundary)
            {
                const double stepMs = barMs / 16.0;
                playStartMs = now - (restartLogical * stepMs);
                lastStepIndex = -1;
                pendingRestartAtNextBoundary = false;
            }
            if (resyncOnBarOneEnabled && masterSeg == 0)
            {
                const double stepMs = barMs / 16.0;
                playStartMs = now - (restartLogical * stepMs);
                lastStepIndex = -1;
            }
        }
        lastMasterSeg = masterSeg;

        for (int i = 0; i < 16; ++i)
        {
            float flash = ringFlashes[i];
            if (flash > 0.0f)
            {
                if (!(i < segmentHoldUntilMs.size() && now < segmentHoldUntilMs[i]))
                {
                    flash *= kFlashDecayFactor;
                }
                if (flash < 0.001f) flash = 0.0f;
                ringFlashes.set(i, flash);
            }
            float fade = (i < segmentFade.size()) ? segmentFade[i] : 0.0f;
            if (fade > 0.0f)
            {
                // If a manual-trigger hold is active for this segment, skip decaying it.
                if (i < segmentHoldUntilMs.size() && now < segmentHoldUntilMs[i])
                    continue;
                // Linear-ish decay based on elapsed time. Original target ~333ms full decay.
                // Extend fade so manual trigger (idx1) flashes linger a bit longer for clearer visual feedback.
                // New target ~580ms full decay.
                fade -= (float)(dtMs / kFadeDecayMs);
                if (fade < 0.0f) fade = 0.0f;
                segmentFade.set(i, fade);
            }
        }

        // Decay full-ring flash (slightly slower than per-segment flashes so it's noticeable)
        if (fullRingFlash > 0.0f)
        {
            fullRingFlash *= 0.92f; // slower falloff
            if (fullRingFlash < 0.01f) fullRingFlash = 0.0f;
        }

        // Only drive internal progression when not using an external (host) playhead
        if (isPlaying && ! useExternalPlayhead)
        {
            const double elapsed = now - playStartMs;
            // Compute progression across bars so slower divisions (8,4) still traverse all 16 steps over 2/4 bars.
            const double bars = elapsed / barMs; // total bars elapsed (continuous)
            const double phaseScaled = std::fmod (bars * speedMultiplier, 1.0); // wrap to 0..1 after scaling
            int seg = (int) std::floor (phaseScaled * 16.0);
            seg = juce::jlimit (0, 15, seg);
            const int segRaw = (seg + logicalToRaw (0)) % 16;
            if (segRaw != lastStepIndex)
            {
                lastStepIndex = segRaw;
                flashSegmentRaw(segRaw, 1.0f, true, false); // internal progression flash (no hold)
                if (onStepChanged) onStepChanged (rawToLogical (segRaw) + 1);
            }
        }

        // ring->onStepChanged will be set below to schedule beat pulses per-circle
        repaint();
    }

    bool isPlaying = false;
    double bpm = 120.0;
    double playStartMs = 0.0;
    double lastTickMs = 0.0;
    int lastStepIndex = -1;
    juce::Array<float> ringFlashes { juce::Array<float> (16, 0.0f) };
    juce::Array<float> segmentFade; // per-wedge fade trail (0..1)
    juce::Array<double> segmentHoldUntilMs; // per-wedge hold-until timestamp to freeze decay after manual trigger
    bool useExternalPlayhead = false;
    int externalPlayheadRaw = -1;
    float fullRingFlash = 0.0f; // transient cyan flash overlay intensity

    // Centralised timing/decay constants
    static constexpr double kFlashHoldMs = 222.0;      // duration to freeze flash/fade decay after manual trigger
    static constexpr float  kFadeDecayMs = 33.0f;      // time for fade trail to fully decay
    static constexpr float  kFlashDecayFactor = 0.95f;  // per-tick multiplicative decay for flash intensity (when not held)

    double masterStartMs = 0.0;
    int lastMasterSeg = -1;
    double speedMultiplier { 1.0 }; // visual chase speed scaling (32->2.0,16->1.0,8->0.5,4->0.25)
    bool pendingRestartAtNextBoundary = false;
    bool resyncOnBarOneEnabled = false;
    int globalRestartLogical = -1;

    const float outerR = 70.0f;
    const float innerR = 55.0f;
    int hovered = -1;
    int selected = -1;
    bool innerHovered = false;
};

// PlaygroundComponent (header-only) adapted from apps/layout_playground/Main.cpp
class PlaygroundComponent : public juce::Component, private juce::Timer
{
public:
    PlaygroundComponent()
    {
        setSize (300, 240);

        // Store canonical positions in baseCircles so we can apply a header
        // overlay offset later without mutating the original design coords.
        baseCircles.add ({ 150.0f, 130.0f, 70.0f, juce::Colours::orangered });
        baseCircles.add ({ 244.0f, 85.0f, 35.0f, juce::Colours::goldenrod });
        baseCircles.add ({ 233.0f, 132.0f, 14.0f, juce::Colours::turquoise });
        baseCircles.add ({ 235.0f, 170.0f, 25.0f, juce::Colours::mediumpurple });
        baseCircles.add ({ 96.0f, 189.0f, 11.0f, juce::Colours::yellowgreen });
        baseCircles.add ({ 78.0f, 172.0f, 14.0f, juce::Colours::pink });
        baseCircles.add ({ 56.0f, 140.0f, 25.0f, juce::Colours::seagreen });
        baseCircles.add ({ 70.0f, 80.0f, 25.0f, juce::Colours::greenyellow });
        baseCircles.add ({ 201.0f, 64.0f, 14.0f, juce::Colours::blueviolet });

        // copy canonical positions into working circles (no header offset yet)
        circles.clear();
        for (int i = 0; i < baseCircles.size(); ++i)
            circles.add (baseCircles.getReference(i));

        flashes.resize (circles.size());
        for (int i = 0; i < flashes.size(); ++i) flashes.set (i, 0.0f);
        scheduledPulseAtMs.clear(); scheduledPulseAtMs.resize(flashes.size());
        for (int i = 0; i < scheduledPulseAtMs.size(); ++i) scheduledPulseAtMs.set(i, 0.0);

        hoverTexts = juce::StringArray {
            "Stop and re-start next bar+offset", "Re-trigger quantized", "Edit autofill pattern",
            "Autofill trigger", "Shuffle Amount", "Switch shuffle type 909 or linear",
            "Set scale 1/32,1/16,1/8,1/4", "Audio Click Division: off,beat,8th,16th,24ppq", "Re-sync at bar+offset on/off",
            // Setup submenu hover entries (forced indices)
            "Send midi-clocks while idle/stopped", "Legacy vs Modern start/stop behaviour", "Send song position pointer (SPP)"
        };

        ring.reset (new Ring16Component());
        if (circles.size() > 0)
        {
            const auto& c0 = circles.getReference (0);
            const int size = 140;
            ring->setBounds ((int)(c0.x - 70.0f), (int)(c0.y - 70.0f), size, size);
        }
        ring->onHoverChanged = [this](int logical){ ringHoverSegment = logical; repaint(); };
        ring->onInnerHover = [this](bool in){ if (in) { hoverIndex = 0; ringHoverSegment = -1; } else if (hoverIndex == 0) { hoverIndex = -1; } repaint(); };
        ring->onClicked = [this](int logical)
        {
            ringSelectedSegment = logical;
            if (ring) ring->setGlobalRestartLogical (logical);
            // notify host/editor that a logical resync step was selected (1..16)
            if (onResyncStepRequested) onResyncStepRequested(logical + 1);
            repaint();
        };
        // Treat the ring's inner circle as button idx 0: forward play state changes
        // so the host/editor can toggle RUN based on the ring's center click.
        ring->onPlayStateChanged = [this](bool on)
        {
            runState = on;
            if (onRunToggleRequested) onRunToggleRequested(on);
            repaint();
        };
        // When the ring advances a step, schedule beat pulses across circles but only on quarter-note beats
        ring->onStepChanged = [this](int logical1)
        {
            // logical1 is 1..16; trigger on quarter-note steps: 1,5,9,13
            if (logical1 >= 1 && ((logical1 - 1) % 4) == 0)
                startBeatPulse();
            repaint();
        };
        addAndMakeVisible (ring.get());

        makeButton (2);
        makeButton (5);
        makeButton (8);

        // Initialize shuffle positions (replace circle 5 region)
        shufflePositions.add ({ 1, 96.0f, 189.0f, 11.0f });
        shufflePositions.add ({ 2, 109.0f, 200.0f, 12.0f });
        shufflePositions.add ({ 3, 124.0f, 208.0f, 13.0f });
        shufflePositions.add ({ 4, 143.0f, 213.0f, 14.0f });
        shufflePositions.add ({ 5, 163.0f, 213.0f, 15.0f });
        shufflePositions.add ({ 6, 184.0f, 208.0f, 16.0f });
        shufflePositions.add ({ 7, 204.0f, 197.0f, 17.0f });

        // default visible shuffle id
        selectedShuffle = 1;
        // explicit per-shuffle text offsets (id 1..7). Edit these values to fine-tune placement.
        shuffleTextOffsets.clear();
        shuffleTextOffsets.add ({ 0.3f, 0.1f }); // id 1
        shuffleTextOffsets.add ({ 0.6f, 0.2f }); // id 2
        shuffleTextOffsets.add ({ 1.0f, 0.0f }); // id 3
        shuffleTextOffsets.add ({ 1.0f, 0.0f }); // id 4
        shuffleTextOffsets.add ({ 0.2f, 0.0f }); // id 5
        shuffleTextOffsets.add ({ 0.7f, 0.0f }); // id 6
        shuffleTextOffsets.add ({ 1.3f, 0.2f }); // id 7

        // labels / numeric values for the extra-6 popup children (clock divisions)
        extra6Labels.clear(); extra6Labels.add ("4"); extra6Labels.add ("8"); extra6Labels.add ("16"); extra6Labels.add ("32");
        extra6Values.clear(); extra6Values.add (4); extra6Values.add (8); extra6Values.add (16); extra6Values.add (32);
        mainCircle6Value = 16; // default shown in the main circle inset

        popup6.setValues (extra6Values);
        popup6.setLabels (extra6Labels);
        popup6.setAngles (extra6StartAngleDeg, extra6EndAngleDeg);
        popup6.setCircleRadius (extra6R);
        popup6.setExpansionRadius (extra6R - 6.0f);
        popup6.setAnimDurationMs (extra6AnimDurationMs);
        popup6.setStaggerMs (extra6StaggerMs);

        juce::StringArray popup3Labels { "RND", "64", "32", "16", "8", "4", "2", "1", "OFF" };
        popup3.setLabels (popup3Labels);
        popup3.setAngles (150.0f, -420.0f);
        popup3.setCircleRadius (11.0f);
        popup3.setExpansionRadius (30.0f - 15.0f);
        // Speed up popup3 (idx 3) by 1.5x for snappier UX
        popup3.setAnimDurationMs (extra6AnimDurationMs / 1.5f);
        popup3.setStaggerMs (extra6StaggerMs / 1.5f);

        if (ring) ring->toBack();

        extra6Progress.clear();
        for (int i = 0; i < extra6Count; ++i) extra6Progress.add (0.0f);
        extra6HoverScale.clear(); extra6HoverTarget.clear();
        for (int i = 0; i < extra6Count; ++i) { extra6HoverScale.add (1.0f); extra6HoverTarget.add (1.0f); }

        startTimerHz(60);
    }

    ~PlaygroundComponent() override
    {
        if (ring) removeChildComponent (ring.get());
        for (auto* b : onOffButtons) if (b) removeChildComponent (b);
    }

    // Callbacks that the host/editor can set to receive user actions from
    // the playground. These are optional and invoked when the user performs
    // the corresponding interaction.
    std::function<void(int)> onResyncStepRequested; // step 1..16
    std::function<void(bool)> onRunToggleRequested;
    std::function<void()> onTriggerOnceRequested;
    std::function<void(int)> onClockRateIndexRequested; // passes value (e.g. 4/8/16/32)
    std::function<void(int)> onShuffleStepRequested;
    std::function<void(bool)> onClockWhileStoppedRequested;
    std::function<void(int)> onClickRateRequested;
    std::function<void(bool)> onClickPulseRequested;
    std::function<void(bool)> onTriggerModeRequested;
    std::function<void(int)> onPopup3Selected;
    std::function<void(bool)> onPatternEditToggled; // idx2 edit/autofill toggle

    // canonical circle structure and member containers used throughout
    struct Circle { float x, y, r; juce::Colour colour; };
    // working and canonical circle lists
    juce::Array<Circle> circles;
    juce::Array<Circle> baseCircles;
    int headerHeight = 0;
    juce::Array<float> flashes;
    // Scheduled beat pulses (absolute ms times). When a beat occurs we schedule
    // pulses for each circle with increasing delay so shrinking circles follow.
    juce::Array<double> scheduledPulseAtMs;
    double basePulseDelayMs = 11.0; // delay between successive circles (ms) — big->small stagger
    juce::StringArray hoverTexts;
    std::unique_ptr<Ring16Component> ring;
    juce::OwnedArray<OnOffButton> onOffButtons;
    // mapping of OwnedArray index -> canonical circle index
    juce::Array<int> onOffButtonIdxs;
    int hoverIndex = -1;
    int ringHoverSegment = -1;
    int ringSelectedSegment = -1;
    int forcedHoverIndex = -1; // when >=0 overrides label display (used by setup submenu buttons)
    bool externalHoverBlocked = false; // editor can block all playground hovers while submenu active
    bool linearShuffleMode = false;
    bool isDraggingLinear = false;
    bool isDraggingShuffle = false;
    float linearShufflePos = 0.0f;
    float linearShuffleAmount = 0.5f;
    static constexpr int offsetRotarySteps = 5; // five discrete positions (matches original playground)
    bool isDraggingOffsetRotary = false;
    juce::Point<float> offsetDragStart { 0.0f, 0.0f };
    float offsetRotaryStartT = 0.0f;
    float offsetRotaryT = 0.0f;
    bool clickToPulseOn = false;
    bool clickToPulseHover = false;
    bool expanded6 = false;
    int hoverExtra6 = -1;
    int extra6Count = 4;
    static constexpr float extra6R = 12.0f;
    juce::Array<float> extra6Progress;
    juce::Array<float> extra6HoverScale;
    juce::Array<float> extra6HoverTarget;
    PopupMenuRing popup6;
    PopupMenuRing popup3;
    juce::String mainCircle3Label { "OFF" };
    juce::StringArray extra6Labels;
    juce::Array<int> extra6Values;
    int mainCircle6Value = 16;
    float extra6StartAngleDeg = 100.0f;
    float extra6EndAngleDeg = -120.0f;
    double extra6AnimStartMs = 0.0;
    float extra6AnimDurationMs = 166.0f;
    float extra6StaggerMs = 25.0f;
    bool extra6Animating = false;
    bool extra6ExpandingTarget = false;
    double lastTickMs = 0.0;
    bool isTimerRunning = false;
    // Host-driven visual step for idx1 display (1..16). When set, overrides
    // the ring's internal lastStepIndex so the number matches PluginEditor.
    int externalStepForDisplay = -1;
    // Cached RUN state so we can toggle on external idx 0 clicks
    bool runState = false;

    // Methods for the editor to push parameter state into the playground
    void setRunState (bool on)
    {
        // update internal run indicator and ring play state
        runState = on;
        if (ring) ring->setPlaying(on);
        if (on)
        {
            // Transport/run start: flash full ring (boost if offset selected)
            float boost = (ringSelectedSegment >= 0 ? 1.25f : 1.0f);
            flashFullRing(boost);
        }
        repaint();
    }

    // Flash the current playhead segment on the ring (used when idx1 trigger fires)
    void flashCurrentStepSegment()
    {
        if (ring)
            ring->flashCurrentSegment();
    }

    // Flash specific logical step (1..16) without taking over the ring playhead.
    void flashSegmentLogical(int step1to16)
    {
        if (ring) ring->flashSegmentLogical(step1to16);
    }

    // Flash entire ring cyan (used for restart or transport start acknowledgment)
    void flashFullRing(float intensity = 1.0f)
    {
        if (ring) ring->triggerFullRingFlash(intensity);
    }

    void setResyncStepSelected (int step1to16)
    {
        if (ring) ring->setGlobalRestartLogical (juce::jlimit(1,16,step1to16) - 1);
        repaint();
    }

    void setClockRateIndexValue (int val)
    {
        mainCircle6Value = val;
        // Map division value to speed multiplier
        double mult = 1.0;
        if (val == 32) mult = 2.0; else if (val == 16) mult = 1.0; else if (val == 8) mult = 0.5; else if (val == 4) mult = 0.25;
        // Defer applying multiplier until a scheduled restart is actually applied.
        // First initialisation (startup) commits immediately so baseline matches current division.
        if (! speedCommitInitialised)
        {
            committedSpeedMultiplier = mult;
            pendingSpeedMultiplier = -1.0; // nothing pending
            speedCommitInitialised = true;
            if (ring) ring->setSpeedMultiplier(committedSpeedMultiplier);
        }
        else
        {
            // Store as pending; applied when editor detects restartApplied edge (NEXT cleared)
            pendingSpeedMultiplier = mult;
        }
        repaint();
    }
    void commitPendingSpeedMultiplier()
    {
        if (pendingSpeedMultiplier > 0.0 && std::abs(pendingSpeedMultiplier - committedSpeedMultiplier) > 1e-6)
        {
            committedSpeedMultiplier = pendingSpeedMultiplier;
            pendingSpeedMultiplier = -1.0;
            if (ring) ring->setSpeedMultiplier(committedSpeedMultiplier);
        }
    }

    double getCommittedSpeedMultiplier() const noexcept { return committedSpeedMultiplier; }
    double getPendingSpeedMultiplier() const noexcept { return pendingSpeedMultiplier; }
    // Deferred speed multiplier commit support
    bool speedCommitInitialised { false };
    double committedSpeedMultiplier { 1.0 }; // actively applied to ring
    double pendingSpeedMultiplier { -1.0 };   // waiting for restartApplied edge

    void setHostTempo(double bpm) { if (ring) ring->setBpm(bpm); }

    void setClickPulseState (bool on)
    {
        clickToPulseOn = on;
        repaint();
    }

    // Public wrapper so the editor can request the playground to schedule
    // per-circle beat pulses. Currently the playground disables processing
    // of scheduled pulses when editor-driven backdrop-only animation is
    // desired; this wrapper is provided for completeness.
    void scheduleBeatPulse()
    {
        startBeatPulse();
    }

    // Host-driven playhead step (1..16). When this is set, the ring stops its
    // internal timer-based progression and uses the provided step.
    void setExternalPlayheadStep (int step1to16)
    {
        externalStepForDisplay = juce::jlimit (1, 16, step1to16);
        if (ring) ring->setExternalPlayheadStepLogical (step1to16);
    }

    // Mirror APVTS -> UI: set the state of the idx8 trigger-mode button.
    void setTriggerModeState (bool on)
    {
        for (int i = 0; i < onOffButtons.size(); ++i)
        {
            if (i < onOffButtonIdxs.size() && onOffButtonIdxs.getReference(i) == 8)
            {
                if (onOffButtons[i]) onOffButtons[i]->setState(on);
                break;
            }
        }
        repaint();
    }

    // Allow external callers (the plugin editor) to toggle the playground's
    // internal popups when the editor places native components over the
    // playground canvas. These wrappers call the private expand/collapse
    // helpers defined below.
    void togglePopup3()
    {
        if (popup3.isVisible() && ! popup3.isAnimating()) { startCollapse3(); return; }
        if (! popup3.isVisible() && ! popup3.isAnimating()) { startExpand3(); return; }
    }

    void togglePopup6()
    {
        if (popup6.isVisible() && ! popup6.isAnimating()) { startCollapse6(); return; }
        if (! popup6.isVisible() && ! popup6.isAnimating()) { startExpand6(); return; }
    }

    // Editor-facing helpers: allow the plugin editor to inform the playground
    // about hover and click events that land on the canonical circle areas
    // when native components sit on top of the playground canvas.
    void setHoverIndexFromEditor (int idx)
    {
        if (externalHoverBlocked) return; // ignore while blocked
        if (idx < 0 || idx >= circles.size()) { if (hoverIndex != -1) { hoverIndex = -1; repaint(); } return; }
        if (hoverIndex != idx)
        {
            hoverIndex = idx;
            // clear ring hover when a circle is hovered
            ringHoverSegment = -1;
            repaint();
        }
    }

    void clearHoverFromEditor()
    {
        if (forcedHoverIndex >= 0) { forcedHoverIndex = -1; }
        if (hoverIndex != -1 || ringHoverSegment != -1)
        {
            hoverIndex = -1;
            ringHoverSegment = -1;
            repaint();
        }
    }

    // Editor-driven forced hover (setup submenu buttons)
    void setForcedHoverIndex (int idx)
    {
        if (idx < 0) { if (forcedHoverIndex != -1) { forcedHoverIndex = -1; repaint(); } return; }
        if (forcedHoverIndex != idx)
        {
            forcedHoverIndex = idx;
            hoverIndex = -1; // suppress normal index
            ringHoverSegment = -1;
            repaint();
        }
    }

    void clearForcedHoverIndex()
    {
        if (forcedHoverIndex != -1)
        {
            forcedHoverIndex = -1;
            repaint();
        }
    }

    void setExternalHoverBlocked (bool b)
    {
        if (externalHoverBlocked != b)
        {
            externalHoverBlocked = b;
            if (b)
            {
                hoverIndex = -1; ringHoverSegment = -1; forcedHoverIndex = -1; repaint();
            }
        }
    }

    // Handle a click that an external container (the editor) wants the
    // playground to process for the canonical circle at `idx`. Returns true
    // if the playground consumed the click and performed an action.
    bool handleExternalClickIndex (int idx)
    {
        if (idx < 0 || idx >= circles.size()) return false;
        // mirror minimal behaviour from the internal mouseDown handler
        if (idx == 0)
        {
            // Ring center acts as button idx 0: toggle RUN state
            const bool next = ! runState;
            runState = next;
            if (ring) ring->setPlaying(next);
            if (onRunToggleRequested) onRunToggleRequested(next);
            repaint();
            return true;
        }
        if (idx == 1)
        {
            // Mirror idx1 behaviour: flash, ensure Run ON, and request one-shot trigger
            if (1 >= 0 && 1 < flashes.size()) flashes.set (1, 1.0f);
            if (onRunToggleRequested) onRunToggleRequested (true);
            if (onTriggerOnceRequested) onTriggerOnceRequested();
            if (ring) ring->flashCurrentSegment(); // flash current playhead wedge
            startFadeTimer();
            repaint();
            return true;
        }
        if (idx == 3)
        {
            if (popup3.isVisible() && ! popup3.isAnimating()) { startCollapse3(); return true; }
            if (! popup3.isVisible() && ! popup3.isAnimating()) { startExpand3(); return true; }
            return true;
        }
        if (idx == 6)
        {
            if (popup6.isVisible() && ! popup6.isAnimating()) { startCollapse6(); return true; }
            if (! popup6.isVisible() && ! popup6.isAnimating()) { startExpand6(); return true; }
            return true;
        }
        // generic feedback for other circles: flash and repaint
        if (idx >= 0 && idx < flashes.size()) flashes.set (idx, 1.0f);
        startFadeTimer();
        repaint();
        return true;
    }

    // Explicitly set the toggle state for the pattern edit control (circle idx2)
    // to avoid accidental tri-state behaviour when the editor temporarily disables
    // mouse interception for pattern wedge editing.
    void setPatternEditButtonState(bool enabled)
    {
        // onOffButtonIdxs holds mapping from onOffButtons vector indices -> circle indices
        for (int i = 0; i < onOffButtons.size(); ++i)
        {
            if (! onOffButtons[i]) continue;
            int circleIdx = (i < onOffButtonIdxs.size()) ? onOffButtonIdxs.getReference(i) : i;
            if (circleIdx == 2) // pattern edit control
            {
                onOffButtons[i]->setState(enabled);
                onOffButtons[i]->repaint();
            }
        }
    }

    // If the plugin editor uses a fixed header overlay (e.g. top 30px), call
    // this to apply a Y-offset to the playground's layout so the playground
    // visuals sit under the header without being shifted by the editor.
    void setHeaderHeight (int h)
    {
        // Store header height but DO NOT mutate canonical positions.
        // Positions imported from Main.cpp are absolute from the canvas origin
        // and must never be modified by overlay framing. Keep working circles
        // identical to the canonical base positions so the editor can place
        // overlay controls exactly where the original author intended.
        headerHeight = h;
        circles.clear();
        for (int i = 0; i < baseCircles.size(); ++i)
        {
            Circle b = baseCircles.getReference(i);
            circles.add (b); // no Y offset applied
        }

        // reposition visual children that depend on circle coordinates (use absolute coords)
        if (ring && circles.size() > 0)
        {
            const auto& c0 = circles.getReference (0);
            ring->setBounds ((int)std::round(c0.x - 70.0f), (int)std::round(c0.y - 70.0f), 140, 140);
        }
        for (int i = 0; i < onOffButtons.size(); ++i)
        {
            if (onOffButtons[i])
            {
                const int circleIdx = (i < onOffButtonIdxs.size()) ? onOffButtonIdxs.getReference(i) : i;
                if (circleIdx >= 0 && circleIdx < circles.size())
                {
                    const auto& c = circles.getReference(circleIdx);
                    onOffButtons[i]->setBounds ((int)std::round(c.x - 14.0f), (int)std::round(c.y - 14.0f), 28, 28);
                }
            }
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        // g.fillAll (UiThemeColours::base());
        // g.setColour (UiThemeColours::cyan().withAlpha (0.1f));
        // for (int x = 0; x < getWidth(); x += 20) g.drawVerticalLine (x, 0.0f, (float)getHeight());
        // for (int y = 0; y < getHeight(); y += 20) g.drawHorizontalLine (y, 0.0f, (float)getWidth());

        if (circles.size() > 0)
        {
            const auto& c0 = circles.getReference (0);
            float maskOuter = 70.0f; // default
            float maskInner = 55.0f; // default
            if (ring) { maskOuter = ring->getOuterRadius(); maskInner = ring->getInnerRadius(); }

            // Draw a donut-shaped path (outer ellipse minus inner ellipse) so
            // the inner area remains transparent and the editor's backdrop
            // circles can be seen through the hole. Use even-odd winding so
            // the second ellipse becomes a hole.
            juce::Path donutPath;
            donutPath.addEllipse (c0.x - maskOuter, c0.y - maskOuter, maskOuter*2.0f, maskOuter*2.0f);
            donutPath.addEllipse (c0.x - maskInner, c0.y - maskInner, maskInner*2.0f, maskInner*2.0f);
            donutPath.setUsingNonZeroWinding (false);
            g.setColour (UiThemeColours::accent());
            g.fillPath (donutPath);
        }

        // Draw circles 1..N only — skip index 0 so the donut hole remains transparent.
        // Additionally skip index 4 (shuffle anchor) to avoid stacking with the
        // custom shuffle drawing below (shufflePositions id1 overlaps that base circle).
        for (int i = 1; i < circles.size(); ++i)
        {
            if (i == 4) continue; // prevent duplicate overlapping shuffle circle
            const auto& c = circles.getReference (i);
            const float rr = c.r;
            const float f = juce::jlimit (0.0f, 1.0f, flashes[i]);
            juce::Colour fillCol = UiThemeColours::accent().interpolatedWith (UiThemeColours::cyan(), f).withAlpha (1.0f);
            g.setColour (fillCol);
            float scale = 1.0f;
            if (i == 1) scale = 1.0f + 0.2f * f;
            const float rrScaled = rr * scale;
            g.fillEllipse (c.x - rrScaled, c.y - rrScaled, rrScaled*2.0f, rrScaled*2.0f);
        }

        // Draw Start-offset rotary control on circle index 3 (if present)
        if (circles.size() > 7)
        {
            const auto& c = circles.getReference (7);
            const float insetR = 17.0f;
            // inset base circle
            g.setColour (UiThemeColours::base());
            g.fillEllipse (c.x - insetR, c.y - insetR, insetR*2.0f, insetR*2.0f);

            // rotary needle: compute t from 0..1 using continuous offsetRotaryT
            const float t = offsetRotaryT;
            const float startAng = -juce::MathConstants<float>::pi * 1.2f; // -135deg
            const float endAng   =  juce::MathConstants<float>::pi * 0.2f; // +135deg
            const float ang = startAng + t * (endAng - startAng);

            const float needleR = insetR + 2.0f;
            const float x2 = c.x + std::cos (ang) * needleR;
            const float y2 = c.y + std::sin (ang) * needleR;

            // colour interpolated from accent -> cyan
            juce::Colour col = UiThemeColours::accent().interpolatedWith (UiThemeColours::cyan(), t);
            g.setColour (col);
            g.drawLine (c.x, c.y, x2, y2, 4.0f);
        }

        if (shufflePositions.size() > 0)
        {
            if (linearShuffleMode && shufflePositions.size() > 1)
            {
                const auto posPt = getPointAlongShufflePolyline (linearShufflePos);
                const float Rstart = shufflePositions.getReference (0).r;
                const float Rend = shufflePositions.getReference (shufflePositions.size() - 1).r;
                const float R = Rstart + (Rend - Rstart) * linearShufflePos;
                const float innerR = R * 0.6f;

                g.setColour (UiThemeColours::accent());
                g.fillEllipse (posPt.x - R, posPt.y - R, R*2.0f, R*2.0f);

                g.setColour (UiThemeColours::base());
                g.fillEllipse (posPt.x - innerR, posPt.y - innerR, innerR*2.0f, innerR*2.0f);

                linearShuffleAmount = 0.5f + 0.25f * linearShufflePos;
                const int numeric = 50 + (int) std::round (linearShufflePos * 25.0f);
                g.setColour (UiThemeColours::cyan());
                g.setFont (juce::Font (juce::FontOptions ("Arial", 12.0f, juce::Font::bold)));
                juce::String txt = juce::String (numeric);
                g.drawFittedText (txt, (int)(posPt.x - innerR), (int)(posPt.y - innerR), (int)(innerR*2), (int)(innerR*2), juce::Justification::centred, 1);
            }
            else if (selectedShuffle >= 1 && selectedShuffle <= shufflePositions.size())
            {
                const auto pos = shufflePositions[selectedShuffle - 1];
                const float R = pos.r;
                const float innerR = R * 0.6f;

                g.setColour (UiThemeColours::accent());
                g.fillEllipse (pos.x - R, pos.y - R, R*2.0f, R*2.0f);

                g.setColour (UiThemeColours::base());
                g.fillEllipse (pos.x - innerR, pos.y - innerR, innerR*2.0f, innerR*2.0f);

                static const float sizes[7] = { 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f, 17.0f };
                const float fontSize = (pos.id >= 1 && pos.id <= 7) ? sizes[pos.id - 1] : 12.0f;
                g.setColour (UiThemeColours::cyan());
                g.setFont (juce::Font (juce::FontOptions ("Arial", fontSize, juce::Font::bold)));
                float offX = 0.0f, offY = 0.0f;
                const int si = selectedShuffle - 1;
                if (si >= 0 && si < shuffleTextOffsets.size()) { offX = shuffleTextOffsets.getReference(si).dx; offY = shuffleTextOffsets.getReference(si).dy; }
                g.drawFittedText (juce::String (pos.id), (int)(pos.x - innerR + offX), (int)(pos.y - innerR + offY), (int)(innerR*2), (int)(innerR*2), juce::Justification::centred, 1);
            }
        }

        // g.setColour (UiThemeColours::cyan().withAlpha (0.8f));
        // g.setFont (juce::Font (juce::FontOptions ("Arial", 14.0f, juce::Font::bold)));
        // g.drawFittedText ("Layout Playground (300 x 240)", 8, 6, getWidth() - 16, 20, juce::Justification::centred, 1);

        juce::String label;
        if (forcedHoverIndex >= 0 && forcedHoverIndex < hoverTexts.size())
            label = hoverTexts[forcedHoverIndex];
        else if (ringHoverSegment >= 0)
            label = "Offset start & trigger to step " + juce::String (ringHoverSegment + 1);
        else if (hoverIndex >= 0 && hoverIndex < hoverTexts.size())
            label = juce::String (hoverIndex + 1) + ": " + hoverTexts[hoverIndex];

        if (clickToPulseHover)
        {
            if (label.isNotEmpty()) label += " — ";
            label += "Switch Click to Pulse";
        }

        if (label.isNotEmpty())
        {
            auto area = juce::Rectangle<int> (8, getHeight() - 24, getWidth() - 16, 18);
            g.setColour (UiThemeColours::cyan());
            g.setFont (juce::Font (juce::FontOptions ("Arial", 13.0f, juce::Font::bold)));
            g.drawFittedText (label, area, juce::Justification::centred, 1);
        }
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (circles.size() <= 1) return;
        const auto& c1 = circles.getReference (1);
        const float outerR = c1.r;
        const float fOuter = juce::jlimit (0.0f, 1.0f, flashes[1]);
        const float outerScale = 1.0f + 0.2f * fOuter;
        const float outerRScaled = outerR * outerScale;
        juce::Colour fillCol = UiThemeColours::accent().interpolatedWith (UiThemeColours::cyan(), fOuter).withAlpha (1.0f);
        g.setColour (fillCol);
        g.fillEllipse (c1.x - outerRScaled, c1.y - outerRScaled, outerRScaled*2.0f, outerRScaled*2.0f);

        const float insetR = 22.0f;
        const float f1 = fOuter;
        const float insetScale = 1.0f + 0.2f * f1;
        const float insetRScaled = insetR * insetScale;

        g.setColour (UiThemeColours::base());
        g.fillEllipse (c1.x - insetRScaled, c1.y - insetRScaled, insetRScaled*2.0f, insetRScaled*2.0f);

        int stepNum = -1;
        // Prefer the editor/host-driven visual step so this number matches the
        // plugin's trigger circle behaviour (freezes when not running).
        if (externalStepForDisplay >= 1 && externalStepForDisplay <= 16)
            stepNum = externalStepForDisplay;
        else if (ring)
            stepNum = ring->getCurrentStepLogical();
        juce::String stepText = (stepNum > 0) ? juce::String (stepNum) : "-";
        g.setColour (UiThemeColours::cyan());
        g.setFont (juce::Font (juce::FontOptions ("Arial", 30.0f, juce::Font::bold)));
        g.drawFittedText (stepText, (int)(c1.x - insetRScaled), (int)(c1.y - insetRScaled), (int)(insetRScaled*2.0f), (int)(insetRScaled*2.0f), juce::Justification::centred, 1);

        if (circles.size() > 7)
        {
            const auto& c7 = circles.getReference (7);
            const float testYOffset = 0.0f; // testing: draw button moved up
            const float smallR = 8.0f;
            juce::Colour bcol = clickToPulseOn ? UiThemeColours::cyan() : UiThemeColours::accent();
            g.setColour (bcol);
            g.fillEllipse (c7.x - smallR, (c7.y + testYOffset) - smallR, smallR*2.0f, smallR*2.0f);
            if (clickToPulseHover)
            {
                g.setColour (UiThemeColours::cyan().withAlpha (0.25f));
                g.drawEllipse (c7.x - smallR - 2.0f, (c7.y + testYOffset) - smallR - 2.0f, (smallR + 2.0f)*2.0f, (smallR + 2.0f)*2.0f, 2.0f);
            }
        }

        if (circles.size() > 6)
        {
            const auto& c6 = circles.getReference (6);
            const float insetR6 = 17.0f;
            g.setColour (UiThemeColours::base());
            g.fillEllipse (c6.x - insetR6, c6.y - insetR6, insetR6*2.0f, insetR6*2.0f);
            g.setColour (UiThemeColours::cyan());
            g.setFont (juce::Font (juce::FontOptions ("Arial", 25.0f, juce::Font::bold)));
            juce::String mainTxt = juce::String (mainCircle6Value);
            g.drawFittedText (mainTxt, (int)(c6.x - insetR6), (int)(c6.y - insetR6), (int)(insetR6*2.0f), (int)(insetR6*2.0f), juce::Justification::centred, 1);
        }

        if (circles.size() > 6)
        {
            const auto& base = circles.getReference (6);
            if (popup6.isVisible() || popup6.isAnimating())
                popup6.draw (g, base.x, base.y, base.r);
        }
        if (circles.size() > 3)
        {
            const auto& c3 = circles.getReference (3);
            const float insetR3 = 17.0f;
            g.setColour (UiThemeColours::base());
            g.fillEllipse (c3.x - insetR3, c3.y - insetR3, insetR3*2.0f, insetR3*2.0f);
            g.setColour (UiThemeColours::cyan());
            g.setFont (juce::Font (juce::FontOptions ("Arial", 14.0f, juce::Font::bold)));
            g.drawFittedText (mainCircle3Label, (int)(c3.x - insetR3), (int)(c3.y - insetR3), (int)(insetR3*2.0f), (int)(insetR3*2.0f), juce::Justification::centred, 1);

            if (popup3.isVisible() || popup3.isAnimating())
                popup3.draw (g, c3.x, c3.y, c3.r);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // Handle small inset click-to-pulse switch (circle index 7) first so it sits "in front"
        if (circles.size() > 7)
        {
            const auto& c7 = circles.getReference (7);
            const float smallR = 10.0f; // enlarge hit radius a bit for easier clicking
            const float testYOffset = 0.0f; // testing: move button up visually and for hit-test
            const float dx = e.position.x - c7.x;
            const float dy = (e.position.y - (c7.y + testYOffset));
            const float d2 = dx*dx + dy*dy;
            if (d2 <= smallR * smallR)
            {
                // Toggle local state and notify host/editor
                clickToPulseOn = ! clickToPulseOn;
                if (onClickPulseRequested) onClickPulseRequested (clickToPulseOn);
                // Visual feedback and repaint
                startFadeTimer();
                repaint();
                return;
            }
        }

        // If popup3 (bar interval / pattern menu) is open, suppress all shuffle clicks
        // so selecting entries like RND or 64 is not misinterpreted as a shuffle selection.
        // We intentionally skip the shuffle region logic entirely while visible; the
        // popup3 handleMouseDown below will consume valid menu item clicks. Non-item
        // clicks are ignored (treated as no-op) which avoids accidental shuffle changes.
        const bool popup3Open = (circles.size() > 3) && (popup3.isVisible() || popup3.isAnimating());
        if (popup3Open)
        {
            // Attempt to process a menu selection immediately; if none chosen, simply return.
            const auto& base3ForPopup = circles.getReference (3);
            const int clickedPopup3 = popup3.handleMouseDown (e.position, base3ForPopup.x, base3ForPopup.y, base3ForPopup.r);
            if (clickedPopup3 >= 0)
            {
                const auto labs = popup3.getLabels();
                if (clickedPopup3 < labs.size()) mainCircle3Label = labs.getReference (clickedPopup3);
                popup3.startCollapse();
                if (3 >= 0 && 3 < flashes.size()) flashes.set (3, 1.0f);
                startFadeTimer();
                if (onPopup3Selected) onPopup3Selected (clickedPopup3);
                repaint();
            }
            return; // Always return while popup3 open to block shuffle interaction beneath.
        }

        // Shuffle region: enable dragging in both linear and discrete modes
        if (shufflePositions.size() > 0)
        {
            // If in linear mode, allow starting a drag by clicking the current handle position
            if (linearShuffleMode && shufflePositions.size() > 1)
            {
                const auto curPt = getPointAlongShufflePolyline (linearShufflePos);
                const float Rstart = shufflePositions.getReference(0).r;
                const float Rend   = shufflePositions.getReference(shufflePositions.size() - 1).r;
                const float Rcur   = Rstart + (Rend - Rstart) * linearShufflePos;
                const float handleHitR = Rcur * 1.4f;
                const float dxh = e.position.x - curPt.x;
                const float dyh = e.position.y - curPt.y;
                const float d2h = dxh*dxh + dyh*dyh;
                if (d2h <= handleHitR * handleHitR)
                {
                    isDraggingShuffle = true;
                    isDraggingLinear  = true;
                    linearShufflePos = pointToShufflePolylineT (e.position);
                    linearShuffleAmount = 0.5f + 0.25f * linearShufflePos;
                    // live notify while dragging happens in mouseDrag; send initial too
                    if (onShuffleStepRequested)
                    {
                        const int n = shufflePositions.size();
                        const int nearest = 1 + (int) std::round (linearShufflePos * (n - 1));
                        onShuffleStepRequested (juce::jlimit (1, n, nearest));
                    }
                    repaint();
                    return;
                }
            }

            // Otherwise, check the discrete shuffle dots and start drag/select
            for (int i = 0; i < shufflePositions.size(); ++i)
            {
                const auto& p = shufflePositions.getReference (i);
                const float dx = e.position.x - p.x;
                const float dy = e.position.y - p.y;
                const float d2 = dx*dx + dy*dy;
                const float hitR = p.r * 1.4f;
                if (d2 <= hitR * hitR)
                {
                    if (linearShuffleMode && shufflePositions.size() > 1)
                    {
                        // In linear mode, allow starting continuous drag when near id1 or id2
                        if (i == 0 || i == 1)
                        {
                            isDraggingShuffle = true;
                            isDraggingLinear  = true;
                            linearShufflePos = pointToShufflePolylineT (e.position);
                            linearShuffleAmount = 0.5f + 0.25f * linearShufflePos;
                            if (onShuffleStepRequested)
                            {
                                const int n = shufflePositions.size();
                                const int nearest = 1 + (int) std::round (linearShufflePos * (n - 1));
                                onShuffleStepRequested (juce::jlimit (1, n, nearest));
                            }
                            repaint();
                            return;
                        }
                        // else: fall through to discrete selection below
                    }
                    selectedShuffle = p.id;
                    isDraggingShuffle = true;
                    if (onShuffleStepRequested) onShuffleStepRequested (juce::jlimit (1, shufflePositions.size(), selectedShuffle));
                    repaint();
                    return;
                }
            }
        }

        if (circles.size() > 3)
        {
            const auto& base3 = circles.getReference (3);
            const int clicked3 = popup3.handleMouseDown (e.position, base3.x, base3.y, base3.r);
            if (clicked3 >= 0)
            {
                const auto labs = popup3.getLabels();
                if (clicked3 < labs.size()) mainCircle3Label = labs.getReference (clicked3);
                popup3.startCollapse();
                if (3 >= 0 && 3 < flashes.size()) flashes.set (3, 1.0f);
                startFadeTimer();
                // notify editor/playground consumer of popup3 selection
                if (onPopup3Selected) onPopup3Selected(clicked3);
                repaint();
                return;
            }
        }

        if (circles.size() > 6)
        {
            const auto& base = circles.getReference (6);
            const int clicked = popup6.handleMouseDown (e.position, base.x, base.y, base.r);
            if (clicked >= 0)
            {
                const auto vals = popup6.getValues();
                if (clicked < vals.size()) mainCircle6Value = vals.getReference (clicked);
                popup6.startCollapse();
                if (6 >= 0 && 6 < flashes.size()) flashes.set (6, 1.0f);
                startFadeTimer();
                // notify editor: pass chosen division value (e.g. 4,8,16,32)
                if (onClockRateIndexRequested) onClockRateIndexRequested(vals.getReference(clicked));
                repaint();
                return;
            }
        }

        // simplified: handle ring, shuffle, and small buttons as in Main.cpp (not all code replicated)
        const int idx = hitTestIndex (e.position);
        if (idx < 0) return;
        if (idx == 1)
        {
            // Emulate PluginEditor triggerRect: flash, ensure Run ON, request one-shot trigger
            if (1 >= 0 && 1 < flashes.size()) flashes.set (1, 1.0f);
            if (onRunToggleRequested) onRunToggleRequested (true); // idempotent set to ON
            if (onTriggerOnceRequested) onTriggerOnceRequested();
            if (ring) ring->flashCurrentSegment();
            startFadeTimer();
            repaint();
            return;
        }
        else if (idx == 3)
        {
            if (popup3.isVisible() && ! popup3.isAnimating()) { startCollapse3(); return; }
            if (! popup3.isVisible() && ! popup3.isAnimating()) { startExpand3(); return; }
            return;
        }
        else if (idx == 6)
        {
            if (popup6.isVisible() && ! popup6.isAnimating())
            {
                popup6.startCollapse();
                startFadeTimer();
                return;
            }
            if (! popup6.isVisible() && ! popup6.isAnimating())
            {
                popup6.startExpand();
                startFadeTimer();
                return;
            }
            return;
        }
        else if (idx == 7)
        {
            isDraggingOffsetRotary = true;
            offsetDragStart = e.position;
            offsetRotaryStartT = offsetRotaryT;
            offsetRotaryT = angleToRotaryT (e.position);
            // notify listener of immediate click-rate selection (quantized)
            if (onClickRateRequested)
            {
                const int nearest = (int) std::round (offsetRotaryT * (offsetRotarySteps - 1));
                onClickRateRequested (juce::jlimit (0, offsetRotarySteps - 1, nearest));
            }
            repaint();
            return;
        }
        else
        {
            if (idx >= 0 && idx < flashes.size()) flashes.set (idx, 1.0f);
            startFadeTimer();
            repaint();
            return;
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (externalHoverBlocked) { return; }
        // Hover for the small inset click-to-pulse switch (circle index 7)
        bool nowHover = false;
        if (circles.size() > 7)
        {
            const auto& c7 = circles.getReference (7);
            const float smallR = 10.0f;
            const float testYOffset = 0.0f; // testing: move button up
            const float dx = e.position.x - c7.x;
            const float dy = e.position.y - (c7.y + testYOffset);
            const float d2 = dx*dx + dy*dy;
            nowHover = (d2 <= smallR * smallR);
        }
        if (nowHover != clickToPulseHover)
        {
            clickToPulseHover = nowHover;
            setMouseCursor (clickToPulseHover ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            repaint();
            if (clickToPulseHover) return; // keep hover local when over the switch
        }

        // Popup hover for expanded menus (idx 6 and idx 3)
        bool did = false;
        if (circles.size() > 6)
        {
            const auto& base6 = circles.getReference (6);
            const int hi6 = popup6.handleMouseMove (e.position, base6.x, base6.y, base6.r);
            if (hi6 >= 0 || popup6.isAnimating()) { repaint(); did = true; }
        }
        if (circles.size() > 3)
        {
            const auto& base3 = circles.getReference (3);
            const int hi3 = popup3.handleMouseMove (e.position, base3.x, base3.y, base3.r);
            if (hi3 >= 0 || popup3.isAnimating()) { repaint(); did = true; }
        }
        if (did) return;

        // Generic hover detection for circles without dedicated components (1 trigger, 3 popup3, 4 shuffle anchor,
        // 6 clock rate/division, 7 rotary). Skip if ring segment hover active or a popup animating.
        if (ringHoverSegment == -1 && ! popup6.isAnimating() && ! popup3.isAnimating())
        {
            auto containsCircle = [&](int idx)->bool
            {
                if (idx < 0 || idx >= circles.size()) return false;
                const auto& c = circles.getReference(idx);
                const float dx = e.position.x - c.x;
                const float dy = e.position.y - c.y;
                return (dx*dx + dy*dy) <= (c.r * c.r);
            };
            int newHover = -1;
            if (containsCircle(1)) newHover = 1; // trigger
            else if (containsCircle(3)) newHover = 3; // popup3 base
            else if (containsCircle(4)) newHover = 4; // shuffle amount anchor
            else if (containsCircle(6)) newHover = 6; // clock rate
            else if (containsCircle(7)) newHover = 7; // rotary
            if (newHover != hoverIndex)
            {
                hoverIndex = newHover;
                repaint();
            }
        }

        // Default behaviour: delegate to base for any other state updates
        juce::Component::mouseMove (e);
    }
    void timerCallback() override
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        const double dtMs = now - lastTickMs; lastTickMs = now;
        const double fadeMs = 300.0;
        bool any = false;
        // Playground scheduled-beat pulses are intentionally disabled when
        // the editor drives backdrop-only animation. scheduledPulseAtMs is
        // retained for backward-compatibility but is not processed here so
        // UI circles won't pulse on editor beats.
        for (int i = 0; i < flashes.size(); ++i)
        {
            float v = flashes[i];
            if (v > 0.0f)
            {
                v *= 0.9f; if (v < 0.001f) v = 0.0f; flashes.set (i, v); if (v > 0.0f) any = true;
            }
        }

        if (popup3.isAnimating() || popup3.isVisible()) { popup3.update (now, dtMs); any = true; }
        if (popup6.isAnimating() || popup6.isVisible()) { popup6.update (now, dtMs); any = true; }

        if (! any) { stopTimer(); isTimerRunning = false; }
        repaint();
    }

    void resized() override
    {
        if (ring) ring->toBack();
    }

    

    // Simple button maker for three on-off buttons
    void makeButton (int idx)
    {
        if (idx < 0) return;
        const auto& c = circles.getReference (idx);
        auto* b = onOffButtons.add (new OnOffButton());
        b->setBounds ((int)(c.x - 14.0f), (int)(c.y - 14.0f), 28, 28);
        onOffButtonIdxs.add(idx);
        b->onHoverChanged = [this, idx](bool over){ hoverIndex = over ? idx : (hoverIndex == idx ? -1 : hoverIndex); repaint(); };
        if (idx == 8)
            b->onToggled = [this](bool on)
            {
                // Notify editor to update trigger mode param.
                if (onTriggerModeRequested) onTriggerModeRequested(on);
            };
        else if (idx == 5)
            b->onToggled = [this](bool on){ linearShuffleMode = on; if (! linearShuffleMode && shufflePositions.size() > 1) linearShufflePos = (selectedShuffle - 1) / (float)(shufflePositions.size() - 1); repaint(); };
        else if (idx == 2)
            b->onToggled = [this](bool on){ if (onPatternEditToggled) onPatternEditToggled(on); };
        addAndMakeVisible (b);
    }

    bool getOnOffStateForCircle(int circleIdx) const
    {
        for (int i = 0; i < onOffButtons.size(); ++i)
        {
            if (i < onOffButtonIdxs.size() && onOffButtonIdxs[i] == circleIdx)
            {
                if (auto* b = onOffButtons[i]) return b->getState();
            }
        }
        return false;
    }

    // Minimal shuffle data used by painting code
    struct ShufflePos { int id; float x, y, r; };
    juce::Array<ShufflePos> shufflePositions;
    int selectedShuffle = 1;
    struct ShuffleTextOffset { float dx, dy; };
    juce::Array<ShuffleTextOffset> shuffleTextOffsets;

    // --- Helpers used in painting and interaction ---
    // Return a point along the shuffle polyline (points defined by shufflePositions)
    // t in [0..1] maps from first to last point.
    juce::Point<float> getPointAlongShufflePolyline (float t) const
    {
        if (shufflePositions.size() == 0) return { 0.0f, 0.0f };
        const int n = shufflePositions.size();
        juce::Array<juce::Point<float>> pts;
        pts.ensureStorageAllocated (n);
        for (int i = 0; i < n; ++i) pts.add ({ shufflePositions.getReference(i).x, shufflePositions.getReference(i).y });

        // compute segment lengths
        std::vector<float> segLen; segLen.resize (std::max(0, n - 1));
        float total = 0.0f;
        for (int i = 0; i + 1 < n; ++i)
        {
            const auto a = pts.getReference(i); const auto b = pts.getReference(i + 1);
            const float l = std::hypot (b.x - a.x, b.y - a.y);
            segLen[(size_t)i] = l; total += l;
        }
        if (total <= 0.0001f) return pts.getReference(0);

        float target = juce::jlimit (0.0f, 1.0f, t) * total;
        float acc = 0.0f;
        for (int i = 0; i + 1 < n; ++i)
        {
            const float l = segLen[(size_t)i];
            if (target <= acc + l || i + 1 == n - 1)
            {
                const float local = juce::jlimit (0.0f, 1.0f, (l <= 0.0f) ? 0.0f : ((target - acc) / l));
                const auto a = pts.getReference(i); const auto b = pts.getReference(i + 1);
                return a + (b - a) * local;
            }
            acc += l;
        }
        return pts.getReference(n - 1);
    }

    // Project a point onto the shuffle polyline and return t in [0..1]
    float pointToShufflePolylineT (juce::Point<float> p) const
    {
        const int n = shufflePositions.size();
        if (n == 0) return 0.0f;
        juce::Array<juce::Point<float>> pts; pts.ensureStorageAllocated(n);
        for (int i = 0; i < n; ++i) pts.add ({ shufflePositions.getReference(i).x, shufflePositions.getReference(i).y });

        // lengths
        std::vector<float> segLen; segLen.resize (std::max(0, n - 1));
        float total = 0.0f;
        for (int i = 0; i + 1 < n; ++i)
        {
            const auto a = pts.getReference(i); const auto b = pts.getReference(i + 1);
            const float l = std::hypot (b.x - a.x, b.y - a.y);
            segLen[(size_t)i] = l; total += l;
        }
        if (total <= 0.0001f) return 0.0f;

        // find best projection
        float bestD = std::numeric_limits<float>::max();
        float bestT = 0.0f;
        float acc = 0.0f;
        for (int i = 0; i + 1 < n; ++i)
        {
            const auto a = pts.getReference(i); const auto b = pts.getReference(i + 1);
            const auto ab = b - a;
            const float denom = ab.x * ab.x + ab.y * ab.y;
            float u = 0.0f;
            if (denom > 0.00001f)
            {
                u = ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / denom;
                u = juce::jlimit (0.0f, 1.0f, u);
            }
            const auto proj = a + ab * u;
            const float d2 = (proj.x - p.x)*(proj.x - p.x) + (proj.y - p.y)*(proj.y - p.y);
            if (d2 < bestD)
            {
                bestD = d2;
                const float segLenLocal = segLen[(size_t)i];
                const float tAlong = (segLenLocal <= 0.0f) ? 0.0f : (acc + u * segLenLocal) / total;
                bestT = juce::jlimit (0.0f, 1.0f, tAlong);
            }
            acc += segLen[(size_t)i];
        }
        return bestT;
    }

    // Convert a mouse point to a continuous rotary t in [0..1] (used as fallback on click)
    float angleToRotaryT (juce::Point<float> p) const
    {
        if (circles.size() <= 7) return 0.0f;
        const auto& c = circles.getReference (7);
        const float dx = p.x - c.x;
        const float dy = p.y - c.y;
        const float ang = std::atan2 (dy, dx);
        const float startAng = -juce::MathConstants<float>::pi * 0.75f;
        const float endAng   =  juce::MathConstants<float>::pi * 0.75f;
        const float range = endAng - startAng;
        float clamped = ang;
        if (clamped < startAng) clamped = startAng;
        if (clamped > endAng) clamped = endAng;
        const float t = (clamped - startAng) / range;
        return juce::jlimit (0.0f, 1.0f, t);
    }

    int hitTestIndex (juce::Point<float> p) const
    {
        int best = -1; float bestd = std::numeric_limits<float>::max();
        for (int i = 0; i < circles.size(); ++i)
        {
            const auto& c = circles.getReference (i);
            const float dx = p.x - c.x, dy = p.y - c.y;
            const float d2 = dx*dx + dy*dy;
            // Use a smaller hit radius for circle 1 to target the inset number button
            const float hitR = (i == 1 ? 22.0f : c.r);
            if (d2 <= hitR * hitR && d2 < bestd) { best = i; bestd = d2; }
        }
        return best;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (isDraggingShuffle && shufflePositions.size() > 0)
        {
            if (isDraggingLinear)
            {
                // continuous dragging along the curve
                linearShufflePos = pointToShufflePolylineT (e.position);
                linearShuffleAmount = 0.5f + 0.25f * linearShufflePos;
                    // notify editor while dragging continuous shuffle
                    if (onShuffleStepRequested)
                    {
                        const int n = shufflePositions.size();
                        const int nearest = 1 + (int) std::round (linearShufflePos * (n - 1));
                        onShuffleStepRequested (juce::jlimit (1, n, nearest));
                    }
                // update nearest discrete id for compatibility with existing UI
                const int n = shufflePositions.size();
                const int nearest = 1 + (int) std::round (linearShufflePos * (n - 1));
                if (nearest != selectedShuffle) selectedShuffle = juce::jlimit (1, n, nearest);
                repaint();
                return;
            }

            // pick nearest shuffle position to the drag point (original behaviour)
            int bestId = selectedShuffle;
            float bestd = std::numeric_limits<float>::max();
            for (int i = 0; i < shufflePositions.size(); ++i)
            {
                const auto& p = shufflePositions.getReference (i);
                const float dx = e.position.x - p.x; const float dy = e.position.y - p.y;
                const float d2 = dx*dx + dy*dy;
                if (d2 < bestd) { bestd = d2; bestId = p.id; }
            }
            if (bestId != selectedShuffle)
            {
                selectedShuffle = bestId;
                if (onShuffleStepRequested) onShuffleStepRequested (juce::jlimit (1, shufflePositions.size(), selectedShuffle));
                repaint();
            }
            return;
        }
        // if dragging the offset rotary, update its position using vertical/horizontal moves
        if (isDraggingOffsetRotary)
        {
            const float dx = e.position.x - offsetDragStart.x;
            const float dy = e.position.y - offsetDragStart.y;
            // choose dominant axis: vertical by default, horizontal if user drags more in X
            const float absdx = std::fabs (dx), absdy = std::fabs (dy);
            const float sensitivity = 100.0f; // pixels for full travel
            float deltaT = 0.0f;
            if (absdx > absdy)
                deltaT = dx / sensitivity; // horizontal increases t to the right
            else
                deltaT = -dy / sensitivity; // vertical increases t upwards
            float t = juce::jlimit (0.0f, 1.0f, offsetRotaryStartT + deltaT);
            // Quantize immediately to discrete steps during drag
            const int steps = offsetRotarySteps;
            if (steps > 1)
            {
                const int nearest = (int) std::round (t * (steps - 1));
                t = nearest / (float)(steps - 1);
            }
            offsetRotaryT = t;
            // notify editor of intermediate quantized click-rate selection
            if (onClickRateRequested)
            {
                const int nearest = (int) std::round (offsetRotaryT * (offsetRotarySteps - 1));
                onClickRateRequested (juce::jlimit (0, offsetRotarySteps - 1, nearest));
            }
            repaint();
            return;
        }

        // otherwise default behavior: update hover
        mouseMove (e);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (isDraggingShuffle)
        {
            if (isDraggingLinear)
            {
                // finalize continuous drag
                isDraggingLinear = false;
                isDraggingShuffle = false;
                // snap selectedShuffle to nearest id
                const int n = shufflePositions.size();
                const int nearest = 1 + (int) std::round (linearShufflePos * (n - 1));
                selectedShuffle = juce::jlimit (1, n, nearest);
                linearShuffleAmount = 0.5f + 0.25f * linearShufflePos;
                repaint();
                return;
            }
            isDraggingShuffle = false;
            // final repaint
            repaint();
            return;
        }
        // finalize offset rotary drag if active
        if (isDraggingOffsetRotary)
        {
            isDraggingOffsetRotary = false;
            // snap to nearest discrete step on release
            const int n = offsetRotarySteps;
            const int nearest = 0 + (int) std::round (offsetRotaryT * (n - 1));
            offsetRotaryT = (n > 1) ? (nearest / (float)(n - 1)) : 0.0f;
            // notify editor of final selection
            if (onClickRateRequested)
                onClickRateRequested (juce::jlimit (0, n - 1, nearest));
            repaint();
            return;
        }
        // Do not trigger clicks on mouseUp; only mouseDown should perform actions.
    }

    void startFadeTimer() { if (! isTimerRunning) { isTimerRunning = true; lastTickMs = juce::Time::getMillisecondCounterHiRes(); startTimerHz (60); } }
    void startExpand6() { popup6.startExpand(); startFadeTimer(); }
    void startCollapse6() { popup6.startCollapse(); startFadeTimer(); }
    void startExpand3() { popup3.startExpand(); startFadeTimer(); }
    void startCollapse3() { popup3.startCollapse(); startFadeTimer(); }

    // Schedule pulses across circles ordered by radius (largest first), each delayed
    // by basePulseDelayMs relative to the previous one.
    void startBeatPulse()
    {
        if (circles.size() == 0) return;
        // Ensure scheduled array matches
        if (scheduledPulseAtMs.size() != flashes.size())
        {
            scheduledPulseAtMs.clear(); scheduledPulseAtMs.resize(flashes.size());
            for (int i = 0; i < scheduledPulseAtMs.size(); ++i) scheduledPulseAtMs.set(i, 0.0);
        }
        // Schedule pulses for ALL circles on each beat in index order (0..N-1)
        const double now = juce::Time::getMillisecondCounterHiRes();
        const int n = (int) circles.size();
        for (int i = 0; i < n; ++i)
        {
            const double t = now + (double)i * basePulseDelayMs; // i*33ms delay chain
            if (i >= 0 && i < scheduledPulseAtMs.size()) scheduledPulseAtMs.set(i, t);
        }
        startFadeTimer();
    }
};

                        
