# From HQ circuit processing to Eco: an engineering handoff guide

This guide explains how Tube Channel gained its Eco engine, why the major decisions
were made, and how another team can repeat the process on a similar audio product.
It covers the reduction delivered in 1.2.0 and the integration lessons carried into
1.2.1. It is based on the retained code, measurement reports and release records.

**The architectural change was to move expensive circuit evaluation into offline
measurement tools and ship a fixed-cost approximation of the measured behavior.**
The original engine remained available for users, comparison and future calibration.

“HQ to Eco” describes the product direction, not the denominator of every result.
Tube Channel's published Eco speedups use **v1.1.4 Default**, subsequently labeled
Circuit, as the reference. HQ has a different detector selection. Do not relabel
these measurements as HQ-versus-Eco results or assume the engines sound identical.

## How to use this guide

Read sections 1–4 before changing DSP. Sections 5–9 describe model identification
and implementation. Sections 10–13 cover product integration and validation.
Sections 14–17 provide reproduction commands, handoff requirements and a reusable
project template.

Throughout this document:

- **Implemented/recorded** describes this repository and its saved evidence.
- **Recommended** identifies work a future team should do; it does not imply that
  Tube Channel completed that work.
- A passing engineering test establishes only the property that the test checks.
  It does not establish perceptual equivalence, reliable operation in every DAW,
  or performance on an unmeasured machine.

## 1. Write the behavioral contract before optimizing

The initial problem was a full channel strip whose circuit processing cost limited
practical instance counts. The revised brief allowed all runtime circuit stages to
be replaced, provided useful nonlinear character and relevant dynamics survived.
The performance requirement increased from 5x to a **10x minimum**, with **20x**
as the stretch target. Eco became the default for new instances; existing code and
old sessions had to survive.

This permission mattered. Preserving every circuit equation and making a useful,
much cheaper audio model are different engineering objectives. A team must know
which one it has been asked to deliver.

Record the following before choosing an approximation:

| Contract area | Tube Channel decision | What the next team must decide |
|---|---|---|
| Reference | Pinned v1.1.4 source and explicit mode | Exact version, mode, controls and assets |
| Sound | Preserve useful coloration, asymmetry, compression, loading and memory | Audible traits and allowed departures |
| Performance | At least 10x in the declared matrix; seek 20x | Minimum speedup and absolute callback budget |
| Original engine | Keep Circuit/HQ and legacy processing | Comparison, fallback and user access |
| Defaults | Eco/1x for new instances | Whether existing projects may change |
| Compatibility | Preserve parameter IDs, order, automation and state | Host-facing invariants |
| Runtime | No extra audio threads; no Eco circuit solves | Allocation, locking, threading and latency limits |
| Acceptance | Objective checks plus listening and host checks | Named owners and pass/fail thresholds |

**Deliverable:** a written contract, baseline manifest and acceptance matrix.
Do not use a version bump as evidence that the new sound has been accepted.

## 2. Freeze a reproducible reference

The source checkpoint was:

```text
v1.1.4 source: a80455549b9b92466f73d9017b8127fad779f013
initial reference architecture: arm64
reference optimization: -O3 -DNDEBUG -std=gnu++20
macOS deployment target: 11.0
```

The [baseline manifest](measurements/reduced-engine-1.2.0/reference.json) records
reference executable hashes. The circuit model and netlists remained in the
repository, and experiments were separated from normal plugin behavior.

For a new project, save source revision, executable hash, compiler and flags,
dependency revisions, machine/OS, sample rate, buffer size, routing, input signal,
all controls, random seed, noise, tolerances, supply setting and warmup duration.
Save the actual measurement command and raw output with each result.

Keep the reference independently buildable. If the candidate overwrites the
reference executable or shares mutable generated assets with it, later A/B results
can become comparisons against the wrong implementation.

The benchmark harness was extended to make previously implicit settings explicit.
When instrumenting an older build, apply only the measurement changes to its pinned
DSP. Archive that harness patch and prove that it did not alter reference output.
An old executable that silently ignores new arguments is not a matched reference.

**Exit criterion:** another engineer can regenerate the same reference workload and
identify its binary without relying on a developer's current build directory.

## 3. Profile the complete path and calculate the budget

The initial instrumented stage profile attributed approximately:

