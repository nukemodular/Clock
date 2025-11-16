cmake_minimum_required(VERSION 3.21)

project(ClockSync VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# JUCE should be available as a submodule at ./JUCE
if (NOT EXISTS "${CMAKE_SOURCE_DIR}/JUCE/CMakeLists.txt")
    message(FATAL_ERROR "JUCE submodule not found at ${CMAKE_SOURCE_DIR}/JUCE. Please run 'git submodule update --init --recursive'.")
endif()

add_subdirectory(JUCE)

# Embed consolidated dancer SVG (layered file) only; individual per-frame assets removed.
juce_add_binary_data(DancerFrames
  NAMESPACE BinaryData
  SOURCES
    dancer/dancer_all.svg
    dancer/png/dancer_0.png
    dancer/png/dancer_1.png
    dancer/png/dancer_2.png
    dancer/png/dancer_3.png
    dancer/png/dancer_4.png
    dancer/png/dancer_5.png
    dancer/png/dancer_6.png
    dancer/png/dancer_7.png
    dancer/png/dancer_8.png
    dancer/png/dancer_9.png
    dancer/png/dancer_10.png
    dancer/png/dancer_11.png
    dancer/png/dancer_12.png
    dancer/png/dancer_13.png
    dancer/png/dancer_14.png
    dancer/png/dancer_15.png
    dancer/png/dancer_16.png
    dancer/png/dancer_17.png
    dancer/png/dancer_18.png
    dancer/png/dancer_19.png
    dancer/png/dancer_20.png
    dancer/png/dancer_21.png
    dancer/png/dancer_22.png
    dancer/png/dancer_23.png
)

juce_add_plugin(ClockSync
    COMPANY_NAME "toolBoy"
    PRODUCT_NAME "toolBoy Clock v3"
    BUNDLE_ID com.toolBoy.clockVst3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE PLPR
    PLUGIN_CODE CLKS
    FORMATS VST3
    # Include Synth subcategory to improve host discovery under Instruments
    VST3_CATEGORIES "Toolbox;Instrument,MIDIEffect"
)


target_sources(ClockSync
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.h
)

target_compile_definitions(ClockSync
    PRIVATE
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(ClockSync
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_animation
        juce::juce_core
        DancerFrames
)

if (APPLE)
    # Set macOS bundle properties if needed
    set_target_properties(ClockSync PROPERTIES
        MACOSX_BUNDLE TRUE
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Developer ID Application: Dominik Bohn (3J6FW877T8)"  # Replace with your actual code signing identity
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "3J6FW877T8"  # Replace with your actual development team ID
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.toolBoy.clockVst3"
    )

    # Optional install rule: allows installing to system VST3 folder via
    #   cmake --install <build-dir> --component ClockSync_VST3 --prefix /Library
    install(TARGETS ClockSync
        BUNDLE DESTINATION "Audio/Plug-Ins/VST3"
        COMPONENT "toolBoy Clock v3")
endif()
