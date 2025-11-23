cmake_minimum_required(VERSION 3.21)

project(ClockV3 VERSION 0.1.0 LANGUAGES C CXX)

# Plugin version available to other CMake logic and the increment script
set(PLUGIN_VERSION ${PROJECT_VERSION})

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

juce_add_plugin(ClockV3
    COMPANY_NAME "toolBoy"
    PRODUCT_NAME "toolBoy Clock v3"
    BUNDLE_ID com.toolBoy.clockVst3
    # Build as an effect (FX) instead of a synth/instrument
    IS_SYNTH FALSE
    # Typically effects don't need MIDI; enable if you require MIDI control
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE PLPR
    PLUGIN_CODE CLKS
    FORMATS VST3 AU
    # Plugin categories: use Fx for effect plugins
    VST3_CATEGORIES "Toolbox;Fx"
)


target_sources(ClockV3
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.h
)

target_compile_definitions(ClockV3
    PRIVATE
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(ClockV3
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_animation
        juce::juce_core
        DancerFrames
)

# Generate and include build info (increments build_number.txt at build time)
add_custom_target(increment_build ALL
    COMMAND ${CMAKE_COMMAND} -DPLUGIN_VERSION=${PLUGIN_VERSION} -P ${CMAKE_SOURCE_DIR}/cmake/increment_build.cmake
    COMMENT "Incrementing build number and generating build_info.h"
)

# Ensure the plugin target depends on the increment step and can include generated headers
add_dependencies(ClockV3 increment_build)
target_include_directories(ClockV3 PRIVATE ${CMAKE_BINARY_DIR}/generated)

if (APPLE)
    # Set macOS bundle properties if needed
    set_target_properties(ClockV3 PROPERTIES
        MACOSX_BUNDLE TRUE
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Developer ID Application: Dominik Bohn (3J6FW877T8)"  # Replace with your actual code signing identity
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "3J6FW877T8"  # Replace with your actual development team ID
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.toolBoy.clockVst3"
    )

    # Optional install rules: allows installing to system VST3/AU folders via
    #   cmake --install <build-dir> --component <component-name> --prefix /Library
    install(TARGETS ClockV3
        BUNDLE DESTINATION "Audio/Plug-Ins/VST3"
        COMPONENT "toolBoy Clock v3_vst3")

    # Install the AU component bundle into the standard Components location
    install(TARGETS ClockV3
        BUNDLE DESTINATION "Audio/Plug-Ins/Components"
        COMPONENT "toolBoy Clock v3_au")
endif()


