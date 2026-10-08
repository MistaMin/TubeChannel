# Tube Channel

**A circuit-modelled channel strip for macOS and desktop audio workstations.** Tube Channel combines a preamp, compressor, four-band equalizer, output stage, and optional power-supply simulation in one plug-in.

![Digital interface: input, compressor, equalizer, and output](docs/ui-digital.png)

The interface is available in a digital layout and an analogue-style layout. Both control the same audio model.

| Digital | Analogue-style |
|---|---|
| ![Digital interface](docs/ui-digital.png) | ![Analogue-style interface](docs/ui-analogue.png) |

## Download

The macOS universal installer supports Apple silicon and Intel Macs. It installs VST3, Audio Unit, CLAP, LV2, and the standalone app. Choose formats in the installer. The package includes the user guide and license texts. AAX is not included in the release.

Download the latest installer from [GitHub Releases](https://github.com/MistaMin/TubeChannel/releases).

## User guide

See the [Tube Channel User Guide](docs/USER_MANUAL.md) for the complete control reference, signal-flow diagrams, quality-mode details, and setup notes.

## Quality modes

Default selects the lower-CPU prototype compressor detector and runs at 2x oversampling at 44.1/48 kHz. HQ switches to the original modeled detector curve and keeps 2x oversampling. Derp uses that original detector at 4x. At higher session rates the oversampling factor falls to keep the internal rate within range:

| Session rate | Default | HQ | Derp |
|---|---:|---:|---:|
| 44.1 / 48 kHz | 2x | 2x | 4x |
| 88.2 / 96 kHz | 1x | 1x | 2x |
| 176.4 / 192 kHz | 1x | 1x | 1x |

The preamp, tube stage, warm feedback, compressor audio path, optical response, equalizer, output stage, and supply model remain circuit-based in every mode. The HQ mode retains the same modeled equalizer as Default. The supply simulation is off in a fresh instance to reduce CPU use; turn on **PSU SIM** in the System page when you want it.

Minimum-phase IIR filtering adds no reported latency. Selecting **+L LINEAR** uses linear-phase FIR filtering and reports its latency to the host.

## Audio model

The audio path is solved sample by sample at the selected oversampling rate. The detector is mode-selected and runs at the session rate. The supply model can feed changing rails back into the audio stages when enabled.

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

Tube Channel source code is MIT licensed; see [LICENSE](LICENSE). The package includes the applicable third-party license texts. See [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) for the dependency list and build requirements. Building from source requires a JUCE license that covers your use and distribution.
