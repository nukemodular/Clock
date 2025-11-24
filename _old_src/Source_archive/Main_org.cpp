#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>
#include <vector>
#include <cmath>
#include "LookAndFeels.h" // UiThemeColours

// Simple on/off button component
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

// 16-part ring component
class Ring16Component : public juce::Component, private juce::Timer
{
public:
    Ring16Component()
    {
        setSize ((int)(outerR*2), (int)(outerR*2));
        masterStartMs = juce::Time::getMillisecondCounterHiRes();
        startTimerHz (60);
        globalRestartLogical = 0; // default to logical segment 1
        // Make visual segment 1 lit by default on startup (logical 0)
        selected = logicalToRaw (globalRestartLogical);
        if (selected >= 0 && selected < ringFlashes.size())
            ringFlashes.set (selected, 1.0f);
    }

    void paint (juce::Graphics& g) override
    {
        auto boundsF = getLocalBounds().toFloat();
        const float innerProp = innerR / outerR;
        const float step = juce::MathConstants<float>::twoPi / 16.0f;
        const float start0 = -juce::MathConstants<float>::halfPi;

        for (int i = 0; i < 16; ++i)
        {
            const float a0 = start0 + i*step;
            const float a1 = a0 + step;
            juce::Path seg;
            seg.addPieSegment (boundsF, a0, a1, innerProp);

            juce::Colour col = UiThemeColours::accent();//.darker (0.20f);
            if (i == hovered) col = UiThemeColours::accent().brighter (0.15f);
            if (i == selected) col = UiThemeColours::cyan();
            if (ringFlashes[i] > 0.0f)
                col = col.interpolatedWith (UiThemeColours::cyan(), juce::jlimit (0.0f, 1.0f, ringFlashes[i]));

            //g.setColour (col.withAlpha (0.95f));
            g.setColour (col);
            g.fillPath (seg);

            // (no persistent indicator - startup lighting is done via `selected` + initial flash)
        }

        // center control
        const auto c = boundsF.getCentre();
        const float innerD = innerR*2.0f;
        juce::Rectangle<float> innerCircle (c.x - innerR, c.y - innerR, innerD, innerD);
        g.setColour (isPlaying ? UiThemeColours::cyan().withAlpha (0.25f) : UiThemeColours::accent().withAlpha (0.18f));
        g.fillEllipse (innerCircle);
        g.setColour (UiThemeColours::cyan().withAlpha (0.9f));
        g.setFont (juce::Font (juce::FontOptions ("Arial", 11.0f, juce::Font::bold)));
        g.drawFittedText (isPlaying ? "CHASE ON" : "CHASE OFF", innerCircle.toNearestInt(), juce::Justification::centred, 1);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        // detect inner-circle hover specially
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

    // Return current playing logical step (1..16) or -1 if none
    int getCurrentStepLogical() const
    {
        if (lastStepIndex < 0) return -1;
        return rawToLogical (lastStepIndex) + 1;
    }

    // Expose ring radii so parent paint can stay in sync with the ring geometry.
    float getOuterRadius() const noexcept { return outerR; }
    float getInnerRadius() const noexcept { return innerR; }

    void requestRestartAtNextMasterBoundary() { pendingRestartAtNextBoundary = true; }
    void setResyncOnBarOneEnabled (bool en) { resyncOnBarOneEnabled = en; }
    void setGlobalRestartLogical (int logical) { if (logical >= 0 && logical < 16) globalRestartLogical = logical; }
    int getGlobalRestartLogical() const noexcept { return globalRestartLogical; }

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

    // Only accept mouse events that hit a visible wedge or the center inner circle.
    bool hitTest (int x, int y) override
    {
        juce::Point<float> p ((float) x, (float) y);
        if (isInsideInner (p)) return true;
        return (hitSegment (p) >= 0);
    }

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

        // fade (exponential smoothing) - multiply by 0.9 each tick for a smoother decay
        for (int i = 0; i < 16; ++i)
        {
            float v = ringFlashes[i];
            if (v > 0.0f)
            {
                v *= 0.9f;
                if (v < 0.001f) v = 0.0f;
                ringFlashes.set (i, v);
            }
        }

        if (isPlaying)
        {
            const double elapsed = now - playStartMs;
            const double phase = std::fmod (elapsed, barMs) / barMs;
            int seg = (int) std::floor (phase * 16.0);
            seg = juce::jlimit (0, 15, seg);
            const int segRaw = (seg + logicalToRaw (0)) % 16;
            if (segRaw != lastStepIndex)
            {
                lastStepIndex = segRaw;
                ringFlashes.set (segRaw, 1.0f);
                if (onStepChanged) onStepChanged (rawToLogical (segRaw) + 1);
            }
        }

        repaint();
    }

    bool isPlaying = false;
    double bpm = 120.0;
    double playStartMs = 0.0;
    double lastTickMs = 0.0;
    int lastStepIndex = -1;
    juce::Array<float> ringFlashes { juce::Array<float> (16, 0.0f) };

    double masterStartMs = 0.0;
    int lastMasterSeg = -1;
    bool pendingRestartAtNextBoundary = false;
    bool resyncOnBarOneEnabled = false;
    int globalRestartLogical = -1;

    const float outerR = 70.0f;
    const float innerR = 55.0f;
    int hovered = -1;
    int selected = -1;
    bool innerHovered = false;
};
