// ============================================================================
// Clean deduplicated PlaygroundComponent header
// ============================================================================
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <cmath>
#include <vector>
#include "UiTheme.h"
#include "PopupMenuRing.h"
#include "Pattern.h" // PatternRing for edit mode
#include "UiComponents.h"

// --------------------------------------------------------------
// OnOffButton (small circular toggle)
// --------------------------------------------------------------
class OnOffButton : public juce::Component
{
public:
    OnOffButton()
    {
        setSize(diameter, diameter);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    void paint(juce::Graphics &g) override
    {
        auto b = getLocalBounds().toFloat();
        auto c = b.getCentre();
        g.setColour(UiThemeColours::accent());
        g.fillEllipse(b);
        const float innerR = 8.0f;
        g.setColour(UiThemeColours::base());
        g.fillEllipse(c.x - innerR, c.y - innerR, innerR * 2, innerR * 2);
        if (isOn)
        {
            const float dotR = 5.0f;
            g.setColour(UiThemeColours::cyan());
            g.fillEllipse(c.x - dotR, c.y - dotR, dotR * 2, dotR * 2);
        }
    }
    void mouseDown(const juce::MouseEvent &) override
    {
        isOn = !isOn;
        if (onToggled)
            onToggled(isOn);
        repaint();
    }
    void mouseEnter(const juce::MouseEvent &) override
    {
        if (onHoverChanged)
            onHoverChanged(true);
    }
    void mouseExit(const juce::MouseEvent &) override
    {
        if (onHoverChanged)
            onHoverChanged(false);
    }
    void setState(bool on)
    {
        isOn = on;
        repaint();
    }
    bool getState() const noexcept { return isOn; }
    std::function<void(bool)> onToggled;
    std::function<void(bool)> onHoverChanged;

private:
    static constexpr int diameter = 28;
    bool isOn = false;
};

// --------------------------------------------------------------
// Ring16Component (16 wedge ring; logical numbering identity to raw wedge index for click & hover alignment)
// --------------------------------------------------------------
class Ring16Component : public juce::Component, private juce::Timer
{
public:
    Ring16Component()
    {
        setSize((int)(outerR * 2), (int)(outerR * 2));
        masterStartMs = juce::Time::getMillisecondCounterHiRes();
        ringFlashes.resize(16);
        segmentFade.resize(16);
        segmentHoldUntilMs.resize(16);
        for (int i = 0; i < 16; ++i)
        {
            ringFlashes.set(i, 0.0f);
            segmentFade.set(i, 0.0f);
            segmentHoldUntilMs.set(i, 0.0);
        }
        globalRestartLogical = 0;
        selected = logicalToRaw(globalRestartLogical);
        if (selected >= 0)
            flashSegmentRaw(selected, 0.9f, true, false);
        startTimerHz(60);
    }
    // Identity mapping: logical index (0..15) matches raw wedge index; visual step = logical+1.
    int logicalToRaw(int logical) const { return logical & 15; }
    int rawToLogical(int raw) const { return raw & 15; }
    void flashSegmentLogical(int step1to16)
    {
        int l0 = juce::jlimit(1, 16, step1to16) - 1;
        flashSegmentRaw(logicalToRaw(l0));
    }
    void flashCurrentSegment()
    {
        if (lastStepIndex >= 0)
            flashSegmentRaw(lastStepIndex, 1.0f, true, false);
    }
    void triggerFullRingFlash(float intensity = 1.0f) { fullRingFlash = juce::jlimit(0.0f, 1.5f, intensity); }
    // state setters
    void setSpeedMultiplier(double m) { speedMultiplier = juce::jlimit(0.25, 4.0, m); }
    void setBpm(double b)
    {
        if (b > 1.0)
            bpm = b;
    }
    void setPlaying(bool on)
    {
        if (isPlaying != on)
        {
            isPlaying = on;
            if (onPlayStateChanged)
                onPlayStateChanged(isPlaying);
            if (isPlaying)
                playStartMs = juce::Time::getMillisecondCounterHiRes();
            startTimerHz(60);
        }
    }
    void setExternalPlayheadStepLogical(int step1to16)
    {
        if (step1to16 < 1 || step1to16 > 16)
        {
            useExternalPlayhead = false;
            return;
        }
        externalPlayheadRaw = logicalToRaw(step1to16 - 1);
        useExternalPlayhead = true;
        lastStepIndex = externalPlayheadRaw;
        repaint();
    }
    void clearExternalPlayhead() { useExternalPlayhead = false; }
    void requestRestartAtNextMasterBoundary() { pendingRestartAtNextBoundary = true; }
    void setResyncOnBarOneEnabled(bool en) { resyncOnBarOneEnabled = en; }
    void setGlobalRestartLogical(int logical)
    {
        if (logical >= 0 && logical < 16)
        {
            globalRestartLogical = logical;
            selected = logicalToRaw(logical);
            repaint();
        }
    }
    int getCurrentStepLogical() const { return lastStepIndex >= 0 ? rawToLogical(lastStepIndex) + 1 : -1; }
    float getOuterRadius() const noexcept { return outerR; }
    float getInnerRadius() const noexcept { return innerR; }
    void setPatternEditMode(bool enabled) { patternEditMode = enabled; }
    // callbacks
    std::function<void(int)> onHoverChanged;
    std::function<void(int)> onClicked;
    std::function<void(bool)> onPlayStateChanged;
    std::function<void(int)> onStepChanged;
    std::function<void(bool)> onInnerHover;
    // centre click callback: invoked when the inner ring center is clicked.
    // Playground will wire this to forward a Run toggle request so the APVTS/host
    // path is used for authoritative run toggles.
    std::function<void()> onCenterClicked;

