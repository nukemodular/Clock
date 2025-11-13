#include "PluginEditor.h"
#include "PluginProcessor.h"

ClockSyncAudioProcessorEditor::ClockSyncAudioProcessorEditor(ClockSyncAudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processor(p)
{
    setResizable(false, false);
    setSize(360, 160);

    // Resolution
    resolutionBox.addItemList({"24", "48", "96"}, 1);
    resolutionLabel.attachToComponent(&resolutionBox, true);
    addAndMakeVisible(resolutionBox);
    addAndMakeVisible(resolutionLabel);

    // Click toggle
    addAndMakeVisible(clickToggle);

    // Click level
    clickLevelSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    clickLevelSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
    clickLevelSlider.setRange(-12.0, 0.0, 0.1);
    addAndMakeVisible(clickLevelSlider);
    addAndMakeVisible(clickLevelLabel);
    clickLevelLabel.setJustificationType(juce::Justification::centred);

    auto& apvts = processor.getAPVTS();
    resolutionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(apvts, ClockSyncAudioProcessor::paramClockResolution, resolutionBox);
    clickEnableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, ClockSyncAudioProcessor::paramClickEnable, clickToggle);
    clickLevelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, ClockSyncAudioProcessor::paramClickLevelDb, clickLevelSlider);
}

void ClockSyncAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(16.0f, juce::Font::bold));
    g.drawFittedText("ClockSync", getLocalBounds().removeFromTop(24), juce::Justification::centred, 1);
}

void ClockSyncAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced(12);
    r.removeFromTop(28); // title area

    auto row = r.removeFromTop(28);
    resolutionBox.setBounds(row.removeFromLeft(120));
    clickToggle.setBounds(row.removeFromLeft(120));

    auto center = r.reduced(20);
    clickLevelLabel.setBounds(center.removeFromTop(18));
    clickLevelSlider.setBounds(center.withSizeKeepingCentre(120, 90));
}
