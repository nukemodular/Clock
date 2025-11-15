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

# Embed dancer SVG frames into binary for distribution (no runtime filesystem dependency)
juce_add_binary_data(DancerFrames
    SOURCES
        dancer/Dancer_01.svg
        dancer/Dancer_02.svg
        dancer/Dancer_03.svg
        dancer/Dancer_04.svg
        dancer/Dancer_05.svg
        dancer/Dancer_06.svg
        dancer/Dancer_07.svg
        dancer/Dancer_08.svg
        dancer/Dancer_09.svg
        dancer/Dancer_10.svg
        dancer/Dancer_11.svg
        dancer/Dancer_12.svg
        dancer/Dancer_13.svg
        dancer/Dancer_14.svg
        dancer/Dancer_15.svg
        dancer/Dancer_16.svg
        dancer/Dancer_17.svg
        dancer/Dancer_18.svg
        dancer/Dancer_19.svg
        dancer/Dancer_20.svg
        dancer/Dancer_21.svg
        dancer/Dancer_22.svg
        dancer/Dancer_23.svg
        dancer/Dancer_24.svg
        dancer/dancer_neu_all.svg
)

juce_add_plugin(ClockSync
    COMPANY_NAME "PlugProcess"
    PRODUCT_NAME "ClockSync"
    BUNDLE_ID com.plugprocess.clocksync
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
    VST3_CATEGORIES "Instrument Synth"
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
        juce::juce_core
    DancerFrames
)

if (APPLE)
    # Set macOS bundle properties if needed
    set_target_properties(ClockSync PROPERTIES
        MACOSX_BUNDLE TRUE
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
    )

    # Optional install rule: allows installing to system VST3 folder via
    #   cmake --install <build-dir> --component ClockSync_VST3 --prefix /Library
    install(TARGETS ClockSync
        BUNDLE DESTINATION "Audio/Plug-Ins/VST3"
        COMPONENT ClockSync_VST3)
endif()
