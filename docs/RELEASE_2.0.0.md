# Tube Channel 2.0.0

Tube Channel 2.0.0 is the first version built on OpenPluginSDK instead of JUCE. The
editor is drawn by GoodLookin DRI. The audio model is the same code as 1.2.x, and
projects and presets saved with 1.x open with the same settings.

## What is the same

- Every 1.x parameter keeps its id, order, range, default and value text, so host
  automation and saved projects carry over. The plug-in ids are unchanged: 2.0.0
  replaces 1.x in the host.
- The Eco, Circuit and HQ engines, oversampling, Offline Bounce, the supply model and
  both skins.
- At the default settings, and with the main controls automated, the VST3 output is
  bit-identical to the 1.2.5 build at 48 kHz. The Circuit and HQ engines and explicit
  oversampling run the same code but were not compared sample by sample.

## New

- **Autogain** matches the output loudness to the input loudness (ITU-R BS.1770
  weighting, a three second window, held during silence). Off by default.
- **Delta** plays only what the processing changes. Off by default.
- A change of engine, oversampling, phase, tolerances or seed is built in the
  background and faded in.
- An offline render always starts with the circuit at rest, so repeated bounces give
  the same result.
- The plug-in reports a 2.5 second tail.

## Changed or removed

- No undo history inside the plug-in, and no CPU readout on the System page. A/B and
  Copy are kept.
- The program and Offline Bounce menus are lists drawn over the page.
- The factory programs are offered through the host's preset list. The old "Program"
  parameter is gone.
- An explicit Quality change from the host no longer turns Eco off by itself; the
  editor's mode keys set both, as before.
- A choice written exactly half-way between two entries rounds up (1.x rounded to
  even).
- No standalone app.

## Fixes from the unreleased 1.2.5

- Non-finite input and sidechain samples are treated as silence instead of silencing
  the plug-in until it is reloaded.
- A re-activation during playback no longer builds the circuit inside the audio
  callback.

## Licensing

Tube Channel source code is MIT licensed. All official plugin binaries are freeware.
The packages include the applicable third-party license texts.