| Work | Share of instrumented stage time |
|---|---:|
| Microphone, tube and compressor audio | 69.5% |
| EQ | 16.6% |
| Detector | 1.2% |

These timers affected execution and excluded chain oversampling/scaling. They were
useful for prioritization, not a complete plugin CPU accounting. Full processor
measurements later included parameter reads, meters and spectrum work.

The provisional 48 kHz stereo Default baseline gave the following process CPU
budgets per block:

| Block | Reference | 10x candidate budget | 20x candidate budget |
|---|---:|---:|---:|
| 64 samples | 0.582 ms | 0.0582 ms | 0.0291 ms |
| 256 samples | 2.346 ms | 0.2346 ms | 0.1173 ms |

Use these equations:

```text
speedup = reference process CPU / candidate process CPU
candidate budget = reference process CPU / required speedup
callback deadline in ms = 1000 * block frames / sample rate
```

A 10x speedup means 90% less processing time; 20x means 95% less. A “500% reduction”
is not a useful requirement. Also distinguish relative improvement from meeting the
audio deadline: a faster engine may still be too slow for a particular workload.

The architectural implication follows from the profile. Even making the detector
free could not meet the target. Accelerating EQ alone could not meet it either.
Most of the expensive runtime structure had to disappear.

**Recommended:** allocate budgets to model stages, oversampling and host overhead.
Re-profile after each large replacement; the bottleneck moves.

## 4. Learn from the approaches that did not justify promotion

Before the full reduction, a tube device-law lookup experiment accelerated a small
part of the existing solver. Its preliminary matched benchmark saved about **5.18%**
of process CPU. It did not approach the later 10x target.

More importantly, a high-drive fixture failed: the output residual was about
**−26.1 dB RMS**, with some harmonics differing by up to **2.95 dB**. Small device-law
errors could change the iterative solver's recovery trajectory. Accurate local
function values did not guarantee accurate behavior of the complete nonlinear loop.
The experiment stayed out of the normal tube path.

The earlier hybrid detector calibration also retained failed fidelity results.
Those experiments are useful engineering evidence, but they do not certify Eco's
detector or establish that manual/Auto compression matches HQ.

The reusable decision rule is:

1. Measure the fraction of total cost that an optimization can remove.
2. Validate the complete stage and its feedback, not only the substituted function.
3. Reject a candidate that fails the required operating domain.
4. Preserve failed results so the next team does not repeat the same assumption.

See the [tube experiment](measurements/tube-optimization-1.1.4/README.md) and
[hybrid calibration report](measurements/hybrid-calibration/README.md).

## 5. Decompose behavior into filters, curves and states

The new design replaced repeated Newton/matrix work with a short measured model:

```text
input and loading correction
  -> measured input-stage filters and nonlinear maps
  -> tube filter and asymmetric map
  -> preamp output and causal compressor/optical response
  -> measured EQ/buffer filters
  -> output filter and nonlinear map
  -> reduced supply interaction
```

This is an architectural sketch. For the exact sample order, read
[`EcoModel::process`](../Source/Hybrid/EcoModel.h). Retaining stage boundaries made
gain staging, troubleshooting and routing easier than fitting one opaque mapping
from plugin input to output.

| Circuit behavior | Reduced implementation | Main risk to validate |
|---|---|---|
| Small-signal frequency response | Fixed-capacity second-order filter sections | Phase, stability, sample-rate conversion |
| Asymmetric saturation | Measured piecewise-linear maps | Drive dependence, harmonics and extrapolation |
| Gain and pot loading | Explicit gain/loading expressions | Control law and interstage level |
| Input impedance | Source-loading gain and instrument filter correction | Pickup/source interaction |
| Compression | Reduced detector with retained optical response | Feedback timing, Auto and recovery |
| Supply | Reduced load/recovery/ripple state anchored to measured rails | Modulation and overload behavior |
| Noise/tolerances | Seeded approximate noise and gain spread | Statistical behavior and reproducibility |

Factor a control out of a table when it only changes a known gain or time constant.
Keep it in the measured domain when it changes shape, topology or loading. Tube
plate and +20 settings received separate profiles. EQ range and width switches
received separate profile families.

**Recommended:** if a static map cannot explain attack/release hysteresis, introduce
the smallest measurable dynamic state that fixes the failure. Do not hide memory
errors by averaging them into an attractive transfer curve.

## 6. Build the offline measurement pipeline

