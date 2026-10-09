# Hybrid engine implementation history

> Current direction: [1.2.0 reduced sound engine](SOUND_MODEL_PLAN.md) supersedes
> the retained-circuit constraints and 80% target below. All stages may be reduced;
> the minimum target is 10x, with 20x as the stretch target. The sections below
> describe existing implementation and historical measurements.

## Structure

The existing channel is the shared orchestrator. `Chain` owns oversampling, calibration,
input/output trims, host-buffer chunking and stereo routing. `ChannelModel` preserves stage
order, control smoothing, feedback timing, rail distribution and meters. `ChannelTypes.h`
now holds the common control, netlist and telemetry contracts.

```text
host audio + optional external sidechain
                 |
        existing oversampling
                 |
     input/preamp circuits [retained]
                 |
 tube + warm feedback circuit [retained]
                 |
     preamp buffer circuit [retained initially]
                 |
 compressor audio circuit + optical response [retained]
                 |                         |
                 |                feedback detector input
                 |                         |
                 |               circuit OR compact detector
                 |                         |
                 |                  LED current -> optical response
                 |                     -> next audio sample
                 |
     EQ circuits [retained until profiles are fitted]
                 |
   output circuit [retained until profiles are fitted]
                 |
        existing downsampling -> output

circuit-derived loads -> circuit OR compact supply -> retained circuit rails
```

Engine selection happens during preparation, never through an audio callback rebuild.
No new threads or saved parameter IDs are introduced. The existing quality selector now chooses
Default: reduced detector, 2x; HQ: original detector, 2x; Derp: original detector, 4x, at 44.1/48 kHz.
Factors halve as session rate doubles. Supply remains a separate offline comparison option.
The full-path oversampling remains unchanged in this first prototype. Moving fitted filters
outside oversampling is a later routing change with separate phase/latency/aliasing checks.

## Parts already implemented

| Part | Implementation | Current readiness |
| --- | --- | --- |
| Retained analog stages | Existing stage circuits, shared by both paths | Production reference |
| Detector | `Source/Hybrid/Detector.h`: two directional charging paths, shared attack resistance, one capacitor, LED current | Connected to channel; offline fits fail some independent transient checks |
| Optical response | Existing `Vactrol` after either detector | Retained unchanged |
| Supply | `Source/Hybrid/Supply.h`: settled reference rail anchors, load/recovery states, optional measured ripple, smooth plate-rail transition | Connected to channel/telemetry; initial load estimates, ripple zero until fitted |
| EQ | `Source/Hybrid/Equalizer.h` and `FilterBank.h`: four coefficient-driven bands and common dry coupling tap | Default 96 kHz profile fitted and independently checked; original EQ remains pending full control/rate profiles |
| Output | `Source/Hybrid/Output.h`: fitted filters around an asymmetric measured transfer table | Default small-signal 96 kHz filters fitted; original output remains pending overload/memory and control profiles |

EQ/output profiles are data contracts, not generic EQ or saturation presets masquerading as
measurements. Identity profiles allow module verification; no identity profile replaces the
actual channel stage. A complete coefficient update is rejected if it contains unstable sections,
non-finite data or ambiguous transfer coordinates. Filter histories survive coefficient updates;
callers must reset outside playback for topology changes. Both wet and dry EQ taps advance,
so eventual EQ bypass need not return to a stale filter state.

The detector consumes the reference engine's already-smoothed electrical controls. It runs at
exactly the existing detector cadence, uses the same internal/external and stereo-link routing,
and emits LED current into the retained optical model. Compressor gain is still applied through
the retained audio circuit. Auto preserves the shared capacitor but disconnects the manual timing controls. Its charging-path approximation still requires better transient matching.
The compact supply starts from the original settled circuit, preserves freeze-on-disable semantics,
and feeds the same rails and telemetry; its heater reading is a settled anchor rather than a
new heater simulation. Detector load estimates and supply response remain experimental.

## Comparison entry points

For a native engine-only Release build:

```sh
cmake -S . -B build/hybrid -DTUBECHANNEL_BUILD_PLUGIN=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build/hybrid -j 4
ctest --test-dir build/hybrid --output-on-failure
```

`TubeChannelChainBench` appends `detectorOverride` (-1 follows quality, 0 forces original, 1 forces prototype)
and `compactSupply` (default 0) to its existing arguments. Run reference, detector-only, supply-only and combined variants sequentially,
without another build/test/analyzer workload, and repeat in alternating order.

