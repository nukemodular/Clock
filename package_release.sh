#!/bin/bash
set -e

# Configuration
APP_NAME="${APP_NAME:-toolBoy Clock v3}"
FALLBACK_APP_NAME="${FALLBACK_APP_NAME:-Clock v3}"
DMG_NAME="${DMG_NAME:-toolBoy_Clock_v3_Installer.dmg}"
VOL_NAME="${VOL_NAME:-toolBoy Clock v3 Installer}"
BUILD_PRESET="${BUILD_PRESET:-release}"
ARTEFACTS_DIR="${ARTEFACTS_DIR:-build/release/ClockV3_artefacts/Release}"
STAGING_DIR="${STAGING_DIR:-release_dmg_temp}"

# Allow overriding via environment variables, and optionally via local id/ files.
# (Do NOT commit private keys/secrets.)
DEFAULT_IDENTITY="Developer ID Application: Dominik Bohn (3J6FW877T8)"
DEFAULT_NOTARY_PROFILE="toolboy-audio"

IDENTITY="${IDENTITY:-$DEFAULT_IDENTITY}"
NOTARY_PROFILE="${NOTARY_PROFILE:-$DEFAULT_NOTARY_PROFILE}"

# Optional directory containing helper text files:
# - codesign_id.txt
# - notary_profile.txt
# You can point this at a folder outside the repo to avoid committing anything.
ID_DIR="${ID_DIR:-id}"

if [ -f "$ID_DIR/codesign_id.txt" ] && [ "$IDENTITY" = "$DEFAULT_IDENTITY" ]; then
    IDENTITY="$(cat "$ID_DIR/codesign_id.txt")"
fi

if [ -f "$ID_DIR/notary_profile.txt" ] && [ "$NOTARY_PROFILE" = "$DEFAULT_NOTARY_PROFILE" ]; then
    NOTARY_PROFILE="$(cat "$ID_DIR/notary_profile.txt")"
fi

find_bundle_dir() {
    local format_dir="$1"
    local suffix="$2"

    if [ -d "${ARTEFACTS_DIR}/${format_dir}/${APP_NAME}.${suffix}" ]; then
        printf '%s' "${ARTEFACTS_DIR}/${format_dir}/${APP_NAME}.${suffix}"
        return 0
    fi

    if [ -d "${ARTEFACTS_DIR}/${format_dir}/${FALLBACK_APP_NAME}.${suffix}" ]; then
        printf '%s' "${ARTEFACTS_DIR}/${format_dir}/${FALLBACK_APP_NAME}.${suffix}"
        return 0
    fi

    return 1
}

find_bundle_binary() {
    local bundle_path="$1"
    local bundle_name
    bundle_name="$(basename "$bundle_path")"
    bundle_name="${bundle_name%.*}"
    printf '%s' "$bundle_path/Contents/MacOS/$bundle_name"
}

verify_universal_binary() {
    local binary_path="$1"
    local archs

    archs="$(lipo -archs "$binary_path")"
    echo "Verified architectures for $(basename "$binary_path"): $archs"

    if [[ "$archs" != *"arm64"* ]] || [[ "$archs" != *"x86_64"* ]]; then
        echo "Error: expected universal binary with arm64 and x86_64: $binary_path"
        exit 1
    fi
}

echo "Starting packaging process..."

echo "Configuring release preset..."
cmake --preset "$BUILD_PRESET"

echo "Building universal release..."
cmake --build --preset "$BUILD_PRESET"

# Clean staging
rm -rf "$STAGING_DIR"
mkdir -p "$STAGING_DIR"

# Copy artifacts
echo "Copying artifacts..."
AU_SOURCE="$(find_bundle_dir "AU" "component")"
VST3_SOURCE="$(find_bundle_dir "VST3" "vst3")"

if [ -z "$AU_SOURCE" ]; then
    echo "Error: AU component not found in release artefacts."
    exit 1
fi

if [ -z "$VST3_SOURCE" ]; then
    echo "Error: VST3 bundle not found in release artefacts."
    exit 1
fi

cp -R "$AU_SOURCE" "$STAGING_DIR/$APP_NAME.component"
cp -R "$VST3_SOURCE" "$STAGING_DIR/$APP_NAME.vst3"

AU_BINARY="$(find_bundle_binary "$STAGING_DIR/$APP_NAME.component")"
VST3_BINARY="$(find_bundle_binary "$STAGING_DIR/$APP_NAME.vst3")"

verify_universal_binary "$AU_BINARY"
verify_universal_binary "$VST3_BINARY"

# Create symlinks to /Library/Audio/Plug-Ins/...
echo "Creating symlinks..."
ln -s "/Library/Audio/Plug-Ins" "$STAGING_DIR/Plug-Ins"
ln -s "/Library/Audio/Plug-Ins/Components" "$STAGING_DIR/Components"
ln -s "/Library/Audio/Plug-Ins/VST3" "$STAGING_DIR/VST3"

cat > "$STAGING_DIR/INSTALL.txt" <<EOF
${APP_NAME} install

1. Drag ${APP_NAME}.component onto the Components link.
2. Drag ${APP_NAME}.vst3 onto the VST3 link.
3. Or open the Plug-Ins link and copy the bundles manually.

Formats included:
- Audio Unit: Components
- VST3: VST3
EOF

# Codesign
echo "Codesigning binaries..."
codesign --force --deep --options runtime --sign "$IDENTITY" --timestamp "$STAGING_DIR/$APP_NAME.component"
codesign --force --deep --options runtime --sign "$IDENTITY" --timestamp "$STAGING_DIR/$APP_NAME.vst3"
codesign --verify --deep --strict --verbose=2 "$STAGING_DIR/$APP_NAME.component"
codesign --verify --deep --strict --verbose=2 "$STAGING_DIR/$APP_NAME.vst3"

# Create DMG
echo "Creating DMG..."
rm -f "$DMG_NAME"
hdiutil create -volname "$VOL_NAME" -srcfolder "$STAGING_DIR" -ov -format UDZO "$DMG_NAME"

# Sign DMG
echo "Signing DMG..."
codesign --force --sign "$IDENTITY" --timestamp "$DMG_NAME"
codesign --verify --strict --verbose=2 "$DMG_NAME"

# Notarize
echo "Notarizing DMG (using profile '$NOTARY_PROFILE')..."
if xcrun notarytool submit "$DMG_NAME" --keychain-profile "$NOTARY_PROFILE" --wait; then
    echo "Notarization successful."
    echo "Stapling ticket..."
    xcrun stapler staple "$DMG_NAME"
    echo "Validating stapled DMG..."
    xcrun stapler validate "$DMG_NAME"
    if ! spctl -a -vv -t open "$DMG_NAME"; then
        echo "Warning: spctl open assessment returned a non-fatal result for the DMG."
    fi
else
    echo "Notarization failed. Please check your keychain profile '$NOTARY_PROFILE'."
    echo "Create (or overwrite) the profile with: xcrun notarytool store-credentials '$NOTARY_PROFILE' ..."
    echo "See available options via: xcrun notarytool help store-credentials"
    # Do not exit with error, just warn, so the DMG is still available
fi

# Cleanup
rm -rf "$STAGING_DIR"

echo "Packaging complete: $DMG_NAME"
