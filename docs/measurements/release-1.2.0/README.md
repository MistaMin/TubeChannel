# Tube Channel 1.2.0 release verification

The universal Release build contains arm64 and x86_64 code for VST3, Audio Unit,
CLAP, LV2 and Standalone, with minimum macOS 11. No AAX payload is included.
`build-manifest.json` records exact runtime source SHA256 values, dependency commits
and compiler version. The release uses JUCE 9.0.3; the source dependency pin matches.

All 14 native engine tests passed. The universal Apple silicon processor and GUI
checks passed 2/2; the x86_64 versions passed under Rosetta with zero failures.
This validates the Intel code path, not performance on physical Intel hardware.
The mode-switch fixture now waits for the real asynchronous handoff, with a five
second bound, rather than assuming one millisecond dispatch during concurrent load.
Every requested switch still requires exactly one adoption with the correct
engine and oversampling factor. No audio-runtime change was needed for this fix.

All five staged and extracted format payloads pass strict Developer ID signature
verification and contain both architectures. The installer is signed with Developer
ID Installer, accepted by Apple's notarization service, stapled and validated.
Gatekeeper accepts it as Notarized Developer ID. Notarization submission:
`9bdc6f64-a053-44c6-b6de-093a0f82db71`.

The expanded installer contains the updated user guide, all four interface
screenshots, signal-flow image, release notes, build manifest, project license and
third-party overview. Its license inventory contains 31 files covering Tube Channel,
GoodLookinUI, JUCE and bundled codecs/graphics code, CLAP/helpers/extensions, VST3,
LV2 and AudioUnit SDK notices. All packaged runtime source hashes match the source.
The installed resource location is `/Library/Application Support/Tube Channel/`.
The root license copies also make the guide's relative license links resolve.

`installer.sha256` contains the checksum of the final stapled installer. Signing,
notarization, package inspection, build and test logs are preserved here.

The [Eco report](../eco-1.2.0/README.md) contains the 10x minimum performance gate,
independent-control checks, calibration diagnostics, waveform comparisons, and
harmonic/IMD plots. Heavy-drive sound differences are disclosed. Program-material
listening acceptance and actual DAW/audio-device multi-instance playback remain
unverified; no claim of circuit-identical Eco sound or zero aliasing is made.