[`tests/eco_capture.cpp`](../tests/eco_capture.cpp) constructs the retained circuit
offline. That executable is not linked into the plugin. It captured nominal
**96 kHz** behavior with noise and PSU modulation disabled, recording nominal rails
separately. This isolates the behavior being fitted.

The capture set contained **311 linear impulse profiles** and **56 drive captures**
across **eight nonlinear families**. The fixtures covered EQ grids and discrete
switch settings as well as several nonlinear drive levels.

The tool writes:

- `fixtures.csv`: stage, controls, switches and drive amplitude for each capture.
- `*-impulse.bin` / `*-curve.bin`: double-precision input/output pairs.
- `*-valid.bin`: per-sample reference convergence flags.
- `rails.txt`: nominal supply anchors.
- Console diagnostics: DC solve status and unconverged steps.

The impulse captures used 65,536 samples and a 0.001 input impulse. Drive captures
used 24,576 samples of a 1 kHz sine at several amplitudes. These are identification
fixtures, not a general-purpose proof of the model's accuracy.

Reference quality must be checked before fitting. Two high-drive tube captures
contained 127 and 12 unconverged steps. The nonlinear fitter excluded invalid steps
and their following recovery neighborhood, using a 193-sample quarantine window at
96 kHz. It fitted the settled final segment after sample 16,384.

**Recommended:** give DC failure, invalid sample count and excluded duration explicit
acceptance thresholds. Re-capture a domain that lacks enough valid data. A fitting
script finishing successfully is not evidence that its source circuit converged.

Keep three distinct datasets: identification data, development validation and an
untouched final holdout. Once a holdout informs a model change, treat it as development
data and generate a fresh holdout.

## 7. Fit linear behavior without losing stability or control meaning

[`tools/fit_eco.py`](../tools/fit_eco.py) turns the captures into
[`EcoCalibration.h`](../Source/Hybrid/EcoCalibration.h). NumPy fitting runs offline;
the shipped processor uses the generated coefficients and fixed-size arrays.

The linear fitting process:

1. Remove the measured tail offset and normalize the impulse by its input amplitude.
2. Transform to frequency response and use relative weighting across the spectrum.
3. Try low-order rational fits, rejecting unstable poles.
4. Represent the result as normalized second-order sections, with capacity limited
   to four sections per runtime profile.
5. Save magnitude/phase errors and reject unacceptable generated EQ profiles.

EQ required extra structure. Flat settings make canceled poles difficult to identify
from output alone. The fitter used the actual passive topology to anchor pole groups,
then fitted the measured **complete stage**, including finite amplifier bandwidth
and loading. It retained consistent section identities across the control atlas.

That consistency matters during interpolation. Independently fitted filters can
exchange pole order or change effective order as a knob moves. Coefficients that
look similar in a table may then interpolate into the wrong response. Tube Channel
also applies a frequency transformation to the middle-band resonator section before
blending neighboring profiles.

At captured EQ settings, the final correction profiles had maximum errors of about
**0.0014 dB / 0.0163°**, measured over 20 Hz–20 kHz at the calibration rate. The fitter's
generated EQ gate was 0.1 dB / 1°. Use the `correction_*` entries in
[`fit.json`](measurements/eco-1.2.0/fit.json) for the final EQ atlas; earlier raw-fit
entries are retained in the same file.

These are training-domain fitting errors. They do not establish accuracy between
grid points, after sample-rate conversion, at overload or for the complete channel.
The independent off-grid fixture differed by approximately −0.14 dB at 96 kHz.

**Recommended:** sweep interpolation interiors, control boundaries and supported
sample rates. Check pole stability throughout automation, not only at grid corners.
Preserve filter histories when changing coefficients; use deliberate reset or
crossfade behavior when changing topology.

## 8. Fit asymmetric nonlinear maps and control aliasing

The nonlinear fitter accounts for the measured linear stage response at 1 kHz
before pairing drive values with output. It averages nearby samples from rising
and falling cycles, resamples the result to **1,025 points**, and removes the
zero-input offset. It blends the center toward the calibrated small-signal slope
so that a high-drive fit does not corrupt ordinary gain.

Multiple drive captures are combined into one curve per family. Smaller valid
domains supply the central region; wider captures supply the extremes. The fitter
records a hysteresis residual rather than claiming a static map explains all memory.

