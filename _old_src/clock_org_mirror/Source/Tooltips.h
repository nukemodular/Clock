#pragma once

#include <juce_core/juce_core.h>
#include <unordered_map>

// Centralised tooltip registry. Add or edit keys here to change tooltip
// text shown throughout the editor. Keys are referenced from UI code so
// components only need to know the key name.
static inline std::unordered_map<juce::String, juce::String> getTooltipTextRegistry()
{
    using Map = std::unordered_map<juce::String, juce::String>;
    Map m;
    m["refreshButton"]   = "Refresh MIDI device list";
    m["deviceBox"]       = "Select external MIDI output device";
    m["nameBox"]         = "Choose or create a saved instrument name";
    m["nameMidiSwitch"]  = "Toggle between Name and MIDI device selector";
    m["clickButton"]     = "Toggle between sample click or 1ms pulse";
    m["clickLevelSlider"] = "Click rate off, beat, 8th, 16th, 24ppq";
    m["triggerModeToggle"] = "If enabled , trigger causes bar restart at next bar + offset-step";
    m["idleClockToggle"] = "When enabled, clock continues sending F8 while stopped";
    m["shuffleScaleToggle"] = "Switching shuffle mode. Off: TR-909 style, On: linear ";
    m["stepOffsetMenu"]  = "Offset-step, sets step which re-start occur (1..16), relative to bar start";
    m["gridScaleMenu"]   = "Set clock division ( 1/32, 1/16, 1/8, 1/4 )";
    m["shuffleModeMenu"] = "Set shuffle intensity (1..7) or (50% to 75%)";
    m["helpToggle"]      = "Show tooltips ";
    m["runButton"]       = "Toggle Run (Start/Stop)";
    m["triggerArea"]     = "Trigger-start: click to retrigger start and to arm and fire next bar ( + offset-step )";
    // Add more keys as needed here.
    return m;
}
