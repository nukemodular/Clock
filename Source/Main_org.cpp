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

// Playground component
class PlaygroundComponent : public juce::Component, private juce::Timer
{
public:
    PlaygroundComponent()
    {
        setSize (300, 240);

        circles.add ({ 150.0f, 130.0f, 70.0f, juce::Colours::orangered });
        circles.add ({ 244.0f, 85.0f, 35.0f, juce::Colours::goldenrod });
        circles.add ({ 234.0f, 132.0f, 14.0f, juce::Colours::turquoise });
        circles.add ({ 235.0f, 170.0f, 25.0f, juce::Colours::mediumpurple });
        circles.add ({ 95.0f, 189.0f, 11.0f, juce::Colours::yellowgreen });
        circles.add ({ 78.0f, 172.0f, 14.0f, juce::Colours::pink });
        circles.add ({ 56.0f, 140.0f, 25.0f, juce::Colours::seagreen });
        circles.add ({ 70.0f, 80.0f, 25.0f, juce::Colours::greenyellow });
        circles.add ({ 201.0f, 64.0f, 14.0f, juce::Colours::blueviolet });

        flashes.resize (circles.size());
        for (int i = 0; i < flashes.size(); ++i) flashes.set (i, 0.0f);

        hoverTexts = juce::StringArray {
            "Stop and re-start next bar + offset", "Re-trigger quantized", "Re-sync at bar + offset on/off",
            "Start offset step", "Shuffle Amount", "Switch shuffle type 909 or linear",
            "Set scale 1/32, 1/16, 1/8, 1/4", "Audio Click: off, 4th,8th, 16th, 24ppq", "Autofill on/off"
        };

        ring.reset (new Ring16Component());
        if (circles.size() > 0)
        {
            const auto& c0 = circles.getReference (0);
            const int size = 140;
            ring->setBounds ((int)(c0.x - 70.0f), (int)(c0.y - 70.0f), size, size);
        }
        ring->onHoverChanged = [this](int logical){ ringHoverSegment = logical; repaint(); };
        // Notify parent when the ring inner circle is hovered so we can show hoverTexts for index 0
        ring->onInnerHover = [this](bool in){ if (in) { hoverIndex = 0; ringHoverSegment = -1; } else if (hoverIndex == 0) { hoverIndex = -1; } repaint(); };
        ring->onClicked = [this](int logical){ ringSelectedSegment = logical; if (ring) ring->setGlobalRestartLogical (logical); repaint(); };
        ring->onStepChanged = [this](int /*logical1*/){ repaint(); };
        addAndMakeVisible (ring.get());

        auto makeButton = [this](int idx)
        {
            if (idx < 0 || idx >= circles.size()) return;
            const auto& c = circles.getReference (idx);
            auto* b = onOffButtons.add (new OnOffButton());
            b->setBounds ((int)(c.x - 14.0f), (int)(c.y - 14.0f), 28, 28);
            b->onHoverChanged = [this, idx](bool over){ hoverIndex = over ? idx : (hoverIndex == idx ? -1 : hoverIndex); repaint(); };
            if (idx == 2) // circle 3 button controls resync
                b->onToggled = [this](bool on){ if (ring) ring->setResyncOnBarOneEnabled (on); };
            else if (idx == 5)
                // circle 6 toggles between quantized shuffle (1..7) and a linear fader mode
                b->onToggled = [this](bool on)
                {
                    linearShuffleMode = on;
                    // initialize linear pos from current selectedShuffle when turning off
                    if (! linearShuffleMode && shufflePositions.size() > 1)
                        linearShufflePos = (selectedShuffle - 1) / (float)(shufflePositions.size() - 1);
                    repaint();
                };
            addAndMakeVisible (b);
        };

        makeButton (2);
        makeButton (5);
        makeButton (8);

        // Initialize shuffle positions (replace circle 5 region)
        shufflePositions.add ({ 1, 95.0f, 189.0f, 11.0f });
        shufflePositions.add ({ 2, 108.0f, 200.0f, 12.0f });
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

        // Ensure ring is behind other UI elements so our overlayless inset (painted in
        // paintOverChildren) remains visually on top. Previously we used a dedicated
        // overlay child for input; we now centralize hit-testing in the parent.
        if (ring) ring->toBack();

        // prepare extra6 progress values (start collapsed)
        extra6Progress.clear();
        for (int i = 0; i < extra6Count; ++i) extra6Progress.add (0.0f);
        // prepare per-child hover scale arrays (used to animate hover scaling smoothly)
        extra6HoverScale.clear(); extra6HoverTarget.clear();
        for (int i = 0; i < extra6Count; ++i) { extra6HoverScale.add (1.0f); extra6HoverTarget.add (1.0f); }

        // labels / numeric values for the extra-6 popup children (clock divisions)
        extra6Labels.clear(); extra6Labels.add ("4"); extra6Labels.add ("8"); extra6Labels.add ("16"); extra6Labels.add ("32");
        extra6Values.clear(); extra6Values.add (4); extra6Values.add (8); extra6Values.add (16); extra6Values.add (32);
        mainCircle6Value = 16; // default shown in the main circle inset
    }

    ~PlaygroundComponent() override
    {
        if (ring) removeChildComponent (ring.get());
        for (auto* b : onOffButtons) if (b) removeChildComponent (b);
    }