Runtime antialiasing uses first-order antiderivative antialiasing, or ADAA, on the
**nonlinear residual**. Conceptually:

```text
measured map: f(x)
nonlinear residual: r(x) = f(x) - x
R(x): an antiderivative of r(x)

output[n] = x[n] + (R(x[n]) - R(x[n-1])) / (x[n] - x[n-1])
```

For tiny input differences, the implementation evaluates the residual at the
midpoint to avoid numerical cancellation. The antiderivative is prepared from the
piecewise-linear table. Keeping the identity path separate avoids applying the
ADAA averaging effect to the entire calibrated linear path.

Endpoint behavior is part of the model. Microphone/output captures ended before
their buffers clipped, so runtime code extends the measured endpoint slope to a
known rail bound instead of clipping immediately at the table edge. Input and
output bounds protect extreme processing; they do not prove that overload is faithful.

Eco defaults to **1x**, with zero engine oversampling latency. Later integration
made 2x/4x/8x independently available with every engine through the shared oversampler.
The released follow-up oversamples the Eco chain; stage-local oversampling from the
initial brief was a possible design direction, not the implemented architecture.

**Recommended:** measure aliasing with high-frequency drive, sweeps and two-tone
inputs. ADAA reduces a class of aliasing problems; it is not a promise of zero
aliasing or parity with HQ. Repeat the performance matrix for every advertised
oversampled setting rather than reusing the 1x result.

## 9. Keep dynamics causal and make their cost predictable

Eco retained the optical response model and used a reduced detector at approximately
**12 kHz**, a supply model at approximately **8 kHz**, and coefficient/gain smoothing
updates at approximately **1.5 kHz**. Actual cadence is derived from the internal
sample rate with integer strides, so these are target rates, not exact rates at
every supported sample rate.

The detector integrates input over its stride and ramps gain reduction between
updates. The internal feedback detector uses the prior gain state; stereo linking
uses the other channel's prior detector level. These choices preserve a causal
loop and avoid a channel-order dependency.

Control counters continue across host blocks. A 32-frame callback and a 257-frame
callback must not produce different dynamics just because update counters restart
at different boundaries. Tests compare those partitions exactly for a fixed input.

Other implementation details include:

- Gain/loading compensation outside the nonlinear tables.
- Smoothed filter and gain targets with retained histories.
- A short tube-profile crossfade, with the new shape's history initialized.
- Smoothed compressor/EQ bypass and explicit limiter state handling.
- Fixed-size filter/table state and tiny-state cleanup against denormals.
- ARM NEON for the pair of filter-history updates, with a scalar fallback.

The speedup should primarily be attributed to replacing solver work and reducing
default oversampling. The record does not isolate a standalone speedup for NEON;
it does not demonstrate that every nonlinear stage was vectorized.

The reduced supply is an approximation anchored to nominal measured rails. It is
not a circuit-equivalent, independently validated supply modulation model. Noise
and tolerance controls likewise approximate variation rather than reproducing
every original component's behavior.

## 10. Integrate Eco without changing existing projects

[`Chain.h`](../Source/Plugin/Chain.h) selects either EcoModel or the retained circuit
model. Eco preparation works with empty circuit data, providing a useful regression
check that the cheap path does not quietly initialize the expensive circuit first.

The host integration kept existing Quality IDs, ordering, range and automation
mapping. An appended `ecomode` boolean used AU version hint 2. Old state XML missing
that parameter explicitly restores Eco **off**, while new instances default to Eco.

This separates two requirements that are often accidentally conflated:

```text
new instance -> use the new default
old project  -> restore the behavior that project saved
```

The original three modes matched saved reference output byte-for-byte over 491,520
float samples in the recorded compatibility comparison. That establishes the
tested cases; it is not a proof for every possible project.

The GUI labels became **Eco, Circuit, HQ**. Oversampling became an independent
selection: 2x, 4x or Derp/8x; clicking the selected button returns to 1x. Missing
oversampling state restores legacy adaptive behavior. The first explicit mode
change freezes the old effective factor before switching engines.

For a new project, test the parameter migration before relying on GUI labels.
Hosts store IDs and normalized automation values, not the user's memory of a name.
Appending parameters is generally safer than renumbering or reinterpreting existing
ones, but still requires host and state round-trip tests.

## 11. Validate sound, runtime safety and performance separately

Use separate reports for these questions:

