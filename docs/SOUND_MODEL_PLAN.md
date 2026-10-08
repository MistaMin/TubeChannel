# v1.1.2 efficient sound model

## Scope and acceptance

The approved direction is a hybrid engine. Preserve the actual microphone/instrument preamp,
tube stage and warm-feedback circuit, and retain the existing optical-response algorithm. Simplify
the surrounding stages only after measurement and listening comparisons. The compressor audio
stage contains additional tube coloration and is a retention candidate until comparisons establish
that a cheaper implementation is acceptable.

The user permits some behavioral approximation, but preserving audible character takes priority
over the earlier aspiration of more than 80% lower CPU. That reduction remains an investigation
target, not an acceptance requirement that overrides retained circuits. No additional processing
threads are part of this redesign. Saved parameter IDs, automation, gain staging, sidechain routing
and stereo modes must remain compatible. Keep the original circuit engine as the comparison reference.

VERSION reads 1.1.2. This is the development version for this work, not a newly released binary.
Default now uses the reduced detector; HQ and Derp preserve the original detector at 2x and 4x
respectively at 44.1/48 kHz. Supply remains a separate comparison option; EQ/output modules accept fitted profiles but are not yet substituted
into the channel. See [HYBRID_ENGINE.md](HYBRID_ENGINE.md) for implementation and validation status.

CPU reduction means processing time below 20% of the current v1.1.1 reference on identical inputs,
settings, hardware, compiler and buffer size. The observed steady stereo reference of 3.946 ms at
48 kHz/256 samples gives a target below 0.7892 ms per block. The demanding stereo recording replay
averaged about 22.38 ms of process CPU at 48 kHz/1,024 samples, giving a target below about 4.48 ms.
These are budgets, not achieved results or performance predictions. Re-measure references alongside
each candidate, including callback tails and multiple active instances.

"90% of the sound" is a listening preference, not a defined numerical measure. Final acceptance needs
level-matched comparisons with the user. Numerical comparisons diagnose what changed; a waveform
error percentage alone cannot certify perceived similarity.

## Replacement signal path

```text
input trim / calibration
  -> retained input/preamp circuit
  -> retained tube and warm-feedback circuit
  -> preamp output gain
  -> feedback compressor gain cell + compressor coloration
  -> compressor output gain
  -> fitted shelving and parametric EQ filters
  -> output coloration + master/output calibration
  -> output trim / meters

compressor output or external sidechain
  -> rectification + detector states + attack/release/Auto behavior
  -> existing optical-response model
  -> compressor gain cell (preserve causal feedback timing)

estimated signal load
  -> compact supply-sag/ripple states
  -> modulation of relevant nonlinear models
```

## What changes

| Existing work | Proposed replacement | Character to measure and retain |
| --- | --- | --- |
| Microphone and instrument circuits | Retain the existing circuits; optimize implementation | Gain-knob law, impedance/loading response, low cut, overload onset and recovery |
| Tube circuit and its feedback network | Retain the tube and warm-feedback circuits | Asymmetry, harmonics versus drive/frequency, both gain and plate settings, transient memory |
| Compressor detector circuit | Small stateful detector model fitted to transient measurements | Threshold/ratio dependence, slow timing, program dependence, Auto behavior and feedback topology |
| Compressor audio circuit | Retain initially; evaluate simplifying buffers while preserving tube coloration | Compression amount, makeup-gain law, tube coloration and behavior under overload |
| EQ circuits and intermediate buffers | Cascaded digital filters fitted across knob settings, with coloration retained where audible | Actual shelf/peak shapes, phase, bandwidth/range switches and control interactions |
| Output circuit | Calibrated gain, filters and nonlinear output model | Output level, drive response and clipping character |
| Power-supply circuit | Low-cost load envelope, sag/recovery and ripple model | Audible modulation under sustained drive and changing loads |
| Full-path oversampling | Local oversampling around nonlinear coloration stages | Aliasing and transient response; account for changed filter phase/delay |

The existing Vactrol model is already a small stateful optical-response algorithm. Reuse it initially;
evaluate replacing the detector that feeds it. Retain the compressor audio circuit initially. Do not reduce the whole
compressor to a generic peak detector and one gain multiplier without measuring its dynamic behavior.
Likewise, one generic saturation curve is not sufficient evidence of a tube-stage match.

The current implementation runs all audio-stage circuits at the oversampled rate, while the detector
already runs at approximately 48 kHz. Merely lowering the whole chain's Default rate from 2x to 1x
cannot by itself deliver an 80% reduction and can increase aliasing. Retain the existing 2x filtering
around the retained nonlinear circuits; validate any further reduction separately. Run ordinary linear
filters and gain stages at the host rate when their fitted response permits it. Detector and supply
state cadence must be selected through transient and modulation tests, not an arbitrary low rate.

## Development order

1. Freeze reproducible reference workloads and collect stage measurements. Record gain/frequency
   response, distortion versus drive/frequency, overload recovery, compression curves and transient
   histories across control settings. Include combined stages: downstream loading and supply feedback
   mean isolated measurements alone cannot establish a complete-channel match.
2. Establish Plugin Doctor baselines for the released reference and solver-fixed engine, with
   explicit matched states. The Plugin Doctor captures compare released and solver-fixed circuits; do not label them
   as a hybrid comparison. Follow the supplied v2.7 manual and the measurement protocol.
3. Prototype cheaper EQ/buffer processing and a stateful detector while retaining the preamp,
   tube, warm-feedback and compressor audio circuits. Validate each change against the baseline.
4. Evaluate a compact supply model through sag, recovery and modulation measurements. Retain
   the circuit supply option until listening demonstrates that its approximation is acceptable.
5. Evaluate running fitted linear processing at the session rate while retained nonlinear circuits
   remain oversampled. Re-check response, phase, latency and aliasing after any routing change.
6. Integrate validated replacements with controls, saved states and meters. Compare complete-channel
   sound and CPU in Plugin Doctor and host playback, including slow compressor timing. Report the
   attainable CPU reduction honestly; retained nonlinear circuits limit the potential saving.

Detector-only optimization cannot deliver an 80% whole-plugin reduction: the earlier instrumented
run attributed about 28% of stage time to it. The tube, compressor audio and microphone stages were
about 16%, 16% and 14% respectively. These measurements prioritize work; they do not predict the
speed or quality of replacements. An 80% reduction may be unattainable while retaining the selected circuit stages. Measure the hybrid
before setting a firm saving target.

## Validation gates

- Keep independent measurement fixtures separate from those used to fit models. Cover multiple
  rates, drive levels, tolerance seeds, controls, source impedances and input combinations.
- Compare complete-channel frequency response, harmonics, gain-reduction trajectories, attack/release,
  Auto timing, silence/noise, clipping and overload recovery. Publish errors rather than claiming
  "90%" from a single correlation or null test.
- Include vocals, drums, bass, dense mixes, impulses, bursts and high-frequency signals. Preserve
  private recordings outside the repository. Use matched output levels for listening comparisons.
- Require finite stable output, clean silence, safe bypass transitions and no callback allocations.
  Test automation, restoration, external sidechain, linked stereo and dual mono.
- Alternate benchmark order with no concurrent builds or tests, repeat runs and compare both average
  CPU and callback tails. Verify actual multi-instance host playback at 48 kHz/1,024 samples and
  smaller buffers. An inexpensive steady tone alone is not success.

Keep the UI focused on the sound and controls. Do not expose circuit implementation details as a
mandatory step in ordinary use. Signing/notarization continues through the user's Terminal workflow.