    // Set per-shuffle text offset (id in 1..N). Call this from code to fine-tune placement.
    void setShuffleTextOffset (int id, float dx, float dy)
    {
        const int idx = id - 1;
        if (idx >= 0 && idx < shuffleTextOffsets.size())
        {
            shuffleTextOffsets.getReference (idx).dx = dx;
            shuffleTextOffsets.getReference (idx).dy = dy;
            repaint();
        }
    }

    juce::Point<float> getShuffleTextOffset (int id) const
    {
        const int idx = id - 1;
        if (idx >= 0 && idx < shuffleTextOffsets.size())
            return { shuffleTextOffsets.getReference (idx).dx, shuffleTextOffsets.getReference (idx).dy };
        return { 0.0f, 0.0f };
    }

    void paint (juce::Graphics& g) override
    {
        // g.fillAll (UiThemeColours::base());
        // g.setColour (UiThemeColours::cyan().withAlpha (0.1f));
        // for (int x = 0; x < getWidth(); x += 20) g.drawVerticalLine (x, 0.0f, (float)getHeight());
        // for (int y = 0; y < getHeight(); y += 20) g.drawHorizontalLine (y, 0.0f, (float)getWidth());

        // Draw a donut-shaped mask behind the ring (circle index 0) to hide thin aliasing lines.
        // Use the ring's own radius values so the visual mask stays in sync if the ring constants change.
        if (circles.size() > 0)
        {
            const auto& c0 = circles.getReference (0);
            float maskOuter = 70.0f; // default
            float maskInner = 55.0f; // default
            if (ring) { maskOuter = ring->getOuterRadius(); maskInner = ring->getInnerRadius(); }

            juce::Path donutPath;
            donutPath.addEllipse (c0.x - maskOuter, c0.y - maskOuter, maskOuter*2.0f, maskOuter*2.0f);
            donutPath.addEllipse (c0.x - maskInner, c0.y - maskInner, maskInner*2.0f, maskInner*2.0f);
            // Use the alternate winding rule (even-odd) so the second ellipse becomes a hole.
            donutPath.setUsingNonZeroWinding (false);
            g.setColour (UiThemeColours::accent());
            g.fillPath (donutPath);
        }

        for (int i = 0; i < circles.size(); ++i)
        {
            // Do not draw circle 0 (ring), circle 1 (re-trigger - drawn over children),
            // circle 3 (OnOff), circle 6 (OnOff), circle 9 (OnOff)
            // and do not draw circle 5 (index 4) because it is replaced by the shuffle selector.
            if (i == 0 || i == 1 || i == 2 || i == 4 || i == 5 || i == 8) continue;
            const auto& c = circles.getReference (i);
            const float rr = c.r;
            const float f = juce::jlimit (0.0f, 1.0f, flashes[i]);
            juce::Colour fillCol = UiThemeColours::accent().interpolatedWith (UiThemeColours::cyan(), f).withAlpha (1.0f);
            g.setColour (fillCol);
            // If this is circle index 1, scale it by 1.2 * flashes amount so clicking causes a scale effect
            float scale = 1.0f;
            if (i == 1) scale = 1.0f + 0.2f * f; // scale between 1.0 and 1.2
            const float rrScaled = rr * scale;
            g.fillEllipse (c.x - rrScaled, c.y - rrScaled, rrScaled*2.0f, rrScaled*2.0f);
            // g.setColour (juce::Colours::white.withAlpha (0.3f));
            // g.drawEllipse (c.x - rr, c.y - rr, rr*2.0f, rr*2.0f, 1.0f);
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

        // Draw shuffle selector. When there are shuffle positions, draw a smooth curve
        // through their centres. In linearShuffleMode the first shuffle id becomes a
        // draggable continuous fader that moves along that curve from id1 -> id7.
        if (shufflePositions.size() > 0)
        {
            // build points array
            juce::Array<juce::Point<float>> pts;
            for (int i = 0; i < shufflePositions.size(); ++i)
                pts.add ({ shufflePositions.getReference(i).x, shufflePositions.getReference(i).y });

            // (visual curve intentionally removed per request)

            if (linearShuffleMode && shufflePositions.size() > 1)
            {
                // continuous draggable indicator: position and radius interpolate between id1 and id7
                const auto posPt = getPointAlongShufflePolyline (linearShufflePos);
                const float Rstart = shufflePositions.getReference (0).r;
                const float Rend = shufflePositions.getReference (shufflePositions.size() - 1).r;
                const float R = Rstart + (Rend - Rstart) * linearShufflePos;
                const float innerR = R * 0.6f;

                // accent outer
                g.setColour (UiThemeColours::accent());
                g.fillEllipse (posPt.x - R, posPt.y - R, R*2.0f, R*2.0f);

                // base inner
                g.setColour (UiThemeColours::base());
                g.fillEllipse (posPt.x - innerR, posPt.y - innerR, innerR*2.0f, innerR*2.0f);

                // show a numeric value 50..75 mapped from linearShufflePos (no percent sign)
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

                // accent outer
                g.setColour (UiThemeColours::accent());
                g.fillEllipse (pos.x - R, pos.y - R, R*2.0f, R*2.0f);

                // base inner
                g.setColour (UiThemeColours::base());
                g.fillEllipse (pos.x - innerR, pos.y - innerR, innerR*2.0f, innerR*2.0f);

                // cyan id in center, font size scales with id: 1->11, 2->12, ..., 7->17
                static const float sizes[7] = { 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f, 17.0f };
                const float fontSize = (pos.id >= 1 && pos.id <= 7) ? sizes[pos.id - 1] : 12.0f;
                g.setColour (UiThemeColours::cyan());
                g.setFont (juce::Font (juce::FontOptions ("Arial", fontSize, juce::Font::bold)));
                // Apply per-id offsets and a default +1px X nudge to compensate for rounding/centering artifacts
                float offX = 0.0f, offY = 0.0f;
                const int si = selectedShuffle - 1;
                if (si >= 0 && si < shuffleTextOffsets.size()) { offX = shuffleTextOffsets.getReference(si).dx; offY = shuffleTextOffsets.getReference(si).dy; }
                g.drawFittedText (juce::String (pos.id), (int)(pos.x - innerR + offX), (int)(pos.y - innerR + offY), (int)(innerR*2), (int)(innerR*2), juce::Justification::centred, 1);
            }
        }

        // (small inset button is drawn on the top-most overlay so it visually sits
        // above the rotary needle; see paintOverChildren())

        // The re-trigger inset and its step number are rendered by a top-most overlay child component

        g.setColour (UiThemeColours::cyan().withAlpha (0.8f));
        g.setFont (juce::Font (juce::FontOptions ("Arial", 14.0f, juce::Font::bold)));
        g.drawFittedText ("Layout Playground (300 x 240)", 8, 6, getWidth() - 16, 20, juce::Justification::centred, 1);

        juce::String label;
        if (ringHoverSegment >= 0)
            label = "Re-sync at step " + juce::String (ringHoverSegment + 1);
        else if (hoverIndex >= 0 && hoverIndex < hoverTexts.size())
            label = juce::String (hoverIndex + 1) + ": " + hoverTexts[hoverIndex];

        // If the small inset button is hovered, add the hint text so it appears in
        // the same bottom-label area (hoverTexts index 3 is "Start offset step").
        if (clickToPulseHover)
        {
            if (label.isNotEmpty()) label += " — ";
            label += "Click - Pulse";
        }

        if (label.isNotEmpty())
        {
            auto area = juce::Rectangle<int> (8, getHeight() - 24, getWidth() - 16, 18);
            g.setColour (UiThemeColours::cyan());
            g.setFont (juce::Font (juce::FontOptions ("Arial", 13.0f, juce::Font::bold)));
            g.drawFittedText (label, area, juce::Justification::centred, 1);
        }
    }

    // Paint the re-trigger inset on top of all child components so it is never visually overlapped.
    void paintOverChildren (juce::Graphics& g) override
    {
        if (circles.size() <= 1) return;
        const auto& c1 = circles.getReference (1);
        // Draw the accent outer circle (matching how other circles are drawn) so the accent
        // colored circle for index 1 remains visible and on top of the ring and buttons.
        const float outerR = c1.r;
        const float fOuter = juce::jlimit (0.0f, 1.0f, flashes[1]);
        const float outerScale = 1.0f + 0.2f * fOuter;
        const float outerRScaled = outerR * outerScale;
        juce::Colour fillCol = UiThemeColours::accent().interpolatedWith (UiThemeColours::cyan(), fOuter).withAlpha (1.0f);
        g.setColour (fillCol);
        g.fillEllipse (c1.x - outerRScaled, c1.y - outerRScaled, outerRScaled*2.0f, outerRScaled*2.0f);

        // Draw the inset base-colour circle and the step number on top of the outer accent circle.
        const float insetR = 22.0f;
        const float f1 = fOuter; // reuse same flash for inset scaling
        const float insetScale = 1.0f + 0.2f * f1;
        const float insetRScaled = insetR * insetScale;

        g.setColour (UiThemeColours::base());
        g.fillEllipse (c1.x - insetRScaled, c1.y - insetRScaled, insetRScaled*2.0f, insetRScaled*2.0f);

        int stepNum = -1;
        if (ring) stepNum = ring->getCurrentStepLogical();
        juce::String stepText = (stepNum > 0) ? juce::String (stepNum) : "-";
        g.setColour (UiThemeColours::cyan());
        g.setFont (juce::Font (juce::FontOptions ("Arial", 30.0f, juce::Font::bold)));
        g.drawFittedText (stepText, (int)(c1.x - insetRScaled), (int)(c1.y - insetRScaled), (int)(insetRScaled*2.0f), (int)(insetRScaled*2.0f), juce::Justification::centred, 1);

        // Draw the small inset switch button centered inside circle index 3 so it
        // visually overlays the rotary needle. This is drawn here (after the
        // re-trigger inset) so it remains on top of the main paint() contents.
        if (circles.size() > 7)
        {
            const auto& c3 = circles.getReference (7);
            const float smallR = 8.0f;
            juce::Colour bcol = clickToPulseOn ? UiThemeColours::cyan() : UiThemeColours::accent();
            g.setColour (bcol);
            g.fillEllipse (c3.x - smallR, c3.y - smallR, smallR*2.0f, smallR*2.0f);
            if (clickToPulseHover)
            {
                g.setColour (UiThemeColours::cyan().withAlpha (0.25f));
                g.drawEllipse (c3.x - smallR - 2.0f, c3.y - smallR - 2.0f, (smallR + 2.0f)*2.0f, (smallR + 2.0f)*2.0f, 2.0f);
            }
        }
        // Draw a small inset for circle index 6 showing the currently selected numeric value
        if (circles.size() > 6)
        {
            const auto& c6 = circles.getReference (6);
            const float insetR6 = 17.0f; // requested base inset size
            g.setColour (UiThemeColours::base());
            g.fillEllipse (c6.x - insetR6, c6.y - insetR6, insetR6*2.0f, insetR6*2.0f);
            // numeric value inside inset (cyan)
            g.setColour (UiThemeColours::cyan());
            g.setFont (juce::Font (juce::FontOptions ("Arial", 25.0f, juce::Font::bold)));
            juce::String mainTxt = juce::String (mainCircle6Value);
            g.drawFittedText (mainTxt, (int)(c6.x - insetR6), (int)(c6.y - insetR6), (int)(insetR6*2.0f), (int)(insetR6*2.0f), juce::Justification::centred, 1);
        }
        // If circle index 6 has been expanded, draw its 6 popped-out children here
        // so they appear above all other elements. They are arranged evenly around
        // the base circle at a fixed distance.
        if ((expanded6 || extra6Animating) && circles.size() > 6)
        {
            const auto& base = circles.getReference (6);
            const float finalDist = base.r + extra6R - 6.0f;
            // compute start/end angles in radians; support end < start by wrapping
            const float startAngRad = juce::degreesToRadians (extra6StartAngleDeg);
            float spanRad = juce::degreesToRadians (extra6EndAngleDeg - extra6StartAngleDeg);
            if (spanRad <= 0.0f) spanRad += juce::MathConstants<float>::twoPi;
            const bool fullCircle = std::fabs (spanRad - juce::MathConstants<float>::twoPi) < 0.001f;
            const float denom = fullCircle ? (float) extra6Count : (extra6Count > 1 ? (float)(extra6Count - 1) : 1.0f);

            // Draw non-hovered children first so hovered one can be drawn last and appear on top.
            for (int i = 0; i < extra6Count; ++i)
            {
                if (i == hoverExtra6) continue;
                const float prog = juce::jlimit (0.0f, 1.0f, (i < extra6Progress.size()) ? extra6Progress.getReference(i) : 0.0f);
                if (prog <= 0.001f) continue; // skip fully hidden
                const float ang = startAngRad + ( (float)i / denom ) * spanRad;
                const float dist = finalDist * prog;
                const float px = base.x + std::cos (ang) * dist;
                const float py = base.y + std::sin (ang) * dist;
                const float scale = 0.7f + 0.3f * prog;
                float r = extra6R * scale;
                const float hoverMult = (i < extra6HoverScale.size()) ? extra6HoverScale.getReference(i) : 1.0f;
                r *= hoverMult;
                juce::Colour col = UiThemeColours::accent();
                g.setColour (col);
                g.fillEllipse (px - r, py - r, r*2.0f, r*2.0f);
                // draw label text centered in the small circle
                if (i < extra6Labels.size())
                {
                    const float fontSize = juce::jlimit (12.0f, 24.0f, r * 0.9f);
                    g.setColour (UiThemeColours::base());
                    g.setFont (juce::Font (juce::FontOptions ("Arial", fontSize, juce::Font::bold)));
                    g.drawFittedText (extra6Labels[i], (int)(px - r), (int)(py - r), (int)(r*2.0f), (int)(r*2.0f), juce::Justification::centred, 1);
                }
            }

            // Draw hovered child last
            if (hoverExtra6 >= 0 && hoverExtra6 < extra6Count)
            {
                const int i = hoverExtra6;
                const float prog = juce::jlimit (0.0f, 1.0f, (i < extra6Progress.size()) ? extra6Progress.getReference(i) : 0.0f);
                    if (prog > 0.001f)
                {
                    const float ang = startAngRad + ( (float)i / denom ) * spanRad;
                    const float dist = finalDist * prog;
                    const float px = base.x + std::cos (ang) * dist;
                    const float py = base.y + std::sin (ang) * dist;
                    const float scale = 0.7f + 0.3f * prog;
                    float r = extra6R * scale;
                    const float hoverMult = (i < extra6HoverScale.size()) ? extra6HoverScale.getReference(i) : 1.0f;
                    r *= hoverMult;
                    juce::Colour col = UiThemeColours::cyan();
                    g.setColour (col);
                    g.fillEllipse (px - r, py - r, r*2.0f, r*2.0f);
                        // hovered label (drawn last) - use base colour for contrast
                        if (i < extra6Labels.size())
                        {
                            const float fontSize = juce::jlimit (12.0f, 26.0f, r * 0.95f);
                            g.setColour (UiThemeColours::base());
                            g.setFont (juce::Font (juce::FontOptions ("Arial", fontSize, juce::Font::bold)));
                            g.drawFittedText (extra6Labels[i], (int)(px - r), (int)(py - r), (int)(r*2.0f), (int)(r*2.0f), juce::Justification::centred, 1);
                        }
                }
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // If the extra-6 popup is visible, check for clicks on any of its children
        if ((expanded6 || extra6Animating) && circles.size() > 6)
        {
            const auto& base = circles.getReference (6);
            const float finalDist = base.r + extra6R - 6.0f;
            const float startAngRad = juce::degreesToRadians (extra6StartAngleDeg);
            float spanRad = juce::degreesToRadians (extra6EndAngleDeg - extra6StartAngleDeg);
            if (spanRad <= 0.0f) spanRad += juce::MathConstants<float>::twoPi;
            const bool fullCircle = std::fabs (spanRad - juce::MathConstants<float>::twoPi) < 0.001f;
            const float denom = fullCircle ? (float) extra6Count : (extra6Count > 1 ? (float)(extra6Count - 1) : 1.0f);
            for (int i = 0; i < extra6Count; ++i)
            {
                const float ang = startAngRad + ( (float)i / denom ) * spanRad;
                // consider animated progress when testing hit
                const float prog = (i < extra6Progress.size()) ? extra6Progress.getReference(i) : 1.0f;
                // Only allow clicks if the child is sufficiently expanded to be interactable.
                if (prog < 0.6f) continue;
                const float px = base.x + std::cos (ang) * (finalDist * prog);
                const float py = base.y + std::sin (ang) * (finalDist * prog);
                const float dx = e.position.x - px;
                const float dy = e.position.y - py;
                // compute current visual radius (includes animated hover multiplier)
                const float baseScale = 0.7f + 0.3f * prog;
                const float hoverMultHit = (i < extra6HoverScale.size()) ? extra6HoverScale.getReference(i) : 1.0f;
                const float curR = extra6R * baseScale * hoverMultHit;
                if (dx*dx + dy*dy <= curR * curR)
                {
                    // clicked one of the extras -> update main circle value and animate collapse
                    if (i < extra6Values.size()) mainCircle6Value = extra6Values.getReference(i);
                    startCollapse6();
                    // flash the base circle to give feedback
                    if (6 >= 0 && 6 < flashes.size()) flashes.set (6, 1.0f);
                    startFadeTimer();
                    repaint();
                    return;
                }
            }
        }

        // First, check if the click is on the shuffle selector area
        if (shufflePositions.size() > 0)
        {
            // If in linear mode, allow starting a drag by clicking the current handle
            // position (so user can grab the slider where it currently sits).
                if (linearShuffleMode && shufflePositions.size() > 1)
            {
                const auto curPt = getPointAlongShufflePolyline (linearShufflePos);
                const float dxh = e.position.x - curPt.x; const float dyh = e.position.y - curPt.y;
                const float d2h = dxh*dxh + dyh*dyh;
                // handle radius: interpolate between start/end radii for current size
                const float Rstart = shufflePositions.getReference(0).r;
                const float Rend = shufflePositions.getReference(shufflePositions.size() - 1).r;
                const float Rcur = Rstart + (Rend - Rstart) * linearShufflePos;
                const float handleHitR = Rcur * 1.4f;
                if (d2h <= handleHitR * handleHitR)
                {
                    isDraggingShuffle = true;
                    isDraggingLinear = true;
                    linearShufflePos = pointToShufflePolylineT (e.position);
                    linearShuffleAmount = 0.5f + 0.25f * linearShufflePos;
                    repaint();
                    return;
                }
            }
                // small inset button (circle index 3) clickable area - handle here so clicks don't fall through
                if (circles.size() > 7)
                {
                    const auto& c3 = circles.getReference (7);
                    const float smallR = 8.0f;
                    const float dxs = e.position.x - c3.x;
                    const float dys = e.position.y - c3.y;
                    const float d2s = dxs*dxs + dys*dys;
                    if (d2s <= smallR * smallR)
                    {
                        // toggle on mouseDown
                        clickToPulseOn = ! clickToPulseOn;
                        repaint();
                        return;
                    }
                }
            for (int i = 0; i < shufflePositions.size(); ++i)
            {
                const auto& p = shufflePositions.getReference (i);
                const float dx = e.position.x - p.x; const float dy = e.position.y - p.y;
                const float d2 = dx*dx + dy*dy;
                const float hitR = p.r * 1.4f;
                if (d2 <= hitR * hitR)
                {
                    if (linearShuffleMode && shufflePositions.size() > 1)
                    {
                        // In linear mode, only allow starting a continuous drag when clicking
                        // near shuffle id 1 or id 2. This prevents grabbing the indicator at an
                        // arbitrary position when the slider is not at the default (50).
                        if (i == 0 || i == 1)
                        {
                            // start continuous drag along the curve from id1->id7
                            isDraggingShuffle = true;
                            isDraggingLinear = true;
                            linearShufflePos = pointToShufflePolylineT (e.position);
                            linearShuffleAmount = 0.5f + 0.25f * linearShufflePos;
                            repaint();
                            return;
                        }
                        // clicking other positions will just select the discrete id below
                    }
                    selectedShuffle = p.id;
                    isDraggingShuffle = true;
                    repaint();
                    return;
                }
            }
        }

        const int idx = hitTestIndex (e.position);
        if (idx >= 0)
        {
            if (idx == 1)
            {
                // Re-trigger inset: only respond on mouseDown (we are in mouseDown) and
                // request the ring to restart at the next master boundary.
                if (ring) ring->requestRestartAtNextMasterBoundary();
                flashes.set (1, 1.0f);
                startFadeTimer();
                repaint();
            }
            else if (idx == 6)
            {
                // clicking circle index 6 toggles the extra 6 popup: expand if closed,
                // collapse if already open (use animated collapse/expand helpers).
                if (expanded6 && ! extra6Animating)
                {
                    startCollapse6();
                    return;
                }
                if (! expanded6 && ! extra6Animating)
                {
                    startExpand6();
                    return;
                }
                // if an animation is running, ignore the click to avoid conflicts
                return;
            }
            else if (idx == 7)
            {
                // Start-offset rotary control: begin dragging using vertical/horizontal moves.
                isDraggingOffsetRotary = true;
                offsetDragStart = e.position;
                offsetRotaryStartT = offsetRotaryT;
                // also set immediate value from angle click fallback
                offsetRotaryT = angleToRotaryT (e.position);
                repaint();
            }
            else
            {
                flashes.set (idx, 1.0f);
                startFadeTimer();
                repaint();
            }
        }
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
            if (bestId != selectedShuffle) { selectedShuffle = bestId; repaint(); }
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
            repaint();
            return;
        }

        // otherwise default behavior: update hover
        // check hover for small inset button (circle index 3)
        // if (circles.size() > 3)
        // {
        //     const auto& c3 = circles.getReference (3);
        //     const float smallR = 8.0f;
        //     const float dxs = e.position.x - c3.x;
        //     const float dys = e.position.y - c3.y;
        //     const float d2s = dxs*dxs + dys*dys;
        //     const bool nowHover = (d2s <= smallR * smallR);
        //     if (nowHover != clickToPulseHover)
            // distribution angle for the extra-6 popup (degrees). Default creates full circle
            float extra6StartAngleDeg = -90.0f;
            float extra6EndAngleDeg = 270.0f;
        //     {
        //         clickToPulseHover = nowHover;
        //         setMouseCursor (clickToPulseHover ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        //         repaint();
        //     }
        //     if (clickToPulseHover) return; // keep hover handling local
        // }
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
            repaint();
            return;
        }
        // Do not trigger clicks on mouseUp; only mouseDown should perform actions.
    }

    // NOTE: ReTriggerOverlay removed — hit-testing and click handling for the inset
    // are centralized in the parent component (see hitTestIndex() and mouseDown()).

    void mouseMove (const juce::MouseEvent& e) override
    {
        // If extra6 popup is visible, prefer hovering its children
        if ((expanded6 || extra6Animating) && circles.size() > 6)
        {
            const auto& base = circles.getReference (6);
            const float finalDist = base.r + extra6R - 6.0f;
            const float startAngRad = juce::degreesToRadians (extra6StartAngleDeg);
            float spanRad = juce::degreesToRadians (extra6EndAngleDeg - extra6StartAngleDeg);
            if (spanRad <= 0.0f) spanRad += juce::MathConstants<float>::twoPi;
            const bool fullCircle = std::fabs (spanRad - juce::MathConstants<float>::twoPi) < 0.001f;
            const float denom = fullCircle ? (float) extra6Count : (extra6Count > 1 ? (float)(extra6Count - 1) : 1.0f);
            int newHoverExtra = -1;
            for (int i = 0; i < extra6Count; ++i)
            {
                const float ang = startAngRad + ( (float)i / denom ) * spanRad;
                const float prog = (i < extra6Progress.size()) ? extra6Progress.getReference(i) : 1.0f;
                // Only allow hover detection after the child is mostly expanded to avoid
                // transient hover as it animates into position.
                if (prog < 0.6f) continue;
                const float px = base.x + std::cos (ang) * (finalDist * prog);
                const float py = base.y + std::sin (ang) * (finalDist * prog);
                const float dx = e.position.x - px;
                const float dy = e.position.y - py;
                const float baseScale = 0.7f + 0.3f * prog;
                const float hoverMultHit = (i < extra6HoverScale.size()) ? extra6HoverScale.getReference(i) : 1.0f;
                const float curR = extra6R * baseScale * hoverMultHit;
                if (dx*dx + dy*dy <= curR * curR) { newHoverExtra = i; break; }
            }
            if (newHoverExtra != hoverExtra6)
            {
                hoverExtra6 = newHoverExtra;
                // update hover targets so the hovered child scales up to 1.33 and others return to 1.0
                for (int j = 0; j < extra6Count; ++j)
                    extra6HoverTarget.set (j, (j == hoverExtra6) ? 1.33f : 1.0f);
                // take a small immediate step towards the new target so the first frame isn't a hard jump
                for (int j = 0; j < extra6Count; ++j)
                {
                    const float cur = extra6HoverScale.getReference(j);
                    const float tgt = extra6HoverTarget.getReference(j);
                    extra6HoverScale.set (j, cur + (tgt - cur) * 0.25f);
                }
                setMouseCursor (hoverExtra6 >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
                startFadeTimer(); // ensure timer runs to animate hover scaling
                repaint();
            }
            return;
        }

        const int newH = hitTestIndex (e.position);
        if (newH != hoverIndex) { hoverIndex = newH; setMouseCursor (hoverIndex >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor); repaint(); }
    }

    void resized() override
    {
        // keep ring behind after any layout changes
        if (ring) ring->toBack();
    }

    void mouseExit (const juce::MouseEvent&) override { if (hoverIndex != -1) { hoverIndex = -1; setMouseCursor (juce::MouseCursor::NormalCursor); repaint(); } }

    // --- Public API to configure the extra-6 popup -------------------------------------------------
    // Set explicit numeric values for the popup children (labels are derived from values).
    void setExtra6Values (const juce::Array<int>& vals)
    {
        extra6Values.clear(); extra6Labels.clear();
        for (int i = 0; i < vals.size(); ++i)
        {
            const int v = vals.getReference(i);
            extra6Values.add (v);
            extra6Labels.add (juce::String (v));
        }
        extra6Count = juce::jmax (1, extra6Values.size());

        // reset progress and hover arrays to match new count
        extra6Progress.clear(); extra6HoverScale.clear(); extra6HoverTarget.clear();
        for (int i = 0; i < extra6Count; ++i) { extra6Progress.add (0.0f); extra6HoverScale.add (1.0f); extra6HoverTarget.add (1.0f); }

        // default the main circle value to the first element if present
        if (extra6Values.size() > 0) mainCircle6Value = extra6Values.getReference(0);
        repaint();
    }

    // Convenience: choose from a set of common clock-division values between min and max (inclusive)
    void setExtra6Range (int minDiv, int maxDiv)
    {
        juce::Array<int> standard;
        standard.addArray ({ 3, 4, 6, 8, 12, 16, 24 });
        juce::Array<int> chosen;
        for (int i = 0; i < standard.size(); ++i)
        {
            const int v = standard.getReference(i);
            if (v >= minDiv && v <= maxDiv) chosen.add (v);
        }
        if (chosen.size() == 0) chosen.add (juce::jlimit (minDiv, maxDiv, minDiv));
        setExtra6Values (chosen);
    }

    juce::Array<int> getExtra6Values() const { return extra6Values; }
    juce::StringArray getExtra6Labels() const { return extra6Labels; }

    // Set the numeric value displayed in the circle-6 inset directly
    void setMainCircle6Value (int v) { mainCircle6Value = v; repaint(); }

private:
    struct Circle { float x, y, r; juce::Colour colour; };
    juce::Array<Circle> circles;
    juce::Array<float> flashes;
    juce::StringArray hoverTexts;

    // Shuffle selector data
    struct ShufflePos { int id; float x, y, r; };
    juce::Array<ShufflePos> shufflePositions;
    int selectedShuffle = 1; // 1..7
    bool isDraggingShuffle = false;

    // Per-shuffle text offsets to fine-tune x/y for drawn numeric ids (defaults to 0)
    struct ShuffleTextOffset { float dx, dy; };
    juce::Array<ShuffleTextOffset> shuffleTextOffsets;

    std::unique_ptr<Ring16Component> ring;
    juce::OwnedArray<OnOffButton> onOffButtons;
    int hoverIndex = -1;
    int ringHoverSegment = -1;
    int ringSelectedSegment = -1;
    // Linear shuffle mode state: when enabled the first shuffle id becomes a continuous
    // draggable fader along a curve from id 1 -> id 7. linearShufflePos ranges 0..1.
    bool linearShuffleMode = false;
    bool isDraggingLinear = false;
    float linearShufflePos = 0.0f; // 0 => id1, 1 => id7
    float linearShuffleAmount = 0.5f; // maps linearShufflePos -> 0.5..0.75

    // Start-offset rotary state (circle index 3)
    // rotary state: continuous t in [0..1] and discrete step count
    float offsetRotaryT = 0.0f; // continuous 0..1
    static constexpr int offsetRotarySteps = 5; // five discrete positions
    bool isDraggingOffsetRotary = false;
    juce::Point<float> offsetDragStart { 0.0f, 0.0f };
    float offsetRotaryStartT = 0.0f;
    // small inset button (inside circle index 3)
    bool clickToPulseOn = false;
    bool clickToPulseHover = false;
    // extra popup circles for circle index 6
    bool expanded6 = false;
    int hoverExtra6 = -1;
    // make the extra-child count configurable at runtime (default 4)
    int extra6Count = 4;
    static constexpr float extra6R = 12.0f;
    juce::Array<float> extra6Progress;
    // per-child hover scale (animated) and target values. Default 1.0 (no extra scale).
    juce::Array<float> extra6HoverScale;
    juce::Array<float> extra6HoverTarget;
    // labels and numeric values for the extra popup children (e.g. 4,8,16,32)
    juce::StringArray extra6Labels;
    juce::Array<int> extra6Values;
    // currently selected / displayed numeric value for circle index 6
    int mainCircle6Value = 16;
    // Arc limits (degrees) for arranging the extra popup children around the base circle.
    // Default values create a full-circle distribution: start=-90, end=270 (wraps to 360deg)
    float extra6StartAngleDeg = 100.0f;
    float extra6EndAngleDeg = -120.0f;
    // animation timing
    // animation timing
    double extra6AnimStartMs = 0.0;
    float extra6AnimDurationMs = 166.0f; // per-circle duration in ms
    float extra6StaggerMs = 25.0f; // delay between each circle's start
    bool extra6Animating = false;
    bool extra6ExpandingTarget = false;

    static inline float easeOutCubic (float t)
    {
        t = juce::jlimit (0.0f, 1.0f, t);
        const float u = 1.0f - t;
        return 1.0f - u * u * u; // 1 - (1-t)^3
    }

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
            // Special-case circle index 1 (re-trigger inset) to use the inset radius
            // (smaller hit area) so only clicks on the inset trigger the re-trigger.
            if (i == 1)
            {
                const float insetR = 22.0f;
                const float f = juce::jlimit (0.0f, 1.0f, flashes[1]);
                const float insetScale = 1.0f + 0.2f * f;
                const float r = insetR * insetScale;
                if (d2 <= r * r && d2 < bestd) { best = i; bestd = d2; }
            }
            else
            {
                if (d2 <= c.r * c.r && d2 < bestd) { best = i; bestd = d2; }
            }
        }
        return best;
    }