    void paint(juce::Graphics &g) override
    {
        if (patternEditMode)
        {
            // Minimal rendering: only selected offset step highlight and chase light (playhead) without full grid.
            auto b = getLocalBounds().toFloat();
            const float innerProp = innerR / outerR;
            const float step = juce::MathConstants<float>::twoPi / 16.0f;
            const float start0 = -juce::MathConstants<float>::halfPi;
            int playheadRaw = useExternalPlayhead ? externalPlayheadRaw : lastStepIndex;
            static constexpr int kHighlightRotation = 4;
            // Draw outer reference ring faint with adjusted alpha.
            //   g.setColour(UiThemeColours::accent().withAlpha(0.1f));
            //   g.drawEllipse(b.getCentreX()-outerR, b.getCentreY()-outerR, outerR*2, outerR*2, 1.0f);
            // Selected offset wedge (radially reduced by 1px)
            if (selected >= 0)
            {
                int selIdx = (selected + kHighlightRotation) & 15;
                auto b2 = b.reduced(0.5f);                     // reduce outer radius by ~1px
                float innerPropSel = (innerR + 0.5f) / outerR; // inward growth keeps thickness balanced
                juce::Path seg;
                seg.addPieSegment(b2, start0 + selIdx * step, start0 + (selIdx + 1) * step, innerPropSel);
                g.setColour(UiThemeColours::cyan().withAlpha(0.55f));
                g.fillPath(seg);
                g.setColour(UiThemeColours::cyan().withAlpha(1.0f));
                g.strokePath(seg, juce::PathStrokeType(1.0f));
            }
            // Chase/playhead wedge (different) — draw full wedge so chase colour renders at full thickness
            if (playheadRaw >= 0)
            {
                int chaseIdx = (playheadRaw + kHighlightRotation) & 15;
                // Use the full wedge geometry (b) and the normal innerProp so the cyan/fade
                // draw covers the same area as other segments (no reduced inset).
                juce::Path seg;
                seg.addPieSegment(b, start0 + chaseIdx * step, start0 + (chaseIdx + 1) * step, innerProp);
                g.setColour(UiThemeColours::cyan().withAlpha(isPlaying ? 0.75f : 0.45f));
                g.fillPath(seg);
                // Do not draw an outline for the chase/playhead wedge so underlying
                // chaselight (circle idx1) can show through without stroked edges.
            }
            return;
        }
        auto b = getLocalBounds().toFloat();
        const float innerProp = innerR / outerR;
        const float step = juce::MathConstants<float>::twoPi / 16.0f;
        // Default geometry: start at 12 o'clock (step 1 at top).
        const float start0 = -juce::MathConstants<float>::halfPi;
        int playheadRaw = useExternalPlayhead ? externalPlayheadRaw : lastStepIndex;
        static constexpr int kHighlightRotation = 4; // visually advance highlight by +4 wedges
        for (int i = 0; i < 16; ++i)
        {
            juce::Path seg;
            seg.addPieSegment(b, start0 + i * step, start0 + (i + 1) * step, innerProp);
            static constexpr int kHighlightRotation = 4; // visually advance highlight by +4 wedges
            bool isSel = (i == ((selected + kHighlightRotation) & 15));
            bool isPlay = (i == ((playheadRaw + kHighlightRotation) & 15));
            bool isHover = (i == hovered);
            float flash = ringFlashes[i];
            float fade = segmentFade[i];
            if (isSel || isPlay)
            {
                if (isSel)
                {
                    // Selected wedge uses a slightly reduced thickness and an outline.
                    auto b2 = b.reduced(0.5f);
                    float innerPropHi = (innerR + 0.5f) / outerR;
                    juce::Path segHi;
                    segHi.addPieSegment(b2, start0 + i * step, start0 + (i + 1) * step, innerPropHi);
                    g.setColour(UiThemeColours::cyan().withAlpha(0.55f));
                    g.fillPath(segHi);
                    // Keep selected wedge outline cyan-only so it doesn't draw accent over the parent donut.
                    g.setColour(UiThemeColours::cyan().withAlpha(1.0f));
                    g.strokePath(segHi, juce::PathStrokeType(1.0f));
                }
                else if (isPlay)
                {
                    // Playhead/chase should render at full wedge geometry (no reduced inset)
                    juce::Path segPlay;
                    segPlay.addPieSegment(b, start0 + i * step, start0 + (i + 1) * step, innerProp);
                    g.setColour(UiThemeColours::cyan().withAlpha(isPlaying ? 1.0f : 0.45f));
                    g.fillPath(segPlay);
                    // Do not stroke the playhead wedge here so the underlying chaselight can show through.
                }
                continue; // skip normal seg painting when highlighted
            }
            if (flash > 0.01f)
            {
                g.setColour(UiThemeColours::cyan().withAlpha(juce::jlimit(0.0f, 1.0f, flash)));
                g.fillPath(seg);
                float whiteA = flash * 0.35f;
                if (whiteA > 0.02f)
                {
                    g.setColour(juce::Colours::white.withAlpha(juce::jlimit(0.0f, 0.45f, whiteA)));
                    g.fillPath(seg);
                }
            }
            else if (!isPlay && !isSel && fade > 0.01f)
            {
                g.setColour(UiThemeColours::cyan().withAlpha(fade));
                g.fillPath(seg);
            }
            else if (!isPlay && !isSel && isHover)
            {
                g.setColour(UiThemeColours::accent().darker(0.25f).withAlpha(0.25f));
                g.fillPath(seg);
            }
        }
        if (fullRingFlash > 0.01f)
        {
            juce::Path rp;
            rp.addEllipse(b.getCentreX() - outerR, b.getCentreY() - outerR, outerR * 2, outerR * 2);
            rp.addEllipse(b.getCentreX() - innerR, b.getCentreY() - innerR, innerR * 2, innerR * 2);
            rp.setUsingNonZeroWinding(false);
            g.setColour(UiThemeColours::cyan().withAlpha(juce::jlimit(0.0f, 0.85f, fullRingFlash)));
            g.fillPath(rp);
        }
        g.setColour(UiThemeColours::accent().withAlpha(0.15f));
        g.drawEllipse(b.getCentreX() - innerR, b.getCentreY() - innerR, innerR * 2, innerR * 2, 1.0f);
    }
    void mouseMove(const juce::MouseEvent &e) override
    {
        bool nowInner = isInsideInner(e.position);
        if (nowInner != innerHovered)
        {
            innerHovered = nowInner;
            if (onInnerHover)
                onInnerHover(innerHovered);
        }
        int h = hitSegment(e.position);
        if (h != hovered)
        {
            hovered = h;
            setMouseCursor(hovered >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            if (onHoverChanged)
                onHoverChanged(hovered >= 0 ? rawToLogical(hovered) : -1);
            repaint();
        }
    }
    void mouseExit(const juce::MouseEvent &) override
    {
        if (hovered != -1)
        {
            hovered = -1;
            if (onHoverChanged)
                onHoverChanged(-1);
        }
        if (innerHovered)
        {
            innerHovered = false;
            if (onInnerHover)
                onInnerHover(false);
        }
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }
    void mouseDown(const juce::MouseEvent &e) override
    {
        if (patternEditMode)
        {
            // When pattern edit is active we let PlaygroundComponent manage clicks inside inner region.
            // Treat inner area as pass-through so pattern wedges can be toggled.
            if (isInsideInner(e.position))
                return;
            // Allow wedge selection only if not inside inner hole (normal behaviour) so offset selection still available.
        }
        if (isInsideInner(e.position))
        {
            // Prefer the playground/editor path for center clicks so the APVTS run
            // parameter is changed via the editor and the processor/host sees an
            // authoritative toggle. If no callback wired, fall back to local toggle.
            if (onCenterClicked)
            {
                onCenterClicked();
                return;
            }
            togglePlay();
            return;
        }
        int s = hitSegment(e.position);

        if (s >= 0)
        {
            selected = s; // raw wedge for highlight
            int logical = rawToLogical(selected);

            if (onClicked)
                onClicked(logical); // pass identity logical
            repaint();
            draggingWedges = true; // allow drag in both normal and pattern edit modes
        }
    }
    void mouseDrag(const juce::MouseEvent &e) override
    {
        if (!draggingWedges)
            return; // drag only while active
        // Lock drag: derive wedge index from angle even if cursor leaves ring annulus.
        auto b = getLocalBounds().toFloat();
        auto c = b.getCentre();
        float dx = e.position.x - c.x;
        float dy = e.position.y - c.y;
        float angle = std::atan2(dy, dx);
        const float startAt12 = -juce::MathConstants<float>::halfPi;
        float rel = angle - startAt12;
        while (rel < 0.0f)
            rel += juce::MathConstants<float>::twoPi;
        float slice = juce::MathConstants<float>::twoPi / 16.0f;
        int raw = juce::jlimit(0, 15, (int)std::floor(rel / slice));
        if (raw != selected)
        {
            selected = raw;
            int logical = rawToLogical(selected);
            if (onClicked)
                onClicked(logical);
            repaint();
        }
    }
    void mouseUp(const juce::MouseEvent &) override
    {
        draggingWedges = false;
    }
    bool hitTest(int x, int y) override
    {
        // Restrict interception to within the ring's outer radius so clicks on shuffle points
        // (which lie outside outerR but inside the ring's bounding box) are not swallowed.
        auto b = getLocalBounds().toFloat();
        auto centre = b.getCentre();
        float dx = (float)x - centre.x;
        float dy = (float)y - centre.y;
        float d2 = dx * dx + dy * dy;
        float outerR2 = outerR * outerR;
        if (d2 > outerR2)
            return false; // outside ring entirely -> let parent handle (needed for shuffle endpoints)

        if (!patternEditMode)
            return true; // normal mode: inside outer radius we intercept (inner + annulus)

        // Pattern edit mode: allow clicks inside pattern ring band & inner hole to pass through for pattern wedge routing.
        float patternOuter = (innerR - PatternGeometry::kPatternInset) + PatternGeometry::kPatternOutwardShift;
        float patternInner = patternOuter * PatternGeometry::kPatternThicknessRatio;
        if (d2 <= patternOuter * patternOuter)
            return false; // pass through to parent for pattern wedge editing / inner hole actions
        return true;      // outside pattern ring (but still within outerR) we intercept as usual
    }

private:
    bool isInsideInner(juce::Point<float> p) const
    {
        auto c = getLocalBounds().toFloat().getCentre();
        float dx = p.x - c.x, dy = p.y - c.y;
        return dx * dx + dy * dy <= innerR * innerR;
    }
    int hitSegment(juce::Point<float> p) const
    {
        auto b = getLocalBounds().toFloat();
        auto c = b.getCentre();
        float dx = p.x - c.x, dy = p.y - c.y;
        float d2 = dx * dx + dy * dy;
        if (d2 > outerR * outerR || d2 < innerR * innerR)
            return -1;
        float angle = std::atan2(dy, dx);
        // Hit testing start at 12 o'clock for identity mapping.
        const float startAt12 = -juce::MathConstants<float>::halfPi;
        float rel = angle - startAt12;
        while (rel < 0.0f)
            rel += juce::MathConstants<float>::twoPi;
        float slice = juce::MathConstants<float>::twoPi / 16.0f;
        int raw = (int)std::floor(rel / slice);
        return juce::jlimit(0, 15, raw);
    }
    void togglePlay() { setPlaying(!isPlaying); }
    void flashSegmentRaw(int rawIndex, float flashIntensity = 1.0f, bool seedFade = true, bool applyHold = true)
    {
        if (rawIndex < 0 || rawIndex >= 16)
            return;
        ringFlashes.set(rawIndex, juce::jlimit(0.0f, 1.5f, flashIntensity));
        if (seedFade)
            segmentFade.set(rawIndex, 1.0f);
        if (applyHold)
        {
            double now = juce::Time::getMillisecondCounterHiRes();
            segmentHoldUntilMs.set(rawIndex, now + kFlashHoldMs);
        }
        repaint();
    }
    void timerCallback() override
    {
        double now = juce::Time::getMillisecondCounterHiRes();
        double dt = now - lastTickMs;
        lastTickMs = now;
        bool any = false;
        if (isPlaying && !useExternalPlayhead)
        {
            tickAccumMs += dt * speedMultiplier;
            double msPerStep = (60000.0 / bpm) / 4.0;
            while (tickAccumMs >= msPerStep)
            {
                tickAccumMs -= msPerStep;
                lastStepIndex = (lastStepIndex + 1) & 15;
                flashSegmentRaw(lastStepIndex, 1.0f, true, false);
                if (onStepChanged)
                    onStepChanged(rawToLogical(lastStepIndex));
                if (pendingRestartAtNextBoundary && lastStepIndex == logicalToRaw(globalRestartLogical))
                {
                    pendingRestartAtNextBoundary = false;
                    triggerFullRingFlash(1.15f);
                }
            }
        }
        for (int i = 0; i < 16; ++i)
        {
            double holdUntil = segmentHoldUntilMs[i];
            if (holdUntil > now)
            {
                any = true;
                continue;
            }
            float f = ringFlashes[i];
            if (f > 0.0f)
            {
                f *= kFlashDecayFactor;
                if (f < 0.01f)
                    f = 0.0f;
                ringFlashes.set(i, f);
            }
            float trail = segmentFade[i];
            if (trail > 0.0f)
            {
                trail -= (float)(dt / kFadeDecayMs);
                if (trail < 0.0f)
                    trail = 0.0f;
                segmentFade.set(i, trail);
            }
            if (ringFlashes[i] > 0.0f || segmentFade[i] > 0.0f)
                any = true;
        }
        if (fullRingFlash > 0.0f)
        {
            fullRingFlash *= 0.92f;
            if (fullRingFlash < 0.01f)
                fullRingFlash = 0.0f;
            else
                any = true;
        }
        if (!any && !isPlaying)
            stopTimer();
    }
    // members
    bool isPlaying = false;
    double bpm = 120.0;
    double playStartMs = 0.0;
    double masterStartMs = 0.0;
    double lastTickMs = 0.0;
    double tickAccumMs = 0.0;
    int lastStepIndex = -1;
    int selected = -1;
    int hovered = -1;
    bool innerHovered = false;
    int globalRestartLogical = 0;
    bool pendingRestartAtNextBoundary = false;
    bool resyncOnBarOneEnabled = false;
    double speedMultiplier = 1.0;
    bool useExternalPlayhead = false;
    int externalPlayheadRaw = -1;
    float fullRingFlash = 0.0f;
    juce::Array<float> ringFlashes;
    juce::Array<float> segmentFade;
    juce::Array<double> segmentHoldUntilMs;
    static constexpr double kFlashHoldMs = 220.0;
    static constexpr float kFadeDecayMs = 33.0f;
    static constexpr float kFlashDecayFactor = 0.94f;
    const float outerR = 70.0f;
    const float innerR = 55.0f;
    bool patternEditMode = false;
    bool draggingWedges = false;
};

// --------------------------------------------------------------
// PlaygroundComponent (clean)
// --------------------------------------------------------------
class PlaygroundComponent : public juce::Component, private juce::Timer
{
public:
    PlaygroundComponent();
    ~PlaygroundComponent() override;

