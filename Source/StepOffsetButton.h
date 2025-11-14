#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Animated radial step selector button (1..16)
// - Base: 60x60 kAccent circle with inner (reduced by 10) kBase
// - Cyan number centered showing selected step
// - On click: shows 16 small ring options around, with simple expand/collapse animation
// - onStepChanged callback invoked when a step is selected
class AnimatedStepOffsetButton : public juce::Component, private juce::Timer
{
public:
    AnimatedStepOffsetButton()
    {
        setInterceptsMouseClicks(true, true);
        startTimerHz(60);
    }

    // Color theme (defaults match PluginEditor)
    void setColours(juce::Colour accent, juce::Colour base, juce::Colour cyan)
    {
        kAccent = accent; kBase = base; kCyan = cyan; repaint();
    }

    void setStep(int step)
    {
        step = juce::jlimit(1, 16, step);
        if (selectedStep != step)
        {
            selectedStep = step;
            repaint();
        }
    }
    int getStep() const { return selectedStep; }

    std::function<void(int)> onStepChanged;

    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto center = r.getCentre();

        // Base button at center: 60x60 outer, inner reduced by 10
        const float outerD = 60.0f;
        const float innerReduce = 10.0f;
        juce::Rectangle<float> outer(center.x - outerD * 0.5f, center.y - outerD * 0.5f, outerD, outerD);
        juce::Rectangle<float> inner = outer.reduced(innerReduce);

        // Base button
        g.setColour(kAccent); g.fillEllipse(outer);
        g.setColour(kBase);   g.fillEllipse(inner);

        // Draw options if open
        if (openAmount > 0.01f)
            drawOptions(g, center, outerD);