    void startFadeTimer()
    {
        if (! isTimerRunning) { isTimerRunning = true; lastTickMs = juce::Time::getMillisecondCounterHiRes(); startTimerHz (60); }
    }

    void startExpand6()
    {
        extra6AnimStartMs = juce::Time::getMillisecondCounterHiRes();
        extra6Animating = true;
        extra6ExpandingTarget = true;
        expanded6 = true; // make visible immediately so we can animate in
        // reset progress for each
        for (int i = 0; i < extra6Count; ++i) extra6Progress.set (i, 0.0f);
        startFadeTimer();
    }

    void startCollapse6()
    {
        extra6AnimStartMs = juce::Time::getMillisecondCounterHiRes();
        extra6Animating = true;
        extra6ExpandingTarget = false;
        // ensure progress values exist
        for (int i = 0; i < extra6Count; ++i) extra6Progress.set (i, (i < extra6Progress.size()) ? extra6Progress.getReference(i) : 1.0f);
        startFadeTimer();
    }

    void timerCallback() override
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        const double dtMs = now - lastTickMs; lastTickMs = now;

        const double fadeMs = 300.0;
        bool any = false;
        for (int i = 0; i < flashes.size(); ++i)
        {
            float v = flashes[i];
            if (v > 0.0f)
            {
                v *= 0.9f; // exponential smoothing for a softer fade
                if (v < 0.001f) v = 0.0f;
                flashes.set (i, v);
                if (v > 0.0f) any = true;
            }
        }