| Question | Appropriate evidence | What it cannot prove |
|---|---|---|
| Does the fit describe its captures? | Filter/curve fit diagnostics | Generalization or audible acceptance |
| Does the full chain retain behavior? | Fresh fixtures, harmonics, IMD, timing and residuals | Listener preference or every source signal |
| Is processing safe? | Allocation, finite, routing, state and latency tests | Sound equivalence |
| Is it fast enough? | Matched timing matrix and deadline tails | Real-DAW stability on another machine |
| Is the trade acceptable? | Program-material listening and host sessions | Automatic certification of untested modes |

### Sound checks and the actual limits found

The holdout tool exercised 20 fixtures at each of 48/96 kHz, including fresh
frequency/drive settings, plate/+20 combinations, compression timing and Auto,
off-grid EQ, bypass, external sidechain, two-tone IMD, loud treble and high instrument
source impedance. It records raw levels, harmonics 2–5, IMD probes, gain reduction
and direct waveform residuals in [`holdout.csv`](measurements/eco-1.2.0/holdout.csv).

Do not normalize away a gain error in the objective report. For listening, make a
separate level-matched set and preserve its normalization metadata. Direct residuals
also include phase differences: removing the original 2x minimum-phase filters at
48 kHz changes phase even where the tone is similar.

Recorded examples expose meaningful limits: the 96 kHz two-tone fixture had an
approximately −39.1 dB waveform residual; a slammed high-frequency/+20 fixture had
only about −6.1 dB. Moderate operation was much closer than severe drive. Neither
number is a “percentage of sound preserved.”

Synthetic drums, bass and chords were generated for review with identical −12 dB
output trim, not independent loudness normalization. They were not listening signoff.
Real vocals, drums, bass and dense-mix acceptance remained unverified in the records.

**Recommended listening gate:** appoint a sound owner, use repeatable private source
material, compare raw and level-matched renders, include clean/driven/slammed cases,
and record acceptance or the exact rejected domain. Keep confidential recordings
outside the repository. Add richer dynamic states or restrict claims if the model
fails; do not retune only to the failing clip.

### Runtime and host checks

Recorded tests covered finite extremes, silence tails, buffer-partition independence,
callback allocation/free, routing, mono/stereo, stereo link, Dual Mono, external
sidechain, automation, mode changes, state restoration and new defaults. Later
checks exercised all engines with independent 1x/2x/4x/8x at 48/96 kHz.

Test allocation and destruction in the actual processor callback, including adoption
and retirement of prepared engines. Testing an isolated waveshaper is insufficient.
Prepare expensive banks away from realtime processing and move retired-bank cleanup
off that callback as well.

**Recommended host gate:** run the target DAWs with an audio device, many instances,
small buffers, UI interaction, automation, bypass, reopening sessions and long tails.
Record underruns and audible artifacts. Offline harnesses do not replace this work.

## 12. Benchmark with a declared matrix and retain every run

The recorded matrix used two channels, 48/96 kHz, 32/64/256-frame blocks, ordinary
sine, silence, full-scale EQ automation with PSU, and full-scale bursts with PSU.
It matched controls, seed, noise/tolerances and audio duration. Each case used
0.5 seconds of warmup and 1 second of measurement, repeated three times per engine
in **reference/Eco/Eco/reference/reference/Eco** order. Builds and analyzers were stopped.

[`tools/benchmark_eco.py`](../tools/benchmark_eco.py) saves arguments, executable
hashes, raw logs, CPU means and missed deadlines. Raw logs retain callback timing
tails. Its fixed metadata describes this experiment; update it for another project
or compiler rather than inheriting an inaccurate description.

| Rate | Measured Eco/1x speedup against v1.1.4 Default | Acceptance |
|---|---:|---|
| 48 kHz | 26.64x–35.11x | Exceeded 10x minimum and 20x stretch |
| 96 kHz | 13.98x–18.61x | Exceeded 10x minimum; missed 20x stretch |

The 96 kHz legacy reference had a different effective processing cost under its
adaptive quality mapping. The falling ratio is a reminder to inspect absolute CPU
and the actual oversampling factor, not infer that Eco gets proportionally worse.

A separate full JUCE harness processed 16 serial instances. Eco had zero deadline
misses in 256 measured callbacks per case. This included processor overhead, but
did not operate a real DAW or audio device. The machine was a local arm64 Mac; these
results are not certified performance numbers for every M1 Max.

