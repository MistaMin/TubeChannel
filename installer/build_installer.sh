#!/bin/bash
# Build the Tube Channel installer from a universal Release build.
#   installer/build_installer.sh                 unsigned package (for a local test)
#   SIGN=1 installer/build_installer.sh          sign every bundle, build, sign and notarize + staple the package
# Run SIGN=1 from your own Terminal: codesign / notarytool need the keychain prompt.
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION=$(cat VERSION)
ART=build/universal/TubeChannel_artefacts/Release
OUT=${OUT:-dist}
BUILD=build/universal
APP_ID="Developer ID Application: Marcos Deida (ZQYU67JUBH)"
INST_ID="Developer ID Installer: Marcos Deida (ZQYU67JUBH)"
mkdir -p "$OUT"
rm -rf "$OUT/stage" "$OUT/pkgs"
rm -f "$OUT/TubeChannel-$VERSION-unsigned.pkg" "$OUT/distribution.xml"
mkdir -p "$OUT/stage" "$OUT/pkgs"

cache_value() {
  sed -n "s/^$1:[^=]*=//p" "$BUILD/CMakeCache.txt" | head -n 1
}
JUCE_SRC=$(cache_value FETCHCONTENT_SOURCE_DIR_JUCE)
CLAP_EXT_SRC=$(cache_value FETCHCONTENT_SOURCE_DIR_CLAP-JUCE-EXTENSIONS)
[ -n "$JUCE_SRC" ] || JUCE_SRC="$BUILD/_deps/juce-src"
[ -n "$CLAP_EXT_SRC" ] || CLAP_EXT_SRC="$BUILD/_deps/clap-juce-extensions-src"

RESOURCE_ROOT="$OUT/stage/licenses/Tube Channel"
LICENSE_ROOT="$RESOURCE_ROOT/Licenses"
mkdir -p "$LICENSE_ROOT/Tube Channel" "$LICENSE_ROOT/GoodLookinUI" "$LICENSE_ROOT/JUCE" \
         "$LICENSE_ROOT/CLAP" "$LICENSE_ROOT/VST3-SDK" "$LICENSE_ROOT/LV2-SDK" "$LICENSE_ROOT/AudioUnit-SDK"
mkdir -p "$RESOURCE_ROOT/Manual"
cp docs/USER_MANUAL.md docs/ui-analogue.png docs/ui-digital.png docs/ui-system-analogue.png \
   docs/ui-system-digital.png docs/signal-path.svg "$RESOURCE_ROOT/Manual/"
cp LICENSE "$LICENSE_ROOT/Tube Channel/LICENSE"
cp THIRD_PARTY_LICENSES.md "$LICENSE_ROOT/Tube Channel/THIRD_PARTY_LICENSES.md"
cp third_party/GoodLookinUI/LICENSE "$LICENSE_ROOT/GoodLookinUI/LICENSE"
cp "$JUCE_SRC/LICENSE.md" "$LICENSE_ROOT/JUCE/LICENSE.md"
cp "$CLAP_EXT_SRC/LICENSE.md" "$LICENSE_ROOT/CLAP/clap-juce-extensions-LICENSE.md"
cp "$CLAP_EXT_SRC/clap-libs/clap/LICENSE" "$LICENSE_ROOT/CLAP/clap-LICENSE"
cp "$CLAP_EXT_SRC/clap-libs/clap-helpers/LICENSE" "$LICENSE_ROOT/CLAP/clap-helpers-LICENSE"

