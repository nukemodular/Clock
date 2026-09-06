// UiAssets.h
// Consolidated asset helpers: SvgUtils + DancerFramesCache
#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>



namespace DancerFramesCache
{
    void ensureLoaded();
    int getFrameCount();
    juce::Drawable* getFrame(int index);
}
