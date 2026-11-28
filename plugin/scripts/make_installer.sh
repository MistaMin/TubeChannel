#!/bin/bash
# Builds the signed, notarized macOS installer for Tube Channel from plugin/dist/:
#   plugin/scripts/make_installer.sh
# 1. Copies VST3, Audio Unit, CLAP and LV2 to dist/signed and signs them with the
#    Developer ID Application identity (hardened runtime, secure timestamp). The AAX
#    must already be in dist/signed, PACE-signed (sign_aax.sh of the release skill).
# 2. One component package per format (the user picks them) and one for the manual
#    and licences (always installed, /Library/Application Support/Tube Channel).
# 3. Distribution package with the binary licence, signed with Developer ID Installer,
#    notarized with the notarytool profile and stapled.
# Environment: APP_ID, INSTALLER_ID, NOTARY_PROFILE (defaults below); NOTARIZE=0 skips
# the Apple submission. Writes plugin/dist/Tube-Channel-<version>-macos.pkg; installs nothing.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"   # the repository; the bundles are in plugin/dist
APP_ID="${APP_ID:-Developer ID Application: Marcos Deida (ZQYU67JUBH)}"
INSTALLER_ID="${INSTALLER_ID:-Developer ID Installer: Marcos Deida (ZQYU67JUBH)}"
NOTARY_PROFILE="${NOTARY_PROFILE:-AC_NOTARY}"
NOTARIZE="${NOTARIZE:-1}"
NAME="Tube Channel"
FILE=Tube-Channel                      # file names carry no space
LV2=TubeChannel
ID=com.OpenGrid.TubeChannel
VERSION="$(tr -d '[:space:]' < "$ROOT/VERSION")"
LABEL="$VERSION"
DIST="$ROOT/plugin/dist"
SIGNED="$DIST/signed"
WORK="$ROOT/plugin/target/installer"
OUT="$DIST/$FILE-$LABEL-macos.pkg"
MANUAL="$ROOT/output/pdf/$FILE-User-Manual.pdf"

[ -d "$SIGNED/$NAME.aaxplugin" ] || { echo "PACE-signed AAX missing: $SIGNED/$NAME.aaxplugin" >&2; exit 1; }
[ -f "$MANUAL" ] || { echo "manual missing: run tools/build_user_manual.py" >&2; exit 1; }
case "$WORK" in "$ROOT"/plugin/target/?*) ;; *) echo "bad work folder $WORK" >&2; exit 1 ;; esac
rm -rf "$WORK"
mkdir -p "$WORK/stage" "$WORK/components" "$WORK/plists" "$WORK/resources"

echo "== Developer ID signatures"
for b in "$NAME.vst3" "$NAME.component" "$NAME.clap" "$LV2.lv2"; do
  rm -rf "${SIGNED:?}/$b"
  COPYFILE_DISABLE=1 ditto --norsrc --noextattr --noqtn "$DIST/$b" "$SIGNED/$b"
done
# Code nested in a bundle (the CLAP's ARA library in Contents/Resources) is signed first, inside out.
nested() { find "$SIGNED/$NAME.vst3" "$SIGNED/$NAME.component" "$SIGNED/$NAME.clap" -name '*.dylib' -print0; }
nested | xargs -0 -n1 codesign --force --timestamp --options runtime --sign "$APP_ID"
for b in "$NAME.vst3" "$NAME.component" "$NAME.clap"; do
  codesign --force --timestamp --options runtime --sign "$APP_ID" "$SIGNED/$b"
done
# --verify --deep accepts nested code that is only signed ad hoc; notarization does not.
while IFS= read -r -d '' f; do
  codesign -dvv "$f" 2>&1 | grep "^Authority=Developer ID Application" > /dev/null || { echo "not Developer ID signed: $f" >&2; exit 1; }
done < <(nested)
codesign --force --timestamp --options runtime --sign "$APP_ID" "$SIGNED/$LV2.lv2/$LV2.dylib"
for b in "$NAME.vst3" "$NAME.component" "$NAME.clap" "$LV2.lv2/$LV2.dylib" "$NAME.aaxplugin"; do
  codesign --verify --deep --strict --verbose=1 "$SIGNED/$b"
done

component() { # key, bundle or folder, install location, identifier
  local key="$1" item="$2" loc="$3" pid="$4" plist="$WORK/plists/$1.plist"
  mkdir -p "$WORK/stage/$key"
  COPYFILE_DISABLE=1 ditto --norsrc --noextattr --noqtn "$item" "$WORK/stage/$key/$(basename "$item")"
  pkgbuild --analyze --root "$WORK/stage/$key" "$plist"
  local i=0
  while /usr/libexec/PlistBuddy -c "Print :$i:BundleIsRelocatable" "$plist" >/dev/null 2>&1; do
    /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$plist"
    i=$((i + 1))
  done
  pkgbuild --root "$WORK/stage/$key" --component-plist "$plist" --identifier "$pid" --version "$VERSION" \
    --install-location "$loc" --ownership recommended --sign "$INSTALLER_ID" --timestamp "$WORK/components/$FILE-$key.pkg"
  # The payload lists extended attributes (com.apple.provenance) as ._ entries; the installer folds them back into
  # attributes. What must not ship is a real ._ file, so expand the package as the installer does and look.
  rm -rf "$WORK/check"
  pkgutil --expand-full "$WORK/components/$FILE-$key.pkg" "$WORK/check" >/dev/null
  if [ -n "$(find "$WORK/check" -name '._*' -print -quit)" ]; then
    echo "AppleDouble files in the $key payload" >&2; exit 1
  fi
  rm -rf "$WORK/check"
}