# Include notices for code used by the shipped macOS formats: codecs, graphics helpers, and
# the VST3, LV2, and Audio Unit SDKs. Windows/Android backends and disabled browser/scripting
# modules are not part of these binaries.
while IFS= read -r -d '' license; do
  case "$license" in
    */juce_audio_processors_headless/format_types/VST3_SDK/*)
      rel=${license#*VST3_SDK/}; mkdir -p "$LICENSE_ROOT/VST3-SDK/$(dirname "$rel")"; cp "$license" "$LICENSE_ROOT/VST3-SDK/$rel" ;;
    */juce_audio_processors_headless/format_types/LV2_SDK/*)
      rel=${license#*LV2_SDK/}; mkdir -p "$LICENSE_ROOT/LV2-SDK/$(dirname "$rel")"; cp "$license" "$LICENSE_ROOT/LV2-SDK/$rel" ;;
    */juce_audio_plugin_client/AU/AudioUnitSDK/*)
      rel=${license#*AudioUnitSDK/}; mkdir -p "$LICENSE_ROOT/AudioUnit-SDK/$(dirname "$rel")"; cp "$license" "$LICENSE_ROOT/AudioUnit-SDK/$rel" ;;
    */modules/*)
      rel=${license#*modules/}; mkdir -p "$LICENSE_ROOT/JUCE/modules/$(dirname "$rel")"; cp "$license" "$LICENSE_ROOT/JUCE/modules/$rel" ;;
  esac
done < <(find \
  "$JUCE_SRC/modules/juce_audio_formats/codecs" \
  "$JUCE_SRC/modules/juce_core/zip/zlib" \
  "$JUCE_SRC/modules/juce_graphics/drawables/lunasvg" \
  "$JUCE_SRC/modules/juce_graphics/fonts/harfbuzz" \
  "$JUCE_SRC/modules/juce_graphics/image_formats/libwebp" \
  "$JUCE_SRC/modules/juce_graphics/image_formats/pnglib" \
  "$JUCE_SRC/modules/juce_graphics/unicode/sheenbidi" \
  "$JUCE_SRC/modules/juce_audio_processors_headless/format_types/VST3_SDK" \
  "$JUCE_SRC/modules/juce_audio_processors_headless/format_types/LV2_SDK" \
  "$JUCE_SRC/modules/juce_audio_plugin_client/AU/AudioUnitSDK" \
  -type f \( -iname 'LICENSE*' -o -iname 'COPYING*' -o -iname 'NOTICE*' \) -print0)

declare -a BUNDLES=(
  "VST3/Tube Channel.vst3|/Library/Audio/Plug-Ins/VST3|vst3"
  "AU/Tube Channel.component|/Library/Audio/Plug-Ins/Components|au"
  "CLAP/Tube Channel.clap|/Library/Audio/Plug-Ins/CLAP|clap"
  "LV2/Tube Channel.lv2|/Library/Audio/Plug-Ins/LV2|lv2"
  "Standalone/Tube Channel.app|/Applications|app"
)
for entry in "${BUNDLES[@]}"; do
  IFS='|' read -r rel dest id <<<"$entry"
  name=$(basename "$rel")
  mkdir -p "$OUT/stage/$id"
  cp -R "$ART/$rel" "$OUT/stage/$id/$name"
  find "$OUT/stage/$id" -name '._*' -delete
  xattr -cr "$OUT/stage/$id/$name"
  if [ "${SIGN:-0}" = "1" ]; then
    if [ "$id" = "lv2" ]; then
      # an LV2 bundle is a plain folder: sign the binaries inside it
      find "$OUT/stage/$id/$name" -type f \( -name '*.dylib' -o -name '*.so' \) | while read -r f; do
        codesign --force --timestamp --options runtime --sign "$APP_ID" "$f"
        codesign --verify --strict --verbose=2 "$f"
      done
    else
      codesign --force --deep --timestamp --options runtime --sign "$APP_ID" "$OUT/stage/$id/$name"
      codesign --verify --deep --strict --verbose=2 "$OUT/stage/$id/$name"
    fi
  fi
  pkgbuild --root "$OUT/stage/$id" --install-location "$dest" --identifier "com.opengrid.tubechannel.$id" \
           --version "$VERSION" "$OUT/pkgs/TubeChannel-$id.pkg" >/dev/null
done

pkgbuild --root "$OUT/stage/licenses" --install-location "/Library/Application Support" \
         --identifier "com.opengrid.tubechannel.licenses" --version "$VERSION" \
         "$OUT/pkgs/TubeChannel-licenses.pkg" >/dev/null

cat > "$OUT/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="1">
  <title>Tube Channel $VERSION</title>
  <options customize="always" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <choices-outline>
    <line choice="vst3"/><line choice="au"/><line choice="clap"/><line choice="lv2"/><line choice="app"/><line choice="licenses"/>
  </choices-outline>
  <choice id="vst3" title="VST3"><pkg-ref id="com.opengrid.tubechannel.vst3"/></choice>
  <choice id="au" title="Audio Unit"><pkg-ref id="com.opengrid.tubechannel.au"/></choice>
  <choice id="clap" title="CLAP"><pkg-ref id="com.opengrid.tubechannel.clap"/></choice>
  <choice id="lv2" title="LV2"><pkg-ref id="com.opengrid.tubechannel.lv2"/></choice>
  <choice id="app" title="Standalone application"><pkg-ref id="com.opengrid.tubechannel.app"/></choice>
  <choice id="licenses" title="User guide and license texts" selected="true" visible="false"><pkg-ref id="com.opengrid.tubechannel.licenses"/></choice>
  <pkg-ref id="com.opengrid.tubechannel.vst3">TubeChannel-vst3.pkg</pkg-ref>
  <pkg-ref id="com.opengrid.tubechannel.au">TubeChannel-au.pkg</pkg-ref>
  <pkg-ref id="com.opengrid.tubechannel.clap">TubeChannel-clap.pkg</pkg-ref>
  <pkg-ref id="com.opengrid.tubechannel.lv2">TubeChannel-lv2.pkg</pkg-ref>
  <pkg-ref id="com.opengrid.tubechannel.app">TubeChannel-app.pkg</pkg-ref>
  <pkg-ref id="com.opengrid.tubechannel.licenses">TubeChannel-licenses.pkg</pkg-ref>
</installer-gui-script>
XML
PKG="$OUT/TubeChannel-$VERSION-unsigned.pkg"
productbuild --distribution "$OUT/distribution.xml" --package-path "$OUT/pkgs" "$PKG" >/dev/null
echo "built $PKG"

if [ "${SIGN:-0}" = "1" ]; then
  FINAL="$OUT/TubeChannel-$VERSION.pkg"
  productsign --sign "$INST_ID" --timestamp "$PKG" "$FINAL"
  pkgutil --check-signature "$FINAL"
  xcrun notarytool submit "$FINAL" --keychain-profile AC_NOTARY --wait
  xcrun stapler staple "$FINAL"
  xcrun stapler validate "$FINAL"
  echo "signed, notarized and stapled: $FINAL"
fi
