#!/bin/bash
set -e

APP_NAME="Clock v3 Demo" \
FALLBACK_APP_NAME="Clock v3 Demo" \
DMG_NAME="toolBoy_Clock_v3_Demo_Installer.dmg" \
VOL_NAME="toolBoy Clock v3 Demo Installer" \
BUILD_PRESET="release-demo" \
ARTEFACTS_DIR="build/release_demo/ClockV3Demo_artefacts/Release" \
STAGING_DIR="release_demo_dmg_temp" \
"$(dirname "$0")/package_release.sh"