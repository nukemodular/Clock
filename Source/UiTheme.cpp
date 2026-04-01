#include "UiTheme.h"

#include "UiTheme.h"

ThemeLNF::~ThemeLNF() = default;

void ThemeLNF::drawComboBox(juce::Graphics& g, int w, int h, bool isButtonDown, int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox& box)
{
    juce::ignoreUnused(isButtonDown, buttonX, buttonY, buttonW, buttonH);
    auto bounds = juce::Rectangle<int>(0, 0, w, h);
    g.setColour(theme.fixedBase());
    g.fillRoundedRectangle(bounds.toFloat(), 3.0f);
    g.setColour(theme.accent());
    g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 3.0f, 1.5f);
    g.setColour(theme.accent());
    g.setFont(getComboBoxFont(box));
    g.drawFittedText(box.getText(), bounds.reduced(4), juce::Justification::centred, 1);
}