        // step extra6 animation if active
        if (extra6Animating)
        {
            bool allDone = true;
            for (int i = 0; i < extra6Count; ++i)
            {
                const double delayMs = (double)i * (double)extra6StaggerMs;
                const double local = (now - extra6AnimStartMs - delayMs) / (double)extra6AnimDurationMs;
                const float t = (float) juce::jlimit (0.0, 1.0, local);
                float p = 0.0f;
                if (extra6ExpandingTarget)
                    p = easeOutCubic (t); // ease-out: slows down toward the end
                else
                    p = 1.0f - easeOutCubic (t); // collapse: fast at start, slows toward end
                extra6Progress.set (i, p);
                const bool done = extra6ExpandingTarget ? (p >= 0.999f) : (p <= 0.001f);
                if (! done) allDone = false;
            }
            any = true; // keep timer running while animating
            if (allDone)
            {
                extra6Animating = false;
                if (! extra6ExpandingTarget)
                {
                    // fully collapsed
                    expanded6 = false;
                    hoverExtra6 = -1;
                }
            }
        }

        // animate hover scale values towards their targets using frame delta so smoothing feels natural
        if (extra6HoverScale.size() == extra6HoverTarget.size())
        {
            // Use exponential smoothing based on frame delta so interpolation is framerate-independent
            // alpha = 1 - exp(-dt / tau). Tau controls responsiveness (ms).
            const double tau = 80.0; // ms time constant; lower -> faster
            const float alpha = (float) (1.0 - std::exp (- (double) dtMs / tau));
            for (int i = 0; i < extra6HoverScale.size(); ++i)
            {
                const float cur = extra6HoverScale.getReference(i);
                const float tgt = extra6HoverTarget.getReference(i);
                const float next = cur + (tgt - cur) * alpha;
                if (std::fabs (next - cur) > 0.0005f) { extra6HoverScale.set (i, next); any = true; }
                else if (cur != tgt) { extra6HoverScale.set (i, tgt); }
            }
        }

        if (! any) { stopTimer(); isTimerRunning = false; }
        repaint();
    }

    bool isTimerRunning = false;
    double lastTickMs = 0.0;
};

class PlaygroundWindow : public juce::DocumentWindow
{
public:
    PlaygroundWindow() : juce::DocumentWindow ("Layout Playground", juce::Colours::black, juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setResizable (false, false);
        setContentOwned (new PlaygroundComponent(), true);
        centreWithSize (300, 240);
        setVisible (true);
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class PlaygroundApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Layout Playground"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void initialise (const juce::String&) override { window.reset (new PlaygroundWindow()); }
    void shutdown() override { window = nullptr; }
    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String&) override {}
private:
    std::unique_ptr<PlaygroundWindow> window;
};

START_JUCE_APPLICATION (PlaygroundApplication)