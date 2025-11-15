#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Shared theme colours (extern declarations if needed elsewhere)
struct UiThemeColours
{
    static juce::Colour accent() { return juce::Colour::fromRGB(0xFF, 0x4E, 0x5B); }
    static juce::Colour base()   { return juce::Colour::fromRGB(0x26, 0x26, 0x26); }
    static juce::Colour cyan()   { return juce::Colour::fromRGB(0x00, 0xD7, 0xFF); }
};

// Rotary (click level) look-and-feel
class ClickRotaryLNF : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                          float sliderPos, float startAng, float endAng, juce::Slider& s) override
    {
        juce::ignoreUnused(s);
        auto bounds = juce::Rectangle<float>(x, y, w, h).reduced(4.0f);
        const float radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float ringThickness = 8.0f;
        const float innerReduce = ringThickness;

        g.setColour(UiThemeColours::accent());
        g.fillEllipse(bounds);
        g.setColour(UiThemeColours::base());
        g.fillEllipse(bounds.reduced(innerReduce));

        const float angle = (startAng + sliderPos * (endAng - startAng))
                            - (juce::MathConstants<float>::pi * 0.75f);
        const float needleLen = radius - innerReduce * 0.5f - 2.0f;
        const float nx = centre.x + needleLen * std::cos(angle);
        const float ny = centre.y + needleLen * std::sin(angle);

        juce::Colour needleCol = UiThemeColours::accent().interpolatedWith(UiThemeColours::cyan(), sliderPos);
        g.setColour(needleCol);
        g.drawLine(centre.x, centre.y, nx, ny, 3.0f);

        g.setColour(UiThemeColours::accent());
        g.fillEllipse(centre.x - 5.0f, centre.y - 5.0f, 10.0f, 10.0f);
    }
};

// General button / popup look-and-feel
class ThemeLNF : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&,
                              bool isHighlighted, bool isDown) override
    {
        auto r = b.getLocalBounds().toFloat();
        const float corner = 3.0f;
        juce::Colour fill = UiThemeColours::accent().brighter(0.15f);
        if (b.getToggleState() || isDown) fill = UiThemeColours::cyan();
        else if (isHighlighted) fill = UiThemeColours::accent().brighter(0.25f);

        g.setColour(UiThemeColours::base());
        g.fillRoundedRectangle(r.translated(2.0f, 2.0f), corner);
        g.setColour(fill);
        g.fillRoundedRectangle(r, corner);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setColour(UiThemeColours::base());
        g.setFont(juce::Font(juce::FontOptions("Arial", 11.0f, juce::Font::bold)));
        g.drawFittedText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, 1);
    }

    void drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        juce::ignoreUnused(w, h);
        auto r = box.getLocalBounds().toFloat();
        g.setColour(UiThemeColours::accent());
        g.fillRoundedRectangle(r, 5.0f);
        g.setColour(UiThemeColours::base());
        g.fillRoundedRectangle(r.reduced(2.0f), 4.0f);
    }

    juce::Font getPopupMenuFont() override { return juce::Font(juce::FontOptions("Arial", 11.0f, juce::Font::bold)); }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return getPopupMenuFont(); }

    void drawPopupMenuBackground(juce::Graphics& g, int w, int h) override
    {
        g.fillAll(UiThemeColours::base());
        g.setColour(UiThemeColours::accent());
        g.drawRect(0, 0, w, h, 1);
    }

    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive, bool isHighlighted, bool isTicked, bool,
                           const juce::String& text, const juce::String& shortcut, const juce::Drawable*, const juce::Colour*) override
    {
        if (isSeparator)
        {
            g.setColour(UiThemeColours::accent().darker(0.2f));
            g.fillRect(area.reduced(4).removeFromTop(1));
            return;
        }

        juce::Colour textCol;
        if (! isActive) textCol = UiThemeColours::accent().withAlpha(0.5f);
        else if (isHighlighted) textCol = juce::Colours::black;
        else if (isTicked) textCol = UiThemeColours::cyan();
        else textCol = UiThemeColours::accent();

        if (isHighlighted && isActive)
        {
            g.setColour(UiThemeColours::accent());
            g.fillRoundedRectangle(area.reduced(2).toFloat(), 3.0f);
        }

        g.setColour(textCol);
        g.setFont(getPopupMenuFont());
        auto r = area.reduced(6);
        g.drawFittedText(text, r, juce::Justification::centredLeft, 1);
        if (shortcut.isNotEmpty())
        {
            g.setColour(textCol.withAlpha(0.8f));
            g.drawFittedText(shortcut, r, juce::Justification::centredRight, 1);
        }
    }
};
