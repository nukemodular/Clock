#include <utility>
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "build_info.h"
#include "GridScaleMenu.h"
#include "BinaryData.h"
#include "LookAndFeels.h" // Use centralised LookAndFeel & theme colours
#include "Tooltips.h"
#include "UiLayoutConstants.h"
#include "LayoutOffsets.h"
#include <array>
#include <optional>

namespace
{
    // Derived theme colour variant (slightly lighter/darker base) while core colours come from UiThemeColours
    const juce::Colour kBaseLo  = UiThemeColours::base().darker(0.12f);

    // Arc size button definitions (absolute positions from canvas origin)
    // Previously positioned algorithmically with baseY/xStart/gap; converted to fixed rects for easier manual tweaking.
    // Each rect: {x, y, w, h}. Derived from former layout: sizes {22,25,28,31,34,37,40} and y-offsets {0,16,22,26,28,26,16} added to baseY=215.
    // Visual shallow arc preserved. Adjust these directly as needed.
    // juce::Rectangle isn't a literal type; use static const (not constexpr)
    // Global offset for non-excluded components (exclude: reset/refreshButton, deviceBox, header, LED, status text)
    // Requested shift: move content (excluding header) by x -5, y -10.
    const int offX = -20;
    const int offY = -40;

    static const std::array<juce::Rectangle<int>, 7> kArcButtonRects = {
        juce::Rectangle<int>( 107 + offX, 220 + offY, 22, 22),
        juce::Rectangle<int>(118 + offX, 230 + offY, 25, 25),
        juce::Rectangle<int>(131 + offX, 236 + offY, 28, 28),
        // Adjusted Y for value 4 button (index 3) to sit along the drag path and avoid being skipped (was 220)
        juce::Rectangle<int>(147 + offX, 239 + offY, 31, 31),
        juce::Rectangle<int>(166 + offX, 238 + offY, 34, 34),
        juce::Rectangle<int>(187 + offX, 231 + offY, 37, 37),
        juce::Rectangle<int>(204 + offX, 221 + offY, 40, 40)
    };
}

