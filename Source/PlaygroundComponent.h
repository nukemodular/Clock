// ============================================================================
// Clean deduplicated PlaygroundComponent header
// ============================================================================
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <cmath>
#include <vector>
#include <array>
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
    OnOffButton(UiThemeColours& t) : theme(t)
    {
        setSize(diameter, diameter);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    void paint(juce::Graphics &g) override
    {
        auto b = getLocalBounds().toFloat();
        auto c = b.getCentre();
        g.setColour(theme.accent());
        g.fillEllipse(b);
        const float innerR = 8.0f;
        g.setColour(theme.base());
        g.fillEllipse(c.x - innerR, c.y - innerR, innerR * 2, innerR * 2);
        if (isOn)
        {
            const float dotR = 5.0f;
            g.setColour(theme.cyan());
            g.fillEllipse(c.x - dotR, c.y - dotR, dotR * 2, dotR * 2);
        }
    }
    void mouseDown(const juce::MouseEvent &e) override
    {
        if (e.mods.isShiftDown() && onShiftClicked)
        {
            onShiftClicked();
            return;
        }
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
    std::function<void()> onShiftClicked;
    std::function<void(bool)> onHoverChanged;

private:
    bool rotaryHandleHover = false;
    static constexpr int diameter = 28;
    bool isOn = false;
    UiThemeColours& theme;
};

// --------------------------------------------------------------
// Ring16Component (16 wedge ring; logical numbering identity to raw wedge index for click & hover alignment)
// --------------------------------------------------------------
class Ring16Component : public juce::Component, private juce::Timer
{
public:
    // Quick toggle for experimentation (set to false to revert the new behaviour).
    static constexpr bool kShowOffsetHoverInPatternEdit = true;

    Ring16Component(UiThemeColours& t) : theme(t)
    {
        setWantsKeyboardFocus(true);
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
        rebuildPaths();
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
    void triggerFullRingFlash(float intensity = 1.0f)
    {
        fullRingFlash = juce::jlimit(0.0f, 1.5f, intensity);
        // Ensure the decay timer is running so the cyan overlay fades out.
        if (! isTimerRunning()) startTimerHz(60);
        if (! isTimerRunning())
        {
            lastTickMs = juce::Time::getMillisecondCounterHiRes();
            startTimerHz(60);
        }
        repaint();
    }
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
            {
                playStartMs = juce::Time::getMillisecondCounterHiRes();
            startTimerHz(60);
                lastTickMs = playStartMs;
                startTimerHz(60);
            }
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
            static constexpr int kHighlightRotation = 0;
            // Draw outer reference ring faint with adjusted alpha.
            //   g.setColour(theme.accent().withAlpha(0.1f));
            //   g.drawEllipse(b.getCentreX()-outerR, b.getCentreY()-outerR, outerR*2, outerR*2, 1.0f);
            const int selIdx = selected;
            const int chaseIdx = playheadRaw;

            // Hover wedge (subtle) so OFFSET selection intent is obvious even in pattern edit mode.
            if (kShowOffsetHoverInPatternEdit && hovered >= 0)
            {
                const int hovIdx = hovered & 15;
                if (hovIdx != selIdx && hovIdx != chaseIdx)
                {
                    const juce::Path& seg = wedgePaths[(size_t) hovIdx];
                    g.setColour(theme.accent().darker(0.25f).withAlpha(0.25f));
                    g.fillPath(seg);
                }
            }

            // Flashed wedges in pattern edit mode (e.g. trigger click)
            for (int i = 0; i < 16; ++i)
            {
                float flash = ringFlashes[i];
                if (flash > 0.01f && i != selIdx && i != chaseIdx)
                {
                    const juce::Path& seg = wedgePaths[(size_t) i];
                    g.setColour(theme.cyan().withAlpha(juce::jlimit(0.0f, 1.0f, flash)));
                    g.fillPath(seg);
                }
            }

            // Selected offset wedge (radially reduced by 1px)
            if (selIdx >= 0)
            {
                const juce::Path& segHi = wedgePathsSelected[(size_t) selIdx];
                g.setColour(theme.cyan().withAlpha(0.55f));
                g.fillPath(segHi);
                g.setColour(theme.cyan().withAlpha(1.0f));
                g.strokePath(segHi, juce::PathStrokeType(1.0f));
            }

            // Chase/playhead wedge (different) — draw full wedge so chase colour renders at full thickness
            if (chaseIdx >= 0)
            {
                const juce::Path& seg = wedgePaths[(size_t) chaseIdx];
                g.setColour(theme.cyan().withAlpha(isPlaying ? 0.75f : 0.45f));
                g.fillPath(seg);
            }
            return;
        }
        auto b = getLocalBounds().toFloat();
        const float innerProp = innerR / outerR;
        const float step = juce::MathConstants<float>::twoPi / 16.0f;
        // Default geometry: start at 12 o'clock (step 1 at top).
        const float start0 = -juce::MathConstants<float>::halfPi;
        int playheadRaw = useExternalPlayhead ? externalPlayheadRaw : lastStepIndex;
        static constexpr int kHighlightRotation = 0; // visually advance highlight by +4 wedges
        for (int i = 0; i < 16; ++i)
        {
            const juce::Path &seg = wedgePaths[(size_t)i];
            static constexpr int kHighlightRotation = 0; // visually advance highlight by +4 wedges
            bool isSel = (i == ((selected + kHighlightRotation) & 15));
            bool isPlay = (i == ((playheadRaw + kHighlightRotation) & 15));
            int hoverIdx = hovered >= 0 ? ((hovered + kHighlightRotation) & 15) : -1;
            bool isHover = (i == hoverIdx);
            float flash = ringFlashes[i];
            float fade = segmentFade[i];
            if (isSel || isPlay)
            {
                if (isSel)
                {
                    // Selected wedge uses a slightly reduced thickness and an outline.
                    const juce::Path &segHi = wedgePathsSelected[(size_t)i];
                    g.setColour(theme.cyan().withAlpha(0.55f));
                    g.fillPath(segHi);
                    // Keep selected wedge outline cyan-only so it doesn't draw accent over the parent donut.
                    g.setColour(theme.cyan().withAlpha(1.0f));
                    g.strokePath(segHi, juce::PathStrokeType(1.0f));
                }
                else if (isPlay)
                {
                    // Playhead/chase should render at full wedge geometry (no reduced inset)
                    g.setColour(theme.cyan().withAlpha(isPlaying ? 1.0f : 0.45f));
                    g.fillPath(seg);
                    // Do not stroke the playhead wedge here so the underlying chaselight can show through.
                }
                continue; // skip normal seg painting when highlighted
            }
            if (flash > 0.01f)
            {
                g.setColour(theme.cyan().withAlpha(juce::jlimit(0.0f, 1.0f, flash)));
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
                g.setColour(theme.cyan().withAlpha(fade));
                g.fillPath(seg);
            }
            else if (!isPlay && !isSel && isHover)
            {
                g.setColour(theme.accent().darker(0.25f).withAlpha(0.25f));
                g.fillPath(seg);
            }
        }
        if (fullRingFlash > 0.01f)
        {
            g.setColour(theme.cyan().withAlpha(juce::jlimit(0.0f, 0.85f, fullRingFlash)));
            g.fillPath(donutPathCache);
        }
        g.setColour(theme.accent().withAlpha(0.15f));
        g.drawEllipse(b.getCentreX() - innerR, b.getCentreY() - innerR, innerR * 2, innerR * 2, 1.0f);
    }
    void resized() override { rebuildPaths(); }
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
        // Ensure we have focus when user interacts so subsequent key presses work.
        if (!hasKeyboardFocus(true))
            grabKeyboardFocus();

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
            bool changed = (selected != s);
            selected = s; // raw wedge for highlight
            int logical = rawToLogical(selected);

            if (onClicked)
                onClicked(logical); // pass identity logical
            // If offset changed, schedule a resync at next master boundary and update global restart logical mapping.
            if (changed)
            {
                setGlobalRestartLogical(logical);
                requestRestartAtNextMasterBoundary();
                if (onStepChanged)
                    onStepChanged(logical);
            }
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
        // Hit testing start at 12 o'clock for identity mapping.
        // atan2 returns 0 at 3 o'clock, so 12 o'clock is -pi/2.
        const float startAt12 = -juce::MathConstants<float>::halfPi;
        float rel = angle - startAt12;
        while (rel < 0.0f)
            rel += juce::MathConstants<float>::twoPi;
        float slice = juce::MathConstants<float>::twoPi / 16.0f;
        int raw = (int)std::floor(rel / slice) & 15;
        if (raw != selected)
        {
            selected = raw;
            int logical = rawToLogical(selected);
            if (onClicked)
                onClicked(logical);
            // Schedule resync on drag offset change as well.
            setGlobalRestartLogical(logical);
            requestRestartAtNextMasterBoundary();
            if (onStepChanged)
                onStepChanged(logical);
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
    UiThemeColours& theme;
    void rebuildPaths()
    {
        auto b = getLocalBounds().toFloat();
        wedgePaths.fill(juce::Path());
        wedgePathsSelected.fill(juce::Path());
        const float innerProp = innerR / outerR;
        const float step = juce::MathConstants<float>::twoPi / 16.0f;
        const float start0 = 0.0f; // 12 o'clock in JUCE addPieSegment
        auto b2 = b.reduced(0.5f);
        float innerPropSel = (innerR + 0.5f) / outerR;
        for (int i = 0; i < 16; ++i)
        {
            juce::Path p;
            p.addPieSegment(b, start0 + i * step, start0 + (i + 1) * step, innerProp);
            wedgePaths[(size_t)i] = std::move(p);
            juce::Path ph;
            ph.addPieSegment(b2, start0 + i * step, start0 + (i + 1) * step, innerPropSel);
            wedgePathsSelected[(size_t)i] = std::move(ph);
        }
        juce::Path rp;
        rp.addEllipse(b.getCentreX() - outerR, b.getCentreY() - outerR, outerR * 2, outerR * 2);
        rp.addEllipse(b.getCentreX() - innerR, b.getCentreY() - innerR, innerR * 2, innerR * 2);
        rp.setUsingNonZeroWinding(false);
        donutPathCache = std::move(rp);
    }
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
        // atan2 returns 0 at 3 o'clock, so 12 o'clock is -pi/2.
        const float startAt12 = -juce::MathConstants<float>::halfPi;
        float rel = angle - startAt12;
        while (rel < 0.0f)
            rel += juce::MathConstants<float>::twoPi;
        float slice = juce::MathConstants<float>::twoPi / 16.0f;
        int raw = (int)std::floor(rel / slice) & 15;
        return raw;
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
        else
        {
            segmentHoldUntilMs.set(rawIndex, 0.0);
        }
        if (! isTimerRunning())
        {
            lastTickMs = juce::Time::getMillisecondCounterHiRes();
            startTimerHz(60);
        }
        repaint();
    }
    void timerCallback() override
    {
        double now = juce::Time::getMillisecondCounterHiRes();
        double dt = now - lastTickMs;
        lastTickMs = now;
        if (dt < 0.0 || dt > 100.0) dt = 16.67;
        bool any = false;
        if (isPlaying && !useExternalPlayhead)
        {
            any = true;
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
        // Decay full ring flash
        if (fullRingFlash > 0.0f)
        {
            fullRingFlash *= 0.92f;
            if (fullRingFlash < 0.01f)
                fullRingFlash = 0.0f;
            any = true;
        }

        if (any)
            repaint();
        else
            stopTimer();
    }
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
    // timing/state
    double masterStartMs = 0.0;
    double playStartMs = 0.0;
    double lastTickMs = 0.0;
    double tickAccumMs = 0.0;
    double bpm = 120.0;
    bool isPlaying = false;
    float fullRingFlash = 0.0f;
    juce::Array<float> ringFlashes;
    juce::Array<float> segmentFade;
    juce::Array<double> segmentHoldUntilMs;
    static constexpr double kFlashHoldMs = 280.0;
    static constexpr float kFadeDecayMs = 300.0f;
    static constexpr float kFlashDecayFactor = 0.92f;
    const float outerR = 70.0f;
    const float innerR = 55.0f;
    bool patternEditMode = false;
    bool draggingWedges = false;
    std::array<juce::Path, 16> wedgePaths;
    std::array<juce::Path, 16> wedgePathsSelected;
    juce::Path donutPathCache;
};

// --------------------------------------------------------------
// PlaygroundComponent (clean)
// --------------------------------------------------------------
class PlaygroundComponent : public juce::Component, private juce::Timer
{
public:
    PlaygroundComponent(UiThemeColours& t);
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
    // Called for automatic pattern-driven step triggers (NOT user clicks).
    // Editor should NOT map this to `processor.requestTriggerOnce()` to avoid
    // mutating processor run/restart state from UI playback; keep it for
    // visual-only preview handling instead.
    std::function<void()> onAutoTriggerRequested;
    std::function<void(int)> onClockRateIndexRequested;
    std::function<void(int)> onShuffleStepRequested;
    std::function<void(bool)> onClockWhileStoppedRequested;
    std::function<void(int)> onClickRateRequested;
    std::function<void(bool)> onClickPulseRequested;
    std::function<void(bool)> onTriggerModeRequested;
    std::function<void(int)> onPopup3Selected;
    std::function<void(bool)> onPatternEditToggled;
    std::function<void()> onPatternEditShiftClicked;
    // Callback invoked when the pattern bitmask changes via UI edits
    std::function<void(uint16_t)> onPatternChanged;
    // Additional callbacks the editor can wire for UI-only toggles
    std::function<void(bool)> onLinearShuffleModeChanged;
    // Linear shuffle amount callback: sends continuous 0.5..0.75 range to editor
    std::function<void(float)> onLinearShuffleAmountChanged;

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
    juce::String getCurrentHoverText() const;
    void setClickToPulseHoverFromEditor(bool hover);
    // UI state setters (used by editor to restore visual-only state)
    void setPopup3Index(int popupIndex);
    void setMainCircle6Value(int val);
    void setSelectedShuffle(int v);
    void setLinearShuffleModeState(bool on);
    void setLinearShuffleAmount(float amount);
    void setPatternBarActive(bool active);
    bool isPatternBarActive() const noexcept { return patternBarActive; }
    void setSubmenuActive(bool active)
    {
        if (submenuActive != active)
        {
            submenuActive = active;
            repaint();
        }
    }
    bool isSubmenuActive() const noexcept { return submenuActive; }

    void setClickRateIndex(int index)
    {
        int nearest = juce::jlimit(0, offsetRotarySteps - 1, index);
        offsetRotaryT = (offsetRotarySteps > 1) ? (float) nearest / (float) (offsetRotarySteps - 1) : 0.0f;
        repaint();
    }
    int getClickRateIndex() const noexcept
    {
        return juce::jlimit(0, offsetRotarySteps - 1, (int) std::round(offsetRotaryT * (offsetRotarySteps - 1)));
    }
    juce::String getClickRateText() const
    {
        const int idx = getClickRateIndex();
        static const juce::String names[] = { "off", "beat", "8th", "16th", "24ppq", "48ppq", "96ppq" };
        if (idx >= 0 && idx < 7)
            return names[idx];
        return "off";
    }
    bool isRotaryHandleDragging() const noexcept { return isDraggingOffsetRotary; }

    // Pattern bitmask accessors for external sync (editor)
    inline uint16_t getPatternBitmask() const noexcept { return (uint16_t) pattern.getBitmask(); }
    inline void setPatternBitmask(uint16_t m) { pattern.setBitmask(m); repaint(); }

    // Helper: convert UI step (1..16) to stored bit index (0..15) using visual +4 rotation
    inline int uiStep1to16ToStoredIndex(int step1to16) const noexcept { int l0 = juce::jlimit(1, 16, step1to16) - 1; return l0; }

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
    UiThemeColours& theme;
    // Pre-created fonts to avoid CoreText static lifetime issues
    juce::Font fontSmall12{ juce::FontOptions("Arial", 12.0f, juce::Font::bold) };
    juce::Font fontMid15{ juce::FontOptions("Arial", 15.0f, juce::Font::bold) };
    juce::Font fontRndSmall14{ juce::FontOptions("Arial", 14.0f, juce::Font::bold) };
    juce::Font fontRndNum24{ juce::FontOptions("Arial", 24.0f, juce::Font::bold) };
    juce::Font fontHover12{ juce::FontOptions("Arial", 12.0f, juce::Font::bold) };
    juce::Font fontStep30{ juce::FontOptions("Arial", 30.0f, juce::Font::bold) };
    juce::Font fontMain6_22{ juce::FontOptions("Arial", 22.0f, juce::Font::bold) };
    juce::Font fontStatus10{ juce::FontOptions("Arial", 10.0f, juce::Font::bold) };
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
    static constexpr int offsetRotarySteps = 7;
    bool isDraggingOffsetRotary = false;
    juce::Point<float> offsetDragStart{0, 0};
    float offsetRotaryStartT = 0.0f;
    float offsetRotaryT = 0.0f;
    bool clickToPulseOn = false;
    bool clickToPulseHover = false;
    bool hoverTextEnabled = true;
    bool rotaryHandleHover = false;
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
    bool submenuActive = false;        // true when setup submenu ("S") is active/opening
    int barsSinceAutoTrigger = 0;      // completed bars since last auto trigger
    int lastExternalBarNumber = -1;    // last host-provided bar number (for robust interval tracking)
    juce::Random autoTriggerRng;       // random source for RND mode
    bool patternEditMode = false;      // active when pattern edit button (circle idx2) toggled on
    bool patternBarActive = false;     // true while pattern is firing steps this bar (one-bar fill unless interval=1 for continuous)
    bool patternRandomizedForCurrentFill = false; // ensures randomization occurs only once AFTER the pattern has finished triggering
    int rndPatternAmount = 6;          // 1..16: exact number of randomized steps
    bool autoRandomizePattern = false; // AUTO toggle: randomize pattern each time it engages
    bool rndAmountDragging = false;
    int rndAmountStart = 50;
    juce::Point<float> rndDragOrigin{0, 0};
    float rndTriggerFlash = 0.0f; // transient flash for RND button
    // Status LED recovery (header right side)
    enum class StatusState { Idle, Armed, Pending };
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