        // Number
        g.setColour(kCyan);
        g.setFont(juce::Font(juce::FontOptions("Arial", 22.0f, juce::Font::bold)));
        g.drawFittedText(juce::String(selectedStep), outer.toNearestInt(), juce::Justification::centred, 1);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        // Toggle menu if clicked within base circle
        auto r = getLocalBounds().toFloat();
        auto c = r.getCentre();
        const float outerR = 30.0f; // 60/2
        if (c.getDistanceFrom(e.position) <= outerR)
        {
            opening = !menuOpen;
            closing = menuOpen;
            menuOpen = !menuOpen;
            // Kick animation
            // If opening, go forward; if closing, go backward
            // openAmount remains as is to support quick toggles
        }
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (! menuOpen)
            return;
        const auto clickPos = e.position;
        int hit = hitTestOption(clickPos);
        if (hit >= 1 && hit <= 16)
        {
            selectedStep = hit;
            if (onStepChanged)
                onStepChanged(selectedStep);
            // Collapse
            opening = false; closing = true; menuOpen = false;
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        if (! menuOpen || openAmount <= 0.01f) return;
        const int idx = hitTestOptionHover(e.position);
        if (idx != hoverIndex)
        {
            hoverIndex = idx;
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        if (hoverIndex != -1)
        {
            hoverIndex = -1;
            repaint();
        }
    }

    void resized() override { }

private:
    // Animation state
    bool menuOpen { false };
    bool opening  { false };
    bool closing  { false };
    float openAmount { 0.0f }; // 0..1

    int selectedStep { 1 };
    int hoverIndex   { -1 }; // 1..16 when hovering, else -1

    // Local theme colours (defaults match PluginEditor)
    juce::Colour kAccent { juce::Colour::fromRGB(0xFF, 0x4E, 0x5B) };
    juce::Colour kBase   { juce::Colour::fromRGB(0x26, 0x26, 0x26) };
    juce::Colour kCyan   { juce::Colour::fromRGB(0x00, 0xD7, 0xFF) };

    void timerCallback() override
    {
        const float speed = 0.18f; // animation speed
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
        // Arrange 16 small rings around the button, radius grows by openAmount
        const int count = 16;
        const float itemD = 18.0f; // unified with hit-test; can touch easily
        const float itemR = itemD * 0.5f;
        // Hover scaling ripple factors
        const float s0 = 1.66f, s1 = 1.44f, s2 = 1.22f, s3 = 1.05f;
        const float maxScaleR = itemR * s0; // ensure no clipping when fully open
        const float minRadius = baseD * 0.5f + 2.0f; // bring circles closer together
        const float maxRadius = juce::jmin<float>(getWidth(), getHeight()) * 0.5f - maxScaleR - 3.0f;
        const float radius = juce::jlimit(minRadius, juce::jmax(minRadius, maxRadius), minRadius + (maxRadius - minRadius) * openAmount);

        g.addTransform(juce::AffineTransform());
        const float startAt12 = -juce::MathConstants<float>::halfPi;
        const float step = juce::MathConstants<float>::twoPi / (float) count;
        auto scaleFor = [this, s0, s1, s2, s3](int idx)->float
        {
            if (hoverIndex < 1) return 1.0f;
            // idx is 0-based; hoverIndex is 1-based
            const int a = idx + 1;
            const int b = hoverIndex;
            int d = std::abs(a - b);
            d = juce::jmin(d, 16 - d); // circular distance
            switch (d)
            {
                case 0: return s0;
                case 1: return s1;
                case 2: return s2;
                case 3: return s3;
                default: return 1.0f;
            }
        };

        // Draw non-hovered items first (with ripple scaling)
        for (int i = 0; i < count; ++i)
        {
            if (i + 1 == hoverIndex) continue;
            const float ang = startAt12 + step * (float) i;
            const float cx = center.x + radius * std::cos(ang);
            const float cy = center.y + radius * std::sin(ang);
            const float sc = scaleFor(i);
            const float d = itemD * sc;
            const float r = d * 0.5f;
            juce::Rectangle<float> ring(cx - r, cy - r, d, d);
            g.setColour(kAccent); g.fillEllipse(ring);
            g.setColour(kBase);
            g.setFont(juce::Font(juce::FontOptions("Arial", 15.0f * sc, juce::Font::bold)));
            g.drawFittedText(juce::String(i + 1), ring.toNearestInt(), juce::Justification::centred, 1);
        }
        // Draw hovered last, scaled and on top
        if (hoverIndex >= 1 && hoverIndex <= count)
        {
            const int i = hoverIndex - 1;
            const float ang = startAt12 + step * (float) i;
            const float cx = center.x + radius * std::cos(ang);
            const float cy = center.y + radius * std::sin(ang);
            const float scale = s0;
            const float d = itemD * scale;
            const float r = d * 0.5f;
            juce::Rectangle<float> ring(cx - r, cy - r, d, d);
            // Optional subtle shadow to emphasize front
            g.setColour(juce::Colours::black.withAlpha(0.25f));
            g.fillEllipse(ring.translated(0.0f, 1.5f));
            g.setColour(kCyan); g.fillEllipse(ring);
            g.setColour(kBase);
            g.setFont(juce::Font(juce::FontOptions("Arial", 17.0f * scale / s0, juce::Font::bold)));
            g.drawFittedText(juce::String(i + 1), ring.toNearestInt(), juce::Justification::centred, 1);
        }
    }

    int hitTestOption(juce::Point<float> pos) const
    {
        if (openAmount <= 0.01f) return -1;
        const int count = 16;
        const float itemD = 22.0f;
        const float itemR = itemD * 0.5f;
        const auto r = getLocalBounds().toFloat();
        const auto center = r.getCentre();
        const float baseD = 60.0f;
        const float s0 = 1.6f, s1 = 1.4f, s2 = 1.2f, s3 = 1.1f;
        const float maxScaleR = itemR * s0;
        const float minRadius = baseD * 0.5f + 2.0f;
        const float maxRadius = juce::jmin<float>(getWidth(), getHeight()) * 0.5f - maxScaleR - 2.0f;
        const float radius = juce::jlimit(minRadius, juce::jmax(minRadius, maxRadius), minRadius + (maxRadius - minRadius) * openAmount);
        const float startAt12 = -juce::MathConstants<float>::halfPi;
        const float step = juce::MathConstants<float>::twoPi / (float) count;

        for (int i = 0; i < count; ++i)
        {
            const float ang = startAt12 + step * (float) i;
            const float cx = center.x + radius * std::cos(ang);
            const float cy = center.y + radius * std::sin(ang);
            const float dx = pos.x - cx;
            const float dy = pos.y - cy;
            const float rr = itemR; // strict click area equals base circle
            if ((dx*dx + dy*dy) <= (rr * rr))
                return i + 1;
        }
        return -1;
    }

    // Hover detection: expanded radius and choose the nearest circle among candidates
    int hitTestOptionHover(juce::Point<float> pos) const
    {
        if (openAmount <= 0.01f) return -1;
        const int count = 16;
        const float itemD = 22.0f;
        const float itemR = itemD * 0.5f;
        const float extra = 8.0f; // expand hover radius by 8px for easier targeting
        const auto r = getLocalBounds().toFloat();
        const auto center = r.getCentre();
        const float baseD = 60.0f;
        const float minRadius = baseD * 0.5f + 2.0f;
        // Allow enough headroom for hover scaling in layout
        const float maxRadius = juce::jmin<float>(getWidth(), getHeight()) * 0.5f - (itemR * 1.6f) - 2.0f;
        const float radius = juce::jlimit(minRadius, juce::jmax(minRadius, maxRadius), minRadius + (maxRadius - minRadius) * openAmount);
        const float startAt12 = -juce::MathConstants<float>::halfPi;
        const float step = juce::MathConstants<float>::twoPi / (float) count;

        int bestIndex = -1;
        float bestDist2 = std::numeric_limits<float>::max();
        const float rr = itemR + extra;
        const float rr2 = rr * rr;
        for (int i = 0; i < count; ++i)
        {
            const float ang = startAt12 + step * (float) i;
            const float cx = center.x + radius * std::cos(ang);
            const float cy = center.y + radius * std::sin(ang);
            const float dx = pos.x - cx;
            const float dy = pos.y - cy;
            const float d2 = dx*dx + dy*dy;
            if (d2 <= rr2 && d2 < bestDist2)
            {
                bestDist2 = d2;
                bestIndex = i + 1;
            }
        }
        return bestIndex;
    }
};