ClockSyncAudioProcessorEditor::ClockSyncAudioProcessorEditor(ClockSyncAudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processor(p)
{
    setResizable(false, false);
    setSize(300, 240);
    startTimerHz(60);

    // Component setup (colour, style, add, z-order)
    {
        gridScaleMenu.setColours(UiThemeColours::accent(), UiThemeColours::base(), UiThemeColours::cyan());
        shuffleModeMenu.setColours(UiThemeColours::accent(), UiThemeColours::base(), UiThemeColours::cyan());
        triggerModeToggle.setColours(UiThemeColours::accent(), UiThemeColours::base(), UiThemeColours::cyan());
        idleClockToggle.setColours(UiThemeColours::accent(), UiThemeColours::base(), UiThemeColours::cyan());
        shuffleScaleToggle.setColours(UiThemeColours::accent(), UiThemeColours::base(), UiThemeColours::cyan());
        stepOffsetMenu.setColours(UiThemeColours::accent(), UiThemeColours::base(), UiThemeColours::cyan());
        clickButton.setColours(UiThemeColours::accent(), UiThemeColours::cyan());

        clickLevelSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        clickLevelSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        clickRotaryLNF = std::make_unique<ClickRotaryLNF>();
        clickLevelSlider.setLookAndFeel(clickRotaryLNF.get());
        // Repurposed rotary: discrete 0..4 stages for click rate (0=off,1=4th,2=8th,3=16th,4=24ppq)
        clickLevelSlider.setRange(0, 4, 1);
        clickLevelSlider.setTooltip("Off / 4th / 8th / 16th / 24ppq");

        addAndMakeVisible(gridScaleMenu);
        addAndMakeVisible(shuffleModeMenu);
        addAndMakeVisible(deviceBox);
        addAndMakeVisible(refreshButton);
        addAndMakeVisible(nameBox);
        addAndMakeVisible(nameMidiSwitch);
        // attach a mouse listener so double-clicking the name combo opens an inline editor
        struct NameBoxMouseListener : public juce::MouseListener
        {
            ClockSyncAudioProcessorEditor* owner;
            NameBoxMouseListener(ClockSyncAudioProcessorEditor* o) : owner(o) {}
            void mouseDoubleClick(const juce::MouseEvent&) override { owner->toggleNameEditorOrCommit(); }
        };
        nameBox.addMouseListener(new NameBoxMouseListener(this), true);
        addAndMakeVisible(clickButton);
        addAndMakeVisible(clickLevelSlider);
        addAndMakeVisible(triggerModeToggle);
        addAndMakeVisible(idleClockToggle);
        addAndMakeVisible(shuffleScaleToggle);
        addAndMakeVisible(stepOffsetMenu);

            // Help toggle (small '?' square) - visible by default off; toggles tooltips
            addAndMakeVisible(helpToggle);
            helpToggle.setButtonText("?");
            helpToggle.setClickingTogglesState(true);
            // text colours: cyan when enabled, slightly darker cyan when disabled
            helpToggle.setColour(juce::TextButton::textColourOffId, UiThemeColours::cyan().darker(0.45f));
            helpToggle.setColour(juce::TextButton::textColourOnId, UiThemeColours::cyan());
                    // Create a tiny LookAndFeel so the help button only draws text (no background/borders)
                    struct HelpBtnLNF : public juce::LookAndFeel_V4
                    {
                        void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override {}
                        void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override
                        {
                            const float fontSize = UiLayout::kFontMedium;
                            // Show colour based on toggle state so the button clearly
                            // indicates whether help/tooltips are enabled.
                            if (b.getToggleState())
                                g.setColour(b.findColour(juce::TextButton::textColourOnId));
                            else
                                g.setColour(b.findColour(juce::TextButton::textColourOffId));
                            g.setFont(juce::Font(juce::FontOptions("Arial", (float)fontSize, juce::Font::bold)));
                            g.drawFittedText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, 1);
                        }
                    };
                    helpButtonLNF = std::make_unique<HelpBtnLNF>();
                    helpToggle.setLookAndFeel(helpButtonLNF.get());
                    helpToggle.setRepeatSpeed(0, 0);
                    helpToggle.onClick = [this]() { applyTooltips(helpToggle.getToggleState()); };
                    // ensure the component background is not painted by the editor
                    helpToggle.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
            helpToggle.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);

        gridScaleMenu.toBack();
        // Keep slider and its small click button in front so they are visually prominent
        clickLevelSlider.toFront(true);
        clickButton.toFront(true);
        idleClockToggle.toBack();
        stepOffsetMenu.toFront(true);
    }

    auto& apvts = processor.getAPVTS();

    // clickButton now toggles click variant (sample spike vs 1ms pulse)
    clickEnableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, ClockSyncAudioProcessor::paramClickPulse, clickButton);
    // clickLevelSlider repurposed to select click rate stages (0..4)
    clickLevelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ClockSyncAudioProcessor::paramClickRate, clickLevelSlider);
    idleClockAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, ClockSyncAudioProcessor::paramClockWhileStopped, idleClockToggle);
    triggerModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, ClockSyncAudioProcessor::paramTriggerModeEnabled, triggerModeToggle);

    // Name/Instrument combo setup (look-and-feel assigned after ThemeLNF is created)
    nameBox.setColour(juce::ComboBox::textColourId, UiThemeColours::cyan());
    nameBox.setJustificationType(juce::Justification::centred);
    // hide the arrow for nameBox so clicks are handled by overlay
    nameBox.setColour(juce::ComboBox::arrowColourId, juce::Colours::transparentBlack);
    // Ensure the internal text component does not allow editing (prevents extra text widgets)
    nameBox.setEditableText(false);
    // Load saved instrument names from state
    loadInstrumentNamesFromState();
    populateNameBox();

    // Default: NAME mode off => show deviceBox
    nameMidiSwitch.setToggleState(false, juce::dontSendNotification);
    nameMidiSwitch.setButtonText("NAME");
    nameMidiSwitch.onClick = [this]
    {
        const bool isOn = nameMidiSwitch.getToggleState();
        if (isOn) nameMidiSwitch.setButtonText("MIDI"); else nameMidiSwitch.setButtonText("NAME");
        // Show only the active combo and ensure z-order to avoid accidental overlap
        nameBox.setVisible(isOn);
        deviceBox.setVisible(! isOn);
        if (isOn)
        {
            nameBox.toFront(true);
            deviceBox.toBack();
        }
        else
        {
            deviceBox.toFront(true);
            nameBox.toBack();
        }
        // nothing else needed; editing trigger handled by double-click on the combo
    };
