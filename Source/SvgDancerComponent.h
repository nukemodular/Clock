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
    
    // Set the tint colour for the dancer
    void setTint(juce::Colour c);

private:
    std::unique_ptr<juce::Drawable> rootDrawable;
    std::vector<juce::Drawable*> layers; // Pointers to children of rootDrawable
    int currentFrame = 0;
    
    // Cache for rasterized frames
    std::vector<juce::Image> frameCache;
    bool useCache = true; // Enable caching for performance
    
    juce::Colour tintColour = juce::Colours::transparentBlack; // If transparent, no tint

    void parseLayers();
    juce::Image getCachedFrame(int index, int w, int h);
};
