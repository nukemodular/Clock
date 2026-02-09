cmake_minimum_required(VERSION 3.21)

if (APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum OS X deployment version")
endif()

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
    assets/dancer_fix.svg
    assets/setup.svg
)


juce_add_plugin(ClockV3
    COMPANY_NAME "toolBoy"
    PRODUCT_NAME "toolBoy Clock v3"
    BUNDLE_ID com.toolboy.clock_v3
    # Build as an effect (FX) so hosts classify it as an audio effect
    IS_SYNTH FALSE
    # Typically effects don't need MIDI; enable if you require MIDI control
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE PLPR
    PLUGIN_CODE CLKS
    FORMATS VST3 AU
    # Plugin categories: use Fx for effect plugins
    VST3_CATEGORIES "Toolbox;Fx;Utilities;Clock"
)


target_sources(ClockV3
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.h
        Source/UiTheme.cpp
        Source/UiTheme.h
        Source/SvgDancerComponent.cpp
        Source/SvgDancerComponent.h
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

# Include generated headers for embedded DancerFrames BinaryData (includes setup.svg now)
target_include_directories(ClockV3 PRIVATE ${CMAKE_BINARY_DIR}/juce_binarydata_DancerFrames/JuceLibraryCode)

# Generate and include build info (increments build_number.txt at build time)
add_custom_target(increment_build ALL
    COMMAND ${CMAKE_COMMAND} -DPLUGIN_VERSION=${PLUGIN_VERSION} -P ${CMAKE_SOURCE_DIR}/cmake/increment_build.cmake
    COMMENT "Incrementing build number and generating build_info.h"
)

# Ensure the plugin target depends on the increment step and can include generated headers
add_dependencies(ClockV3 increment_build)
target_include_directories(ClockV3 PRIVATE ${CMAKE_BINARY_DIR}/generated)

# Ensure assets (setup.svg) are available at runtime by copying to the build directory
add_custom_target(copy_assets ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/assets
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/assets/setup.svg ${CMAKE_BINARY_DIR}/assets/setup.svg
    BYPRODUCTS ${CMAKE_BINARY_DIR}/assets/setup.svg
    COMMENT "Copying assets (setup.svg) to build directory"
)

# Make main targets depend on assets copy so the file is present when running
add_dependencies(ClockV3 copy_assets)

if (APPLE)
    # Set macOS bundle properties if needed
    set_target_properties(ClockV3 PROPERTIES
        MACOSX_BUNDLE TRUE
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Developer ID Application: Dominik Bohn (3J6FW877T8)"  # Replace with your actual code signing identity
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "3J6FW877T8"  # Replace with your actual development team ID
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.toolboy.clock_v3"
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


