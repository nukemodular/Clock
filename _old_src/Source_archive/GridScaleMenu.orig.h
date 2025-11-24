// Original GridScaleMenu.h (archived)
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "UiTheme.h"

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
        const float outerD = UiLayout::kGridOuterD;
        const float innerReduce = UiLayout::kGridInnerReduce;
        juce::Rectangle<float> outer(center.x - outerD * 0.5f, center.y - outerD * 0.5f, outerD, outerD);
        juce::Rectangle<float> inner = outer.reduced(innerReduce);

        g.setColour(kAccent); g.fillEllipse(outer);
        g.setColour(kBase);   g.fillEllipse(inner);

        if (openAmount > 0.01f)
            drawOptions(g, center, outerD);

        // Display current value text
        static const char* labels[4] = { "32", "16", "8", "4" };
        g.setColour(kCyan);
        g.setFont(juce::Font(juce::FontOptions("Arial", UiLayout::kFontMedium, juce::Font::bold)));
        g.drawFittedText(labels[currentIndex], outer.toNearestInt(), juce::Justification::centred, 1);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        auto r = getLocalBounds().toFloat();
        auto c = r.getCentre();
        const float outerR = UiLayout::kGridOuterR + UiLayout::kGridOuterMargin * 0.5f;
        const float dist = c.getDistanceFrom(e.position);
        if (! menuOpen)
        {
            if (dist <= outerR)
            {
                opening = true; closing = false; menuOpen = true;
                // Precompute hover for immediate feedback/selection on the coming mouseUp
                hoverIndex = hitTestOptionHover(e.position);
            }
        }
        else
        {
            int opt = hitTestOption(e.position);
            if (opt >= 0)
            {
                // clicking an option -> register hover so mouseUp selects it
                hoverIndex = hitTestOptionHover(e.position);
            }
            else
            {
                if (dist <= outerR)
                {
                    opening = false; closing = true; menuOpen = false;
                    hoverIndex = -1;
                }
            }
        }
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (! menuOpen) return;
        if (opening) return;
        int hit = hitTestOption(e.position);
        if (hit < 0 && hoverIndex >= 0)
            hit = hoverIndex;
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
        const float baseD = UiLayout::kGridBaseD;
        const float itemD = UiLayout::kOptionItemD;
        const float itemR = itemD * 0.5f;
        const float s0 = UiLayout::kGridS0; // keep in sync with drawOptions
        const float maxScaleR = itemR * s0;
        const float minRadius = baseD * 0.5f - 2.0f;
        const float tighten = UiLayout::kGridTighten; // reduced to allow wider spread
        const float maxRadius = juce::jmin<float>(getWidth(), getHeight()) * 0.5f - maxScaleR  - tighten;
        const float radius = juce::jlimit(minRadius, juce::jmax(minRadius, maxRadius), minRadius + (maxRadius - minRadius) * openAmount);
        const float baseRadius = radius - UiLayout::kInwardWhenOpen * openAmount;
        const float outerBound = radius + maxScaleR + UiLayout::kGridOuterMargin; // slightly larger margin to avoid accidental collapse at extremes
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
    int currentIndex { 1 };
    int hoverIndex { -1 };

    juce::Colour kAccent { juce::Colour::fromRGB(0xFF, 0x4E, 0x5B) };
    juce::Colour kBase   { juce::Colour::fromRGB(0x26, 0x26, 0x26) };
    juce::Colour kCyan   { juce::Colour::fromRGB(0x00, 0xD7, 0xFF) };

    static constexpr float kStartDeg = 110.0f; // lower bound of arc
    static constexpr float kEndDeg   = 250.0f; // upper bound of arc
};