`TubeChannelHybridDetectorCapture Netlists output.csv` produces six independent burst/recovery
fixtures: manual and Auto at short, middle and long attack/release. It reports LED error and the
resulting optical gain-reduction difference, without converting either into a sound percentage.
These captures support parameter fitting; retain independent holdout stimuli for validation.

The standard plugin now exposes both detector responses through quality modes. The previous detector
compile-time switch is retired so it cannot override HQ. `TUBECHANNEL_EXPERIMENTAL_SUPPLY` remains
Off by default for separate comparisons. The prototype has known timing differences; its promotion
to Default is a user-selected CPU/sound tradeoff, not a claim of sound matching or multi-instance readiness.

## Next fitting work

1. Fit rectifier transfer and timing against the new circuit captures, then validate on different
   drive levels, frequencies, thresholds, ratios and transient histories.
2. Fit supply sag, recovery and ripple against multiple loads/mains/plate settings. Seed-dependent
   component tolerances must have an explicit policy rather than silently disappear.
3. Capture stage EQ response over controls; fit stable sections and build control interpolation.
   Preserve common coupling response, gain, phase and audible overload coloration.
4. Fit the output transfer and filters versus master/load/drive; add dynamic states if a static
   table fails overload recovery. Retained circuits stay available during this work.
5. Measure complete-channel CPU and callback tails, then repeat Hammerstein, harmonic/2D sweeps,
   slow compressor timing and level-matched listening on an actual prototype plugin build.

An 80% whole-plugin CPU reduction has not been achieved. Retaining the preamp, tube and compressor
circuits limits savings; each replacement must earn its place through measured CPU and sound results.

## First results

The nine native engine checks passed, including the module fixtures and expanded no-allocation
coverage. Additional hybrid sidechain and stereo checks passed; paired versus independent
processing meets the -125 dB comparison gate. The initial detector capture remained finite
but differs by up to 18.698 dB in optical gain reduction. It is not ready for production.

The initial sequential stereo burst benchmark measured about 16.9% lower process CPU with
the detector prototype and 25.8% with detector plus supply. These are not sound-matched savings:
changed detector behavior also changes circuit loading. Some wall-time deadlines were still
missed. See [initial measurements](measurements/hybrid-initial/README.md) for raw data/settings.

## Repeated build/test/calibration results

The detector calibration cycle built v1.1.2 standard VST3/AU/standalone and a separate detector-only prototype VST3.
All 10 native checks passed, and both builds passed the processor and GUI checks. Eight detector fits
were explored, including a two-path scalar circuit with a shared timing capacitor. Its fresh long
recovery checks still differ by up to 7.50 dB in optical gain reduction. Complete-channel manual
maximum timing differs by 1.38 dB RMS in GR telemetry. Auto bursts are closer (0.17 dB RMS).

Sequential repeated stereo tests at 48 kHz/1024 samples measured 17.5% less process CPU for the final
candidate. This is not a sound-matched saving and does not establish multi-instance reliability.
Normal-level default EQ/output filter fits pass independent waveform checks, but overload does not;
one-pole supply fits also miss high-rail dynamics. EQ/output/supply replacements remain outside the standard
engine. The user subsequently selected the prototype detector for Default, with the original in HQ/Derp. See [calibration measurements](measurements/hybrid-calibration/README.md) for settings,
profiles, failed fidelity checks, Plugin Doctor exports and remaining domains to calibrate.


## 1.1.3 matrix optimization

The paired linear stages now use a four-output tiled inverse. Outputs stay in registers through the
matrix product; zero padding columns and extra output rows are omitted. The original passive,
op-amp and protection-device equations, physical histories and nonlinear fallback remain in use.
This applies to stereo linear solves in all quality modes; mono retains the scalar solver. The fitted
EQ/output profiles remain offline modules until their control/rate/overload domain is calibrated.

The new Default profile puts the detector near 1% of instrumented stage time, four EQ stages near
15.5%, and microphone/tube/compressor audio together near 62%. These timings include timer overhead
and exclude host scaling/oversampling. The next major savings therefore require work on the retained
nonlinear solvers as well as the EQ. See [1.1.3 measurements](measurements/eq-optimization-1.1.3/README.md)
for matching checks, actual CPU reductions and reproduction settings.
