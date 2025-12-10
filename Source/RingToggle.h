#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "UiTheme.h"

// Simple elliptical toggle button (30x30 by design)
// - Outer: UiThemeColours::accent() filled ellipse
// - Middle: inner ellipse reduced(8.0f) in UiThemeColours::base()
// - When ON: draw additional ellipse reduced(12.0f) in UiThemeColours::cyan(); when OFF: hide this cyan ellipse
// Usage: set size to at least 30x30; you can place larger, it will center a 30x30 disk
class RingToggle : public juce::ToggleButton
{
public:
    RingToggle(UiThemeColours& t) : theme(t) {}

    void setColours(juce::Colour, juce::Colour, juce::Colour)
    {
        repaint();
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
        g.setColour(theme.accent());
        g.fillEllipse(rf);
        // Middle base disk (reduced by 8px overall => 4px per side)
        g.setColour(theme.base());
        g.fillEllipse(rf.reduced(reducedSize));
        // ON state cyan disk (reduced by 12px overall => 6px per side)
        if (getToggleState())
        {
            g.setColour(theme.cyan());
            g.fillEllipse(rf.reduced(reducedSize * 1.5f));
        }
    }

    // Toggle on mouse-down to make interactions feel more immediate; suppress
    // the default mouseUp behaviour so clicks aren't processed twice.
    void mouseDown(const juce::MouseEvent& /*e*/) override
    {
        setToggleState(! getToggleState(), juce::sendNotification);
    }

    void mouseUp(const juce::MouseEvent& /*e*/) override
    {
        // Intentionally empty: we've already handled the toggle on mouseDown.
    }

private:
    UiThemeColours& theme;
};
