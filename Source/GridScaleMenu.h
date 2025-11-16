#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Animated radial grid scale selector button (values: 1/32,1/16,1/8,1/4)
// - Base: 60x60 accent circle with inner base fill
// - Cyan text shows current grid value
// - On click: shows 4 small option circles arranged on an arc from 4 o'clock to 1 o'clock
// - Auto-closes when pointer leaves outer ring bounds
// - onGridChanged callback invoked passing choice index 0..3 (0=>1/32,1=>1/16,2=>1/8,3=>1/4)
class GridScaleMenu : public juce::Component, private juce::Timer
{
public:
    GridScaleMenu()
    {
        setInterceptsMouseClicks(true, true);
        startTimerHz(60);
    }

    void setColours(juce::Colour accent, juce::Colour base, juce::Colour cyan)
    {
        kAccent = accent; kBase = base; kCyan = cyan; repaint();
    }

    void setIndex(int idx)
    {
        idx = juce::jlimit(0, 3, idx);
        if (currentIndex != idx)
        {
            currentIndex = idx;
            repaint();
        }
    }
    int getIndex() const { return currentIndex; }

    std::function<void(int)> onGridChanged; // passes index 0..3

    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto center = r.getCentre();
        const float outerD = 53.0f;
        const float innerReduce = 9.0f;
        juce::Rectangle<float> outer(center.x - outerD * 0.5f, center.y - outerD * 0.5f, outerD, outerD);
        juce::Rectangle<float> inner = outer.reduced(innerReduce);

        g.setColour(kAccent); g.fillEllipse(outer);
        g.setColour(kBase);   g.fillEllipse(inner);

        if (openAmount > 0.01f)
            drawOptions(g, center, outerD);

