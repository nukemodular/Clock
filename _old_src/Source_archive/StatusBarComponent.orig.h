// StatusBarComponent.h (archived original wrapper)
// Compatibility wrapper: implementation moved to UiComponents.h
#pragma once
#include "UiComponents.h"

#pragma once

// Archived original StatusBarComponent implementation (copied from UiComponents.h)
// Kept here for safety in case restoration is required.

#include <juce_gui_basics/juce_gui_basics.h>
#include "LookAndFeels.h"

class StatusBarComponent : public juce::Component
{
public:
    void setStatusText(const juce::String& s)
    {
        if (statusText != s)
        {
            statusText = s;
            repaint();
        }
    }

    void setLedLevel(float v)
    {
        v = juce::jlimit(0.0f, 1.0f, v);
        if (std::abs(ledLevel - v) > 0.001f)
        {
            ledLevel = v;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds();
        auto textArea = b.withTrimmedRight(20);
        g.setColour(UiThemeColours::cyan());
        g.setFont(juce::Font(juce::FontOptions("Arial", 11.0f, juce::Font::bold)));
        g.drawFittedText(statusText, textArea, juce::Justification::centredRight, 1);

        const float ledR = 6.0f;
        auto cx = (float) (b.getRight() - 5);
        auto cy = (float) b.getCentreY();
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillEllipse(cx - ledR - 1.5f, cy - ledR + 1.5f, ledR * 2.0f, ledR * 2.0f);
        auto ledColour = UiThemeColours::cyan().withAlpha(0.10f).interpolatedWith(UiThemeColours::cyan().withAlpha(0.97f), ledLevel);
        g.setColour(ledColour);
        g.fillEllipse(cx - ledR, cy - ledR, ledR * 2.0f, ledR * 2.0f);
        g.setColour(juce::Colours::white.withAlpha(0.15f));
        g.drawEllipse(cx - ledR, cy - ledR, ledR * 2.0f, ledR * 2.0f, 1.0f);
    }

private:
    juce::String statusText { "" };
    float ledLevel { 0.0f };
};
