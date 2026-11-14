# Tube Channel 1.2.4

This hotfix release addresses a host-side engine switching issue and improves
host activation stability across macOS, Windows, and Linux.

## Highlights and fixes

- **Host-side Quality switching**: When controlled or automated from a digital
  audio workstation (DAW) that sets the exposed `Quality` parameter, the plugin
  now explicitly clears the Eco flag and switches to the requested engine
  (Circuit or HQ). In prior versions, because new instances default to Eco mode,
  host-side Quality changes alone did not leave Eco mode, causing renders across
  different host Quality settings to remain bit-identical. UI control banks
  continue to set both parameters simultaneously, and state restoration / A/B
  switching remain protected so saved Eco selections are preserved.
- **Audio-thread chain rebuild**: Chain rebuilding triggered by `prepareToPlay`
  is now deferred cleanly to the audio thread to prevent race conditions during
  host activation and playback startup.
- **Latency announcement deduplication**: Latency changes are now reported to
  the host only when latency actually changes, avoiding redundant host re-scans
  and playback glitches during deferred prepare passes.

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

Tube Channel source code is MIT licensed. All official plugin binaries, including
the macOS AAX binary, are freeware and are built under the author's commercial JUCE
Developer license. Each package includes the project license, user manual,
third-party license overview, and applicable dependency license texts. See
[THIRD_PARTY_LICENSES.md](../THIRD_PARTY_LICENSES.md).

## Validation

Windows x64 and Linux x86_64 release workflows build supported formats and run the
platform test suite (18/18 tests pass, including `processor_stress` regression
checks for host-driven Quality switching). DAW verification in REAPER confirms
audible and measured differences between Quality modes under host automation.
The universal macOS build is validated locally.
