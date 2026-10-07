# v1.1.0 optimization work

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
                     [amplitude=0.126] [block=256] [quality=0] [automate=0]
```

Example from the repository root:

```sh
cmake --build build/juce --target TubeChannelChainBench
build/juce/TubeChannelChainBench Netlists 2 1 1 800 0.126 256 0 1
```

Quality values are 0 for Default, 1 for HQ and 2 for Derp mode. `NOLOCK=1` selects independent scalar
channels for comparison. The benchmark reports average, p99 and worst wall time, deadline misses,
and average process CPU time. Process CPU excludes time spent descheduled; wall time determines
whether playback can meet a deadline. Run timing comparisons without other builds or tests running.

At 48 kHz/256 samples, Default stereo still needs roughly 3.9–4.0 ms per block with steady controls,
against a 5.33 ms deadline. Automation gains are modest and measurements vary with machine load.
This pass removes heap-related hazards; it does not yet make many simultaneous instances inexpensive.
Further work should profile the detector and nonlinear solves without relaxing their accuracy or
changing their circuits. Multi-instance host playback remains to be verified.
