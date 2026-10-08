# Plugin Doctor 2.7.2 baseline measurements

Comparison: released v1.0.0 in chain 1 and the local solver-fixed v1.1.1 build in chain 2.
Source version is now 1.1.2; a hybrid engine has not been implemented or measured.

Analyzer: 48 kHz, Higher FFT (32,768), Realtime speed, parallel LR. Both plugins used Init,
Default 2x oversampling, tolerance and supply simulation On, seed 1, alignment -18 dBFS,
and noise explicitly Off. See ../../PLUGIN_DOCTOR_PROTOCOL.md for executable identities
and export mapping verification.

## Captured results

- Linear delta at -30 dBFS: maximum ordinate difference 0.09738 dB at 120.1245 Hz.
  Independent supply phases confound mains-related bins; this is not a clean measure of
  solver-induced frequency-response change.
- Settled 999 Hz harmonic test at -18 dBFS: both displayed THD -39.8 dB and THD+N -39.9 dB.
  The first two harmonic peaks matched to the export precision; the third differed by 0.00001 dB.
- Default attack/release: 3 seconds at -36 dBFS, 2 seconds at -6 dBFS, 6 seconds at -36 dBFS.
  Visually overlapping waveforms were captured. The compressed raw export contains four
  528,000-point segments. Quantitative envelope comparison and channel mapping remain pending.
- Performance export was inconsistent and remains provisional; it supports no CPU-saving claim.

linear.csv and harmonic.csv retain the frequency-bin exports and their differences.
summary.json records numerical results and outstanding measurements.
dynamics-default.txt.gz preserves the raw dynamics export without private license information.

## Outstanding tests

After reopening and restarting the analyzer, the order-six Hammerstein test completed at
-18 dBFS, Normal FFT quality and Realtime speed. Twelve 4,094-point kernel segments were
exported to hammerstein-order6-complete.txt. The overlay appears similar; the export mapping
has not been separately established for this mode, so no numerical inter-version kernel
error is reported.

Separate-chain 2D sweeps were captured in 2d-reference.png and 2d-candidate.png at -18 dBFS,
10–24,000 Hz, exponential sweep, Normal FFT, Realtime, and a -200 to 0 dB color scale.
Both show reflected harmonic traces with similar overall patterns. The analyzer's frequency
axis ends at 22.1 kHz despite the settings displaying 48 kHz: resolve that discrepancy before
claiming exact alias frequencies or levels. The images establish a visual baseline, not
an audibility threshold or an anti-aliasing pass.

Both 1D sweep displays were captured at -18 dBFS, Normal FFT and Ultra speed, with 83
frequency points spanning approximately 5.86 Hz–21.90 kHz. thd-sweep.txt and
fundamental-sweep.txt retain the exports. Comparing aligned finite segment 1/3 ordinates
between 20 Hz and 20 kHz gives maximum differences of 0.00005 dB for THD and 0.00087 dB
for fundamental. Segment identities follow the verified frequency-export convention;
mode-specific mapping still needs independent confirmation. Non-finite THD and -500
sentinel values are excluded, and extra segments are preserved without assuming validity.
These small differences are baseline diagnostics, not a sound-similarity percentage.

Maximum attack/Auto release, export mapping checks, and reliable callback-performance
checks remain pending.

Hammerstein is a diagnostic of nonlinear frequency response; it does not establish accuracy
for a stateful feedback compressor. These measurements are a baseline, not evidence that a
future simplified engine preserves a percentage of the sound or meets the CPU reduction target.
