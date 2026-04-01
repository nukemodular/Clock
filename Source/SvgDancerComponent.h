#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class SvgDancerComponent : public juce::Component
{
public:
    SvgDancerComponent();
    ~SvgDancerComponent() override = default;

    void setFrame(int frameIndex);
    void paint(juce::Graphics& g) override;
    void resized() override;
    void setTint(juce::Colour c);

private:
    int currentFrame = 0;
    std::vector<std::unique_ptr<juce::Drawable>> tintedFrames;
    juce::Colour tintColour = juce::Colours::transparentBlack;

    void rebuildTintedFrames();
};