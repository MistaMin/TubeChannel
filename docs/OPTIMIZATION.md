# Optimization history and development 1.1.3

The subsequent v1.1.2 redesign now permits behavioral approximations, targeting more than 80% CPU
reduction subject to listening approval. See [efficient sound model](SOUND_MODEL_PLAN.md). The
unchanged-equation work described below remains the reference engine. Compact detector/supply
prototypes and fitted-profile EQ/output modules now exist; see [hybrid engine](HYBRID_ENGINE.md).

This first pass removes avoidable work from the audio callback while retaining the circuit equations,
solver tolerances, iteration limits, oversampling factors, noise sequence and control smoothing.

- Allocate scalar solver scratch buffers and inverse layouts during circuit preparation.
- Allocate and pack stereo inverse storage before processing starts.
- Reuse control target maps; calculate targets and limiter coefficients only when controls change.
- Cache power-supply ports, matrix diagonal indices and plugin parameter addresses.
- Retire old chains on the message thread. Publishing a replacement also clears the retirement slot.

## Validation

The `realtime` test checks C++ heap allocations and frees inside `Chain::process`, including the first
block, variable block lengths, mono/stereo, automation, instrument gain switching, limiter operation,
HQ and linear-phase filtering at 48/96 kHz. The v1.0.0 source fails all eight scenarios; this revision
performs zero counted allocations or frees. This is a chain test, not a complete host memory audit.

The processor stress test also checks cached parameters through automation, state restoration and
presets, and successive live quality switches.

A same-machine comparison against the v1.0.0 source produced bit-identical output across 321,120 float
samples in eight scenarios. This comparison establishes equivalence for those workloads and compiler
settings, not every possible input or platform.

## Measuring CPU

Build `TubeChannelChainBench` in a Release configuration. Arguments are:

```text
TubeChannelChainBench <netlist dir> [channels=2] [tolerances=1] [psu=1] [blocks=400]
                     [amplitude=0.126] [block=256] [quality=0] [automate=0:off/1:gain/2:EQ]
                     [attack=0.5] [release=0.5] [bursts=0] [detectorOverride=-1] [compactSupply=0]
```

Example from the repository root:

```sh
cmake --build build/juce --target TubeChannelChainBench
build/juce/TubeChannelChainBench Netlists 2 1 1 800 0.126 256 0 1
```

Quality values are 0 for Default (prototype, 2x), 1 for HQ (original, 2x) and 2 for Derp mode (original, 4x) at 44.1/48 kHz. A detector override of 0/1 forces circuit/prototype for comparison; -1 follows quality. `NOLOCK=1` selects independent scalar
channels for comparison. The benchmark reports average, p99 and worst wall time, deadline misses,
and average process CPU time. Process CPU excludes time spent descheduled; wall time determines
whether playback can meet a deadline. Run timing comparisons without other builds or tests running.
Attack and release are normalized knob positions from 0 to 1. Burst mode uses a 220 Hz sine with
5 ms ramps, active from 0.1 to 0.4 seconds in each one-second cycle. The benchmark also reports
output peak and nonfinite samples; finite output alone does not establish that audio is free of clicks.

At 48 kHz/256 samples, Default stereo still needs roughly 3.9–4.0 ms per block with steady controls,
against a 5.33 ms deadline. Automation gains are modest and measurements vary with machine load.
This pass removes heap-related hazards; it does not yet make many simultaneous instances inexpensive.
Further work should profile the detector and nonlinear solves without relaxing their accuracy or
changing their circuits. Multi-instance host playback remains to be verified.

## Next profiling priorities

Before the reduced detector was selected for Default, an instrumented stereo run identified the detector as the largest individual stage (about
28% of measured stage time), followed by the tube and compressor audio circuits (about 16% each)
and the microphone circuit (about 14%). Timing instrumentation and scheduling affect these figures;
they identify priorities rather than predict an exact speedup.

Next candidates are device evaluation, matrix stamping/factorization and memory layout in those
solvers, with output-equivalence checks after each change. Parameter-expression bindings can reduce
work while automating controls. Keep circuit equations and accuracy intact; avoid retry work by improving convergence rather than relaxing stopping criteria.

The editor now skips spectrum transforms and display updates for hidden pages. This saves message-
thread work while the analogue or System page is selected; it does not reduce circuit solve cost.

The v1.1.1 development pass adds offline detector measurements. See
[detector investigation](DETECTOR_OPTIMIZATION.md) for the reference capture, initial findings and
the distinction between fitting measured behavior and reducing the existing equations.


## v1.1.1 detector convergence

The detector uses guarded Newton directions in mono and paired stereo. It keeps the circuit equations,
component values, sample cadence and stopping tolerances, while preventing large junction-voltage
excursions and unnecessary solve restarts. The new `detector_guard` test compares results against
much tighter solves of the same equations and checks overload convergence and stereo agreement.
See the detector investigation for numerical differences and validation limits. This addresses
expensive recovery spikes; multi-instance host performance still needs measurement.


## 1.1.3 paired linear solves

Profile Default with `TubeChannelChainProfile`; profiling is a separate target and does not enable
per-sample timers in the plugin. Both scalar and stereo profiles now identify all eleven stages plus
control/supply and meter/load work. Warm-up is excluded from the chain profile.

The retained EQ and other linear blocks use four-output inverse tiles for paired stereo. This cuts
intermediate memory traffic and avoids zero padding arithmetic while maintaining every node voltage,
capacitor/inductor/op-amp history and the original overload solver. Inverse storage is allocated during
preparation and refreshed in place when parameters change. No fitted EQ profile is promoted yet.

`REFERENCE_LINEAR=1` selects the preceding paired inverse layout in the offline chain benchmark;
`NOLOCK=1` selects the scalar path. Neither is a plugin parameter. Automation mode 2 exercises all
EQ levels/frequencies, bypass, range and bandwidth changes; mode 1 retains the original gain test.

The measured further reductions are 3.7% steady and 2.5% under continuous EQ automation. These are
relative process CPU savings from the 1.1.2 Default backend, not percentage points of host CPU or
an 80% reduction. The new profile identifies retained microphone, tube and compressor audio solves
as the largest remaining combined cost. See [raw results and validation](measurements/eq-optimization-1.1.3/README.md).


## 1.1.4 tube-law experiment

A smooth lookup for the existing triode current equation saves about 5.2% in one preliminary stereo Default timing set. The isolated curve error is tiny, but longer complete-channel comparisons diverge under several loud/changing conditions, and a loud harmonic sweep differs. It is kept as an offline experiment; all normal modes continue to use the original tube model. See [tube experiment and failed fidelity cases](measurements/tube-optimization-1.1.4/README.md).
