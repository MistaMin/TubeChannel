#!/bin/bash
# Stages the plugin bundles in the folders the plugin manager installs them to, and zips them.
# The zip root mirrors the macOS install tree, so the manager copies each entry to "/" as-is.
#
# Usage: make_bundle_zip.sh <Plugin Name> <version> <artefacts/Release dir> <out dir>
#
# Writes:  <out>/<Plugin>-<version>-macos-bundles.zip
#          <out>/<Plugin>-<version>-macos-bundles.zip.installs.json  (read by write_opengrid_manifest.py)
# Optional: RESOURCES_DIR=<dir> copies that folder (Licenses/, Manual/, ...) to
# Library/Application Support/<Plugin>/.
# MIN_OS (default 11.0) is recorded in the sidecar for the manifest.
# Standalone apps are not included. The zip is not a package: nothing is installed by this script.
set -euo pipefail

if [ $# -ne 4 ]; then
  echo "usage: $0 <Plugin Name> <version> <artefacts/Release dir> <out dir>" >&2
  exit 2
fi
PLUGIN=$1; VERSION=$2; ART=$3; OUT=$4
STAGE="$OUT/stage-macos-bundles"
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
STAGE="$OUT/stage-macos-bundles"
NAME="$PLUGIN-$VERSION-macos-bundles.zip"
NAME=${NAME// /-}
ZIP="$OUT/$NAME"
rm -rf "$STAGE"
mkdir -p "$STAGE"

# format folder under artefacts | bundle name | install folder (relative to /) | manifest format name
declare -a BUNDLES=(
  "VST3|%s.vst3|Library/Audio/Plug-Ins/VST3|vst3"
  "AU|%s.component|Library/Audio/Plug-Ins/Components|au"
  "CLAP|%s.clap|Library/Audio/Plug-Ins/CLAP|clap"
  "LV2|%s.lv2|Library/Audio/Plug-Ins/LV2|lv2"
  "AAX|%s.aaxplugin|Library/Application Support/Avid/Audio/Plug-Ins|aax"
)

INSTALLS=()
for entry in "${BUNDLES[@]}"; do
  IFS='|' read -r fmt pattern dest id <<<"$entry"
  name=$(printf "$pattern" "$PLUGIN")
  src="$ART/$fmt/$name"
  if [ ! -e "$src" ]; then
    if [ "$fmt" = "AAX" ]; then continue; fi   # AAX is optional: only with the Avid SDK and PACE signing
    echo "missing $src" >&2; exit 1
  fi
  mkdir -p "$STAGE/$dest"
  # ditto without extended attributes or resource forks keeps AppleDouble entries out of the zip.
  ditto --norsrc --noextattr --noacl "$src" "$STAGE/$dest/$name"
  xattr -cr "$STAGE/$dest/$name" 2>/dev/null || true
  INSTALLS+=("$id|$dest/$name")
done

if [ -n "${RESOURCES_DIR:-}" ]; then
  mkdir -p "$STAGE/Library/Application Support/$PLUGIN"
  ditto --norsrc --noextattr --noacl "$RESOURCES_DIR" "$STAGE/Library/Application Support/$PLUGIN"
fi

if find "$STAGE" -name '._*' | grep . >/dev/null; then
  echo "AppleDouble files in the stage; refusing to zip" >&2; exit 1
fi
for entry in "${INSTALLS[@]}"; do
  rel=${entry#*|}
  lipo -info "$STAGE/$rel/Contents/MacOS/"* 2>/dev/null | sed "s|^|$rel: |" || true
done

rm -f "$ZIP"
(cd "$STAGE" && zip -qry "$ZIP" Library)
rm -rf "$STAGE"

# Installs list, one entry per bundle, for the manifest writer.
{
  printf '{"os":"macos","arch":"universal","min_os":"%s","installs":[' "${MIN_OS:-11.0}"
  first=1
  for entry in "${INSTALLS[@]}"; do
    id=${entry%%|*}; rel=${entry#*|}
    [ $first -eq 1 ] || printf ','
    printf '{"format":"%s","path":"%s"}' "$id" "$rel"
    first=0
  done
  printf ']}\n'
} > "$ZIP.installs.json"

echo "wrote $ZIP"
unzip -l "$ZIP" | tail -n +1
