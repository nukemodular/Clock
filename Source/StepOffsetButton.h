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
        const int idx = hitTestOption(e.position);
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
        const float itemD = 18.0f; // larger; can touch easily
        const float itemR = itemD * 0.5f;
        const float minRadius = baseD * 0.5f + 4.0f; // closer to base so items can touch easily
        const float maxRadius = juce::jmin<float>(getWidth(), getHeight()) * 0.5f - itemR - 2.0f;
        const float radius = juce::jlimit(minRadius, juce::jmax(minRadius, maxRadius), minRadius + (maxRadius - minRadius) * openAmount);

        g.addTransform(juce::AffineTransform());
        const float startAt12 = -juce::MathConstants<float>::halfPi;
        const float step = juce::MathConstants<float>::twoPi / (float) count;
        // Draw non-hovered items first
        for (int i = 0; i < count; ++i)
        {
            if (i + 1 == hoverIndex) continue;
            const float ang = startAt12 + step * (float) i;
            const float cx = center.x + radius * std::cos(ang);
            const float cy = center.y + radius * std::sin(ang);
            juce::Rectangle<float> ring(cx - itemR, cy - itemR, itemD, itemD);
            g.setColour(kAccent); g.fillEllipse(ring);
            g.setColour(kBase);
            g.setFont(juce::Font(juce::FontOptions("Arial", 15.0f, juce::Font::bold)));
            g.drawFittedText(juce::String(i + 1), ring.toNearestInt(), juce::Justification::centred, 1);
        }
        // Draw hovered last, scaled and on top
        if (hoverIndex >= 1 && hoverIndex <= count)
        {
            const int i = hoverIndex - 1;
            const float ang = startAt12 + step * (float) i;
            const float cx = center.x + radius * std::cos(ang);
            const float cy = center.y + radius * std::sin(ang);
            const float scale = 1.5f;
            const float d = itemD * scale;
            const float r = d * 0.5f;
            juce::Rectangle<float> ring(cx - r, cy - r, d, d);
            // Optional subtle shadow to emphasize front
            g.setColour(juce::Colours::black.withAlpha(0.25f));
            g.fillEllipse(ring.translated(0.0f, 1.5f));
            g.setColour(kCyan); g.fillEllipse(ring);
            g.setColour(kBase);
            g.setFont(juce::Font(juce::FontOptions("Arial", 17.0f, juce::Font::bold)));
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
        const float minRadius = baseD * 0.5f + 4.0f;
        const float maxRadius = juce::jmin<float>(getWidth(), getHeight()) * 0.5f - itemR - 2.0f;
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
            if ((dx*dx + dy*dy) <= (itemR * itemR))
                return i + 1;
        }
        return -1;
    }
};
