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
    // Callbacks
    std::function<void(int)> onButtonClicked; // legacy click callback, passes 1..7 (on mouseUp)
    std::function<void(int)> onValueChanged;  // slider-like change callback, fires on drag/mouseDown as value changes (1..7)

    // Value API (1..7)
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

    // Provide manual absolute layout: caller passes seven rectangles (each square) in parent coordinates.
    // When set, the dynamic arc layout is disabled and these fixed bounds are used for painting & hit-testing.
    void setManualBounds(const std::array<juce::Rectangle<int>, 7>& rects)
    {
        manualMode = true;
        // Convert from parent absolute coordinates to our local space, so caller can pass global positions.
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
            // Slider mode: draw only the current 'handle' (hide others)
            const int i = juce::jlimit(0, (int) buttonBounds.size() - 1, currentIndex);
            auto outer = buttonBounds[(size_t) i].toFloat();
            const float sz = outer.getWidth();
            const float t = juce::jlimit(0.0f, 1.0f, (sz - 30.0f) / 20.0f); // map 30..50 -> 0..1
            // subtle drop shadow
            // g.setColour(juce::Colours::black.withAlpha(0.35f));
            // g.fillEllipse(outer.translated(2.0f, 2.0f));
            g.setColour(kAccent); g.fillEllipse(outer);
            const float reduce = sz * 0.22f;
            g.setColour(kBase); g.fillEllipse(outer.reduced(reduce));
            g.setColour(kCyan);
            g.setFont(juce::Font(juce::FontOptions("Arial", 13.0f + t * 4.5f, juce::Font::bold)));
            g.drawFittedText(juce::String(i + 1), outer.toNearestInt(), juce::Justification::centred, 1);
            return;
        }

        // Fallback dynamic layout (legacy arc mode)
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
        // Draw only the current index in dynamic mode too
        const int i = juce::jlimit(0, (int) buttonBounds.size() - 1, currentIndex);
        auto outer = buttonBounds[(size_t) i].toFloat();
        const float sz = outer.getWidth();
        const float tt = juce::jlimit(0.0f, 1.0f, (sz - 30.0f) / 20.0f);
        // g.setColour(juce::Colours::black.withAlpha(0.33f));
        // g.fillEllipse(outer.translated(2.33f, 2.33f));
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
        // Capture mouse anywhere within our bounds so dragging can start from empty spaces too
        // (we'll snap to nearest index on mouseDown).
        return getLocalBounds().contains(x, y);
    }

private:
    juce::Colour kAccent { juce::Colour::fromRGB(0xFF, 0x4E, 0x5B) };
    juce::Colour kBase   { juce::Colour::fromRGB(0x26, 0x26, 0x26) };
    juce::Colour kCyan   { juce::Colour::fromRGB(0x00, 0xD7, 0xFF) };

    std::array<juce::Rectangle<int>, 7> buttonBounds {};
    bool manualMode { false };
    int currentIndex { 3 }; // 0..6, default to 4th (value 4)
    bool dragging { false };

    int hitTestIndex(juce::Point<float> pos) const
    {
        for (int i = 0; i < (int) buttonBounds.size(); ++i)
        {
            if (buttonBounds[i].contains((int) std::round(pos.x), (int) std::round(pos.y)))
                return i;
        }
        return -1;
    }

    int nearestIndex(juce::Point<float> pos) const
    {
        int best = -1;
        float bestD2 = std::numeric_limits<float>::max();
        for (int i = 0; i < (int) buttonBounds.size(); ++i)
        {
            auto b = buttonBounds[(size_t) i];
            auto c = b.getCentre();
            const float dx = pos.x - (float) c.x;
            const float dy = pos.y - (float) c.y;
            const float d2 = dx*dx + dy*dy;
            if (d2 < bestD2) { bestD2 = d2; best = i; }
        }
        return best;
    }

    void applyIndexFromUser(int idx, bool notify)
    {
        idx = juce::jlimit(0, (int) buttonBounds.size() - 1, idx);
        if (currentIndex != idx)
        {
            currentIndex = idx;
            repaint();
            if (notify && onValueChanged) onValueChanged(currentIndex + 1);
        }
    }
};
