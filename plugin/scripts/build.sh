#!/bin/sh
# Builds Tube Channel's macOS bundles (CLAP, LV2, VST3, Audio Unit, AAX) with OpenPluginSDK's
# scripts/build_plugin.sh. Nothing is installed and nothing is signed beyond ad hoc.
#
#   scripts/build.sh [--archs arm64|x86_64|universal] [--formats LIST] [--out DIR] [--work-dir DIR] [--qa]
#
# --qa builds "Tube Channel QA": its own ids (feature qa-identity), so it installs beside a release.
#
# Where things are found (set the variable, or keep the checkout in a Dependencies or SDKs folder
# beside or above this repository):
#   OPENPLUGIN_SDK   OpenPluginSDK checkout (GoodLookin DRI is in its crates/)
#   VST3_SDK, AU_SDK, AAX_SDK
set -eu

here="$(cd "$(dirname "$0")/.." && pwd)"
root="$(cd "$here/.." && pwd)"

find_dir() { # env value, then candidate names
  if [ -n "$1" ] && [ -d "$1" ]; then echo "$1"; return 0; fi
  shift
  for name in "$@"; do
    for base in "$root/.." "$root/../.." "$root/../Dependencies" "$root/../../Dependencies" "$root/../SDKs" "$root/../../SDKs" /Volumes/*/Dependencies /Volumes/*/SDKs "$HOME/Dependencies" "$HOME/SDKs"; do
      [ -d "$base/$name" ] && { (cd "$base/$name" && pwd); return 0; }
    done
  done
  return 1
}

archs=universal
formats=clap,lv2,vst3,au,aax
out="$here/dist"
work="$here/target/bundles"
name="Tube Channel"; bundle=com.OpenGrid.TubeChannel; subtype=TbCh; features=""
while [ $# -gt 0 ]; do
  case "$1" in
    --archs) archs="$2"; shift 2 ;;
    --formats) formats="$2"; shift 2 ;;
    --out) out="$2"; shift 2 ;;
    --work-dir) work="$2"; shift 2 ;;
    --qa) name="Tube Channel QA"; bundle=com.OpenGrid.TubeChannelQA; subtype=TbQA; features=qa-identity; shift ;;
    -h|--help) sed -n 2,12p "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

SDK="$(find_dir "${OPENPLUGIN_SDK:-}" OpenPluginSDK)" || { echo "OpenPluginSDK not found; set OPENPLUGIN_SDK" >&2; exit 1; }
set --
case ",$formats," in *,vst3,*)
  VST3="$(find_dir "${VST3_SDK:-}" vst3sdk clap-wrapper/build/cpm/vst3sdk)" || { echo "VST3 SDK not found; set VST3_SDK" >&2; exit 1; }
  [ -f "$VST3/public.sdk/source/main/macmain.cpp" ] || { echo "$VST3 is not a complete VST3 SDK; set VST3_SDK" >&2; exit 1; }
  set -- "$@" --vst3-sdk "$VST3" ;;
esac
case ",$formats," in *,au,*)
  AU="$(find_dir "${AU_SDK:-}" AudioUnitSDK)" || { echo "AudioUnitSDK not found; set AU_SDK" >&2; exit 1; }
  set -- "$@" --au-sdk "$AU" ;;
esac
case ",$formats," in *,aax,*)
  AAX="$(find_dir "${AAX_SDK:-}" aax-sdk-2-8-1)" || { echo "AAX SDK not found; set AAX_SDK" >&2; exit 1; }
  set -- "$@" --aax-sdk "$AAX" ;;
esac

# Cargo takes the SDK crates from the git revision Cargo.toml pins; this checkout gives only the
# build scripts and should be at the same revision.

# The Audio Unit type and codes are given here because the shell build cannot ask a library
# that also holds C++ (its probe links as C). They are the ones `Plugin::au_ids` pins.
cd "$here"
"$SDK/scripts/build_plugin.sh" --manifest-path "$here/Cargo.toml" --name "$name" \
  --bundle-id "$bundle" --manufacturer OpenGrid \
  --ttl-bin lv2_ttl --formats "$formats" --editor --tail --archs "$archs" \
  --au-type aufx --au-subtype "$subtype" --au-manufacturer OpGr \
  ${features:+--features "$features"} "$@" --out "$out" --work-dir "$work"
