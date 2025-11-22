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
// ClickRotaryLNF removed (rotary visual migrated to PlaygroundComponent). Re-add if a standalone Slider is reintroduced.

// General button / popup look-and-feel
class ThemeLNF : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&,
                              bool isHighlighted, bool isDown) override
    {
        auto r = b.getLocalBounds().toFloat();
        const float corner = 2.0f;
        juce::Colour fill = UiThemeColours::accent().darker(0.15f);
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
        g.setFont(juce::Font(juce::FontOptions("Arial", 12.0f, juce::Font::bold)));
        g.drawFittedText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, 1);
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool isMouseOverButton, bool isButtonDown) override
    {
        juce::ignoreUnused(isMouseOverButton, isButtonDown);
        // Draw same background as regular buttons
        drawButtonBackground(g, b, juce::Colours::transparentBlack, isMouseOverButton, isButtonDown);

        // Draw the label centred
        g.setColour(UiThemeColours::base());
        g.setFont(juce::Font(juce::FontOptions("Arial", 12.0f, juce::Font::bold)));
        g.drawFittedText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, 1);
    }

    void drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        juce::ignoreUnused(w, h);
        auto r = box.getLocalBounds().toFloat();
        // background
        g.setColour(UiThemeColours::accent());
        g.fillRoundedRectangle(r, 5.0f);
        g.setColour(UiThemeColours::base());
        g.fillRoundedRectangle(r.reduced(2.0f), 4.0f);

        // Draw ComboBox text (use ComboBox::textColourId if set) rather than
        // relying on the internal label which can inherit an unreadable colour.
        juce::String txt = box.getText();
        if (txt.isNotEmpty())
        {
            juce::Colour textCol = box.findColour(juce::ComboBox::textColourId);
            if (! textCol.isOpaque()) textCol = UiThemeColours::cyan();
            g.setColour(textCol);
            g.setFont(getComboBoxFont(box));
            auto textR = r.reduced(6.0f, 2.0f);
            g.drawFittedText(txt, textR.toNearestInt(), juce::Justification::centred, 1);
        }

        // draw a small drop-arrow at the far right (respect arrow colour)
        juce::Colour arrowCol = box.findColour(juce::ComboBox::arrowColourId);
        if (arrowCol.isOpaque())
        {
            const float aw = 10.0f;
            const float padding = 6.0f;
            juce::Path p;
            const float cx = r.getRight() - padding - aw * 0.5f;
            const float cy = r.getCentreY();
            p.startNewSubPath(cx - aw * 0.5f, cy - aw * 0.25f);
            p.lineTo(cx, cy + aw * 0.5f);
            p.lineTo(cx + aw * 0.5f, cy - aw * 0.25f);
            p.closeSubPath();
            g.setColour(arrowCol);
            g.fillPath(p);
        }
    }

    juce::Font getPopupMenuFont() override { return juce::Font(juce::FontOptions("Arial", 12.0f, juce::Font::bold)); }
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
        else if (isHighlighted) textCol = UiThemeColours::base();
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
