// Original ShuffleModeMenu.h (archived)
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

// Seven arc-arranged ellipse buttons below the main ring.
// Sizes interpolate from 30x30 (number 1) to 50x50 (number 7).
// Inner ellipse reduced by (size * 0.33f) in kBase; optional cyan inner highlight when pressed (momentary flash).
// Numbers (1..7) drawn in kCyan.
// Order along arc from 7 o'clock to 5 o'clock clockwise: 1..7 (smallest to largest).
// Callback invoked on click with selected number (1..7).
class ShuffleModeMenu : public juce::Component
{
public:
    std::function<void(int)> onButtonClicked; // legacy click callback, passes 1..7 (on mouseUp)
    std::function<void(int)> onValueChanged;  // slider-like change callback, fires on drag/mouseDown as value changes (1..7)

    void setValue(int v)
    {
        int clamped = juce::jlimit(1, 7, v);
        if (currentIndex != clamped - 1)
        {
            currentIndex = clamped - 1;
            repaint();
        }
    }
    int getValue() const noexcept { return currentIndex + 1; }

    void setManualBounds(const std::array<juce::Rectangle<int>, 7>& rects)
    {
        manualMode = true;
        const auto origin = getPosition();
        for (size_t i = 0; i < rects.size(); ++i)
            buttonBounds[i] = rects[i].translated(-origin.x, -origin.y);
        repaint();
    }

    bool isManualMode() const noexcept { return manualMode; }

    void setColours(juce::Colour accent, juce::Colour base, juce::Colour cyan)
    {
        kAccent = accent; kBase = base; kCyan = cyan; repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        if (manualMode)
        {
            const int i = juce::jlimit(0, (int) buttonBounds.size() - 1, currentIndex);
            auto outer = buttonBounds[(size_t) i].toFloat();
            const float sz = outer.getWidth();
            const float t = juce::jlimit(0.0f, 1.0f, (sz - 30.0f) / 20.0f);
            g.setColour(kAccent); g.fillEllipse(outer);
            const float reduce = sz * 0.22f;
            g.setColour(kBase); g.fillEllipse(outer.reduced(reduce));
            g.setColour(kCyan);
            g.setFont(juce::Font(juce::FontOptions("Arial", 13.0f + t * 4.5f, juce::Font::bold)));
            g.drawFittedText(juce::String(i + 1), outer.toNearestInt(), juce::Justification::centred, 1);
            return;
        }

        const float maxSize = 50.0f;
        const float minSize = 30.0f;
        const float leftMargin = 4.0f, rightMargin = 4.0f, topMargin = 2.0f, bottomMargin = 2.0f;
        const float yTopLimit = r.getY() + topMargin + maxSize * 0.5f;
        const float yBottomLimit = r.getBottom() - bottomMargin - maxSize * 0.5f;
        float centerY = (yBottomLimit + 2.0f * yTopLimit) / 3.0f;
        centerY = juce::jlimit(yTopLimit, yBottomLimit, centerY);
        float radiusV = juce::jmax(0.0f, juce::jmin(yBottomLimit - centerY, 2.0f * (centerY - yTopLimit)));
        const float xLeftLimit = r.getX() + leftMargin + maxSize * 0.5f;
        const float xRightLimit = r.getRight() - rightMargin - maxSize * 0.5f;
        const float centerX = r.getCentreX();
        float radiusH = juce::jmax(0.0f, juce::jmin((xRightLimit - centerX) / 0.866f, (centerX - xLeftLimit) / 0.866f));
        const float radius = juce::jmax(0.0f, juce::jmin(radiusV, radiusH));
        juce::Point<float> center(centerX, centerY);
        const float startDeg = 200.0f;
        const float endDeg   = 340.0f;
        const int count = 7;
        for (int i = count - 1; i >= 0; --i)
        {
            const float t = (float) i / (float) (count - 1);
            const float deg = startDeg + (endDeg - startDeg) * t;
            const float ang = juce::degreesToRadians(deg);
            const float size = minSize + (maxSize - minSize) * t;
            const float sz = size;
            const float cx = center.x + radius * std::cos(ang);
            const float cy = center.y + radius * std::sin(ang);
            juce::Rectangle<float> outer(cx - sz * 0.5f, cy - sz * 0.5f, sz, sz);
            buttonBounds[i] = outer.toNearestInt();
        }
        const int i = juce::jlimit(0, (int) buttonBounds.size() - 1, currentIndex);
        auto outer = buttonBounds[(size_t) i].toFloat();
        const float sz = outer.getWidth();
        const float tt = juce::jlimit(0.0f, 1.0f, (sz - 30.0f) / 20.0f);
        g.setColour(kAccent); g.fillEllipse(outer);
        const float reduce = sz * 0.22f;
        g.setColour(kBase); g.fillEllipse(outer.reduced(reduce));
        g.setColour(kCyan);
        g.setFont(juce::Font(juce::FontOptions("Arial", 13.0f + tt * 4.5f, juce::Font::bold)));
        g.drawFittedText(juce::String(i + 1), outer.toNearestInt(), juce::Justification::centred, 1);
    }

    void resized() override
    {
        if (! manualMode)
            for (auto& b : buttonBounds) b = juce::Rectangle<int>();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        dragging = true;
        const int idx = nearestIndex(e.position);
        applyIndexFromUser(idx, true);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        const int idx = nearestIndex(e.position);
        applyIndexFromUser(idx, true);
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        juce::ignoreUnused(e);
        dragging = false;
        if (onButtonClicked) onButtonClicked(currentIndex + 1);
    }

    bool hitTest(int x, int y) override
    {
        return getLocalBounds().contains(x, y);
    }
};
