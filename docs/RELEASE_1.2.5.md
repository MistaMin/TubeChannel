# Tube Channel 1.2.5

This release refreshes the interface with the latest GoodLookinUI and hardens the
plugin against bad input and host re-activation. The sound is unchanged from 1.2.4.

## Interface

- The editor now draws with the current GoodLookinUI toolkit: new VU meter faces,
  rack ears, brushed panels, knobs, lamps and switches. Layout, controls and
  parameters are unchanged. The screenshots in the manual are regenerated.

## Fixes

- **NaN and infinity input**: a single non-finite sample from the host (an upstream
  plugin glitch) could enter the filters and detector and silence the plugin until
  it was reloaded. Non-finite input and sidechain samples are now treated as silence.
- **Re-activation**: when a host re-prepares the plugin during playback (sample-rate
  or block-size change), the Circuit and HQ engines used to build their whole chain
  inside the next audio callback, a stall of about 200 ms. The chain is now built in
  `prepareToPlay` and the audio thread only swaps it in; the first callback takes
  about 1.5 ms.
- **Latency reporting**: a chain built while a re-activation landed could report the
  old latency to the host, and unchanged latency could be announced twice. Latency
  is now announced from one place, and only when it changes.

## Packaging

- The macOS and Windows packaging scripts refuse to run when the built bundles do not
  match `VERSION`, and `installer/build_pkg.sh` builds the signed macOS package.
- The Windows build is a bundle zip (VST3, CLAP, LV2); the README now says so.

## Formats and platforms

- macOS universal (Apple silicon and Intel): VST3, Audio Unit, CLAP, LV2, and AAX.
- Windows x64: VST3, CLAP, and LV2.
- Linux x86_64: VST3, CLAP, LV2, and standalone.

## Licensing

Tube Channel source code is MIT licensed. All official plugin binaries, including
the macOS AAX binary, are freeware and are built under the author's commercial JUCE
Developer license. See [THIRD_PARTY_LICENSES.md](../THIRD_PARTY_LICENSES.md).
