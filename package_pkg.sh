#!/usr/bin/env bash
set -euo pipefail

# ==============================================================================
# Clock v3 Installer Packaging & Notarization Script
# Matches the packaging architecture of seQOne and TR-909
# STRICT RULE: NEVER build with concurrency higher than -j2!
# ==============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="${SCRIPT_DIR}"

APP_IDENTITY="Developer ID Application: Dominik Bohn (3J6FW877T8)"
INSTALLER_IDENTITY="Developer ID Installer: Dominik Bohn (3J6FW877T8)"
NOTARY_PROFILE="toolboy-audio.com"
ENTITLEMENTS="${PROJECT_DIR}/plugin.entitlements"

DEMO_MODE=false
SKIP_NOTARIZE=false

for arg in "$@"; do
    case $arg in
        --demo)
            DEMO_MODE=true
            shift
            ;;
        --skip-notarize)
            SKIP_NOTARIZE=true
            shift
            ;;
        -h|--help)
            echo "Usage: ./package_pkg.sh [--demo] [--skip-notarize]"
            echo "  --demo           Build and package the 30-minute Demo version ('Clock v3 Demo')"
            echo "  --skip-notarize  Build, package, and sign the .pkg locally without submitting to Apple Notary"
            exit 0
            ;;
        *)
            ;;
    esac
done

if [ "$DEMO_MODE" = true ]; then
    BUILD_DIR="${PROJECT_DIR}/build/release_demo"
    TARGET_NAME="ClockV3Demo"
    PRODUCT_NAME="Clock v3 Demo"
    BUNDLE_ID="com.toolboy.clock"
    DEMO_FLAG="ON"
else
    BUILD_DIR="${PROJECT_DIR}/build/release"
    TARGET_NAME="ClockV3"
    PRODUCT_NAME="Clock v3"
    BUNDLE_ID="com.toolboy.clock"
    DEMO_FLAG="OFF"
fi

PROJECT_VERSION="0.1.0"
STAGE_DIR="${BUILD_DIR}/PKG_Stage"
COMPONENT_PKG_DIR="${BUILD_DIR}/PKG_Component"
COMPONENT_PKG="${COMPONENT_PKG_DIR}/${PRODUCT_NAME}_Component.pkg"
OUTPUT_DIR="${BUILD_DIR}/Final_Installer"
UNSIGNED_PKG="${OUTPUT_DIR}/${PRODUCT_NAME}_Unsigned.pkg"
FINAL_PKG="${OUTPUT_DIR}/${PRODUCT_NAME} Installer.pkg"
DIST_DIR="${PROJECT_DIR}/dist"
RES_SRC_DIR="${PROJECT_DIR}/installer_resources"
RES_BUILD_DIR="${BUILD_DIR}/installer_resources"

echo "===================================================================="
echo " Packaging: ${PRODUCT_NAME} (v${PROJECT_VERSION})"
echo " Build directory: ${BUILD_DIR}"
echo " Skip Notarize: ${SKIP_NOTARIZE}"
echo "===================================================================="

# 1. Configure and Build (STRICT RULE: concurrency <= -j2)
echo ">>> [1/7] Configuring and Building with CMake (-j2)..."
cmake -B "${BUILD_DIR}" -S "${PROJECT_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCLOCKV3_DEMO_BUILD="${DEMO_FLAG}"

cmake --build "${BUILD_DIR}" --config Release -j2

# 2. Prepare staging area
echo ">>> [2/7] Staging plugin bundles..."
rm -rf "${STAGE_DIR}" "${COMPONENT_PKG_DIR}" "${OUTPUT_DIR}" "${RES_BUILD_DIR}"
mkdir -p "${STAGE_DIR}/Library/Audio/Plug-Ins/VST3"
mkdir -p "${STAGE_DIR}/Library/Audio/Plug-Ins/Components"
mkdir -p "${COMPONENT_PKG_DIR}"
mkdir -p "${OUTPUT_DIR}"
mkdir -p "${DIST_DIR}"
mkdir -p "${RES_BUILD_DIR}"

VST3_SRC="${BUILD_DIR}/${TARGET_NAME}_artefacts/Release/VST3/${PRODUCT_NAME}.vst3"
AU_SRC="${BUILD_DIR}/${TARGET_NAME}_artefacts/Release/AU/${PRODUCT_NAME}.component"

if [ ! -d "${VST3_SRC}" ]; then
    echo "Error: VST3 bundle not found at ${VST3_SRC}" >&2
    exit 1