    // Accessor API for unified HitRouting (replaces direct private member access)
    inline bool isPopup3Active() const noexcept { return popup3.isVisible() || popup3.isAnimating(); }
    inline bool isPopup6Active() const noexcept { return popup6.isVisible() || popup6.isAnimating(); }
    inline int popup3Hit(const juce::Point<float> &p, float cx, float cy, float baseR) { return popup3.handleMouseMove(p, cx, cy, baseR); }
    inline int popup6Hit(const juce::Point<float> &p, float cx, float cy, float baseR) { return popup6.handleMouseMove(p, cx, cy, baseR); }
    inline int getCircleCount() const noexcept { return baseCircles.size(); }
    inline bool getCircleInfo(int idx, float &x, float &y, float &r) const noexcept
    {
        if (idx < 0 || idx >= baseCircles.size())
            return false;
        const auto &c = baseCircles.getReference(idx);
        x = c.x;
        y = c.y;
        r = c.r;
        return true;
    }
    inline int getShuffleCount() const noexcept { return shufflePositions.size(); }
    inline bool getShuffleInfo(int idx, int &id, float &x, float &y, float &r) const noexcept
    {
        if (idx < 0 || idx >= shufflePositions.size())
            return false;
        const auto &sp = shufflePositions.getReference(idx);
        id = sp.id;
        x = sp.x;
        y = sp.y;
        r = sp.r;
        return true;
    }

    // Host callbacks
    std::function<void(int)> onResyncStepRequested;
    std::function<void(bool)> onRunToggleRequested;
    std::function<void()> onTriggerOnceRequested;
    std::function<void(int)> onClockRateIndexRequested;
    std::function<void(int)> onShuffleStepRequested;
    std::function<void(bool)> onClockWhileStoppedRequested;
    std::function<void(int)> onClickRateRequested;
    std::function<void(bool)> onClickPulseRequested;
    std::function<void(bool)> onTriggerModeRequested;
    std::function<void(int)> onPopup3Selected;
    std::function<void(bool)> onPatternEditToggled;
    // Additional callbacks the editor can wire for UI-only toggles
    std::function<void(bool)> onLinearShuffleModeChanged;

