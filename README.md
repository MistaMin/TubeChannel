# Tube Channel

**A circuit-modelled channel strip for macOS and desktop audio workstations.** Tube Channel combines a preamp, compressor, four-band equalizer, output stage, and optional power-supply simulation in one plug-in.

![Digital interface: input, compressor, equalizer, and output](docs/ui-digital.png)

The interface is available in a digital layout and an analogue-style layout. Both control the same audio model.

| Digital | Analogue-style |
|---|---|
| ![Digital interface](docs/ui-digital.png) | ![Analogue-style interface](docs/ui-analogue.png) |

## Version 1.2.0

**1.2.0** adds the reduced **Eco** engine as the default for new instances.
It uses measured filters and asymmetric nonlinear maps without runtime circuit solves.
Circuit and HQ retain the original engine. Older sessions, including legacy Derp
settings, retain their original processing.

The measured stereo workload matrix achieved **26–35x at 48 kHz** and **14–19x at
96 kHz** against v1.1.4 Default, exceeding the 10x minimum throughout. The 20x
stretch target is met at 48 kHz. These are local measured results; they do not establish listening acceptance
or actual multi-instance DAW playback. See the [measurement report](docs/measurements/eco-1.2.0/README.md)
and [development plan](docs/SOUND_MODEL_PLAN.md).

## Download

The signed and notarized macOS universal installer supports Apple silicon and Intel Macs. It installs VST3, Audio Unit, CLAP, LV2, and the standalone app. Choose formats in the installer. The package includes the user guide and license texts. AAX is not included in the release.

Download the [1.2.0 universal macOS installer](https://github.com/MistaMin/TubeChannel/releases/download/v1.2.0/TubeChannel-1.2.0.pkg) or browse [GitHub Releases](https://github.com/MistaMin/TubeChannel/releases). See the [release notes](docs/RELEASE_1.2.0.md).

## User guide

See the [Tube Channel User Guide](docs/USER_MANUAL.md) for the complete control reference, signal-flow diagrams, quality-mode details, and setup notes.

## Modes and oversampling

The System page has three engine buttons, in this order: **Eco**, **Circuit**, **HQ**.
Eco is the new-instance default and uses the reduced engine. Circuit is the renamed
Default mode with its prototype detector; HQ uses the original modeled detector.
The original circuit code remains available in Circuit and HQ.

Three separate small buttons select **2x**, **4x**, or **8x** (the Derp symbol).
Oversampling applies independently to every engine at every session rate. Clicking
the selected oversampling button returns to **1x**, shown with no button lit.
New instances start at 1x to retain Eco's low CPU cost. **+L LINEAR** chooses
linear-phase filters and reports their latency when oversampling is active;
minimum-phase filtering reports zero latency.

Older saved states retain their previous adaptive oversampling and quality mapping.
The first engine click freezes their current factor as an independent setting.
The legacy Quality automation parameter retains its three-value range; the new
Oversampling parameter is appended after the existing parameters.

Eco approximates the stages with calibrated filters, nonlinear maps, the retained
optical response, and reduced supply/load states. Heavy-drive harmonics and aliasing
differ; Eco is not circuit-identical. Oversampling increases CPU cost; the published
Eco measurements use its 1x default. PSU SIM is off for new instances.

## Audio model

In Circuit and HQ, the audio path is solved sample by sample at the selected oversampling rate. Eco uses fixed-cost filter and nonlinear stages. The detector is mode-selected and runs at the session rate. The supply model can feed changing rails back into the audio stages when enabled.

![Signal flow and supply interaction](docs/signal-path.svg)

Stereo processing uses a lockstep circuit pair. Tests compare it with two independent channels to better than -125 dB. The circuit netlists are embedded at build time.

## Performance

Earlier development measurements on an Apple M1 Max at 48 kHz, 2x, 256-sample blocks, with circuit noise and component tolerances on, processing a -18 dBFS sine:

| Configuration | Measured real-time CPU |
|---|---:|
| Mono, supply simulation on | about 35% |
| Mono, supply simulation off | about 30% |
| Stereo, supply simulation on | about 70% |
| Stereo, supply simulation off | about 60% |

These measurements predate the current quality-mode mapping and came from a machine that was not idle; use them as historical reference only. Actual results vary by host, session settings, and computer. The supply simulation is off by default for new instances.

## Build from source

```sh
git submodule update --init
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The build produces VST3, Standalone, LV2, and CLAP; Audio Unit is also built on macOS. AAX can be enabled when its SDK is available, but it is not part of the public release package. On macOS, pass `-DTUBECHANNEL_UNIVERSAL=ON` for an arm64 + x86_64 build. Existing JUCE and CLAP extension checkouts can be supplied with `-DTUBECHANNEL_JUCE_SOURCE=<path>` and `-DTUBECHANNEL_CLAP_EXT_SOURCE=<path>`.

## License

Tube Channel source code is MIT licensed; see [LICENSE](LICENSE). The provided plugin binaries are freeware. The package includes the applicable third-party license texts. See [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) for the dependency list and build requirements. Building from source requires a JUCE license that covers your use and distribution.
