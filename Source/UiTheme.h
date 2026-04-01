#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_core/juce_core.h>
#include <array>

// Forward declaration for editor highlight logic
class ClockSyncAudioProcessorEditor;
// UiTheme.h
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_core/juce_core.h>
#include <array>

// Consolidated UI theme and layout constants.
// Merges LookAndFeels.h and UiLayoutConstants.h into a single include to
// reduce the number of tiny headers and keep theme + layout tightly coupled.

struct UiThemeColours
{
    static juce::Colour fixedBaseColour() { return juce::Colour::fromRGB(0x26, 0x26, 0x26); }

    juce::Colour accentColour { juce::Colour::fromRGB(0xFF, 0x4E, 0x5B) };
    juce::Colour baseColour   { fixedBaseColour() };
    juce::Colour cyanColour   { juce::Colour::fromRGB(0x00, 0xD7, 0xFF) };

    juce::Colour accent() const { return accentColour; }
    juce::Colour base()   const { return baseColour; }
    juce::Colour cyan()   const { return cyanColour; }
    juce::Colour fixedBase() const { return fixedBaseColour(); }

    void setAccent(juce::Colour c) { accentColour = c; }
    void setBase(juce::Colour c)   { baseColour = c; }
    void setCyan(juce::Colour c)   { cyanColour = c; }
};

// General look-and-feel class used across the editor
class ThemeLNF : public juce::LookAndFeel_V4
{
    UiThemeColours& theme;
public:
    ThemeLNF(UiThemeColours& t) : theme(t) {}
    virtual ~ThemeLNF() override;
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&,
                              bool isHighlighted, bool isDown) override
    {
        auto r = b.getLocalBounds().toFloat();
        const float corner = 2.0f;
        juce::Colour fill = theme.accent().darker(0.15f);
        if (b.getToggleState() || isDown) fill = theme.cyan();
        else if (isHighlighted) fill = theme.accent().brighter(0.25f);

        g.setColour(theme.fixedBase());
        g.fillRoundedRectangle(r.translated(2.0f, 2.0f), corner);
        g.setColour(fill);
        g.fillRoundedRectangle(r, corner);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setColour(theme.fixedBase());
        g.setFont(juce::Font(juce::FontOptions("Arial", 12.0f, juce::Font::bold)));
        g.drawFittedText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, 1);
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool isMouseOverButton, bool isButtonDown) override
    {
        juce::ignoreUnused(isMouseOverButton, isButtonDown);
        drawButtonBackground(g, b, juce::Colours::transparentBlack, isMouseOverButton, isButtonDown);
        g.setColour(theme.fixedBase());
        g.setFont(juce::Font(juce::FontOptions("Arial", 12.0f, juce::Font::bold)));
        g.drawFittedText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, 1);
    }

    void drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override;

    juce::Font getPopupMenuFont() override { return juce::Font(juce::FontOptions("Arial", 12.0f, juce::Font::bold)); }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return getPopupMenuFont(); }

    void drawPopupMenuBackground(juce::Graphics& g, int w, int h) override
    {
        g.fillAll(theme.fixedBase());
        g.setColour(theme.accent());
        g.drawRect(0, 0, w, h, 1);
    }

    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive, bool isHighlighted, bool isTicked, bool,
                           const juce::String& text, const juce::String& shortcut, const juce::Drawable*, const juce::Colour*) override
    {
        if (isSeparator)
        {
            g.setColour(theme.accent().darker(0.2f));
            g.fillRect(area.reduced(4).removeFromTop(1));
            return;
        }

        juce::Colour textCol;
        if (! isActive) textCol = theme.accent().withAlpha(0.5f);
        else if (isHighlighted) textCol = theme.fixedBase();
        else if (isTicked) textCol = theme.cyan();
        else textCol = theme.accent();

        if (isHighlighted && isActive)
        {
            g.setColour(theme.accent());
            g.fillRoundedRectangle(area.reduced(2).toFloat(), 3.0f);
        }

        g.setColour(textCol);
        g.setFont(getPopupMenuFont());
        auto r = area.reduced(6);
        const bool isDeleteCurrentAction = (text == "delete current");

        if (isDeleteCurrentAction)
        {
            auto textArea = r.withTrimmedRight(16);
            g.drawFittedText(text, textArea, juce::Justification::centredLeft, 1);

            auto xArea = r.removeFromRight(14);
            g.setColour(isHighlighted ? theme.fixedBase().withAlpha(0.9f) : theme.cyan().withAlpha(0.9f));
            g.setFont(juce::Font(juce::FontOptions("Arial", 11.0f, juce::Font::bold)));
            g.drawFittedText("X", xArea, juce::Justification::centred, 1);
        }
        else
        {
            g.drawFittedText(text, r, juce::Justification::centredLeft, 1);
        }

        if (shortcut.isNotEmpty())
        {
            g.setColour(textCol.withAlpha(0.8f));
            g.drawFittedText(shortcut, r, juce::Justification::centredRight, 1);
        }
    }
};

// ---------------- UiLayout constants (from UiLayoutConstants.h) ----------------
namespace UiLayout
{
    static constexpr float kFontSmall = 10.0f;
    static constexpr float kFontMedium = 16.0f;
    static constexpr float kFontTooltip = 13.0f;
    static constexpr float kCornerRadius = 6.0f;
    static constexpr float kLedRadius = 6.0f;
    static constexpr float kBarWidth = 18.0f;
    static constexpr float kInnerReduce = 6.0f;

    static constexpr float kBackdropMultiplier = 1.25f;
    static constexpr float kBackdropBaseSize = 55.55f * kBackdropMultiplier;

    static constexpr std::array<float, 8> kBackdropSizes = [] {
        std::array<float, 8> a{};
        a[7] = kBackdropBaseSize;
        for (int i = 6; i >= 0; --i)
            a[i] = a[i + 1] * kBackdropMultiplier;
        return a;
    }();

    static constexpr std::array<float, 8> kBackdropPulseScales = { { 0.12f, 0.12f, 0.12f, 0.12f, 0.12f, 0.12f, 0.12f, 0.12f } };
    static constexpr float kBackdropPulseDecay = 0.933f;
    static constexpr float kBackdropMinScale = 0.90f;
    static constexpr float kBackdropMaxScale = 1.2f;
    static constexpr float kBackdropPulseDurationMs = 333.333f;
}

// Pattern geometry constants (previously in PatternGeometry.h)
namespace UiLayout
{
    static constexpr float kPatternInset = 8.0f;            // inward inset from ring inner radius
    static constexpr float kPatternOutwardShift = 5.0f;     // net outward visual shift applied
    static constexpr float kPatternThicknessRatio = 0.775f; // inner radius = outer * ratio
}

// Backwards-compatibility mapping for code that referenced PatternGeometry::*
namespace PatternGeometry {
    static constexpr float kPatternInset = UiLayout::kPatternInset;
    static constexpr float kPatternOutwardShift = UiLayout::kPatternOutwardShift;
    static constexpr float kPatternThicknessRatio = UiLayout::kPatternThicknessRatio;
}
