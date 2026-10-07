#!/bin/bash
# Build the Tube Channel installer from a universal Release build.
#   installer/build_installer.sh                 unsigned package (for a local test)
#   SIGN=1 installer/build_installer.sh          sign every bundle, build, sign and notarize + staple the package
# Run SIGN=1 from your own Terminal: codesign / notarytool need the keychain prompt.
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION=$(cat VERSION)
ART=build/universal/TubeChannel_artefacts/Release
OUT=dist
APP_ID="Developer ID Application: Marcos Deida (ZQYU67JUBH)"
INST_ID="Developer ID Installer: Marcos Deida (ZQYU67JUBH)"
rm -rf "$OUT" && mkdir -p "$OUT/stage" "$OUT/pkgs"

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

cat > "$OUT/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="1">
  <title>Tube Channel $VERSION</title>
  <options customize="always" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <choices-outline>
    <line choice="vst3"/><line choice="au"/><line choice="clap"/><line choice="lv2"/><line choice="app"/>
  </choices-outline>
  <choice id="vst3" title="VST3"><pkg-ref id="com.opengrid.tubechannel.vst3"/></choice>
  <choice id="au" title="Audio Unit"><pkg-ref id="com.opengrid.tubechannel.au"/></choice>
  <choice id="clap" title="CLAP"><pkg-ref id="com.opengrid.tubechannel.clap"/></choice>
  <choice id="lv2" title="LV2"><pkg-ref id="com.opengrid.tubechannel.lv2"/></choice>
  <choice id="app" title="Standalone application"><pkg-ref id="com.opengrid.tubechannel.app"/></choice>
  <pkg-ref id="com.opengrid.tubechannel.vst3">TubeChannel-vst3.pkg</pkg-ref>
  <pkg-ref id="com.opengrid.tubechannel.au">TubeChannel-au.pkg</pkg-ref>
  <pkg-ref id="com.opengrid.tubechannel.clap">TubeChannel-clap.pkg</pkg-ref>
  <pkg-ref id="com.opengrid.tubechannel.lv2">TubeChannel-lv2.pkg</pkg-ref>
  <pkg-ref id="com.opengrid.tubechannel.app">TubeChannel-app.pkg</pkg-ref>
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
