cmake_minimum_required(VERSION 3.21)

if (APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum OS X deployment version")
endif()

project(ClockV3 VERSION 0.1.0 LANGUAGES C CXX)

option(CLOCKV3_DEMO_BUILD "Build the 30-minute demo variant instead of the full plugin" OFF)

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

# Embed per-frame dancer SVGs and setup art.
juce_add_binary_data(DancerFrames
  NAMESPACE BinaryData
  SOURCES
        assets/Dancer_frames/Dancer_1.svg
        assets/Dancer_frames/Dancer_2.svg
        assets/Dancer_frames/Dancer_3.svg
        assets/Dancer_frames/Dancer_4.svg
        assets/Dancer_frames/Dancer_5.svg
        assets/Dancer_frames/Dancer_6.svg
        assets/Dancer_frames/Dancer_7.svg
        assets/Dancer_frames/Dancer_8.svg
        assets/Dancer_frames/Dancer_9.svg
        assets/Dancer_frames/Dancer_10.svg
        assets/Dancer_frames/Dancer_11.svg
        assets/Dancer_frames/Dancer_12.svg
        assets/Dancer_frames/Dancer_13.svg
        assets/Dancer_frames/Dancer_14.svg
        assets/Dancer_frames/Dancer_15.svg
        assets/Dancer_frames/Dancer_16.svg
        assets/Dancer_frames/Dancer_17.svg
        assets/Dancer_frames/Dancer_18.svg
        assets/Dancer_frames/Dancer_19.svg
        assets/Dancer_frames/Dancer_20.svg
        assets/Dancer_frames/Dancer_21.svg
        assets/Dancer_frames/Dancer_22.svg
        assets/Dancer_frames/Dancer_23.svg
        assets/Dancer_frames/Dancer_24.svg
    assets/help.svg
    assets/setup.svg
    assets/toolboy_clock.svg
    assets/shadow.png
)


set(CLOCKV3_TARGET_NAME "ClockV3")
set(CLOCKV3_PRODUCT_NAME "Clock v3")
set(CLOCKV3_BUNDLE_ID "com.toolboy.clock")
set(CLOCKV3_PLUGIN_CODE "CLKS")
set(CLOCKV3_COMPONENT_PREFIX "Clock v3")
set(CLOCKV3_BUILD_NUMBER_FILE "${CMAKE_SOURCE_DIR}/build_number.txt")

if (CLOCKV3_DEMO_BUILD)
    set(CLOCKV3_TARGET_NAME "ClockV3Demo")
    set(CLOCKV3_PRODUCT_NAME "Clock v3 Demo")
    set(CLOCKV3_BUNDLE_ID "com.toolboy.clock")
    set(CLOCKV3_PLUGIN_CODE "CLKD")
    set(CLOCKV3_COMPONENT_PREFIX "Clock v3 Demo")
    set(CLOCKV3_BUILD_NUMBER_FILE "${CMAKE_SOURCE_DIR}/build_number_demo.txt")
endif()

set(CLOCKV3_COMMON_SOURCES
    Source/DancerFramesCache.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.h
    Source/UiTheme.cpp
    Source/UiTheme.h
    Source/SvgDancerComponent.cpp
    Source/SvgDancerComponent.h
    Source/PlaygroundComponent.cpp
)

juce_add_plugin(${CLOCKV3_TARGET_NAME}
    COMPANY_NAME "toolBoy"
    PRODUCT_NAME "${CLOCKV3_PRODUCT_NAME}"
    BUNDLE_ID ${CLOCKV3_BUNDLE_ID}
    # Build as an effect (FX) so hosts classify it as an audio effect
    IS_SYNTH FALSE
    # Typically effects don't need MIDI; enable if you require MIDI control
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    NEEDS_WINDOWS_MIDI_SERVICES TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE TOOL
    PLUGIN_CODE ${CLOCKV3_PLUGIN_CODE}
    FORMATS VST3 AU
    # Plugin categories: use Fx for effect plugins
    VST3_CATEGORIES "Toolbox;Fx;Utilities;Clock"
)


if (NOT CLOCKV3_DEMO_BUILD)
    list(APPEND CLOCKV3_COMMON_SOURCES
        Source/GumroadLicenseValidator.cpp
        Source/GumroadLicenseValidator.h
        Source/LicenseDialog.cpp
        Source/LicenseDialog.h
        Source/LicenseManager.cpp
        Source/LicenseManager.h
    )
endif()

target_sources(${CLOCKV3_TARGET_NAME} PRIVATE ${CLOCKV3_COMMON_SOURCES})

target_compile_definitions(${CLOCKV3_TARGET_NAME}
    PRIVATE
        CLOCKV3_DEMO=$<BOOL:${CLOCKV3_DEMO_BUILD}>
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(${CLOCKV3_TARGET_NAME}
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_animation
        $<$<NOT:$<BOOL:${CLOCKV3_DEMO_BUILD}>>:juce::juce_cryptography>
        juce::juce_core
    DancerFrames
)

# Include generated headers for embedded DancerFrames BinaryData (includes setup.svg now)
target_include_directories(${CLOCKV3_TARGET_NAME} PRIVATE ${CMAKE_BINARY_DIR}/juce_binarydata_DancerFrames/JuceLibraryCode)

# Generate and include build info (increments build_number.txt at build time)
add_custom_target(increment_build ALL
    COMMAND ${CMAKE_COMMAND} -DPLUGIN_VERSION=${PLUGIN_VERSION} -DBUILD_NUM_FILE=${CLOCKV3_BUILD_NUMBER_FILE} -P ${CMAKE_SOURCE_DIR}/cmake/increment_build.cmake
    COMMENT "Incrementing build number and generating build_info.h"
)

# Ensure the plugin target depends on the increment step and can include generated headers
add_dependencies(${CLOCKV3_TARGET_NAME} increment_build)
target_include_directories(${CLOCKV3_TARGET_NAME} PRIVATE ${CMAKE_BINARY_DIR}/generated)

# Ensure assets (setup.svg) are available at runtime by copying to the build directory
add_custom_target(copy_assets ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/assets
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/assets/setup.svg ${CMAKE_BINARY_DIR}/assets/setup.svg
    BYPRODUCTS ${CMAKE_BINARY_DIR}/assets/setup.svg
    COMMENT "Copying assets (setup.svg) to build directory"
)

# Make main targets depend on assets copy so the file is present when running
add_dependencies(${CLOCKV3_TARGET_NAME} copy_assets)

if (APPLE)
    # Set macOS bundle properties if needed
    set_target_properties(${CLOCKV3_TARGET_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Developer ID Application: Dominik Bohn (3J6FW877T8)"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "3J6FW877T8"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${CLOCKV3_BUNDLE_ID}"
    )

    # Optional install rules: allows installing to system VST3/AU folders via
    #   cmake --install <build-dir> --component <component-name> --prefix /Library
    install(TARGETS ${CLOCKV3_TARGET_NAME}
        BUNDLE DESTINATION "Audio/Plug-Ins/VST3"
        COMPONENT "${CLOCKV3_COMPONENT_PREFIX}_vst3")

    # Install the AU component bundle into the standard Components location
    install(TARGETS ${CLOCKV3_TARGET_NAME}
        BUNDLE DESTINATION "Audio/Plug-Ins/Components"
        COMPONENT "${CLOCKV3_COMPONENT_PREFIX}_au")

    # JUCE target names
    set(VST3_TARGET "${CLOCKV3_TARGET_NAME}_VST3")
    set(AU_TARGET   "${CLOCKV3_TARGET_NAME}_AU")
    set(USER_PLUGIN_FOLDER "$ENV{HOME}/Library/Audio/Plug-Ins")
    set(ENTITLEMENTS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/plugin.entitlements")

    set(APPLE_SIGNING_IDENTITY "Developer ID Application: Dominik Bohn (3J6FW877T8)")
    set(APPLE_INSTALLER_IDENTITY "Developer ID Installer: Dominik Bohn (3J6FW877T8)")
    set(APPLE_NOTARY_PROFILE "toolboy-audio.com")

    # 1. Local copy and codesigning for VST3
    add_custom_command(TARGET ${VST3_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${USER_PLUGIN_FOLDER}/VST3"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_BUNDLE_DIR:${VST3_TARGET}>" "${USER_PLUGIN_FOLDER}/VST3/${CLOCKV3_PRODUCT_NAME}.vst3"
        COMMAND codesign --force --deep --options runtime --timestamp 
                         --entitlements "${ENTITLEMENTS_PATH}" 
                         --sign "${APPLE_SIGNING_IDENTITY}" 
                         "${USER_PLUGIN_FOLDER}/VST3/${CLOCKV3_PRODUCT_NAME}.vst3"
        COMMENT "Kopiere und signiere VST3 mit Hardened Runtime im Zielordner"
        VERBATIM
    )

    # 2. Local copy and codesigning for AU (Components)
    add_custom_command(TARGET ${AU_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${USER_PLUGIN_FOLDER}/Components"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_BUNDLE_DIR:${AU_TARGET}>" "${USER_PLUGIN_FOLDER}/Components/${CLOCKV3_PRODUCT_NAME}.component"
        COMMAND codesign --force --deep --options runtime --timestamp 
                         --entitlements "${ENTITLEMENTS_PATH}" 
                         --sign "${APPLE_SIGNING_IDENTITY}" 
                         "${USER_PLUGIN_FOLDER}/Components/${CLOCKV3_PRODUCT_NAME}.component"
        COMMENT "Kopiere und signiere AU mit Hardened Runtime im Zielordner"
        VERBATIM
    )

    # =============================================================================
    # AUTOMATISCHE INSTALLER-ERSTELLUNG & NOTARISIERUNG (.PKG)
    # =============================================================================
    set(PKG_STAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}/PKG_Stage")
    set(PKG_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/Final_Installer")
    set(COMPONENT_PKG_DIR "${CMAKE_CURRENT_BINARY_DIR}/PKG_Component")
    set(COMPONENT_PKG "${COMPONENT_PKG_DIR}/${CLOCKV3_PRODUCT_NAME}_Component.pkg")
    set(UNSIGNED_PKG "${PKG_OUTPUT_DIR}/${CLOCKV3_PRODUCT_NAME}_Unsigned.pkg")
    set(FINAL_PKG "${PKG_OUTPUT_DIR}/${CLOCKV3_PRODUCT_NAME} Installer.pkg")

    # Installer Resources & Distribution
    set(INSTALLER_RES_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/installer_resources")
    set(INSTALLER_RES_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/installer_resources")
    set(DISTRIBUTION_XML "${INSTALLER_RES_BUILD_DIR}/distribution.xml")

    add_custom_target(Notarize
        # 1. Bereinigen und Ordnerstrukturen für den Installer spiegeln
        COMMAND ${CMAKE_COMMAND} -E rm -r -f "${PKG_STAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E rm -r -f "${COMPONENT_PKG_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PKG_STAGE_DIR}/Library/Audio/Plug-Ins/VST3"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PKG_STAGE_DIR}/Library/Audio/Plug-Ins/Components"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${COMPONENT_PKG_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PKG_OUTPUT_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${INSTALLER_RES_BUILD_DIR}"

        # 2. Installer-Ressourcen vorbereiten (Grafiken kopieren & XML/HTML konfigurieren)
        COMMAND ${CMAKE_COMMAND} -E copy "${INSTALLER_RES_SOURCE_DIR}/background.png" "${INSTALLER_RES_BUILD_DIR}/background.png"
        COMMAND ${CMAKE_COMMAND} -E copy "${INSTALLER_RES_SOURCE_DIR}/background-darkAqua.png" "${INSTALLER_RES_BUILD_DIR}/background-darkAqua.png"
        COMMAND ${CMAKE_COMMAND}
                -DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DBIN_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -DTB_PRODUCT_NAME="${CLOCKV3_PRODUCT_NAME}"
                -DPROJECT_VERSION=${PROJECT_VERSION}
                -DTB_BUNDLE_ID=${CLOCKV3_BUNDLE_ID}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/configure_installer_res.cmake

        # 3. Die frisch gebauten und codesignierten Plugins in die Installer-Staging-Area kopieren
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${USER_PLUGIN_FOLDER}/VST3/${CLOCKV3_PRODUCT_NAME}.vst3" "${PKG_STAGE_DIR}/Library/Audio/Plug-Ins/VST3/${CLOCKV3_PRODUCT_NAME}.vst3"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${USER_PLUGIN_FOLDER}/Components/${CLOCKV3_PRODUCT_NAME}.component" "${PKG_STAGE_DIR}/Library/Audio/Plug-Ins/Components/${CLOCKV3_PRODUCT_NAME}.component"
        COMMAND codesign --force --deep --options runtime --timestamp --entitlements "${ENTITLEMENTS_PATH}" --sign "${APPLE_SIGNING_IDENTITY}" "${PKG_STAGE_DIR}/Library/Audio/Plug-Ins/VST3/${CLOCKV3_PRODUCT_NAME}.vst3"
        COMMAND codesign --force --deep --options runtime --timestamp --entitlements "${ENTITLEMENTS_PATH}" --sign "${APPLE_SIGNING_IDENTITY}" "${PKG_STAGE_DIR}/Library/Audio/Plug-Ins/Components/${CLOCKV3_PRODUCT_NAME}.component"

        # 4. Das Component-Paket erstellen (.pkg)
        COMMAND pkgbuild --root "${PKG_STAGE_DIR}"
                         --identifier "${CLOCKV3_BUNDLE_ID}.pkg"
                         --version "${PROJECT_VERSION}"
                         --install-location "/"
                         "${COMPONENT_PKG}"

        # 5. Das finale gestaltete Installer-Paket mit Grafiken und Welcome-Page bauen
        COMMAND productbuild --distribution "${DISTRIBUTION_XML}"
                             --resources "${INSTALLER_RES_BUILD_DIR}"
                             --package-path "${COMPONENT_PKG_DIR}"
                             "${UNSIGNED_PKG}"

        # 6. Das Installer-Paket mit Apple-Installer-Zertifikat signieren
        COMMAND productsign --sign "${APPLE_INSTALLER_IDENTITY}"
                            --timestamp
                            "${UNSIGNED_PKG}"
                            "${FINAL_PKG}"
        
        # Temporäres unsigniertes Paket löschen
        COMMAND ${CMAKE_COMMAND} -E rm -f "${UNSIGNED_PKG}"

        # 7. Das fertige, voll-signierte Installer-Paket zur Notarisierung an Apple senden
        COMMAND xcrun notarytool submit "${FINAL_PKG}" 
                --keychain-profile "${APPLE_NOTARY_PROFILE}"
                --wait

        # 8. Jetzt heftet der Stapler das Ticket fehlerfrei an das signierte .pkg an!
        COMMAND xcrun stapler staple "${FINAL_PKG}"
        
        # 9. Kopiere finalen Installer in das Projekt-Hauptverzeichnis und dist/
        COMMAND ${CMAKE_COMMAND} -E copy "${FINAL_PKG}" "${CMAKE_CURRENT_SOURCE_DIR}/${CLOCKV3_PRODUCT_NAME} Installer.pkg"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/dist"
        COMMAND ${CMAKE_COMMAND} -E copy "${FINAL_PKG}" "${CMAKE_CURRENT_SOURCE_DIR}/dist/${CLOCKV3_PRODUCT_NAME} Installer.pkg"

        COMMENT "Erstelle personalisiertes Installer-Paket, signiere es und notarisiere das finale .pkg..."
        VERBATIM
    )
    
    # Abhängigkeiten definieren
    add_dependencies(Notarize ${VST3_TARGET} ${AU_TARGET})
endif()