echo "== component packages"
component vst3 "$SIGNED/$NAME.vst3" /Library/Audio/Plug-Ins/VST3 "$ID.pkg.vst3"
component au "$SIGNED/$NAME.component" /Library/Audio/Plug-Ins/Components "$ID.pkg.au"
component clap "$SIGNED/$NAME.clap" /Library/Audio/Plug-Ins/CLAP "$ID.pkg.clap"
component lv2 "$SIGNED/$LV2.lv2" /Library/Audio/Plug-Ins/LV2 "$ID.pkg.lv2"
component aax "$SIGNED/$NAME.aaxplugin" "/Library/Application Support/Avid/Audio/Plug-Ins" "$ID.pkg.aax"
DOCS="$WORK/docs/$NAME"
mkdir -p "$DOCS/Licenses"
cp "$MANUAL" "$DOCS/$FILE-$LABEL-User-Manual.pdf"
cp "$ROOT/LICENSE" "$DOCS/Licenses/LICENSE.txt"
cp "$ROOT/BINARY_LICENSE.txt" "$ROOT/THIRD_PARTY_LICENSES.md" "$ROOT"/Licenses/* "$DOCS/Licenses/"
mkdir -p "$WORK/stage/docs"
COPYFILE_DISABLE=1 ditto --norsrc --noextattr --noqtn "$DOCS" "$WORK/stage/docs/$NAME"
pkgbuild --root "$WORK/stage/docs" --identifier "$ID.pkg.docs" --version "$VERSION" \
  --install-location "/Library/Application Support" --ownership recommended \
  --sign "$INSTALLER_ID" --timestamp "$WORK/components/$FILE-docs.pkg"

echo "== distribution"
cp "$ROOT/BINARY_LICENSE.txt" "$WORK/resources/license.txt"
choice() { # key, title, location
  printf '  <choice id="choice.%s" title="%s" description="Installs %s in %s" start_selected="true">\n    <pkg-ref id="%s.pkg.%s"/>\n  </choice>\n' "$1" "$2" "$2" "$3" "$ID" "$1"
}
{
  cat <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>$NAME $LABEL</title>
  <license file="license.txt"/>
  <options customize="always" require-scripts="false" rootVolumeOnly="true" hostArchitectures="arm64,x86_64"/>
  <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>
  <os-version min="11.0"/>
  <choices-outline>
    <line choice="choice.vst3"/>
    <line choice="choice.au"/>
    <line choice="choice.clap"/>
    <line choice="choice.lv2"/>
    <line choice="choice.aax"/>
    <line choice="choice.docs"/>
  </choices-outline>
XML
  choice vst3 "VST3" "/Library/Audio/Plug-Ins/VST3"
  choice au "Audio Unit" "/Library/Audio/Plug-Ins/Components"
  choice clap "CLAP" "/Library/Audio/Plug-Ins/CLAP"
  choice lv2 "LV2" "/Library/Audio/Plug-Ins/LV2"
  choice aax "AAX (Pro Tools)" "/Library/Application Support/Avid/Audio/Plug-Ins"
  cat <<XML
  <choice id="choice.docs" title="Manual and licences" description="Installs the user manual and the licence texts in /Library/Application Support/$NAME" start_selected="true" enabled="false">
    <pkg-ref id="$ID.pkg.docs"/>
  </choice>
XML
  for k in vst3 au clap lv2 aax docs; do
    printf '  <pkg-ref id="%s.pkg.%s" version="%s" onConclusion="none">%s-%s.pkg</pkg-ref>\n' "$ID" "$k" "$VERSION" "$FILE" "$k"
  done
  echo '</installer-gui-script>'
} > "$WORK/distribution.xml"
rm -f "$OUT"
productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK/components" --resources "$WORK/resources" \
  --version "$VERSION" --sign "$INSTALLER_ID" --timestamp "$OUT"

if [ "$NOTARIZE" = 1 ]; then
  echo "== notarization"
  xcrun notarytool submit "$OUT" --keychain-profile "$NOTARY_PROFILE" --wait
  xcrun stapler staple "$OUT"
  xcrun stapler validate "$OUT"
  spctl --assess --type install -vv "$OUT"
fi
pkgutil --check-signature "$OUT"
shasum -a 256 "$OUT" | sed "s#  .*/#  #" > "$OUT.sha256"
cat "$OUT.sha256"
