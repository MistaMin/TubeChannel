# Tube Channel 1.2.2

Tube Channel 1.2.2 prepares the current sound engine for distribution across the
requested desktop plugin formats and platforms. It does not change the audio model.

## Formats and platforms

- macOS universal (Apple silicon and Intel): VST3, Audio Unit, CLAP, LV2,
  standalone, and AAX.
- Windows x64: VST3, CLAP, LV2, and standalone, built and tested by GitHub Actions.
- Linux x86_64: VST3, CLAP, LV2, and standalone, built and tested by GitHub Actions.

The AAX build uses the separately licensed AAX SDK. Its macOS binary is signed with
the developer's PACE account before it is included in the signed and notarized Mac
installer. The AAX SDK is not included in the source repository.

## Licensing

Tube Channel source code is MIT licensed. Official plugin binaries are freeware and
are built under the author's commercial JUCE Developer license. Each package includes
the project license, user manual, third-party license overview, and applicable
dependency license texts. See [THIRD_PARTY_LICENSES.md](../THIRD_PARTY_LICENSES.md).

## Validation

The Windows x64 and Linux x86_64 workflows build all supported formats, run the
platform test suite, and package the plugins with license files. The universal macOS
build is validated locally; the signed installer and disk image are notarized before
release. No change to DSP behavior is part of this release.
