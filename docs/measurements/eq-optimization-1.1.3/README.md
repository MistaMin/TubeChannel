# 1.1.3 paired linear-solver measurements

The preceding 1.1.2 detector/quality engine is checkpointed at `04a8676`. These measurements use
1.1.3 arm64 Release native tools and identical circuit parameters. The candidate changes paired
linear matrix layout, not the fitted detector calibration, oversampling or any audio circuit.
Reference mode selects the preceding column inverse through `REFERENCE_LINEAR=1`.

## CPU comparisons

Three reference and three candidate runs per case, sequential order reference/candidate/candidate/
reference/reference/candidate. No build or test was running during timing. Other desktop apps remained
open; the machine was not verified idle. CPU time excludes descheduling; wall time includes it.

| Default stereo, 48 kHz, 1024 samples | Reference CPU ms/block | Tiled CPU ms/block | Relative reduction |
|---|---:|---:|---:|
| Steady 220 Hz sine, 0.126 peak | 11.3913 | 10.9683 | 3.71% |
| Continuous EQ automation, 0.8 peak, slowest attack/release | 12.8747 | 12.5493 | 2.53% |

Both cases use 2x oversampling, original supply simulation, noise and component tolerances on,
350 blocks (first 51 excluded), seed 1, linked stereo. EQ automation changes shelf/mid levels,
frequencies, bypass, ranges and bandwidth. No audio alignment, gain correction or modified solver
accuracy is applied. Every run produced finite output. Some wall-time deadlines were missed in both
layouts; this does not establish reliable multi-instance playback.

Exact arguments and per-run numbers are in `benchmark-final.json`; named raw logs sit alongside it.
`benchmark-initial.json` and `benchmark-N-*.txt` describe the first trial before padding/tail work.

```sh
cmake --build build/hybrid --target TubeChannelChainBench TubeChannelChainProfile
REFERENCE_LINEAR=1 build/hybrid/TubeChannelChainBench Netlists 2 1 1 350 0.126 1024 0 0
build/hybrid/TubeChannelChainBench Netlists 2 1 1 350 0.126 1024 0 0
REFERENCE_LINEAR=1 build/hybrid/TubeChannelChainBench Netlists 2 1 1 350 0.8 1024 0 2 1 1
build/hybrid/TubeChannelChainBench Netlists 2 1 1 350 0.8 1024 0 2 1 1
```

## Profile and correctness

`profile-before.txt` is the initial Default profile before the layout change. The detector accounts
for about 1.1%, four EQ stages (including optical work in EQ1) about 15.5%, and microphone/tube/
compressor audio together about 62%. These are instrumented shares with timer overhead, including
controls/supply/meters but excluding host oversampling/scaling. The final reference/candidate profiles
show the retained stage distribution; they are diagnostic, not the timing used for CPU savings.

`linear_layout` compares all node voltages against the preceding paired solver for the instrument,
preamp buffer, four EQ stages and output, with different left/right waveforms. It exercises control
extremes, switches, parameter rebuilds, rail motion, overload, silence and recovery at 44.1/48/88.2/
96/192/384 kHz. Tolerances are on except the 48 kHz fixture. All 19,070,976 node comparisons matched
exactly on this arm64 build. Whole-chain 48/96 kHz checks for Default/HQ/Derp also produced identical
float samples under EQ automation, timing changes, Auto, limiter and stereo/dual-mono routing.
The reported -300 dB is the diagnostic floor for zero observed residual, not a measured noise floor.

See `native-tests.txt`, `host-tests.txt` and `layout-equivalence.txt` for complete results. Local plugin
build identities are recorded separately after the rebuild. The macOS VST3 build now repeats JUCE's local ad-hoc bundle check after moduleinfo.json generation, which otherwise invalidates the earlier seal. AU and standalone builds also refresh stale local seals after relinking. This is separate from the installer's Developer ID signing/notarization flow. Existing Plugin Doctor exports remain
historical comparisons of detector behavior; no new Plugin Doctor measurement is claimed for this
layout pass. The active Plugin Doctor session was preserved.

## Remaining work

The fitted default EQ/output profiles remain offline: they have not been calibrated across controls,
component tolerances, sample rates and overload, and cannot safely replace the circuit in general.
The new layout keeps circuit clipping and state continuously active, including bypass. Mono is unchanged.
Further large savings must address the retained nonlinear preamp/tube/compressor solves without
losing their audible behavior. Multi-instance host playback, Windows/Linux and x86_64 validation remain open.
