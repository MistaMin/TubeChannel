#!/bin/bash
# SPDX-License-Identifier: MIT
# Builds, signs, notarizes and staples the macOS DMG for Tube Channel.
# Includes the signed .pkg installer, documentation, and all license files.
set -euo pipefail
export COPYFILE_DISABLE=1

cd "$(dirname "$0")/.."
VERSION=$(tr -d '[:space:]' < VERSION)

if [ -d "dist/release-$VERSION" ]; then
  OUT="dist/release-$VERSION"
else
  OUT="dist"
fi

PKG="$OUT/TubeChannel-$VERSION.pkg"
DMG="$OUT/TubeChannel-$VERSION.dmg"
STAGE="$OUT/dmg-stage"
APP_ID=${TC_APP_ID:-"Developer ID Application: Marcos Deida (ZQYU67JUBH)"}
NOTARY_PROFILE=${TC_NOTARY_PROFILE:-AC_NOTARY}

if [ ! -f "$PKG" ]; then
  echo "Error: Package $PKG does not exist. Run installer/build_installer.sh first." >&2
  exit 1
fi

echo "==> Preparing DMG staging directory"
rm -rf "$STAGE" "$DMG"
mkdir -p "$STAGE"

# 1. Installer package
cp "$PKG" "$STAGE/"

# 2. Documentation
mkdir -p "$STAGE/Documentation"
for doc in docs/USER_MANUAL.md docs/RELEASE_$VERSION.md; do
  if [ -f "$doc" ]; then
    cp "$doc" "$STAGE/Documentation/"
  fi
done

# Diagrams and UI screenshots
for asset in docs/signal-path.svg docs/ui-analogue.png docs/ui-digital.png \
             docs/ui-system-analogue.png docs/ui-system-digital.png \
             docs/offline-bounce-analogue.png docs/offline-bounce-digital.png; do
  if [ -f "$asset" ]; then
    cp "$asset" "$STAGE/Documentation/"
  fi
done

if [ -f "$OUT/TubeChannel-$VERSION-build-manifest.json" ]; then
  cp "$OUT/TubeChannel-$VERSION-build-manifest.json" "$STAGE/Documentation/release-manifest.json"
fi

# 3. Licenses
mkdir -p "$STAGE/Licenses"
cp LICENSE THIRD_PARTY_LICENSES.md "$STAGE/Licenses/"

# Third-party licenses tree
if [ -d "$OUT/stage/licenses/Tube Channel/Licenses" ]; then
  ditto "$OUT/stage/licenses/Tube Channel/Licenses" "$STAGE/Licenses/third-party"
elif [ -d "$OUT/stage/licenses/Tube Channel" ]; then
  ditto "$OUT/stage/licenses/Tube Channel" "$STAGE/Licenses/third-party"
fi

# 4. Root INSTALL instructions
cat > "$STAGE/INSTALL.txt" <<EOF
Tube Channel $VERSION — macOS Universal (Apple Silicon & Intel)
OpenGrid / Marcos Deida

INSTALLATION:
1. Double-click "TubeChannel-$VERSION.pkg" to launch the installer.
2. Follow the standard macOS installer steps (Administrator authorization required).
3. The installer installs the following formats:
   - VST3:        /Library/Audio/Plug-Ins/VST3/Tube Channel.vst3
   - Audio Unit:  /Library/Audio/Plug-Ins/Components/Tube Channel.component
   - CLAP:        /Library/Audio/Plug-Ins/CLAP/Tube Channel.clap
   - LV2:         /Library/Audio/Plug-Ins/LV2/Tube Channel.lv2
   - AAX:         /Library/Application Support/Avid/Audio/Plug-Ins/Tube Channel.aaxplugin
   - Standalone:  /Applications/Tube Channel.app
4. Restart your DAW and rescan plugins.

DOCUMENTATION:
Refer to the "Documentation" folder on this disk image for the User Manual,
Release Notes, and signal-flow diagrams.
Documentation and licenses are also installed to:
/Library/Application Support/Tube Channel

LICENSING:
- Tube Channel source code is MIT licensed (see Licenses/LICENSE).
- Official binaries are distributed as freeware under the author's commercial JUCE license (JUCE 9.0.3).
- See Licenses/THIRD_PARTY_LICENSES.md and Licenses/third-party for complete third-party component licenses and attributions (JUCE, GoodLookinUI, CLAP, VST3 SDK, LV2 SDK, Audio Unit SDK).
EOF

# Clean up unwanted metadata and attributes
xattr -cr "$STAGE" 2>/dev/null || true
find "$STAGE" -name '._*' -delete 2>/dev/null || true
find "$STAGE" -name '.DS_Store' -delete 2>/dev/null || true

echo "==> Creating disk image"
hdiutil create -volname "Tube Channel $VERSION" -srcfolder "$STAGE" -ov -format UDZO "$DMG"

echo "==> Signing DMG with Developer ID"
codesign --force --timestamp --sign "$APP_ID" "$DMG"
codesign --verify --verbose=2 "$DMG"

if [ "${NOTARIZE:-1}" = "1" ]; then
  echo "==> Submitting DMG for notarization"
  xcrun notarytool submit "$DMG" --keychain-profile "$NOTARY_PROFILE" --wait
  echo "==> Stapling notarization ticket to DMG"
  xcrun stapler staple "$DMG"
  xcrun stapler validate "$DMG"
  echo "==> Assessing DMG with Gatekeeper"
  spctl --assess --type install -v "$DMG" || true
fi

echo "==> Updating SHA-256 checksums"
(cd "$OUT" && shasum -a 256 "$(basename "$PKG")" "$(basename "$DMG")" > "TubeChannel-$VERSION.sha256")
cat "$OUT/TubeChannel-$VERSION.sha256"

# Clean up temporary staging
rm -rf "$STAGE"

echo "==> Successfully built, signed, and notarized $DMG"
