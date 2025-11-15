#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Simple elliptical toggle button (30x30 by design)
// - Outer: kAccent filled ellipse
// - Middle: inner ellipse reduced(8.0f) in kBase
// - When ON: draw additional ellipse reduced(12.0f) in kCyan; when OFF: hide this cyan ellipse
// Usage: set size to at least 30x30; you can place larger, it will center a 30x30 disk
class EllipseToggleButton : public juce::ToggleButton
{
public:
    EllipseToggleButton() = default;

    void setColours(juce::Colour accent, juce::Colour base, juce::Colour cyan)
    {
        kAccent = accent; kBase = base; kCyan = cyan; repaint();
    }

    // Preferred size helper
    static constexpr int preferredSize = 30; // 30x30
    static constexpr int reducedSize = preferredSize * 0.25f; // 25% reduction for inner ellipse
    void paintButton(juce::Graphics& g, bool /*isMouseOverButton*/, bool /*isButtonDown*/) override
    {
        auto lb = getLocalBounds();
        // Constrain to a centered square of up to 30x30 (or smaller if component is smaller)
        const int d = std::max(preferredSize, std::min(lb.getWidth(), lb.getHeight()));
        juce::Rectangle<int> square(lb.getCentreX() - d / 2, lb.getCentreY() - d / 2, d, d);
        auto rf = square.toFloat();

        // Outer accent disk
        g.setColour(kAccent);
        g.fillEllipse(rf);
        // Middle base disk (reduced by 8px overall => 4px per side)
        g.setColour(kBase);
        g.fillEllipse(rf.reduced(reducedSize));
        // ON state cyan disk (reduced by 12px overall => 6px per side)
        if (getToggleState())
        {
            g.setColour(kCyan);
            g.fillEllipse(rf.reduced(reducedSize * 1.5f));
        }
    }

private:
    juce::Colour kAccent { juce::Colour::fromRGB(0xFF, 0x4E, 0x5B) };
    juce::Colour kBase   { juce::Colour::fromRGB(0x26, 0x26, 0x26) };
    juce::Colour kCyan   { juce::Colour::fromRGB(0x00, 0xD7, 0xFF) };
};
