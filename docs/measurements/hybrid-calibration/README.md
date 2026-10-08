# Hybrid calibration iterations — v1.1.2

Historical measurements before the quality-mode change. The user subsequently selected the prototype
for Default and the original for HQ/Derp. These recorded binaries and prototype fidelity findings
are preserved; see [current quality mapping](../quality-modes/README.md).

These are development measurements, not a release certification or a percentage of sound quality.
The standard plugin retains the circuit engine. The separate prototype replaces only the detector;
its supply, preamp, tube/warm feedback, compressor audio path, EQ and output remain circuits.
Plugin identities are recorded in `plugin-identity.json`. Neither build is a notarized installer.

## Detector

Eight fits were explored. Early rounds used a rectified envelope with progressively adjusted timing;
rounds 7–8 use two directional charging paths, their shared attack resistance, one timing capacitor,
and a release network. Four precomputed scalar solves replace matrix/Newton iteration. Auto uses a
continuously connected slow charging path and disconnects manual timing controls. Both modes keep
capacitor state across control changes and feed the unchanged optical model.

The final round-8 defaults and named parameters are in `detector-fit.json`. Earlier numbered parameter
files belong to earlier schemas and are historical diagnostic records, not profiles for the current
source. Round 7 has 14 parameters; round 8 has 16. The current reader rejects extra/truncated values.

Reference captures solve the guarded circuit at fixed nominal rails with tolerances off. Training
`train4` has 72 two-second fixtures at 48 kHz across threshold, ratio, attack/release and Auto/manual.
Fitting minimizes optical GR RMS; it does not directly optimize the largest transient error. Final
validation uses the production optical calculation. GR here uses a 14.7 kOhm divider; full-channel
telemetry uses the panel's calibration setting and is reported separately.

Round 8 training RMS is 0.997353 dB. The fresh `holdout6` set has 24 six-second two-tone burst/recovery
fixtures at 44.1/48 kHz, different frequencies, drive levels and controls. It was not used in fitting.
Aggregate optical GR error is 0.835832 dB RMS, with a 7.5034 dB maximum. This fails a close transient
match. Round-7/holdout5 also failed (0.877435 dB RMS, 6.1774 dB maximum). Some validation sets from older
rounds informed later model changes and are development data rather than fresh holdouts.

`chain-comparison.txt` checks the actual feedback loop, stereo link, original supply, tolerances,
control smoothing and default oversampling at 48 kHz/1024 samples, noise off. It runs each reference
and candidate sequentially with the same seed and inputs. No delay or level normalization is applied.
All output is finite. Candidate-minus-reference waveform errors range from -17.09 to -31.52 dB relative
RMS. GR telemetry RMS error is 0.604 dB for quiet/default, 1.382 dB for maximum manual timing,
0.166 dB for Auto bursts and 0.293 dB for external sidechain/maximum timing. These fixtures do not
establish that playback is free of crackles or that multiple instances meet host deadlines.

Reproduce the current detector capture/fit/validation with:

```sh
build/hybrid/TubeChannelHybridCalibrate capture Netlists /tmp/train.bin train4
build/hybrid/TubeChannelHybridCalibrate fit /tmp/train.bin /tmp/fit.txt
build/hybrid/TubeChannelHybridCalibrate capture Netlists /tmp/validation.bin holdout6
build/hybrid/TubeChannelHybridCalibrate evaluate /tmp/validation.bin /tmp/fit.txt /tmp/curves.csv
build/hybrid/TubeChannelHybridChainCompare Netlists
```

Capture inputs are synthetic. Temporary binary datasets use a native local format and are regenerated
by the source; CSV validation and fixture settings are retained here. `evaluate` and chain comparison
return success for a completed finite computation, not for passing a sound-match threshold.

## EQ/output

`linear-fit.json` fits stable sections from independent stage impulses at 96 kHz, default EQ/master,
nominal components, fixed settled rails and 0.001 V. Captures use each op-amp output actually passed
to the next stage. The generated `DefaultLinearCalibration.h` is a standalone profile, not an enabled
channel replacement. Individual 20 Hz–20 kHz response errors are below 0.011 dB and 0.061 degrees.

The separate waveform test uses 37/997/4001/14003 Hz bursts at 0.25 or 2 V. EQ plus output residuals
are -49.05 to -84.69 dB relative RMS and pass the -35 dB gate. A 17 V overload gives about -15.85 dB,
which is intentionally reported as a failed approximation domain rather than accepted linear behavior.
Other controls, rates, component tolerances, EQ bypass, output trims and overload memory need profiles
or retained nonlinear states before these filters can replace circuits.

