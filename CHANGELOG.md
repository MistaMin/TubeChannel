# Changelog

## 2.0.0

- Built on OpenPluginSDK with a GoodLookin DRI editor; the JUCE build is retired.
- 1.x projects and presets open unchanged; same parameters and plug-in ids.
- New Autogain and Delta.
- Engine and oversampling changes are built in the background and faded in.
- Offline renders start with the circuit at rest.
- Reports a 2.5 second tail.
- Removed the plug-in's undo history, the CPU readout, the "Program" parameter and the standalone app.
- Includes the fixes prepared for 1.2.5, which was not released: non-finite input is treated as silence, and re-activation no longer builds the circuit on the audio thread.

Details: [docs/RELEASE_2.0.0.md](docs/RELEASE_2.0.0.md).

## 1.2.4

Engine mode switching from host automation, steadier host activation. [Notes](docs/RELEASE_1.2.4.md).

## 1.2.3

Standalone app icon; Linux GUI test setup. [Notes](docs/RELEASE_1.2.3.md).

## 1.2.2

[Notes](docs/RELEASE_1.2.2.md).

## 1.2.1

Offline Bounce. [Notes](docs/RELEASE_1.2.1.md).

## 1.2.0

Eco engine, the default for new instances. [Notes](docs/RELEASE_1.2.0.md).
