#!/bin/bash
# Refuse to package or sign bundles whose version differs from VERSION.
#   installer/check_bundle_versions.sh <artefacts/Release folder>
# A signed, notarized package built from stale binaries cannot be recalled once it is published.
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION=$(tr -d '[:space:]' < VERSION)
ART=${1:?usage: check_bundle_versions.sh <artefacts/Release folder>}
bad=0
while IFS= read -r -d '' plist; do
  found=$(plutil -extract CFBundleShortVersionString raw "$plist" 2>/dev/null || echo "?")
  if [ "$found" != "$VERSION" ]; then
    echo "version mismatch: $plist is $found, VERSION is $VERSION" >&2
    bad=1
  fi
done < <(find "$ART" -maxdepth 4 -name Info.plist -path '*/Tube Channel.*/Contents/Info.plist' -print0)
[ "$bad" = 0 ] || { echo "Rebuild the binaries from this source revision before packaging." >&2; exit 1; }
echo "bundle versions match VERSION $VERSION"