fi
if [ ! -d "${AU_SRC}" ]; then
    echo "Error: AU bundle not found at ${AU_SRC}" >&2
    exit 1
fi

cp -R "${VST3_SRC}" "${STAGE_DIR}/Library/Audio/Plug-Ins/VST3/${PRODUCT_NAME}.vst3"
cp -R "${AU_SRC}" "${STAGE_DIR}/Library/Audio/Plug-Ins/Components/${PRODUCT_NAME}.component"

# 3. Codesign plugin bundles with Hardened Runtime & Entitlements
echo ">>> [3/7] Codesigning staged plugin bundles with Developer ID Application..."
codesign --force --deep --options runtime --timestamp \
    --entitlements "${ENTITLEMENTS}" \
    --sign "${APP_IDENTITY}" \
    "${STAGE_DIR}/Library/Audio/Plug-Ins/VST3/${PRODUCT_NAME}.vst3"

codesign --force --deep --options runtime --timestamp \
    --entitlements "${ENTITLEMENTS}" \
    --sign "${APP_IDENTITY}" \
    "${STAGE_DIR}/Library/Audio/Plug-Ins/Components/${PRODUCT_NAME}.component"

# Verify bundle codesigning
codesign --verify --deep --strict --verbose=2 "${STAGE_DIR}/Library/Audio/Plug-Ins/VST3/${PRODUCT_NAME}.vst3"
codesign --verify --deep --strict --verbose=2 "${STAGE_DIR}/Library/Audio/Plug-Ins/Components/${PRODUCT_NAME}.component"

# 4. Prepare Installer Resources
echo ">>> [4/7] Generating installer graphics & scripts..."
cp "${RES_SRC_DIR}/background.png" "${RES_BUILD_DIR}/background.png"
cp "${RES_SRC_DIR}/background-darkAqua.png" "${RES_BUILD_DIR}/background-darkAqua.png"

cmake -DSRC_DIR="${PROJECT_DIR}" \
      -DBIN_DIR="${BUILD_DIR}" \
      -DTB_PRODUCT_NAME="${PRODUCT_NAME}" \
      -DPROJECT_VERSION="${PROJECT_VERSION}" \
      -DTB_BUNDLE_ID="${BUNDLE_ID}" \
      -P "${PROJECT_DIR}/scripts/configure_installer_res.cmake"

# 5. Build Component PKG & Distribution PKG
echo ">>> [5/7] Building Component and Distribution PKG..."
pkgbuild --root "${STAGE_DIR}" \
         --identifier "${BUNDLE_ID}.pkg" \
         --version "${PROJECT_VERSION}" \
         --install-location "/" \
         "${COMPONENT_PKG}"

productbuild --distribution "${RES_BUILD_DIR}/distribution.xml" \
             --resources "${RES_BUILD_DIR}" \
             --package-path "${COMPONENT_PKG_DIR}" \
             "${UNSIGNED_PKG}"

# 6. Sign PKG with Developer ID Installer
echo ">>> [6/7] Signing Distribution PKG with Developer ID Installer..."
productsign --sign "${INSTALLER_IDENTITY}" \
            --timestamp \
            "${UNSIGNED_PKG}" \
            "${FINAL_PKG}"

rm -f "${UNSIGNED_PKG}"

# 7. Notarize and Staple (unless skipped)
if [ "$SKIP_NOTARIZE" = false ]; then
    echo ">>> [7/7] Submitting to Apple Notary Service via '${NOTARY_PROFILE}'..."
    xcrun notarytool submit "${FINAL_PKG}" \
        --keychain-profile "${NOTARY_PROFILE}" \
        --wait

    echo ">>> Stapling notarization ticket..."
    xcrun stapler staple "${FINAL_PKG}"
    spctl -a -vv -t install "${FINAL_PKG}"
else
    echo ">>> [7/7] Skipping notarization as requested."
fi

# Copy final artifact to project root and dist/
ROOT_PKG="${PROJECT_DIR}/${PRODUCT_NAME} Installer.pkg"
DIST_PKG="${DIST_DIR}/${PRODUCT_NAME} Installer.pkg"
cp "${FINAL_PKG}" "${ROOT_PKG}"
cp "${FINAL_PKG}" "${DIST_PKG}"

# Verify PKG signature
pkgutil --check-signature "${ROOT_PKG}"

echo ""
echo "===================================================================="
echo " SUCCESS! Installer ready at:"
echo " -> ${ROOT_PKG}"
echo " -> ${DIST_PKG}"
echo "===================================================================="

