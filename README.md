# Tube Channel

Analogue-modelled tube channel strip (preamp, opto compressor, EQ, output stage) by OpenGrid.

Status: working. The channel is simulated at circuit level (see below). CPU cost is the main open issue (see CPU).
See [docs/DESIGN.md](docs/DESIGN.md) for the specification.

## Quality modes

Default uses the reduced detector for lower CPU. HQ uses the original detector circuit at the same
oversampling factor. Derp uses the original detector with one extra oversampling stage. The preamp,
tube and warm feedback, compressor audio stage, optical response, EQ, output and supply retain their
circuit models in every mode. Default intentionally has the measured prototype timing differences;
choose HQ for the original compressor response.

The oversampling factor shrinks as the session rate rises:

| Session rate | Default | HQ | `\( ᐖ)/ - Derp mode` |
|---|---|---|---|
| 44.1 / 48 kHz | 2x | 2x | 4x |
| 88.2 / 96 kHz | 1x | 1x | 2x |
| 176.4 / 192 kHz | 1x | 1x | 1x |

Each mode runs minimum-phase IIR half-bands (no reported latency), or linear-phase FIR half-bands
with `+L` (latency reported to the host).

## Build

```sh
git submodule update --init                       # GoodLookinUI
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

Targets: VST3, Standalone, LV2 (all platforms), AU (macOS), CLAP (via clap-juce-extensions), AAX
(when the AAX SDK is found next to the repo). `-DTUBECHANNEL_UNIVERSAL=ON` builds arm64 + x86_64 on
macOS. `-DTUBECHANNEL_JUCE_SOURCE=<path>` and `-DTUBECHANNEL_CLAP_EXT_SOURCE=<path>` reuse existing
checkouts instead of downloading.

The engine test (`TubeChannelOversamplerTest`) has no JUCE dependency and runs anywhere.

## What it is

The audio stages are circuits solved sample by sample at the oversampled rate: the
discrete input stage with its current-feedback loop, the 12AX7 tube stage with its Warm feedback,
the opto compressor audio path, its mode-selected detector and the 12AT7 make-up stage, the four-band EQ, the output stage, and the power
supply (transformer, rectifiers, voltage multipliers, regulators, plate supply), whose sag and
ripple feed back into the audio circuits. Component values carry per-unit tolerances and the
circuits have noise.

Netlists live in `Netlists/`, are embedded at configure time, and were validated against ngspice
on the original circuit (operating point within 0.3 percent, gain within 0.05 dB, tube second
harmonic within 0.1 dB).

## CPU

Earlier all-circuit measurements on an Apple M1 Max, 2x at 48 kHz, 256-sample blocks, circuit noise and component
tolerances on, a -18 dBFS sine (the machine was not idle, so read these as upper bounds):

| | one block of audio costs |
|---|---|
| mono, supply model on | about 35 percent of real time |
| mono, supply model off | about 30 percent |
| stereo, supply model on | about 70 percent |
| stereo, supply model off | about 60 percent |

Before the optimisation pass (and at 4x) stereo cost about 170 percent. These measurements predate the new quality mapping; see [quality-mode validation](docs/measurements/quality-modes/README.md) for current settings and checks. The supply model can be switched off in the Unit panel (PSU SIM / FIXED): the rails then stay at
their settled values and the supply circuit is not solved.

What made the difference: fewer oversampling stages, a sparse solver whose solves are row-oriented and whose
factorisation is a precomputed program, a chord Newton iteration that refactors only when a device has moved
by more than 5 percent and accepts a first update a few tolerances wide, a blocked dense update for the linear
stages, a detector circuit that runs at 48 kHz however far the audio is oversampled, and stereo in lockstep:
the two channels share SIMD lanes in the sparse factorisations, triangular solves and linear-stage inverses
(`CircuitPair`), which gives the same result as two independent channels to better than -125 dB.

## License

The source code is MIT licensed, see [LICENSE](LICENSE). Third-party components and their licenses are listed in
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md). The released plugin binaries are freeware. Building from source
requires a JUCE license (AGPL-3.0 or commercial) appropriate for how you distribute the result.