**Recommended:** use longer repeated runs for production capacity decisions, capture
thermal/power conditions, test the target machine, and require headroom in worst-case
callback time. Do not choose the slowest reference run or change signal settings to
inflate the speedup. Expand the declared matrix before making broader claims.

## 13. Separate playback quality from offline rendering quality

The 1.2.1 follow-up made the architecture more useful: users can work in Eco while
selecting any of nine engine/factor combinations for offline rendering.

- New playback instances remain Eco/1x; new offline settings default to HQ/8x.
- Only the host's `isNonRealtime()` signal selects the offline engine.
- Realtime exports remain realtime and use playback settings.
- Playback and bounce have independently prepared engines and histories.
- Repeated bounces and host resets restart the offline engine deterministically.
- Linear-phase paths use delay padding to keep reported latency stable across
  playback and bounce profiles while the override is enabled.

This was an integration feature, not a new Eco sound model or new Eco benchmark.
The [1.2.1 report](measurements/offline-bounce-1.2.1/README.md) documents exact render
comparison, state migration, zero realtime callback allocations/frees and remaining
manual host/UI checks.

For a future product, plan rendering policy, lifecycle and latency together. A cheap
playback mode and an expensive offline mode are only useful if the host gets the
correct engine from the first rendered sample and the live engine resumes cleanly.

## 14. Reproduce the engineering pipeline

Run these commands from the repository root. Paths are examples for a new build
directory. Configure JUCE/CLAP source overrides as needed for the local environment.
Do not overwrite the committed calibration header during an exploratory fit.

```sh
cmake -S . -B build/eco-guide -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/eco-guide --target TubeChannelEcoCapture TubeChannelEcoCompare TubeChannelEcoListen TubeChannelEcoTest TubeChannelChainBench TubeChannelChainProfile

build/eco-guide/TubeChannelEcoCapture Netlists build/eco-guide/captures
python3 tools/fit_eco.py build/eco-guide/captures build/eco-guide/EcoCalibration.candidate.h build/eco-guide/fit.json

build/eco-guide/TubeChannelEcoCompare Netlists build/eco-guide/holdout.csv
build/eco-guide/TubeChannelEcoListen Netlists build/eco-guide/listening

ctest --test-dir build/eco-guide -R '^eco$' --output-on-failure
```

Python fitting requires NumPy. Capture is expensive because it executes the original
circuits offline. Retain logs as well as the binary capture data.

**Important:** compare/listen/test executables compile the currently included
calibration header. Generating `EcoCalibration.candidate.h` does not switch them to
that fit. Review the candidate, install it in a dedicated experimental checkout,
rebuild, and then run fresh validation before promoting it.

For the complete current test suite, build all test executables before running
CTest. Building only the targets above does not produce the processor/GUI tests:

```sh
cmake --build build/eco-guide
ctest --test-dir build/eco-guide --output-on-failure
```

For a historical benchmark, first regenerate the pinned reference with the documented
measurement-only harness extension. Supply the resulting matched binaries:

```sh
python3 tools/benchmark_eco.py /absolute/path/to/pinned-reference/TubeChannelChainBench /absolute/path/to/candidate/TubeChannelChainBench build/eco-guide/benchmark
```

The script uses Default index 0 and Eco index 3. Current independent oversampling
controls and later changes must be checked against the historical configuration
before treating a new run as a reproduction. Do not compare Debug against Release,
native ARM against Rosetta, or a new oversampled Eco path against the old 1x claim.

## 15. Package the exact tested implementation

The delivery process built all shipped formats and architectures, verified their
versions and signatures, inspected the installer payload and license inventory,
then signed, notarized, stapled and checked Gatekeeper acceptance. Release uploads
included the installer, checksum and build manifest. Uploaded asset digests were
compared with local files.

For another project, keep these boundaries explicit:

1. Freeze source, generated calibration and dependency revisions after validation.
2. Build the release configurations and architecture slices actually distributed.
3. Confirm the packaged binaries and docs are the intended ones, including applicable
   project/SDK/dependency license files.
4. Sign and notarize the final payload. Compute the public installer checksum
   **after stapling**, because attaching a ticket changes the package bytes.
5. Publish source/tag, release notes, measurements, images, manifest and binaries.
6. Verify remote asset checksums and retain the source-to-package evidence.

