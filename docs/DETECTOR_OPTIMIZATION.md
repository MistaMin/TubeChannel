# v1.1.1 detector investigation

## What measurements can establish

A measured model can run at the same sample times as the reference without producing the same
sample values. Finite measurements alone cannot establish equivalence for all signals, histories,
control settings, component tolerances and supply conditions. A fitted envelope, lookup table or
neural model is an approximation unless a separate equivalence argument is available.

The current detector is already a discrete-time algorithm, built from circuit equations. It runs
at approximately 48 kHz using averaged oversampled inputs. Its result drives a separate stateful
gain-control element in a feedback compressor. The detector's own supply loads also feed the power
supply model. Matching only its steady-state compression curve would leave transient behavior and
power-supply interactions unverified.

Keep the existing equations, integration method, input averaging, control smoothing, component
tolerances and convergence criteria. The safeguarded Newton work below changes iteration trajectories
and avoids unnecessary recovery; the existing damped and half-step fallback remains available.
Do not replace the production detector with a fitted surrogate.

## Reference capture

`TubeChannelDetectorCapture` is an offline tool; it does not change plugin processing. Build it in
Release mode, then run:

```sh
build/detector-study/TubeChannelDetectorCapture Netlists reference.csv 48000
```

A final argument of `1` selects the experimental reduced solver; `0` selects the original.
Use `-` instead of an output path to omit CSV output. Compare both paths with:

```sh
python3 tests/detector_compare.py build/detector-study/TubeChannelDetectorCapture Netlists
```

The comparison reports sample errors and solver-status differences, then alternates timing order
and reports per-case median wall time. It does not certify equivalence against a perceptual threshold.

The CSV records input voltage, LED current, timing voltage, both rail currents, op-amp supply load,
final-solve iteration count, factorization count, damping retries, half-step rescues and convergence
for each sample. Output is written after each scenario, outside the measured processing interval.
Timing includes stimulus generation, measurement and sample storage; it is a scalar diagnostic,
not a stereo plugin CPU estimate. Final-solve iterations exclude earlier failed solve attempts;
retry and rescue columns must also be examined.

Each case begins from its own DC operating point and runs for 1.5 seconds: silence, a quiet sine,
a loud sine burst, release into silence, and a two-tone signal. The cases cover nominal manual mode,
automatic mode, short attack/release, and a fixed tolerance seed with threshold/ratio and supply
changes halfway through. Parameters otherwise retain the detector netlist defaults. The tool's
rate is the detector rate, not the host or oversampled audio rate.

Cases 4–6 additionally cover maximum attack resistance, maximum release position, and both at
maximum. The original four-case measurements below predate these additions.

This is an initial reference set, not a complete training corpus or proof of equivalence. Extend
coverage to impulses, random signals, full control sweeps, longer releases, multiple seeds and
actual detector inputs recorded from the complete chain. Final validation must include closed-loop
audio, external sidechain, stereo link, power-supply feedback and automation.

### Initial measurements

The four cases passed at detector rates of 44.1, 48 and 96 kHz: 1,128,600 samples in total, with
finite recorded outputs and no reported final convergence failures. The prepared detector has 35
unknowns. At 48 kHz, final-solve iterations averaged 4.31–4.91 per sample and factorizations averaged
3.74–4.48. Damping retries occurred on 3,655–4,813 of the 72,000 steps per case (about 5–7%); the
short attack/release case also required nine half-step rescues.

These results make recovery behavior a measurement priority alongside equation reduction. They
do not justify relaxing solver criteria. The 96 kHz run is a diagnostic of a different detector
timestep, not a proposal to change the production detector's cadence.

## Candidate for preserving the equations

Investigate eliminating linear unknowns from the detector's equation system, leaving a smaller
nonlinear solve and reconstructing the required voltages and supply currents. Precompute coefficients
that remain constant between control updates. Preserve all dynamic state rather than replacing it
with a single attack/release envelope.

