#!/bin/bash
set -e

# Configuration
APP_NAME="toolBoy Clock v3"
DMG_NAME="toolBoy_Clock_v3_Installer.dmg"
VOL_NAME="toolBoy Clock v3 Installer"
IDENTITY="Developer ID Application: Dominik Bohn (3J6FW877T8)"
NOTARY_PROFILE="toolboy-audio" # Replace with your actual profile name if different
STAGING_DIR="release_dmg_temp"

echo "Starting packaging process..."

# Clean staging
rm -rf "$STAGING_DIR"
mkdir -p "$STAGING_DIR"

# Copy artifacts
echo "Copying artifacts..."
if [ -d "build/release/ClockV3_artefacts/Release/AU/$APP_NAME.component" ]; then
    cp -R "build/release/ClockV3_artefacts/Release/AU/$APP_NAME.component" "$STAGING_DIR/"
else
    echo "Error: AU component not found at build/release/ClockV3_artefacts/Release/AU/$APP_NAME.component"
    exit 1
fi

if [ -d "build/release/ClockV3_artefacts/Release/VST3/$APP_NAME.vst3" ]; then
    cp -R "build/release/ClockV3_artefacts/Release/VST3/$APP_NAME.vst3" "$STAGING_DIR/"
else
    echo "Error: VST3 bundle not found at build/release/ClockV3_artefacts/Release/VST3/$APP_NAME.vst3"
    exit 1
fi

# Create symlinks to /Library/Audio/Plug-Ins/...
echo "Creating symlinks..."
ln -s "/Library/Audio/Plug-Ins/Components" "$STAGING_DIR/Components"
ln -s "/Library/Audio/Plug-Ins/VST3" "$STAGING_DIR/VST3"

# Codesign
echo "Codesigning binaries..."
codesign --force --deep --options runtime --sign "$IDENTITY" --timestamp "$STAGING_DIR/$APP_NAME.component"
codesign --force --deep --options runtime --sign "$IDENTITY" --timestamp "$STAGING_DIR/$APP_NAME.vst3"

# Create DMG
echo "Creating DMG..."
rm -f "$DMG_NAME"
hdiutil create -volname "$VOL_NAME" -srcfolder "$STAGING_DIR" -ov -format UDZO "$DMG_NAME"

# Sign DMG
echo "Signing DMG..."
codesign --force --sign "$IDENTITY" --timestamp "$DMG_NAME"

# Notarize
echo "Notarizing DMG (using profile '$NOTARY_PROFILE')..."
if xcrun notarytool submit "$DMG_NAME" --keychain-profile "$NOTARY_PROFILE" --wait; then
    echo "Notarization successful."
    echo "Stapling ticket..."
    xcrun stapler staple "$DMG_NAME"
else
    echo "Notarization failed. Please check your keychain profile '$NOTARY_PROFILE'."
    echo "You can list profiles with: xcrun notarytool list-profiles"
    # Do not exit with error, just warn, so the DMG is still available
fi

# Cleanup
rm -rf "$STAGING_DIR"

echo "Packaging complete: $DMG_NAME"
