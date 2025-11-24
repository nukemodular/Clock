// Auto-backup of Source/GridScaleMenu.h — copied on 2025-11-24
// To restore: copy this file back to Source/GridScaleMenu.h and re-run CMake

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
        // Allow a small slack around the visual root so slightly outward clicks still toggle
        const float outerR = UiLayout::kGridOuterR + UiLayout::kGridOuterMargin * 0.5f;
        const float dist = c.getDistanceFrom(e.position);
        if (! menuOpen)
        {
            // Closed -> open when clicking near the base
            if (dist <= outerR)
            {
                opening = true; closing = false; menuOpen = true;
                // Precompute hover for immediate feedback/selection on the coming mouseUp
                hoverIndex = hitTestOptionHover(e.position);
            }
        }
        else
        {
            // Menu already open: if the click landed on an option, don't toggle/close here
            int opt = hitTestOption(e.position);
            if (opt >= 0)
            {
                // clicking an option -> register hover so mouseUp selects it
                hoverIndex = hitTestOptionHover(e.position);
            }
            else
            {
                // Click didn't hit an option — if it's within the base area, treat as a toggle
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
        // Ignore selection while the menu is still animating open to avoid
        // immediate collapse when the toggle was triggered on mouseDown.
        if (opening) return;
        int hit = hitTestOption(e.position);
        // If the precise hit test missed (due to small timing/jitter), prefer the current hoverIndex
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
    int currentIndex { 1 }; // default to 1/16
    int hoverIndex { -1 }; // 0..3 when hovering

    juce::Colour kAccent { juce::Colour::fromRGB(0xFF, 0x4E, 0x5B) };
    juce::Colour kBase   { juce::Colour::fromRGB(0x26, 0x26, 0x26) };
    juce::Colour kCyan   { juce::Colour::fromRGB(0x00, 0xD7, 0xFF) };

    // Centralized tweakable layout constants (use values from `UiLayout`)

    // Centralised arc degrees for option placement (degrees)
    static constexpr float kStartDeg = 110.0f; // lower bound of arc
    static constexpr float kEndDeg   = 250.0f; // upper bound of arc

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
        const float itemD = UiLayout::kOptionItemD;
        const float itemR = itemD * 0.5f;
        const float s0 = UiLayout::kGridS0, s1 = UiLayout::kGridS1; // hovered, adjacent, normal
        const float maxScaleR = itemR * s0;
        const float minRadius = baseD * 0.5f - 4.0f;
        const float tighten = UiLayout::kGridTighten;
        const float maxRadius = juce::jmin<float>(getWidth(), getHeight()) * 0.5f - maxScaleR  - tighten;
        const float radius = juce::jlimit(minRadius, juce::jmax(minRadius, maxRadius), minRadius + (maxRadius - minRadius) * openAmount);

        // Pull the whole ring slightly inward when open so the options sit closer to centre.
        const float baseRadius = radius - UiLayout::kInwardWhenOpen * openAmount;

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

        // Non-hovered items first
        for (int i = 0; i < count; ++i)
        {
            if (i == hoverIndex) continue; // draw hovered last
            const float ang = angleForIndex(i);
            // Non-hovered items sit slightly inward (baseRadius)
            const float cx = center.x + baseRadius * std::cos(ang);
            const float cy = center.y + baseRadius * std::sin(ang);
            const float sc = scaleFor(i);
            const float d = itemD * sc;
            const float r = d * 0.5f;
            juce::Rectangle<float> ring(cx - r, cy - r, d, d);
            g.setColour(juce::Colours::black.withAlpha(0.25f));
            g.fillEllipse(ring.translated(2.0f, 2.0f));
            g.setColour(kAccent); g.fillEllipse(ring);
            g.setColour(kBase);
            g.setFont(juce::Font(juce::FontOptions("Arial", UiLayout::kFontSmall * juce::jlimit(1.0f, s0, sc), juce::Font::bold)));
            g.drawFittedText(labels[i], ring.toNearestInt(), juce::Justification::centred, 1);
        }
        if (hoverIndex >= 0 && hoverIndex < count)
        {
            const int i = hoverIndex;
            const float ang = angleForIndex(i);
            // Hovered item moves outward relative to the baseRadius for emphasis
            const float cx = center.x + (baseRadius + UiLayout::kHoverOutward * openAmount) * std::cos(ang);
            const float cy = center.y + (baseRadius + UiLayout::kHoverOutward * openAmount) * std::sin(ang);
            const float scale = s0;
            const float d = itemD * scale;
            const float r = d * 0.5f;
            juce::Rectangle<float> ring(cx - r, cy - r, d, d);
            g.setColour(juce::Colours::black.withAlpha(0.35f));
            g.fillEllipse(ring.translated(2.0f, 2.0f));
            g.setColour(kCyan); g.fillEllipse(ring);
            g.setColour(kBase);
            g.setFont(juce::Font(juce::FontOptions("Arial", UiLayout::kFontSmall * scale / s0 + 3.0f, juce::Font::bold)));
            g.drawFittedText(labels[i], ring.toNearestInt(), juce::Justification::centred, 1);
        }
    }

    int hitTestOption(juce::Point<float> pos) const
    {
        if (openAmount <= 0.01f) return -1;
        const int count = 4;
        const float itemD = UiLayout::kOptionItemD;
        const float itemR = itemD * 0.5f;
        auto r = getLocalBounds().toFloat();
        auto center = r.getCentre();
        const float baseD = UiLayout::kGridBaseD;
        const float s0 = UiLayout::kGridS0; // unify with drawOptions
        const float maxScaleR = itemR * s0;
        const float minRadius = baseD * 0.5f - 2.0f;
        const float tighten = UiLayout::kGridTighten;
        const float maxRadius = juce::jmin<float>(getWidth(), getHeight()) * 0.5f - maxScaleR  - tighten;
        const float radius = juce::jlimit(minRadius, juce::jmax(minRadius, maxRadius), minRadius + (maxRadius - minRadius) * openAmount);
        const float baseRadius = radius - UiLayout::kInwardWhenOpen * openAmount;
        auto angleForIndex = [](int i)
        {
            const float stepDeg  = (kEndDeg - kStartDeg) / 3.0f;
            const float deg = kStartDeg + stepDeg * (float) i;
            return juce::degreesToRadians(deg);
        };

        const float hoverShift = UiLayout::kHoverOutward * openAmount;
        const float rr = itemR + 2.5f;
        const float rr2 = rr * rr;
        for (int i = 0; i < count; ++i)
        {
            const float ang = angleForIndex(i);
            const float cx1 = center.x + baseRadius * std::cos(ang);
            const float cy1 = center.y + baseRadius * std::sin(ang);
            const float dx1 = pos.x - cx1;
            const float dy1 = pos.y - cy1;
            if ((dx1*dx1 + dy1*dy1) <= rr2) return i;

            const float cx2 = center.x + (baseRadius + hoverShift) * std::cos(ang);
            const float cy2 = center.y + (baseRadius + hoverShift) * std::sin(ang);
            const float dx2 = pos.x - cx2;
            const float dy2 = pos.y - cy2;
            if ((dx2*dx2 + dy2*dy2) <= rr2) return i;
        }
        return -1;
    }

    int hitTestOptionHover(juce::Point<float> pos) const
    {
        if (openAmount <= 0.01f) return -1;
        const int count = 4;
        const float itemD = UiLayout::kOptionItemD;
        const float itemR = itemD * 0.5f;
        const float extra = 10.0f; // expand hover radius for stability, especially at index 0/3
        auto r = getLocalBounds().toFloat();
        auto center = r.getCentre();
        const float baseD = UiLayout::kGridBaseD;
        const float s0 = UiLayout::kGridS0; // unify with drawOptions
        const float maxScaleR = itemR * s0;
        const float minRadius = baseD * 0.5f - 2.0f;
        const float tighten = UiLayout::kGridTighten;
        const float maxRadius = juce::jmin<float>(getWidth(), getHeight()) * 0.5f - maxScaleR  - tighten;
        const float radius = juce::jlimit(minRadius, juce::jmax(minRadius, maxRadius), minRadius + (maxRadius - minRadius) * openAmount);
        const float baseRadius = radius - UiLayout::kInwardWhenOpen * openAmount;
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
        const float hoverShift = UiLayout::kHoverOutward * openAmount;
        for (int i = 0; i < count; ++i)
        {
            const float ang = angleForIndex(i);
            const float cx1 = center.x + baseRadius * std::cos(ang);
            const float cy1 = center.y + baseRadius * std::sin(ang);
            const float dx1 = pos.x - cx1;
            const float dy1 = pos.y - cy1;
            const float d21 = dx1*dx1 + dy1*dy1;
            if (d21 <= rr2 && d21 < bestDist2) { bestDist2 = d21; bestIndex = i; }

            const float cx2 = center.x + (baseRadius + hoverShift) * std::cos(ang);
            const float cy2 = center.y + (baseRadius + hoverShift) * std::sin(ang);
            const float dx2 = pos.x - cx2;
            const float dy2 = pos.y - cy2;
            const float d22 = dx2*dx2 + dy2*dy2;
            if (d22 <= rr2 && d22 < bestDist2) { bestDist2 = d22; bestIndex = i; }
        }
        return bestIndex;
    }
};