Exact decomposition is an established circuit-simulation technique; applicability depends on the
actual coupling between nonlinear elements. See
[Automatic Decomposition of Non-linear Equation Systems in Audio Effect Circuit Simulation](https://www.dafx.de/paper-archive/2017/papers/DAFx17_paper_29.pdf).
Whether this detector admits a useful reduction remains to be demonstrated. Algebraic equivalence
also does not imply bit-identical floating-point output: changed operation ordering and convergence
paths require sample-by-sample comparison against the existing implementation.

## Experimental implementation

`tests/DetectorReduction.h` implements a Schur-complement solve. The full Newton linearization is
partitioned into linear-only voltages `y` and voltages touching nonlinear devices `z`:

```text
A y + B z = p
C y + D z = q

W = solve(A, B)
v = solve(A, p)
(D - C W) z = q - C v
y = v - W z
```

`p` and `q` include the current input and all original capacitor/op-amp history terms. `D` and `q`
include the existing device linearization; they are not fitted functions. `W` and the correction
`C W` are rebuilt when the base matrix changes, including control updates and recovery timesteps.
Sparse structural closure includes all couplings introduced by eliminating a linear component.
Exactly zero reconstruction coefficients are omitted without thresholding small coefficients.

For this detector the partition eliminates 10 linear-only voltages, leaving 25 retained unknowns.
The reduced matrix has 104 registered entries versus 130 in the original. All original voltages are
reconstructed before the existing convergence test and state update. Device equations, tolerances,
iteration limits, limiting, damping and half-step recovery remain in the original Circuit code.

The experiment uses hooks compiled only into `TubeChannelDetectorCapture` with
`TC_DETECTOR_EXPERIMENT`. Normal plugin and test targets do not contain these hooks. It is specific
to this detector; it does not support the production low-rank resistor fast path or paired solver.
No reduced detector is enabled in plugin processing. Keep it experimental until speed and complete
closed-loop output comparisons justify adoption.

### First comparison at 48 kHz

Across 288,000 samples, LED-current relative RMS error was about -172.62 dB, with maximum absolute
error 1.63e-11 A. Timing-voltage relative RMS error was about -172.62 dB, with maximum absolute
error 4.88e-9 V. Positive rail current matched exactly in these cases; negative rail current and
op-amp load differed slightly. These are detector quantities, not a full audio null measurement.
2,057 sample rows had at least one different iteration/factor/retry/rescue/convergence field.
Therefore the implementation is not bit-identical even though the equations are algebraically
equivalent and the measured differences are very small.

Five alternating timing runs per implementation gave median scalar wall-time changes of -1.3%,
-1.3%, -2.0% and -0.6% for cases 0–3. This is too small to establish a robust CPU advantage and
does not justify enabling the replacement in the plugin. The original sparse factorization already
eliminates linear nodes efficiently; extra reduction/reconstruction work consumes most of the gain.
Further investigation should measure device evaluation and recovery costs, and consider a reduction
in nonlinear device coordinates rather than only linear-only nodal voltages.

The same comparison at 44.1 and 96 kHz brought total compared samples to 1,128,600; both paths
completed all cases without reported convergence failures. LED-current relative RMS errors were
about -172.63 and -172.79 dB respectively. Single paired timing runs at those rates are diagnostics
only and are insufficient for a performance conclusion. Fresh production-path `circuit`, `lockstep`
and `realtime` tests passed with the experimental hooks compiled out.

## Slow attack/release crackling investigation

The reported symptom is crackling/breakup rather than intentional transient overshoot. At a 48 kHz
detector rate, each added slow-setting case ran 72,000 samples through both original and reduced
solvers without final convergence failures or half-step rescues. Original final-solve iterations
averaged 4.19–4.29 per sample, versus 4.36 for nominal manual mode. Damping retries were similar
(3,654–3,660 versus 3,655). These open-loop workloads do not show a special slow-setting solver
failure; they cannot rule out a failure on other signals or in host playback.

The production stereo chain at Default/48 kHz and a 256-sample buffer used about 75–76% of the
deadline as process CPU with either default or maximum attack/release. For a louder burst workload
at 128 samples, the maximum-setting run missed 7 of 1,149 measured wall-time deadlines, despite
finite output and output peak below full scale. Average process CPU was about 66% of the deadline.
Wall-time misses include scheduling effects and do not identify the cause of a particular DAW
dropout. The experiment demonstrates limited headroom; it does not prove that slow controls are
responsible for the user's crackling. Exact host settings and workload are still needed to reproduce it.

A matching 128-sample burst run at default settings missed 22 of 1,149 deadlines; repeating the slow
settings missed none. Average process CPU remained about 63–66% of the deadline. This variation
prevents attributing the misses to slow controls. The user subsequently confirmed a 48 kHz session
and one instance, naming OpenGrid; buffer size, standalone versus hosted operation and tested build
version remain to be confirmed.

The user then specified a 1,024-sample buffer and the v1 release. At 48 kHz the callback deadline
is 21.333 ms. The newer production chain's maximum-setting burst run missed 2/349 deadlines
(average process CPU 13.497 ms); default settings missed 1/349 (13.376 ms). A saved v1.0.0 source
copy compiled with the same benchmark workload missed 2/349 in Default (12.881 ms average process
CPU), and 236/349 in HQ (22.417 ms average process CPU). These are individual runs, not a controlled
speed comparison between releases. The HQ workload exceeds its deadline on average and is a clear
CPU overload case. Default's occasional wall-time outliers could also cause dropouts but do not
establish the cause of the reported sound. The selected quality and exact hosted/standalone setup
are still needed; no production sound behavior has been changed to mask the symptom.

### Recorded-audio follow-up

A recovered host session state provided a more representative workload: mono, Default, automatic
detector mode, maximum attack knob, release position about 0.154, minimum threshold/ratio knobs,
and input gain about +18.36 dB. A 30-second recorded-vocal excerpt was replayed through temporary
instrumented copies of v1.0.0 and the current production path. These used a fresh startup state and
an assumed stereo-to-mono average rather than the complete host routing and previous state history.
The two paths produced bit-identical output across 1,440,000 samples. Audio-stage solves converged
and output did not clip, but each path recorded 38,224 detector damping retries.

A separate v1.0.0 replay with per-block process CPU logging found 66 of 1,407 callbacks exceeding
the 21.333 ms deadline in process CPU alone. CPU p99 was 27.956 ms and worst CPU 34.332 ms.
CPU-late blocks averaged about 225 detector retries versus 17 in CPU-on-time blocks; per-block
CPU and retry counts had correlation 0.935. This implicates recovery work in expensive blocks
even when the eventual solve succeeds. It does not prove the cause of every live-host click.
Power-supply solve failures were not counted by this diagnostic. Focus subsequent optimization
on the detector's recovery triggers and worst-case cost using recorded inputs, not only steady
tones or the average cost of factorization. Private session state and audio remain outside the repo.


## v1.1.1 safeguarded detector Newton solve

The production detector now limits the proposed Newton direction using the existing pn-junction
voltage limiter. One scale applies to the complete update vector, preserving the direction of the
coupled update. Guards cover forward diode conduction, zener reverse breakdown,
transistor base-emitter/base-collector junctions, and both input-protection polarities of each op-amp.
These are iteration safeguards, not clipping of the audio or replacements for nonlinear equations.

The previous solver could reject a very large proposed solution before a safeguard had a chance to
reduce it. The guarded path rejects nonfinite proposals immediately, applies junction limiting, then
checks an unlimited proposal against the existing voltage bound. A limited update cannot count as
converged. It gets up to 50 iterations before the original damped/half-step fallback, rather than
20 unguarded iterations. Other audio stages retain their existing iteration settings. Scalar and
paired stereo paths apply the same guards independently per channel. No additional processing
threads, audio allocations, oversampling changes or fitted models are introduced.

`detector_guard` exercises 24 cases across 44.1, 48 and 96 kHz, two different input channels,
manual/Auto timing, shortest/maximum attack, release extremes, two tolerance seeds, control and
supply changes, and abrupt bursts. It compares guarded scalar solves to paired solves and to a
reference using the same equations with a 1e-9 stopping tolerance and tighter Jacobian reuse.
All guarded paths and tighter references converged; scalar-to-paired differences were below
-164 dB and differences against tighter references below -164 dB for LED/timing quantities and
below -190 dB for op-amp supply load in this run.

The same workload exposed 201 unsuccessful legacy scalar steps and 47,521 damping retries,
compared with zero guarded retries. Where legacy solves fail or resort to half-step integration,
their trajectory is not an accuracy reference for a successful full-step solve. The test reports
those differences explicitly and uses the tighter successful solver for its accuracy gate.
This fixes numerical behavior in stressed cases; it cannot promise bit identity with a failed
legacy solution. Closed-loop audio comparisons and host deadline checks remain necessary.


### Closed-loop recording replay

A fresh-start replay of the same private 30-second mono excerpt and recovered controls at Default,
48 kHz/1,024 samples recorded zero detector retries, zero half-step rescues and zero unsuccessful
audio-stage solves. In sequential runs without a concurrent build/test, average process CPU fell
from 60.9% to 56.7% of real time; CPU p99 fell from 26.649 to 18.919 ms, and worst CPU from
32.927 to 20.907 ms. CPU deadline misses fell from 58/1,407 to zero. Wall-time misses fell from
66 to three; scheduling still matters. These are individual runs, not a universal performance guarantee.
The final output null was about -177.28 dB relative RMS across 1,440,000 floats, with maximum
absolute difference 7.45e-9, finite output and no clipping.

A 10-second stereo replay with a second input at 70% of the first remained too expensive:
112.5% average process CPU for v1.0.0 versus 104.9% for the guarded version, with 329 versus
312 CPU-late callbacks out of 469. Worst CPU fell from 60.603 to 36.486 ms and detector retries
from 20,744 to zero. The stereo output null was about -143.31 dB relative RMS. This synthetic
second channel is a diagnostic, not the recovered mono host routing. The result rules out calling
this a complete multi-instance performance fix. Host playback and larger average CPU reductions
remain outstanding; Default uses IIR oversampling filtering, so removing FIR convolution does not
address this workload.

A further experiment stopped each paired lane after convergence. It gave no useful measured CPU
benefit on this stereo excerpt and increased the difference against v1.0.0 to about -127 dB.
It was rejected and is not in the production code.


### Final build checks

The arm64 Release build completed for v1.1.1 and all ten CTest checks passed, including circuit,
stereo lockstep, sidechain, limiter, realtime allocation checks, the new detector guard, processor
state/quality stress and GUI tests. The VST3 bundle reports 1.1.1. This build has not been installed
into the host or submitted for signing/notarization; the universal configuration was not rebuilt
in this pass.

A separate Default stereo steady-tone benchmark at 48 kHz/256 samples measured 74.0% average
process CPU for v1.1.1 versus 71.6% for the v1.0.0 source, in sequential individual runs. Wall-time
misses were zero versus two out of 749 blocks. This workload shows no average CPU improvement;
extra safeguards have a cost when the legacy path is already behaving well. The improvement is
convergence and worst-case detector recovery on the stressed workloads above, not a blanket
reduction in CPU on every signal. Larger average reductions remain necessary for multiple channels.
