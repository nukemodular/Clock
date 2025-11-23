﻿#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// Lightweight one-time loader for layered dancer SVG frames.
// Parses groups with id starting with "Layer" or "layer" and creates a drawable per group.
// Colour scrubbing sets fill/stroke to accent (#FF4E5B). Parsing occurs once (first call).
namespace DancerFramesCache
{
    // Ensure frames are loaded; thread-safe and idempotent.
    void ensureLoaded();
    // Total number of frames (>=1 if loaded successfully).
    int getFrameCount();
    // Retrieve frame drawable pointer (may be nullptr if index invalid).
    juce::Drawable* getFrame(int index);
}