A signature confirms provenance, not sound quality. A successful installer does not
close a missing listening or DAW test. The release notes must state material limits.

## 16. Handoff checklist and division of responsibility

Assign owners even if one engineer fills several roles. Avoid making the final
sound decision implicitly through a performance-only review.

| Owner | Required handoff |
|---|---|
| DSP/modeling | Stage diagram, control domains, capture schema, fitter, generated data, failure cases |
| Measurement | Pinned reference, harness patch, binary hashes, raw timings and sound metrics |
| Plugin/host | Parameter migration, preparation/retirement lifecycle, routing, latency and host matrix |
| Sound acceptance | Source-material list, normalization rules, listening decisions and rejected domains |
| Release | Source/dependency manifest, package inventory, licenses, signatures and final checksums |

Before declaring the work complete, the next team should be able to answer:

- Which exact reference mode does each claim compare against?
- Which behavior is measured, approximated, intentionally changed or unverified?
- Can the captures and generated coefficients be regenerated?
- Are reference convergence failures visible and bounded?
- Do fresh fixtures cover interpolation, overload and dynamic recovery?
- Are old projects and automation preserved?
- Are callback allocation, destruction, deadlines and latency verified?
- Did the intended machine and real DAWs pass the declared workload?
- Who accepted the listening tradeoff, and for which domain?
- Do the published binary, checksum, tag and manifest describe the same release?

Tube Channel's retained records support a strong architectural speedup and extensive
engineering checks. They also explicitly leave program-material listening acceptance
and actual multi-instance DAW playback unverified. A future team should close those
gates rather than copy the omission into its definition of done.

## 17. Reusable brief for the next project

Copy this into the next project's engineering plan and replace every placeholder:

```text
Product and owner:
Reference source, mode and executable hash:
Target platforms, machines, formats and architectures:
Supported rates, buffers, routing and control domain:

Behavior to retain:
Allowed sound differences:
Required dynamic states and loading interactions:
Original engine retention/fallback policy:

Minimum speedup:
Absolute CPU and worst-callback budgets:
Declared workload matrix:
Noise/tolerance/seed/supply/oversampling settings:

Capture schema and reference-validity rules:
Identification/development/final-holdout separation:
Linear, harmonic, IMD, aliasing and recovery thresholds:
Listening material, normalization policy and acceptance owner:

Parameter/state migration and old-session requirements:
Preparation, state retirement and realtime allocation rules:
Latency and offline-render policy:
Target-host and multi-instance test procedure:

Evidence directory and reproduction commands:
Package manifest, licenses and signing requirements:
Known limitations and release acceptance decision:
```

## Repository reading map

| Resource | Purpose |
|---|---|
| [Development plan](SOUND_MODEL_PLAN.md) | Revised brief, budgets and intended gates |
| [Baseline checkpoint](measurements/reduced-engine-1.2.0/README.md) | Frozen reference and measurement setup |
| [Eco measurement report](measurements/eco-1.2.0/README.md) | Results, raw evidence links and sound limits |
| [Capture tool](../tests/eco_capture.cpp) | Offline circuit fixtures and validity flags |
| [Fitter](../tools/fit_eco.py) | Stable filters, EQ topology and nonlinear curves |
| [Generated calibration](../Source/Hybrid/EcoCalibration.h) | Shipped measured profiles |
| [Eco runtime](../Source/Hybrid/EcoModel.h) | Fixed-cost DSP, ADAA, loading and state cadence |
| [Chain integration](../Source/Plugin/Chain.h) | Engine selection, oversampling, routing and telemetry |
| [Engineering regression](../tests/eco_test.cpp) | Partition, finite, routing and automation checks |
| [Full-chain holdout tool](../tests/eco_compare.cpp) | Fresh fixtures and objective differences |
| [Listening generator](../tests/eco_listen.cpp) | Synthetic review renders |
| [Benchmark driver](../tools/benchmark_eco.py) | Alternating matched workloads and raw logs |
| [1.2.0 release evidence](measurements/release-1.2.0/README.md) | Universal packaging and release validation |
| [1.2.1 offline-bounce evidence](measurements/offline-bounce-1.2.1/README.md) | Separate rendering engines, lifecycle and latency |
| [User guide](USER_MANUAL.md) | Product behavior and control semantics |