## Supply

`supply-reference.csv.gz` contains an eight-second, 8 kHz nominal-circuit load-step capture at 60 Hz.
The offline fit uses a one-pole load response plus eight mains-frequency Fourier harmonics. Training
ends at 4.5 seconds; later load steps are validation. Half-wave multiplier ripple requires the mains
fundamental, so the original twice-mains-only basis was rejected.

`supply-fit.json` still misses the high/plate rails by about 1.51 V RMS, with up to 2.63 V peak error.
These free-intercept offline fits are not validation of the anchored C++ supply module. They were
not injected into it or enabled in the candidate plugin. Further work needs hidden charge states,
correct initialization and load/plate/mains coverage; a single sag pole is insufficient here.

## Engineering checks

`engine-tests-final.txt` records all 10 native checks passing after the final detector calibration,
including no-allocation processing, hybrid sidechain, stereo lockstep, diode guard, passive detector
recovery, Auto controls, overload bounds and the limited-domain linear profiles. Host processor and
GUI results are recorded separately for the standard and prototype builds. Passing these engineering
checks does not override the failed detector fidelity checks.

## Complete-channel CPU

Six sequential alternating runs, stereo/48 kHz/1024 samples/Default 2x, original supply and tolerances on, maximum manual timing and 0.8-peak bursts: reference mean 13.482 ms versus candidate 11.123 ms CPU per block, a 17.5% reduction. The block deadline is 21.333 ms. The last candidate run missed two of 349 measured wall-time deadlines (worst 39.50 ms); other runs missed none. Process CPU excludes descheduled time. This does not prove multi-instance host reliability, and changed compression/loading means these are not sound-matched savings. The 80% target remains unachieved.

## Plugin Doctor 2.7.2

The rebuilt reference is Chain 1 and the detector-only candidate is Chain 2, loaded by their exact
paths in `plugin-identity.json`. Both use Init, Default oversampling, nominal seed 1, tolerances and
original supply on, stereo link on, noise off, -18 dBFS alignment and zero trims. Analyzer settings
are 48 kHz, parallel `1|2`, LR and -18 dBFS drive. Higher FFT (32768) and realtime speed are used for
the 999 Hz harmonic snapshot. Hammerstein order 6 and separate identified 2D sweeps use Normal
FFT (16384) and realtime speed. Full 1D THD/fundamental sweeps use Normal/Ultra for offline analysis.
Analyzer performance is not used as the CPU benchmark.

The harmonic snapshot has a reference fundamental of -20.32596 dBFS versus -19.38403 dBFS for the
candidate: +0.94193 dB. The second harmonic relative to the fundamental differs by +0.94393 dB.
The fundamental frequency sweep confirms about +0.9424 dB near 1 kHz. These gains are not normalized
away. The native closed-loop checks and actual plugin measurements both reject a close manual-mode
match. Hammerstein order-6 data/plot and separate reference/candidate 2D images are retained; their
higher-order and folded traces also differ. This work preserves existing oversampling and does not
claim improved alias rejection.

A Chain-2 bypass capture independently verifies the harmonic file's segment order: segments 1/2
retain the processed reference, while 3/4 become the -18 dBFS bypass sine with the second harmonic
below -202 dBFS. The export omits headers for some segments; preserve this check rather than inferring
identity from the incomplete headers alone. Fundamental exports have four complete 83-point segments.
The 1D THD view stayed blank although left-channel sweep data was exported; the other two segments
have a short, inconsistent frequency grid and are not accepted as THD curves. High-frequency `-inf`
THD values occur where no higher harmonics remain below Nyquist, not from nonfinite plugin audio.

The 2D spectral axis still ends at 22.1 kHz despite 48 kHz settings and 10–24000 Hz input sweep;
this analyzer-display discrepancy remains unresolved. Do not use these images for an absolute
Nyquist/alias-rejection certification. Single-frequency FFT exports extend to 24 kHz.
Native text exports were normalized from literal escaped line separators into ordinary newlines;
curve numbers are unchanged. Native camera exports contain the analysis pane only, without the
license footer or unrelated plugin library. Plugin Doctor is left in Higher/realtime single-sine
mode, parallel/LR, both plugins restored from the temporary bypass, for review.

## Remaining work

At the time of these measurements the detector was not promoted: its manual charging/release and frontend saturation need a better
reduction, with independent long/transient validation after each change. Keep the original supply
until a stateful replacement passes load/rail tests. EQ/output require control/rate/overload coverage
before integration. Multi-instance OpenGrid playback and level-matched listening remain necessary;
these measurements neither prove that the original crackles are fixed nor establish 90% of its sound.