    // External sync/setters
    // setRunState(on, notifyParam=true): when notifyParam==true this is a user action
    // (clicking idx0) and may update parameter-driven UI; when false this call is
    // driven by host transport and should not mutate the user run-toggle state.
    void setRunState(bool on, bool notifyParam = true);
    void flashCurrentStepSegment();
    void flashSegmentLogical(int step1to16);
    void flashFullRing(float intensity = 1.0f);
    void setResyncStepSelected(int step1to16);
    void setHostTempo(double bpm);
    void setClickPulseState(bool on);
    void setExternalPlayheadStep(int step1to16);
    void setExternalBarNumber(int barNumber);
    void setTriggerModeState(bool on);
    void togglePopup3();
    void togglePopup6();
    void setHoverIndexFromEditor(int idx);
    void clearHoverFromEditor();
    void setForcedHoverIndex(int idx);
    void clearForcedHoverIndex();
    void setExternalHoverBlocked(bool b);
    bool handleExternalClickIndex(int idx);
    void setPatternEditButtonState(bool enabled);
    void setHeaderHeight(int h);
    void setClockRateIndexValue(int val);
    void commitPendingSpeedMultiplier();
    double getCommittedSpeedMultiplier() const noexcept;
    double getPendingSpeedMultiplier() const noexcept;
    void setHoverTextEnabled(bool en);
    bool getHoverTextEnabled() const noexcept;
    void setClickToPulseHoverFromEditor(bool hover);
    // UI state setters (used by editor to restore visual-only state)
    void setPopup3Index(int popupIndex);
    void setMainCircle6Value(int val);
    void setSelectedShuffle(int v);
    void setLinearShuffleModeState(bool on);

    // Pattern bitmask accessors for external sync (editor)
    inline uint16_t getPatternBitmask() const noexcept { return (uint16_t) pattern.getBitmask(); }
    inline void setPatternBitmask(uint16_t m) { pattern.setBitmask(m); repaint(); }

    // Helper: convert UI step (1..16) to stored bit index (0..15) using visual +4 rotation
    inline int uiStep1to16ToStoredIndex(int step1to16) const noexcept { int l0 = juce::jlimit(1, 16, step1to16) - 1; return (l0 + 4) & 15; }

    // JUCE overrides
    void paint(juce::Graphics &) override;
    void paintOverChildren(juce::Graphics &) override;
    void mouseDown(const juce::MouseEvent &) override;
    void mouseDrag(const juce::MouseEvent &) override;
    void mouseUp(const juce::MouseEvent &) override;
    void mouseMove(const juce::MouseEvent &) override;
    void timerCallback() override;
    void resized() override;
    bool keyPressed(const juce::KeyPress &) override;

private:
    // helpers
    void makeButton(int idx);
    juce::Point<float> getPointAlongShufflePolyline(float t) const;
    float pointToShufflePolylineT(juce::Point<float>) const;
    float angleToRotaryT(juce::Point<float>) const;
    void startFadeTimer();
    void startExpand6();
    void startCollapse6();
    void startExpand3();
    void startCollapse3();
    void startBeatPulse();
    void randomizePatternSteps();

    struct Circle
    {
        float x, y, r;
        juce::Colour colour;
    };
    struct ShufflePos
    {
        int id;
        float x, y, r;
    };
    struct ShuffleTextOffset
    {
        float dx, dy;
    };
    juce::Array<Circle> baseCircles;
    juce::Array<Circle> circles;
    int headerHeight = 0;
    juce::Array<float> flashes;
    juce::Array<double> scheduledPulseAtMs;
    double basePulseDelayMs = 11.0;
    juce::StringArray hoverTexts;
    std::unique_ptr<Ring16Component> ring;
    std::unique_ptr<RunButton> runIndicator;
    juce::OwnedArray<OnOffButton> onOffButtons;
    juce::Array<int> onOffButtonIdxs;
    int hoverIndex = -1;
    int ringHoverSegment = -1;
    int ringSelectedSegment = -1;
    int forcedHoverIndex = -1;
    bool externalHoverBlocked = false;
    bool linearShuffleMode = false;
    bool isDraggingLinear = false;
    bool isDraggingShuffle = false;
    float linearShufflePos = 0.0f;
    float linearShuffleAmount = 0.5f;
    static constexpr int offsetRotarySteps = 5;
    bool isDraggingOffsetRotary = false;
    juce::Point<float> offsetDragStart{0, 0};
    float offsetRotaryStartT = 0.0f;
    float offsetRotaryT = 0.0f;
    bool clickToPulseOn = false;
    bool clickToPulseHover = false;
    bool hoverTextEnabled = true;
    bool expanded6 = false;
    int hoverExtra6 = -1;
    int extra6Count = 4;
    static constexpr float extra6R = 12.0f;
    juce::Array<float> extra6Progress;
    juce::Array<float> extra6HoverScale;
    juce::Array<float> extra6HoverTarget;
    PopupMenuRing popup6;
    PopupMenuRing popup3;
    juce::String mainCircle3Label{"OFF"};
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
    bool isTimerRunning = true;
    int externalStepForDisplay = -1;
    bool runState = false;
    bool speedCommitInitialised = false;
    double committedSpeedMultiplier = 1.0;
    double pendingSpeedMultiplier = -1.0;
    juce::Array<ShufflePos> shufflePositions;
    juce::Array<ShuffleTextOffset> shuffleTextOffsets;
    int selectedShuffle = 1;
    PatternRing pattern;
    int patternHoverIndex = -1;
    bool patternDragActive = false;
    int lastPatternDragWedge = -1;
    bool patternDragPaintState = false;
    int hoverShuffleId = -1; // current shuffle point hovered (for glow accent)
public:
    // Auto pattern trigger scheduling (popup3). Interval in bars (16 steps). 0 = OFF.
    int autoTriggerIntervalBars = 0;   // 1,2,4,8,16,32,64
    bool autoTriggerRandom = false;    // RND mode selected
    int barsSinceAutoTrigger = 0;      // completed bars since last auto trigger
    int lastExternalBarNumber = -1;    // last host-provided bar number (for robust interval tracking)
    juce::Random autoTriggerRng;       // random source for RND mode
    bool patternEditMode = false;      // active when pattern edit button (circle idx2) toggled on
    bool patternBarActive = false;     // true while pattern is firing steps this bar (one-bar fill unless interval=1 for continuous)
    int rndPatternAmount = 50;         // 1..100 randomization density / probability
    bool autoRandomizePattern = false; // AUTO toggle: randomize pattern each time it engages
    bool rndAmountDragging = false;
    int rndAmountStart = 50;
    juce::Point<float> rndDragOrigin{0, 0};
    float rndTriggerFlash = 0.0f; // transient flash for RND button
    // Status LED recovery (header right side)
    enum class StatusState
    {
        Idle,
        Armed,
        Pending
    };
    StatusState statusState = StatusState::Idle;
    float statusPulse = 0.0f; // pulse animation for Pending
    void setStatusState(StatusState s)
    {
        if (statusState != s)
        {
            statusState = s;
            repaint();
        }
    }
};

// ================= Inline implementations =================
// Include unified hit routing AFTER class definition so inline methods are visible.
#include "HitRouting.h"
inline PlaygroundComponent::PlaygroundComponent()
{
    setSize(300, 240);
    // Allow this component to receive keyboard focus (for future shortcuts if needed).
    setWantsKeyboardFocus(true);
    baseCircles.add({150.0f, 130.0f, 70.0f, juce::Colours::orangered});
    baseCircles.add({244.0f, 85.0f, 35.0f, juce::Colours::goldenrod});
    baseCircles.add({233.0f, 132.0f, 14.0f, juce::Colours::turquoise});
    baseCircles.add({235.0f, 170.0f, 25.0f, juce::Colours::mediumpurple});
    baseCircles.add({96.0f, 189.0f, 11.0f, juce::Colours::yellowgreen});
    baseCircles.add({78.0f, 172.0f, 14.0f, juce::Colours::pink});
    baseCircles.add({56.0f, 140.0f, 25.0f, juce::Colours::seagreen});
    baseCircles.add({70.0f, 80.0f, 25.0f, juce::Colours::greenyellow});
    baseCircles.add({201.0f, 64.0f, 14.0f, juce::Colours::blueviolet});
    circles = baseCircles;
    flashes.resize(circles.size());
    scheduledPulseAtMs.resize(circles.size());
    for (int i = 0; i < flashes.size(); ++i)
    {
        flashes.set(i, 0.0f);
        scheduledPulseAtMs.set(i, 0.0);
    }
    hoverTexts = {"Stop/Re-sync next bar + offset", "Re-trigger quantized", "Autofill pattern edit", "Autofill play interval", "Shuffle Amount", "Shuffle type 909 (1-7) or linear", "Set scale 1/32, 1/16, 1/8, 1/4", "Audio Click: off, beat, 8th, 16th, 24ppq", "Re-sync + offset on/off", "Send midi-clocks while idle/stopped", "Legacy send always stop before start", "Send song position pointer (SPP)"};
    ring.reset(new Ring16Component());
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
    { if (patternEditMode) return; ringSelectedSegment=logical; ring->setGlobalRestartLogical(logical); if (onResyncStepRequested) onResyncStepRequested(logical+1); repaint(); };
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
                int rotatedTriggerIdx = (triggerIdx + 4) & 15; // account for visual +4 rotation
                if (patternBarActive && pattern.getStep(rotatedTriggerIdx))
                {
                    if (onTriggerOnceRequested)
                        onTriggerOnceRequested();
                }

            // End of bar: disable fill unless continuous (interval == 1)
            if (patternBarActive && logical == 15)
            {
                if (autoTriggerRandom || autoTriggerIntervalBars != 1)
                    patternBarActive = false;
            }
        }
        repaint(); };
    shufflePositions.add({1, 96.0f, 189.0f, 11.0f});
    shufflePositions.add({2, 109.0f, 200.0f, 12.0f});
    shufflePositions.add({3, 124.0f, 208.0f, 13.0f});
    shufflePositions.add({4, 143.0f, 213.0f, 14.0f});
    shufflePositions.add({5, 163.0f, 213.0f, 15.0f});
    shufflePositions.add({6, 184.0f, 208.0f, 16.0f});
    shufflePositions.add({7, 204.0f, 197.0f, 17.0f});
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
    popup6.setExpansionRadius(extra6R - 6.0f);
    popup6.setAnimDurationMs(extra6AnimDurationMs);
    popup6.setStaggerMs(extra6StaggerMs);
    popup3.setLabels({"RND", "64", "32", "16", "8", "4", "2", "1", "OFF"});
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
inline PlaygroundComponent::~PlaygroundComponent()
{
    if (ring)
        removeChildComponent(ring.get());
}
inline void PlaygroundComponent::setRunState(bool on, bool notifyParam)
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
inline void PlaygroundComponent::flashCurrentStepSegment()
{
    if (ring)
        ring->flashCurrentSegment();
}
inline void PlaygroundComponent::flashSegmentLogical(int s)
{
    if (ring)
        ring->flashSegmentLogical(s);
}
inline void PlaygroundComponent::flashFullRing(float i)
{
    if (ring)
        ring->triggerFullRingFlash(i);
}
inline void PlaygroundComponent::setResyncStepSelected(int s)
{
    if (ring)
        ring->setGlobalRestartLogical(juce::jlimit(1, 16, s) - 1);
    repaint();
}
inline void PlaygroundComponent::setHostTempo(double bpm)
{
    if (ring)
        ring->setBpm(bpm);
}
inline void PlaygroundComponent::setClickPulseState(bool on)
{
    clickToPulseOn = on;
    repaint();
}
inline void PlaygroundComponent::setExternalPlayheadStep(int s)
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

inline void PlaygroundComponent::setExternalBarNumber(int barNumber)
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
    lastExternalBarNumber = barNumber;

