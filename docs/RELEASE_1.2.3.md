# Tube Channel 1.2.3

This patch release fixes the Linux GUI test setup and refreshes the cross-platform
release packages. The audio engine and sound are unchanged from 1.2.2.

## Formats and platforms

- macOS universal (Apple silicon and Intel): VST3, Audio Unit, CLAP, LV2,
  standalone, and AAX.
- Windows x64: VST3, CLAP, LV2, and standalone.
- Linux x86_64: VST3, CLAP, LV2, and standalone.

AAX is signed through the developer's PACE account. macOS plugin bundles and the
installer are signed with the developer's Developer ID and notarized when the
local Apple notarization profile is available. Windows signing depends on an
Authenticode certificate; Linux archives include SHA-256 checksums.

## Licensing

Tube Channel source code is MIT licensed. Official plugin binaries are freeware
and are built under the author's commercial JUCE Developer license. Each package
includes the project license, user manual, third-party license overview, and
applicable dependency license texts. See [THIRD_PARTY_LICENSES.md](../THIRD_PARTY_LICENSES.md).

## Validation

Windows x64 and Linux x86_64 release workflows build supported formats and run the
platform test suite. The Linux GUI test now creates a native desktop peer before
opening popup menus, avoiding the X11 `BadAtom` failure in headless CI. The
universal macOS build is validated locally. This release makes no DSP changes.
