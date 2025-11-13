#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class ClockSyncAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit ClockSyncAudioProcessorEditor(ClockSyncAudioProcessor&);
    ~ClockSyncAudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    ClockSyncAudioProcessor& processor;

    // UI components
    juce::ComboBox resolutionBox;
    juce::ToggleButton clickToggle { "Audio Click" };
    juce::Slider clickLevelSlider;
    juce::Label resolutionLabel { {}, "Resolution" };
    juce::Label clickLevelLabel { {}, "Click Level (dB)" };

    // Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> resolutionAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> clickEnableAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> clickLevelAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClockSyncAudioProcessorEditor)
};