    // First time initialization: don't trigger immediately, just align counters
    if (previous == -1)
    {
        barsSinceAutoTrigger = 0;
        patternBarActive = false;
        return;
    }

    // A new bar has begun (host-provided). Decide whether this bar should be a pattern fill
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
inline void PlaygroundComponent::setTriggerModeState(bool on)
{
    for (int i = 0; i < onOffButtons.size(); ++i)
    {
        if (i < onOffButtonIdxs.size() && onOffButtonIdxs[i] == 8)
            if (onOffButtons[i])
                onOffButtons[i]->setState(on);
    }
    repaint();
}
inline void PlaygroundComponent::togglePopup3()
{
    if (popup3.isVisible() && !popup3.isAnimating())
        startCollapse3();
    else if (!popup3.isVisible() && !popup3.isAnimating())
        startExpand3();
}
inline void PlaygroundComponent::togglePopup6()
{
    if (popup6.isVisible() && !popup6.isAnimating())
        startCollapse6();
    else if (!popup6.isVisible() && !popup6.isAnimating())
        startExpand6();
}
inline void PlaygroundComponent::setHoverIndexFromEditor(int idx)
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
inline void PlaygroundComponent::clearHoverFromEditor()
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
inline void PlaygroundComponent::setForcedHoverIndex(int idx)
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
inline void PlaygroundComponent::clearForcedHoverIndex()
{
    if (forcedHoverIndex != -1)
    {
        forcedHoverIndex = -1;
        repaint();
    }
}
inline void PlaygroundComponent::setExternalHoverBlocked(bool b)
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
inline bool PlaygroundComponent::handleExternalClickIndex(int idx)
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
inline void PlaygroundComponent::setPatternEditButtonState(bool enabled)
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
inline void PlaygroundComponent::setHeaderHeight(int h)
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
inline void PlaygroundComponent::setClockRateIndexValue(int val)
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
inline void PlaygroundComponent::commitPendingSpeedMultiplier()
{
    if (pendingSpeedMultiplier > 0.0 && std::abs(pendingSpeedMultiplier - committedSpeedMultiplier) > 1e-6)
    {
        committedSpeedMultiplier = pendingSpeedMultiplier;
        pendingSpeedMultiplier = -1.0;
        if (ring)
            ring->setSpeedMultiplier(committedSpeedMultiplier);
    }
}
inline double PlaygroundComponent::getCommittedSpeedMultiplier() const noexcept { return committedSpeedMultiplier; }
inline double PlaygroundComponent::getPendingSpeedMultiplier() const noexcept { return pendingSpeedMultiplier; }
inline void PlaygroundComponent::setHoverTextEnabled(bool en)
{
    hoverTextEnabled = en;
    repaint();
}
inline bool PlaygroundComponent::getHoverTextEnabled() const noexcept { return hoverTextEnabled; }
inline void PlaygroundComponent::setClickToPulseHoverFromEditor(bool hover)
{
    if (clickToPulseHover != hover)
    {
        clickToPulseHover = hover;
        repaint();
    }
}
inline void PlaygroundComponent::paint(juce::Graphics &g)
{
    // Draw run-state indicator behind the donut: compute its bounds and draw
    // it here BEFORE the donut so the donut will paint on top (visually
    // occluding the indicator), which guarantees the indicator sits "behind"
    // the red donut in z-order. Keep the magenta diagnostic outline visible
    // in Release builds.
    juce::Rectangle<float> runRectFloat;
    if (circles.size() > 0)
    {
        const auto &c0 = circles.getReference(0);
        auto r = RunButton::suggestedBoundsForCentre((int)std::round(c0.x), (int)std::round(c0.y));
        runRectFloat = r.toFloat();
        // Draw the rotated run indicator here (static draw) so it is painted
        // before the donut and thus visually behind it.
        RunButton::drawAt(g, (int)std::round(c0.x), (int)std::round(c0.y), runState);

        // (Diagnostics removed per user request)
    }
    // Draw the red donut (outer ring minus inner hole) so it paints on top of
    // the statically drawn run indicator above. This restores the original
    // visual where the run indicator sits behind the donut.
    if (circles.size() > 0)
    {
        const auto &c0 = circles.getReference(0);
        float maskOuter = ring ? ring->getOuterRadius() : 70.0f;
        float maskInner = ring ? ring->getInnerRadius() : 55.0f;
        juce::Path donut;
        donut.addEllipse(c0.x - maskOuter, c0.y - maskOuter, maskOuter * 2.0f, maskOuter * 2.0f);
        donut.addEllipse(c0.x - maskInner, c0.y - maskInner, maskInner * 2.0f, maskInner * 2.0f);
        donut.setUsingNonZeroWinding(false);
        g.setColour(UiThemeColours::accent());
        g.fillPath(donut);
    }
    // Pattern ring (inward) when edit mode active
    if (patternEditMode && ring && circles.size() > 0)
    {
        const auto &c0 = circles.getReference(0);
        float outerR = (ring->getInnerRadius() - PatternGeometry::kPatternInset) + PatternGeometry::kPatternOutwardShift;
        float innerR = outerR * PatternGeometry::kPatternThicknessRatio; // keep proportional thickness
        // Hide inactive wedges (only show active cyan ones) per user request.
        pattern.draw(g, {c0.x, c0.y}, outerR, innerR, UiThemeColours::cyan(), UiThemeColours::base(), patternHoverIndex, true);
        // Center UI (RND amount number, AUTO toggle above, RND trigger below)
        float centreX = c0.x;
        float centreY = c0.y;
        float boxW = 40.0f;
        float boxH = 20.0f;
        float gapY = 2.0f;
        juce::Rectangle<float> amountRect(centreX - boxW * 0.5f, centreY - boxH * 0.5f, boxW, boxH);
        juce::Rectangle<float> autoRect(amountRect.withY(amountRect.getY() - boxH - gapY));
        juce::Rectangle<float> rndRect(amountRect.withY(amountRect.getY() + boxH + gapY));
        auto fontSmall = juce::Font(juce::FontOptions("Arial", 12.0f, juce::Font::bold));
        auto fontMid = juce::Font(juce::FontOptions("Arial", 15.0f, juce::Font::bold));
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
            juce::Colour fillCol = autoRandomizePattern ? UiThemeColours::cyan().withAlpha(0.33f) : UiThemeColours::accent().withAlpha(0.33f);
            juce::Colour strokeCol = autoRandomizePattern ? UiThemeColours::cyan().withAlpha(0.66f) : UiThemeColours::accent().withAlpha(0.66f);
            g.setColour(fillCol);
            g.fillPath(p);
            g.setColour(autoRandomizePattern ? UiThemeColours::cyan() : UiThemeColours::accent());
            g.setFont(fontSmall);
            g.drawFittedText("AUTO", autoRect.toNearestInt(), juce::Justification::centred, 1);
            g.setColour(strokeCol);
            g.strokePath(p, juce::PathStrokeType(1.0f));
        }
        // Amount rectangle (no rounded corners)
        g.setColour(UiThemeColours::accent().withAlpha(0.33f));
        g.fillRect(amountRect);
        g.setColour(UiThemeColours::cyan());
        g.setFont(fontMid);
        g.drawFittedText(juce::String(rndPatternAmount), amountRect.toNearestInt(), juce::Justification::centred, 1);
        g.setColour(UiThemeColours::accent().withAlpha(0.66f));
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
            juce::Colour baseFill = UiThemeColours::accent().withAlpha(0.33f);
            juce::Colour fillCol = baseFill.interpolatedWith(UiThemeColours::cyan(), flashAmt);
            juce::Colour strokeCol = UiThemeColours::accent().withAlpha(0.66f).interpolatedWith(UiThemeColours::cyan(), flashAmt);
            // Fade text colour between accent and cyan using flashAmt instead of popping.
            juce::Colour textCol = UiThemeColours::accent().interpolatedWith(UiThemeColours::cyan(), flashAmt);
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
            juce::Colour col = UiThemeColours::accent().interpolatedWith(UiThemeColours::cyan(), f);
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
        g.setColour(UiThemeColours::base());
        g.fillEllipse(c.x - insetR, c.y - insetR, insetR * 2, insetR * 2);
        const float startAng = -juce::MathConstants<float>::pi * 1.2f;
        const float endAng = juce::MathConstants<float>::pi * 0.2f;
        float ang = startAng + offsetRotaryT * (endAng - startAng);
        float needleR = insetR + 2.0f;
        float x2 = c.x + std::cos(ang) * needleR;
        float y2 = c.y + std::sin(ang) * needleR;
        g.setColour(UiThemeColours::accent().interpolatedWith(UiThemeColours::cyan(), offsetRotaryT));
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
            juce::Colour linearFill = UiThemeColours::accent();
            if (hoverOnLinear)
                linearFill = linearFill.darker(0.1f);

            float innerR = R * 0.6f;
            g.setColour(linearFill);
            g.fillEllipse(posPt.x - R, posPt.y - R, R * 2, R * 2);
            g.setColour(UiThemeColours::base());
            g.fillEllipse(posPt.x - innerR, posPt.y - innerR, innerR * 2, innerR * 2);

            int numeric = 50 + (int)std::round(linearShufflePos * 25.0f);
            g.setColour(UiThemeColours::cyan());
            g.setFont(juce::Font(juce::FontOptions("Arial", 12.0f, juce::Font::bold)));
            g.drawFittedText(juce::String(numeric), (int)(posPt.x - innerR), (int)(posPt.y - innerR), (int)(innerR * 2), (int)(innerR * 2), juce::Justification::centred, 1);
        }
        else if (selectedShuffle >= 1 && selectedShuffle <= shufflePositions.size())
        {
            auto pos = shufflePositions[selectedShuffle - 1];
            float R = pos.r;
            bool hoverSelected = (hoverShuffleId == selectedShuffle);

            juce::Colour shuffleFill = UiThemeColours::accent();
            if (hoverSelected)
                shuffleFill = shuffleFill.darker(0.1f);

            float innerR = R * 0.6f;
            g.setColour(shuffleFill);
            g.fillEllipse(pos.x - R, pos.y - R, R * 2, R * 2);
            g.setColour(UiThemeColours::base());
            g.fillEllipse(pos.x - innerR, pos.y - innerR, innerR * 2, innerR * 2);

            static const float sizes[7] = {11, 12, 13, 14, 15, 16, 17};
            float fontSize = (pos.id >= 1 && pos.id <= 7) ? sizes[pos.id - 1] : 12.0f;
            g.setColour(UiThemeColours::cyan());
            g.setFont(juce::Font(juce::FontOptions("Arial", fontSize, juce::Font::bold)));

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
    juce::String label;
    // Prefer forced/editor-driven hover or ring/small-circle hover over shuffle text
    // so moving from a shuffle point into the ring or inner-circle will show the
    // appropriate context help (wedge/resync/inner circle) instead of sticking
    // on the shuffle label.
    if (forcedHoverIndex >= 0 && forcedHoverIndex < hoverTexts.size())
    {
        // If the editor forced hover points to the small click/pulse control (idx 7),
        // show the concise help text only.
        if (forcedHoverIndex == 7)
            label = "Sample-click or pulse";
        else
            label = hoverTexts[forcedHoverIndex];
    }
    else if (ringHoverSegment >= 0)
        label = "Re-sync at step " + juce::String(ringHoverSegment + 1);
    else if (hoverIndex >= 0 && hoverIndex < hoverTexts.size())
    {
        // When hovering the small circle (idx 7) we want a concise single-line
        // help text instead of the numbered long description + appended suffix.
        if (hoverIndex == 7)
            label = "Sample-click or pulse";
        else
            label = juce::String(hoverIndex + 1) + ": " + hoverTexts[hoverIndex];
    }
    else if (hoverShuffleId != -1)
    {
        // If the linear shuffle mode (toggled by circle idx 5) is active,
        // show the linear percentage range. Otherwise show the discrete 1..7
        // intensity help text.
        if (linearShuffleMode && shufflePositions.size() > 1)
            label = "Shuffle intensity 50% - 75%";
        else
            label = "Shuffle intensity 1 (off) - 7";
    }
    // Do not append the generic "Click - Pulse" suffix when the small click/pulse
    // control (idx 7) is the source of the hover label — it already displays the
    // intended concise help text above.
    if (clickToPulseHover && hoverIndex != 7 && forcedHoverIndex != 7)
    {
        if (label.isNotEmpty())
            label += " — ";
        label += "Click - Pulse";
    }
    if (hoverTextEnabled && label.isNotEmpty())
    {
        // move hover text up 1px for tighter layout
        auto area = juce::Rectangle<int>(8, getHeight() - 17, getWidth() - 16, 18);
        g.setColour(UiThemeColours::cyan());
        g.setFont(juce::Font(juce::FontOptions("Arial", 12.0f, juce::Font::bold)));
        g.drawFittedText(label, area, juce::Justification::centred, 1);
    }
}
inline void PlaygroundComponent::paintOverChildren(juce::Graphics &g)
{
    if (circles.size() <= 1)
        return;

    const auto &c1 = circles.getReference(1);
    float outerR = c1.r;
    float fOuter = juce::jlimit(0.0f, 1.0f, flashes[1]);
    float outerScale = 1.0f + 0.2f * fOuter;
    float outerRScaled = outerR * outerScale;

    juce::Colour col = UiThemeColours::accent().interpolatedWith(UiThemeColours::cyan(), fOuter);
    g.setColour(col);
    g.fillEllipse(c1.x - outerRScaled, c1.y - outerRScaled, outerRScaled * 2, outerRScaled * 2);

    float insetR = 22.0f;
    float insetRScaled = insetR * (1.0f + 0.2f * fOuter);
    g.setColour(UiThemeColours::base());
    g.fillEllipse(c1.x - insetRScaled, c1.y - insetRScaled, insetRScaled * 2, insetRScaled * 2);

    int stepNum = externalStepForDisplay > 0 ? externalStepForDisplay : (ring ? ring->getCurrentStepLogical() : -1);
    juce::String stepText = stepNum > 0 ? juce::String(stepNum) : "-";
    g.setColour(UiThemeColours::cyan());
    g.setFont(juce::Font(juce::FontOptions("Arial", 30.0f, juce::Font::bold)));
    g.drawFittedText(stepText, (int)(c1.x - insetRScaled), (int)(c1.y - insetRScaled), (int)(insetRScaled * 2), (int)(insetRScaled * 2), juce::Justification::centred, 1);

    if (circles.size() > 7)
    {
        const auto &c7 = circles.getReference(7);
        float smallR = 8.0f;
        juce::Colour bcol = clickToPulseOn ? UiThemeColours::cyan() : UiThemeColours::accent();
        g.setColour(bcol);
        g.fillEllipse(c7.x - smallR, c7.y - smallR, smallR * 2, smallR * 2);
        if (clickToPulseHover)
        {
            g.setColour(UiThemeColours::cyan().withAlpha(0.25f));
            g.drawEllipse(c7.x - smallR - 2.0f, c7.y - smallR - 2.0f, (smallR + 2.0f) * 2, (smallR + 2.0f) * 2, 2.0f);
        }
    }

    if (circles.size() > 6)
    {
        const auto &c6 = circles.getReference(6);
        float insetR6 = 17.0f;
        g.setColour(UiThemeColours::base());
        g.fillEllipse(c6.x - insetR6, c6.y - insetR6, insetR6 * 2, insetR6 * 2);
        g.setColour(UiThemeColours::cyan());
        g.setFont(juce::Font(juce::FontOptions("Arial", 22.0f, juce::Font::bold)));
        g.drawFittedText(juce::String(mainCircle6Value), (int)(c6.x - insetR6), (int)(c6.y - insetR6), (int)(insetR6 * 2), (int)(insetR6 * 2), juce::Justification::centred, 1);
        if (popup6.isVisible() || popup6.isAnimating())
            popup6.draw(g, c6.x, c6.y, c6.r);
    }

    if (circles.size() > 3)
    {
        const auto &c3 = circles.getReference(3);
        float insetR3 = 17.0f;
        g.setColour(UiThemeColours::base());
        g.fillEllipse(c3.x - insetR3, c3.y - insetR3, insetR3 * 2, insetR3 * 2);
        g.setColour(UiThemeColours::cyan());
        g.setFont(juce::Font(juce::FontOptions("Arial", 14.0f, juce::Font::bold)));
        g.drawFittedText(mainCircle3Label, (int)(c3.x - insetR3), (int)(c3.y - insetR3), (int)(insetR3 * 2), (int)(insetR3 * 2), juce::Justification::centred, 1);
        if (popup3.isVisible() || popup3.isAnimating())
            popup3.draw(g, c3.x, c3.y, c3.r);
    }

    // (Shuffle debug overlay removed)
    // Status LED (top-right)
    const float ledR = 6.0f;
    float cx = (float)getWidth() - 6.0f;
    float cy = 12.0f;
    juce::Colour ledColour;
    juce::String statusLabel;
    switch (statusState)
    {
    // Increase Idle alpha so the LED remains visible against dark backgrounds.
    case StatusState::Idle:
        ledColour = UiThemeColours::accent().withAlpha(0.85f);
        statusLabel = "IDLE";
        break;
    case StatusState::Armed:
        ledColour = UiThemeColours::cyan().withAlpha(0.95f);
        statusLabel = "ARM";
        break;
    case StatusState::Pending:
        ledColour = UiThemeColours::accent().interpolatedWith(UiThemeColours::cyan(), 0.6f).withAlpha(0.95f);
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
    g.setColour(UiThemeColours::cyan().withAlpha(0.12f));
    g.fillEllipse(cx - (ledR + 3.0f), cy - (ledR + 3.0f), (ledR + 3.0f) * 2, (ledR + 3.0f) * 2);
    g.setColour(ledColour);
    g.fillEllipse(cx - ledR, cy - ledR, ledR * 2, ledR * 2);
    // Thin outer stroke for crisp edge visibility
    g.setColour(UiThemeColours::base().withAlpha(0.9f));
    g.drawEllipse(cx - ledR, cy - ledR, ledR * 2, ledR * 2, 1.2f);
    // Text to left of LED (moved right along with LED)
    juce::Font sf(juce::FontOptions("Arial", 11.0f, juce::Font::bold));
    g.setFont(sf);
    float textRight = cx - ledR + 2.0f;
    juce::Rectangle<float> txtArea(11.0f + 5.0f, cy - 8.0f, textRight - (11.0f + 5.0f), 16.0f);
    g.setColour(UiThemeColours::cyan());
    g.drawFittedText(statusLabel, txtArea.toNearestInt(), juce::Justification::right, 1);
}
inline void PlaygroundComponent::mouseDown(const juce::MouseEvent &e)
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
                rndPatternAmount = 50;
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
            // Indices: 0:RND 1:64 2:32 3:16 4:8 5:4 6:2 7:1 8:OFF
            // NOTE: do not reset barsSinceAutoTrigger here — changing the
            // interval should not immediately schedule or force a restart.
            autoTriggerRandom = false;
            autoTriggerIntervalBars = 0;
            switch (clicked)
            {
            case 0:
                autoTriggerRandom = true;
                break; // random each bar (probability)
            case 1:
                autoTriggerIntervalBars = 64;
                break;
            case 2:
                autoTriggerIntervalBars = 32;
                break;
            case 3:
                autoTriggerIntervalBars = 16;
                break;
            case 4:
                autoTriggerIntervalBars = 8;
                break;
            case 5:
                autoTriggerIntervalBars = 4;
                break;
            case 6:
                autoTriggerIntervalBars = 2;
                break;
            case 7:
                autoTriggerIntervalBars = 1;
                break;
            case 8:
            default:
                autoTriggerIntervalBars = 0;
                break; // OFF
            }
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
                ring->flashCurrentSegment();
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
        if (hr.index == 7)
        {
            isDraggingOffsetRotary = true;
            offsetDragStart = e.position;
            offsetRotaryStartT = offsetRotaryT;
            offsetRotaryT = angleToRotaryT(e.position);
            if (onClickRateRequested)
            {
                int nearest = (int)std::round(offsetRotaryT * (offsetRotarySteps - 1));
                onClickRateRequested(juce::jlimit(0, offsetRotarySteps - 1, nearest));
            }
            repaint();
            return;
        }
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
            // Update selectedShuffle for consistency (round to nearest)
            selectedShuffle = sid;
            isDraggingShuffle = true;
            isDraggingLinear = true;
            if (onShuffleStepRequested)
                onShuffleStepRequested(juce::jlimit(1, n, selectedShuffle));
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
    {
        isDraggingOffsetRotary = true;
        offsetDragStart = e.position;
        offsetRotaryStartT = offsetRotaryT;
        offsetRotaryT = angleToRotaryT(e.position);
        if (onClickRateRequested)
        {
            int nearest = (int)std::round(offsetRotaryT * (offsetRotarySteps - 1));
            onClickRateRequested(juce::jlimit(0, offsetRotarySteps - 1, nearest));
        }
        repaint();
        return;
    }
    default:
        break;
    }
}
inline void PlaygroundComponent::mouseDrag(const juce::MouseEvent &e)
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
            const float startAt12 = -juce::MathConstants<float>::halfPi;
            float rel = angle - startAt12;
            while (rel < 0.0f)
                rel += juce::MathConstants<float>::twoPi;
            float slice = juce::MathConstants<float>::twoPi / 16.0f;
            int raw = juce::jlimit(0, 15, (int)std::floor(rel / slice));
            // Apply same +4 rotation as click hit-test mapping so visual wedge under cursor matches drag index.
            static constexpr int kHighlightRotation = 4;
            int rotated = (raw + kHighlightRotation) & 15;
            if (rotated != lastPatternDragWedge)
            {
                lastPatternDragWedge = rotated;
                pattern.setStep(rotated, patternDragPaintState);
                repaint();
            }
        }
        return; // swallow other drag behaviors while pattern editing
    }
    if (patternEditMode && rndAmountDragging)
    {
        float dy = e.position.y - rndDragOrigin.y; // drag up decreases y -> increase amount
        int delta = (int)std::round(-dy / 2.0f);   // sensitivity
        rndPatternAmount = juce::jlimit(1, 100, rndAmountStart + delta);
        repaint();
        return;
    }
    if (isDraggingShuffle && shufflePositions.size() > 0)
    {
        if (isDraggingLinear)
        {
            linearShufflePos = pointToShufflePolylineT(e.position);
            linearShuffleAmount = 0.5f + 0.25f * linearShufflePos;
            if (onShuffleStepRequested)
            {
                int n = shufflePositions.size();
                int nearest = 1 + (int)std::round(linearShufflePos * (n - 1));
                onShuffleStepRequested(juce::jlimit(1, n, nearest));
            }
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
inline void PlaygroundComponent::mouseUp(const juce::MouseEvent &e)
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
inline void PlaygroundComponent::mouseMove(const juce::MouseEvent &e)
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
    switch (hr.type)
    {
    case ZoneType::RingCenter:
        newHover = 0;
        break;
    case ZoneType::SmallCircle:
        newHover = hr.index;
        break;
    case ZoneType::ClickPulseButton:
        newHover = 7;
        break;
    case ZoneType::RotaryHandle:
        newHover = 7;
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
    bool pulseHover = (hr.type == ZoneType::ClickPulseButton);
    if (pulseHover != clickToPulseHover)
    {
        clickToPulseHover = pulseHover;
        setMouseCursor(clickToPulseHover ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }
    if (newHover != hoverIndex)
    {
        hoverIndex = newHover;
        repaint();
    }
    juce::Component::mouseMove(e);
}
inline bool PlaygroundComponent::keyPressed(const juce::KeyPress &kp)
{
    // No key handling needed post cleanup; always allow other components/host to process.
    juce::ignoreUnused(kp);
    return false;
}

inline void PlaygroundComponent::timerCallback()
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
inline void PlaygroundComponent::resized()
{
    // Position children; the run indicator is now drawn statically in paint()
    // so there is no persistent visible child to position. Keep ring frontmost
    // among children so wedges/chase render above other UI children.
    if (ring)
        ring->toFront(false);
}
inline void PlaygroundComponent::makeButton(int idx)
{
    if (idx < 0 || idx >= circles.size())
        return;
    const auto &c = circles.getReference(idx);
    auto *b = onOffButtons.add(new OnOffButton());
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
        // so we no longer create a persistent visible child here. Clicks are
        // handled via the ring center callback which forwards to the editor
        // (APVTS) path.
    }
    else if (idx == 2)
    {
        b->onToggled = [this](bool on)
        { if (onPatternEditToggled) onPatternEditToggled(on); };
    }
    addAndMakeVisible(b);
}
inline juce::Point<float> PlaygroundComponent::getPointAlongShufflePolyline(float t) const
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
inline float PlaygroundComponent::pointToShufflePolylineT(juce::Point<float> p) const
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
inline float PlaygroundComponent::angleToRotaryT(juce::Point<float> p) const
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
inline void PlaygroundComponent::startFadeTimer()
{
    if (!isTimerRunning)
    {
        isTimerRunning = true;
        lastTickMs = juce::Time::getMillisecondCounterHiRes();
        startTimerHz(60);
    }
}
inline void PlaygroundComponent::startExpand6()
{
    popup6.startExpand();
    startFadeTimer();
}
inline void PlaygroundComponent::startCollapse6()
{
    popup6.startCollapse();
    startFadeTimer();
}
inline void PlaygroundComponent::startExpand3()
{
    popup3.startExpand();
    startFadeTimer();
}
inline void PlaygroundComponent::startCollapse3()
{
    popup3.startCollapse();
    startFadeTimer();
}
inline void PlaygroundComponent::startBeatPulse()
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
inline void PlaygroundComponent::randomizePatternSteps()
{
    // Randomization respects rndPatternAmount as activation probability per UI step (1..16)
    float p = rndPatternAmount / 100.0f;
    int activeCount = 0;
    for (int ui = 1; ui <= 16; ++ui)
    {
        bool on = autoTriggerRng.nextFloat() < p;
        int stored = uiStep1to16ToStoredIndex(ui);
        pattern.setStep(stored, on);
        if (on)
            ++activeCount;
    }
    if (activeCount == 0)
    {
        int pickUi = 1 + autoTriggerRng.nextInt(16);
        int stored = uiStep1to16ToStoredIndex(pickUi);
        pattern.setStep(stored, true); // ensure at least one active so fill isn't silent
    }
}

// ======= UI setters implemented inline =======
inline void PlaygroundComponent::setPopup3Index(int popupIndex)
{
    auto labs = popup3.getLabels();
    if (popupIndex >= 0 && popupIndex < labs.size())
        mainCircle3Label = labs.getReference(popupIndex);
    repaint();
}

inline void PlaygroundComponent::setMainCircle6Value(int val)
{
    mainCircle6Value = val;
    repaint();
}

inline void PlaygroundComponent::setSelectedShuffle(int v)
{
    selectedShuffle = v;
    repaint();
}

inline void PlaygroundComponent::setLinearShuffleModeState(bool on)
{
    linearShuffleMode = on;
    repaint();
}
