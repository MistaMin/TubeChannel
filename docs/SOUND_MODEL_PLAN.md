# Reduced sound engine — development 1.2.0

## Direction and version boundary

The user's October 9, 2026 brief supersedes the earlier requirement to retain the
microphone/instrument preamp, tube and warm-feedback circuits. Every runtime circuit
stage may now be replaced by a measured reduced model. Preserve audible nonlinear
character, loading, asymmetry, compression and relevant memory; component-level
circuit equivalence is no longer the implementation requirement.

VERSION is **1.2.0**. Eco is implemented as the default for new instances. The
current engine selector is Eco, Circuit, HQ, with independent oversampling. The
original circuit engine and legacy Derp processing remain available. Older states without the new Eco flag restore
with Eco off. Existing quality parameter IDs, values, ordering and automation ranges
are unchanged; a new boolean is appended with AU version hint 2.

The [measurement report](measurements/eco-1.2.0/README.md) records a passing 10x
minimum across the declared matrix, with 20x reached at 48 kHz only. The user subsequently requested packaging and publication of 1.2.0; see the
[release verification](measurements/release-1.2.0/README.md). Program-material
listening acceptance and actual multi-instance DAW playback remain unverified
and are disclosed in the release notes. The version bump alone does not certify sound.

Keep v1.1.4 as the sound and performance reference. Its exact clean source checkpoint
is `a80455549b9b92466f73d9017b8127fad779f013`. Retain the original circuit engine for
offline dataset generation and A/B comparison. Work is isolated on
`codex/reduced-engine-1.2.0`; the installed release is not replaced.

Saved parameter IDs, automation, state restoration, gain staging, external sidechain,
stereo link, dual mono, bypass and reported latency must stay compatible. Keep the
existing optical-response model initially. No additional audio processing threads.

## Independent controls follow-up

The engine selector is now Eco, Circuit (renamed Default), HQ. Oversampling is a
separate control: 2x, 4x, 8x (Derp symbol), or 1x when the selected button is toggled
off. Every factor is available in every engine. New instances retain Eco at 1x.
Old states keep their adaptive oversampling until the user changes an engine or
factor. The first engine click freezes the old effective factor. The legacy Quality
parameter range and original engine code are retained.

## Performance acceptance

**Minimum: 10x faster than v1.1.4. Stretch: 20x faster.**

Define speedup as reference process CPU time divided by candidate process CPU time
for identical input, controls, session rate, buffer size, channel count, quality,
noise, tolerances, supply setting, hardware and compiler optimization.
10x requires at most 10% of reference cost (90% lower CPU); 20x requires at most 5%
(95% lower CPU). The previous 5x / 80% goal is superseded. Results are recorded separately in the measurement report.

The initial October 9 native arm64 Release stereo Default measurement, 48 kHz,
PSU off, noise/tolerances on, seed 1, 0.126-peak 220 Hz sine, 350 blocks with the
first 51 excluded, yielded:

| Buffer | v1.1.4 CPU ms/block | 10x budget | 20x budget |
| --- | ---: | ---: | ---: |
| 256 samples | 2.346 | 0.2346 ms | 0.1173 ms |
| 64 samples | 0.582 | 0.0582 ms | 0.0291 ms |

These single short runs establish provisional budgets only. They are not an idle-M1
certification, a maximum-load benchmark or a claim of multi-instance reliability.
The 256-sample instrumented profile attributes about 69.5% of stage time to microphone,
tube and compressor audio, about 16.6% to EQ, and 1.2% to the detector. Timers affect
cost and exclude chain oversampling/scaling. GUI work is not measured by this native
harness. Neither the old tube-law lookup nor replacing EQ alone can meet this budget.

Freeze repeated baseline workloads before evaluating candidates: mono/stereo,
48/96 kHz, buffers 32/64/256, silence, ordinary input, full scale, bursts, loud gain,
slow compression, Auto, sidechain, PSU on/off and gain/EQ automation. Use equal audio
durations across buffer sizes and sufficient warmup. Alternate reference/candidate
order, with builds/tests/analyzers stopped. Record mean process CPU plus p99/worst
wall time, deadline misses and nonfinite samples. Use matched absolute budgets per
workload; do not select the slowest reference or change controls to inflate speedup.

## Runtime design

Replace Newton/matrix work with fixed-cost measured stages:

1. Preamp, tube/warm feedback and compressor audio: fitted pre/post filters around
   asymmetric nonlinear maps, with one or a few measured dynamic states. Sweep drive,
   gain, input/plate switches, warm feedback and relevant slow state offline. Factor
   out controls that only scale gain or time constants.
2. EQ/buffers/output: stable fitted filters over the actual control/rate domain,
   with a reduced coloration/overload model where measurements show it matters.
   The existing default-only 96 kHz fit is not sufficient for general use.
3. Detector and optical path: preserve causal feedback and the existing optical model;
   validate manual/Auto histories and slow release. The released Default detector
   already differs from the circuit detector, so compare against the correct mode.
4. Supply: fitted load/recovery/ripple states with measured rail/plate transitions.
   Control-rate updates must pass modulation tests. Do not initialize a runtime
   reduced engine by solving all original circuits.
5. Antialiasing: evaluate ADAA and local 2x nonlinear oversampling against the reference.
   Linear EQ, detectors and meters run at session/control rate when validated.
   The original quality modes retain their behavior; Eco is a separate host flag and
   runs at 1x with residual ADAA, with zero reported latency.
6. Runtime tables and coefficients are allocated/prepared outside processing.
   Block updates and parameter ramps feed fixed-size states; vectorize independent
   work where beneficial. No callback allocation, locks, circuit solves or live
   table fitting in the final reduced runtime path.

A generic saturator is not a calibrated replacement. Tiny device-law approximation
errors previously changed the nonlinear solver's recovery path; the new work must
identify complete stage behavior rather than insert another LUT into Newton.

## Delivery sequence and gates

1. Version and freeze the reference; make sample-rate/noise settings explicit in the
   benchmark. Establish the workload matrix and retain raw timing and sound captures.
2. Generate offline nonlinear datasets and fit the dominant preamp/tube/compressor
   stages. Separate fitting fixtures from fresh validation. Measure individual and
   combined stages, including downstream loading and supply feedback.
3. Assemble a complete experimental reduced chain, retaining original-engine A/B
   tools. Integrate control/rate-aware EQ, output, detector and supply models.
4. Check full-channel response, harmonic levels 2–5 versus drive/frequency, two-tone
   IMD, aliasing, impulses, bursts, gain-reduction trajectories and overload recovery.
   Preserve raw gain/delay differences; any level-matched listening normalization
   must be separately reported. Null differences diagnose changes, not a sound score.
5. Run no-allocation, finite/silence, automation, routing, state restoration, bypass,
   stereo and latency regression checks. Assess click-free transitions and recovery.
6. Require at least 10x CPU improvement across the declared operating workload matrix;
   report any failing mode or workload. Seek 20x without abandoning sound gates.
   Verify actual multi-instance host playback and callback tails on the target M1.
7. Present level-matched program-material A/Bs for listening acceptance before
   packaging a public release. Eco is already the local new-instance default as requested. Include vocals, drums, bass and
   dense mixes, with private recordings kept outside the repository.

No speedup or perceptual sound-match claim is accepted from a single sine benchmark.
The minimum performance target and sound acceptance both apply; a fast but rejected
model remains experimental. The reference implementation remains available offline.