        // Display current value text
        static const char* labels[4] = { "32", "16", "8", "4" };
        g.setColour(kCyan);
        g.setFont(juce::Font(juce::FontOptions("Arial", 22.0f, juce::Font::bold)));
        g.drawFittedText(labels[currentIndex], outer.toNearestInt(), juce::Justification::centred, 1);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        auto r = getLocalBounds().toFloat();
        auto c = r.getCentre();
        const float outerR = 30.0f;
        if (c.getDistanceFrom(e.position) <= outerR)
        {
            opening = !menuOpen;
            closing = menuOpen;
            menuOpen = !menuOpen;
        }
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (! menuOpen) return;
        int hit = hitTestOption(e.position);
        if (hit >= 0 && hit < 4)
        {
            currentIndex = hit;
            if (onGridChanged) onGridChanged(currentIndex);
            opening = false; closing = true; menuOpen = false; // collapse
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        if (! menuOpen || openAmount <= 0.01f) return;
        // Auto-close if pointer leaves outer bounds
        auto r = getLocalBounds().toFloat();
        auto center = r.getCentre();
        const float baseD = 60.0f;
        const float itemD = 20.0f;
        const float itemR = itemD * 0.5f;
        const float s0 = 1.5f; // keep in sync with drawOptions
        const float maxScaleR = itemR * s0;
        const float minRadius = baseD * 0.5f - 2.0f;
        const float tighten = 5.0f; // reduced to allow wider spread
        const float maxRadius = juce::jmin<float>(getWidth(), getHeight()) * 0.5f - maxScaleR  - tighten;
        const float radius = juce::jlimit(minRadius, juce::jmax(minRadius, maxRadius), minRadius + (maxRadius - minRadius) * openAmount);
        const float outerBound = radius + maxScaleR + 12.0f; // slightly larger margin to avoid accidental collapse at extremes
        if (center.getDistanceFrom(e.position) > outerBound)
        {
            opening = false; closing = true; menuOpen = false; hoverIndex = -1; repaint();
            return;
        }

        int idx = hitTestOptionHover(e.position);
        if (idx != hoverIndex)
        {
            hoverIndex = idx;
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        if (menuOpen)
        {
            opening = false; closing = true; menuOpen = false; hoverIndex = -1; repaint();
            return;
        }
        if (hoverIndex != -1) { hoverIndex = -1; repaint(); }
    }

    void resized() override {}

private:
    bool menuOpen { false };
    bool opening { false };
    bool closing { false };
    float openAmount { 0.0f };
    int currentIndex { 1 }; // default to 1/16
    int hoverIndex { -1 }; // 0..3 when hovering

    juce::Colour kAccent { juce::Colour::fromRGB(0xFF, 0x4E, 0x5B) };
    juce::Colour kBase   { juce::Colour::fromRGB(0x26, 0x26, 0x26) };
    juce::Colour kCyan   { juce::Colour::fromRGB(0x00, 0xD7, 0xFF) };

    // Centralised arc degrees for option placement (degrees)
    static constexpr float kStartDeg = 110.0f; // lower bound of arc
    static constexpr float kEndDeg   = 230.0f; // upper bound of arc

    void timerCallback() override
    {
        const float speed = 0.22f;
        if (opening && openAmount < 1.0f)
        {
            openAmount = juce::jmin(1.0f, openAmount + speed);
            repaint();
        }
        else if (closing && openAmount > 0.0f)
        {
            openAmount = juce::jmax(0.0f, openAmount - speed);
            repaint();
        }
        if (openAmount <= 0.0f) closing = false;
        if (openAmount >= 1.0f) opening = false;
    }

    void drawOptions(juce::Graphics& g, juce::Point<float> center, float baseD)
    {
        const int count = 4;
        const float itemD = 20.0f;
        const float itemR = itemD * 0.5f;
        const float s0 = 1.5f, s1 = 1.1f; // hovered, adjacent, normal
        const float maxScaleR = itemR * s0;
        const float minRadius = baseD * 0.5f - 2.0f;
        const float tighten = 5.0f;
        const float maxRadius = juce::jmin<float>(getWidth(), getHeight()) * 0.5f - maxScaleR  - tighten;
        const float radius = juce::jlimit(minRadius, juce::jmax(minRadius, maxRadius), minRadius + (maxRadius - minRadius) * openAmount);

        // Arc option angles (3x wider spread): 60° to 300° covering most of circle, midpoint at 180°
        auto angleForIndex = [](int i)
        {
            const float stepDeg  = (kEndDeg - kStartDeg) / 3.0f;
            const float deg = kStartDeg + stepDeg * (float) i;
            return juce::degreesToRadians(deg);
        };
        static const char* labels[count] = { "32", "16", "8", "4" };

        auto scaleFor = [this, s0, s1](int i) -> float
        {
            if (hoverIndex < 0) return 1.0f;
            if (i == hoverIndex) return s0;
            // Linear adjacency only (no wrap-around): neighbors are |i - hoverIndex| == 1
            if (std::abs(i - hoverIndex) == 1) return s1;
            return 1.0f;
        };

        for (int i = 0; i < count; ++i)
        {
            if (i == hoverIndex) continue; // draw hovered last
            const float ang = angleForIndex(i);
            const float cx = center.x + radius * std::cos(ang);
            const float cy = center.y + radius * std::sin(ang);
            const float sc = scaleFor(i);
            const float d = itemD * sc;
            const float r = d * 0.5f;
            juce::Rectangle<float> ring(cx - r, cy - r, d, d);
            g.setColour(juce::Colours::black.withAlpha(0.25f));
            g.fillEllipse(ring.translated(2.0f, 2.0f));
            g.setColour(kAccent); g.fillEllipse(ring);
            g.setColour(kBase);
            g.setFont(juce::Font(juce::FontOptions("Arial", 15.0f * juce::jlimit(1.0f, s0, sc), juce::Font::bold)));
            g.drawFittedText(labels[i], ring.toNearestInt(), juce::Justification::centred, 1);
        }
        if (hoverIndex >= 0 && hoverIndex < count)
        {
            const int i = hoverIndex;
            const float ang = angleForIndex(i);
            const float cx = center.x + radius * std::cos(ang);
            const float cy = center.y + radius * std::sin(ang);
            const float scale = s0;
            const float d = itemD * scale;
            const float r = d * 0.5f;
            juce::Rectangle<float> ring(cx - r, cy - r, d, d);
            g.setColour(juce::Colours::black.withAlpha(0.35f));
            g.fillEllipse(ring.translated(2.0f, 2.0f));
            g.setColour(kCyan); g.fillEllipse(ring);
            g.setColour(kBase);
            g.setFont(juce::Font(juce::FontOptions("Arial", 18.0f * scale / s0, juce::Font::bold)));
            g.drawFittedText(labels[i], ring.toNearestInt(), juce::Justification::centred, 1);
        }
    }

    int hitTestOption(juce::Point<float> pos) const
    {
        if (openAmount <= 0.01f) return -1;
        const int count = 4;
        const float itemD = 20.0f;
        const float itemR = itemD * 0.5f;
        auto r = getLocalBounds().toFloat();
        auto center = r.getCentre();
        const float baseD = 60.0f;
        const float s0 = 1.5f; // unify with drawOptions
        const float maxScaleR = itemR * s0;
        const float minRadius = baseD * 0.5f - 2.0f;
        const float tighten = 5.0f;
        const float maxRadius = juce::jmin<float>(getWidth(), getHeight()) * 0.5f - maxScaleR  - tighten;
        const float radius = juce::jlimit(minRadius, juce::jmax(minRadius, maxRadius), minRadius + (maxRadius - minRadius) * openAmount);
        auto angleForIndex = [](int i)
        {
            const float stepDeg  = (kEndDeg - kStartDeg) / 3.0f;
            const float deg = kStartDeg + stepDeg * (float) i;
            return juce::degreesToRadians(deg);
        };

        for (int i = 0; i < count; ++i)
        {
            const float ang = angleForIndex(i);
            const float cx = center.x + radius * std::cos(ang);
            const float cy = center.y + radius * std::sin(ang);
            const float dx = pos.x - cx;
            const float dy = pos.y - cy;
            // Slightly relaxed hit circle to improve edge selection
            const float rr = itemR + 2.0f;
            if ((dx*dx + dy*dy) <= (rr * rr)) return i;
        }
        return -1;
    }

    int hitTestOptionHover(juce::Point<float> pos) const
    {
        if (openAmount <= 0.01f) return -1;
        const int count = 4;
        const float itemD = 20.0f;
        const float itemR = itemD * 0.5f;
        const float extra = 10.0f; // expand hover radius for stability, especially at index 0/3
        auto r = getLocalBounds().toFloat();
        auto center = r.getCentre();
        const float baseD = 60.0f;
        const float s0 = 1.5f; // unify with drawOptions
        const float maxScaleR = itemR * s0;
        const float minRadius = baseD * 0.5f - 2.0f;
        const float tighten = 5.0f;
        const float maxRadius = juce::jmin<float>(getWidth(), getHeight()) * 0.5f - maxScaleR  - tighten;
        const float radius = juce::jlimit(minRadius, juce::jmax(minRadius, maxRadius), minRadius + (maxRadius - minRadius) * openAmount);
        auto angleForIndex = [](int i)
        {
            const float stepDeg  = (kEndDeg - kStartDeg) / 3.0f;
            const float deg = kStartDeg + stepDeg * (float) i;
            return juce::degreesToRadians(deg);
        };

        int bestIndex = -1;
        float bestDist2 = std::numeric_limits<float>::max();
        const float rr = itemR + extra;
        const float rr2 = rr * rr;
        for (int i = 0; i < count; ++i)
        {
            const float ang = angleForIndex(i);
            const float cx = center.x + radius * std::cos(ang);
            const float cy = center.y + radius * std::sin(ang);
            const float dx = pos.x - cx;
            const float dy = pos.y - cy;
            const float d2 = dx*dx + dy*dy;
            if (d2 <= rr2 && d2 < bestDist2) { bestDist2 = d2; bestIndex = i; }
        }
        return bestIndex;
    }
};
